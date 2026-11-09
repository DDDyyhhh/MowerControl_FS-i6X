#include "ft6336u.h"
#include "cmsis_os.h"

extern I2C_HandleTypeDef hi2c1;

// ==========================================
// 架构师专属：全局调试变量
// ==========================================
uint32_t g_dbg_i2c_err_cnt = 0; // I2C 通信失败次数
uint8_t  g_dbg_touch_pts = 0;   // 读到的触摸点数
uint16_t g_dbg_raw_x = 0;       // 原始 X 坐标
uint16_t g_dbg_raw_y = 0;       // 原始 Y 坐标
// ==========================================

static void FT6336U_WriteReg(uint8_t reg, uint8_t data) {
    HAL_I2C_Mem_Write(&hi2c1, FT6336U_ADDR_WRITE, reg, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}

// 【已修改】带返回值检测的读函数
static bool FT6336U_ReadRegs_Safe(uint8_t reg, uint8_t *data, uint8_t len) {
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&hi2c1, FT6336U_ADDR_READ, reg, I2C_MEMADD_SIZE_8BIT, data, len, 10);
    if(status != HAL_OK) {
        g_dbg_i2c_err_cnt++; // 如果读取失败，错误计数器加 1
        return false;
    }
    return true;
}

void FT6336U_Init(void) {
    // 1. 硬件复位触摸芯片 (PE6)
    HAL_GPIO_WritePin(FT6336U_CS_PORT, FT6336U_CS_PIN, GPIO_PIN_RESET);
    osDelay(20);
    HAL_GPIO_WritePin(FT6336U_CS_PORT, FT6336U_CS_PIN, GPIO_PIN_SET);
    osDelay(50); 

    FT6336U_WriteReg(0x00, 0x00);
}

bool FT6336U_ReadTouch(FT6336U_TouchPoint_t *touch) {
    uint8_t data[4];
    uint8_t touch_points = 0;

    // 1. 安全读取 0x02 寄存器
    if (FT6336U_ReadRegs_Safe(0x02, &touch_points, 1) == false) {
        touch->is_pressed = false;
        return false; // I2C 炸了，直接返回
    }

    touch_points &= 0x0F;
    g_dbg_touch_pts = touch_points; // 记录点数到全局变量

    if (touch_points == 0) {
        touch->is_pressed = false;
        return false;
    }

    // 2. 安全读取坐标
    if (FT6336U_ReadRegs_Safe(0x03, data, 4) == true) {
        touch->x = ((data[0] & 0x0F) << 8) | data[1];
        touch->y = ((data[2] & 0x0F) << 8) | data[3];

        // 记录原始坐标到全局变量 (反转前的真实硬件数据)
        g_dbg_raw_x = touch->x;
        g_dbg_raw_y = touch->y;

        // 3. 屏幕坐标反转映射
        touch->x = 239 - touch->x; 
        touch->y = 319 - touch->y; 
        touch->is_pressed = true;
        return true;
    }

    return false;
}