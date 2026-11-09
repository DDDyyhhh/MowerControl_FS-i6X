#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "remote_backend.h"
#include "flysky_ibus_servo.h"

static uint32_t now_ms;
static uint32_t irq_mask;
static unsigned critical_depth;
static unsigned entries;
static unsigned barriers;
static unsigned authorization_calls;
static void (*before_mask)(void);
static void (*while_masked)(void);
static void (*pending)(void);
static uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];

void __real_FlySkyIbusServo_FeedReceived(FlySkyIbusServo_t *ibus, const uint8_t *data,
                                uint16_t length, uint32_t received_at_ms, uint32_t time_ms);
void __wrap_FlySkyIbusServo_FeedReceived(FlySkyIbusServo_t *ibus, const uint8_t *data,
                                uint16_t length, uint32_t received_at_ms, uint32_t time_ms)
{
    assert(critical_depth == 0U);
    __real_FlySkyIbusServo_FeedReceived(ibus, data, length, received_at_ms, time_ms);
}

void __real_FlySkyIbusServo_Advance(FlySkyIbusServo_t *ibus, uint32_t time_ms);
void __wrap_FlySkyIbusServo_Advance(FlySkyIbusServo_t *ibus, uint32_t time_ms)
{
    assert(critical_depth == 0U);
    __real_FlySkyIbusServo_Advance(ibus, time_ms);
}

uint32_t IbusTest_GetPrimask(void)
{
    if (before_mask != NULL)
    {
        void (*callback)(void) = before_mask;
        before_mask = NULL;
        callback();
    }
    return irq_mask;
}

void IbusTest_DisableIrq(void)
{
    irq_mask = 1U;
    ++critical_depth;
    ++entries;
    if (while_masked != NULL)
    {
        pending = while_masked;
        while_masked = NULL;
    }
}

void IbusTest_SetPrimask(uint32_t mask)
{
    assert(critical_depth > 0U);
    --critical_depth;
    irq_mask = mask;
    if (mask == 0U && pending != NULL)
    {
        void (*callback)(void) = pending;
        pending = NULL;
        callback();
    }
}

void IbusTest_CompilerBarrier(void)
{
    assert(irq_mask == 1U && critical_depth > 0U);
    ++barriers;
}

void __real_RemoteAuthorization_Advance(RemoteAuthorization_t *authorization,
                                        const RemoteAuthorizationInput_t *input,
                                        uint32_t time_ms);
void __wrap_RemoteAuthorization_Advance(RemoteAuthorization_t *authorization,
                                        const RemoteAuthorizationInput_t *input,
                                        uint32_t time_ms)
{
    assert(critical_depth == 0U);
    ++authorization_calls;
    __real_RemoteAuthorization_Advance(authorization, input, time_ms);
}

static uint32_t ClockNow(void *context)
{
    (void)context;
    assert(critical_depth == 0U);
    return now_ms;
}

static void FeedFrameWithChecksum(bool corrupt)
{
    uint8_t frame[REMOTE_IBUS_FRAME_SIZE] = {0x20U, 0x40U};
    uint16_t checksum = 0xFFFFU;
    for (unsigned i = 0; i < REMOTE_IBUS_CHANNEL_COUNT; ++i)
    {
        frame[2U + i * 2U] = (uint8_t)channels[i];
        frame[3U + i * 2U] = (uint8_t)(channels[i] >> 8);
    }
    for (unsigned i = 0; i < 30U; ++i) checksum -= frame[i];
    frame[30] = (uint8_t)checksum;
    frame[31] = (uint8_t)(checksum >> 8);
    if (corrupt) frame[30] ^= 1U;
    RemoteBackend_OnUartRx(1U, frame, sizeof(frame));
}

static void FeedFrame(void)
{
    FeedFrameWithChecksum(false);
}

static void Init(uint16_t pulse)
{
    assert(irq_mask == 0U && critical_depth == 0U);
    for (unsigned i = 0; i < REMOTE_IBUS_CHANNEL_COUNT; ++i) channels[i] = pulse;
    RemoteBackend_Init((RemoteBackendClock_t){ClockNow, NULL});
}

static void TestMotionTimeoutDoesNotWriteCommState(uint32_t start_ms)
{
    RemoteCommand_t command;
    Init(1500U);
    now_ms = start_ms;
    FeedFrame();
    now_ms += 99U;
    RemoteBackend_Advance(now_ms, (RemoteSafetyInput_t){0});
    assert(RemoteBackend_GetCommand(&command));
    now_ms++;
    /* Exercise both the Motion tick and GetCommand's own timeout guard. */
    if (start_ms == 10U) RemoteBackend_Advance(now_ms, (RemoteSafetyInput_t){0});
    assert(!RemoteBackend_GetCommand(&command));
    assert(!command.run_permitted && command.timeout_latched);
    assert(command.steering == 2048U && command.throttle == 2048U);
    assert(command.travel_speed_raw == 0U);
    /* Comm has not run: Motion must not publish a parser timeout event. */
    assert(command.timeout_sequence == 0U);
    assert(command.recoverable_latch_reason == REMOTE_LATCH_REASON_TIMEOUT);
    uint32_t invalidations = command.command_invalidation_sequence;
    for (unsigned i = 0; i < 10U; ++i)
    {
        assert(!RemoteBackend_GetCommand(&command));
        assert(command.timeout_sequence == 0U);
        assert(command.command_invalidation_sequence == invalidations);
    }
    /* No-data Comm iteration records the same outage exactly once. */
    RemoteBackend_Process();
    assert(!RemoteBackend_GetCommand(&command));
    assert(command.timeout_sequence == 1U);
    RemoteBackend_Process();
    assert(!RemoteBackend_GetCommand(&command));
    assert(command.timeout_sequence == 1U);
    now_ms++;
    FeedFrame();
    assert(RemoteBackend_GetCommand(&command));
    assert(!command.stable_online && !command.run_permitted);
    assert(command.timeout_latched && command.timeout_sequence == 1U);
    assert(!RemoteBackend_ConfirmUnlock());
}

static void ReadOldDuringPublication(void)
{
    RemoteCommand_t command;
    assert(RemoteBackend_GetCommand(&command));
    assert(command.frame_sequence == 1U);
    for (unsigned i = 0; i < REMOTE_IBUS_CHANNEL_COUNT; ++i)
        assert(command.channels[i] == 1000U);
}

static void ReadNewAfterPublication(void)
{
    RemoteCommand_t command;
    assert(irq_mask == 0U && critical_depth == 0U);
    assert(RemoteBackend_GetCommand(&command));
    assert(command.frame_sequence == 2U);
    for (unsigned i = 0; i < REMOTE_IBUS_CHANNEL_COUNT; ++i)
        assert(command.channels[i] == 2000U);
}

static void TestPublicationPreemption(void)
{
    Init(1000U);
    now_ms = 10U;
    FeedFrame();
    before_mask = ReadOldDuringPublication;
    for (unsigned i = 0; i < REMOTE_IBUS_CHANNEL_COUNT; ++i) channels[i] = 2000U;
    now_ms = 20U;
    FeedFrame();
    assert(before_mask == NULL);
    ReadNewAfterPublication();

    Init(1000U);
    now_ms = 10U;
    FeedFrame();
    while_masked = ReadNewAfterPublication;
    for (unsigned i = 0; i < REMOTE_IBUS_CHANNEL_COUNT; ++i) channels[i] = 2000U;
    now_ms = 20U;
    FeedFrame();
    assert(while_masked == NULL && pending == NULL);
    assert(irq_mask == 0U && critical_depth == 0U);
}

static void ExpireOldWhileCommPreparesFrame(void)
{
    RemoteCommand_t command;
    now_ms = 110U;
    RemoteBackend_Advance(now_ms, (RemoteSafetyInput_t){0});
    assert(!RemoteBackend_GetCommand(&command));
    assert(command.frame_sequence == 1U && command.timeout_sequence == 0U);
    assert(command.timeout_latched && !command.run_permitted);
}

static void TestTimeoutCannotOverwritePreparedFrame(void)
{
    RemoteCommand_t command;
    Init(1000U);
    now_ms = 10U;
    FeedFrame();
    /* Comm prepares at 109ms, then Motion preempts at the old frame's deadline. */
    now_ms = 109U;
    channels[4] = 2000U;
    before_mask = ExpireOldWhileCommPreparesFrame;
    FeedFrame();
    assert(before_mask == NULL);
    assert(RemoteBackend_GetCommand(&command));
    assert(command.frame_sequence == 2U && command.last_valid_ms == 109U);
    assert(command.run_enable_raw == 2000U);
    assert(command.timeout_sequence == 0U && command.timeout_latched);
    assert(!command.run_permitted);
    /* A later real timeout belongs to Comm and must not lose its frame count. */
    now_ms = 209U;
    RemoteBackend_Process();
    assert(!RemoteBackend_GetCommand(&command));
    assert(command.frame_sequence == 2U && command.timeout_sequence == 1U);
}

static void TestQueriesAreReadOnlyAndPreserveMask(void)
{
    RemoteIbusProbe_t probe;
    Init(1500U);
    unsigned calls = authorization_calls;
    assert(!RemoteBackend_IsConnected());
    assert(RemoteBackend_LastReceiveAgeMs() == UINT32_MAX);
    assert(!RemoteBackend_ReadIbusProbe(&probe));
    now_ms = 0U;
    FeedFrame();
    now_ms = 100U;
    irq_mask = 1U;
    assert(!RemoteBackend_IsConnected());
    assert(RemoteBackend_LastReceiveAgeMs() == 100U);
    assert(RemoteBackend_ReadIbusProbe(&probe));
    assert(!probe.link_fresh && probe.timeout_sequence == 0U);
    assert(irq_mask == 1U && critical_depth == 0U);
    irq_mask = 0U;
    assert(authorization_calls == calls);
}

static void TickFrames(unsigned count)
{
    for (unsigned i = 0U; i < count; ++i)
    {
        now_ms += 10U;
        FeedFrame();
        RemoteBackend_Advance(now_ms, (RemoteSafetyInput_t){0});
    }
}

static void Arm(void)
{
    RemoteCommand_t command;
    Init(1500U);
    now_ms = 0U;
    channels[4] = 1000U;
    channels[5] = 1000U;
    channels[6] = channels[7] = 2000U;
    channels[9] = 2000U;
    TickFrames(25U);
    channels[4] = 2000U;
    TickFrames(3U);
    channels[7] = 1000U;
    TickFrames(3U);
    channels[7] = 2000U;
    TickFrames(25U);
#if MOWER_BLADE_OK_ENABLED
    channels[3] = 1000U;
    TickFrames(1U);
    assert(RemoteBackend_ConfirmUnlock());
#endif
    channels[1] = 2012U;
    TickFrames(1U);
    assert(RemoteBackend_GetCommand(&command));
    assert(command.run_permitted && command.throttle == 4095U);
}

static void TestRunningTimeoutAndReauthorization(void)
{
    RemoteCommand_t command;
    Arm();
    now_ms += 99U;
    assert(RemoteBackend_GetCommand(&command) && command.run_permitted);
    now_ms++;
    assert(!RemoteBackend_ConfirmUnlock());
    assert(!RemoteBackend_GetCommand(&command));
    assert(!command.run_permitted && command.throttle == 2048U);
    assert(command.travel_speed_mps == 0.0f && command.timeout_sequence == 0U);
    assert(command.recoverable_latch_reason == REMOTE_LATCH_REASON_TIMEOUT);
    /* Comm resumes directly with data: pre-feed timeout must reset stability. */
    FeedFrame();
    assert(RemoteBackend_GetCommand(&command));
    assert(command.timeout_sequence == 1U && !command.stable_online);
    TickFrames(25U);
    assert(RemoteBackend_GetCommand(&command));
    assert(command.stable_online && !command.run_permitted);
    assert(!RemoteBackend_ConfirmUnlock());
}

static void TestRejectedFrameAndStreamResetWhileRunning(void)
{
    RemoteCommand_t command;
    Arm();
    channels[8] = 1255U; /* A normal height change must not revoke permission. */
    TickFrames(1U);
    assert(RemoteBackend_GetCommand(&command) && command.run_permitted);
    const uint32_t frame_sequence = command.frame_sequence;
    const uint32_t last_valid_ms = command.last_valid_ms;
    const uint32_t invalidations = command.command_invalidation_sequence;
    RemoteRxDiagnostics_t rx_before, rx_after;
    RemoteBackend_GetRxDiagnostics(&rx_before);

    now_ms += 10U;
    FeedFrameWithChecksum(true);
    RemoteBackend_Advance(now_ms, (RemoteSafetyInput_t){0});
    assert(RemoteBackend_GetCommand(&command) && command.run_permitted);
    assert(command.frame_sequence == frame_sequence);
    assert(command.last_valid_ms == last_valid_ms);
    RemoteBackend_GetRxDiagnostics(&rx_after);
    assert(rx_after.crc_errors == rx_before.crc_errors + 1U);
    TickFrames(1U);
    assert(RemoteBackend_GetCommand(&command));
    assert(command.stable_online && command.run_permitted);
    assert(command.command_invalidation_sequence == invalidations);

    /* A UART discontinuity discards partial bytes, not established admission. */
    RemoteBackend_OnUartError(1U);
    TickFrames(1U);
    assert(RemoteBackend_GetCommand(&command));
    assert(command.stable_online && command.run_permitted);
    assert(command.command_invalidation_sequence == invalidations);
    RemoteBackend_GetRxDiagnostics(&rx_after);
    assert(rx_after.uart_errors == rx_before.uart_errors + 1U);
    channels[8]=4000U; /* Valid checksum, invalid mapped height channel. */
    now_ms+=10U;
    FeedFrameWithChecksum(false);
    RemoteBackend_GetRxDiagnostics(&rx_after);
    assert(rx_after.invalid_channel_frames == rx_before.invalid_channel_frames + 1U);
}

static void TestRejectedFramesCannotExtendRunningFreshness(void)
{
    RemoteCommand_t command;
    Arm();
    assert(RemoteBackend_GetCommand(&command));
    const uint32_t last_valid_ms = command.last_valid_ms;
    now_ms += 99U;
    FeedFrameWithChecksum(true);
    assert(RemoteBackend_GetCommand(&command) && command.run_permitted);
    assert(command.last_valid_ms == last_valid_ms);
    now_ms++;
    FeedFrameWithChecksum(true);
    assert(!RemoteBackend_GetCommand(&command));
    assert(!command.run_permitted && !command.stable_online);
    assert(command.last_valid_ms == last_valid_ms);
    assert(command.recoverable_latch_reason == REMOTE_LATCH_REASON_TIMEOUT);
    FeedFrame();
    assert(RemoteBackend_GetCommand(&command) && !command.stable_online);
    TickFrames(25U);
    assert(RemoteBackend_GetCommand(&command));
    assert(command.stable_online && !command.run_permitted);
    assert(!RemoteBackend_ConfirmUnlock());
}

#if MOWER_BLADE_OK_ENABLED
static void ReadArmedBeforeCh5Publication(void)
{
    RemoteCommand_t command;
    assert(RemoteBackend_GetCommand(&command) && command.run_permitted);
    assert(command.run_enable_raw == 2000U);
}

static void TestCh5EventsSurviveOverwrittenPublications(void)
{
    RemoteCommand_t command;
    Arm();
    channels[4] = 1000U;
    now_ms += 10U;
    before_mask = ReadArmedBeforeCh5Publication;
    FeedFrame();
    assert(before_mask == NULL);
    /* Motion misses the low snapshot; the following high snapshot must carry
     * cumulative CH5 changes so the running authorization is still revoked. */
    channels[4] = 2000U;
    now_ms += 10U;
    FeedFrame();
    assert(RemoteBackend_GetCommand(&command));
    assert(!command.run_permitted);
    assert(command.recoverable_latch_reason == REMOTE_LATCH_REASON_REMOTE_STOP);
}
#endif

int main(void)
{
    TestMotionTimeoutDoesNotWriteCommState(10U);
    TestMotionTimeoutDoesNotWriteCommState(UINT32_MAX - 50U);
    TestPublicationPreemption();
    TestTimeoutCannotOverwritePreparedFrame();
    TestQueriesAreReadOnlyAndPreserveMask();
    TestRunningTimeoutAndReauthorization();
    TestRejectedFrameAndStreamResetWhileRunning();
    TestRejectedFramesCannotExtendRunningFreshness();
#if MOWER_BLADE_OK_ENABLED
    TestCh5EventsSurviveOverwrittenPublications();
#endif
    assert(entries > 0U && barriers == 2U * entries);
    puts("PASS i-BUS ownership, timeout, publication preemption and IRQ restore");
    return 0;
}
