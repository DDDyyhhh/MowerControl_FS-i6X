#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lifter.h"
#include "cmsis_os.h"
#include "EG2104.h"
#include "flash_storage.h"
#include "app_context.h"

TIM_HandleTypeDef htim2 = {2}, htim3 = {3};
BagTestContext_t g_app;
static uint32_t now_ms;
static int lock_depth;
static bool initialized, fail_mutex, inject_target;
static float bag_speed;
static unsigned bag_starts;
static int critical_depth;
void LifterTest_EnterCritical(void) { assert(critical_depth == 0); critical_depth = 1; }
void LifterTest_ExitCritical(void) { assert(critical_depth == 1); critical_depth = 0; }
uint32_t HAL_GetTick(void) { return now_ms; }
void HAL_Delay(uint32_t ms) {
    now_ms += ms;
    if (inject_target) { inject_target = false; Lifter_SetGrassBagTarget(1); }
}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel) {
    (void)timer; (void)channel; return HAL_OK;
}
osMutexId_t osMutexNew(const osMutexAttr_t *attr) {
    assert(attr->attr_bits == osMutexPrioInherit);
    return fail_mutex ? NULL : (void *)&lock_depth;
}
osStatus_t osMutexAcquire(osMutexId_t mutex, uint32_t timeout) {
    assert(mutex && timeout == osWaitForever && lock_depth == 0);
    lock_depth = 1; return osOK;
}
osStatus_t osMutexRelease(osMutexId_t mutex) {
    assert(mutex && lock_depth == 1); lock_depth = 0; return osOK;
}
HAL_StatusTypeDef EG2104_HBridge_SetSpeed(EG2104_HBridge_t *bridge, float speed) {
    if (bridge->left_timer == &htim2) {
        assert(lock_depth == 1 && bag_speed == 0.0f);
        bag_speed = speed; ++bag_starts;
    }
    return HAL_OK;
}
void EG2104_HBridge_Stop(EG2104_HBridge_t *bridge) {
    if (bridge->left_timer == &htim2) {
        assert(!initialized || lock_depth == 1);
        bag_speed = 0.0f;
    }
}
bool FlashStorage_LoadSettings(AppSettings_t *s) {
    s->lifter_current_height_cm = 11.0f; return true;
}
bool FlashStorage_SaveSettings(const AppSettings_t *s) { (void)s; return true; }
void Suspend_Heartbeat_Check(unsigned char suspend) { (void)suspend; }

int main(void)
{
    Lifter_Init(); initialized = true;
    Lifter_SetGrassBagTarget(1); assert(bag_speed == 0.0f); /* default fail closed */
    Lifter_SetGrassBagPermission(true);
    now_ms = 100;
    Lifter_SetGrassBagTarget(1); assert(bag_speed == 60.0f && bag_starts == 1);
    now_ms = 200;
    Lifter_SetGrassBagTarget(1); assert(bag_starts == 1);
    now_ms = 20099;
    Lifter_Task_Update(); assert(!g_app.grass_bag_status && bag_speed == 60.0f);
    now_ms = 20100;
    Lifter_Task_Update(); assert(g_app.grass_bag_status && bag_speed == 0.0f);
    Lifter_SetGrassBagTarget(0); assert(bag_speed == -60.0f);
    Lifter_SetGrassBagPermission(false); assert(bag_speed == 0.0f);
    now_ms = 60000;
    Lifter_Task_Update(); assert(g_app.grass_bag_status); /* no false completion */
    Lifter_SetGrassBagTarget(0); assert(bag_speed == 0.0f);
    Lifter_SetGrassBagPermission(true);
    Lifter_SetGrassBagTarget(0); assert(bag_speed == -60.0f);
    now_ms = 80000;
    Lifter_Task_Update(); assert(!g_app.grass_bag_status && bag_speed == 0.0f);
    Lifter_SetGrassBagTarget(1); assert(bag_speed == 60.0f);
    Lifter_SetGrassBagTarget(0); assert(bag_speed == -60.0f);
    Lifter_StopGrassBag(); assert(bag_speed == 0.0f);
    now_ms = 110000;
    Lifter_Task_Update(); assert(!g_app.grass_bag_status);
    /* Command arrives during delayed height persistence before bag update. */
    Lifter_SetTargetLevel(4);
    Lifter_Task_Update();
    now_ms += 2800;
    Lifter_Task_Update();
    now_ms += 10000;
    inject_target = true;
    Lifter_Task_Update();
    assert(bag_speed == 60.0f && !g_app.grass_bag_status);
    assert(!inject_target && lock_depth == 0);
    Lifter_StopGrassBag();
    initialized = false; fail_mutex = true;
    Lifter_Init(); initialized = true;
    Lifter_SetGrassBagPermission(true);
    Lifter_SetGrassBagTarget(1); assert(bag_speed == 0.0f);
    puts("[PASS] Production Lifter mutex/permission/PWM stop/completion/time race/fail closed");
    return 0;
}
