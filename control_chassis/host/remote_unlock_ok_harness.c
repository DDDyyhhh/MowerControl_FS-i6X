#include "remote_authorization.h"
#include <assert.h>
#include <stdio.h>

static RemoteAuthorization_t auth;
static RemoteAuthorizationInput_t input;
static uint32_t now;

static void frame(uint16_t ch4, uint32_t at)
{
    now = at;
    input.ch4_raw = ch4;
    ++input.frame_sequence;
    RemoteAuthorization_Advance(&auth, &input, now);
}

static void init(uint16_t ch4)
{
    FlySkyIbusConfig_t config = {0};
    config.run_enable_semantics_configured = true;
    config.run_enable_on_us = 2000U;
    config.run_enable_off_us = 1000U;
    config.switch_position_tolerance_us = 100U;
    config.switch_confirm_frames = 3U;
    config.steering.center_us = config.throttle.center_us = 1500U;
    config.stick_center_tolerance_us = 50U;
    config.stick_center_hold_ms = 200U;
    RemoteAuthorization_Init(&auth, &config);
    input = (RemoteAuthorizationInput_t){0};
    input.link_fresh = input.stable_online = true;
    input.run_enable_raw = 2000U;
    input.steering_raw = input.throttle_raw = 1500U;
    for (unsigned t = 0U; t <= 250U; t += 10U) frame(ch4, t);
}

static void request(void)
{
    init(1500U);
    frame(1000U, 260U);
    assert(auth.output.state == REMOTE_AUTH_WAIT_OK);
    assert(!auth.output.run_permitted);
}

static void normal_cancel_and_duplicates(void)
{
    request(); input.physical_latched = true; /* Historical collision/button latch, raw input released. */
    assert(RemoteAuthorization_ConfirmOK(&auth, &input, now));
    frame(1000U, 270U); assert(auth.output.run_permitted);
    input.physical_latched = false; /* ApplyCommand clears it only after explicit authorization. */
    frame(1000U, 280U); assert(auth.output.run_permitted);
    request();
    frame(1000U, 500U);
    assert(!auth.output.run_permitted); /* Holding full is not confirmation. */
    assert(RemoteAuthorization_ConfirmOK(&auth, &input, now));
    assert(!auth.output.run_permitted);
    assert(!RemoteAuthorization_ConfirmOK(&auth, &input, now));
    RemoteAuthorization_Advance(&auth, &input, now + 1U);
    assert(!auth.output.run_permitted); /* Same frame never drives. */
    frame(1000U, 510U);
    assert(auth.output.run_permitted);
    frame(1500U, 520U);
    RemoteAuthorization_CancelOK(&auth); /* Returning CH4 cannot relock after success. */
    assert(auth.output.run_permitted);
    RemoteAuthorization_Revoke(&auth);
    frame(1000U, 530U);
    assert(!RemoteAuthorization_ConfirmOK(&auth, &input, now));
    request(); frame(1500U, 270U);
    assert(!RemoteAuthorization_ConfirmOK(&auth, &input, now));
}

static void endpoints_and_ch8(void)
{
    const uint16_t values[] = {899U, 900U, 988U, 1000U, 1100U, 1101U, 1900U, 2000U, 2100U};
    for (unsigned i = 0U; i < sizeof(values)/sizeof(values[0]); ++i) {
        init(1500U); frame(values[i], 260U);
        assert(RemoteAuthorization_ConfirmOK(&auth, &input, now) ==
               (values[i] >= 900U && values[i] <= 1100U));
    }
    const uint16_t centers[] = {1399U, 1400U, 1600U, 1601U};
    for (unsigned i = 0; i < 4; ++i) {
        init(1000U); frame(centers[i], 260U); frame(1000U, 270U);
        assert(RemoteAuthorization_ConfirmOK(&auth, &input, now) ==
               (centers[i] >= 1400U && centers[i] <= 1600U));
    }
    init(1000U);
    assert(!RemoteAuthorization_ConfirmOK(&auth, &input, now));
    for (unsigned t = 260U; t < 1000U; t += 10U) {
        input.unlock_raw = ((t / 30U) & 1U) ? 1000U : 2000U;
        frame(1000U, t);
        assert(!auth.output.run_permitted);
    }
}

static void faults_center_hold_and_recovery(void)
{
    for (unsigned fault = 0; fault < 6; ++fault) {
        request();
        if (fault == 0) input.emergency_active = true;
        if (fault == 1) input.collision_active = true;
        if (fault == 2) input.tilt_active = true;
        if (fault == 3) input.link_fresh = input.stable_online = false;
        if (fault == 4) input.run_enable_raw = 1000U;
        if (fault == 5) input.steering_raw = 1551U;
        frame(1000U, 270U); frame(1000U, 280U); frame(1000U, 290U);
        assert(!RemoteAuthorization_ConfirmOK(&auth, &input, now));
        input.emergency_active = input.collision_active = input.tilt_active = false;
        input.link_fresh = input.stable_online = true;
        input.run_enable_raw = 2000U; input.steering_raw = 1500U;
        for (unsigned t = 300U; t <= 600U; t += 10U) frame(1000U, t);
        assert(!RemoteAuthorization_ConfirmOK(&auth, &input, now));
        assert(!auth.output.run_permitted);
    }
    request(); input.steering_raw = 1600U; frame(1000U, 270U);
    input.steering_raw = 1500U; frame(1000U, 280U);
    frame(1000U, 479U);
    assert(!RemoteAuthorization_ConfirmOK(&auth, &input, now)); /* 199ms rejects and consumes. */
    frame(1500U, 480U); frame(1000U, 490U);
    for (unsigned t = 500U; t <= 700U; t += 10U) frame(1000U, t);
    assert(RemoteAuthorization_ConfirmOK(&auth, &input, now));
    frame(1000U, 710U); assert(auth.output.run_permitted);
    input.physical_latched = true;
    frame(1000U, 711U); assert(!auth.output.run_permitted);
    assert(!RemoteAuthorization_ConfirmOK(&auth, &input, now));
    input.physical_latched = false;
    input.timeout_sequence++; input.link_fresh = false; frame(1000U, 720U);
    input.link_fresh = true;
    for (unsigned t = 730U; t <= 1000U; t += 10U) frame(1000U, t);
    assert(!RemoteAuthorization_ConfirmOK(&auth, &input, now));
    assert(!auth.output.run_permitted);
}

int main(void)
{
    normal_cancel_and_duplicates(); endpoints_and_ch8(); faults_center_hold_and_recovery();
    puts("PASS production CH4/OK: fresh edge, thresholds, CH8 inert, safety, 200ms hold, no replay/duplicate/same-frame drive");
}
