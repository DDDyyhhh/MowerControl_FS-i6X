#include "lv_port_indev.h"
#include "ft6336u.h"
#include "lvgl.h"

static lv_indev_drv_t indev_drv;

/* 回调函数：LVGL 会定期调用这个函数来询问触摸状态 */
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static FT6336U_TouchPoint_t touch_data;

    // 调用我们在裸机测试中完美运行的读坐标函数
    if(FT6336U_ReadTouch(&touch_data) == true) {
        data->state = LV_INDEV_STATE_PRESSED; // 【修复】LVGL v8 标准宏
        data->point.x = touch_data.x;
        data->point.y = touch_data.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED; // 【修复】LVGL v8 标准宏
    }
}

void lv_port_indev_init(void)
{
    // 【修复】移除错误的 XPT2046 初始化，改为确保 FT6336U 初始化
    // (如果在 main.c 已经初始化过，这里再调一次通常无妨，或者直接注释掉)
    // FT6336U_Init(); 

    static lv_indev_t * indev_touchpad;
    
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read; 
    indev_touchpad = lv_indev_drv_register(&indev_drv);
}