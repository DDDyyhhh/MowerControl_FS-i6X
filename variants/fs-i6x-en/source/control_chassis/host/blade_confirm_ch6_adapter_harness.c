#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "blade_confirm_ch6_adapter.h"
#include "blade_manual_control.h"

static BladeConfirmCh6Input_t MakeCh6Input(uint32_t frame, uint16_t pulse_us,
                                           bool fresh, bool timeout,
                                           uint32_t req_seq)
{
    BladeConfirmCh6Input_t in;
    memset(&in, 0, sizeof(in));
    in.frame_sequence = frame;
    in.ch6_raw = pulse_us;
    in.link_fresh = fresh;
    in.timeout_latched = timeout;
    in.active_request_sequence = req_seq;
    return in;
}

/* 1. Test baseline established on startup without generating any event */
static void TestStartupBaselineNoEvent(void)
{
    BladeConfirmCh6Adapter_t adapter;
    BladeConfirmCh6Config_t cfg = BladeConfirmCh6Adapter_DefaultConfig();
    BladeConfirmCh6Adapter_Init(&adapter, &cfg);

    assert(!BladeConfirmCh6Adapter_IsBaselineObserved(&adapter));
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 0U);

    /* Feed 2 frames at LOW (1000 us) - not enough for 3-frame debounce */
    BladeConfirmCh6Input_t in1 = MakeCh6Input(1U, 1000U, true, false, 0U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in1);
    assert(!BladeConfirmCh6Adapter_IsBaselineObserved(&adapter));
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));

    BladeConfirmCh6Input_t in2 = MakeCh6Input(2U, 1000U, true, false, 0U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in2);
    assert(!BladeConfirmCh6Adapter_IsBaselineObserved(&adapter));
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));

    /* Frame 3: reaches 3 frames -> baseline established, but NO event! */
    BladeConfirmCh6Input_t in3 = MakeCh6Input(3U, 1000U, true, false, 0U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in3);
    assert(BladeConfirmCh6Adapter_IsBaselineObserved(&adapter));
    assert(BladeConfirmCh6Adapter_GetCurrentPosition(&adapter) == BLADE_CONFIRM_CH6_POS_LOW);
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 0U);

    /* Frame 4, 5: holding LOW -> still no event */
    BladeConfirmCh6Input_t in4 = MakeCh6Input(4U, 1000U, true, false, 0U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in4);
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 0U);
    assert(adapter.candidate_count == cfg.confirm_frames);
}



/* 2. Test stable new edge generates single event with incremented sequence */
static void TestConfirmedEdgeGeneratesEvent(void)
{
    BladeConfirmCh6Adapter_t adapter;
    BladeConfirmCh6Adapter_Init(&adapter, NULL);

    /* Establish baseline at LOW */
    for (uint32_t f = 1U; f <= 3U; f++)
    {
        BladeConfirmCh6Input_t in = MakeCh6Input(f, 1000U, true, false, 0U);
        BladeConfirmCh6Adapter_Advance(&adapter, &in);
    }
    assert(BladeConfirmCh6Adapter_IsBaselineObserved(&adapter));
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));

    /* Now flip switch to HIGH (2000 us) */
    /* Frame 4: candidate HIGH count 1 -> no event yet */
    BladeConfirmCh6Input_t in4 = MakeCh6Input(4U, 2000U, true, false, 42U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in4);
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));

    /* Frame 5: candidate HIGH count 2 -> no event yet */
    BladeConfirmCh6Input_t in5 = MakeCh6Input(5U, 2000U, true, false, 42U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in5);
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));

    /* Frame 6: candidate HIGH count 3 -> CONFIRMED EDGE! */
    BladeConfirmCh6Input_t in6 = MakeCh6Input(6U, 2000U, true, false, 42U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in6);
    assert(BladeConfirmCh6Adapter_HasEvent(&adapter));
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 1U);
    assert(BladeConfirmCh6Adapter_GetCurrentPosition(&adapter) == BLADE_CONFIRM_CH6_POS_HIGH);

    /* Verify GetConfirmOK */
    BladeConfirmOK_t ok;
    assert(BladeConfirmCh6Adapter_GetConfirmOK(&adapter, 42U, &ok));
    assert(ok.version == BLADE_PROTOCOL_VERSION);
    assert(ok.request_sequence == 42U);
    assert(!BladeConfirmCh6Adapter_GetConfirmOK(&adapter, 41U, &ok));
    assert(ok.event_sequence == 1U);

    /* Holding HIGH must not erase an unconsumed event or generate another one. */
    for (uint32_t f = 7U; f <= 12U; f++)
    {
        BladeConfirmCh6Input_t held = MakeCh6Input(f, 2000U, true, false, 42U);
        BladeConfirmCh6Adapter_Advance(&adapter, &held);
        assert(BladeConfirmCh6Adapter_HasEvent(&adapter));
        assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 1U);
    }

    BladeConfirmCh6Adapter_ConsumeEvent(&adapter, 1U);
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 1U);

    /* The next stable HIGH -> LOW edge creates exactly one new event. */
    for (uint32_t f = 13U; f <= 15U; f++)
    {
        BladeConfirmCh6Input_t low = MakeCh6Input(f, 1000U, true, false, 43U);
        BladeConfirmCh6Adapter_Advance(&adapter, &low);
    }
    assert(BladeConfirmCh6Adapter_HasEvent(&adapter));
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 2U);
    assert(BladeConfirmCh6Adapter_GetCurrentPosition(&adapter) == BLADE_CONFIRM_CH6_POS_LOW);
    assert(adapter.candidate_count == adapter.config.confirm_frames);
}

/* 3. Test noise and transitional pulse rejection during debounce */
static void TestNoiseRejectionDuringDebounce(void)
{
    BladeConfirmCh6Adapter_t adapter;
    BladeConfirmCh6Adapter_Init(&adapter, NULL);

    /* Establish baseline at LOW */
    for (uint32_t f = 1U; f <= 3U; f++)
    {
        BladeConfirmCh6Input_t in = MakeCh6Input(f, 1000U, true, false, 0U);
        BladeConfirmCh6Adapter_Advance(&adapter, &in);
    }

    /* 2 frames at HIGH, then a noise pulse (1500 us, neither LOW nor HIGH), then HIGH again */
    BladeConfirmCh6Input_t in4 = MakeCh6Input(4U, 2000U, true, false, 1U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in4);
    BladeConfirmCh6Input_t in5 = MakeCh6Input(5U, 2000U, true, false, 1U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in5);
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));

    /* Noise pulse resets candidate */
    BladeConfirmCh6Input_t in6 = MakeCh6Input(6U, 1500U, true, false, 1U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in6);
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));

    /* Now needs 3 fresh frames at HIGH */
    BladeConfirmCh6Input_t in7 = MakeCh6Input(7U, 2000U, true, false, 1U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in7);
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));

    BladeConfirmCh6Input_t in8 = MakeCh6Input(8U, 2000U, true, false, 1U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in8);
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));

    BladeConfirmCh6Input_t in9 = MakeCh6Input(9U, 2000U, true, false, 1U);
    BladeConfirmCh6Adapter_Advance(&adapter, &in9);
    assert(BladeConfirmCh6Adapter_HasEvent(&adapter));
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 1U);
}

/* 4. Test link timeout and recovery resets baseline without generating events */
static void TestLinkTimeoutAndRecoveryResetsBaseline(void)
{
    BladeConfirmCh6Adapter_t adapter;
    BladeConfirmCh6Adapter_Init(&adapter, NULL);

    /* Establish baseline at LOW */
    for (uint32_t f = 1U; f <= 3U; f++)
    {
        BladeConfirmCh6Input_t in = MakeCh6Input(f, 1000U, true, false, 0U);
        BladeConfirmCh6Adapter_Advance(&adapter, &in);
    }
    assert(BladeConfirmCh6Adapter_IsBaselineObserved(&adapter));

    /* Generate edge to HIGH -> event sequence 1 */
    for (uint32_t f = 4U; f <= 6U; f++)
    {
        BladeConfirmCh6Input_t in = MakeCh6Input(f, 2000U, true, false, 1U);
        BladeConfirmCh6Adapter_Advance(&adapter, &in);
    }
    assert(BladeConfirmCh6Adapter_HasEvent(&adapter));
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 1U);

    /* Link times out */
    BladeConfirmCh6Input_t timeout_in = MakeCh6Input(7U, 2000U, false, true, 1U);
    BladeConfirmCh6Adapter_Advance(&adapter, &timeout_in);
    assert(!BladeConfirmCh6Adapter_IsBaselineObserved(&adapter));
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));
    assert(BladeConfirmCh6Adapter_GetCurrentPosition(&adapter) == BLADE_CONFIRM_CH6_POS_UNKNOWN);

    /* Link recovers with switch still at HIGH:
     * Must establish baseline at HIGH ONLY and NOT generate a new event! */
    for (uint32_t f = 8U; f <= 10U; f++)
    {
        BladeConfirmCh6Input_t in = MakeCh6Input(f, 2000U, true, false, 1U);
        BladeConfirmCh6Adapter_Advance(&adapter, &in);
    }
    assert(BladeConfirmCh6Adapter_IsBaselineObserved(&adapter));
    assert(BladeConfirmCh6Adapter_GetCurrentPosition(&adapter) == BLADE_CONFIRM_CH6_POS_HIGH);
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 1U); /* Sequence did not increment! */

    /* Only a subsequent flip from HIGH to LOW creates a new event (seq = 2) */
    for (uint32_t f = 11U; f <= 13U; f++)
    {
        BladeConfirmCh6Input_t in = MakeCh6Input(f, 1000U, true, false, 2U);
        BladeConfirmCh6Adapter_Advance(&adapter, &in);
    }
    assert(BladeConfirmCh6Adapter_HasEvent(&adapter));
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 2U);
    assert(BladeConfirmCh6Adapter_GetCurrentPosition(&adapter) == BLADE_CONFIRM_CH6_POS_LOW);
}

/* 5. Test single-use consumption and duplicate rejection */
static void TestSingleUseConsumptionAndDuplicateRejection(void)
{
    BladeConfirmCh6Adapter_t adapter;
    BladeConfirmCh6Adapter_Init(&adapter, NULL);

    /* Baseline LOW */
    for (uint32_t f = 1U; f <= 3U; f++)
    {
        BladeConfirmCh6Input_t in = MakeCh6Input(f, 1000U, true, false, 0U);
        BladeConfirmCh6Adapter_Advance(&adapter, &in);
    }

    /* Edge to HIGH with request sequence 10 */
    for (uint32_t f = 4U; f <= 6U; f++)
    {
        BladeConfirmCh6Input_t in = MakeCh6Input(f, 2000U, true, false, 10U);
        BladeConfirmCh6Adapter_Advance(&adapter, &in);
    }
    assert(BladeConfirmCh6Adapter_HasEvent(&adapter));

    BladeConfirmOK_t ok;
    assert(BladeConfirmCh6Adapter_GetConfirmOK(&adapter, 10U, &ok));
    assert(ok.event_sequence == 1U);

    /* Consume event */
    BladeConfirmCh6Adapter_ConsumeEvent(&adapter, 1U);
    assert(!BladeConfirmCh6Adapter_HasEvent(&adapter));

    /* Duplicate attempt to query same consumed event must fail */
    assert(!BladeConfirmCh6Adapter_GetConfirmOK(&adapter, 10U, &ok));

    /* Mismatched request sequence must also fail */
    assert(!BladeConfirmCh6Adapter_GetConfirmOK(&adapter, 99U, &ok));
}

/* 6. Test sequence counter wraparound (UINT32_MAX -> 1) */
static void TestSequenceWraparound(void)
{
    BladeConfirmCh6Adapter_t adapter;
    BladeConfirmCh6Adapter_Init(&adapter, NULL);
    adapter.event_sequence = UINT32_MAX;

    /* Baseline LOW */
    for (uint32_t f = 1U; f <= 3U; f++)
    {
        BladeConfirmCh6Input_t in = MakeCh6Input(f, 1000U, true, false, 0U);
        BladeConfirmCh6Adapter_Advance(&adapter, &in);
    }

    /* Edge to HIGH */
    for (uint32_t f = 4U; f <= 6U; f++)
    {
        BladeConfirmCh6Input_t in = MakeCh6Input(f, 2000U, true, false, 1U);
        BladeConfirmCh6Adapter_Advance(&adapter, &in);
    }

    assert(BladeConfirmCh6Adapter_HasEvent(&adapter));
    /* Wraparound must be 1, never 0 */
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 1U);
}

/* 7. Full Integration: BladeManualControl with CH6 Adapter */
static void TestIntegrationWithBladeManualControl(void)
{
    BladeConfirmCh6Adapter_t adapter;
    BladeConfirmCh6Adapter_Init(&adapter, NULL);

    BladeManualControl_t bmc;
    BladeManualControl_Init(&bmc, NULL);

    uint32_t frame = 0U;

    /* A. System starts: establish CH6 baseline (LOW) and CH4 center baseline */
    for (uint32_t i = 0U; i < 3U; i++)
    {
        frame++;
        BladeConfirmCh6Input_t ch6_in = MakeCh6Input(frame, 1000U, true, false, 0U);
        BladeConfirmCh6Adapter_Advance(&adapter, &ch6_in);

        BladeManualControlInput_t bmc_in;
        memset(&bmc_in, 0, sizeof(bmc_in));
        bmc_in.frame_sequence = frame;
        bmc_in.link_fresh = true;
        bmc_in.remote_run_enable = true;
        bmc_in.run_permitted = true;
        bmc_in.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
        bmc_in.ch4_position = BLADE_CH4_POS_CENTER;
        bmc_in.ch7_raw = 1000U;
        bmc_in.confirmed_gear = REMOTE_BLADE_GEAR_HIGH;
        BladeManualControl_Step(&bmc, &bmc_in);
    }
    assert(BladeConfirmCh6Adapter_IsBaselineObserved(&adapter));
    assert(BladeManualControl_IsCenterBaselineObserved(&bmc));
    assert(BladeManualControl_GetState(&bmc) == BLADE_STATE_IDLE);

    /* B. Scenario: Operator flips CH6 BEFORE pending (pending 前) */
    for (uint32_t i = 0U; i < 3U; i++)
    {
        frame++;
        BladeConfirmCh6Input_t ch6_in = MakeCh6Input(frame, 2000U, true, false, 0U);
        BladeConfirmCh6Adapter_Advance(&adapter, &ch6_in);

        BladeManualControlInput_t bmc_in;
        memset(&bmc_in, 0, sizeof(bmc_in));
        bmc_in.frame_sequence = frame;
        bmc_in.link_fresh = true;
        bmc_in.remote_run_enable = true;
        bmc_in.run_permitted = true;
        bmc_in.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
        bmc_in.ch4_position = BLADE_CH4_POS_CENTER;
        bmc_in.ch7_raw = 1000U;
        bmc_in.confirmed_gear = REMOTE_BLADE_GEAR_HIGH;
        BladeManualControl_Step(&bmc, &bmc_in);
    }
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 1U);
    /* Blade is still IDLE because CH4 was CENTER */
    assert(BladeManualControl_GetState(&bmc) == BLADE_STATE_IDLE);

    /* C. Operator now moves CH4 to START -> enters PENDING */
    frame++;
    uint32_t req_seq = 0U;
    {
        BladeManualControlInput_t bmc_in;
        memset(&bmc_in, 0, sizeof(bmc_in));
        bmc_in.frame_sequence = frame;
        bmc_in.link_fresh = true;
        bmc_in.remote_run_enable = true;
        bmc_in.run_permitted = true;
        bmc_in.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
        bmc_in.ch4_position = BLADE_CH4_POS_START;
        bmc_in.ch7_raw = 1000U;
        bmc_in.confirmed_gear = REMOTE_BLADE_GEAR_HIGH;
        bmc_in.confirm_event_sequence = BladeConfirmCh6Adapter_GetEventSequence(&adapter);
        /* Attempt to feed the prior event generated before pending:
         * Must be rejected because it arrived at or before pending baseline */
        if (BladeConfirmCh6Adapter_HasEvent(&adapter))
        {
            BladeConfirmOK_t ok;
            if (BladeConfirmCh6Adapter_GetConfirmOK(&adapter, 1U, &ok))
            {
                bmc_in.has_confirm_event = true;
                bmc_in.confirm_event = ok;
            }
        }
        BladeManualControl_Step(&bmc, &bmc_in);
        assert(BladeManualControl_GetState(&bmc) == BLADE_STATE_START_CONFIRM_PENDING);
        req_seq = BladeManualControl_GetRequestSequence(&bmc);
        assert(req_seq == 1U);
        assert(BladeManualControl_GetCommand(&bmc) == 0.0f);
    }

    /* Next frame: verify that the old event (seq = 1) cannot start the blade */
    frame++;
    {
        BladeManualControlInput_t bmc_in;
        memset(&bmc_in, 0, sizeof(bmc_in));
        bmc_in.frame_sequence = frame;
        bmc_in.link_fresh = true;
        bmc_in.remote_run_enable = true;
        bmc_in.run_permitted = true;
        bmc_in.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
        bmc_in.ch4_position = BLADE_CH4_POS_START;
        bmc_in.ch7_raw = 2000U;
        bmc_in.confirmed_gear = REMOTE_BLADE_GEAR_LOW;
        bmc_in.confirm_event_sequence = BladeConfirmCh6Adapter_GetEventSequence(&adapter);
        bmc_in.confirm_event.version = BLADE_PROTOCOL_VERSION;
        bmc_in.confirm_event.request_sequence = req_seq;
        bmc_in.confirm_event.event_sequence = 1U; /* Old event! */
        BladeManualControl_Step(&bmc, &bmc_in);
        assert(BladeManualControl_GetState(&bmc) == BLADE_STATE_START_CONFIRM_PENDING);
        assert(BladeManualControl_GetCommand(&bmc) == 0.0f);
    }

    /* D. Now operator creates a NEW confirmed edge on CH6 (flip from HIGH back to LOW) */
    for (uint32_t i = 0U; i < 3U; i++)
    {
        frame++;
        BladeConfirmCh6Input_t ch6_in = MakeCh6Input(frame, 1000U, true, false, req_seq);
        BladeConfirmCh6Adapter_Advance(&adapter, &ch6_in);

        BladeManualControlInput_t bmc_in;
        memset(&bmc_in, 0, sizeof(bmc_in));
        bmc_in.frame_sequence = frame;
        bmc_in.link_fresh = true;
        bmc_in.remote_run_enable = true;
        bmc_in.run_permitted = true;
        bmc_in.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
        bmc_in.ch4_position = BLADE_CH4_POS_START;
        bmc_in.ch7_raw = 2000U;
        bmc_in.confirmed_gear = REMOTE_BLADE_GEAR_LOW;
        bmc_in.confirm_event_sequence = BladeConfirmCh6Adapter_GetEventSequence(&adapter);
        BladeConfirmOK_t ok;
        if (BladeConfirmCh6Adapter_GetConfirmOK(&adapter, req_seq, &ok))
        {
            bmc_in.has_confirm_event = true;
            bmc_in.confirm_event = ok;
        }

        BladeManualControl_Step(&bmc, &bmc_in);

        if (BladeManualControl_GetState(&bmc) == BLADE_STATE_RUNNING && bmc_in.has_confirm_event)
        {
            BladeConfirmCh6Adapter_ConsumeEvent(&adapter, ok.event_sequence);
        }
    }

    /* Confirmation succeeded! Transitioned to RUNNING */
    assert(BladeManualControl_GetState(&bmc) == BLADE_STATE_RUNNING);
    assert(BladeConfirmCh6Adapter_GetEventSequence(&adapter) == 2U);

    /* Frame after confirmation: fresh CH7 activates speed */
    frame++;
    {
        BladeManualControlInput_t bmc_in;
        memset(&bmc_in, 0, sizeof(bmc_in));
        bmc_in.frame_sequence = frame;
        bmc_in.link_fresh = true;
        bmc_in.remote_run_enable = true;
        bmc_in.run_permitted = true;
        bmc_in.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
        bmc_in.ch4_position = BLADE_CH4_POS_START;
        bmc_in.ch7_raw = 2000U;
        bmc_in.confirmed_gear = REMOTE_BLADE_GEAR_LOW;
        bmc_in.confirm_event_sequence = BladeConfirmCh6Adapter_GetEventSequence(&adapter);
        BladeManualControl_Step(&bmc, &bmc_in);
        assert(BladeManualControl_GetState(&bmc) == BLADE_STATE_RUNNING);
        assert(BladeManualControl_GetCommand(&bmc) > 0.0f);
    }

    /* E. Scenario: Cancellation (pending 取消后)
     * Operator returns CH4 to center -> Immediate Stop */
    frame++;
    {
        BladeManualControlInput_t bmc_in;
        memset(&bmc_in, 0, sizeof(bmc_in));
        bmc_in.frame_sequence = frame;
        bmc_in.link_fresh = true;
        bmc_in.remote_run_enable = true;
        bmc_in.run_permitted = true;
        bmc_in.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
        bmc_in.ch4_position = BLADE_CH4_POS_CENTER;
        bmc_in.ch7_raw = 2000U;
        bmc_in.confirmed_gear = REMOTE_BLADE_GEAR_LOW;
        bmc_in.confirm_event_sequence = BladeConfirmCh6Adapter_GetEventSequence(&adapter);
        BladeManualControl_Step(&bmc, &bmc_in);
        assert(BladeManualControl_GetState(&bmc) == BLADE_STATE_IDLE ||
               BladeManualControl_GetState(&bmc) == BLADE_STATE_STOPPING);
        assert(BladeManualControl_GetCommand(&bmc) == 0.0f);
    }

    /* Step to IDLE */
    frame++;
    {
        BladeManualControlInput_t bmc_in;
        memset(&bmc_in, 0, sizeof(bmc_in));
        bmc_in.frame_sequence = frame;
        bmc_in.link_fresh = true;
        bmc_in.remote_run_enable = true;
        bmc_in.run_permitted = true;
        bmc_in.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
        bmc_in.ch4_position = BLADE_CH4_POS_CENTER;
        bmc_in.ch7_raw = 1000U;
        bmc_in.confirmed_gear = REMOTE_BLADE_GEAR_HIGH;
        bmc_in.confirm_event_sequence = BladeConfirmCh6Adapter_GetEventSequence(&adapter);
        BladeManualControl_Step(&bmc, &bmc_in);
        assert(BladeManualControl_GetState(&bmc) == BLADE_STATE_IDLE);
    }

    /* Start new pending with request sequence 2 */
    frame++;
    {
        BladeManualControlInput_t bmc_in;
        memset(&bmc_in, 0, sizeof(bmc_in));
        bmc_in.frame_sequence = frame;
        bmc_in.link_fresh = true;
        bmc_in.remote_run_enable = true;
        bmc_in.run_permitted = true;
        bmc_in.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
        bmc_in.ch4_position = BLADE_CH4_POS_START;
        bmc_in.ch7_raw = 1000U;
        bmc_in.confirmed_gear = REMOTE_BLADE_GEAR_HIGH;
        bmc_in.confirm_event_sequence = BladeConfirmCh6Adapter_GetEventSequence(&adapter);
        BladeManualControl_Step(&bmc, &bmc_in);
        assert(BladeManualControl_GetState(&bmc) == BLADE_STATE_START_CONFIRM_PENDING);
        assert(BladeManualControl_GetRequestSequence(&bmc) == 2U);
    }

    /* Attempt to replay the old event (from the cancelled session: request_sequence = 1, event_sequence = 2):
     * Must be rejected! */
    frame++;
    {
        BladeManualControlInput_t bmc_in;
        memset(&bmc_in, 0, sizeof(bmc_in));
        bmc_in.frame_sequence = frame;
        bmc_in.link_fresh = true;
        bmc_in.remote_run_enable = true;
        bmc_in.run_permitted = true;
        bmc_in.selected_owner = EXECUTION_OWNER_MANUAL_REMOTE;
        bmc_in.ch4_position = BLADE_CH4_POS_START;
        bmc_in.ch7_raw = 2000U;
        bmc_in.confirmed_gear = REMOTE_BLADE_GEAR_LOW;
        bmc_in.confirm_event_sequence = BladeConfirmCh6Adapter_GetEventSequence(&adapter);
        bmc_in.has_confirm_event = true;
        bmc_in.confirm_event.version = BLADE_PROTOCOL_VERSION;
        bmc_in.confirm_event.request_sequence = 1U; /* Mismatched old request sequence! */
        bmc_in.confirm_event.event_sequence = 2U;    /* Old event sequence! */
        BladeManualControl_Step(&bmc, &bmc_in);
        assert(BladeManualControl_GetState(&bmc) == BLADE_STATE_START_CONFIRM_PENDING);
        assert(BladeManualControl_GetCommand(&bmc) == 0.0f);
    }
}

int main(void)
{
    TestStartupBaselineNoEvent();
    TestConfirmedEdgeGeneratesEvent();
    TestNoiseRejectionDuringDebounce();
    TestLinkTimeoutAndRecoveryResetsBaseline();
    TestSingleUseConsumptionAndDuplicateRejection();
    TestSequenceWraparound();
    TestIntegrationWithBladeManualControl();

    printf("  [PASS] Blade Confirm CH6 Adapter (Baseline, edge, debounce, replay, integration)\n");
    return 0;
}
