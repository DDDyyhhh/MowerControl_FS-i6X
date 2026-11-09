#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "flipsky_can.h"

static uint32_t g_now_ms;

uint32_t HAL_GetTick(void)
{
    return g_now_ms;
}

uint8_t CAN_Enqueue_Ext_Message(uint32_t id, uint8_t *data, uint8_t len,
                                uint8_t is_remote)
{
    (void)id;
    (void)data;
    (void)len;
    (void)is_remote;
    return 1U;
}

static uint16_t Crc16Modbus(const uint8_t *data, uint32_t length)
{
    static const uint16_t crc_table[16] = {
        0x0000U, 0xC0C1U, 0xC181U, 0x0140U,
        0xC301U, 0x03C0U, 0x0280U, 0xC241U,
        0xC601U, 0x06C0U, 0x0780U, 0xC741U,
        0x0500U, 0xC5C1U, 0xC481U, 0x0440U
    };
    uint16_t crc = 0xFFFFU;

    while (length-- != 0U)
    {
        crc ^= *data++;
        crc = (uint16_t)((crc >> 4) ^ crc_table[crc & 0x0FU]);
        crc = (uint16_t)((crc >> 4) ^ crc_table[crc & 0x0FU]);
    }
    return crc;
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

int main(void)
{
    Flipsky_CAN_Init();
    TestRealtime1Decoding();
    TestCrcFailurePreservesLastGoodErpm();
    TestUnrelatedExtendedFrameCannotClearCrcFault();
    TestOnlineTimeoutWraparound();
    return 0;
}
