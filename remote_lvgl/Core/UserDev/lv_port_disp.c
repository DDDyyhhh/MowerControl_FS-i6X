/**
  ******************************************************************************
  * @file           : lv_port_disp.c
  * @author         :[Your Name/Team]
  * @version        : V1.0
  * @date           : 2026-03-11
  * @brief          : LVGL 显示接口 (Display Port) 底层移植与 DMA 加速
  * 
  * 主要功能：
  * 1. 分配并管理 LVGL 的局部渲染显存缓冲区 (Draw Buffer)
  * 2. 将 LVGL 的渲染回调绑定到 ST7789 的硬件 SPI DMA 发送
  * 3. 利用 `HAL_SPI_TxCpltCallback` 异步通知 LVGL 帧刷新完成 (`lv_disp_flush_ready`)
  *
  * 注意事项：
  * - 此处依赖了 `LV_COLOR_16_SWAP` 的大端序设置，因此无需在软件中翻转 RGB565 高低字节。
  ******************************************************************************
  */
#include "lv_port_disp.h"
#include "lvgl.h"
#include "st7789.h" // 引用你原本的驱动

/* 定义缓冲区高度：1/10 屏幕高度 (320 * 240 / 10 = 7680 像素) */
/* 显存占用约 7680 * 2 = 15KB RAM */
#define DISP_HOR_RES 240
#define DISP_VER_RES 320
#define DRAW_BUF_SIZE (DISP_HOR_RES * DISP_VER_RES / 10)
extern SPI_HandleTypeDef hspi1;
static lv_disp_drv_t disp_drv;      // 显示驱动描述符
static lv_disp_draw_buf_t draw_buf; // 绘图缓冲区描述符
static lv_color_t buf_1[DRAW_BUF_SIZE]; // 实际的缓冲区

/* 刷新回调函数：LVGL 渲染完一块区域后调用此函数 */
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    // 1. 计算宽高
    uint16_t width = area->x2 - area->x1 + 1;
    uint16_t height = area->y2 - area->y1 + 1;

    // 2. 设置 ST7789 的显示窗口
    ST7789_SetAddressWindow(area->x1, area->y1, area->x2, area->y2);

    // 3. 发送数据
    // 你的 ST7789_Write_Datas 接收的是 uint8_t*
    // 由于开启了 LV_COLOR_16_SWAP，color_p 里的数据已经是大端序，直接发即可
    ST7789_Select();
    ST7789_DC_Set(); // 写数据模式
    
     // 4. 计算数据长度 (字节数)
    // ST7789 是 16位色，所以像素数 * 2
    uint32_t size = width * height * 2;

    // 5. 启动 DMA 传输
    // 注意：这里不再调用 ST7789_UnSelect()，也不调用 lv_disp_flush_ready()
    // 它们必须移到 DMA 完成中断里去执行！
    HAL_SPI_Transmit_DMA(&hspi1, (uint8_t *)color_p, size);
}

void lv_port_disp_init(void)
{

    // 2. 初始化显示缓冲区
    lv_disp_draw_buf_init(&draw_buf, buf_1, NULL, DRAW_BUF_SIZE);

    // 3. 初始化显示驱动
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISP_HOR_RES;
    disp_drv.ver_res = DISP_VER_RES;
    disp_drv.flush_cb = disp_flush; // 设置回调
    disp_drv.draw_buf = &draw_buf;  // 设置缓冲区

    // 4. 注册驱动
    lv_disp_drv_register(&disp_drv);
}

/**
 * @brief SPI DMA 发送完成回调函数
 * @param hspi SPI句柄
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    // 判断是否是屏幕用的 SPI1
    if (hspi->Instance == SPI1)
    {
        // 1. 传输完成，拉高 CS 结束通信
        ST7789_UnSelect();

        // 2. 通知 LVGL 这一帧刷新结束，可以准备下一帧了
        // 注意：disp_drv 是你在 lv_port_disp_init 里定义的那个静态变量
        // 如果你的 disp_drv 定义在函数内部，请把它移到全局变量 (static)
        lv_disp_flush_ready(&disp_drv); 
    }
}
