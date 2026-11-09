
/**
 ******************************************************************************
 * @file           : lifter.c
 * @author         : [Your Name/Team]
 * @version        : V1.2
 * @date           : 2026-03-11
 * @brief          : 割草机刀盘高度调节与草袋开合控制状态机
 *
 * 主要功能：
 * 1. 刀盘高度：通过时间积分估算电机位置，具备开机归位与掉电保存功能。
 * 2. 草袋开合：单键切换 (Toggle)，采用超时保护机制 (无物理限位) 开合。
 * 3. 延时持久化 (Lazy Save)：高度调节停止后 10 秒才写入 Flash，极大延长寿命。
 ******************************************************************************
 */

#include "lifter.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "grass_bag_control.h"
#include "remote_backend_config.h"
#include "EG2104.h"
#include "tim.h"
#include "serial_port.h"
#include "flash_storage.h"
#include "connet_check.h"
#include "math.h"
#include "app_context.h"
#include "../../../experiments/ticket03/height_feedback.h"
// =================================================================
// 1. 硬件定义
// =================================================================
EG2104_HBridge_t blade_height_bridge = {
    .left_timer = &htim3,
    .right_timer = &htim3,
    .left_in_channel = TIM_CHANNEL_3,
    .right_in_channel = TIM_CHANNEL_4,
};
EG2104_HBridge_t grass_bag_bridge = {
    .left_timer = &htim2,
    .right_timer = &htim2,
    .left_in_channel = TIM_CHANNEL_4,
    .right_in_channel = TIM_CHANNEL_3,
};

// =================================================================
// 2. 核心参数与数据表
// =================================================================
// 推杆10kHz/60%PWM版：48V母线下约为28.8V平均输出，脉冲幅度仍接近母线电压。
// 高度时间表、归位时间与草袋动作时长暂沿用旧值，待10kHz/60%PWM运行实测后重新标定。
// --- 刀盘高度参数 ---
#define LIFTER_SPEED 60.0f      // 刀盘升降PWM占空比 (%)
#define HOMING_TIMEOUT_MS 10000U // 每次最高档归位完整上升10秒
#define DEAD_BAND_MS 100        // 死区：误差小于100ms(约2mm)认为到位

// 上升速度补偿系数 (0.0 ~ 1.0)
// 假设上升比下降慢 (克服重力)。如果下降用6s，上升用8s，系数约为 0.75
// 如果你发现上升总是“不到位”，请调小这个值；如果上升“过头”，调大这个值。
#define UP_SPEED_FACTOR 0.80f

// --- 草袋开合参数 ---
#define GRASS_BAG_SPEED 60.0f      // 草袋开合PWM占空比 (%)
/* Run time is shared with the deterministic grass bag actuator model. */

// --- 【核心】实测数据点 (时间 ms -> 高度 cm) ---
// 基于你提供的数据：0s=11.3, 1s=10.0, 2s=7.2, 3s=5.7, 4s=4.4, 5s=3.2, 6s=2.1
typedef struct
{
    uint32_t time_ms;
    float height_cm;
} HeightMap_t;

static const HeightMap_t HEIGHT_MAP[] = {
    {0, 11.0f},   // 0ms = 5档 = 11cm
    {2800, 9.0f}, // 2800 = 4档 = 9cm
    {4500, 7.0f}, // 4500 = 3档 = 7cm
    {6500, 5.0f}, // 6500ms = 2档 = 5cm
    {9500, 2.0f}, // 9500 = 1档 = 2cm
    {10000, 1.0f} // 预留一个极限值，防止计算越界
};
#define MAP_SIZE (sizeof(HEIGHT_MAP) / sizeof(HeightMap_t))

// --- 【核心】档位目标时间表 (根据要求计算) ---
// 5档(11cm) -> 约 230ms
// 4档(9cm)  -> 约 1360ms (在10.0和7.2之间插值)
// 3档(7cm)  -> 约 2130ms (在7.2和5.7之间插值)
// 2档(5cm)  -> 约 3540ms (在5.7和4.4之间插值)
// 1档(2cm)  -> 约 6100ms (在2.1附近外推)
static const uint32_t LEVEL_TARGET_MS[6] = {
    0,    // 0档占位
    9500, // 1档: 目标 2cm (最低) - 大幅增加时间
    6500, // 2档: 目标 5cm
    4500, // 3档: 目标 7cm
    2800, // 4档: 目标 9cm
    0     // 5档: 目标 11cm (最高，基准点)
};

// =================================================================
// 3. 内部变量
// =================================================================
static Lifter_MachineState g_state = LIFTER_STATE_IDLE;
static uint32_t g_last_tick = 0;
static uint32_t g_homing_start_ts = 0;

/* Task-to-task mailbox. Only Lifter_Task_Update changes motion state/targets. */
typedef struct
{
    uint8_t level;
    bool target_pending;
    bool home_pending;
} LifterHeightRequest_t;

static LifterHeightRequest_t g_height_request;
static volatile uint16_t g_height_feedback_word; // atomic publication for Motion/SENS
static uint8_t g_last_requested_level; // 0: no command received since initialization

static float g_current_pos_ms = 0.0f;     // 当前虚拟位置 (ms)
static float g_target_pos_ms = 0.0f;      // 目标位置 (ms)
static float g_current_height_cm = 11.0f; // 对外显示的CM值

static int8_t g_dir = 0; // 1:Up(向0ms), -1:Down(向6000ms), 0:Stop

// === 延时保存 (Lazy Save) 变量 ===
#define FLASH_SAVE_DELAY_MS 10000        // 停稳后 10秒 (10000ms) 才保存 Flash
static uint8_t g_flash_save_pending = 0; // 0:无需保存, 1:等待保存中
static uint32_t g_flash_save_timer = 0;  // 记录停稳时的时间戳
static uint32_t g_dump_done_timer = 0;   // 用于倒草完成显示的计时

// --- 草袋状态变量 (【新增】) ---
static GrassBagActuator_t g_bag;
static osMutexId_t g_bag_mutex;
static bool g_bag_permitted = !MOWER_GRASS_BAG_CH6_ENABLED;

static bool GrassBag_Lock(void)
{
    return g_bag_mutex != NULL && osMutexAcquire(g_bag_mutex, osWaitForever) == osOK;
}

// =================================================================
// 4. 辅助函数
// =================================================================

void EG2104_HBridge_Stop(EG2104_HBridge_t *bridge);
HAL_StatusTypeDef EG2104_HBridge_SetSpeed(EG2104_HBridge_t *bridge, float speed);

static void Motor_Up(void)
{ // 物理向上 (高度增加，时间减小)
    Lifter_Stop(); // 先关闭两路PWM，再施加反向后的上升输出
    EG2104_HBridge_SetSpeed(&blade_height_bridge, -LIFTER_SPEED);
    g_dir = 1;
    g_flash_save_pending = 0; // 只要电机动了，就取消未执行的保存计划
}

static void Motor_Down(void)
{ // 物理向下 (高度减小，时间增加)
    Lifter_Stop(); // 换向前先关闭两路PWM
    EG2104_HBridge_SetSpeed(&blade_height_bridge, LIFTER_SPEED);
    g_dir = -1;
    g_flash_save_pending = 0; // 只要电机动了，就取消未执行的保存计划
}

void Lifter_Stop(void)
{
    EG2104_HBridge_Stop(&blade_height_bridge);
    g_dir = 0;
    // 停止时不改变 g_state，由调用者决定
}

// --- 草袋控制驱动 (【新增】) ---
static void GrassBag_Motor_Open(void)
{
    // 假设正转是打开草袋，如果实际相反，改为 -GRASS_BAG_SPEED 即可
    EG2104_HBridge_SetSpeed(&grass_bag_bridge, GRASS_BAG_SPEED);
}

static void GrassBag_Motor_Close(void)
{
    EG2104_HBridge_SetSpeed(&grass_bag_bridge, -GRASS_BAG_SPEED);
}

static void GrassBag_Motor_Stop(void)
{
    EG2104_HBridge_Stop(&grass_bag_bridge);
}

/**
 * @brief 根据当前毫秒数，反向插值计算厘米高度
 */
static void Update_Height_CM_From_MS(float current_ms)
{
    // 1. 边界处理
    if (current_ms <= 0)
    {
        g_current_height_cm = HEIGHT_MAP[0].height_cm;
        return;
    }
    if (current_ms >= HEIGHT_MAP[MAP_SIZE - 1].time_ms)
    {
        g_current_height_cm = HEIGHT_MAP[MAP_SIZE - 1].height_cm;
        return;
    }

    // 2. 线性插值
    for (uint32_t i = 0; i < MAP_SIZE - 1; i++)
    {
        if (current_ms >= HEIGHT_MAP[i].time_ms && current_ms < HEIGHT_MAP[i + 1].time_ms)
        {
            float t1 = (float)HEIGHT_MAP[i].time_ms;
            float t2 = (float)HEIGHT_MAP[i + 1].time_ms;
            float h1 = HEIGHT_MAP[i].height_cm;
            float h2 = HEIGHT_MAP[i + 1].height_cm;

            // 线性公式: y = y1 + (x - x1) * (y2 - y1) / (x2 - x1)
            float ratio = (current_ms - t1) / (t2 - t1);
            g_current_height_cm = h1 + ratio * (h2 - h1);
            return;
        }
    }
}

/**
 * @brief 反向插值：根据 CM 计算 MS (用于开机恢复位置)
 */
static float Get_MS_From_Height_CM(float height_cm)
{
    // 保护范围
    if (height_cm >= HEIGHT_MAP[0].height_cm)
        return 0.0f; // >11cm -> 0ms
    if (height_cm <= HEIGHT_MAP[MAP_SIZE - 1].height_cm)
        return (float)HEIGHT_MAP[MAP_SIZE - 1].time_ms;

    // 遍历查找区间 (注意高度是递减的)
    for (uint32_t i = 0; i < MAP_SIZE - 1; i++)
    {
        float h_high = HEIGHT_MAP[i].height_cm;    // 如 11.0
        float h_low = HEIGHT_MAP[i + 1].height_cm; // 如 9.0

        // 如果目标高度在这个区间内
        if (height_cm <= h_high && height_cm > h_low)
        {
            float t1 = (float)HEIGHT_MAP[i].time_ms;
            float t2 = (float)HEIGHT_MAP[i + 1].time_ms;

            // 线性插值
            float ratio = (h_high - height_cm) / (h_high - h_low);
            return t1 + ratio * (t2 - t1);
        }
    }
    return 0.0f;
}

// =================================================================
// 5. 接口实现
// =================================================================

void Lifter_Init(void)
{
    const osMutexAttr_t bag_mutex_attr = {
        .name = "GrassBag", .attr_bits = osMutexPrioInherit
    };
    g_bag_mutex = osMutexNew(&bag_mutex_attr);
    HAL_TIM_PWM_Start(blade_height_bridge.left_timer, blade_height_bridge.left_in_channel);
    HAL_TIM_PWM_Start(blade_height_bridge.right_timer, blade_height_bridge.right_in_channel);

    HAL_TIM_PWM_Start(grass_bag_bridge.left_timer, grass_bag_bridge.left_in_channel);
    HAL_TIM_PWM_Start(grass_bag_bridge.right_timer, grass_bag_bridge.right_in_channel);

    // 确保电机初始处于停止状态
    Lifter_Stop();
    GrassBag_Motor_Stop();

    taskENTER_CRITICAL();
    g_height_request = (LifterHeightRequest_t){0};
    g_last_requested_level = 0U;
    taskEXIT_CRITICAL();
    g_current_pos_ms = 0.0f;
    g_target_pos_ms = 0.0f;
    g_current_height_cm = HEIGHT_MAP[0].height_cm;
    g_homing_start_ts = 0U;
    g_height_feedback_word = 0U;
    g_flash_save_pending = 0U;

    // 初始化草袋状态为关闭
    g_bag = (GrassBagActuator_t){0};
    g_app.grass_bag_status = 0; // 全局标志位

    // 2. 尝试读取 Flash
    AppSettings_t settings;
    bool load_ok = FlashStorage_LoadSettings(&settings);

    // 3. 判断是否有效
    if (load_ok && settings.lifter_current_height_cm >= 1.0f && settings.lifter_current_height_cm <= 13.0f)
    {
        // === 成功读取：恢复状态，跳过归位 ===
        g_current_height_cm = settings.lifter_current_height_cm;
        // 反算当前的时间位置 (ms)
        g_current_pos_ms = Get_MS_From_Height_CM(g_current_height_cm);
        // 目标设为当前，保持静止
        g_target_pos_ms = g_current_pos_ms;

        g_state = LIFTER_STATE_IDLE; // 直接进入空闲
        // Serial_Printf(SERIAL_PORT_2, "[LIFTER] Loaded from Flash: %.1f cm (%.0f ms). Skip Homing.\r\n",
        //               g_current_height_cm, g_current_pos_ms);
    }
    else
    {
        // === 读取失败或非法：强制归位 ===
        g_state = LIFTER_STATE_HOMING_START;
        // Serial_Printf(SERIAL_PORT_2, "[LIFTER] No saved data. Force Homing...\r\n");
    }

    g_last_tick = HAL_GetTick();
}

/**
 * @brief 暴露给用户的强制重新初始化接口 (长按触发)
 * @note  无视 Flash 历史记录，强制推杆上升到最高点进行物理归零
 */
void Lifter_User_Init(void)
{
    taskENTER_CRITICAL();
    g_height_request.level = 5U;
    g_height_request.target_pending = true;
    g_height_request.home_pending = true;
    g_last_requested_level = 5U;
    taskEXIT_CRITICAL();
}

void Lifter_SetTargetLevel(uint8_t level)
{
    if (level < 1)
        level = 1;
    if (level > 5)
        level = 5;

    taskENTER_CRITICAL();
    if (level != 5U)
        g_height_request.home_pending = false; // 最新的普通档位取消尚未执行的归位请求
    else if (g_last_requested_level != 5U)
        g_height_request.home_pending = true;
    g_height_request.level = level;
    g_height_request.target_pending = true;
    g_last_requested_level = level;
    taskEXIT_CRITICAL();
}

/* Called only after the upward PWM has run for the full homing interval. */
static void Lifter_CompleteHoming(uint32_t now)
{
    Lifter_Stop();
    g_current_pos_ms = 0.0f;
    Update_Height_CM_From_MS(g_current_pos_ms);
    g_state = LIFTER_STATE_HOMING_DONE;
    if (g_app.g_system_report_state == REPORT_BLADE_INITIALIZING)
        g_app.g_system_report_state = REPORT_NONE;
    g_flash_save_pending = 1;
    g_flash_save_timer = now;
}

/* Latest target wins, including a normal target that interrupts homing. */
static void Lifter_ApplyHeightRequest(uint32_t now)
{
    LifterHeightRequest_t request;
    taskENTER_CRITICAL();
    request = g_height_request;
    g_height_request.target_pending = false;
    g_height_request.home_pending = false;
    taskEXIT_CRITICAL();

    if (!request.target_pending)
        return;

    g_target_pos_ms = (float)LEVEL_TARGET_MS[request.level];
    if (g_state == LIFTER_STATE_HOMING_START || g_state == LIFTER_STATE_HOMING_RUNNING)
    {
        if (request.level == 5U)
            return; // 重复最高档不重置归位计时
        if (g_state == LIFTER_STATE_HOMING_RUNNING &&
            (uint32_t)(now - g_homing_start_ts) >= HOMING_TIMEOUT_MS)
        {
            // 已完整运行10秒：先完成校准，再在本周期执行新目标。
            Lifter_CompleteHoming(now);
        }
        else if (g_app.g_system_report_state == REPORT_BLADE_INITIALIZING)
        {
            // 未完成的归位直接取消，不强制清零位置。
            g_app.g_system_report_state = REPORT_NONE;
        }
    }

    g_state = request.home_pending ? LIFTER_STATE_HOMING_START : LIFTER_STATE_AUTO_ADJUST;
}

void Lifter_SaveHeightToFlash(void)
{
    Suspend_Heartbeat_Check(1);

    // 稍微延时，确保电机完全停稳
    HAL_Delay(50);

    AppSettings_t settings;
    settings.lifter_current_height_cm = g_current_height_cm;
    settings.magic_number = APP_SETTINGS_MAGIC_NUMBER;

    if (FlashStorage_SaveSettings(&settings))
    {
        // Serial_Printf(SERIAL_PORT_2, "[LIFTER] Saved %.1f cm.\r\n", g_current_height_cm);
    }


    Suspend_Heartbeat_Check(0);
}

float Lifter_GetCurrentHeight_cm(void) { return g_current_height_cm; }
uint16_t Lifter_GetHeightFeedback(void) { return g_height_feedback_word; }
Lifter_MachineState Lifter_GetState(void) { return g_state; }

/**
 * @brief 设置草袋目标状态
 * @param open 1=打开，0=关闭
 */
/* All state and hardware changes below are serialized with periodic completion. */
static void GrassBag_SetTargetLocked(bool open)
{
    if (!g_bag_permitted || !GrassBagActuator_SetTarget(&g_bag, open, HAL_GetTick()))
        return;
    /* Zero both bridge legs before reversing. */
    GrassBag_Motor_Stop();
    if (open) GrassBag_Motor_Open();
    else GrassBag_Motor_Close();
}

void Lifter_SetGrassBagTarget(uint8_t open)
{
    if (!GrassBag_Lock()) return;
    GrassBag_SetTargetLocked(open != 0U);
    osMutexRelease(g_bag_mutex);
}

void Lifter_ToggleGrassBag(void)
{
    if (!GrassBag_Lock()) return;
    GrassBag_SetTargetLocked(!g_bag.last_target_open);
    osMutexRelease(g_bag_mutex);
}

void Lifter_StopGrassBag(void)
{
    if (!GrassBag_Lock()) return;
    GrassBag_Motor_Stop();
    GrassBagActuator_Stop(&g_bag);
    osMutexRelease(g_bag_mutex);
}

void Lifter_SetGrassBagPermission(bool permitted)
{
    if (!GrassBag_Lock()) return;
    g_bag_permitted = permitted;
    if (!permitted) {
        GrassBag_Motor_Stop();
        GrassBagActuator_Stop(&g_bag);
    }
    osMutexRelease(g_bag_mutex);
}

/**
 * @brief 周期任务
 */
void Lifter_Task_Update(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t dt = now - g_last_tick;
    g_last_tick = now;
    // =================================================================
    //  A. 刀盘高度逻辑
    // =================================================================

    // --- 1. 位置积分 ---
    // 归位期间也累计位移，确保中途换档使用的是当前估算位置。
    if (g_dir != 0)
    {
        float change = (float)dt;
        if (g_dir == 1)
        {
            g_current_pos_ms -= (change * UP_SPEED_FACTOR);
        }
        else
        {
            g_current_pos_ms += change;
        }
        if (g_current_pos_ms < 0)
            g_current_pos_ms = 0;
        if (g_current_pos_ms > 15000.0f)
            g_current_pos_ms = 15000.0f;
    }

    Update_Height_CM_From_MS(g_current_pos_ms);
    Lifter_ApplyHeightRequest(now);

    // --- 2. 状态机 ---
    switch (g_state)
    {
    case LIFTER_STATE_HOMING_START:
        Motor_Up();
        g_homing_start_ts = HAL_GetTick(); // 从实际开始输出上升PWM后计时
        g_app.g_system_report_state = REPORT_BLADE_INITIALIZING;
        g_state = LIFTER_STATE_HOMING_RUNNING;
        break;

    case LIFTER_STATE_HOMING_RUNNING:
        if ((uint32_t)(now - g_homing_start_ts) >= HOMING_TIMEOUT_MS)
            Lifter_CompleteHoming(now);
        break;

    case LIFTER_STATE_HOMING_DONE:
        g_state = LIFTER_STATE_AUTO_ADJUST;
        break;

    case LIFTER_STATE_AUTO_ADJUST:
    {
        float err = g_target_pos_ms - g_current_pos_ms;

        // 死区判断 (到位)
        if (fabsf(err) <= DEAD_BAND_MS)
        {
            if (g_dir != 0)
            {
                Lifter_Stop();
                // 到位后不立刻保存，而是触发 10 秒倒计时
                g_flash_save_pending = 1;
                g_flash_save_timer = now;
            }
        }
        else if (err > 0)
        {
            if (g_dir != -1)
                Motor_Down();
        }
        else
        {
            if (g_dir != 1)
                Motor_Up();
        }
        break;
    }
    default:
        break;
    }

    /* Publish after integration/homing completion. SENS must not invent a new
     * heartbeat when this task stops advancing its height estimate. */
    if (g_current_height_cm >= 1.0f && g_current_height_cm <= 11.0f)
    {
        uint16_t mm = (uint16_t)(g_current_height_cm * 10.0f + 0.5f);
        g_height_feedback_word = (uint16_t)(
            (((now / MOWER_HEIGHT_HEARTBEAT_MS) & MOWER_HEIGHT_HEARTBEAT_MASK) << 8) |
            MOWER_HEIGHT_SAMPLE_VALID | mm);
    }
    else
    {
        g_height_feedback_word = 0U;
    }

    // =================================================================
    //  <<<---  3. 延时保存 Flash (Lazy Save) 机制 ---<<<
    // =================================================================

    // 只有当有保存任务，且电机确实停稳了，才进行倒计时判定
    if (g_flash_save_pending && g_dir == 0 &&
        fabsf(g_target_pos_ms - g_current_pos_ms) <= DEAD_BAND_MS)
    {
        // 检查是否已经停稳超过 10 秒 (FLASH_SAVE_DELAY_MS)
        if (now - g_flash_save_timer >= FLASH_SAVE_DELAY_MS)
        {
            Lifter_SaveHeightToFlash(); // 真正执行 Flash 写入操作

            g_flash_save_pending = 0; // 写入完毕，清除标志位

            // 可以通过串口打印确认一下保存成功
            // Serial_Printf(SERIAL_PORT_2, "[LIFTER] Lazy Save Executed after 10s stop.\r\n");
        }
    }

    // =================================================================
    //  B. 草袋开合状态机 (被动等待接口触发，处理超时)
    // =================================================================
    if (!GrassBag_Lock()) return;
    /* Sample time under the lock: a target can change during the height update. */
    now = HAL_GetTick();
    if (GrassBagActuator_Advance(&g_bag, now)) {
        GrassBag_Motor_Stop();
        g_app.grass_bag_status = g_bag.completed_open;
        if (!g_bag.completed_open) {
            g_dump_done_timer = now;
            if (g_bag_permitted) g_app.g_system_report_state = REPORT_DUMPING_DONE;
        }
    }
    if (g_bag_permitted) {
        switch (g_bag.state) {
        case GRASS_BAG_OPENING:
        case GRASS_BAG_OPEN:
            g_app.g_system_report_state = REPORT_DUMPING_START;
            break;
        case GRASS_BAG_CLOSING:
            g_app.g_system_report_state = REPORT_DUMPING_CLOSING;
            break;
        case GRASS_BAG_CLOSED:
            if (g_app.g_system_report_state == REPORT_DUMPING_DONE &&
                (uint32_t)(now - g_dump_done_timer) >= 2000U)
                g_app.g_system_report_state = REPORT_NONE;
            break;
        default:
            break;
        }
    }
    osMutexRelease(g_bag_mutex);
}
