/* Soft start of the production lifter.c ramp, with only HAL/RTOS/bridge fakes. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "lifter.h"
#include "cmsis_os.h"
#include "EG2104.h"
#include "flash_storage.h"
#include "app_context.h"

TIM_HandleTypeDef htim2 = {2}, htim3 = {3};
BagTestContext_t g_app;
static uint32_t now_ms;
static int lock_depth;
static float bag_speed, lift_speed, peak;
void LifterTest_EnterCritical(void) {}
void LifterTest_ExitCritical(void) {}
uint32_t HAL_GetTick(void) { return now_ms; }
void HAL_Delay(uint32_t ms) { now_ms += ms; }
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel) {
    (void)timer; (void)channel; return HAL_OK;
}
osMutexId_t osMutexNew(const osMutexAttr_t *attr) { (void)attr; return (void *)&lock_depth; }
osStatus_t osMutexAcquire(osMutexId_t m, uint32_t t) { (void)m; (void)t; lock_depth = 1; return osOK; }
osStatus_t osMutexRelease(osMutexId_t m) { (void)m; lock_depth = 0; return osOK; }
HAL_StatusTypeDef EG2104_HBridge_SetSpeed(EG2104_HBridge_t *bridge, float speed) {
    if (fabsf(speed) > peak) peak = fabsf(speed);
    if (bridge->left_timer == &htim2) { assert(lock_depth == 1); bag_speed = speed; }
    else lift_speed = speed;
    return HAL_OK;
}
void EG2104_HBridge_Stop(EG2104_HBridge_t *bridge) {
    if (bridge->left_timer == &htim2) bag_speed = 0.0f; else lift_speed = 0.0f;
}
bool FlashStorage_LoadSettings(AppSettings_t *s) { s->lifter_current_height_cm = 11.0f; return true; }
bool FlashStorage_SaveSettings(const AppSettings_t *s) { (void)s; return true; }
void Suspend_Heartbeat_Check(unsigned char suspend) { (void)suspend; }

static void Tick(uint32_t t) { now_ms = t; Lifter_Task_Update(); }
static void Near(float a, float b) { assert(fabsf(a - b) < 0.01f); }

int main(void)
{
    Lifter_Init();
    Lifter_SetGrassBagPermission(true);

    /* Opening starts low, ramps linearly, reaches and never exceeds the target. */
    now_ms = 1000; peak = 0.0f;
    Lifter_SetGrassBagTarget(1); Near(bag_speed, 10.0f);
    Tick(1200); Near(bag_speed, 30.0f);
    Tick(1399); assert(bag_speed < 50.0f && bag_speed > 30.0f);
    Tick(1400); Near(bag_speed, 50.0f);
    Tick(1450); Near(bag_speed, 50.0f); Near(peak, 50.0f);

    /* A reversal restarts from the low duty with the new sign. */
    Lifter_SetGrassBagTarget(0); Near(bag_speed, -10.0f);
    Tick(1650); Near(bag_speed, -30.0f);

    /* Every stop path during the ramp is immediate and cannot be revived. */
    Lifter_StopGrassBag(); assert(bag_speed == 0.0f);
    Tick(1700); assert(bag_speed == 0.0f);
    now_ms = 2000; Lifter_SetGrassBagTarget(1); Near(bag_speed, 10.0f);
    Lifter_SetGrassBagPermission(false); assert(bag_speed == 0.0f);
    Tick(2100); assert(bag_speed == 0.0f);
    Lifter_SetGrassBagPermission(true);

    /* The ramp must not extend the fixed run time. */
    now_ms = 3000; Lifter_SetGrassBagTarget(1);
    Tick(3100); Tick(22999); Near(bag_speed, 50.0f);
    Tick(23000); assert(bag_speed == 0.0f && g_app.grass_bag_status);

    /* Height: ramp output, and position integrates the reduced travel. */
    uint32_t t = 30000U;
    Tick(t);
    Lifter_SetTargetLevel(4);
    t += 20U; Tick(t);
    uint32_t start = t;
    Near(lift_speed, 10.0f);
    t += 200U; Tick(t); Near(lift_speed, 30.0f);
    t += 200U; Tick(t); Near(lift_speed, 50.0f);
    /* Ramp delivers 240ms-equivalent travel in 400ms: 160ms behind a hard start. */
    uint32_t hard_stop_ms = 2800U - 100U;
    while (t < start + hard_stop_ms + 150U) { t += 10U; Tick(t); Near(lift_speed, 50.0f); }
    while (t < start + hard_stop_ms + 180U) { t += 10U; Tick(t); }
    assert(lift_speed == 0.0f);

    puts("[PASS] Actuator soft start (ramp, reversal, immediate stops, run time, height compensation)");
    return 0;
}
