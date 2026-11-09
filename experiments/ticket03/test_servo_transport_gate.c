#include "exp3_board.h"
#include "flysky_ibus_servo.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned transport_frames;

void exp3_board_channels(uint32_t now, uint16_t ch4, uint16_t ch11, uint16_t ch12)
{
    (void)now;
    (void)ch4;
    (void)ch11;
    (void)ch12;
    ++transport_frames;
}

static void make_frame(uint8_t frame[REMOTE_IBUS_FRAME_SIZE], const uint16_t *channels)
{
    uint16_t sum = 0U;
    unsigned i;
    frame[0] = 0x20U;
    frame[1] = 0x40U;
    for (i = 0U; i < REMOTE_IBUS_CHANNEL_COUNT; ++i) {
        frame[2U + 2U * i] = (uint8_t)channels[i];
        frame[3U + 2U * i] = (uint8_t)(channels[i] >> 8);
    }
    for (i = 0U; i < 30U; ++i)
        sum = (uint16_t)(sum + frame[i]);
    sum = (uint16_t)(0xffffU - sum);
    frame[30] = (uint8_t)sum;
    frame[31] = (uint8_t)(sum >> 8);
}

int main(void)
{
    FlySkyIbusServo_t ibus;
    FlySkyIbusConfig_t config = FlySkyIbusServo_DefaultConfig();
    uint16_t channels[REMOTE_IBUS_CHANNEL_COUNT];
    uint8_t frame[REMOTE_IBUS_FRAME_SIZE];
    unsigned i;

    memset(&ibus, 0, sizeof(ibus));
    for (i = 0U; i < REMOTE_IBUS_CHANNEL_COUNT; ++i)
        channels[i] = 1500U;
    channels[4] = 2000U;
    config.blade_start_semantics_configured = true;
    FlySkyIbusServo_Init(&ibus, &config);
    make_frame(frame, channels);
    FlySkyIbusServo_Feed(&ibus, frame, sizeof(frame), 1U);
    assert(transport_frames == 1U);

    channels[3] = 3000U;
    make_frame(frame, channels);
    FlySkyIbusServo_Feed(&ibus, frame, sizeof(frame), 2U);
    assert(transport_frames == 1U);
    puts("PASS invalid mapped safety frame cannot advance Experiment3 transport");
    return 0;
}
