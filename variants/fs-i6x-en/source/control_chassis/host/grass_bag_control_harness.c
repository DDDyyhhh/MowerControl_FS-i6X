#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "grass_bag_control.h"

static void TestRemote(void)
{
    GrassBagRemote_t rc = {0};
    GrassBagTarget_t target = GRASS_BAG_TARGET_UNKNOWN;
    assert(!GrassBagRemote_Advance(&rc, 1000, 1, true, &target));
    for (unsigned i = 0; i < 10; ++i)
        assert(!GrassBagRemote_Advance(&rc, 1000, 1, true, &target));
    assert(rc.candidate_count == 1);
    assert(!GrassBagRemote_Advance(&rc, 1000, 2, true, &target));
    assert(!GrassBagRemote_Advance(&rc, 1000, 3, true, &target));
    assert(rc.stable_target == GRASS_BAG_TARGET_OPEN);
    assert(!GrassBagRemote_Advance(&rc, 1880, 4, true, &target));
    assert(!GrassBagRemote_Advance(&rc, 2000, 5, true, &target));
    assert(!GrassBagRemote_Advance(&rc, 1500, 6, true, &target));
    assert(rc.candidate_count == 0);
    assert(!GrassBagRemote_Advance(&rc, 2120, 7, true, &target));
    assert(!GrassBagRemote_Advance(&rc, 2000, 8, true, &target));
    assert(GrassBagRemote_Advance(&rc, 2000, 9, true, &target));
    assert(target == GRASS_BAG_TARGET_CLOSED);
    for (uint32_t f = 10; f < 20; ++f)
        assert(!GrassBagRemote_Advance(&rc, 2000, f, true, &target));
    /* Fault/lock/bag unplug/API mode/reconnect all reset through this input. */
    for (unsigned reason = 0; reason < 6; ++reason) {
        uint32_t base = 20 + reason * 10;
        assert(!GrassBagRemote_Advance(&rc, 2000, base, false, &target));
        assert(!GrassBagRemote_Advance(&rc, 1000, base, true, &target));
        assert(rc.candidate_count == 0);
        for (uint32_t f = base + 1; f <= base + 3; ++f)
            assert(!GrassBagRemote_Advance(&rc, 1000, f, true, &target));
        for (uint32_t f = base + 4; f <= base + 6; ++f)
            assert(GrassBagRemote_Advance(&rc, 2000, f, true, &target) == (f == base + 6));
    }
    assert(!GrassBagRemote_Advance(&rc, 1000, UINT32_MAX, true, &target));
    assert(!GrassBagRemote_Advance(&rc, 1000, 0, true, &target));
    assert(GrassBagRemote_Advance(&rc, 1000, 1, true, &target));
}

static void TestActuator(void)
{
    GrassBagActuator_t bag = {0};
    assert(GrassBagActuator_SetTarget(&bag, true, 100));
    assert(!GrassBagActuator_SetTarget(&bag, true, 500));
    assert(bag.started_ms == 100);
    assert(!GrassBagActuator_Advance(&bag, 20099));
    assert(GrassBagActuator_Advance(&bag, 20100));
    assert(bag.completed_open && bag.state == GRASS_BAG_OPEN);
    assert(!GrassBagActuator_SetTarget(&bag, true, 20101));
    assert(GrassBagActuator_SetTarget(&bag, false, 22000));
    GrassBagActuator_Stop(&bag);
    assert(bag.state == GRASS_BAG_STOPPED && bag.completed_open);
    assert(!GrassBagActuator_Advance(&bag, 90000));
    assert(GrassBagActuator_SetTarget(&bag, false, 90001));
    assert(GrassBagActuator_Advance(&bag, 110001));
    assert(!bag.completed_open);
    assert(GrassBagActuator_SetTarget(&bag, true, 120000));
    assert(GrassBagActuator_SetTarget(&bag, false, 121000));
    assert(!GrassBagActuator_Advance(&bag, 140000));
    assert(GrassBagActuator_Advance(&bag, 141000));
    assert(!bag.completed_open);
    assert(GrassBagActuator_SetTarget(&bag, true, UINT32_MAX - 100));
    assert(!GrassBagActuator_Advance(&bag, 19898));
    assert(GrassBagActuator_Advance(&bag, 19899));
}
int main(void)
{
    TestRemote();
    TestActuator();
    puts("[PASS] Grass bag baseline/debounce/invalidation/reversal/stop/timeout");
    return 0;
}
