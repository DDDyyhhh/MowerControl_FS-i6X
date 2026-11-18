#include "app_imu.h"
#include "bsp_imu_solver.h"
#include "cmsis_os.h"
#include <math.h>
#include <string.h>

static void Increment(uint32_t *count)
{
    if (*count != UINT32_MAX) ++*count;
}

static bool SampleValid(const icm42688RealData_t *a, const icm42688RealData_t *g)
{
    float norm2 = a->x * a->x + a->y * a->y + a->z * a->z;
    return isfinite(a->x) && isfinite(a->y) && isfinite(a->z) &&
           isfinite(g->x) && isfinite(g->y) && isfinite(g->z) &&
           isfinite(norm2) && norm2 > 0.0f;
}

static int8_t ReadSample(AppImu_t *imu, icm42688RealData_t *a, icm42688RealData_t *g)
{
    uint32_t start = HAL_GetTick();
    int8_t status = bsp_IcmGetRawData(a, g);
    uint32_t elapsed = HAL_GetTick() - start;
    if (elapsed > imu->sample_max_ms) imu->sample_max_ms = elapsed;
    if (status == 0 && !SampleValid(a, g)) status = ICM_ERROR_DATA;
    imu->last_error = status;
    if (status != 0)
    {
        Increment(&imu->failed_reads);
        Increment(&imu->consecutive_failures);
    }
    else imu->consecutive_failures = 0;
    return status;
}

void AppImu_Init(AppImu_t *imu, icm42688RealData_t *acc, icm42688RealData_t *gyro)
{
    icm42688RealData_t a = {0}, g = {0};
    float sx = 0, sy = 0, sz = 0;
    memset(imu, 0, sizeof(*imu));
    imu->last_error = bsp_Icm42688Init();
    if (imu->last_error != 0)
    {
        imu->state = APP_IMU_INIT_FAILED;
        return;
    }
    imu->init_ok = true;
    /* Retain the existing post-configuration settling interval; startup only. */
    osDelay(100U);
    uint32_t start = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - start) < APP_IMU_CALIBRATION_MS)
    {
        int8_t status = ReadSample(imu, &a, &g);
        uint32_t now = HAL_GetTick();
        if ((uint32_t)(now - start) >= APP_IMU_CALIBRATION_MS) break;
        if (status == 0)
        {
            sx += g.x; sy += g.y; sz += g.z;
            imu->last_valid_ms = now;
            if (++imu->calibration_samples == APP_IMU_CALIBRATION_SAMPLES)
            {
                IMU_Solver_SetOffset(sx / APP_IMU_CALIBRATION_SAMPLES,
                                     sy / APP_IMU_CALIBRATION_SAMPLES,
                                     sz / APP_IMU_CALIBRATION_SAMPLES);
                IMU_Solver_Init(a.x, a.y, a.z);
                *acc = a; *gyro = g;
                imu->calibrated = imu->has_sample = imu->pose_available = true;
                imu->state = APP_IMU_READY;
                return;
            }
        }
        uint32_t elapsed = HAL_GetTick() - start;
        if (elapsed >= APP_IMU_CALIBRATION_MS) break;
        uint32_t remaining = APP_IMU_CALIBRATION_MS - elapsed;
        osDelay(remaining < 5U ? remaining : 5U);
    }
    imu->state = APP_IMU_CALIBRATION_FAILED;
    imu->last_error = APP_IMU_ERROR_CALIBRATION;
}

bool AppImu_Update(AppImu_t *imu, icm42688RealData_t *acc, icm42688RealData_t *gyro)
{
    icm42688RealData_t a = {0}, g = {0};
    if (imu->state != APP_IMU_READY) return false;
    int8_t status = ReadSample(imu, &a, &g);
    uint32_t now = HAL_GetTick();
    uint32_t gap = now - imu->last_valid_ms;
    imu->pose_available = imu->has_sample && gap < APP_IMU_FRESHNESS_MS;
    if (status != 0) return false;
    if (!imu->pose_available)
        IMU_Solver_Init(a.x, a.y, a.z);
    else
        IMU_Solver_Update(g.x, g.y, g.z, a.x, a.y, a.z,
                          gap == 0U ? 0.001f : (float)gap / 1000.0f);
    *acc = a; *gyro = g;
    imu->last_valid_ms = now;
    imu->has_sample = imu->pose_available = true;
    return true;
}

void AppImu_RecordCycle(AppImu_t *imu, uint32_t start_ms, uint32_t end_ms)
{
    uint32_t elapsed = end_ms - start_ms;
    if (elapsed > imu->motion_max_ms) imu->motion_max_ms = elapsed;
    if (elapsed >= 10U) Increment(&imu->motion_overruns);
}
