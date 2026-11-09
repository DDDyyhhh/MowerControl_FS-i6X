/**
  ******************************************************************************
  * @file           : st7789.c
  * @author         : [Your Name/Team]
  * @version        : V1.2
  * @date           : 2026-03-11
  * @brief          : ST7789 SPI LCD 底层驱动与显示优化算法
  * 
  * 主要功能：
  * 1. ST7789 初始化序列发送，以及屏幕方向与色彩模式配置
  * 2. 提供设置显示窗口与 DMA 连发的高效 API (`ST7789_FillRect`, `ST7789_Clear`)
  *
  * 注意事项：
  * - 【防撕裂优化】: 初始化时利用 `0x28` 关闭 Display，涂黑显存后再使用 `0x29` 开启，
  *   完美规避了 SPI 屏幕上电瞬间的雪花和扫描撕裂感。
  ******************************************************************************
  */
#include "st7789.h"
#include "string.h"
#include "stdarg.h"
#include "stdio.h"
#include "math.h"
#include "stdlib.h"
#include "stdbool.h"
// --- 新增：遥控器自身电池管理所需的全局/静态变量 ---
static uint16_t last_filtered_value = 0;
static uint16_t candidate_value = 0;
static uint8_t confirm_counter = 0;
#define CONFIRM_COUNT 5

static void ST7789_Write_Cmd(uint8_t cmd)
{
    ST7789_Select();
    // ST7789_DC_Clr();
    HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&ST7789_SPI_PORT, &cmd, sizeof(cmd), HAL_MAX_DELAY);
    ST7789_UnSelect();
}
static void ST7789_Write_Data(uint8_t data)
{
    ST7789_Select();
    // ST7789_DC_Clr();
    HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET);
    HAL_SPI_Transmit(&ST7789_SPI_PORT, &data, sizeof(data), HAL_MAX_DELAY);
    ST7789_UnSelect();
}
static void ST7789_Write_Datas(uint8_t *buff, size_t buff_size)
{
    ST7789_Select();
    ST7789_DC_Clr();
    HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET);
    while (buff_size > 0)
    {
        uint16_t chunk_size = buff_size > 65535 ? 65535 : buff_size;
        HAL_SPI_Transmit(&ST7789_SPI_PORT, buff, chunk_size, HAL_MAX_DELAY);

        buff += chunk_size; // 记录当前发送的位置，用于下次发送
        buff_size -= chunk_size;
    }
    ST7789_UnSelect();
}

void ST7789_Init()
{
    // 1. 【硬件层】先关背光 (防止漏光)
    HAL_GPIO_WritePin(ST7789_BLK_PORT, ST7789_BLK_PIN, GPIO_PIN_RESET);

    // 2. 【硬件层】复位 LCD
    HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(120);

    // 3. 【关键步骤】退出睡眠
    ST7789_Write_Cmd(0x11); 
    HAL_Delay(120); // 必须等待电源稳定

    // ============================================================
    // 核心修复：强制关闭显示 (Display OFF)
    // 即使 0x11 让屏幕自动亮了，这句指令会立刻关闭像素输出
    // 此时屏幕处于 "Sleep Out" (能写数据) 但 "Display Off" (面板不显示)
    // ============================================================
    ST7789_Write_Cmd(0x28); 

    // 4. 参数配置 (保持不变)
    ST7789_Write_Cmd(0xB4);
    ST7789_Write_Data(0x0C);
    ST7789_Write_Data(0x0C);
    ST7789_Write_Data(0x00);
    ST7789_Write_Data(0x33);
    ST7789_Write_Data(0x33);

    ST7789_Write_Cmd(0xB7); 
    ST7789_Write_Data(0x35);

    ST7789_Write_Cmd(0xBB);
    ST7789_Write_Data(0x19);

    ST7789_Write_Cmd(0xC0);
    ST7789_Write_Data(0x2C);

    ST7789_Write_Cmd(0xC2);
    ST7789_Write_Data(0x01);

    ST7789_Write_Cmd(0xC3);
    ST7789_Write_Data(0x12);

    ST7789_Write_Cmd(0xC4);
    ST7789_Write_Data(0x20);

    ST7789_Write_Cmd(0xC6);
    ST7789_Write_Data(0x0F);

    ST7789_Write_Cmd(0xD0);
    ST7789_Write_Data(0xA4);
    ST7789_Write_Data(0xA1);

    // Gamma
    ST7789_Write_Cmd(0xE0);
    {
        uint8_t data[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
        ST7789_Write_Datas(data, sizeof(data));
    }
    ST7789_Write_Cmd(0xE1);
    {
        uint8_t data[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
        ST7789_Write_Datas(data, sizeof(data));
    }

    // 5. 设置方向
    ST7789_Write_Cmd(0x36);
    ST7789_Write_Data(0xC0); 
    
    ST7789_Write_Cmd(0x3A);
    ST7789_Write_Data(0x55);

    ST7789_Write_Cmd(0x21);

    // 6. 【暗箱操作】涂黑显存
    // 此时因为发了 0x28，面板是关的，用户看不见这个涂黑的过程
    ST7789_Clear(WHITE);

    // 7. 【最后一步】开启显示
    // 此时显存已经是全黑了，开启瞬间就是纯黑，没有扫描线
    ST7789_Write_Cmd(0x29);
    
    // 8. 注意：依然不要在这里开背光！
    // 让 FreeRTOS 任务画完 UI 后再开。
    HAL_GPIO_WritePin(ST7789_BLK_PORT, ST7789_BLK_PIN, GPIO_PIN_SET);

    
}

void ST7789_SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint16_t x_start = x0, x_end = x1;
    uint16_t y_start = y0, y_end = y1;

    /* Column Address set */
    ST7789_Write_Cmd(0x2A);
    {
        uint8_t data[] = {x_start >> 8, x_start & 0xFF, x_end >> 8, x_end & 0xFF};
        ST7789_Write_Datas(data, sizeof(data));
    }

    /* Row Address set */
    ST7789_Write_Cmd(0x2B);
    {
        uint8_t data[] = {y_start >> 8, y_start & 0xFF, y_end >> 8, y_end & 0xFF};
        ST7789_Write_Datas(data, sizeof(data));
    }
    /* Write to RAM */
    ST7789_Write_Cmd(0x2C);
}

// void ST7789_DrawImage_Scale(uint16_t x, uint16_t y, uint16_t src_w, uint16_t src_h,
//                             uint16_t dst_w, uint16_t dst_h, const uint16_t *src)
// {
//     for (uint16_t j = 0; j < dst_h; j++)
//     {
//         for (uint16_t i = 0; i < dst_w; i++)
//         {
//             // 计算原图对应像素点
//             uint16_t src_x = i * src_w / dst_w;
//             uint16_t src_y = j * src_h / dst_h;
//             uint16_t color = src[src_y * src_w + src_x];
//             ST7789_DrawPoint(x + i, y + j, color);
//         }
//     }
// }

// // void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
// // {
// //     if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT)
// //         return;
// //     if ((x + w - 1) >= ST7789_WIDTH)
// //         w = ST7789_WIDTH - x;
// //     if ((y + h - 1) >= ST7789_HEIGHT)
// //         h = ST7789_HEIGHT - y;

// //     static uint16_t line_buf[ST7789_WIDTH];
// //     for (uint16_t i = 0; i < w; i++)
// //     {
// //         line_buf[i] = color;
// //     }
// //     for (uint16_t j = 0; j < h; j++)
// //     {
// //         ST7789_SetAddressWindow(x, y + j, x + w - 1, y + j);
// //         ST7789_Write_Datas((uint8_t *)line_buf, w * 2);
// //     }
// // }

/**
 * @brief  【已优化】使用指定颜色填充一个矩形区域
 * @param  x, y: 起始坐标
 * @param  w, h: 宽度和高度
 * @param  color: 16-bit RGB565 颜色
 * @retval None
 */
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if ((x >= ST7789_WIDTH) || (y >= ST7789_HEIGHT))
        return;
    if ((x + w - 1) >= ST7789_WIDTH)
        w = ST7789_WIDTH - x;
    if ((y + h - 1) >= ST7789_HEIGHT)
        h = ST7789_HEIGHT - y;

    ST7789_SetAddressWindow(x, y, x + w - 1, y + h - 1);

    // 准备一个足够大的缓冲区，比如一行。使用 static 避免栈溢出
    static uint8_t line_buf[ST7789_WIDTH * 2];

    // 【关键修正】手动按大端序填充颜色数据
    uint8_t color_high = color >> 8;
    uint8_t color_low = color & 0xFF;

    // 只填充缓冲区一次
    for (uint16_t i = 0; i < w; i++)
    {
        line_buf[i * 2] = color_high;
        line_buf[i * 2 + 1] = color_low;
    }

    uint32_t fill_len_bytes = w * 2; // 一行的数据长度 (字节)

    // 循环发送这一行的数据 h 次
    for (uint16_t i = 0; i < h; i++)
    {
        ST7789_Write_Datas(line_buf, fill_len_bytes);
    }
}

// // void ST7789_Clear(uint16_t color)
// // {
// //     static uint16_t line_buf[ST7789_WIDTH];
// //     for (uint16_t i = 0; i < ST7789_WIDTH; i++)
// //     {
// //         line_buf[i] = color;
// //     }
// //     ST7789_SetAddressWindow(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1);
// //     for (uint16_t y = 0; y < ST7789_HEIGHT; y++)
// //     {
// //         ST7789_Write_Datas((uint8_t *)line_buf, sizeof(line_buf));
// //     }
// // }

/**
 * @brief  【已优化】清屏
 * @param  color: 16-bit RGB565 颜色
 * @retval None
 */
void ST7789_Clear(uint16_t color)
{
    ST7789_FillRect(0, 0, ST7789_WIDTH, ST7789_HEIGHT, color);
}

// void ST7789_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
// {

//     if ((x < 0) || (x >= ST7789_WIDTH) || (y < 0) || (y >= ST7789_HEIGHT))
//         return;

//     ST7789_SetAddressWindow(x, y, x, y);
//     uint8_t data[] = {color >> 8, color & 0xFF}; // 高位和低位
//     ST7789_Write_Datas(data, sizeof(data));
// }
// void ST7789_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
// {
//     uint16_t swap;
//     uint16_t steep = ABS(y1 - y0) > ABS(x1 - x0);
//     if (steep)
//     {
//         swap = x0;
//         x0 = y0;
//         y0 = swap;

//         swap = x1;
//         x1 = y1;
//         y1 = swap;
//     }

//     if (x0 > x1)
//     {
//         swap = x0;
//         x0 = x1;
//         x1 = swap;

//         swap = y0;
//         y0 = y1;
//         y1 = swap;
//     }

//     int16_t dx, dy;
//     dx = x1 - x0;
//     dy = ABS(y1 - y0);

//     int16_t err = dx / 2;
//     int16_t ystep;

//     if (y0 < y1)
//     {
//         ystep = 1;
//     }
//     else
//     {
//         ystep = -1;
//     }

//     for (; x0 <= x1; x0++)
//     {
//         if (steep)
//         {
//             ST7789_DrawPoint(y0, x0, color);
//         }
//         else
//         {
//             ST7789_DrawPoint(x0, y0, color);
//         }
//         err -= dy;
//         if (err < 0)
//         {
//             y0 += ystep;
//             err += dx;
//         }
//     }
// }

// void ST7789_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color, uint8_t isFilled)
// {
//     if (isFilled == 0)
//     {
//         ST7789_DrawLine(x1, y1, x2, y1, color);
//         ST7789_DrawLine(x1, y1, x1, y2, color);
//         ST7789_DrawLine(x1, y2, x2, y2, color);
//         ST7789_DrawLine(x2, y1, x2, y2, color);
//     }
//     else
//     {
//         uint16_t width = x2 - x1 + 1;
//         static uint8_t line_buf[ST7789_WIDTH * 2]; // 每像素2字节
//         for (uint16_t i = 0; i < width; i++)
//         {
//             line_buf[2 * i] = color >> 8;       // 高字节
//             line_buf[2 * i + 1] = color & 0xFF; // 低字节
//         }
//         for (uint16_t y = y1; y <= y2; y++)
//         {
//             ST7789_SetAddressWindow(x1, y, x2, y);
//             ST7789_Write_Datas(line_buf, width * 2);
//         }
//     }
// }
// void ST7789_DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data)
// {
//     if ((x >= ST7789_WIDTH) || (y >= ST7789_HEIGHT))
//         return;
//     if ((x + w - 1) >= ST7789_WIDTH)
//         return;
//     if ((y + h - 1) >= ST7789_HEIGHT)
//         return;

//     ST7789_SetAddressWindow(x, y, x + w - 1, y + h - 1);
//     ST7789_Write_Datas((uint8_t *)data, sizeof(uint16_t) * w * h);
// }

// void ST7789_ShowChar(uint16_t x, uint16_t y, uint16_t color, uint16_t bgcolor, char ch, FontDef font)
// {
//     uint8_t line_buf[font.width * 2]; // 每像素2字节
//     for (uint32_t i = 0; i < font.height; i++)
//     {
//         uint32_t b = font.data[(ch - 32) * font.height + i];
//         for (uint32_t j = 0; j < font.width; j++)
//         {
//             uint16_t pixel_color = ((b << j) & 0x8000) ? color : bgcolor;
//             line_buf[2 * j] = pixel_color >> 8;       // 高字节
//             line_buf[2 * j + 1] = pixel_color & 0xFF; // 低字节
//         }
//         ST7789_SetAddressWindow(x, y + i, x + font.width - 1, y + i);
//         ST7789_Write_Datas(line_buf, font.width * 2);
//     }
// }

// void ST7789_ShowString(uint16_t x, uint16_t y, const char *str, FontDef font, uint16_t color, uint16_t bgcolor)
// {
//     while (*str)
//     {
//         if (x + font.width >= ST7789_WIDTH)
//         {
//             x = 0;
//             y += font.height;
//             if (y + font.height >= ST7789_HEIGHT)
//             {
//                 break;
//             }

//             if (*str == ' ')
//             {
//                 // skip spaces in the beginning of the new line
//                 str++;
//                 continue;
//             }
//         }
//         ST7789_ShowChar(x, y, color, bgcolor, *str, font);
//         x += font.width;
//         str++;
//     }
// }

// void ST7789_ShowChinese(uint16_t x, uint16_t y, uint16_t color, uint16_t bgcolor, const uint8_t *Chinese, uint8_t scale)
// {
//     uint8_t line_buf[16 * scale * 2];
//     for (uint32_t i = 0; i < 16; i++)
//     { // 16行
//         uint32_t idx = 0;
//         for (uint32_t j = 0; j < 2; j++)
//         { // 每行2字节
//             uint8_t byte = Chinese[i * 2 + j];
//             for (int b = 0; b < 8; b++)
//             { // 低位在左
//                 uint16_t color_to_draw = ((byte >> b) & 0x01) ? color : bgcolor;
//                 for (uint8_t dx = 0; dx < scale; dx++)
//                 {
//                     line_buf[idx++] = color_to_draw >> 8;   // 高字节
//                     line_buf[idx++] = color_to_draw & 0xFF; // 低字节
//                 }
//             }
//         }
//         // 放大scale倍，写scale行
//         for (uint8_t dy = 0; dy < scale; dy++)
//         {
//             uint16_t py = y + i * scale + dy;
//             ST7789_SetAddressWindow(x, py, x + 16 * scale - 1, py);
//             ST7789_Write_Datas(line_buf, 2 * 16 * scale);
//         }
//     }
// // }
// // void ST7789_ShowChineses(int16_t X, int16_t Y, uint16_t color, uint16_t bgcolor, char *Chinese, uint8_t scale)
// // {
// //     uint8_t pChinese = 0;
// //     uint8_t pIndex;
// //     uint8_t i = 0;
// //     char SingleChinese[4] = {0};

// //     for (i = 0; Chinese[i] != '\0'; i++)
// //     {
// //         SingleChinese[pChinese] = Chinese[i];
// //         pChinese++;
// //         if (pChinese >= 3)
// //         {
// //             SingleChinese[3] = '\0';
// //             pChinese = 0;
// //             for (pIndex = 0; strcmp(OLED_CF16x16[pIndex].Index, "") != 0; pIndex++)
// //             {
// //                 if (strcmp(OLED_CF16x16[pIndex].Index, SingleChinese) == 0)
// //                 {
// //                     break;
// //                 }
// //             }
// //             // 每个汉字宽度scale*16
// //             ST7789_ShowChinese(X + ((i + 1) / 3 - 1) * 16 * scale, Y, color, bgcolor, OLED_CF16x16[pIndex].Data, scale);
// //         }
// //     }
// // }

// static uint32_t ST7789_Pow(uint32_t X, uint32_t Y)
// {
//     uint32_t Result = 1; // 结果默认为1
//     while (Y--)          // 累乘Y次
//     {
//         Result *= X; // 每次把X累乘到结果上
//     }
//     return Result;
// }
// void ST7789_ShowBinNum(int16_t X, int16_t Y, uint16_t color, uint16_t bgcolor, uint32_t Number, uint8_t Length, FontDef font)
// {
//     uint8_t i;
//     for (i = 0; i < Length; i++) // 遍历数字的每一位
//     {
//         /*调用OLED_ShowChar函数，依次显示每个数字*/
//         /*Number / OLED_Pow(2, Length - i - 1) % 2 可以二进制提取数字的每一位*/
//         /*+ '0' 可将数字转换为字符格式*/
//         ST7789_ShowChar(X + i * font.width, Y, color, bgcolor, Number / ST7789_Pow(2, Length - i - 1) % 2 + '0', font);
//     }
// }

// // =======格式化打印函数=======
// void ST7789_Printf(int16_t X, int16_t Y, uint16_t color, uint16_t bgcolor, FontDef font, char *format, ...)
// {
//     char String[256];                                      // 定义字符数组
//     va_list arg;                                           // 定义可变参数列表数据类型的变量arg
//     va_start(arg, format);                                 // 从format开始，接收参数列表到arg变量
//     vsprintf(String, format, arg);                         // 使用vsprintf打印格式化字符串和参数列表到字符数组中
//     va_end(arg);                                           // 结束变量arg
//     ST7789_ShowString(X, Y, String, font, color, bgcolor); // OLED显示字符数组（字符串）
// }

// // =======绘制信号阶梯函数=======
// void DrawSignalStairs(uint16_t x, uint16_t y, uint8_t level, uint16_t color_on, uint16_t color_off)
// {
//     uint8_t i;
//     uint8_t max_level = 4;
//     uint8_t bar_w = 6; // 每格宽度
//     uint8_t bar_h = 7; // 每格高度
//     uint8_t gap = 2;   // 格间距

//     for (i = 0; i < max_level; i++)
//     {
//         uint16_t color = (i < level) ? color_on : color_off;
//         // 每一格的左下角坐标
//         uint16_t bar_x1 = x + i * (bar_w + gap);
//         uint16_t bar_y1 = y - (i + 1) * bar_h;
//         uint16_t bar_x2 = bar_x1 + bar_w;
//         uint16_t bar_y2 = y;

//         ST7789_DrawRectangle(bar_x1, bar_y1, bar_x2, bar_y2, color, 1);
//     }
// }

// // =======绘制垂直滑块函数=======
// void DrawVerticalSlider(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t value, uint16_t max_value, uint16_t color, uint16_t bg_color)
// {
//     uint8_t cell_height = height / max_value; // 每个格子的高度
//     uint8_t gap = 1;                          // 格子间距

//     // 绘制所有格子（灰色背景）
//     for (uint8_t i = 0; i < max_value; i++)
	
//     {
//         uint16_t cell_y = y + i * cell_height;
//         ST7789_DrawRectangle(x, cell_y, x + width, cell_y + cell_height - gap, bg_color, 1);
//     }

//     if (value <= max_value)
//     {
//         uint16_t cell_y;
//         if (value == 0)
//         {
//             // 0值显示在最底部
//             cell_y = y + (max_value - 1) * cell_height;
//         }
//         else
//         {
//             // 其他值从底部开始计算位置
//             cell_y = y + (max_value - value) * cell_height;

//             ST7789_DrawRectangle(x, cell_y, x + width, cell_y + cell_height - gap, color, 1);
//         }
//     }
//     // 绘制边框
//     ST7789_DrawRectangle(x, y, x + width, y + height, BLACK, 0);
// }

// // =======绘制状态滑块函数=======
// void DrawStatusSlider(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t is_on, uint16_t on_color, uint16_t off_color)
// {
//     uint16_t mid_y = y + height / 2; // 中间位置

//     // 绘制上部分（打开状态区域）
//     if (is_on)
//     {
//         ST7789_DrawRectangle(x, y, x + width, mid_y, on_color, 1);      // 上部分绿色
//         ST7789_DrawRectangle(x, mid_y, x + width, y + height, GRAY, 1); // 下部分灰色
//     }
//     else
//     {
//         ST7789_DrawRectangle(x, y, x + width, mid_y, GRAY, 1);               // 上部分灰色
//         ST7789_DrawRectangle(x, mid_y, x + width, y + height, off_color, 1); // 下部分红色
//     }

//     // 绘制边框
//     ST7789_DrawRectangle(x, y, x + width, y + height, BLACK, 0);

//     // 绘制中间分隔线
//     ST7789_DrawLine(x, mid_y, x + width, mid_y, BLACK);
// }



// // =======绘制锁图标函数=======
// void DrawLockIcon(uint16_t x, uint16_t y, uint16_t size, uint8_t is_unlocked, uint16_t unlocked_color, uint16_t locked_color)
// {
//     static uint8_t last_status = 0xFF;

//     if (last_status != is_unlocked)
//     {
//         last_status = is_unlocked;

//         // 清除区域
//         uint16_t clear_margin = 6;
//         ST7789_DrawRectangle(x - size / 2 - clear_margin,
//                              y - size / 2 - clear_margin,
//                              x + size / 2 + clear_margin,
//                              y + size + clear_margin,
//                              WHITE, 1);

//         uint16_t color = is_unlocked ? unlocked_color : locked_color;

//         // 绘制锁环基础部分
//         ST7789_DrawRectangle(x - size / 4, y - size / 3, x + size / 4, y - size / 6, color, 1); // 上横梁
//         ST7789_DrawRectangle(x - size / 4, y - size / 3, x - size / 4 + 2, y, color, 1);        // 左竖梁

//         if (is_unlocked)
//         {
//             // 解锁状态：显示明显的解锁箭头

//             // 清除右竖梁区域
//             ST7789_DrawRectangle(x + size / 4 - 2, y - size / 3, x + size / 4 + 8, y, WHITE, 1);
//         }
//         else
//         {
//             // 锁定状态：完整的U形锁环
//             ST7789_DrawRectangle(x + size / 4 - 2, y - size / 3, x + size / 4, y, color, 1); // 右竖梁
//         }

//         // 绘制锁体
//         ST7789_DrawRectangle(x - size / 3, y, x + size / 3, y + size / 2, color, 1);
//     }
// }

// // =======绘制简单挡位指示器函数=======
// void DrawSimpleGearArc(uint16_t x, uint16_t y, uint16_t radius, uint8_t gear)
// {
//     static uint8_t last_gear = 0xFF;

//     if (last_gear != gear)
//     {
//         last_gear = gear;

//         // 清除整个半圆区域（从y-radius到y）
//         ST7789_DrawRectangle(x - radius, y - radius, x + radius, y, WHITE, 1);

//         // 定义挡位颜色
//         uint16_t gear_colors[3] = {GREEN, YELLOW, RED}; // L/M/H 对应 绿/黄/红

//         // 绘制半圆边框
//         for (int16_t py = -radius; py <= 0; py++)
//         {
//             int16_t chord_length = (int16_t)sqrt(radius * radius - py * py);

//             // 绘制圆弧边框
//             ST7789_DrawPoint(x - chord_length, y + py, BLACK);
//             ST7789_DrawPoint(x + chord_length, y + py, BLACK);

//             // 如果需要更粗的边框，可以添加这些点
//             ST7789_DrawPoint(x - chord_length - 1, y + py, BLACK);
//             ST7789_DrawPoint(x + chord_length + 1, y + py, BLACK);
//         }

//         // 绘制分隔线 - 修正角度
//         for (uint8_t i = 1; i < 3; i++)
//         {
//             float angle = 180.0 + (i * 60.0); // 修正：从180°开始，每60°一个分隔线
//             float rad = angle * 3.14159 / 180.0;
//             int16_t end_x = x + radius * cos(rad);
//             int16_t end_y = y + radius * sin(rad);
//             ST7789_DrawLine(x, y, end_x, end_y, BLACK);
//         }

//         // 绘制挡位标签（始终显示）
//         ST7789_ShowString(x - radius + 5, y + radius / 3, "L", Font_11x18, BLACK, WHITE);
//         ST7789_ShowString(x - 5, y + radius / 3, "M", Font_11x18, BLACK, WHITE);
//         ST7789_ShowString(x + radius - 15, y + radius / 3, "H", Font_11x18, BLACK, WHITE);

//         // 只绘制当前挡位对应的扇形
//         if (gear >= 1 && gear <= 3)
//         {
//             uint16_t color = gear_colors[gear - 1];

//             // 计算扇形角度范围
//             float start_angle, end_angle;
//             switch (gear)
//             {
//             case 1: // L挡位：左侧扇形 180°-240°
//                 start_angle = 180.0;
//                 end_angle = 240.0;
//                 break;
//             case 2: // M挡位：中间扇形 240°-300°
//                 start_angle = 240.0;
//                 end_angle = 300.0;
//                 break;
//             case 3: // H挡位：右侧扇形 300°-360°
//                 start_angle = 300.0;
//                 end_angle = 360.0;
//                 break;
//             default:
//                 return;
//             }

//             // 绘制扇形
//             for (int16_t py = -radius; py <= 0; py++)
//             {
//                 int16_t chord_length = (int16_t)sqrt(radius * radius - py * py);

//                 for (int16_t px = -chord_length; px <= chord_length; px++)
//                 {
//                     float angle = atan2(py, px) * 180.0 / 3.14159;
//                     if (angle < 0)
//                         angle += 360.0;

//                     if (angle >= start_angle && angle <= end_angle)
//                     {
//                         ST7789_DrawPoint(x + px, y + py, color);
//                     }
//                 }
//             }
//         }
//     }
// }

// // =======绘制电池图标函数=======

// void draw_battery_icon(uint16_t x, uint16_t y, uint8_t percentage)
// {
//     // 电池主体 (40x20像素)
//     ST7789_DrawRectangle(x, y, x + 40, y + 20, WHITE, 1);         // 外框
//     ST7789_DrawRectangle(x + 2, y + 2, x + 38, y + 18, BLACK, 1); // 内部填充

//     // 电池正极 (8x4像素)
//     ST7789_DrawRectangle(x + 38, y + 8, x + 40, y + 12, BLACK, 1);

//     // 电量填充 (根据百分比)
//     uint8_t fill_width = (percentage * 32) / 100; // 32像素宽度对应100%
//     if (fill_width > 0)
//     {
//         // 根据电量选择颜色
//         uint16_t color;
//         if (percentage > 50)
//             color = GREEN;
//         else if (percentage > 20)
//             color = YELLOW;
//         else
//             color = RED;

//         ST7789_DrawRectangle(x + 4, y + 4, x + 4 + fill_width, y + 16, color, 1);
//     }
// }

// // =======滤波函数=======
// // ======= 终极优化滤波函数 (带迟滞) =======
// uint16_t filter_adc_value(uint16_t new_value)
// {
//     // 1. 平滑滤波变量
//     static float smooth_adc = 0;
//     // 2. 【新增】锁定变量：用于计算的稳定ADC值
//     static uint16_t locked_adc = 0;

//     // 初始化
//     if (smooth_adc == 0) 
//     {
//         smooth_adc = new_value; 
//         locked_adc = new_value;
//     }
    
//     // --- 第一步：加强平滑滤波 ---
//     // 改为 95% 历史权重 + 5% 新值权重
//     // 反应会比之前稍慢一点点，但数值会极其稳定，像粘稠的蜂蜜一样
//     smooth_adc = (smooth_adc * 0.95f) + ((float)new_value * 0.05f);

//     // --- 第二步：【核心】迟滞/死区逻辑 ---
//     // 计算当前平滑值与上一次锁定的值的差距
//     // 你的总量程是 3723 - 3040 = 683。
//     // 1% 大约对应 6.8 个 ADC数值。
//     // 我们设置死区为 4 (大约 0.6%)。意味着：除非电压变化超过 0.6%，否则不改变显示。
//     if (abs((int)smooth_adc - (int)locked_adc) > 5)
//     {
//         locked_adc = (uint16_t)smooth_adc; // 只有变化够大，才更新锁定值
//     }

//     // --- 第三步：使用锁定值进行计算 ---
//     // 注意：这里用 locked_adc 代替了之前的 raw_adc
//     uint16_t calc_adc = locked_adc; 
//     uint8_t percentage = 0;

//     if (calc_adc <= 3040)
//     {
//         percentage = 0;
//     }
//     else if (calc_adc >= 3723)
//     {
//         percentage = 100;
//     }
//     else
//     {
//         percentage = (uint8_t)(((long)(calc_adc - 3040) * 100) / (3723 - 3040));
//     }

//     // --- 第四步：防抖确认 (保持不变) ---
//     if (percentage != last_filtered_value)
//     {
//         if (percentage == candidate_value)
//         {
//             confirm_counter++;
//             // 既然前面已经加了强滤波和死区，这里的确认次数可以不用那么大，20次足够了
//             // 保持原样或者改为 20 都可以
//             if (confirm_counter >= CONFIRM_COUNT) 
//             {
//                 last_filtered_value = percentage;
//                 confirm_counter = 0;
//             }
//         }
//         else
//         {
//             candidate_value = percentage;
//             confirm_counter = 1;
//         }
//     }
//     else
//     {
//         confirm_counter = 0;
//     }

//     return last_filtered_value;
// }


// /**
//  * @brief  【高级优化版】绘制雷达避障区，仅重绘状态变化的扇区
//  * @param  ... (参数不变) ...
//  * @retval None
//  */
// void DrawSemiCircleObstacleZones(uint16_t center_x, uint16_t center_y, uint16_t radius,
//                                  uint8_t left_75_obstacle, uint8_t left_25_obstacle,
//                                  uint8_t front_obstacle, uint8_t right_25_obstacle,
//                                  uint8_t right_75_obstacle)
// {
//     // --- 静态变量缓存 ---
//     static uint8_t last_obstacles[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
//     static bool is_first_draw = true; // 标记是否为第一次绘制

//     // --- 将当前状态存入数组 ---
//     uint8_t current_obstacles[5] = {
//         left_75_obstacle, left_25_obstacle, front_obstacle,
//         right_25_obstacle, right_75_obstacle
//     };

//     // --- 准备静态绘图数据 ---
//     const float sector_angles[5][2] = {
//         {-180.0, -144.0}, {-144.0, -108.0}, {-108.0, -72.0},
//         {-72.0, -36.0},   {-36.0, 0.0}
//     };
//     const uint16_t colors[3] = {GREEN, YELLOW, RED}; // 0, 1, 2

//     // --- 首次绘制逻辑：绘制完整的背景和分隔线 ---
//     if (is_first_draw)
//     {
//         // 1. 清除整个区域
//         ST7789_DrawRectangle(center_x - radius - 2, center_y - radius - 2,
//                              center_x + radius + 2, center_y + 2, WHITE, 1);

//         // 2. 绘制所有扇区的初始状态
//         for (int i = 0; i < 5; i++)
//         {
//             uint8_t state = current_obstacles[i];
//             if (state <= 2) {
//                 DrawSemiCircleSector(center_x, center_y, radius,
//                                      sector_angles[i][0], sector_angles[i][1], colors[state]);
//             }
//         }
        
//         // 3. 绘制分隔线 (只画一次)
//         for (int i = 1; i < 5; i++)
//         {
//             float angle_rad = sector_angles[i][0] * 3.14159 / 180.0;
//             int16_t end_x = center_x + radius * cos(angle_rad);
//             int16_t end_y = center_y + radius * sin(angle_rad);
//             ST7789_DrawLine(center_x, center_y, end_x, end_y, BLACK);
//         }

//         is_first_draw = false; // 标记首次绘制已完成
//     }
//     else // --- 非首次绘制：只更新变化的扇区 ---
//     {
//         for (int i = 0; i < 5; i++)
//         {
//             // 【核心优化】逐个比较每个扇区的状态
//             if (current_obstacles[i] != last_obstacles[i])
//             {
//                 // 状态已改变，只重绘这一个扇区
//                 uint8_t state = current_obstacles[i];
//                 if (state <= 2) { // 安全检查
//                     DrawSemiCircleSector(center_x, center_y, radius,
//                                          sector_angles[i][0], sector_angles[i][1], colors[state]);
                    
//                     // 【可选但推荐】重绘与该扇区相邻的分隔线，防止被扇区颜色覆盖
//                     // 如果不是第一个扇区，重绘左边的分隔线
//                     if (i > 0) {
//                         float angle_rad_left = sector_angles[i][0] * 3.14159 / 180.0;
//                         int16_t end_x_left = center_x + radius * cos(angle_rad_left);
//                         int16_t end_y_left = center_y + radius * sin(angle_rad_left);
//                         ST7789_DrawLine(center_x, center_y, end_x_left, end_y_left, BLACK);
//                     }
//                     // 如果不是最后一个扇区，重绘右边的分隔线
//                     if (i < 4) {
//                         float angle_rad_right = sector_angles[i][1] * 3.14159 / 180.0;
//                         int16_t end_x_right = center_x + radius * cos(angle_rad_right);
//                         int16_t end_y_right = center_y + radius * sin(angle_rad_right);
//                         ST7789_DrawLine(center_x, center_y, end_x_right, end_y_right, BLACK);
//                     }
//                 }
//             }
//         }
//     }

//     // --- 无论如何，最后都要更新缓存，为下一次比较做准备 ---
//     memcpy(last_obstacles, current_obstacles, sizeof(current_obstacles));
// }



// // 绘制半圆扇形区域
// void DrawSemiCircleSector(uint16_t center_x, uint16_t center_y, uint16_t radius,
//                           float start_angle, float end_angle, uint16_t color)
// {
//     // 将角度转换为弧度
//     float start_rad = start_angle * 3.14159 / 180.0;
//     float end_rad = end_angle * 3.14159 / 180.0;

//     // 绘制扇形区域（半圆朝上）
//     for (uint16_t r = 0; r <= radius; r++)
//     {
//         for (float angle = start_rad; angle <= end_rad; angle += 0.03)
//         {
//             int16_t px = center_x + r * cos(angle);
//             int16_t py = center_y + r * sin(angle); // 使用负号使半圆朝上

//             if (px >= 0 && px < ST7789_WIDTH && py >= 0 && py < ST7789_HEIGHT)
//             {
//                 ST7789_DrawPoint(px, py, color);
//             }
//         }
//     }
// }

// // --- 新的雷达UI绘制函数 ---
// void DrawRadarUI(uint16_t center_x, uint16_t center_y, uint16_t radius,
//                  float *distances, RadarDrawMode mode)
// {
//     // 定义雷达的静态属性
//     const float sector_angles[5][2] = {
//         {-180.0, -144.0}, {-144.0, -108.0}, {-108.0, -72.0}, {-72.0, -36.0}, {-36.0, 0.0}};

//     /**************************************************************
//      *                      绘制静态背景
//      **************************************************************/
//     if (mode == RADAR_DRAW_BACKGROUND)
//     {
//         // 1. 清除整个区域
//         ST7789_DrawRectangle(center_x - radius - 2, center_y - radius - 2,
//                              center_x + radius + 2, center_y + 2, WHITE, 1);

//         // 2. 绘制所有扇区的绿色背景
//         for (int i = 0; i < 5; i++)
//         {
//             DrawSemiCircleSector(center_x, center_y, radius,
//                                  sector_angles[i][0], sector_angles[i][1], GREEN);
//         }

//         // 3. 绘制扇形分隔线
//         for (int i = 1; i < 5; i++)
//         {
//             float angle_rad = sector_angles[i][0] * 3.14159 / 180.0;
//             int16_t end_x = center_x + radius * cos(angle_rad);
//             int16_t end_y = center_y + radius * sin(angle_rad);
//             ST7789_DrawLine(center_x, center_y, end_x, end_y, BLACK);
//         }
//         return; // 背景绘制完成，直接返回
//     }

//     /**************************************************************
//      *                      动态更新障碍物红点
//      **************************************************************/
//     if (mode == RADAR_UPDATE_OBSTACLES)
//     {
//         // 缓存上一次绘制的红点坐标，以便清除
//         static int16_t last_points[5][2] = {{0}};

//         for (int i = 0; i < 5; i++)
//         {
//             float dist = distances[i];

//             // --- 1. 清除上一次的红点 ---
//             // 如果上一次的点有效 (非0)，则用背景色(绿色)覆盖它
//             if (last_points[i][0] != 0 || last_points[i][1] != 0)
//             {
//                 // 画一个稍大的绿色方块来确保完全覆盖
//                 ST7789_DrawRectangle(last_points[i][0] - 2, last_points[i][1] - 2,
//                                      last_points[i][0] + 2, last_points[i][1] + 2, GREEN, 1);

//                 // 清空缓存
//                 last_points[i][0] = 0;
//                 last_points[i][1] = 0;
//             }

//             // --- 2. 计算新红点的位置 ---
//             // 只有在有效距离范围内 (20cm - 200cm) 才绘制
//             if (dist >= 20.0f && dist <= 200.0f)
//             {
//                 // a. 将距离映射到半径 (20cm->radius, 200cm->0)
//                 // 距离越近，点离中心越远 (在半径上越大)
//                 uint16_t point_radius = radius - (uint16_t)((dist - 20.0f) * radius / (200.0f - 20.0f));

//                 // b. 计算扇区中心角度
//                 float mid_angle_deg = (sector_angles[i][0] + sector_angles[i][1]) / 2.0f;
//                 float angle_rad = mid_angle_deg * 3.14159 / 180.0;

//                 // c. 计算红点的笛卡尔坐标
//                 int16_t px = center_x + point_radius * cos(angle_rad);
//                 int16_t py = center_y + point_radius * sin(angle_rad);

//                 // --- 3. 绘制新的红点 ---
//                 // 画一个3x3的红色方块作为“红点”
//                 ST7789_DrawRectangle(px - 1, py - 1, px + 1, py + 1, RED, 1);

//                 // --- 4. 缓存新点的位置 ---
//                 last_points[i][0] = px;
//                 last_points[i][1] = py;
//             }
//         }
//     }
// }

// // =======绘制底盘电池图标函数=======
// void DrawSimpleBattery(uint16_t x, uint16_t y, uint8_t percentage)
// {

//     // --- 关键优化：仅在百分比变化时重绘 ---
//     static uint8_t last_percentage = 0xFF; // 用无效值初始化
//     if (last_percentage == percentage)
//     {
//         return; // 值未变，无需重绘，直接返回
//     }
//     last_percentage = percentage;
//     // ------------------------------------

//     // 固定尺寸
//     uint16_t width = 50;
//     uint16_t height = 30;

//     // 清除显示区域
//     ST7789_DrawRectangle(x - 5, y - 25, x + width + 45, y + height + 5, WHITE, 1);

//     // 蓄电池主体边框
//     ST7789_DrawRectangle(x, y, x + width, y + height, BLACK, 0);

//     // 正极电极（顶部右侧）
//     uint16_t positive_width = width / 6;
//     uint16_t positive_height = height / 4;
//     uint16_t positive_x = x + width - positive_width - 6;
//     ST7789_DrawRectangle(positive_x, y - positive_height,
//                          positive_x + positive_width, y, BLACK, 1);

//     // 负极电极（顶部左侧）
//     uint16_t negative_width = width / 6;
//     uint16_t negative_height = height / 4;
//     uint16_t negative_x = x + 6;
//     ST7789_DrawRectangle(negative_x, y - negative_height,
//                          negative_x + negative_width, y, BLACK, 1);

//     // 1. 定义一个用于显示的百分比变量，并处理无效值
//     uint8_t display_percentage = percentage;
//     bool is_valid = true;

//     if (percentage > 100)
//     {                           // 所有大于100的值（包括0xFF=255）都视为无效
//         display_percentage = 0; // 当数据无效时，我们让电池显示为空
//         is_valid = false;
//     }

//     // 2. 电量填充
//     // 只有在数据有效时才填充电量
//     if (is_valid)
//     {
//         uint16_t fill_height = (percentage * (height - 4)) / 100;
//         if (fill_height > 0)
//         {
//             uint16_t color;
//             if (percentage > 70)
//                 color = GREEN;
//             else if (percentage > 30)
//                 color = YELLOW;
//             else
//                 color = RED;

//             // 从底部开始向上填充
//             uint16_t fill_y = y + height - 2 - fill_height;
//             ST7789_DrawRectangle(x + 2, fill_y, x + width - 2, y + height - 2, color, 1);
//         }
//     }

//     // 在电极上方显示正负极符号
//     // 正极符号 "+"
//     uint16_t plus_x = positive_x + positive_width / 2;
//     ST7789_ShowString(plus_x - 4, y - positive_height - 12, "+", Font_7x10, BLACK, WHITE);

//     // 负极符号 "-"
//     uint16_t minus_x = negative_x + negative_width / 2;
//     ST7789_ShowString(minus_x - 4, y - negative_height - 12, "-", Font_7x10, BLACK, WHITE);

//     // 3. 显示电量百分比文字
//     char percentage_text[8];
//     if (is_valid)
//     {
//         sprintf(percentage_text, "%d%%", display_percentage);
//     }
//     else
//     {
//         sprintf(percentage_text, "N/A"); // 无效时显示 N/A
//     }
//     ST7789_ShowString(x + width + 5, y + height / 2 - 5, percentage_text, Font_7x10, BLACK, WHITE);
// }
// void DrawEllipticalWheelIcon(uint16_t x, uint16_t y, uint16_t width, uint16_t height, 
//                            uint8_t is_driving, uint16_t normal_color, uint16_t error_color)
// {
//     // 移除静态变量和状态检查，每次都绘制
//     uint16_t wheel_color = is_driving ? normal_color : error_color;
//     uint16_t a = width / 2;
//     uint16_t b = height / 2;
    
//     // 绘制椭圆形外圈（填充）
//     for (int16_t py = -b; py <= b; py++) {
//         for (int16_t px = -a; px <= a; px++) {
//             float ellipse_value = ((float)px * px) / (a * a) + ((float)py * py) / (b * b);
//             if (ellipse_value <= 1.0f) {
//                 ST7789_DrawPoint(x + px, y + py, wheel_color);
//             }
//         }
//     }
// }

