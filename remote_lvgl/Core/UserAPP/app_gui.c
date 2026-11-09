/**
 ******************************************************************************
 * @file           : app_gui.c
 * @author         : [Your Name/Team]
 * @version        : V2.0
 * @date           : 2026-03-11
 * @brief          : 基于 LVGL 的图形用户界面 (GUI) 主任务与视图更新逻辑
 *
 * @note           任务优先级 : 普通/较低 (Normal / Below Normal)
 *
 * 主要功能：
 * 1. LVGL 核心初始化与底层显示接口 (`lv_port_disp_init`) 绑定
 * 2. 执行开机动画 (Boot Animation)：包含 Logo 渲染、进度条动画及延时亮屏机制
 * 3. 维护 LVGL 心跳与事务：在主循环中高频调用 `lv_timer_handler()`
 * 4. 异步数据绑定与 UI 刷新 (20ms/100ms 错峰刷新)
 * 5. 全局系统通知中心：实时提示配对、断联及各类硬件故障
 *
 * 注意事项：
 * - 【性能优化】: 严格区分了动画刷新 (20ms) 和数据计算刷新 (100ms) 的时间片。
 * - 【防抖算法】: 遥控器电池百分比引入了“95%IIR滤波 + 0.6%死区 + 单向递减”复合算法。
 * - 【线程安全】: GUI 任务只读取全局状态数组，不直接参与底层控制，确保数据流单向。
 ******************************************************************************
 */
#include "app_gui.h"
#include "app_vars.h"
#include "ui.h"
#include "lv_port_disp.h"
#include "read_adc.h" // 假设需要用到ADC相关宏
#include "key.h"
#include "connet_check.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "stdlib.h"
#include "lv_port_indev.h"
#include "flash_storage.h"
#include "pairing_remote.h"
#include "st7789.h"
#include "ft6336u.h"
#include "beep.h"
// 假设开机密码是 "1234"
#define SYSTEM_PIN_CODE "1234"

// 声明原本是 static 的 UI 变量 (如果只在本文件用，保持 static)
static lv_obj_t *ui_ArcUltraFL;
static lv_obj_t *ui_ArcUltraFR;
static lv_obj_t *ui_ArcUltraRL;
static lv_obj_t *ui_ArcUltraRR;
static lv_obj_t *ui_TouchLabel;
static lv_obj_t *ui_PairingPopup = NULL;
static lv_obj_t *ui_LabelAddress = NULL;
static lv_obj_t *ui_UnlockPopup = NULL;
static lv_obj_t *ui_DebugLabel;
static lv_obj_t *ui_PairingHintPanel = NULL;
static uint16_t last_filtered_value = 0;
static uint16_t candidate_value = 0;
static uint8_t confirm_counter = 0;
static uint8_t last_displayed_battery = 255; // 用一个无效值初始化，以确保第一次会刷新
static uint8_t gui_status[24] = {0};         // UI 专属的影子快照缓冲区

// 声明私有辅助函数
static void Run_Boot_Animation(void);
static void Init_Needle_Anim(void);
static void Init_Ultrasonic_UI(void);
static void Update_Blade_Status(void);
static void Update_Mower_Battery_UI(void);
static void Update_Remote_Battery_UI(void);
static void Update_Signal_Status(void);
static void Update_Car_Status(void);
static void Update_Gear_UI(uint8_t gear);
static void Update_Lock_Status(void);
static void Update_Height_UI(void);
static void Update_GrassBag_Status(void);
static void Update_Ultrasonic_UI(void);
static void Update_System_Notification(void);
static void Anim_Radar_Opacity(void *var, int32_t v);
static void Create_Touch_Test_Panel(void);
static void Update_Touch_Debug_Info(void);
static void Update_Motor_Status_UI(void);
static void Update_BMS_Temp_UI(void);
static void Update_Bag_Full_UI(void);
void Update_OpMode_UI_Highlight(uint8_t selected_mode);
void data_update_cb(lv_timer_t *timer);
void anim_update_cb(lv_timer_t *timer);
void Update_Address_UI(void);
void Init_Address_Label(void);
void EventCb_PinCheck(lv_event_t *e);

LV_IMG_DECLARE(logo);
/* USER CODE BEGIN Header_StartGUITask */
/**
 * @brief Function implementing the GUITask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartGUITask */
void App_GUI_Main(void)
{
    /* USER CODE BEGIN StartGUITask */
    // ============================================================
    // <<<---  新增：开机加载系统设置（包括PIN码） --->>>
    // ============================================================
    AppSettings_t settings;
    if (FlashStorage_LoadSettings(&settings))
    {
        // 检查加载的 PIN 是否有效 (防止Flash是空的)
        if (settings.system_pin[0] >= '0' && settings.system_pin[0] <= '9')
        {
            strcpy(g_current_pin, settings.system_pin);
        }
    }

    // 1. 初始化硬件 (确保 SPI 和 GPIO 已经初始化)
    ST7789_Init();
    FT6336U_Init();
    // LVGL 核心初始化
    lv_init();

    // 接口初始化
    lv_port_disp_init();
    // 输入设备初始化
    lv_port_indev_init();

    /* 运行开机动画 */
    // 这个函数内部会负责：画Logo -> 开背光 -> 等待 -> 切换到主界面
    // Run_Boot_Animation();

    // ============================================================
    // 3. UI 界面立刻初始化 (替代原来的 Run_Boot_Animation)
    // ============================================================
    ui_init(); // 初始化由 SquareLine 生成的所有屏幕和控件

    // 将状态栏置顶，并刷新当前操作模式
    lv_obj_set_parent(ui_PanelStatusBar, lv_layer_top());
    Update_OpMode_UI_Highlight(g_sys_config.op_mode);

    // 初始化主界面的其他动态 UI 组件
    Init_Needle_Anim();
    Init_Ultrasonic_UI();
    Init_Address_Label();

    // 不看开机动画了，直接加载密码锁屏界面
    // 采用无动画模式瞬间显示
    lv_scr_load_anim(ui_ScreenLock, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    // 创建一个20ms的动画定时器
    lv_timer_create(anim_update_cb, 20, NULL);

    // 创建一个100ms的数据定时器
    lv_timer_create(data_update_cb, 50, NULL);

    /* Infinite loop */
    for (;;)
    {
        lv_tick_inc(5);     // 提供心跳
        lv_timer_handler(); // LVGL 自己会处理所有事
        osDelay(5);         // 释放CPU
    }
    /* USER CODE END StartGUITask */
}

void anim_update_cb(lv_timer_t *timer)
{
    // 每次动画刷新前，原子抓取一次全局状态的快照
    taskENTER_CRITICAL();
    memcpy(gui_status, status, 22);
    taskEXIT_CRITICAL();

    static uint32_t debug_cnt = 0;
    debug_cnt++;
// ============================================================
// 调试信息刷新 (仅在宏开启时编译)
// ============================================================
#if ENABLE_DEBUG_PANEL

    // ============================================================
    // <<<--- 【核心修改】 懒加载 (Lazy Loading) 调试面板 --->>>
    // ============================================================
    // 如果 ui_DebugLabel 还没有被创建，说明这是第一次进入主界面
    if (ui_DebugLabel == NULL)
    {
        // 只有当主界面(ui_Screen1)成为活动屏幕后，才创建
        if (lv_scr_act() == ui_Screen1)
        {
            ui_DebugLabel = lv_label_create(lv_scr_act());
            lv_obj_set_style_text_font(ui_DebugLabel, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(ui_DebugLabel, lv_color_black(), 0);
            lv_obj_set_style_bg_color(ui_DebugLabel, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(ui_DebugLabel, 180, 0);
            lv_obj_align(ui_DebugLabel, LV_ALIGN_TOP_LEFT, 0, 35);

            // 为了安全，把它强制置顶，防止被其他控件覆盖
            lv_obj_move_foreground(ui_DebugLabel);
        }
    }
    // 为了防止屏幕太小显示不下，我们将 status 分组显示
    // S[0-5]: 基础状态 (刀片,速度,电池等)
    // S[6-10]: 传感器 (草袋,超声波)
    // S[11-12]: 故障码

    if (ui_DebugLabel != NULL)
    {
        lv_label_set_text_fmt(ui_DebugLabel,
                              "Lp:%d Rst:%d\n"     // 循环数, 重启数
                              "TX OK:%d Fail:%d\n" // 发送诊断
                              "Joy: %d %d %d %d\n"
                              "Key: %d %d %d %d %d %d %d %d\n" // 显示8个按键
                              "Key8-10:%d %d %d\n"             // 显示新增的3个挡位
                              "RX Len:%d\n"                    // 接收长度
                              "St[0-5]: %d %d %d %d %d %d\n"   // 状态 0-5
                              "St[6-10]:%d %d %d %d %d\n"      // 状态 6-10
                              "St[11-12]:%d %d %d",            // 状态 11-12

                              debug_cnt,
                              restart_cnt,
                              g_tx_ok_cnt, g_tx_fail_cnt,

                              g_dbg_lx, g_dbg_ly,
                              g_dbg_rx, g_dbg_ry,
                              g_dbg_keys[0], g_dbg_keys[1], g_dbg_keys[2], g_dbg_keys[3],
                              g_dbg_keys[4], g_dbg_keys[5], g_dbg_keys[6], g_dbg_keys[7],
                              g_dbg_keys[8], g_dbg_keys[9], g_dbg_keys[10],

                              isr_rx_len,

                              gui_status[0], gui_status[1], gui_status[2], gui_status[3], gui_status[4], gui_status[5],
                              gui_status[6], gui_status[7], gui_status[8], gui_status[9], gui_status[10],
                              gui_status[11], gui_status[12], adc_dma_buffer[4]);
    }

    // 如果觉得上面太挤，可以用十六进制紧凑显示 status：
    /*
    lv_label_set_text_fmt(ui_DebugLabel,
          "Lp:%d Rst:%d\n"
          "TX Fail:%d\n"
          "S: %02X %02X %02X %02X %02X %02X\n"
          "   %02X %02X %02X %02X %02X %02X\n"
          "   %02X",
          debug_cnt, restart_cnt, g_tx_fail_cnt,
          gui_status[0], gui_status[1], gui_status[2], gui_status[3], gui_status[4], gui_status[5],
          gui_status[6], gui_status[7], gui_status[8], gui_status[9], gui_status[10], gui_status[11],
          gui_status[12]
    );
    */
#endif
    Update_Blade_Status();
}

void data_update_cb(lv_timer_t *timer)
{
    // 每次数据刷新前，原子抓取一次全局状态的快照
    taskENTER_CRITICAL();
    memcpy(gui_status, status, 22);
    taskEXIT_CRITICAL();

    Update_Mower_Battery_UI(); // 割草机电量

    // 更新遥控器电量 (来自 ADC)
    Update_Remote_Battery_UI();
    // 更新信号
    Update_Signal_Status();

    // 更新轮子颜色
    Update_Car_Status();

    // 更新速度挡位
    Update_Gear_UI(gui_status[1]);

    // 更新整车锁定状态
    Update_Lock_Status();

    // 更新刀盘高度显示
    Update_Height_UI();

    // 更新草袋状态
    Update_GrassBag_Status();

    // 刷新雷达数据
    Update_Ultrasonic_UI();
    // 刷新主界面的超声波状态显示
    Update_Ultrasonic_UI_State();

    // 更新系统弹窗
    Update_System_Notification();

    // 更新地址显示
    Update_Address_UI();

    // 更新 4 个轮子的温度与功率双行显示
    Update_Motor_Status_UI();

    // 更新 BMS 温度 UI
    Update_BMS_Temp_UI();

    // 更新草袋满状态 UI
    Update_Bag_Full_UI();
}

// 动画完成后的回调函数
static void boot_anim_ready_cb(lv_anim_t *a)
{
    // 动画结束，现在加载主界面
    ui_init();

    lv_obj_set_parent(ui_PanelStatusBar, lv_layer_top());

    Update_OpMode_UI_Highlight(g_sys_config.op_mode);
    // 使用带动画的切换（例如淡入淡出），或者无动画切换
    // 不加载 Screen1，而是加载密码锁屏界面
    lv_scr_load_anim(ui_ScreenLock, LV_SCR_LOAD_ANIM_FADE_IN, 0, 0, false);
}

/**
 * @brief 执行开机动画
 */
void Run_Boot_Animation(void)
{
    // 1. 创建屏幕
    lv_obj_t *scr_logo = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_logo, lv_color_white(), 0);

    // 2. 创建 Logo
    lv_obj_t *img_logo = lv_img_create(scr_logo);
    lv_img_set_src(img_logo, &logo);
    // 如果觉得图片太小，可以静态放大 1.5倍 (256是原大小, 384是1.5倍)
    lv_img_set_zoom(img_logo, 300);
    lv_obj_align(img_logo, LV_ALIGN_CENTER, 0, -20); // 往上挪

    // 3. 创建进度条
    lv_obj_t *bar = lv_bar_create(scr_logo);
    lv_obj_set_size(bar, 160, 10);             // 宽160，高10
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 80); // 放在 Logo 下方
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF); // 初始值为0

    // 设置进度条颜色 (绿色)
    lv_obj_set_style_bg_color(bar, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);

    // 4. 创建进度条动画
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_values(&a, 0, 100);                                // 从 0 走到 100
    lv_anim_set_time(&a, 2200);                                    // 耗时 2秒
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_bar_set_value); // 绑定函数

    lv_anim_set_ready_cb(&a, boot_anim_ready_cb);
    // 5. 加载屏幕、渲染
    lv_scr_load(scr_logo);

    // 6. 开始跑进度条
    lv_anim_start(&a);
}

// 辅助函数：批量设置4个轮子的颜色
void Set_Wheels_Color(lv_color_t color)
{
    // 检查对象是否存在，防止空指针崩溃
    if (ui_WheelFL)
    {
        lv_obj_set_style_bg_color(ui_WheelFL, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ui_WheelFL, 150, LV_PART_MAIN);
    }

    if (ui_WheelFR)
    {
        lv_obj_set_style_bg_color(ui_WheelFR, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ui_WheelFR, 150, LV_PART_MAIN);
    }

    if (ui_WheelRL)
    {
        lv_obj_set_style_bg_color(ui_WheelRL, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ui_WheelRL, 150, LV_PART_MAIN);
    }

    if (ui_WheelRR)
    {
        lv_obj_set_style_bg_color(ui_WheelRR, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ui_WheelRR, 150, LV_PART_MAIN);
    }
}
// 根据车辆状态更新轮子颜色
void Update_Car_Status(void)
{
    // 根据 gui_status[11] 决定颜色
    // 对应你之前的逻辑

    lv_color_t target_color;
    if (gui_status[11] == 0 || gui_status[11] == 8 || gui_status[11] == 9 || gui_status[11] == 13 || gui_status[11] == 14 || gui_status[11] == 15)
    {
        // 绿色 (正常)
        target_color = lv_palette_main(LV_PALETTE_GREEN);
    }
    else if (gui_status[11] == 1 || gui_status[11] == 4)
    {
        // 红色 (故障/停止?)
        target_color = lv_palette_main(LV_PALETTE_RED);
    }
    else if (gui_status[11] == 2)
    {
        // 蓝绿色 (GBLUE -> Cyan)
        target_color = lv_palette_main(LV_PALETTE_CYAN);
    }
    else if (gui_status[11] == 10)
    {
        // 白色
        target_color = lv_color_white();
    }
    else if (gui_status[11] == 11)
    {
        // 青色
        target_color = lv_palette_main(LV_PALETTE_CYAN);
    }
    else if (gui_status[11] == 12)
    {
        // 紫色 (MAGENTA)
        target_color = lv_palette_main(LV_PALETTE_PURPLE);
    }
    else
    {
        // 默认红色 (还没有状态)
        target_color = lv_palette_main(LV_PALETTE_RED);
    }

    // 应用颜色
    Set_Wheels_Color(target_color);
}

/**
 * @brief 更新挡位显示
 * @param gear 1:L(绿), 2:M(黄), 3:H(红)
 */
void Update_Gear_UI(uint8_t gear)
{
    // 1. 定义颜色
    lv_color_t color_gray = lv_palette_main(LV_PALETTE_GREY);
    lv_color_t color_green = lv_palette_main(LV_PALETTE_LIGHT_GREEN);
    lv_color_t color_yellow = lv_palette_main(LV_PALETTE_ORANGE);
    lv_color_t color_red = lv_palette_main(LV_PALETTE_RED);

    // 2. 先全部重置为灰色 (熄灭状态)
    if (ui_ArcGearL)
        lv_obj_set_style_arc_color(ui_ArcGearL, color_gray, LV_PART_MAIN);
    if (ui_ArcGearM)
        lv_obj_set_style_arc_color(ui_ArcGearM, color_gray, LV_PART_MAIN);
    if (ui_ArcGearH)
        lv_obj_set_style_arc_color(ui_ArcGearH, color_gray, LV_PART_MAIN);

    // 2. 指针角度控制 (单位：0.1度)
    int16_t target_angle = 0;

    switch (gear)
    {
    case 1: // L档 (左)
        if (ui_ArcGearL)
            lv_obj_set_style_arc_color(ui_ArcGearL, color_green, LV_PART_MAIN);
        target_angle = -300; // 向左 30.0 度
        break;

    case 2: // M档 (中)
        if (ui_ArcGearM)
            lv_obj_set_style_arc_color(ui_ArcGearM, color_yellow, LV_PART_MAIN);
        target_angle = 0; // 中间 0 度
        break;

    case 3: // H档 (右)
        if (ui_ArcGearH)
            lv_obj_set_style_arc_color(ui_ArcGearH, color_red, LV_PART_MAIN);
        target_angle = 300; // 向右 30.0 度
        break;

    default:              // N档或其他
        target_angle = 0; // 可以让它指到最左边或者趴下
                          // 可选：把指针变灰，表示无效
        // if (ui_PanelNeedle)
        //   lv_obj_set_style_bg_color(ui_PanelNeedle, lv_palette_main(LV_PALETTE_GREY), 0);
        break;
    }

    // 3. 应用旋转 (带动画效果会更帅！)
    // 如果 ui_PanelNeedle 存在
    // if (ui_PanelNeedle)
    // {
    //   // 直接设置 (无动画)
    //   // lv_obj_set_style_transform_angle(ui_PanelNeedle, target_angle, 0);

    //   // --- 进阶：带平滑动画 ---
    //   // 我们利用简单的过渡动画，或者直接用属性设置
    //   // 为了简单，这里先用直接设置。如果你想要指针慢慢滑过去，需要创建 lv_anim_t
    //   // 这里演示最简单的直接设置，配合 LVGL 自带的过渡样式其实也能动
    //   lv_obj_set_style_transform_angle(ui_PanelNeedle, target_angle, LV_PART_MAIN | LV_STATE_DEFAULT);
    // }
}

/**
 * @brief 更新信号图标颜色
 */
void Update_Signal_Status(void)
{
    // 1. 获取通信状态 (1:连接, 0:断开)
    // 假设 CheckCommunicationStatus() 返回 1 表示连接正常
    uint8_t is_connected = CheckCommunicationStatus();

    // 2. 检查 UI 对象是否存在 (防止空指针)
    if (ui_LabelSignal == NULL)
        return;

    // 3. 根据状态改变颜色
    if (is_connected)
    {
        // 连接：绿色
        lv_obj_set_style_text_color(ui_LabelSignal, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);

        // 可选：如果连接时想显示满格信号图标，断开显示带叉图标
        // lv_label_set_text(ui_Label_Signal, LV_SYMBOL_WIFI);
    }
    else
    {
        // 断开：红色
        lv_obj_set_style_text_color(ui_LabelSignal, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);

        // 可选：断开时显示另一个图标 (例如 Close)
        // lv_label_set_text(ui_LabelSignal, LV_SYMBOL_CLOSE);
    }
}

/**
 * @brief 计算遥控器电池百分比
 */
uint8_t Calc_Remote_Bat_Pct(uint16_t new_value)
{
    // 【核心修复 1：拦截未就绪的 DMA 数据】
    // 正常电池的 ADC 绝对不可能低于 2000。如果读到 0 或异常小的值，
    // 说明 DMA 还没准备好，直接返回 255（无效标记），保护系统不被 0 污染。
    if (new_value < 2000)
    {
        return 255;
    }

    static float smooth_adc = 0;
    static uint16_t locked_adc = 0;

    // 初始化滤波器起点
    if (smooth_adc == 0)
    {
        smooth_adc = new_value;
        locked_adc = new_value;
    }

    // 1. 强效平滑滤波 (95% 历史权重)
    smooth_adc = (smooth_adc * 0.95f) + ((float)new_value * 0.05f);

    // 2. 迟滞死区：变化超过 5 个 ADC 精度 (~0.8%) 才认可更新
    if (abs((int)smooth_adc - (int)locked_adc) > 5)
    {
        locked_adc = (uint16_t)smooth_adc;
    }

    // 3. 使用锁定值进行百分比计算
    uint16_t calc_adc = locked_adc;
    uint8_t percentage = 0;

    if (calc_adc <= 3180)
    {
        percentage = 0;
    }
    else if (calc_adc >= 3723)
    {
        percentage = 100;
    }
    else
    {
        percentage = (uint8_t)(((long)(calc_adc - 3180) * 100) / (3723 - 3180));
    }

    // 【核心修复 2：彻底移除 confirm_counter】
    // IIR + Deadzone 已经足够平滑，直接返回结果，打破防抖死锁！
    return percentage;
}

/**
 * @brief 更新遥控器电量 UI
 */
void Update_Remote_Battery_UI(void)
{

    if (ui_LabelHandleBattery == NULL || ui_BarHandleBattery == NULL)
    {
        return;
    }
    // 1. 获取百分比
    uint8_t pct = Calc_Remote_Bat_Pct(adc_dma_buffer[4]);

    // 数据未就绪时跳过刷新
    if (pct == 255)
    {
        return;
    }

    bool need_update = false;
    int diff = (int)pct - (int)last_displayed_battery;

    // 2. 单向防跳变决策逻辑
    if (last_displayed_battery == 255)
    {
        // 第一次成功拿到有效数据，无条件刷新
        need_update = true;
    }
    else if (diff <= -1)
    {
        // 正常放电：只要电量下降就实时更新 (利用滤波保证了下降也很平滑)
        need_update = true;
    }
    else if (diff >= 5)
    {
        // 充电/换电池：电量突然上升 5% 以上才被认为是真正回升
        need_update = true;
    }

    // 3. 执行刷新
    if (need_update)
    {
        // 更新进度条 (Bar)
        if (ui_BarHandleBattery)
        {
            lv_bar_set_value(ui_BarHandleBattery, pct, LV_ANIM_ON);
            // 低电量变色
            if (pct < 20)
                lv_obj_set_style_bg_color(ui_BarHandleBattery, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);
            else
                lv_obj_set_style_bg_color(ui_BarHandleBattery, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
        }

        // 更新文字 (Label)
        if (ui_LabelHandleBattery)
        {
            lv_label_set_text_fmt(ui_LabelHandleBattery, "%d%%", pct);
        }

        // 同步历史缓存
        last_displayed_battery = pct;
    }
}

// void Update_Remote_Battery_UI(void)
// {
//     // ==============================================================
//     // 【核心修复 1】：UI 生命周期护盾
//     // 如果主界面还没加载出来（播放开机动画中），直接返回！
//     // 绝对不允许提前消耗掉百分比的变化事件！
//     // ==============================================================
//     if (ui_LabelHandleBattery == NULL || ui_BarHandleBattery == NULL) {
//         return;
//     }

//     // ⚠️ 注意：这里删除了内部的 static last_displayed_battery
//     // 现在使用的是文件顶部定义的那个全局 last_displayed_battery

//     uint8_t pct = Calc_Remote_Bat_Pct(adc_dma_buffer[4]);

//     // 如果 DMA 返回 255，说明数据还没就绪，跳过不刷新
//     if (pct == 255) return;

//     // 只要过滤后的有效百分比发生了变化，立刻刷新 UI
//     if (pct != last_displayed_battery)
//     {
//         // 刷新进度条
//         lv_bar_set_value(ui_BarHandleBattery, pct, LV_ANIM_ON);

//         // 变色逻辑：低电量(<20%)变红，否则绿色
//         if (pct < 20) {
//             lv_obj_set_style_bg_color(ui_BarHandleBattery, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);
//         } else {
//             lv_obj_set_style_bg_color(ui_BarHandleBattery, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
//         }

//         // 刷新文字
//         lv_label_set_text_fmt(ui_LabelHandleBattery, "%d%%", pct);

//         // ==============================================================
//         // 【核心修复 2】：同步历史记录
//         // 只有当 UI 真正被刷新了之后，才允许保存当前电量。
//         // 这样不仅解决了 0% 卡死，还让底部的低电量蜂鸣器报警恢复了正常！
//         // ==============================================================
//         last_displayed_battery = pct;
//     }
// }

// 割草机电池电量UI更新
void Update_Mower_Battery_UI(void)
{
    // 1. UI 生命周期护盾
    if (ui_LabelCarBattery == NULL || ui_BarCarBattery == NULL)
    {
        return;
    }

    static uint8_t last_mower_bat = 255; // 历史缓存

    // 2. 获取状态 (只读，绝对不能修改 status 数组！)
    uint8_t current_bat = gui_status[3];
    uint8_t is_connected = CheckCommunicationStatus();

    // 3. 拦截无效数据：
    // - 未连接
    // - 收到无效标记 (255)
    // - 刚开机还未收到任何数据 (0 且 last_receive_timestamp == 0)
    if (!is_connected || current_bat == 255 || (current_bat == 0 && last_receive_timestamp == 0))
    {
        if (last_mower_bat != 255)
        {
            lv_label_set_text(ui_LabelCarBattery, "--%");
            lv_bar_set_value(ui_BarCarBattery, 0, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(ui_BarCarBattery, lv_palette_main(LV_PALETTE_GREY), LV_PART_INDICATOR);
            last_mower_bat = 255; // 更新缓存
        }
        return;
    }

    // 4. 数据容错 (临时变量保护，最高显示100)
    if (current_bat > 100)
    {
        current_bat = 100;
    }

    // 5. 只有数据变化时才刷新屏幕
    if (current_bat != last_mower_bat)
    {
        lv_bar_set_value(ui_BarCarBattery, current_bat, LV_ANIM_ON);

        // 低电量变红，正常变绿
        if (current_bat < 20)
        {
            lv_obj_set_style_bg_color(ui_BarCarBattery, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);
        }
        else
        {
            lv_obj_set_style_bg_color(ui_BarCarBattery, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
        }

        lv_label_set_text_fmt(ui_LabelCarBattery, "%d%%", current_bat);

        last_mower_bat = current_bat;
    }
}
// 初始化指针动画过渡效果
void Init_Needle_Anim(void)
{
    static lv_style_transition_dsc_t trans;
    static lv_style_prop_t props[] = {LV_STYLE_TRANSFORM_ANGLE, 0};

    // 初始化过渡：时间 500ms，缓动效果
    lv_style_transition_dsc_init(&trans, props, lv_anim_path_ease_out, 500, 0, NULL);

    // 给指针添加这个过渡样式
    if (ui_PanelNeedle)
    {
        lv_obj_set_style_transition(ui_PanelNeedle, &trans, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

/**
 * @brief 更新刀盘状态 (颜色 + 旋转 + 文字)
 */
void Update_Blade_Status(void)
{
    // 1. 安全检查
    if (ui_ImageBlade == NULL || ui_LabelBladeSpeed == NULL)
        return;

    // =================================================
    // A. 颜色控制逻辑
    // =================================================
    lv_color_t target_color;

    // 解锁 (1) 且 无故障 (0) -> 绿色
    if (gui_status[0] == 1 && gui_status[11] == 0)
    {
        target_color = lv_palette_main(LV_PALETTE_GREEN);
    }
    // 锁定 (0) 或 有故障 -> 红色
    else
    {
        target_color = lv_palette_main(LV_PALETTE_RED);
    }

    // 应用颜色 (强度 255 表示完全覆盖白色)
    lv_obj_set_style_img_recolor(ui_ImageBlade, target_color, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(ui_ImageBlade, 255, LV_PART_MAIN);

    // =================================================
    // B. 文字显示逻辑 (N / L / M / H)
    // =================================================
    const char *speed_text = "N"; // 默认显示 N

    // 只有在解锁状态下，才显示速度档位
    if (gui_status[0] == 1)
    {
        switch (gui_status[2])
        {
        case 1:
            speed_text = "低速";
            break; // 低速
        case 2:
            speed_text = "中速";
            break; // 中速
        case 3:
            speed_text = "高速";
            break; // 高速
        default:
            speed_text = "N";
            break; // 0 或其他 -> N
        }
    }
    // 如果 gui_status[0] == 0 (锁定)，保持默认的 "N"

    // 更新文字
    lv_label_set_text(ui_LabelBladeSpeed, speed_text);

    // 可选：让文字颜色也跟着变 (解锁绿，锁定红)
    lv_obj_set_style_text_color(ui_LabelBladeSpeed, target_color, 0);

    // =================================================
    // C. 旋转动画逻辑 (让它转起来)
    // =================================================
    static int16_t blade_angle = 0;
    int16_t speed_increment = 0;

    // 只有解锁且有速度时才旋转
    if (gui_status[0] == 1)
    {
        switch (gui_status[2])
        {
        case 1:
            speed_increment = 60;
            break; // 慢转
        case 2:
            speed_increment = 120;
            break; // 中转
        case 3:
            speed_increment = 240;
            break; // 快转
        default:
            speed_increment = 0;
            break;
        }
    }

    if (speed_increment > 0)
    {
        blade_angle += speed_increment;
        if (blade_angle >= 3600)
            blade_angle -= 3600; // 0-3600 (0.1度单位)
        lv_img_set_angle(ui_ImageBlade, blade_angle);
    }
}

/**
 * @brief 更新整车锁定/故障状态
 * 逻辑：gui_status[11] 为 1~4 时显示红色锁定，否则显示绿色解锁
 */
void Update_Lock_Status(void)
{
    if (ui_ImageLock == NULL)
        return;

    uint8_t is_connected = CheckCommunicationStatus();

    // ==========================================================
    //            使用状态机替代 HAL_GetTick()
    // ==========================================================
    static uint16_t disconnect_ms_counter = 0; // 静态计数器，单位：毫秒
    bool is_truly_lost = false;

    if (is_connected)
    {
        // 如果连接恢复，立刻清零计数器
        disconnect_ms_counter = 0;
    }
    else
    {
        // 如果断开连接，开始累加时间
        // 假设这个函数被外层每 100ms 调用一次
        disconnect_ms_counter += 100;
    }

    // 判断是否真正丢失连接（超过1.5秒）
    if (disconnect_ms_counter > 1500)
    {
        is_truly_lost = true;
    }

    // 2. 只有真正丢失连接才强制锁定
    if (is_truly_lost)
    {
        lv_img_set_src(ui_ImageLock, &ui_img_lock_png);
        lv_obj_set_style_img_recolor(ui_ImageLock, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(ui_ImageLock, 255, LV_PART_MAIN);
        return;
    }

    // 3. 如果连接正常，或者只是短暂断开，保持显示 gui_status[11] 的状态
    // (这样在重启 DMA 的那 1秒内，图标会保持上一次的状态，不会闪烁)
    uint8_t fault_code = gui_status[11];

    if (fault_code >= 1 && fault_code <= 4)
    {
        lv_img_set_src(ui_ImageLock, &ui_img_lock_png);
        lv_obj_set_style_img_recolor(ui_ImageLock, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(ui_ImageLock, 255, LV_PART_MAIN);
    }
    else
    {
        lv_img_set_src(ui_ImageLock, &ui_img_unlock_png);
        lv_obj_set_style_img_recolor(ui_ImageLock, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(ui_ImageLock, 255, LV_PART_MAIN);
    }
}

/**
 * @brief 更新刀盘高度显示 (带模式提示)
 */
void Update_Height_UI(void)
{
    // 1. 安全检查
    if (ui_BarHeight == NULL || ui_LabelHeightValue == NULL)
        return;

    // 2. 获取原始高度值
    uint8_t raw_height = gui_status[5];

    // 3. 设置进度条值 (带动画)
    lv_bar_set_value(ui_BarHeight, raw_height, LV_ANIM_ON);

    // 4. 更新文字
    float height_cm = (float)raw_height / 10.0f;
    // 3. 【核心修改】使用整数运算代替 %f
    // 格式： "3" + "." + "5" + " cm"
    lv_label_set_text_fmt(ui_LabelHeightValue, "%d.%d cm",
                          raw_height / 10,  // 整数部分
                          raw_height % 10); // 小数部分
}

/**
 * @brief 初始化超声波雷达UI组件
 */
void Init_Ultrasonic_UI(void)
{
    // 假设父对象是 ui_Car (车辆容器) 或者 ui_Screen1
    lv_obj_t *parent = ui_Screen1;

    // --- 通用样式配置 ---
    static lv_style_t style_radar;
    lv_style_init(&style_radar);
    lv_style_set_arc_width(&style_radar, 6);      // 线条宽度
    lv_style_set_arc_rounded(&style_radar, true); // 圆头
    lv_style_set_bg_opa(&style_radar, 0);         // 隐藏背景槽

    // ============================================================
    // 布局逻辑：从左到右依次排列 (扇形覆盖车头)
    // 中心线是 270度
    // ============================================================

    // --- 1. 最左侧 (复用 FL 变量) ---
    // 范围：195° ~ 230° (偏左侧)
    ui_ArcUltraFL = lv_arc_create(parent);
    lv_obj_add_style(ui_ArcUltraFL, &style_radar, 0);
    lv_arc_set_bg_angles(ui_ArcUltraFL, 200, 225);
    lv_obj_remove_style(ui_ArcUltraFL, NULL, LV_PART_KNOB);
    lv_obj_set_style_opa(ui_ArcUltraFL, 0, LV_PART_INDICATOR); // 让蓝色的进度条透明消失
    lv_obj_clear_flag(ui_ArcUltraFL, LV_OBJ_FLAG_CLICKABLE);
    // 注意：坐标改为 0,0 居中，或者 0, -20 稍微往上提一点，根据你的车图中心调整
    lv_obj_align(ui_ArcUltraFL, LV_ALIGN_CENTER, 0, 85);

    // --- 2. 中间偏左 (复用 RL 变量，当作前向左探头) ---
    // 范围：235° ~ 265° (正前方偏左)
    ui_ArcUltraRL = lv_arc_create(parent);
    lv_obj_add_style(ui_ArcUltraRL, &style_radar, 0);
    lv_arc_set_bg_angles(ui_ArcUltraRL, 235, 260);
    lv_obj_remove_style(ui_ArcUltraRL, NULL, LV_PART_KNOB);
    lv_obj_set_style_opa(ui_ArcUltraRL, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(ui_ArcUltraRL, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(ui_ArcUltraRL, LV_ALIGN_CENTER, 0, 85);

    // --- 3. 中间偏右 (复用 RR 变量，当作前向右探头) ---
    // 范围：275° ~ 305° (正前方偏右)
    ui_ArcUltraRR = lv_arc_create(parent);
    lv_obj_add_style(ui_ArcUltraRR, &style_radar, 0);
    lv_arc_set_bg_angles(ui_ArcUltraRR, 280, 305);
    lv_obj_remove_style(ui_ArcUltraRR, NULL, LV_PART_KNOB);
    lv_obj_set_style_opa(ui_ArcUltraRR, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(ui_ArcUltraRR, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(ui_ArcUltraRR, LV_ALIGN_CENTER, 0, 85);

    // --- 4. 最右侧 (复用 FR 变量) ---
    // 范围：310° ~ 345° (偏右侧)
    ui_ArcUltraFR = lv_arc_create(parent);
    lv_obj_add_style(ui_ArcUltraFR, &style_radar, 0);
    lv_arc_set_bg_angles(ui_ArcUltraFR, 315, 340);
    lv_obj_remove_style(ui_ArcUltraFR, NULL, LV_PART_KNOB);
    lv_obj_set_style_opa(ui_ArcUltraFR, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(ui_ArcUltraFR, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(ui_ArcUltraFR, LV_ALIGN_CENTER, 0, 85);

    // --- 为4个雷达添加呼吸动画 ---
    // 定义一个临时的动画描述符
    lv_anim_t a;
    lv_anim_init(&a);

    // 动画参数：透明度从 150 (半透) 变到 255 (不透)
    lv_anim_set_values(&a, 150, 255);
    lv_anim_set_time(&a, 800);                             // 周期 0.8秒，看起来比较急促，像雷达
    lv_anim_set_playback_time(&a, 800);                    // 回放时间
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); // 无限循环
    lv_anim_set_exec_cb(&a, Anim_Radar_Opacity);           // 绑定回调

    // 对4个对象分别启动动画
    // 为了更像“声波”，我们可以让它们稍微错开一点时间(可选)

    lv_anim_set_var(&a, ui_ArcUltraFL);
    lv_anim_start(&a);

    lv_anim_set_delay(&a, 100); // 错开 100ms
    lv_anim_set_var(&a, ui_ArcUltraFR);
    lv_anim_start(&a);

    lv_anim_set_delay(&a, 200);
    lv_anim_set_var(&a, ui_ArcUltraRL);
    lv_anim_start(&a);

    lv_anim_set_delay(&a, 300);
    lv_anim_set_var(&a, ui_ArcUltraRR);
    lv_anim_start(&a);
}

/**
 * @brief 更新单个雷达圆弧的状态
 * @param arc_obj 圆弧对象
 * @param distance 距离 (cm)
 */
void Update_Single_Radar(lv_obj_t *arc_obj, uint8_t distance)
{

    if (arc_obj == NULL)
        return;

    lv_color_t color;
    int16_t size_offset;

    // --- 1. 颜色逻辑 (依然保持三色预警，这样最直观) ---
    if (distance <= 30)
    {
        color = lv_palette_main(LV_PALETTE_RED); // 危险：红
    }
    else if (distance <= 70)
    {
        color = lv_palette_main(LV_PALETTE_ORANGE); // 警告：橙
    }
    else
    {
        color = lv_palette_main(LV_PALETTE_GREEN); // 安全：绿
    }

    // --- 2. 动态尺寸计算 (核心修改) ---
    // 我们设定屏幕上显示的“最大感应距离”为 100cm (超过100cm圆弧就不再变大了)
    // 同时也设定一个最大像素偏移量，防止圆弧飞出屏幕 (例如最大向外扩 60px)

    int16_t disp_dist = distance;
    if (disp_dist > 100)
        disp_dist = 100; // 限制最大显示距离
    if (disp_dist < 0)
        disp_dist = 0;

    // 比例公式：像素偏移 = (当前距离 / 100) * 最大像素(60)
    // 这样距离每变 1cm，圆弧就动一点点，非常丝滑
    size_offset = (disp_dist * 60) / 100;

    // 特殊处理：如果距离太远(比如255/空闲)，我们可以隐藏圆弧，或者显示最大
    if (distance > 200)
    {
        // 选择A: 隐藏
        lv_obj_add_flag(arc_obj, LV_OBJ_FLAG_HIDDEN);
        return;

        // 选择B: 显示为最大且灰色 (待机状态)
        // color = lv_palette_main(LV_PALETTE_GREY);
        // size_offset = 60;
    }
    else
    {
        lv_obj_clear_flag(arc_obj, LV_OBJ_FLAG_HIDDEN);
    }

    // --- 3. 应用属性 ---
    lv_obj_set_style_arc_color(arc_obj, color, LV_PART_MAIN);

    // 应用计算好的平滑尺寸
    lv_obj_set_size(arc_obj, CAR_WIDTH_BASE + size_offset, CAR_HEIGHT_BASE + size_offset);
}

/**
 * @brief 更新函数 (注意映射关系)
 */
void Update_Ultrasonic_UI(void)
{
    // gui_status[7]~[10] 对应 4 个探头的数据
    // 假设你的 status 数组顺序是从左到右：d1, d2, d3, d4

    // 1. 最左 (FL对象) <- gui_status[7] (d1)
    Update_Single_Radar(ui_ArcUltraFL, gui_status[7]);

    // 2. 中左 (RL对象) <- gui_status[8] (d2)
    // 注意：这里复用了RL对象来显示前向中间的探头
    Update_Single_Radar(ui_ArcUltraRL, gui_status[8]);

    // 3. 中右 (RR对象) <- gui_status[9] (d3)
    // 注意：这里复用了RR对象
    Update_Single_Radar(ui_ArcUltraRR, gui_status[9]);

    // 4. 最右 (FR对象) <- gui_status[10] (d4)
    Update_Single_Radar(ui_ArcUltraFR, gui_status[10]);
}

// 动画回调：改变对象的透明度，模拟呼吸/声波淡入淡出
void Anim_Radar_Opacity(void *var, int32_t v)
{
    lv_obj_set_style_arc_opa((lv_obj_t *)var, v, LV_PART_MAIN);
}
/**
 * @brief 统一的系统通知中心 (配对/解锁/故障/断联)
 * @note  复用 ui_PanelError 和 ui_LabelError
 */
void Update_System_Notification(void)
{
    if (ui_PanelError == NULL || ui_LabelError == NULL)
        return;

    // --- 1. 获取各种状态 ---
    bool is_pairing = g_is_pairing_mode; // 配对状态
    uint8_t unlock_req = gui_status[12]; // 解锁请求 (1=请求解锁)
    uint8_t err_code = gui_status[11];   // 故障码
    uint8_t is_connected = CheckCommunicationStatus();

    // ==========================================================
    //            使用状态机替代 HAL_GetTick()
    // ==========================================================
    static uint16_t disconnect_ms_counter = 0; // 静态计数器，单位：毫秒

    if (is_connected)
    {
        // 如果连接恢复，立刻清零计数器，回到初始状态
        disconnect_ms_counter = 0;
    }
    else
    {
        // 如果断开连接，开始累加时间
        // 假设这个函数被外层每 100ms 调用一次
        // 为了防止无限累加导致溢出（虽然要很久），可以加个上限
        if (disconnect_ms_counter <= 2000)
        { // 2秒已经远超我们的阈值
            disconnect_ms_counter += 100;
        }
    }

    // 判断是否真正丢失连接（超过1.5秒）
    bool is_truly_lost = (disconnect_ms_counter > 1500);

    // ============================================================
    // <<<--- 低电量报警边沿触发与计时逻辑 --->>>
    // ============================================================
    static uint32_t mower_bat_warn_tick = 0;
    static bool mower_bat_warned = false;

    static uint32_t remote_bat_warn_tick = 0;
    static bool remote_bat_warned = false;

    bool show_mower_bat_warn = false;
    bool show_remote_bat_warn = false;

    // A. 割草机低电量判断 (gui_status[3])
    // 255 (0xFF) 是我们设定的无效值，需要过滤掉
    if (gui_status[3] <= 20 && gui_status[3] != 255)
    {
        if (!mower_bat_warned)
        {
            mower_bat_warn_tick = HAL_GetTick(); // 记录初次跌破 20% 的时间
            mower_bat_warned = true;
        }
        // 持续显示 60 秒钟 (60000ms)
        if (HAL_GetTick() - mower_bat_warn_tick < 60000)
        {
            show_mower_bat_warn = true;
        }
    }
    else
    {
        mower_bat_warned = false; // 电量恢复(如充上电了)后重置防抖锁
    }

    // B. 遥控器低电量判断 (复用你在本文件中定义的 last_displayed_battery)
    if (last_displayed_battery <= 20 && last_displayed_battery != 255)
    {
        if (!remote_bat_warned)
        {
            remote_bat_warn_tick = HAL_GetTick();
            remote_bat_warned = true;
        }
        // 持续显示 60 秒钟
        if (HAL_GetTick() - remote_bat_warn_tick < 60000)
        {
            show_remote_bat_warn = true;
        }
    }
    else
    {
        remote_bat_warned = false; // 充电后重置
    }

    // --- 3. 优先级决策逻辑 ---

    // 定义样式配置变量
    const char *msg_text = "";
    bool show_panel = false;

    // >>> 优先级 1: 配对模式 <<<
    if (is_pairing)
    {
        msg_text = "正在连接...\n请稍候";
        show_panel = true;
    }
    // >>> 优先级 2: 通信丢失 <<<
    // 注意：只有在非配对模式下才报丢失
    else if (is_truly_lost)
    {
        msg_text = "通信丢失!\n请检查连接";
        show_panel = true;
    }
    // >>> 优先级 3: 硬件故障 <<<
    else if (err_code != 0 && (err_code < 10 || err_code > 12))
    {
        // 样式：白底红框 (保持原有风格)
        show_panel = true;

        switch (err_code)
        {
        case 1:
            msg_text = "碰撞杆触发!\n请检查安全后\n手动解锁";
            break;
        case 2:
            msg_text = "急停按钮触发!\n请旋出急停钮";
            break;
        case 3:
            msg_text = "检测到倾翻!\n请扶正车辆";
            break;
        case 4:
            msg_text = "通信异常!\n手动解锁";
            break;
        case 5:
            msg_text = "连接成功!\n请手动解锁";
            break;
        case 7:
            msg_text = "割草机初始化成功\n请手动解锁";
            break;
        case 8:
            msg_text = "草袋正在打开\n请注意安全";
            break;

        case 9:
            msg_text = "倒草完成"; // 显示2秒
            break;

        case 13:
            msg_text = "草袋正在关闭...";
            break;

            // case 14:
            //     msg_text = "草袋已集满\n请倾倒草袋";
            //     break;

        case 15:
            msg_text = "正在进行刀盘高度初始化\n请等待约11s...";
            break;

        default:
            msg_text = "未知故障";
            break;
        }
    }
    // ============================================================
    // 低电量显示 (排在硬件故障之后，作为第4优先级)
    // ============================================================
    // else if (show_remote_bat_warn)
    // {
    //     msg_text = "遥控器电量过低!\n请立刻充电";
    //     show_panel = true;
    // }
    else if (show_mower_bat_warn)
    {
        msg_text = "割草机电量过低!\n请立即返航充电";
        show_panel = true;
    }
    // >>> 优先级 4: 解锁请求 <<<
    else if (unlock_req == 1)
    {
        msg_text = "是否解锁割刀?\n请按 Key8 确认";
        show_panel = true;
    }

    // ============================================================
    // 定速巡航弹窗逻辑 (只有在安全、无故障的情况下才允许响应)
    // ============================================================
    if (g_dbg_keys[6] == 1) // Key7 代表定速巡航键短按
    {
        // 【核心校验】：只有当前没有任何系统级报错/提示时，才允许进入定速巡航
        if (show_panel == false)
        {
            if (g_hide_cruise_popup_this_session == false)
            {
                // 如果用户没有选择“不再提示”，就显示弹窗
                if (ui_PanelCruisePopup != NULL)
                {
                    lv_obj_clear_flag(ui_PanelCruisePopup, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_move_foreground(ui_PanelCruisePopup); // 置于顶层
                }
            }
            else
            {
                // 如果用户已经选了“不再提示”，就直接使能底层功能
                Enable_Cruise_Control();
            }
        }
        else
        {
            // 架构师提示：这里加一声蜂鸣器短促报警，提醒用户当前有故障，拒绝巡航！
            // Play_Beep(BEEP_ALARM_FAULT);
        }
    }
    // --- 4. 执行 UI 更新 ---
    if (show_panel)
    {
        // 更新文字
        lv_label_set_text(ui_LabelError, msg_text);

        // 显示并置顶
        lv_obj_clear_flag(ui_PanelError, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ui_PanelError);

        // 2. 【安全机制】：如果系统突然弹出了高优先级错误，必须强制关掉正在显示的巡航弹窗
        if (ui_PanelCruisePopup != NULL && !lv_obj_has_flag(ui_PanelCruisePopup, LV_OBJ_FLAG_HIDDEN))
        {
            lv_obj_add_flag(ui_PanelCruisePopup, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        // 隐藏
        lv_obj_add_flag(ui_PanelError, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 创建触摸测试面板
 */
void Create_Touch_Test_Panel(void)
{
    // 1. 修改高度：从 80 改为 150，确保能放下所有文字
    lv_obj_t *panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(panel, 180, 150); // <--- 宽度改180，高度改150
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);

    // 样式保持不变
    lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(panel, 150, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_palette_main(LV_PALETTE_GREEN), 0);

    // 2. 创建 Label
    ui_TouchLabel = lv_label_create(panel);

    // 初始文字也加上 IRQ，方便调试
    lv_label_set_text(ui_TouchLabel, "Touch Test\nX: --\nY: --\nIRQ: --");

    lv_obj_set_style_text_color(ui_TouchLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(ui_TouchLabel, &lv_font_montserrat_14, 0);

    // 3. 居中显示
    lv_obj_center(ui_TouchLabel);
}

/**
 * @brief 在主循环中调用，实时更新坐标显示
 */
void Update_Touch_Debug_Info(void)
{
    if (ui_TouchLabel == NULL)
        return;

    lv_indev_t *indev = lv_indev_get_act();
    // 即使 indev 为空，我们也可以显示 IRQ 状态，所以把 return 往下放或者分开处理

    // 获取 IRQ 物理引脚状态 (0:按下, 1:未按下)
    // 确保包含了 main.h 才能使用 GPIOA
    int irq_state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);

    int x = 0, y = 0;
    if (indev != NULL)
    {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        x = point.x;
        y = point.y;
    }

    // 强制刷新
    lv_label_set_text_fmt(ui_TouchLabel,
                          "Touch Test\n"
                          "X: %d\n"
                          "Y: %d\n"
                          "IRQ: %s",
                          x,
                          y,
                          (irq_state == 0) ? "LOW (Press)" : "HIGH (Rel)" // 更直观的显示
    );
}

/**
 * @brief 初始化左下角地址显示标签
 */
void Init_Address_Label(void)
{
    // 1. 创建标签，父对象是当前屏幕
    ui_LabelAddress = lv_label_create(ui_Screen1);

    // 2. 设置位置：左下角
    // x: 10 (向右偏移10像素，留点边距)
    // y: -10 (向上偏移10像素)
    lv_obj_align(ui_LabelAddress, LV_ALIGN_BOTTOM_LEFT, 10, -10);

    // 3. 设置样式
    lv_obj_set_style_text_color(ui_LabelAddress, lv_color_black(), 0);      // 白色文字
    lv_obj_set_style_text_font(ui_LabelAddress, &lv_font_montserrat_14, 0); // 字体大小(根据你有的字体选)

    // 4. 初始化显示
    lv_label_set_text_fmt(ui_LabelAddress, "ID: %04X", new_address);
}

// 刷新地址显示
void Update_Address_UI(void)
{
    if (ui_LabelAddress == NULL)
        return;

    // 只有当地址不为0时才显示更有意义，或者一直显示也可以
    // 使用 %04X 以十六进制大写显示，例如: "ID: 1A2B"
    lv_label_set_text_fmt(ui_LabelAddress, "ID: %04X", new_address);
}

/**
 * @brief 更新草袋状态 UI (0:关闭->绿, 1:打开->红)
 */
void Update_GrassBag_Status(void)
{
    // 安全检查，防止 UI 控件未创建导致 HardFault
    if (ui_PanelGrassBag == NULL)
        return;

    // 状态机记录，防止每一帧都重复刷颜色，节约 SPI 带宽
    static uint8_t last_bag_status = 0xFF;

    // 获取当前状态
    uint8_t current_bag_status = gui_status[6];

    // 只有状态发生改变时才更新样式
    if (current_bag_status != last_bag_status)
    {
        if (current_bag_status == 0)
        {
            // 0 = 关闭状态 -> 绿色
            lv_obj_set_style_bg_color(ui_PanelGrassBag, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
            // lv_obj_set_style_bg_opa(ui_PanelGrassBag, 255, LV_PART_MAIN); // 可选：设置透明度
        }
        else if (current_bag_status == 1)
        {
            // 1 = 打开状态 -> 红色
            lv_obj_set_style_bg_color(ui_PanelGrassBag, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
            // lv_obj_set_style_bg_opa(ui_PanelGrassBag, 255, LV_PART_MAIN); // 可选：设置透明度
        }

        last_bag_status = current_bag_status;
    }
}

/**
 * @brief 更新 4 个轮子的温度与功率双行显示
 */
static void Update_Motor_Status_UI(void)
{
    // 获取当前通信状态，如果断联，显示 "--"
    uint8_t is_connected = CheckCommunicationStatus();

    if (!is_connected)
    {
        if (ui_LabelTempR0)
            lv_label_set_text(ui_LabelTempR0, "--°C\n--A");
        if (ui_LabelTempR1)
            lv_label_set_text(ui_LabelTempR1, "--°C\n--A");
        if (ui_LabelTempL0)
            lv_label_set_text(ui_LabelTempL0, "--°C\n--A");
        if (ui_LabelTempL1)
            lv_label_set_text(ui_LabelTempL1, "--°C\n--A");
        return;
    }

    // 解析温度 (gui_status 13~16)
    uint8_t temp_r0 = gui_status[13];
    uint8_t temp_r1 = gui_status[14];
    uint8_t temp_l0 = gui_status[15];
    uint8_t temp_l1 = gui_status[16];

    int pwr_r0_int = gui_status[18] / 10;  // 整数部分
    int pwr_r0_dec = gui_status[18] % 10;  // 小数部分

    int pwr_r1_int = gui_status[19] / 10;
    int pwr_r1_dec = gui_status[19] % 10;

    int pwr_l0_int = gui_status[20] / 10;
    int pwr_l0_dec = gui_status[20] % 10;

    int pwr_l1_int = gui_status[21] / 10;
    int pwr_l1_dec = gui_status[21] % 10;

    // 格式化输出：例如 "45°C\n15.3A"
    if (ui_LabelTempR0)
        lv_label_set_text_fmt(ui_LabelTempR0, "%d°C\n%d.%dA", temp_r0, pwr_r0_int, pwr_r0_dec);
    if (ui_LabelTempR1)
        lv_label_set_text_fmt(ui_LabelTempR1, "%d°C\n%d.%dA", temp_r1, pwr_r1_int, pwr_r1_dec);
    if (ui_LabelTempL0)
        lv_label_set_text_fmt(ui_LabelTempL0, "%d°C\n%d.%dA", temp_l0, pwr_l0_int, pwr_l0_dec);
    if (ui_LabelTempL1)
        lv_label_set_text_fmt(ui_LabelTempL1, "%d°C\n%d.%dA", temp_l1, pwr_l1_int, pwr_l1_dec);
}

/**
 * @brief 更新 BMS 电池温度 UI
 */
static void Update_BMS_Temp_UI(void)
{
    if (ui_LabelBMSTemp == NULL)
        return;

    static uint8_t last_bms_temp = 0xFE;

    // 获取当前收到的温度，并结合通信状态判断
    uint8_t current_temp = gui_status[17];
    if (!CheckCommunicationStatus())
    {
        current_temp = 0xFF; // 断连时强制设为无效值
    }

    if (current_temp != last_bms_temp)
    {
        if (current_temp == 0xFF)
        {
            lv_label_set_text(ui_LabelBMSTemp, "BMS: -- °C");
        }
        else
        {
            lv_label_set_text_fmt(ui_LabelBMSTemp, "BMS: %d °C", current_temp);
        }
        last_bms_temp = current_temp;
    }
}

/**
 * @brief 更新左侧边栏的“草袋集满”状态显示
 */
static void Update_Bag_Full_UI(void)
{
    if (ui_LabelGrassBag == NULL)
        return;

    // 0:正常, 1:已满, 2:未连接
    static uint8_t last_bag_ui_state = 0xFF;
    uint8_t current_state = 0;

    // =========================================================
    // <<<---  解析独立通道 (gui_status[6]) --->>>
    // =========================================================
    uint8_t bag_flags = gui_status[6]; // 获取控制板传来的 buf[7]

    bool is_open = (bag_flags & (1 << 0)) ? true : false;
    bool is_full = (bag_flags & (1 << 1)) ? true : false;
    bool is_disconnect = (bag_flags & (1 << 2)) ? true : false;

    // 独立判定优先级：未连接 > 已满载 > 正常
    if (is_disconnect)
    {
        current_state = 2; // 未连接
    }
    else if (is_full)
    {
        current_state = 1; // 已满
    }
    else
    {
        current_state = 0; // 正常
    }

    // 状态发生变化时才刷新屏幕
    if (current_state != last_bag_ui_state)
    {
        if (current_state == 1) // --- 已满 ---
        {
            lv_label_set_text(ui_LabelGrassBag, "草袋: 已满!");
            lv_obj_set_style_text_color(ui_LabelGrassBag, lv_palette_main(LV_PALETTE_RED), 0);

            if (ui_ImageGrassBag)
            {
                lv_obj_set_style_img_recolor(ui_ImageGrassBag, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
                lv_obj_set_style_img_recolor_opa(ui_ImageGrassBag, 255, LV_PART_MAIN);
            }
        }
        else if (current_state == 2) // --- 未连接 ---
        {
            lv_label_set_text(ui_LabelGrassBag, "草袋: 未连接");
            lv_obj_set_style_text_color(ui_LabelGrassBag, lv_palette_main(LV_PALETTE_RED), 0);

            if (ui_ImageGrassBag)
            {
                lv_obj_set_style_img_recolor(ui_ImageGrassBag, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
                lv_obj_set_style_img_recolor_opa(ui_ImageGrassBag, 255, LV_PART_MAIN);
            }
        }
        else // --- 正常 ---
        {
            lv_label_set_text(ui_LabelGrassBag, "草袋: 正常");
            lv_obj_set_style_text_color(ui_LabelGrassBag, lv_palette_main(LV_PALETTE_GREEN), 0);

            if (ui_ImageGrassBag)
            {
                lv_obj_set_style_img_recolor(ui_ImageGrassBag, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
                lv_obj_set_style_img_recolor_opa(ui_ImageGrassBag, 255, LV_PART_MAIN);
            }
        }

        last_bag_ui_state = current_state;
    }
}

// ==============================================================
// 内部辅助函数：更新卡片的视觉状态 (边框颜色 + 勾号图标)
// ==============================================================
void Update_OpMode_UI_Highlight(uint8_t selected_mode)
{
    // 1. 定义颜色
    lv_color_t color_selected = lv_palette_main(LV_PALETTE_GREEN); // 选中时的边框颜色
    lv_color_t color_unselected = lv_color_hex(0x333333);          // 未选中时的暗灰边框

    // 2. 先把所有卡片【重置为未选中状态】
    // 隐藏所有勾号图标
    lv_obj_add_flag(ui_IconCheck1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_IconCheck2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_IconCheck3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_IconCheck4, LV_OBJ_FLAG_HIDDEN);
    // 边框变暗
    lv_obj_set_style_border_color(ui_PanelMode1, color_unselected, 0);
    lv_obj_set_style_border_color(ui_PanelMode2, color_unselected, 0);
    lv_obj_set_style_border_color(ui_PanelMode3, color_unselected, 0);
    lv_obj_set_style_border_color(ui_PanelMode4, color_unselected, 0);

    // 3. 根据传入的模式，【单独点亮对应的卡片】
    if (selected_mode == 0)
    {
        lv_obj_clear_flag(ui_IconCheck1, LV_OBJ_FLAG_HIDDEN);            // 显示勾号
        lv_obj_set_style_border_color(ui_PanelMode1, color_selected, 0); // 边框变绿
    }
    else if (selected_mode == 1)
    {
        lv_obj_clear_flag(ui_IconCheck2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(ui_PanelMode2, color_selected, 0);
    }
    else if (selected_mode == 2)
    {
        lv_obj_clear_flag(ui_IconCheck3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(ui_PanelMode3, color_selected, 0);
    }
    else if (selected_mode == 3)
    {
        lv_obj_clear_flag(ui_IconCheck4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(ui_PanelMode4, color_selected, 0);
    }
}

void Update_Ultrasonic_UI_State(void)
{
    // --- 更新主界面 Screen1 的状态文字 ---
    if (ui_LabelUltrasonicMain)
    {
        if (g_sys_config.ultrasonic_en)
        {
            lv_label_set_text(ui_LabelUltrasonicMain, "超声波: 开");
            lv_obj_set_style_text_color(ui_LabelUltrasonicMain, lv_palette_main(LV_PALETTE_GREEN), 0);
        }
        else
        {
            lv_label_set_text(ui_LabelUltrasonicMain, "超声波: 关");
            lv_obj_set_style_text_color(ui_LabelUltrasonicMain, lv_palette_main(LV_PALETTE_GREY), 0);
        }
    }
}

void EventCb_PinCheck(lv_event_t *e)
{
    // 因为事件绑定在键盘或文本框上，我们直接使用全局变量获取文本框
    if (ui_TextAreaPin == NULL)
        return;

    // 获取当前输入的文本
    const char *input_txt = lv_textarea_get_text(ui_TextAreaPin);

    // 【核心逻辑】：检测当前输入的长度
    size_t len = strlen(input_txt);

    // 如果还没有输满 4 位数，什么都不做，直接返回，等待用户继续输入
    if (len < 4)
    {
        return;
    }

    // --- 如果已经输满了 4 位，开始校验 ---
    if (strcmp(input_txt, g_current_pin) == 0)
    {
        // 1. 密码正确！
        g_is_unlocked = true;

        Play_Beep(BEEP_CONNECT); // 播放解锁成功音效

        // 2. 清空密码框（方便下次锁屏时使用）
        lv_textarea_set_text(ui_TextAreaPin, "");

        // 3. 切换到真正的主控界面 (Screen1)
        lv_scr_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_FADE_IN, 10, 0, false);
        // 初始化主界面的其他动画和UI
        // Init_Needle_Anim();
        // Init_Ultrasonic_UI();
        // Init_Address_Label();
    }
    else
    {
        // 1. 密码错误！
        Play_Beep(BEEP_ALARM_FAULT); // 播放错误音效

        // 2. 清空输入框，让用户重新从头输入
        lv_textarea_set_text(ui_TextAreaPin, "");

        // 3. (可选) 给密码框加个红边框警告一下
        lv_obj_set_style_border_color(ui_TextAreaPin, lv_palette_main(LV_PALETTE_RED), 0);
    }
}