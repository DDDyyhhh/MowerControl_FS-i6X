#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "flipsky_can.h"
#include "can_tx_scheduler.h"
#include "telemetry_snapshot.h"

static uint32_t g_now_ms;
static uint32_t g_tx_count;
static uint32_t g_tx_id;
static uint8_t g_tx_data[8];
static uint8_t g_tx_len;
static uint8_t g_tx_remote;

uint32_t HAL_GetTick(void)
{
    return g_now_ms;
}

uint8_t CAN_Enqueue_BladeControl_Message(uint32_t id, const uint8_t *data, uint8_t len,
                                         bool is_stop)
{
    assert(len <= sizeof(g_tx_data));
    g_tx_count++;
    g_tx_id = id;
    memcpy(g_tx_data, data, len);
    g_tx_len = len;
    assert(is_stop == ((data[0] | data[1] | data[2] | data[3]) == 0U));
    g_tx_remote = 0U;
    return 1U;
}

static void TestErpmTxCommands(void)
{
    static const float speeds[] = {0.0f, 14000.0f, 15500.0f, 16000.0f};
    static const uint8_t expected[][5] = {
        {0x00U, 0x00U, 0x00U, 0x00U, 0x00U},
        {0x00U, 0x00U, 0x36U, 0xB0U, 0x00U},
        {0x00U, 0x00U, 0x3CU, 0x8CU, 0x00U},
        {0x00U, 0x00U, 0x3EU, 0x80U, 0x00U}
    };

    for (size_t i = 0U; i < sizeof(speeds) / sizeof(speeds[0]); ++i)
    {
        assert(Flipsky_CAN_SetSpeedErpm(speeds[i]) == 1U);
        assert(g_tx_count == i + 1U);
        assert(g_tx_id == 0x00009A18U);
        assert(g_tx_len == 5U);
        assert(g_tx_remote == 0U);
        assert(memcmp(g_tx_data, expected[i], 5U) == 0);
    }

    assert(Flipsky_CAN_SetSpeedErpm(-1.0f) == 0U);
    assert(Flipsky_CAN_SetSpeedErpm(NAN) == 0U);
    assert(Flipsky_CAN_SetSpeedErpm(INFINITY) == 0U);
    assert(g_tx_count == 4U);
}

static uint16_t Crc16Modbus(const uint8_t *data, uint32_t length)
{
    /* Independent bitwise oracle; do not copy the production lookup table. */
    uint16_t crc = 0xFFFFU;

    while (length-- != 0U)
    {
        crc ^= *data++;
        for (uint8_t bit = 0U; bit < 8U; ++bit)
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1U) ? 0xA001U : 0U));
    }
    return crc;
}

static void TestProtocolCrcVectors(void)
{
    /* Frozen using FTESC V1.4 section 2.3's full 256-entry reference tables. */
    const uint8_t frame[8] = {0x00, 0x00, 0x3E, 0x80, 0x01, 0xF4, 0x0C, 0x0C};
    FlipskyCanFeedback_t feedback;
    assert(Crc16Modbus((const uint8_t *)"123456789", 9U) == 0x4B37U);
    assert(Crc16Modbus(frame, 6U) == 0x0C0CU);
    g_now_ms = 100U;
    Flipsky_CAN_ProcessRx(((uint32_t)FLIPSKY_ESC_ID << 8) |
                          FLIPSKY_CAN_CMD_REALTIME_1, frame, sizeof(frame));
    assert(Flipsky_CAN_ReadFeedback(&feedback));
    assert(feedback.crc_error_active == 0U && feedback.erpm_valid == 1U);
    assert(feedback.erpm == 16000 && feedback.erpm_last_rx_tick == 100U);
    assert(fabsf(feedback.duty - 0.5f) < 0.0001f);
    puts("[PASS] FTESC V1.4 frozen CRC vector accepted by production parser");
}

static void WriteI32BE(uint8_t *data, int32_t value)
{
    data[0] = (uint8_t)((uint32_t)value >> 24);
    data[1] = (uint8_t)((uint32_t)value >> 16);
    data[2] = (uint8_t)((uint32_t)value >> 8);
    data[3] = (uint8_t)value;
}

static void WriteI16BE(uint8_t *data, int16_t value)
{
    data[0] = (uint8_t)((uint16_t)value >> 8);
    data[1] = (uint8_t)value;
}

static void FinishFrame(uint8_t data[8])
{
    uint16_t crc = Crc16Modbus(data, 6U);

    data[6] = (uint8_t)(crc >> 8);
    data[7] = (uint8_t)crc;
}

static uint32_t Realtime1Id(void)
{
    return ((uint32_t)FLIPSKY_ESC_ID << 8) |
           FLIPSKY_CAN_CMD_REALTIME_1;
}

static void TestRealtime1Decoding(void)
{
    uint8_t frame[8] = {0};
    FlipskyCanFeedback_t feedback;
    const FlipskyCanStats_t *stats;

    WriteI32BE(frame, -16000);
    WriteI16BE(frame + 4, 875);
    FinishFrame(frame);

    g_now_ms = 1234U;
    Flipsky_CAN_ProcessRx(Realtime1Id(), frame, sizeof(frame));
    assert(Flipsky_CAN_ReadFeedback(&feedback));
    stats = Flipsky_CAN_GetStats();

    assert(feedback.online == 1U);
    assert(feedback.crc_valid == 1U);
    assert(feedback.crc_error_active == 0U);
    assert(feedback.erpm_valid == 1U);
    assert(feedback.erpm == -16000);
    assert(feedback.erpm_last_rx_tick == 1234U);
    assert(fabsf(feedback.duty - 0.875f) < 0.0001f);
    assert(stats->rx_realtime_1_count == 1U);
}

static void TestCrcFailurePreservesLastGoodErpm(void)
{
    uint8_t frame[8] = {0};
    FlipskyCanFeedback_t feedback;
    const FlipskyCanStats_t *stats;

    WriteI32BE(frame, 14000);
    WriteI16BE(frame + 4, 500);
    FinishFrame(frame);
    frame[7] ^= 0x01U;

    g_now_ms = 1300U;
    Flipsky_CAN_ProcessRx(Realtime1Id(), frame, sizeof(frame));
    assert(Flipsky_CAN_ReadFeedback(&feedback));
    stats = Flipsky_CAN_GetStats();

    assert(feedback.crc_valid == 0U);
    assert(feedback.crc_error_active == 1U);
    assert(feedback.erpm == -16000);
    assert(feedback.erpm_last_rx_tick == 1234U);
    assert(stats->rx_crc_error_count == 1U);
    assert(stats->rx_realtime_1_count == 1U);
}

static void TestUnrelatedExtendedFrameCannotClearCrcFault(void)
{
    uint8_t frame[8] = {0};
    FlipskyCanFeedback_t feedback;

    FinishFrame(frame);
    g_now_ms = 1400U;
    Flipsky_CAN_ProcessRx(((uint32_t)FLIPSKY_ESC_ID << 8) | 0x55U,
                          frame, sizeof(frame));
    assert(Flipsky_CAN_ReadFeedback(&feedback));

    assert(feedback.crc_error_active == 1U);
    assert(feedback.last_rx_tick == 1234U);
}

static void TestOnlineTimeoutWraparound(void)
{
    uint8_t frame[8] = {0};

    WriteI32BE(frame, 15500);
    WriteI16BE(frame + 4, 1000);
    FinishFrame(frame);

    g_now_ms = UINT32_MAX - 100U;
    Flipsky_CAN_ProcessRx(Realtime1Id(), frame, sizeof(frame));
    g_now_ms = 399U;
    assert(Flipsky_CAN_IsOnline() == 1U);
    g_now_ms = 400U;
    assert(Flipsky_CAN_IsOnline() == 0U);
}

static void TestOtherFeedbackAndCrcRecovery(void)
{
    uint8_t frame[8] = {0};
    FlipskyCanFeedback_t feedback;
    Flipsky_CAN_Init();
    frame[7] = 1U; /* Invalid CRC: keep it active through unrelated/short packets. */
    Flipsky_CAN_ProcessRx(Realtime1Id(), frame, sizeof(frame));
    assert(Flipsky_CAN_ReadFeedback(&feedback) && feedback.crc_error_active);
    FinishFrame(frame);
    Flipsky_CAN_ProcessRx(((uint32_t)(FLIPSKY_ESC_ID + 1U) << 8) |
                          FLIPSKY_CAN_CMD_REALTIME_1, frame, sizeof(frame));
    Flipsky_CAN_ProcessRx(Realtime1Id(), frame, 7U);
    assert(Flipsky_CAN_ReadFeedback(&feedback) && feedback.crc_error_active);
    g_now_ms = 200U;
    Flipsky_CAN_ProcessRx(((uint32_t)FLIPSKY_ESC_ID << 8) |
                          FLIPSKY_CAN_CMD_REALTIME_0, frame, sizeof(frame));
    assert(Flipsky_CAN_ReadFeedback(&feedback));
    assert(!feedback.crc_error_active && !feedback.erpm_valid);
    assert(feedback.last_rx_tick == 200U && feedback.erpm_last_rx_tick == 0U);
    WriteI16BE(frame, 5092); WriteI16BE(frame + 2, 2500); WriteI16BE(frame + 4, 3500);
    FinishFrame(frame);
    g_now_ms = 300U;
    Flipsky_CAN_ProcessRx(((uint32_t)FLIPSKY_ESC_ID << 8) |
                          FLIPSKY_CAN_CMD_REALTIME_2, frame, sizeof(frame));
    assert(Flipsky_CAN_ReadFeedback(&feedback));
    assert(!feedback.erpm_valid && feedback.erpm_last_rx_tick == 0U);
    assert(fabsf(feedback.battery_voltage_v - 50.92f) < 0.001f);
    puts("[PASS] Real CRC rejection/recovery, ID/DLC guards and per-ERPM freshness");
}

static void TestCrcTransmitCommands(void)
{
    assert(Flipsky_CAN_SetCurrentPercent(50.0f));
    assert(g_tx_id == (((uint32_t)FLIPSKY_ESC_ID << 8) | FLIPSKY_CAN_CMD_CURRENT_PERCENT));
    assert(g_tx_len == 6U && g_tx_remote == 0U);
    assert(g_tx_data[0] == 0U && g_tx_data[1] == 0U &&
           g_tx_data[2] == 0xC3U && g_tx_data[3] == 0x50U);
    assert((((uint16_t)g_tx_data[4] << 8) | g_tx_data[5]) == Crc16Modbus(g_tx_data, 4U));
    assert(Flipsky_CAN_SetDutyCycle(50.0f));
    assert(g_tx_id == (((uint32_t)FLIPSKY_ESC_ID << 8) | 0x02U));
    assert(g_tx_len == 7U && g_tx_data[4] == 0U);
    /* Preserve this legacy command's existing low/high placement. */
    assert((((uint16_t)g_tx_data[6] << 8) | g_tx_data[5]) == Crc16Modbus(g_tx_data, 5U));
    puts("[PASS] Shared transmit CRC corrected; existing command byte placement retained");
}

static void TestFeedbackSnapshotPipeline(void)
{
    uint8_t frame[8] = {0x00, 0x00, 0x3E, 0x80, 0x01, 0xF4, 0x0C, 0x0C};
    FlipskyCanFeedback_t feedback;
    TelemetrySources_t sources = {0};
    TelemetrySnapshot_t snapshot;
    Flipsky_CAN_Init();
    TelemetrySnapshot_Reset();
    g_now_ms = 100U;
    sources.bms.valid = true;
    sources.bms.last_good_ms = 100U;
    sources.bms.voltage_centivolts = 5092U;
    sources.bms.soc_percent = 63U;
    for (uint8_t i = 0U; i < 4U; ++i) {
        sources.wheels[i].valid = true;
        sources.wheels[i].last_good_ms = 100U;
    }
    sources.manual_remote_available = true;
    sources.final_manual_run_permitted = true;
    sources.blade.feedback_enabled = FLIPSKY_REALTIME_FEEDBACK_VALIDATED != 0U;
    assert(!sources.blade.feedback_enabled); /* Do not lift the hardware gate. */
    sources.blade.freshness_ms = FLIPSKY_CAN_RX_TIMEOUT_MS;
    for (unsigned step = 0; step < 3; ++step) {
        frame[7] = step == 1U ? 0x0DU : 0x0CU;
        Flipsky_CAN_ProcessRx(Realtime1Id(), frame, sizeof(frame));
        assert(Flipsky_CAN_ReadFeedback(&feedback));
        sources.blade.valid = feedback.erpm_valid != 0U;
        sources.blade.crc_error = feedback.crc_error_active != 0U;
        sources.blade.last_good_ms = feedback.erpm_last_rx_tick;
        sources.blade.actual_erpm = feedback.erpm;
        TelemetrySnapshot_Update(&sources, 100U);
        assert(TelemetrySnapshot_Read(&snapshot));
        assert(snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT] ==
               (step == 1U ? TELEMETRY_FAULT_FTESC_CRC : 0U));
        assert(snapshot.values[TELEMETRY_VALUE_BLADE_RPM] == 0U);
        assert(snapshot.values[TELEMETRY_VALUE_RUN_PERMISSION] == 1U);
    }
    puts("[PASS] Production CAN parser to fault snapshot: correct/corrupt/recovered frames");
}

int main(void)
{
    Flipsky_CAN_Init();
    TestProtocolCrcVectors();
    Flipsky_CAN_Init();
    TestErpmTxCommands();
    TestRealtime1Decoding();
    TestCrcFailurePreservesLastGoodErpm();
    TestUnrelatedExtendedFrameCannotClearCrcFault();
    TestOnlineTimeoutWraparound();
    TestOtherFeedbackAndCrcRecovery();
    TestCrcTransmitCommands();
    TestFeedbackSnapshotPipeline();
    return 0;
}
