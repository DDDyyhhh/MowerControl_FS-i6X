#ifndef APP_IMU_H
#define APP_IMU_H
#include <stdbool.h>
#include <stdint.h>
#include "icm42688.h"

#define APP_IMU_FRESHNESS_MS 100U
#define APP_IMU_CALIBRATION_MS 2000U
#define APP_IMU_CALIBRATION_SAMPLES 200U
#define APP_IMU_ERROR_CALIBRATION (-10)

typedef enum {
    APP_IMU_NOT_STARTED = 0, APP_IMU_INIT_FAILED,
    APP_IMU_CALIBRATION_FAILED, APP_IMU_READY
} AppImuState_t;

/* Owned exclusively by Motion in g_app. Diagnostic only: no output authority. */
typedef struct {
    AppImuState_t state;
    bool init_ok, calibrated, has_sample, pose_available;
    int8_t last_error;
    uint16_t calibration_samples;
    uint32_t failed_reads, consecutive_failures, last_valid_ms;
    uint32_t sample_max_ms, motion_max_ms, motion_overruns;
} AppImu_t;

/* Startup only: bounded calibration; failure is retried only after reboot. */
void AppImu_Init(AppImu_t *imu, icm42688RealData_t *acc, icm42688RealData_t *gyro);
/* At most one SPI transaction. False leaves pose and caller samples unchanged. */
bool AppImu_Update(AppImu_t *imu, icm42688RealData_t *acc, icm42688RealData_t *gyro);
void AppImu_RecordCycle(AppImu_t *imu, uint32_t start_ms, uint32_t end_ms);
#endif
