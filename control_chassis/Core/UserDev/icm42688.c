/** ICM42688 SPI driver: one bounded transaction, commit outputs only on success.
 * HXY register layout and g units differ from the TDK device (mg units).
 * Keep the existing board-specific configuration and CS wake-up sequence.
 */
#include "main.h"
#include "icm42688.h"
#include "spi.h"
#include <math.h>
#include <string.h>

#define SPI_IMU_CS0 HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET)
#define SPI_IMU_CS1 HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET)
#define ICM_TRY(call) do { int8_t result = (call); if (result != 0) return result; } while (0)

static float accSensitivity;
static float gyroSensitivity;

static int8_t icm_transfer(const uint8_t *tx, uint8_t *rx, uint16_t size)
{
    HAL_StatusTypeDef status;
    SPI_IMU_CS0;
#if ICM42688_TDK_DRIVER
    for (unsigned i = 0; i < 50U; ++i) __NOP();
#endif
    status = HAL_SPI_TransmitReceive(&hspi1, tx, rx, size, ICM_SPI_TIMEOUT_MS);
    SPI_IMU_CS1;
    return (int8_t)status;
}

static int8_t icm_read_regs(uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint8_t tx[13] = {0};
    uint8_t rx[13] = {0};
    if (buf == NULL || len == 0U || len > 12U) return ICM_ERROR_ARGUMENT;
    tx[0] = reg | 0x80U;
    ICM_TRY(icm_transfer(tx, rx, (uint16_t)(len + 1U)));
    memcpy(buf, &rx[1], len);
    return 0;
}

static int8_t icm_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {(uint8_t)(reg & 0x7FU), value};
    uint8_t rx[2] = {0};
    return icm_transfer(tx, rx, 2U);
}

#if ICM42688_TDK_DRIVER
#define ACC_START ICM42688_ACCEL_DATA_X1
#define GYRO_START ICM42688_GYRO_DATA_X1
#define TEMP_START ICM42688_TEMP_DATA1

static int8_t icm_configure(void)
{
    uint8_t id = 0, value = 0;
    /* Preserve the extra TDK wake-up before bank select / soft reset. */
    SPI_IMU_CS1; HAL_Delay(10);
    SPI_IMU_CS0; HAL_Delay(1);
    SPI_IMU_CS1; HAL_Delay(10);
    ICM_TRY(icm_write_reg(ICM42688_REG_BANK_SEL, 0x00));
    ICM_TRY(icm_write_reg(ICM42688_DEVICE_CONFIG, 0x01));
    HAL_Delay(100);
    for (unsigned i = 0; i < 20U; ++i)
    {
        ICM_TRY(icm_write_reg(ICM42688_REG_BANK_SEL, 0x00));
        ICM_TRY(icm_read_regs(ICM42688_WHO_AM_I, &id, 1));
        if (id == ICM42688_ID) break;
        HAL_Delay(100);
    }
    if (id != ICM42688_ID) return ICM_ERROR_DATA;
    ICM_TRY(icm_write_reg(ICM42688_REG_BANK_SEL, 0x00));
    ICM_TRY(icm_write_reg(ICM42688_ACCEL_CONFIG0, (AFS_4G << 5) | AODR_100Hz));
    ICM_TRY(icm_write_reg(ICM42688_REG_BANK_SEL, 0x00));
    ICM_TRY(icm_write_reg(ICM42688_GYRO_CONFIG0, (GFS_1000DPS << 5) | GODR_100Hz));
    ICM_TRY(icm_write_reg(ICM42688_REG_BANK_SEL, 0x00));
    ICM_TRY(icm_read_regs(ICM42688_PWR_MGMT0, &value, 1));
    value = (uint8_t)((value & ~(1U << 5)) | (3U << 2) | 3U);
    ICM_TRY(icm_write_reg(ICM42688_PWR_MGMT0, value));
    HAL_Delay(1);
    accSensitivity = 4000.0f / 32768.0f; /* Existing TDK public unit: mg. */
    gyroSensitivity = 1000.0f / 32768.0f;
    return 0;
}
#else
#define ACC_START ICM_REG_ACC_X_H
#define GYRO_START ICM_REG_GYR_X_H
#define TEMP_START ICM_REG_TEMP_H

static int8_t icm_configure(void)
{
    uint8_t id = 0;
    ICM_TRY(icm_write_reg(ICM_REG_SOFT_RST, 0xA5));
    HAL_Delay(50);
    for (unsigned i = 0; i < 10U; ++i)
    {
        ICM_TRY(icm_read_regs(ICM_REG_WHO_AM_I, &id, 1));
        if (id == ICM_WHO_AM_I_VAL) break;
        HAL_Delay(10);
    }
    if (id != ICM_WHO_AM_I_VAL) return ICM_ERROR_DATA;
    ICM_TRY(icm_write_reg(ICM_REG_ACC_CONF, 0x84));
    ICM_TRY(icm_write_reg(ICM_REG_ACC_RANGE, ICM_ACC_RANGE_4G));
    ICM_TRY(icm_write_reg(ICM_REG_GYR_CONF, 0x84));
    ICM_TRY(icm_write_reg(ICM_REG_GYR_RANGE, ICM_GYR_RANGE_2000DPS));
    ICM_TRY(icm_write_reg(ICM_REG_PWR_CTRL, 0x0E));
    HAL_Delay(20);
    accSensitivity = 4000.0f / 32768.0f; /* Converted to g at public boundary. */
    gyroSensitivity = 2000.0f / 32768.0f;
    return 0;
}
#endif

int8_t bsp_Icm42688Init(void)
{
    uint8_t tx = 0, rx = 0;
    accSensitivity = gyroSensitivity = 0.0f;
    SPI_IMU_CS1; HAL_Delay(10);
    SPI_IMU_CS0; HAL_Delay(1);
    SPI_IMU_CS1; HAL_Delay(10);
    ICM_TRY(icm_transfer(&tx, &rx, 1));
    HAL_Delay(10);
    return icm_configure();
}

static int16_t icm_be16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static icm42688RawData_t icm_decode(const uint8_t *p)
{
    icm42688RawData_t value = {icm_be16(p), icm_be16(p + 2), icm_be16(p + 4)};
    return value;
}

static int icm_all_bytes(const uint8_t *p, unsigned count, uint8_t value)
{
    for (unsigned i = 0; i < count; ++i) if (p[i] != value) return 0;
    return 1;
}

int8_t bsp_IcmGetRawData(icm42688RealData_t *accData, icm42688RealData_t *gyroData)
{
    uint8_t buf[12] = {0};
    icm42688RawData_t a, g;
    icm42688RealData_t acc, gyro;
    if (accData == NULL || gyroData == NULL) return ICM_ERROR_ARGUMENT;
    ICM_TRY(icm_read_regs(ACC_START, buf, 12));
    if (icm_all_bytes(buf, 12, 0xFF) || icm_all_bytes(buf, 6, 0)) return ICM_ERROR_DATA;
    a = icm_decode(buf); g = icm_decode(buf + 6);
#if ICM42688_TDK_DRIVER
    if ((a.x == -1 && a.y == -1 && a.z == -1) || a.x == -32768) return ICM_ERROR_DATA;
#endif
    if (accSensitivity <= 0.0f || gyroSensitivity <= 0.0f) return ICM_ERROR_DATA;
    acc = (icm42688RealData_t){a.x * accSensitivity, a.y * accSensitivity, a.z * accSensitivity};
#if ICM42688_HXY_DRIVER
    acc.x /= 1000.0f; acc.y /= 1000.0f; acc.z /= 1000.0f;
#endif
    gyro = (icm42688RealData_t){g.x * gyroSensitivity, g.y * gyroSensitivity, g.z * gyroSensitivity};
    if (!isfinite(acc.x) || !isfinite(acc.y) || !isfinite(acc.z) ||
        !isfinite(gyro.x) || !isfinite(gyro.y) || !isfinite(gyro.z)) return ICM_ERROR_DATA;
    *accData = acc; *gyroData = gyro;
    return 0;
}

static int8_t icm_read_axes(uint8_t reg, icm42688RawData_t *out)
{
    uint8_t buf[6] = {0};
    if (out == NULL) return ICM_ERROR_ARGUMENT;
    ICM_TRY(icm_read_regs(reg, buf, 6));
    /* Zero gyro is a valid stationary measurement. */
    if (icm_all_bytes(buf, 6, 0xFF) || (reg == ACC_START && icm_all_bytes(buf, 6, 0)))
        return ICM_ERROR_DATA;
    *out = icm_decode(buf);
    return 0;
}
int8_t bsp_IcmGetAccelerometer(icm42688RawData_t *out) { return icm_read_axes(ACC_START, out); }
int8_t bsp_IcmGetGyroscope(icm42688RawData_t *out) { return icm_read_axes(GYRO_START, out); }

int8_t bsp_IcmGetTemperature(int16_t *out)
{
    uint8_t buf[2] = {0};
    if (out == NULL) return ICM_ERROR_ARGUMENT;
    ICM_TRY(icm_read_regs(TEMP_START, buf, 2));
    /* All-zero temperature is valid (23C HXY / 25C TDK). */
#if ICM42688_TDK_DRIVER
    *out = (int16_t)(icm_be16(buf) / 132.48 + 25);
#else
    *out = (int16_t)((float)icm_be16(buf) / 512.0f + 23.0f);
#endif
    return 0;
}
