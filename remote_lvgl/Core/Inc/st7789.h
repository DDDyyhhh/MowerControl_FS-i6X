#ifndef ST7789_H_
#define ST7789_H_

#include "spi.h"
#include "fonts.h"
#include "main.h"
#define ST7789_SPI_PORT hspi1
// Pin
#define ST7789_RST_PORT GPIOE
#define ST7789_RST_PIN  GPIO_PIN_7
#define ST7789_DC_PORT  GPIOD
#define ST7789_DC_PIN   GPIO_PIN_5
#define ST7789_CS_PORT  GPIOA
#define ST7789_CS_PIN  GPIO_PIN_4

#define ST7789_BLK_PORT  GPIOE
#define ST7789_BLK_PIN   GPIO_PIN_5

#define USE_HORIZONTAL 0  //úʾ 01Ϊ 23Ϊ

#define WHITE       0xFFFF
#define BLACK       0x0000
#define BLUE        0x001F
#define RED         0xF800
#define MAGENTA     0xF81F
#define GREEN       0x07E0
#define CYAN        0x7FFF
#define YELLOW      0xFFE0
#define GRAY        0X8430
#define BRED        0XF81F
#define GRED        0XFFE0
#define GBLUE       0X07FF
#define BROWN       0XBC40
#define BRRED       0XFC07
#define DARKBLUE    0X01CF
#define LIGHTBLUE   0X7D7C
#define GRAYBLUE    0X5458

#define LIGHTGREEN  0X841F
#define LGRAY       0XC618
#define LGRAYBLUE   0XA651
#define LBBLUE      0X2B12
#if USE_HORIZONTAL==0||USE_HORIZONTAL==1
#define ST7789_WIDTH 240
#define ST7789_HEIGHT 320

#else
#define ST7789_WIDTH 320
#define ST7789_HEIGHT 240
#endif
#define ST7789_RST_Clr() HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_RESET)
#define ST7789_RST_Set() HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_SET)

#define ST7789_DC_Clr() HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_RESET)
#define ST7789_DC_Set() HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET)

#ifndef CFG_NO_CS
#define ST7789_Select() HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET)
#define ST7789_UnSelect() HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_SET)

#define ABS(x) ((x) > 0 ? (x) : -(x))

void ST7789_Init();
void DrawSignalStairs(uint16_t x, uint16_t y, uint8_t level, uint16_t color_on, uint16_t color_off);
void ST7789_DrawImage_Scale(uint16_t x, uint16_t y, uint16_t src_w, uint16_t src_h, 
                            uint16_t dst_w, uint16_t dst_h, const uint16_t *src);
void ST7789_Clear(uint16_t color);
void ST7789_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
void ST7789_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,uint16_t color);
void ST7789_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color,uint8_t isFilled);
void ST7789_DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data);
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7789_ShowChar(uint16_t x, uint16_t y, uint16_t color, uint16_t bgcolor,char ch, FontDef font);
void ST7789_ShowString(uint16_t x, uint16_t y, const char *str, FontDef font, uint16_t color, uint16_t bgcolor);
// void ST7789_ShowChineses(int16_t X, int16_t Y, uint16_t color,uint16_t bgcolor,char *Chinese);
void ST7789_ShowChineses(int16_t X, int16_t Y, uint16_t color, uint16_t bgcolor, char *Chinese, uint8_t scale);
void ST7789_ShowChinese(uint16_t x, uint16_t y, uint16_t color, uint16_t bgcolor, const uint8_t *Chinese, uint8_t scale);
// void ST7789_ShowChinese(uint16_t x, uint16_t y, uint16_t color,uint16_t bgcolor,const uint8_t *Chinese);
void ST7789_ShowBinNum(int16_t X, int16_t Y,uint16_t color,uint16_t bgcolor, uint32_t Number, uint8_t Length, FontDef font);
void ST7789_Printf(int16_t X, int16_t Y, uint16_t color,uint16_t bgcolor,FontDef font, char *format, ...);
void ST7789_SetBrightness(uint8_t brightness); // 0-100%
void DrawVerticalSlider(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t value, uint16_t max_value, uint16_t color,uint16_t bg_color);
void DrawStatusSlider(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t is_on, uint16_t on_color, uint16_t off_color);
void DrawLockStatus(uint16_t x, uint16_t y, uint16_t size, uint8_t is_unlocked);
void DrawLockIcon(uint16_t x, uint16_t y, uint16_t size, uint8_t is_unlocked, uint16_t unlocked_color, uint16_t locked_color);
void DrawSimpleGearArc(uint16_t x, uint16_t y, uint16_t radius, uint8_t gear);
void draw_battery_icon(uint16_t x, uint16_t y, uint8_t percentage);
uint16_t filter_adc_value(uint16_t new_value);
void DrawSemiCircleSector(uint16_t center_x, uint16_t center_y, uint16_t radius,
                         float start_angle, float end_angle, uint16_t color);
void DrawSemiCircleObstacleZones(uint16_t center_x, uint16_t center_y, uint16_t radius,
                                uint8_t left_75_obstacle, uint8_t left_25_obstacle, 
                                uint8_t front_obstacle, uint8_t right_25_obstacle, 
                                uint8_t right_75_obstacle);
void DrawSimpleBattery(uint16_t x, uint16_t y, uint8_t percentage);
void DrawSimpleBattery(uint16_t x, uint16_t y, uint8_t percentage); // <<< 新增声明
void DrawEllipticalWheelIcon(uint16_t x, uint16_t y, uint16_t width, uint16_t height, 
                           uint8_t is_driving, uint16_t normal_color, uint16_t error_color);
void ST7789_SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);                           

// 定义一个枚举来表示绘制模式
typedef enum {
    RADAR_DRAW_BACKGROUND, // 仅绘制背景
    RADAR_UPDATE_OBSTACLES // 仅更新障碍物红点
} RadarDrawMode;

// 修改函数原型，去掉原来的 a,b,c,d,e 参数，改为接收一个距离数组
void DrawRadarUI(uint16_t center_x, uint16_t center_y, uint16_t radius,
                 float* distances, RadarDrawMode mode);
#endif

#endif /* ST7789_H_ */
