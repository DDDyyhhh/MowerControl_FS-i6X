/**
 *******************************************************************************
 * @file           : chassis_control.c
 * @author         : [Your Name/Team]
 * @version        : V1.1
 * @date           : 2026-04-16
 * @brief          : 4轮差速底盘控制程序，支持遥控模式和API模式
 *
 * 主要功能：
 * 1. 遥控模式：处理遥控器输入，支持死区处理和转向补偿
 * 2. API模式：基于 (vx, vy, w) 输入计算四个轮子的速度
 * 3. 支持4轮差速底盘运动学（vy 默认忽略）
 * 4. 轮子参数：直径240mm，左右轴距690mm，前后轮距640mm
 *5.最大速度约 2.5 m/s（根据实际测试调整）
 * 注意事项：
 * - vx: 前进速度 (m/s)
 * - vy: 侧向速度 (m/s)
 * - w:  角速度 (rad/s)
 * - 输出单位: turn/s (转/秒)
 *******************************************************************************
 */
#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "chassis_control.h"
#include "app_context.h" // 包含全局上下文，获取用户设定的速度
#include "remote_backend.h"
#include "serial_port.h"
#include "cruise_control.h"
#include "connet_check.h"
#include "BladeMotor.h"
#include "lifter.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "odrive_can.h" // 包含ODrive CAN通信函数
#include "tricolor_led.h"
uint8_t Ctrl_Mode = 0; // 0: 遥控模式, 1: API模式
float host_vx = 0.0f;
static float host_vy = 0.0f;
float host_omega = 0.0f;
static uint8_t host_blade_speed_pct = 0;
static uint8_t host_lifter_gear = 1;
static uint8_t host_bag_control = 0;
//  API 上位机心跳时间戳
uint32_t last_host_cmd_tick = 0;
uint8_t api_lockout = 0; // API 锁存标志位：一旦遥控器摇杆动了，置 1 锁死 API，直到遥控器屏幕下发解除指令
extern AppContext_t g_app;
extern volatile ODrive_Data_t g_odrive_motors[ODRIVE_MAX_MOTORS];
uint16_t mapped_throttle = 2048;
uint16_t mapped_steering = 2048;
/**
 * @brief 初始化底盘控制
 */
void chassis_control_init(void)
{
    last_host_cmd_tick = HAL_GetTick();
}

/**
 * @brief 解析上位机控制帧（HOST_CMD_FRAME_ID = 0x005）
 *
 * 控制帧格式（标准帧，DLC=8）：
 * Byte0: ctrl_mode      0=RC模式, 1=API待机, 2=API运行
 * Byte1: vx_raw         int8，线速度指令，单位 0.1 m/s
 * Byte2: vy_raw         int8，预留（差速底盘当前忽略）
 * Byte3: omega_raw      int8，角速度指令，单位 0.1 rad/s
 * Byte4: blade_speed    uint8，割刀挡位 0-3
 * Byte5: lifter_gear    uint8，升降档位 1-5（越界会钳位）
 * Byte6: bag_control    uint8，0=关，非0=开
 * Byte7: magic_key      0xAA=强制解除物理碰撞锁

 * 解析规则：
 * host_vx    = vx_raw * 0.1f;      // m/s
 * host_omega = omega_raw * 0.1f;   // rad/s
 * host_vy    = 0.0f;               // 当前不支持侧移
 *
 * 示例1：前进 0.1m/s，无旋转，API模式
 * ID=0x05, DLC=8, DATA=01 01 00 00 00 01 00 AA
 *
 * 示例2：后退 0.5m/s，右转 0.3rad/s
 * ID=0x05, DLC=8, DATA=01 FB 00 03 00 01 00 AA
 * 说明：FB(int8)=-5 => -0.5m/s，03 => +0.3rad/s
 */
void chassis_control_parse_host_frame(uint8_t frame_id, const uint8_t *payload, uint8_t len)
{
    if (frame_id != HOST_CMD_FRAME_ID || payload == NULL || len < HOST_CMD_PAYLOAD_LEN)
    {
        return;
    }

    last_host_cmd_tick = HAL_GetTick();

    if ((payload[7] == 0xAA) && (g_app.physical_safety_latched == 0U))
    {
        g_app.g_collision_latched = 0;
        g_app.lock = 0;
        RGB_LED_SetColor(LED_1, RGB_COLOR_GREEN);
        if (g_app.g_system_report_state == REPORT_INIT_SUCCESS ||
            g_app.g_system_report_state == REPORT_FAULT_COLLISION)
        {
            g_app.g_system_report_state = REPORT_NONE;
        }
    }

    uint8_t host_cmd = payload[0];

    // ============================================================
    // 【三级跳状态机逻辑】
    // ============================================================
    if (host_cmd == 1)
    {
        // --- 动作：申请进入 API 待机 ---
        // 只要上位机发 1，无论当前在什么状态，都强制回到 API 待机状态
        // 并且【关键】：在这里清空遥控器的拦截锁 (api_lockout)
        Ctrl_Mode = 1;
        api_lockout = 0;
    }
    else if (host_cmd == 2)
    {
        // --- 动作：申请开启 API 动力 ---
        // 只有当前已经在状态 1 (待机)，且没有被遥控器拦截时，才允许跳到状态 2
        if (Ctrl_Mode == 1 && api_lockout == 0)
        {
            Ctrl_Mode = 2;
        }
        // 如果当前是状态 0 或有 lockout，发 2 是无效的，车子不会动
    }
    else if (host_cmd == 0)
    {
        // --- 动作：主动退回遥控 ---
        if (Ctrl_Mode == 1 || Ctrl_Mode == 2)
        {
            // 只要从 API 模式退出来，必须强制锁死底盘，让操作员双摇杆解锁！
            g_app.lock = 1;
            g_app.g_collision_latched = 1;
            g_app.g_system_report_state = 7; // 弹窗：请手动解锁
            g_app.receiver_status[STATUS_INDEX_LOCK] = 0;
            g_app.receiver_status[STATUS_INDEX_BLADE_SPEED] = 0;
        }
        // --- 动作：主动退回遥控 ---
        Ctrl_Mode = 0;
        api_lockout = 0;
    }

    int8_t vx_raw = (int8_t)payload[1];
    int8_t vy_raw = (int8_t)payload[2];
    int8_t omega_raw = (int8_t)payload[3];
    uint8_t blade_speed_pct = payload[4];
    uint8_t lifter_gear = payload[5];
    uint8_t bag_control = payload[6];

    host_blade_speed_pct = (blade_speed_pct > 3) ? 3 : blade_speed_pct;
    host_lifter_gear = (lifter_gear < 1) ? 1 : (lifter_gear > 5 ? 5 : lifter_gear);
    host_bag_control = (bag_control != 0) ? 1 : 0;

    // --- API 模式状态同步覆盖 ---
    if (Ctrl_Mode == 2)
    {
        // 强制覆盖全局应用状态
        Lifter_SetGrassBagTarget(host_bag_control);
        g_app.knob_height = host_lifter_gear;
        g_app.knob_blade = host_blade_speed_pct;

        // 【关键】同步给反馈状态缓冲区，让遥控器屏幕显示 API 设定的真实挡位
        // 注意：根据你 pairing_receiver.c 的协议，receiver_status[0] 是割刀状态
        g_app.receiver_status[STATUS_INDEX_LOCK] = host_blade_speed_pct;
        g_app.receiver_status[STATUS_INDEX_BLADE_SPEED] = host_blade_speed_pct;
    }

    host_vx = (float)vx_raw * 0.1f;       // 假设输入范围 -10 到 +10 对应 -1.0 m/s 到 +1.0 m/s
    host_vy = 0.0f;                       // 差速底盘不支持侧向移动
    host_omega = (float)omega_raw * 0.1f; // 假设输入范围 -10 到 +10 对应 -1.0 rad/s 到 +1.0 rad/s
}

void chassis_control_clear_cached_intents(void)
{
    host_vx = 0.0f;
    host_vy = 0.0f;
    host_omega = 0.0f;
    host_blade_speed_pct = 0U;
}

/**
 * @brief 发送底盘状态反馈帧
 * */
void chassis_control_send_status_feedback(void)
{
    static uint32_t last_feedback_tick = 0;
    uint32_t now_tick = HAL_GetTick();
    if ((now_tick - last_feedback_tick) < 20U)
    {
        return;
    }
    last_feedback_tick = now_tick;

    uint8_t tx_data[6];
    tx_data[0] = Ctrl_Mode;
    tx_data[1] = g_app.bms_temp;
    tx_data[2] = g_app.voltage_int; /* 0xFF = unavailable; never convert to 100%. */

    uint8_t bag_status = 0;
    if (g_app.grass_bag_status)
        bag_status |= 0x01;
    if (g_app.is_bag_connected)
        bag_status |= 0x02;
    if (g_app.is_bag_full)
        bag_status |= 0x04;
    tx_data[3] = bag_status;
    tx_data[4] = (g_app.key[0] & 0x03) | ((g_app.key[1] & 0x03) << 2) | ((g_app.key[2] & 0x03) << 4) | ((g_app.key[3] & 0x03) << 6);
    tx_data[5] = (g_app.key[4] & 0x03) | ((g_app.key[5] & 0x03) << 2) | ((g_app.key[6] & 0x03) << 4) | ((g_app.key[7] & 0x03) << 6);

    odrive_can_send_message(&hcan1, CHASSIS_STATUS_CAN_ID, tx_data, sizeof(tx_data), 0);
}

/**
 * @brief 向 4 个 ODrive 节点手动请求编码器数据 (兼容 0.5.1 固件)
 * @note  发送的是 RTR 远程帧，数据会在随后的 CAN RX 中断中自动更新到 g_odrive_motors 数组
 */
/**
 * @brief 向底层驱动器手动请求数据 (例如编码器速度)
 */
void chassis_control_req_motor_data(void)
{
    static uint32_t last_req_tick = 0;
    static uint8_t req_step = 0;

    uint32_t now_tick = HAL_GetTick();

    /* MSSD 速度由两个 16 位寄存器组成，查询周期不能过慢。 */
#if MOTOR_DRIVER_USED == DRIVER_TYPE_MSSD
    if ((now_tick - last_req_tick) < 20U)
        return;
#else
    /* 诊断查询降频，不能与实时速度查询争抢 CAN 带宽。 */
    if ((now_tick - last_req_tick) < 300U)
        return;
#endif
    last_req_tick = now_tick;

#if MOTOR_DRIVER_USED == DRIVER_TYPE_ODRIVE

    odrive_req_encoder_estimates(&hcan1, 1);
    odrive_req_encoder_estimates(&hcan1, 2);
    odrive_req_encoder_estimates(&hcan1, 3);
    odrive_req_encoder_estimates(&hcan1, 4);

#elif MOTOR_DRIVER_USED == DRIVER_TYPE_MSSD

    // MSSD 错峰查询状态机 (总周期 20ms * 8 = 160ms)
    // - 速度查询会被执行 4 次 (平均 40ms 一次，保证控制平滑)
    // - 功率查询执行 2 次 (160ms 更新一轮)
    // - 温度查询执行 2 次 (160ms 更新一轮)
    switch (req_step)
    {
    case 0:
        MSSD_Req_Velocity(MSSD_NODE_RIGHT, MSSD_CH_FRONT);
        MSSD_Req_Velocity(MSSD_NODE_RIGHT, MSSD_CH_REAR);
        break;
    case 1:
        MSSD_Req_Velocity(MSSD_NODE_LEFT, MSSD_CH_FRONT);
        MSSD_Req_Velocity(MSSD_NODE_LEFT, MSSD_CH_REAR);
        break;
    case 2:
        MSSD_Req_Power(MSSD_NODE_RIGHT); // 查右侧电压电流
        break;
    case 3:
        MSSD_Req_Power(MSSD_NODE_LEFT); // 查左侧电压电流
        break;
    case 4:
        MSSD_Req_Velocity(MSSD_NODE_RIGHT, MSSD_CH_FRONT); // 再次穿插速度请求
        MSSD_Req_Velocity(MSSD_NODE_RIGHT, MSSD_CH_REAR);
        break;
    case 5:
        MSSD_Req_Velocity(MSSD_NODE_LEFT, MSSD_CH_FRONT); // 再次穿插速度请求
        MSSD_Req_Velocity(MSSD_NODE_LEFT, MSSD_CH_REAR);
        break;
    case 6:
        MSSD_Req_Temperature(MSSD_NODE_RIGHT); // 查右侧温度
        break;
    case 7:
        MSSD_Req_Temperature(MSSD_NODE_LEFT); // 查左侧温度
        break;
    }

    req_step++;
    if (req_step > 7)
    {
        req_step = 0;
    }

#elif MOTOR_DRIVER_USED == DRIVER_TYPE_OID
    // OID 诊断轮询：每次只发一帧，避免一轮内突发多个查询。
    switch (req_step)
    {
    case 0:
        OID_Req_Parameter(OID_NODE_FL, OID_PARAM_ERROR);
        break;
    case 1:
        OID_Req_Parameter(OID_NODE_FR, OID_PARAM_ERROR);
        break;
    case 2:
        OID_Req_Parameter(OID_NODE_RL, OID_PARAM_ERROR);
        break;
    case 3:
        OID_Req_Parameter(OID_NODE_RR, OID_PARAM_ERROR);
        break;
    case 4:
        OID_Req_Parameter(OID_NODE_FL, OID_PARAM_PHASE_CURRENT);
        break;
    case 5:
        OID_Req_Parameter(OID_NODE_FR, OID_PARAM_PHASE_CURRENT);
        break;
    case 6:
        OID_Req_Parameter(OID_NODE_RL, OID_PARAM_PHASE_CURRENT);
        break;
    case 7:
        OID_Req_Parameter(OID_NODE_RR, OID_PARAM_PHASE_CURRENT);
        break;
    case 8:
        OID_Req_Parameter(OID_NODE_FL, OID_PARAM_BUS_VOLTAGE);
        break;
    case 9:
        OID_Req_Parameter(OID_NODE_FR, OID_PARAM_BUS_VOLTAGE);
        break;
    }

    req_step++;
    if (req_step > 9)
    {
        req_step = 0;
    }

#endif
}

#if MOTOR_DRIVER_USED == DRIVER_TYPE_OID
void chassis_control_poll_oid_actual_speed(uint8_t phase)
{
    /* Motion task slots 1..4 query one wheel each. Slot 5 sends controls.
     * Keep each wheel at 50ms without same-batch query/control traffic.
     */
    switch (phase)
    {
    case 1: OID_Req_Actual_Speed(OID_NODE_FL); break;
    case 2: OID_Req_Actual_Speed(OID_NODE_FR); break;
    case 3: OID_Req_Actual_Speed(OID_NODE_RL); break;
    case 4: OID_Req_Actual_Speed(OID_NODE_RR); break;
    default: break;
    }
}

static int16_t chassis_erpm_to_speed_mmps(int32_t erpm, int8_t sign)
{
    float speed_mps = ((float)erpm * (float)sign) * PI * WHEEL_DIAMETER /
                      ((float)OID_POLE_PAIRS * GEAR_RATIO * 60.0f);
    float speed_mmps = speed_mps * 1000.0f;
    int32_t value = (speed_mmps >= 0.0f) ? (int32_t)(speed_mmps + 0.5f) : (int32_t)(speed_mmps - 0.5f);

    if (value > INT16_MAX)
        return INT16_MAX;
    if (value < INT16_MIN)
        return INT16_MIN;
    return (int16_t)value;
}

static void chassis_pack_i16_be(uint8_t *buf, uint8_t offset, int16_t value)
{
    buf[offset] = (uint8_t)((uint16_t)value >> 8);
    buf[offset + 1U] = (uint8_t)((uint16_t)value & 0xFFU);
}

static void chassis_control_send_oid_wheel_speed_feedback(void)
{
    static uint32_t last_speed_tick = 0;
    uint32_t now_tick = HAL_GetTick();

    if ((now_tick - last_speed_tick) < 50U)
        return;
    last_speed_tick = now_tick;

    uint8_t tx_buf[8];
    int16_t speed_fl = chassis_erpm_to_speed_mmps(g_oid_drives[OID_NODE_FL].actual_speed_erpm, 1);
    int16_t speed_fr = chassis_erpm_to_speed_mmps(g_oid_drives[OID_NODE_FR].actual_speed_erpm, -1);
    int16_t speed_rl = chassis_erpm_to_speed_mmps(g_oid_drives[OID_NODE_RL].actual_speed_erpm, 1);
    int16_t speed_rr = chassis_erpm_to_speed_mmps(g_oid_drives[OID_NODE_RR].actual_speed_erpm, -1);

    chassis_pack_i16_be(tx_buf, 0, speed_fl);
    chassis_pack_i16_be(tx_buf, 2, speed_fr);
    chassis_pack_i16_be(tx_buf, 4, speed_rl);
    chassis_pack_i16_be(tx_buf, 6, speed_rr);

    odrive_can_send_message(&hcan1, CHASSIS_WHEEL_SPEED_CAN_ID, tx_buf, 8, 0);
}
#endif

bool chassis_control_read_wheel_feedback(ChassisWheelFeedback_t wheels[4])
{
    if (wheels == NULL)
    {
        return false;
    }

#if MOTOR_DRIVER_USED == DRIVER_TYPE_OID
    int32_t erpm[4];
    const uint8_t nodes[4] = {OID_NODE_FL, OID_NODE_FR, OID_NODE_RL, OID_NODE_RR};
    taskENTER_CRITICAL();
    for (unsigned i=0; i<4U; ++i) {
        wheels[i].valid = g_oid_drives[nodes[i]].actual_speed_valid;
        wheels[i].last_good_tick = g_oid_drives[nodes[i]].actual_speed_update_tick;
        erpm[i] = g_oid_drives[nodes[i]].actual_speed_erpm;
    }
    taskEXIT_CRITICAL();
    for (unsigned i=0; i<4U; ++i)
        wheels[i].speed_mm_per_s = chassis_erpm_to_speed_mmps(erpm[i], (i & 1U) ? -1 : 1);
#elif MOTOR_DRIVER_USED == DRIVER_TYPE_MSSD
    {
        taskENTER_CRITICAL();
        const float scale = PI * WHEEL_DIAMETER * 1000.0f /
                            (GEAR_RATIO * 60.0f);
        wheels[0].valid = g_mssd_drives[MSSD_NODE_LEFT].speed_r_valid;
        wheels[0].last_good_tick = g_mssd_drives[MSSD_NODE_LEFT].speed_r_update_tick;
        wheels[0].speed_mm_per_s = (int32_t)(
            (float)g_mssd_drives[MSSD_NODE_LEFT].actual_rpm_r * -scale);
        wheels[1].valid = g_mssd_drives[MSSD_NODE_RIGHT].speed_r_valid;
        wheels[1].last_good_tick = g_mssd_drives[MSSD_NODE_RIGHT].speed_r_update_tick;
        wheels[1].speed_mm_per_s = (int32_t)(
            (float)g_mssd_drives[MSSD_NODE_RIGHT].actual_rpm_r * scale);
        wheels[2].valid = g_mssd_drives[MSSD_NODE_LEFT].speed_l_valid;
        wheels[2].last_good_tick = g_mssd_drives[MSSD_NODE_LEFT].speed_l_update_tick;
        wheels[2].speed_mm_per_s = (int32_t)(
            (float)g_mssd_drives[MSSD_NODE_LEFT].actual_rpm_l * -scale);
        wheels[3].valid = g_mssd_drives[MSSD_NODE_RIGHT].speed_l_valid;
        wheels[3].last_good_tick = g_mssd_drives[MSSD_NODE_RIGHT].speed_l_update_tick;
        wheels[3].speed_mm_per_s = (int32_t)(
            (float)g_mssd_drives[MSSD_NODE_RIGHT].actual_rpm_l * scale);
        taskEXIT_CRITICAL();
    }
#else
    memset(wheels, 0, sizeof(ChassisWheelFeedback_t) * 4U);
#endif
    return true;
}

/**
 * @brief 紧急切断底盘动力
 */
void chassis_stop(void)
{
#if MOTOR_DRIVER_USED == DRIVER_TYPE_ODRIVE
    Send_Motor_speed(1, 0.0f, 0.0f, 0);
    Send_Motor_speed(2, 0.0f, 0.0f, 0);
    Send_Motor_speed(3, 0.0f, 0.0f, 0);
    Send_Motor_speed(4, 0.0f, 0.0f, 0);
#elif MOTOR_DRIVER_USED == DRIVER_TYPE_MSSD
    MSSD_Stop_Motor(MSSD_NODE_RIGHT, MSSD_CH_FRONT);
    MSSD_Stop_Motor(MSSD_NODE_RIGHT, MSSD_CH_REAR);
    MSSD_Stop_Motor(MSSD_NODE_LEFT, MSSD_CH_FRONT);
    MSSD_Stop_Motor(MSSD_NODE_LEFT, MSSD_CH_REAR);
#elif MOTOR_DRIVER_USED == DRIVER_TYPE_OID
    set_chassis_velocity(EXECUTION_OWNER_NONE, 0.0f, 0.0f, 0.0f);
#endif
}

/**
 * @brief 计算真实里程计并发送 (线速度、角速度、编码器累计位置)
 * @note 频率限制为 20ms (50Hz) 发送一次，防止挤占 CAN 总线带宽
 */
void chassis_control_send_odom_feedback(void)
{
    static uint32_t last_odom_tick = 0;
    uint32_t now_tick = HAL_GetTick();

    if ((now_tick - last_odom_tick) < 50U)
        return;
    last_odom_tick = now_tick;

    float tx_vx = 0.0f;
    float tx_omega = 0.0f;

    // ============================================================
    // 【核心修改】：根据当前模式选择反馈的数据源
    // ============================================================
    if (Ctrl_Mode == 1 || Ctrl_Mode == 2)
    {
        // API 模式下，直接反馈上位机发来的设定值 (Echo)
        tx_vx = host_vx;
        tx_omega = host_omega;
    }

    // -------------------------------------------------------------
    // 3. 打包发送 CAN 数据帧 (ID: 0x507)
    // -------------------------------------------------------------
    uint8_t tx_buf[8];

    // Byte 0-3: tx_vx (float)
    // Byte 4-7: tx_omega (float)
    memcpy(&tx_buf[0], &tx_vx, 4);
    memcpy(&tx_buf[4], &tx_omega, 4);

    odrive_can_send_message(&hcan1, CHASSIS_ODOM_CAN_ID, tx_buf, 8, 0);
    // --- 帧 0x08: 右侧编码器绝对位置 (Turns) ---
    // float pos_fr = g_odrive_motors[1].encoder_pos;
    // float pos_rr = g_odrive_motors[2].encoder_pos;
    // memcpy(&tx_buf[0], &pos_fr, 4);
    // memcpy(&tx_buf[4], &pos_rr, 4);
    // odrive_can_send_message(&hcan1, CHASSIS_ENC_RIGHT_CAN_ID, tx_buf, 8, 0);

    // // --- 帧 0x09: 左侧编码器绝对位置 (Turns) ---
    // // 左侧取反，保证前进时所有编码器位置都是单调递增的，方便上位机做里程计
    // float pos_fl = -g_odrive_motors[3].encoder_pos;
    // float pos_rl = -g_odrive_motors[4].encoder_pos;
    // memcpy(&tx_buf[0], &pos_fl, 4);
    // memcpy(&tx_buf[4], &pos_rl, 4);
    // odrive_can_send_message(&hcan1, CHASSIS_ENC_LEFT_CAN_ID, tx_buf, 8, 0);
}

void Send_Motor_speed(uint8_t axis_id, float speed_rpm, float torque_ff, uint8_t reset_integrator)
{
    // 1. 单位转换: Turns/s
    float vel_rps = speed_rpm;
    odrive_set_input_vel(&hcan1, axis_id, vel_rps, torque_ff);
}

void set_chassis_velocity(ExecutionOwner_t owner, float vx, float vy, float omega)
{
    bool nonzero_intent = (vx != 0.0f) || (vy != 0.0f) || (omega != 0.0f);

    if (!SafetyOwnershipGate_Authorize(owner, nonzero_intent))
    {
        vx = 0.0f;
        vy = 0.0f;
        omega = 0.0f;
    }
    if (!isfinite(vx) || !isfinite(vy) || !isfinite(omega))
        vx = vy = omega = 0.0f;
    vy = 0.0f; // 强制不支持侧向移动

    float R = TRACK_WIDTH / 2.0f;

    // 计算各轮子线速度 (m/s)
    float v_fr = vx - vy - omega * R;
    float v_fl = vx + vy + omega * R;
    float v_rr = vx + vy - omega * R;
    float v_rl = vx - vy + omega * R;

    // 转换为RPM (电机输出轴 RPM = 线速度 * 60 / 轮周长)
    float rpm_fl = (v_fl * 60.0f) / (PI * WHEEL_DIAMETER);
    float rpm_fr = (v_fr * 60.0f) / (PI * WHEEL_DIAMETER);
    float rpm_rl = (v_rl * 60.0f) / (PI * WHEEL_DIAMETER);
    float rpm_rr = (v_rr * 60.0f) / (PI * WHEEL_DIAMETER);

    // 乘以减速比，得到电机本体的真实 RPM
    float motor_rpm_fl = rpm_fl * GEAR_RATIO;
    float motor_rpm_fr = rpm_fr * GEAR_RATIO;
    float motor_rpm_rl = rpm_rl * GEAR_RATIO;
    float motor_rpm_rr = rpm_rr * GEAR_RATIO;

#if MOTOR_DRIVER_USED == DRIVER_TYPE_ODRIVE
    // ODrive 接收的是 Turns/s (转/秒)，需要除以 60
    Send_Motor_speed(1, motor_rpm_fr / 60.0f, 0.0f, 0);  // 前右
    Send_Motor_speed(2, motor_rpm_rr / 60.0f, 0.0f, 0);  // 后右
    Send_Motor_speed(3, -motor_rpm_fl / 60.0f, 0.0f, 0); // 前左，物理取反
    Send_Motor_speed(4, -motor_rpm_rl / 60.0f, 0.0f, 0); // 后左，物理取反

#elif MOTOR_DRIVER_USED == DRIVER_TYPE_MSSD
    // MSSD 直接接收 RPM (整型)
    int32_t send_rpm_fr = (int32_t)(motor_rpm_fr);
    int32_t send_rpm_rr = (int32_t)(motor_rpm_rr);
    int32_t send_rpm_fl = (int32_t)(-motor_rpm_fl);
    int32_t send_rpm_rl = (int32_t)(-motor_rpm_rl);
    // Serial_Printf(SERIAL_PORT_2, "Set Vel FR: %ld RPM, RR: %ld RPM, FL: %ld RPM, RL: %ld RPM\r\n", send_rpm_fr, send_rpm_rr, send_rpm_fl, send_rpm_rl);
    //  驱动器 1 (Node 1) 控制左侧前后电机
    MSSD_Set_Velocity(MSSD_NODE_RIGHT, MSSD_CH_FRONT, send_rpm_fr);
    osDelay(1);
    MSSD_Set_Velocity(MSSD_NODE_RIGHT, MSSD_CH_REAR, send_rpm_rr);
    osDelay(1);
    MSSD_Set_Velocity(MSSD_NODE_LEFT, MSSD_CH_FRONT, send_rpm_fl);
    osDelay(1); // 适当延时，避免 CAN 总线过载
    MSSD_Set_Velocity(MSSD_NODE_LEFT, MSSD_CH_REAR, send_rpm_rl);
    osDelay(1);

#elif MOTOR_DRIVER_USED == DRIVER_TYPE_OID
    // OID 使用 erpm，公式: erpm = rpm * 极对数
    // 全部统一使正 erpm = 前进方向
    int32_t oid_erpm_fr = (int32_t)(-motor_rpm_fr * OID_POLE_PAIRS);
    int32_t oid_erpm_rr = (int32_t)(-motor_rpm_rr * OID_POLE_PAIRS);
    int32_t oid_erpm_fl = (int32_t)(motor_rpm_fl * OID_POLE_PAIRS);
    int32_t oid_erpm_rl = (int32_t)(motor_rpm_rl * OID_POLE_PAIRS);

    OID_Set_Speed(OID_NODE_FR, oid_erpm_fr);
    OID_Set_Speed(OID_NODE_RR, oid_erpm_rr);
    OID_Set_Speed(OID_NODE_FL, oid_erpm_fl);
    OID_Set_Speed(OID_NODE_RL, oid_erpm_rl);

#endif
}

/**
 * @brief 使用遥控器输入控制底盘运动（遥控模式）
 * @param ly 左摇杆Y轴值(0-4095)
 * @param rx 右摇杆X轴值(0-4095)
 */
void set_chassis_rc_control(uint16_t ly, uint16_t rx)
{
    // 1. 归一化输入(-1.0 ~ 1.0)
    float raw_forward = ((float)ly - 2048.0f) / 2048.0f;  // 霍尔摇杆前后 (CH2)
    float raw_turn = ((float)2048 - (float)rx) / 2048.0f; // 转向左右 (CH1)

    // 2. 方案 A: 死区连续去偏平滑起步（消除离开死区时的阶跃突变断层）
    float forward_in = 0.0f;
    float abs_forward_raw = fabsf(raw_forward);
    if (abs_forward_raw > RC_DEADZONE)
    {
        float sign_f = (raw_forward > 0.0f) ? 1.0f : -1.0f;
        forward_in = sign_f * ((abs_forward_raw - RC_DEADZONE) / (1.0f - RC_DEADZONE));
        if (forward_in > 1.0f) forward_in = 1.0f;
        if (forward_in < -1.0f) forward_in = -1.0f;
    }
#if REMOTE_BACKEND_FLYSKY_IBUS
    if (ly == 4095U) forward_in = 1.0f;
#endif

    float turn_in = 0.0f;
    float abs_turn_raw = fabsf(raw_turn);
    if (abs_turn_raw > RC_DEADZONE)
    {
        float sign_t = (raw_turn > 0.0f) ? 1.0f : -1.0f;
        turn_in = sign_t * ((abs_turn_raw - RC_DEADZONE) / (1.0f - RC_DEADZONE));
        if (turn_in > 1.0f) turn_in = 1.0f;
        if (turn_in < -1.0f) turn_in = -1.0f;
    }

    // 3. 护草模式设置联动 (若遥控器设置显式开启，禁止原地自转)
    if (g_app.cfg_lawn_protect_en == 1)
    {
        if (forward_in == 0.0f)
        {
            turn_in = 0.0f;
        }
    }

    // 4. 后退限速与倒车转向方向处理
#if REMOTE_BACKEND_A28_E34
    float reverse_factor = 1.0f;
#endif
    float turn_in_dir = turn_in;
    if (forward_in < 0.0f)
    {
#if REMOTE_BACKEND_A28_E34
        if (g_app.cfg_reverse_limit_en == 1)
        {
            reverse_factor = 0.8f; // 开启后退限速时速度降为80%
        }
#endif
        turn_in_dir = -turn_in; // 倒车时转向方向取反
    }

    // 5. 基础线速度与自转角速度
#if REMOTE_BACKEND_FLYSKY_IBUS
    float vx = RemoteBackend_ApplyReverseSpeed(forward_in * g_app.speed,
                                               forward_in < 0.0f);
#else
    float vx = forward_in * g_app.speed * reverse_factor;
#endif
    float vy = 0.0f; // 差速底盘不支持横移
    float omega_pivot = turn_in_dir * g_app.speed * g_app.turn_speed;
    float omega = omega_pivot;

    // 6. 方案 B: 自转到行进转弯的平滑混合 (Differential Blending)
    // 当存在前后油门时，内侧轮如果反转会造成严重顿挫与刨地。
    // 在 0 ~ BLEND_THRESHOLD (25% 油门行程) 过渡带内，动态限制角速度上限，
    // 使得行进转弯以内侧轮平滑减速画弧为主，平滑过渡消除过零点反转卡顿。
    float abs_forward = fabsf(forward_in);
    const float BLEND_THRESHOLD = 0.25f;
    float R = TRACK_WIDTH / 2.0f; // 半轮距 0.32m

    if ((abs_forward > 0.0001f) && (fabsf(turn_in_dir) > 0.0001f))
    {
        // 混合权重 alpha: 0.0 (纯原地自转) -> 1.0 (完全行进转弯)
        float alpha = (abs_forward < BLEND_THRESHOLD) ? (abs_forward / BLEND_THRESHOLD) : 1.0f;

        // 行进转弯时内侧轮不反转的最大角速度: omega <= |vx| / R
        float max_omega_arc = fabsf(vx) / R;

        // 随 alpha 平滑过渡的动态角速度上限
        float max_allowed_omega = (1.0f - alpha) * fabsf(omega_pivot) + alpha * max_omega_arc;

        if (fabsf(omega) > max_allowed_omega)
        {
            float sign_omega = (omega > 0.0f) ? 1.0f : -1.0f;
            omega = sign_omega * max_allowed_omega;
        }
    }

    set_chassis_velocity(EXECUTION_OWNER_MANUAL_REMOTE, vx, vy, omega);
}

static void get_rc_control_inputs(uint16_t *throttle, uint16_t *steering)
{

    if (!CheckCommunicationStatus())
    {
        *throttle = 2048;
        *steering = 2048;
        return;
    }
    uint16_t raw_lx = g_app.Joystick[JOYSTICK_INDEX_LEFT_X];
    uint16_t raw_ly = g_app.Joystick[JOYSTICK_INDEX_THROTTLE];
    uint16_t raw_rx = g_app.Joystick[JOYSTICK_INDEX_STEERING];
    uint16_t raw_ry = g_app.Joystick[JOYSTICK_INDEX_RIGHT_Y];

    switch (g_app.cfg_op_mode)
    {
    case 0: // 双手1: 左手控前后，右手控左右
        *throttle = raw_ly;
        *steering = raw_rx;
        break;
    case 1: // 双手2: 左手控左右(LX)，右手控前后(RY)
        *throttle = raw_ry;
        *steering = raw_lx;
        break;
    case 2: // 单手1: 左手全包，控前后(LY)和左右(LX)
        *throttle = raw_ly;
        *steering = raw_lx;
        break;
    case 3: // 单手2: 右手全包，控前后(RY)和左右(RX)
        *throttle = raw_ry;
        *steering = raw_rx;
        break;
    default:
        *throttle = raw_ly;
        *steering = raw_rx;
        break;
    }
}

void Control_Update(void)
{

#if MOTOR_DRIVER_USED == DRIVER_TYPE_OID
    // 0x50A 已统一由 app_BMS_Sensor.c 的遥测任务发送，避免重复入队。
    // chassis_control_send_oid_wheel_speed_feedback();
#elif MOTOR_DRIVER_USED == DRIVER_TYPE_MSSD
    /* MSSD 实际速度反馈由轮询得到，统一由此处驱动错峰查询。 */
    chassis_control_req_motor_data();
#endif

    // =========================================================
    //  无论什么模式，底层物理安全必须置顶！
    // =========================================================
    if (g_app.g_collision_latched)
    {
        chassis_stop();
        return;
    }

    // --- 状态 0：纯遥控 ---
    if (Ctrl_Mode == 0)
    {

        get_rc_control_inputs(&mapped_throttle, &mapped_steering);
        set_chassis_rc_control(mapped_throttle, mapped_steering);
    }
    // --- 状态 1：API 待机 (已挂载但禁止动力) ---
    else if (Ctrl_Mode == 1)
    {
        chassis_stop(); // 动力切断

        // 安全检测：如果此时遥控器动了，虽然已经是状态1，但我们还是设为api_lockout
        // 这样可以防止上位机在状态1时，人去动摇杆，上位机却突然发指令跳到状态2
        uint16_t lt, ls;
        get_rc_control_inputs(&lt, &ls);
        if (abs((int)lt - 2048) > JOYSTICK_DEADZONE || abs((int)ls - 2048) > JOYSTICK_DEADZONE)
        {
            Ctrl_Mode = 0;
            api_lockout = 1;
            // 【核心新增】：退回遥控后，立刻锁死底盘，必须双摇杆解锁
            g_app.lock = 1;
            g_app.g_collision_latched = 1;
            g_app.g_system_report_state = 7; // 弹窗提示：请手动解锁
            g_app.receiver_status[STATUS_INDEX_LOCK] = 0;
            g_app.receiver_status[STATUS_INDEX_BLADE_SPEED] = 0;
        }
    }
    // --- 状态 2：API 运行 (动力开启) ---
    else if (Ctrl_Mode == 2)
    {
        // 1. API 心跳超时检查
        if (HAL_GetTick() - last_host_cmd_tick > API_TIMEOUT_MS)
        {
            Ctrl_Mode = 0;
            g_app.lock = 1;
            g_app.g_collision_latched = 1;
            g_app.g_system_report_state = 7; // 弹窗提示：请手动解锁
            chassis_stop();
            g_app.receiver_status[STATUS_INDEX_LOCK] = 0;
            g_app.receiver_status[STATUS_INDEX_BLADE_SPEED] = 0;
            return;
        }

        // 2. 遥控器最高优先级拦截
        uint16_t lt, ls;
        get_rc_control_inputs(&lt, &ls);
        if (abs((int)lt - 2048) > JOYSTICK_DEADZONE || abs((int)ls - 2048) > JOYSTICK_DEADZONE)
        {
            Ctrl_Mode = 0;
            api_lockout = 1;
            chassis_stop();

            // ：遥控器接管后，必须先双摇杆解锁才能开

            g_app.is_on_slope = 1;
            BladeMotor_SetSpeed(BLADE_SPEED_OFF);
            Cruise_Stop();

            g_app.lock = 1;                  // 顺便把割刀也锁上，保证绝对安全
            g_app.g_collision_latched = 1;   // 锁底盘
            g_app.g_system_report_state = 7; // 弹窗提示解锁
            g_app.receiver_status[STATUS_INDEX_LOCK] = 0;
            g_app.receiver_status[STATUS_INDEX_BLADE_SPEED] = 0;
            return;
        }

        // 3. 正常执行 API 运动指令
        set_chassis_velocity(EXECUTION_OWNER_API, host_vx, host_vy, -host_omega);
    }

    // chassis_control_req_motor_data();
    osDelay(1);
    // 发送线速度、角速度与编码器数据
}
