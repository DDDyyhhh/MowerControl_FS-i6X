#include "ui.h"
#include "app_vars.h" // 引入全局配置
#include <stdio.h>
#include "app_gui.h"
#include "beep.h"
#include "flash_storage.h"
// ==============================================================
// 架构师全局变量：用于实现“本次开机不再提示”
// false: 每次都弹窗, true: 本次开机不再弹窗
// ==============================================================
bool g_hide_cruise_popup_this_session = false;
extern volatile bool g_is_pairing_mode; // 借用你之前的全局变量
bool g_radar_is_on = true;              // 雷达是否开启的标志位
void Start_Hidden_Snake_Game(void);

void EventCb_ToggleNoRemind(lv_event_t *e)
{
    // Your code here
}

// 声明一个占位函数，代表真正的硬件控制
void Enable_Cruise_Control(void)
{

    g_sys_config.cruise_ctrl_req = 1; // 置位触发标志
    Play_Beep(BEEP_SHORT);
}

// ==============================================================
//  “取消” 按钮的事件回调
// ==============================================================
void EventCb_CruiseCancel(lv_event_t *e)
{
    // 逻辑很简单：直接把弹窗藏起来
    lv_obj_add_flag(ui_PanelCruisePopup, LV_OBJ_FLAG_HIDDEN);
    // printf("[UI Event] 用户取消了定速巡航\r\n");
}

// ==============================================================
//  “OK” 按钮的事件回调
// ==============================================================
void EventCb_CruiseOk(lv_event_t *e)
{
    // 1. 检查“不再提示”开关的状态
    if (lv_obj_has_state(ui_SwitchNoRemind, LV_STATE_CHECKED))
    {
        // 如果开关被打开了，就设置全局标志位
        g_hide_cruise_popup_this_session = true;
        // printf("[UI Pref] 用户设置本次开机不再提示巡航弹窗\r\n");
    }

    // 2. 执行核心功能：使能自动定速巡航
    Enable_Cruise_Control(); // 这是你以后要填写的底层控制函数

    // 3. 把弹窗藏起来
    lv_obj_add_flag(ui_PanelCruisePopup, LV_OBJ_FLAG_HIDDEN);
}

// ==============================================================
// 1. 超声波雷达开关事件
// ==============================================================
void EventCb_ToggleUltrasonic(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
    {
        g_sys_config.ultrasonic_en = 1;
        // printf("[Safety UI] 超声波雷达 -> 开启\r\n");
    }
    else
    {
        g_sys_config.ultrasonic_en = 0;
        // printf("[Safety UI] 超声波雷达 -> 关闭\r\n");
    }
    // 状态改变，立刻通知控制板
    // 调用统一的刷新函数，一次性把两个界面的 UI 都刷新了
    Update_Ultrasonic_UI_State();
}

// ==============================================================
// 2. 碰撞传感器开关事件
// ==============================================================
void EventCb_ToggleCollision(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
    {
        g_sys_config.collision_en = 1;
        // printf("[Safety UI] 碰撞传感器 -> 开启\r\n");
    }
    else
    {
        g_sys_config.collision_en = 0;
        // printf("[Safety UI] 碰撞传感器 -> 关闭 (⚠️ 已屏蔽防撞保护)\r\n");
    }
}

// ==============================================================
// 3. 后退速度限制开关事件
// ==============================================================
void EventCb_ToggleReverseSpeed(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
    {
        g_sys_config.reverse_limit_en = 1;
        // printf("[Safety UI] 后退限速 -> 开启\r\n");
    }
    else
    {
        g_sys_config.reverse_limit_en = 0;
        // printf("[Safety UI] 后退限速 -> 关闭 (全速模式)\r\n");
    }
}

// ==============================================================
// 4. 倾斜传感器开关事件
// ==============================================================
void EventCb_ToggleTiltSensor(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
    {
        g_sys_config.tilt_sensor_en = 1;
        // printf("[Safety UI] 倾斜传感器 -> 开启\r\n");
    }
    else
    {
        g_sys_config.tilt_sensor_en = 0;
        // printf("[Safety UI] 倾斜传感器 -> 关闭\r\n");
    }
}

// ==============================================================
// 2. 打开配对按钮事件 (Trigger: Clicked)
// ==============================================================
void EventCb_StartPairing(lv_event_t *e)
{
    // 将全局配对标志位置为 true
    g_trigger_pairing_from_ui = true;

    // TODO: 调用底层无线模块的配对函数
    // e.g., NRF24L01_EnterPairingMode();

    // printf("[UI Event] 进入配对模式...\r\n");

    // UX 小贴士：为了给用户反馈，你可以弹出一个短暂的提示框 (Toast)
    // 但目前你的 app_gui.c 里面已经有针对 g_is_pairing_mode 的错误提示面板了，
    // 所以只要这里置 true，主界面的 Update_System_Notification 就会自动弹窗！
}

// 【新增】 动画完成回调函数，用于重新启用输入
static void anim_ready_cb_reenable_input(lv_anim_t *a)
{
    // 此时屏幕已完全切换到 Screen1
    lv_obj_t *new_screen = lv_scr_act();

    // 恢复新屏幕的正常点击行为
    lv_obj_clear_flag(new_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(new_screen, LV_OBJ_FLAG_ADV_HITTEST);
}
// ==============================================================
// 3. 初始化刀盘高度按钮事件 (Trigger: Clicked)
// ==============================================================
void EventCb_InitBladeHeight(lv_event_t *e)
{
    // ---  1. 获取最顶层的屏幕对象 ---
    lv_obj_t *current_screen = lv_scr_act();

    // --- 2. 禁用屏幕的所有点击事件，防止用户乱点 ---
    lv_obj_add_flag(current_screen, LV_OBJ_FLAG_CLICKABLE);   // 先确保容器可点击
    lv_obj_add_flag(current_screen, LV_OBJ_FLAG_ADV_HITTEST); // 开启高级点击测试

    // 让屏幕“吃掉”所有点击事件，事件不会再往下传递给按钮
    // 这样，在屏幕切换动画期间，用户再点任何按钮都无效了
    lv_obj_add_event_cb(current_screen, NULL, LV_EVENT_CLICKED, NULL);

    // 3. 发送触发标志给控制板 (保持不变)
    g_sys_config.blade_init_en = 1;

    // 5.  使用带“完成回调”的屏幕切换
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, current_screen);
    lv_anim_set_ready_cb(&a, anim_ready_cb_reenable_input); // 动画结束后解锁的回调

    // 切换到主界面
    lv_scr_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_FADE_ON, 10, 0, false); // 注意最后一个参数是 false

    // 启动一个虚拟动画，只为了在 200ms 后触发解锁回调
    lv_anim_set_time(&a, 20); // 必须比切换动画稍长一点
    lv_anim_set_values(&a, 0, 1);
    lv_anim_set_exec_cb(&a, NULL);
    lv_anim_start(&a);
}

// ==============================================================
// 事件回调：点击了“双手1”卡片
// ==============================================================
void EventCb_SetMode1(lv_event_t *e)
{
    // 防止重复点击触发发送
    if (g_sys_config.op_mode == 0)
        return;

    // 1. 更新全局配置字典
    g_sys_config.op_mode = 0;

    // 2. 刷新 UI 视觉
    Update_OpMode_UI_Highlight(g_sys_config.op_mode);
}

// ==============================================================
// 事件回调：点击了“双手2”卡片
// ==============================================================
void EventCb_SetMode2(lv_event_t *e)
{
    if (g_sys_config.op_mode == 1)
        return;

    g_sys_config.op_mode = 1;
    Update_OpMode_UI_Highlight(g_sys_config.op_mode);
}

// ==============================================================
// 事件回调：点击了“单手1”卡片
// ==============================================================
void EventCb_SetMode3(lv_event_t *e)
{
    if (g_sys_config.op_mode == 2)
        return;

    g_sys_config.op_mode = 2;
    Update_OpMode_UI_Highlight(g_sys_config.op_mode);
}

// ==============================================================
// 事件回调：点击了“单手2”卡片
// ==============================================================
void EventCb_SetMode4(lv_event_t *e)
{
    if (g_sys_config.op_mode == 3)
        return;

    g_sys_config.op_mode = 3;
    Update_OpMode_UI_Highlight(g_sys_config.op_mode);
}


// ==============================================================
// 5. 护草模式开关事件
// ==============================================================
void EventCb_ToggleLawnProtect(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
    {
        g_sys_config.lawn_protect_en = 1;
    }
    else
    {
        g_sys_config.lawn_protect_en = 0;
    }
}

void EventCb_HiddenGame(lv_event_t *e)
{
    // // --- 连击检测逻辑 ---
    static uint32_t last_click_time = 0;
    static uint8_t click_count = 0;

    uint32_t current_time = lv_tick_get(); // 获取 LVGL 当前时间

    // 如果两次点击间隔小于 500 毫秒，则算作连击
    if (current_time - last_click_time < 500)
    {
        click_count++;
    }
    else
    {
        click_count = 1; // 超时重置为 1
    }
    last_click_time = current_time;

    // 当连续点击达到 5 次时触发彩蛋！
    if (click_count >= 5)
    {
        click_count = 0; // 重置计数器
        // printf("🎉 彩蛋触发！进入 Flappy Bug 游戏！\r\n");
        Start_Hidden_Snake_Game(); // 启动游戏
        return;                    // 直接返回，不再执行原来的底层配置逻辑
    }
    // // 你也可以在这里临时改变某个 UI 元素的颜色来表示“正在执行”
    // // 比如：
    // lv_obj_t *btn = lv_event_get_target(e);
    // lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_ORANGE), 0);
    // // 注意：如果是真正的项目，通常会在收到下位机“初始化完成”的回应后，再把颜色改回来。
}

// ============================================================================
//                 🐍 隐藏彩蛋：摇杆贪吃蛇 (Joystick Snake)
// ============================================================================
#include <stdlib.h> // rand()

#define BLOCK_SIZE 10        // 方块像素大小
#define MAX_SNAKE_LENGTH 100 // 最大长度
#define GAME_SPEED_MS 120    // 游戏速度 (越小越快)

// 摇杆死区与阈值定义 (假设 ADC 是 12位 0-4095，中位在 2048 左右)
#define JOY_THRES_HIGH 3000 // 摇杆推到底的阈值
#define JOY_THRES_LOW 1000  // 摇杆拉到底的阈值

typedef enum
{
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} snake_dir_t;

static lv_obj_t *game_scr = NULL;
static lv_obj_t *snake_body[MAX_SNAKE_LENGTH];
static lv_obj_t *food = NULL;
static lv_obj_t *score_label = NULL;
static lv_timer_t *game_timer = NULL;

static uint16_t snake_length = 3;
static snake_dir_t current_dir = DIR_UP;
static bool game_over = false;
static int score = 0;

// 在随机位置生成食物
static void spawn_food(void)
{
    int max_x = 240 / BLOCK_SIZE;
    int max_y = 320 / BLOCK_SIZE;
    while (1)
    {
        bool overlap = false;
        int fx = (rand() % max_x) * BLOCK_SIZE;
        int fy = (rand() % max_y) * BLOCK_SIZE;
        lv_obj_set_pos(food, fx, fy);

        // 检查是否刷在蛇身上
        for (int i = 0; i < snake_length; i++)
        {
            if (lv_obj_get_x(snake_body[i]) == fx && lv_obj_get_y(snake_body[i]) == fy)
            {
                overlap = true;
                break;
            }
        }
        if (!overlap)
            break;
    }
}
// ==============================================================
// 重新开始游戏：清理尸体，重置状态
// ==============================================================
static void reset_snake_game(void)
{
    // 1. 删除超出初始长度（3节）的蛇身尸体，释放内存
    for (int i = 3; i < snake_length; i++)
    {
        if (snake_body[i] != NULL)
        {
            lv_obj_del(snake_body[i]);
            snake_body[i] = NULL;
        }
    }

    // 2. 重置游戏基础变量
    snake_length = 3;
    current_dir = DIR_UP;
    score = 0;
    game_over = false;

    // 3. 让蛇头和初始的两节身体回到屏幕中央
    for (int i = 0; i < snake_length; i++)
    {
        // 重新确保头是蓝色，身体是绿色 (防止颜色错乱)
        lv_obj_set_style_bg_color(snake_body[i], (i == 0) ? lv_palette_main(LV_PALETTE_BLUE) : lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_obj_set_pos(snake_body[i], 120, 160 + i * BLOCK_SIZE);
    }

    // 4. 重置计分板颜色和文字
    lv_label_set_text(score_label, "Score: 0");
    lv_obj_set_style_text_color(score_label, lv_color_white(), 0);

    // 5. 重新生成一个食物
    spawn_food();
}
// ==============================================================
// 屏幕点击事件：Game Over 时点击屏幕重新开始
// ==============================================================
static void game_scr_event_cb(lv_event_t *e)
{
    // 只有在 Game Over 的状态下点击屏幕才有效
    if (game_over)
    {
        reset_snake_game();
    }
}

// 退出游戏并销毁资源
static void btn_exit_cb(lv_event_t *e)
{
    if (game_timer)
    {
        lv_timer_del(game_timer);
        game_timer = NULL;
    }
    // 返回你的主界面 (注意 ui_Screen1 需要根据你实际的屏幕名修改)
    lv_scr_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_FADE_ON, 10, 0, false);
    if (game_scr)
    {
        lv_obj_del_async(game_scr);
        game_scr = NULL;
    }
}

// 物理摇杆读取逻辑
static void process_joystick_input(void)
{
    // 根据你的定义：左摇杆 lx=buffer[1], ly=buffer[0]
    uint16_t lx = adc_dma_buffer[1];
    uint16_t ly = adc_dma_buffer[0];

    // 【摇杆方向映射】：你需要根据你物理摇杆的实际电位器极性来调整这里！
    // 假设：X轴大于3000是右，小于1000是左；Y轴大于3000是下，小于1000是上
    if (lx > JOY_THRES_HIGH && current_dir != DIR_RIGHT)
    {
        current_dir = DIR_LEFT;
    }
    else if (lx < JOY_THRES_LOW && current_dir != DIR_LEFT)
    {
        current_dir = DIR_RIGHT;
    }
    else if (ly > JOY_THRES_HIGH && current_dir != DIR_DOWN)
    {
        current_dir = DIR_UP; // 如果方向反了，把 DIR_DOWN 和 DIR_UP 互换
    }
    else if (ly < JOY_THRES_LOW && current_dir != DIR_UP)
    {
        current_dir = DIR_DOWN;
    }
}

// 游戏主循环 (每 120ms 走一格)
static void game_timer_cb(lv_timer_t *t)
{
    if (game_over)
        return;

    // 1. 读取物理摇杆，决定下一步方向
    process_joystick_input();

    // 2. 记录尾巴的位置 (用于吃食物后身体变长)
    lv_coord_t tail_x = lv_obj_get_x(snake_body[snake_length - 1]);
    lv_coord_t tail_y = lv_obj_get_y(snake_body[snake_length - 1]);

    // 3. 身体跟着头移动
    for (int i = snake_length - 1; i > 0; i--)
    {
        lv_obj_set_pos(snake_body[i], lv_obj_get_x(snake_body[i - 1]), lv_obj_get_y(snake_body[i - 1]));
    }

    // 4. 蛇头移动
    lv_coord_t head_x = lv_obj_get_x(snake_body[0]);
    lv_coord_t head_y = lv_obj_get_y(snake_body[0]);
    switch (current_dir)
    {
    case DIR_UP:
        head_y -= BLOCK_SIZE;
        break;
    case DIR_DOWN:
        head_y += BLOCK_SIZE;
        break;
    case DIR_LEFT:
        head_x -= BLOCK_SIZE;
        break;
    case DIR_RIGHT:
        head_x += BLOCK_SIZE;
        break;
    }
    lv_obj_set_pos(snake_body[0], head_x, head_y);

    // 5. 碰撞检测：撞墙或咬到自己
    if (head_x < 0 || head_x >= 240 || head_y < 0 || head_y >= 320)
    {
        game_over = true;
    }
    for (int i = 1; i < snake_length; i++)
    {
        if (head_x == lv_obj_get_x(snake_body[i]) && head_y == lv_obj_get_y(snake_body[i]))
        {
            game_over = true;
        }
    }

    if (game_over)
    {
        lv_label_set_text_fmt(score_label, "Game Over!\nScore: %d\nTap to Retry", score);
        lv_obj_set_style_text_color(score_label, lv_palette_main(LV_PALETTE_RED), 0);
        return;
    }

    // 6. 吃食物检测
    if (head_x == lv_obj_get_x(food) && head_y == lv_obj_get_y(food))
    {
        if (snake_length < MAX_SNAKE_LENGTH)
        {
            // 增加一节身体
            snake_body[snake_length] = lv_obj_create(game_scr);
            lv_obj_set_size(snake_body[snake_length], BLOCK_SIZE, BLOCK_SIZE);
            lv_obj_set_style_bg_color(snake_body[snake_length], lv_palette_main(LV_PALETTE_GREEN), 0);
            lv_obj_set_style_border_width(snake_body[snake_length], 0, 0);
            lv_obj_set_style_radius(snake_body[snake_length], 2, 0);
            lv_obj_set_pos(snake_body[snake_length], tail_x, tail_y);
            snake_length++;
        }
        score += 10;
        lv_label_set_text_fmt(score_label, "Score: %d", score);
        spawn_food(); // 刷新食物
    }
}

// 游戏启动函数
void Start_Hidden_Snake_Game(void)
{
    snake_length = 3;
    current_dir = DIR_UP;
    game_over = false;
    score = 0;

    // 1. 创建黑底全屏画布
    game_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(game_scr, lv_color_black(), 0);
    // 绑定触摸事件：只有 Game Over 时点击屏幕才能退出
    lv_obj_add_event_cb(game_scr, game_scr_event_cb, LV_EVENT_CLICKED, NULL);

    // 2. 创建食物 (红色)
    food = lv_obj_create(game_scr);
    lv_obj_set_size(food, BLOCK_SIZE, BLOCK_SIZE);
    lv_obj_set_style_bg_color(food, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_width(food, 0, 0);
    lv_obj_set_style_radius(food, 5, 0); // 食物变圆一点

    // 3. 创建蛇的初始身体 (头是蓝色，身子是绿色)
    for (int i = 0; i < snake_length; i++)
    {
        snake_body[i] = lv_obj_create(game_scr);
        lv_obj_set_size(snake_body[i], BLOCK_SIZE, BLOCK_SIZE);
        lv_obj_set_style_bg_color(snake_body[i], (i == 0) ? lv_palette_main(LV_PALETTE_BLUE) : lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_obj_set_style_border_width(snake_body[i], 0, 0);
        lv_obj_set_style_radius(snake_body[i], 2, 0);
        lv_obj_set_pos(snake_body[i], 120, 160 + i * BLOCK_SIZE);
    }

    // 4. 创建分数/提示板
    score_label = lv_label_create(game_scr);
    lv_obj_set_style_text_color(score_label, lv_color_white(), 0);
    lv_label_set_text(score_label, "Score: 0");
    lv_obj_align(score_label, LV_ALIGN_TOP_MID, 0, 10);

    // 5. 退出按钮
    lv_obj_t *btn_exit = lv_btn_create(game_scr);
    lv_obj_set_size(btn_exit, 50, 30);
    lv_obj_align(btn_exit, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_set_style_bg_color(btn_exit, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(btn_exit, btn_exit_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_exit = lv_label_create(btn_exit);
    lv_label_set_text(lbl_exit, "X");
    lv_obj_center(lbl_exit);

    // 刷新第一个食物位置
    spawn_food();
    lv_scr_load(game_scr);

    // 启动游戏主循环引擎
    game_timer = lv_timer_create(game_timer_cb, GAME_SPEED_MS, NULL);
}



void EventCb_PinSave(lv_event_t * e)
{
    // 1. 获取三个输入框的内容
    const char *old_pin = lv_textarea_get_text(ui_TextAreaOldPin);
    const char *new_pin = lv_textarea_get_text(ui_TextAreaNewPin);
    const char *confirm_pin = lv_textarea_get_text(ui_TextAreaConfirmPin);

    // 2. 数据校验
    // 2.1 校验旧密码是否正确
    if (strcmp(old_pin, g_current_pin) != 0)
    {
        lv_label_set_text(ui_PinStatusLabel, "当前密码错误!");
        lv_obj_set_style_text_color(ui_PinStatusLabel, lv_palette_main(LV_PALETTE_RED), 0);
        Play_Beep(BEEP_ALARM_FAULT);
        return;
    }
    
    // 2.2 校验新密码长度是否为 4 位
    if (strlen(new_pin) != 4)
    {
        lv_label_set_text(ui_PinStatusLabel, "新密码必须为4位数字!");
        lv_obj_set_style_text_color(ui_PinStatusLabel, lv_palette_main(LV_PALETTE_RED), 0);
        Play_Beep(BEEP_ALARM_FAULT);
        return;
    }

    // 2.3 校验两次新密码是否一致
    if (strcmp(new_pin, confirm_pin) != 0)
    {
        lv_label_set_text(ui_PinStatusLabel, "两次输入的新密码不一致!");
        lv_obj_set_style_text_color(ui_PinStatusLabel, lv_palette_main(LV_PALETTE_RED), 0);
        Play_Beep(BEEP_ALARM_FAULT);
        return;
    }

    // 3. 所有校验通过，执行保存
    
    // 3.1 更新内存中的全局密码
    strcpy(g_current_pin, new_pin);

    // 3.2 准备数据并写入 Flash
    AppSettings_t settings_to_save;
    // 先加载一次旧设置，避免覆盖其他参数
    FlashStorage_LoadSettings(&settings_to_save);
    
    settings_to_save.magic_number = APP_SETTINGS_MAGIC_NUMBER; // 确保魔术字正确
    strcpy(settings_to_save.system_pin, g_current_pin);
    
    // 调用你已有的 Flash 保存函数
    FlashStorage_SaveSettings(&settings_to_save);

    // 4. 用户反馈
    lv_label_set_text(ui_PinStatusLabel, "密码修改成功!");
    lv_obj_set_style_text_color(ui_PinStatusLabel, lv_palette_main(LV_PALETTE_GREEN), 0);
    Play_Beep(BEEP_CONNECT);

    // 5. 延时1.5秒后自动返回设置界面
    osDelay(1500);
    lv_scr_load(ui_ScreenSettings);
}