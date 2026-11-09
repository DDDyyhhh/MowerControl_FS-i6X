// #include <st7789.h>
// #include <dma.h>
// #include <status.h>
// #include <string.h>
// #include <fonts.h>
// #include <st7789.h>
// #include <stdio.h>
// #include <beep.h>
// #include "key.h"
// #include <read_adc.h> // <<<--- 需要包含这个头文件来使用 Read_ADC_Channel
// #include "car.h"      // <<<--- 包含小车图像数据的头文件
// #include "pairing_remote.h"
// #include "heartbeat.h"
// #include <connet_check.h>

// extern uint8_t status[20];
// extern uint8_t Current_receiver_height;
// extern int8_t Current_Bag;
// extern float Current_rpm;
// extern uint8_t last_height;
// static uint8_t last_displayed_battery = 255; // 用一个无效值初始化，以确保第一次会刷新

// // extern const unsigned short gImage_car[145288];
// static uint32_t last_battery_read_time = 0;
// extern uint16_t adc_dma_buffer[5]; // 告诉编译器这个数组在别的地方定义了

// // --- 新增：定义物理高度范围的宏 ---
// #define PHYSICAL_HEIGHT_MIN 3.6f  // 刀盘最低物理高度 4.0 cm
// #define PHYSICAL_HEIGHT_MAX 11.0f // 刀盘最高物理高度 11.0 cm
// #define SLIDER_VALUE_MAX 20.0f    // 滑块的最大值

// uint8_t front_obstacle = 1;   // 黄色
// uint8_t left_25_obstacle = 2; // 红色
// uint8_t left_75_obstacle = 0; // 测试：左侧-75度有障碍物
// uint8_t right_25_obstacle = 0;
// uint8_t right_75_obstacle = 0;
// // 计算车头中心位置
// uint16_t car_center_x = 90 + (226 / 4) / 2; // 90 + 小车宽度/2
// uint16_t car_center_y = 230;                // 车头在y=230位置
// extern Heartbeat_Handle_t g_hb_remote;      // 心跳句柄

// void status_init(void)
// {

//     for (int i = 0; i < 8; i++)
//     {
//         status[i] = 0;
//     }
//     status[1] = 1;
//     status[5] = 20;
//     // char chinese[] = "速度"; // "参考" GB2312编码
//     // ST7789_ShowChineses(0, 50, WHITE  , BLUE , chinese,2);
//     // ST7789_ShowString(65, 57, "L", Font_16x26,BLUE , WHITE);
//     uint16_t adc_value = adc_dma_buffer[4];
//     uint8_t battery_percentage = filter_adc_value(adc_value);
//     draw_battery_icon(50, 10, battery_percentage);

//     char chinese2[] = "转速"; // "参考" GB2312编码
//     ST7789_ShowChineses(155, 50, WHITE, BLUE, chinese2, 2);
// }

// void status_task(void)
// {
//     // ============================================================
//     // 1. 心跳与信号格处理 (全场景智能防抖版)
//     // ============================================================
//     // Heartbeat_Check(&g_hb_remote);
//     // uint8_t real_signal = Heartbeat_GetSignalLevel(&g_hb_remote);
    
//     // static uint8_t display_signal = 0; 
//     // static uint32_t downgrade_timer = 0;

//     // // --- 智能滤波逻辑 ---


    
//     // // --- 信号显示状态机 ---
    
//     // // 情况 A: 真实信号是 0 (断连) -> 启动严格延时，防止闪烁
//     // if (real_signal == 0)
//     // {
//     //     if (display_signal != 0) // 当前显示不是0，准备降级
//     //     {
//     //         if (downgrade_timer == 0) downgrade_timer = HAL_GetTick();
            
//     //         // 必须连续断连 1.5秒 才显示红叉
//     //         if (HAL_GetTick() - downgrade_timer > 1500) 
//     //         {
//     //             display_signal = 0;
//     //             downgrade_timer = 0;
//     //         }
//     //     }
//     // }
//     // // 情况 B: 真实信号 > 0 (连接中)
//     // else
//     // {
//     //     // 这里的 real_signal 已经是平均值了，很稳
//     //     // 我们可以直接更新，或者加一点点迟滞
        
//     //     // 如果信号变差 (例如 4->3)，稍微等一下 (例如 500ms)，防止临界跳变
//     //     if (real_signal < display_signal)
//     //     {
//     //          if (downgrade_timer == 0) downgrade_timer = HAL_GetTick();
//     //          if (HAL_GetTick() - downgrade_timer > 500) 
//     //          {
//     //              display_signal = real_signal;
//     //              downgrade_timer = 0;
//     //          }
//     //     }
//     //     // 如果信号变好 (例如 1->3)，因为底层已经是平均值了，说明信号真的变好了
//     //     // 可以直接更新，或者也加 200ms 延时让视觉更稳
//     //     else 
//     //     {
//     //         display_signal = real_signal;
//     //         downgrade_timer = 0;
//     //     }
//     // }

//     // --- 绘图 (保持不变) ---
//     // static uint8_t last_drawn_signal = 0xFF;
//     // if (display_signal != last_drawn_signal)
//     // {
//     //     if (display_signal == 0)
//     //         DrawSignalStairs(0, 30, 4, RED, RED); 
//     //     else
//     //         DrawSignalStairs(0, 30, display_signal, GREEN, GRAY);
            
//     //     last_drawn_signal = display_signal;
//     // }

//     if (status[0] == 1 && status[11] == 0)
//     {
//         // char chinese[] = "解锁"; // "参考" GB2312编码
//         //  ST7789_ShowChineses(90, 10, WHITE  , GREEN , chinese,2);
//         DrawLockIcon(200, 15, 40, 1, GREEN, RED);
//     }
//     else
//     {
//         // char chinese[] = "锁定"; // "参考" GB2312编码
//         // ST7789_ShowChineses(90, 10, WHITE  , RED , chinese,2);
//         DrawLockIcon(200, 15, 40, 0, GREEN, RED);
//         ST7789_ShowString(220, 57, "N", Font_16x26, BLUE, WHITE);
//     }

//     if (status[1] == 1)
//     {
//         // ST7789_ShowString(65, 57, "L", Font_16x26,BLUE , WHITE);
//         //  DrawSimpleGearArc(100, 100, 25, 3);
//         DrawSimpleGearArc(30, 100, 25, 1);
//     }
//     if (status[1] == 2)
//     {
//         DrawSimpleGearArc(30, 100, 25, 2);
//     }
//     if (status[1] == 3)
//     {
//         DrawSimpleGearArc(30, 100, 25, 3);
//     }

//     // 原来的挡位显示
//     if (status[2] == 1 && status[0] == 1)
//     {
//         ST7789_ShowString(220, 57, "L", Font_16x26, BLUE, WHITE);
//     }

//     if (status[2] == 2 && status[0] == 1)
//     {
//         ST7789_ShowString(220, 57, "M", Font_16x26, BLUE, WHITE);
//     }

//     if (status[2] == 3 && status[0] == 1)
//     {
//         ST7789_ShowString(220, 57, "H", Font_16x26, BLUE, WHITE);
//     }

//     uint8_t bms_soc = status[3];

//     DrawSimpleBattery(5, 280, bms_soc);

//     // static uint32_t last_1s_update_time = 0;

//     // // 2. 判断当前时间与上次刷新时间差 是否超过 1000ms (1秒)
//     // if (HAL_GetTick() - last_1s_update_time >= 1000)
//     // {
//     //     // 3. 更新时间戳
//     //     last_1s_update_time = HAL_GetTick();
//     //     Current_rpm = status[4];
//     //     char Current_rpm_str[10];
//     //     sprintf(Current_rpm_str, "%d  A", Current_rpm);
//     //     ST7789_ShowString(150, 90, Current_rpm_str, Font_11x18, BLUE, WHITE);
//     // }

//     // ====================== 核心修改逻辑 (方案二) ======================
//     // 1. 从 status[5] 获取原始数据 (这是 height_cm * 10 的值, e.g., 75)
//     uint8_t received_raw_height = status[5];

//     // 2. 将其转换回 float 类型的物理高度 (cm)
//     float current_physical_height_cm = (float)received_raw_height / 10.0f;

//     // 3. 将物理高度 (4.0 ~ 11.0 cm) 映射到滑块的值域 (0 ~ 20)
//     uint8_t slider_value = 0;
//     if (current_physical_height_cm <= PHYSICAL_HEIGHT_MIN)
//     {
//         slider_value = 0;
//     }
//     else if (current_physical_height_cm >= PHYSICAL_HEIGHT_MAX)
//     {
//         slider_value = (uint8_t)SLIDER_VALUE_MAX;
//     }
//     else
//     {
//         float height_range = PHYSICAL_HEIGHT_MAX - PHYSICAL_HEIGHT_MIN; // 7.0
//         float value_range = SLIDER_VALUE_MAX;                           // 20.0
//         slider_value = (uint8_t)((current_physical_height_cm - PHYSICAL_HEIGHT_MIN) * (value_range / height_range));
//     }

//     // 4. 将计算出的滑块值赋给全局变量，用于UI绘制
//     Current_receiver_height = slider_value;

//     // 5. (可选但推荐) 在滑块旁边显示实际的厘米数，提升用户体验
//     char height_str[10];
//     sprintf(height_str, "%.1fcm", current_physical_height_cm);

//     // 在滑块旁边显示 (x=180, y=180)
//     ST7789_DrawRectangle(195, 250, 210 + 4 * 7 + 4, 250 + 10, WHITE, 1);
//     ST7789_ShowString(195, 250, height_str, Font_7x10, BLUE, WHITE);

//     // 6. 检查计算出的滑块值是否发生变化，如果变化了就重绘滑块
//     if (Current_receiver_height != last_height)
//     {
//         if (key_event(7) == 2)
//         {
//             DrawVerticalSlider(215, 140, 20, 100, Current_receiver_height, 20, GREEN, BLUE);
//         }
//         else
//         {
//             DrawVerticalSlider(215, 140, 20, 100, Current_receiver_height, 20, GREEN, GRAY);
//         }

//         last_height = Current_receiver_height;
//     }
//     // ===================================================================

//     // Current_Bag = status[6];

//     // if (Current_Bag)
//     // {

//     //     // 草袋打开状态
//     //     DrawStatusSlider(5, 140, 20, 100, 1, GREEN, RED);
//     // }
//     // else
//     // {

//     //     // 草袋关闭状态
//     //     DrawStatusSlider(5, 140, 20, 100, 0, GREEN, RED);
//     // }

//     /**************************************************************
//      *                遥控器电池电量显示开始
//      **************************************************************/
//     // 1. 读取遥控器电池的ADC值
//     uint16_t adc_value = adc_dma_buffer[4];

//     // 2. 获取滤波后的百分比
//     uint8_t new_percentage = filter_adc_value(adc_value);

//     // 3. 【核心修改】单向更新逻辑
//     bool need_update = false;

//     // 计算新旧值的差 (带符号)
//     int diff = (int)new_percentage - (int)last_displayed_battery;

//     // 情况A: 第一次运行
//     if (last_displayed_battery == 255)
//     {
//         need_update = true;
//     }
//     // 情况B: 电量下降 (正常放电)
//     // 只要现在的电量比显示的小，就更新。
//     // 你可以保留 diff <= -1 (实时) 或者 diff <= -2 (更迟钝/更稳)
//     // 鉴于你之前觉得跳变烦人，这里建议设置为下降超过 1% 就更新，因为我们屏蔽了上升，所以不会跳回去。
//     else if (diff <= -1)
//     {
//         need_update = true;
//     }
//     // 情况C: 电量大幅上升 (充电/换电池)
//     // 只有当电量突然增加了 5% 以上，才认为是有效上升，允许更新
//     // 这就完美屏蔽了 81% -> 83% 这种微小的回升
//     else if (diff >= 5)
//     {
//         need_update = true;
//     }
//     // 情况D: 微小上升 (1% ~ 4%) -> 忽略！保持显示低电量 (81%)

//     // 4. 执行刷新
//     if (need_update)
//     {
//         // 更新图标
//         draw_battery_icon(50, 10, new_percentage);

//         // 更新百分比文字
//         char battery_str[8];
//         sprintf(battery_str, "%3d%%", new_percentage);

//         // 清除旧区域
//         ST7789_DrawRectangle(95, 12, 95 + 4 * 11, 12 + 18, WHITE, 1);
//         ST7789_ShowString(95, 12, battery_str, Font_11x18, BLACK, WHITE);

//         // 更新历史值
//         last_displayed_battery = new_percentage;
//     }

//     /**************************************************************
//      *                遥控器电池电量显示结束
//      **************************************************************/

//     /**************************************************************
//      *            小车模型与雷达图显示 (最终版 - status数组版)
//      **************************************************************/
//     // --- 新增：使用静态标志位确保背景和小车只绘制一次 ---
//     static bool car_and_radar_bg_drawn = false;

//     // 1. 绘制静态背景和小车模型 (只在第一次进入 status_task 时执行)
//     if (!car_and_radar_bg_drawn)
//     {
//         // 定义UI位置参数
//         uint16_t car_center_x = 120; // 屏幕中心X
//         uint16_t car_front_y = 230;  // 小车前端在屏幕上的Y坐标
//         uint16_t radar_radius = 40;

//         // --- 调用雷达绘制函数，但所有状态都设为0 (绿色)，强制其绘制初始背景 ---
//         DrawSemiCircleObstacleZones(car_center_x, car_front_y, radar_radius,
//                                     0, 0, 0, 0, 0);

//         // --- 绘制小车图片 ---
//         // ST7789_DrawImage_Scale(90, 230, 227, 320, 226 / 4, 320 / 4, gImage_car);

//         // --- 设置标志位，确保此代码块不再执行 ---
//         car_and_radar_bg_drawn = true;
//     }

//     // 使用一个独立的、较慢的定时器来刷新雷达图
//     static uint32_t radar_update_timer = 0;
//     if (HAL_GetTick() - radar_update_timer >= 100) // 每100ms才刷新一次雷达图
//     {
//         radar_update_timer = HAL_GetTick();

//         // --- 定义临时变量存储各扇区的状态 (0:绿, 1:黄, 2:红) ---
//         uint8_t left_75_obs = 0;
//         uint8_t left_25_obs = 0;
//         uint8_t right_25_obs = 0;
//         uint8_t right_75_obs = 0;
//         uint8_t front_obs = 2; // 正前方(由碰撞开关决定)

// // --- 辅助宏：根据距离计算颜色状态 ---
// // 逻辑: >=250(无效) -> 绿; <40 -> 红; <100 -> 黄; 其他 -> 绿
// #define CALC_OBS_STATE(dist) \
//     ((dist) >= 250 ? 0 : ((dist) < 40 ? 2 : 0))

//         if (CheckCommunicationStatus())
//         {
//             // --- 2. 从 status 数组获取数据并转换状态 ---

//             // Sensor 0 (左边最外侧) -> status[7]
//             left_75_obs = CALC_OBS_STATE(status[7]);

//             // Sensor 1 (左边靠中间) -> status[8]
//             left_25_obs = CALC_OBS_STATE(status[8]);

//             // Sensor 2 (右边靠中间) -> status[9]
//             right_25_obs = CALC_OBS_STATE(status[9]);

//             // Sensor 3 (右边最外侧) -> status[10]
//             right_75_obs = CALC_OBS_STATE(status[10]);
//         }
//         else
//         {
//             // 如果通信丢失，所有区域显示为安全(绿色)，避免误报
//             left_75_obs = 0;
//             left_25_obs = 0;
//             right_25_obs = 0;
//             right_75_obs = 0;
//         }

//         // --- 3. 碰撞检测逻辑 (覆盖中间扇区) ---
//         // 如果检测到物理碰撞，正前方强制变红
//         if (status[11] == 1)
//         {
//             front_obs = 2; // 2 代表红色
//         }
//         else
//         {
//             front_obs = 0; // 无碰撞时为绿色
//         }

//         // --- 4. 调用函数绘制雷达图 ---
//         uint16_t car_center_x = 120;
//         uint16_t car_front_y = 230;

//         // 传入计算好的状态值
//         DrawSemiCircleObstacleZones(car_center_x, car_front_y, 40,
//                                     left_75_obs, left_25_obs, front_obs,
//                                     right_25_obs, right_75_obs);
//     }

//     if (status[11] == 1 || status[11] == 4)
//     {
//         DrawEllipticalWheelIcon(144, 300, 10, 20, 0, GREEN, RED);
//         DrawEllipticalWheelIcon(94, 300, 10, 20, 0, GREEN, RED);
//         DrawEllipticalWheelIcon(144, 258, 10, 20, 0, GREEN, RED);
//         DrawEllipticalWheelIcon(93, 258, 10, 20, 0, GREEN, RED);
//     }

//     else if (status[11] == 2)
//     {
//         DrawEllipticalWheelIcon(144, 300, 10, 20, 0, GREEN, GBLUE);
//         DrawEllipticalWheelIcon(94, 300, 10, 20, 0, GREEN, GBLUE);
//         DrawEllipticalWheelIcon(144, 258, 10, 20, 0, GREEN, GBLUE);
//         DrawEllipticalWheelIcon(93, 258, 10, 20, 0, GREEN, GBLUE);
//     }
//     else if (status[11] == 10)
//     {
//         DrawEllipticalWheelIcon(144, 300, 10, 20, 0, GREEN, WHITE);
//         DrawEllipticalWheelIcon(94, 300, 10, 20, 0, GREEN, WHITE);
//         DrawEllipticalWheelIcon(144, 258, 10, 20, 0, GREEN, WHITE);
//         DrawEllipticalWheelIcon(93, 258, 10, 20, 0, GREEN, WHITE);
//     }
//     else if (status[11] == 11)
//     {
//         DrawEllipticalWheelIcon(144, 300, 10, 20, 0, GREEN, CYAN);
//         DrawEllipticalWheelIcon(94, 300, 10, 20, 0, GREEN, CYAN);
//         DrawEllipticalWheelIcon(144, 258, 10, 20, 0, GREEN, CYAN);
//         DrawEllipticalWheelIcon(93, 258, 10, 20, 0, GREEN, CYAN);
//     }
//     else if (status[11] == 12)
//     {
//         DrawEllipticalWheelIcon(144, 300, 10, 20, 0, GREEN, MAGENTA);
//         DrawEllipticalWheelIcon(94, 300, 10, 20, 0, GREEN, MAGENTA);
//         DrawEllipticalWheelIcon(144, 258, 10, 20, 0, GREEN, MAGENTA);
//         DrawEllipticalWheelIcon(93, 258, 10, 20, 0, GREEN, MAGENTA);
//     }
//     else
//     {
//         DrawEllipticalWheelIcon(144, 300, 10, 20, 1, GREEN, RED);
//         DrawEllipticalWheelIcon(94, 300, 10, 20, 1, GREEN, RED);
//         DrawEllipticalWheelIcon(144, 258, 10, 20, 1, GREEN, RED);
//         DrawEllipticalWheelIcon(93, 258, 10, 20, 1, GREEN, RED);
//     }
// }