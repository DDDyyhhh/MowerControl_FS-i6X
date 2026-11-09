#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "lifter.h"
#include "cmsis_os.h"
#include "EG2104.h"
#include "flash_storage.h"
#include "app_context.h"
#include "../../experiments/ticket03/height_feedback.h"

TIM_HandleTypeDef htim2 = {2}, htim3 = {3};
BagTestContext_t g_app;
static uint32_t now_ms;
static int critical_depth, lock_depth;
static uint8_t inject_level_on_exit;
static bool load_ok;
static float saved_height, lift_speed, bag_speed;
static unsigned lift_starts, flash_saves;
static float flash_height;

void LifterTest_EnterCritical(void)
{
    assert(critical_depth == 0);
    critical_depth = 1;
}

void LifterTest_ExitCritical(void)
{
    assert(critical_depth == 1);
    critical_depth = 0;
    if (inject_level_on_exit) {
        uint8_t level = inject_level_on_exit;
        inject_level_on_exit = 0;
        Lifter_SetTargetLevel(level);
    }
}

uint32_t HAL_GetTick(void) { assert(!critical_depth); return now_ms; }
void HAL_Delay(uint32_t ms) { assert(!critical_depth); now_ms += ms; }
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)timer; (void)channel; assert(!critical_depth); return HAL_OK;
}
osMutexId_t osMutexNew(const osMutexAttr_t *attr)
{
    assert(attr->attr_bits == osMutexPrioInherit);
    return &lock_depth;
}
osStatus_t osMutexAcquire(osMutexId_t mutex, uint32_t timeout)
{
    assert(mutex && timeout == osWaitForever && !lock_depth && !critical_depth);
    lock_depth = 1; return osOK;
}
osStatus_t osMutexRelease(osMutexId_t mutex)
{
    assert(mutex && lock_depth == 1); lock_depth = 0; return osOK;
}
HAL_StatusTypeDef EG2104_HBridge_SetSpeed(EG2104_HBridge_t *bridge, float speed)
{
    assert(!critical_depth);
    if (bridge->left_timer == &htim3) {
        /* Every start/reversal must first clear both bridge legs. */
        assert(lift_speed == 0.0f);
        assert(speed == -60.0f || speed == 60.0f);
        lift_speed = speed;
        ++lift_starts;
    } else {
        assert(bridge->left_timer == &htim2 && bag_speed == 0.0f);
        assert(speed == 60.0f || speed == -60.0f);
        bag_speed = speed;
    }
    return HAL_OK;
}
void EG2104_HBridge_Stop(EG2104_HBridge_t *bridge)
{
    assert(!critical_depth);
    if (bridge->left_timer == &htim3) lift_speed = 0.0f;
    else { assert(bridge->left_timer == &htim2); bag_speed = 0.0f; }
}
bool FlashStorage_LoadSettings(AppSettings_t *settings)
{
    assert(!critical_depth);
    settings->lifter_current_height_cm = saved_height;
    return load_ok;
}
bool FlashStorage_SaveSettings(const AppSettings_t *settings)
{
    assert(!critical_depth && lift_speed == 0.0f);
    ++flash_saves;
    flash_height = settings->lifter_current_height_cm;
    return true;
}
void Suspend_Heartbeat_Check(unsigned char suspend) { (void)suspend; assert(!critical_depth); }

static void Reset(float height, bool valid, uint32_t tick)
{
    assert(!critical_depth && !lock_depth);
    inject_level_on_exit = 0;
    memset(&g_app, 0, sizeof(g_app));
    saved_height = height; load_ok = valid; now_ms = tick;
    Lifter_Init();
    assert(Lifter_GetHeightFeedback() == 0U);
    lift_starts = flash_saves = 0;
    assert(lift_speed == 0.0f && bag_speed == 0.0f);
}

static void Tick(uint32_t tick)
{
    now_ms = tick;
    Lifter_Task_Update();
    uint16_t word = Lifter_GetHeightFeedback();
    assert(mower_height_word_valid(word));
    assert((word & MOWER_HEIGHT_VALUE_MASK) == (uint16_t)(Lifter_GetCurrentHeight_cm()*10.0f+0.5f));
    assert((word >> 8) == ((tick/MOWER_HEIGHT_HEARTBEAT_MS) & MOWER_HEIGHT_HEARTBEAT_MASK));
    assert(!critical_depth && !lock_depth);
}

static void TestImmediateHomingInterrupt(void)
{
    Reset(7.0f, true, 0);
    Lifter_SetTargetLevel(5); Tick(0);
    assert(lift_speed == -60.0f);
    Lifter_SetTargetLevel(3); Tick(1000);
    /* After 1s upward, the estimate is above 7cm: change to downward now. */
    assert(Lifter_GetState() == LIFTER_STATE_AUTO_ADJUST);
    assert(lift_speed == 60.0f);
    assert(g_app.g_system_report_state == REPORT_NONE);
    assert(Lifter_GetCurrentHeight_cm() > 7.0f && Lifter_GetCurrentHeight_cm() < 11.0f);
    puts("[PASS] New height target interrupts homing on the next lifter update");
}

static void TestFullHomingFromSavedHeights(void)
{
    const float heights[] = {11.0f, 7.0f, 2.0f};
    for (unsigned i = 0; i < sizeof(heights)/sizeof(heights[0]); ++i) {
        Reset(heights[i], true, 100U);
        Lifter_SetTargetLevel(5);
        assert(lift_speed == 0.0f && Lifter_GetState() == LIFTER_STATE_IDLE);
        Tick(1200U); /* Count from actual PWM start, not request or boot. */
        assert(lift_speed == -60.0f && lift_starts == 1);
        assert((Lifter_GetHeightFeedback() & MOWER_HEIGHT_VALUE_MASK) == (uint16_t)(heights[i]*10.0f));
        assert(g_app.g_system_report_state == REPORT_BLADE_INITIALIZING);
        for (uint32_t t = 1250U; t < 11200U; t += 50U) {
            Lifter_SetTargetLevel(5);
            Tick(t);
            assert(lift_speed == -60.0f && lift_starts == 1 && !flash_saves);
        }
        Tick(11199U);
        assert(lift_speed == -60.0f);
        Tick(11200U);
        assert(lift_speed == 0.0f && Lifter_GetState() == LIFTER_STATE_HOMING_DONE);
        assert(Lifter_GetCurrentHeight_cm() == 11.0f);
        assert(g_app.g_system_report_state == REPORT_NONE);
        Lifter_SetTargetLevel(5);
        Tick(11220U);
        assert(lift_starts == 1 && !flash_saves);
        Tick(21199U); assert(!flash_saves);
        Tick(21200U); assert(flash_saves == 1 && flash_height == 11.0f);
        Tick(22000U); assert(flash_saves == 1 && lift_starts == 1);
    }
    puts("[PASS] First/repeated level 5, saved-height drift, exact 10s and lazy save");
}

static void TestLatestTargetWinsDuringHoming(void)
{
    Reset(7.0f, true, 0);
    Lifter_SetTargetLevel(5);
    Lifter_SetTargetLevel(1);
    Lifter_SetTargetLevel(2); /* Latest target cancels the unconsumed home edge. */
    Tick(0);
    assert(lift_speed == 60.0f && Lifter_GetState() == LIFTER_STATE_AUTO_ADJUST);
    assert(lift_starts == 1);
    Tick(2000); assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 5.0f);
    Lifter_SetTargetLevel(5); Tick(2020);
    assert(lift_speed == -60.0f);
    Lifter_SetTargetLevel(4); Lifter_SetTargetLevel(3); Lifter_SetTargetLevel(1);
    Tick(3020); /* pos 6500 - 1000*0.8 = 5700: descend to level 1 now. */
    assert(lift_speed == 60.0f && Lifter_GetState() == LIFTER_STATE_AUTO_ADJUST);
    Tick(6820); assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 2.0f);
    Tick(12020); /* Original homing deadline must not force a false top reset. */
    assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 2.0f && !flash_saves);
    Tick(16819); assert(!flash_saves);
    Tick(16820); assert(flash_saves == 1 && flash_height == 2.0f);
    puts("[PASS] Latest target wins before/during homing; no delayed reset; lazy save");
}

static void TestReentryAndActiveHomingDedup(void)
{
    Reset(7.0f, true, 0);
    Lifter_SetTargetLevel(5); Tick(0);
    Lifter_SetTargetLevel(3); Tick(1000);
    assert(lift_speed == 60.0f);
    Lifter_SetTargetLevel(5); Tick(1100); /* A new home starts with a fresh 10s. */
    assert(lift_speed == -60.0f);
    Lifter_SetTargetLevel(5); Tick(3000);
    Lifter_User_Init(); Tick(5000); /* No restart during active homing. */
    Tick(10000); assert(lift_speed == -60.0f); /* Original deadline is obsolete. */
    Tick(11099); assert(lift_speed == -60.0f);
    Tick(11100); assert(lift_speed == 0.0f);
    Tick(11120); Tick(11140); assert(lift_speed == 0.0f && lift_starts == 3);
    /* Leaving and returning between task cycles still records a fresh edge. */
    Lifter_SetTargetLevel(4); Lifter_SetTargetLevel(5); Tick(11200);
    assert(lift_speed == -60.0f && lift_starts == 4);
    puts("[PASS] Level-5 reentry and repeated/manual requests during active homing");
}

static void TestOrdinaryLevelDirections(void)
{
    Reset(11.0f, true, 0);
    Lifter_SetTargetLevel(1); Tick(0);
    assert(lift_speed == 60.0f);
    Tick(9500); assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 2.0f);
    Lifter_SetTargetLevel(2); Tick(9500);
    assert(lift_speed == -60.0f);
    Tick(13250); assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 5.0f);
    Lifter_SetTargetLevel(3); Tick(13250);
    Tick(15750); assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 7.0f);
    Lifter_SetTargetLevel(4); Tick(15750);
    Tick(17875); assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 9.0f);
    /* Reverse while still moving: mock requires an intervening bridge stop. */
    Lifter_SetTargetLevel(1); Tick(17875); Tick(18075);
    Lifter_SetTargetLevel(4); Tick(18175);
    assert(lift_speed == -60.0f);
    puts("[PASS] Levels 1-4 unchanged, reversed +/-60% PWM and stop before reversal");
}

static void TestWrapAndBootHoming(void)
{
    uint32_t start = UINT32_MAX - 4000U;
    Reset(7.0f, true, start);
    Lifter_SetTargetLevel(5); Tick(start);
    Tick(start + 9999U); assert(lift_speed == -60.0f);
    Tick(start + 10000U); assert(lift_speed == 0.0f);
    Reset(0.0f, false, 100);
    Tick(300); assert(lift_speed == -60.0f);
    Lifter_SetTargetLevel(3); Tick(1300);
    assert(lift_speed == 60.0f && Lifter_GetState() == LIFTER_STATE_AUTO_ADJUST);
    Tick(5800); assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 7.0f);
    Tick(10300); assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 7.0f);
    puts("[PASS] Tick rollover and interruptible invalid-Flash boot homing");
}

static void TestManualInitAndTaskHandoff(void)
{
    Reset(7.0f, true, 0);
    Lifter_User_Init();
    assert(lift_speed == 0.0f && g_app.g_system_report_state == REPORT_NONE);
    Tick(0); assert(lift_speed == -60.0f);
    /* Arrival just after the consumer snapshot must survive for the next update. */
    inject_level_on_exit = 4;
    g_app.g_system_report_state = REPORT_DUMPING_START;
    Tick(1000); assert(lift_speed == -60.0f);
    Tick(1020); /* Late request is consumed on the following update. */
    assert(lift_speed == -60.0f && Lifter_GetState() == LIFTER_STATE_AUTO_ADJUST);
    assert(g_app.g_system_report_state == REPORT_DUMPING_START);
    Tick(2125); assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 9.0f);
    Tick(10000); assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 9.0f);
    Lifter_User_Init(); Tick(10020); assert(lift_speed == -60.0f);
    Lifter_SetTargetLevel(2); Tick(11020);
    assert(lift_speed == 60.0f && Lifter_GetState() == LIFTER_STATE_AUTO_ADJUST);
    puts("[PASS] Manual homing interruption, continued upward target, late request/fault report");
}

static void TestInterruptStopAndTimeBoundaries(void)
{
    Reset(7.0f, true, 0);
    Lifter_User_Init(); Lifter_SetTargetLevel(3); Tick(0);
    assert(lift_speed == 0.0f && lift_starts == 0); /* Cancel before first PWM. */

    Reset(7.0f, true, 0);
    Lifter_SetTargetLevel(5); Tick(0);
    Lifter_SetTargetLevel(4); Tick(2125); /* pos4500-2125*0.8 = level4 exactly. */
    assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 9.0f);
    assert(Lifter_GetState() == LIFTER_STATE_AUTO_ADJUST && lift_starts == 1);
    Tick(12124); assert(!flash_saves);
    Tick(12125); assert(flash_saves == 1 && flash_height == 9.0f);

    Reset(2.0f, true, 0);
    Lifter_SetTargetLevel(5); Tick(0);
    Lifter_SetTargetLevel(3); Tick(9999); /* Last millisecond still interrupts. */
    assert(lift_speed == 60.0f && Lifter_GetCurrentHeight_cm() < 11.0f);
    Tick(10000);
    assert(lift_speed == 60.0f && Lifter_GetState() == LIFTER_STATE_AUTO_ADJUST);
    assert(Lifter_GetCurrentHeight_cm() < 11.0f);

    Reset(2.0f, true, 0);
    Lifter_SetTargetLevel(5); Tick(0);
    Lifter_SetTargetLevel(3); Tick(10000); /* PWM already ran the full 10s. */
    assert(lift_speed == 60.0f && Lifter_GetState() == LIFTER_STATE_AUTO_ADJUST);
    assert(Lifter_GetCurrentHeight_cm() == 11.0f);
    Tick(14500); assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 7.0f);

    uint32_t start = UINT32_MAX - 500U;
    Reset(7.0f, true, start);
    Lifter_SetTargetLevel(5); Tick(start);
    Lifter_SetTargetLevel(3); Tick(start + 1000U);
    assert(lift_speed == 60.0f);
    Tick(start + 1800U);
    assert(lift_speed == 0.0f && Lifter_GetCurrentHeight_cm() == 7.0f);
    Tick(start + 10000U); assert(Lifter_GetCurrentHeight_cm() == 7.0f);
    puts("[PASS] Cancel before start/at target/9999ms, lazy save and interrupt time wrap");
}

static void TestBagUnaffected(void)
{
    Reset(7.0f, true, 0);
    Lifter_SetGrassBagPermission(true);
    Lifter_SetGrassBagTarget(1); assert(bag_speed == 60.0f);
    Lifter_SetTargetLevel(5); Tick(0);
    assert(lift_speed == -60.0f && bag_speed == 60.0f);
    Tick(10000); assert(lift_speed == 0.0f && bag_speed == 60.0f);
    Tick(19999); assert(bag_speed == 60.0f);
    Tick(20000); assert(bag_speed == 0.0f && g_app.grass_bag_status);
    Lifter_SetGrassBagTarget(0); assert(bag_speed == -60.0f);
    Lifter_SetGrassBagPermission(false); assert(bag_speed == 0.0f);
    puts("[PASS] Concurrent grass-bag direction, 20s timing and permission stop unchanged");
}

int main(void)
{
    TestImmediateHomingInterrupt();
    TestFullHomingFromSavedHeights();
    TestLatestTargetWinsDuringHoming();
    TestReentryAndActiveHomingDedup();
    TestOrdinaryLevelDirections();
    TestWrapAndBootHoming();
    TestManualInitAndTaskHandoff();
    TestInterruptStopAndTimeBoundaries();
    TestBagUnaffected();
    puts("[PASS] Production lifter height regression");
    return 0;
}
