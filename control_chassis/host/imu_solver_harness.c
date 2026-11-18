#include <assert.h>
#include <stdio.h>

/* Exercise the actual optimized numeric implementation, including invSqrt. */
#include "../Core/UserDev/bsp_imu_solver.c"

int main(void)
{
    assert(sizeof(float) == sizeof(uint32_t));
    const float values[] = {1e-12f, 1e-6f, 0.01f, 0.25f, 1.0f, 2.0f, 9.81f, 100.0f, 1e6f, 1e12f};
    for (unsigned i = 0U; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        float actual = invSqrt(values[i]);
        float expected = 1.0f / sqrtf(values[i]);
        assert(isfinite(actual) && actual > 0.0f);
        assert(fabsf(actual / expected - 1.0f) < 0.002f);
    }
    const float tilt = 20.0f * 0.01745329252f;
    for (unsigned tilted = 0U; tilted < 2U; ++tilted)
    {
        float ax = tilted ? sinf(tilt) : 0.0f;
        float az = tilted ? cosf(tilt) : 1.0f;
        IMU_Solver_Init(ax, 0.0f, az);
        IMU_Solver_SetOffset(0.1f, -0.2f, 0.3f);
        for (unsigned i = 0U; i < 1000U; ++i)
            IMU_Solver_Update(0.1f, -0.2f, 0.3f, ax, 0.0f, az, 0.01f);
        assert(isfinite(imu_data.pitch) && isfinite(imu_data.roll) && isfinite(imu_data.yaw));
        assert(fabsf(imu_data.roll) < 0.1f);
        assert(fabsf(imu_data.pitch - (tilted ? -20.0f : 0.0f)) < 0.3f);
        float norm = imu_data.q0 * imu_data.q0 + imu_data.q1 * imu_data.q1 +
                     imu_data.q2 * imu_data.q2 + imu_data.q3 * imu_data.q3;
        assert(fabsf(norm - 1.0f) < 0.004f);
        float previous = imu_data.q0;
        IMU_Solver_Update(10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.01f);
        assert(imu_data.q0 == previous); /* Existing zero-acceleration behavior. */
    }
    puts("PASS optimized IMU numeric regression: inverse sqrt accuracy, level/20deg pose, offset, normalization and zero-input behavior");
    return 0;
}
