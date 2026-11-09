#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sens_ibus_responder.h"

typedef struct
{
    TelemetrySnapshot_t snapshot;
    uint32_t read_count;
    uint8_t responses[32][SENS_IBUS_MAX_RESPONSE_SIZE];
    uint8_t lengths[32];
    uint8_t response_count;
    bool accept_send;
} HarnessContext_t;

static bool ReadSnapshot(void *context, TelemetrySnapshot_t *snapshot)
{
    HarnessContext_t *harness = (HarnessContext_t *)context;
    harness->read_count++;
    *snapshot = harness->snapshot;
    return true;
}

static bool CaptureResponse(void *context, const uint8_t *response, uint8_t length)
{
    HarnessContext_t *harness = (HarnessContext_t *)context;

    if (!harness->accept_send)
    {
        return false;
    }
    assert(harness->response_count < 32U);
    assert(length <= SENS_IBUS_MAX_RESPONSE_SIZE);
    memcpy(harness->responses[harness->response_count], response, length);
    harness->lengths[harness->response_count] = length;
    harness->response_count++;
    return true;
}

static uint16_t ExpectedChecksum(const uint8_t *bytes, uint8_t length_without_checksum)
{
    uint16_t checksum = 0xFFFFU;
    uint8_t i;

    for (i = 0U; i < length_without_checksum; i++)
    {
        checksum = (uint16_t)(checksum - bytes[i]);
    }
    return checksum;
}

static void BuildRequest(uint8_t command, uint8_t address, uint8_t request[4])
{
    uint16_t checksum;

    request[0] = 0x04U;
    request[1] = (uint8_t)(command | address);
    checksum = ExpectedChecksum(request, 2U);
    request[2] = (uint8_t)(checksum & 0xFFU);
    request[3] = (uint8_t)(checksum >> 8U);
}

static void AssertChecksum(const uint8_t *response, uint8_t length)
{
    uint16_t expected = ExpectedChecksum(response, (uint8_t)(length - 2U));
    assert(response[length - 2U] == (uint8_t)(expected & 0xFFU));
    assert(response[length - 1U] == (uint8_t)(expected >> 8U));
}

static SensIbusResponder_t NewResponder(HarnessContext_t *harness)
{
    SensIbusResponder_t responder;
    SensIbusResponder_Init(&responder, ReadSnapshot, harness,
                           CaptureResponse, harness);
    return responder;
}


static void TestSequentialDiscoveryAndFirstMissingAddress(void)
{
    HarnessContext_t harness = {0};
    SensIbusResponder_t responder;
    uint8_t stream[9U * 4U];
    uint8_t address;

    harness.accept_send = true;
    responder = NewResponder(&harness);
    for (address = 1U; address <= 9U; address++)
    {
        BuildRequest(SENS_IBUS_COMMAND_DISCOVERY, address,
                     &stream[(address - 1U) * 4U]);
    }

    SensIbusResponder_Feed(&responder, stream, sizeof(stream));

    assert(harness.response_count == 8U);
    assert(harness.read_count == 0U);
    for (address = 1U; address <= 8U; address++)
    {
        const uint8_t *response = harness.responses[address - 1U];
        assert(harness.lengths[address - 1U] == 4U);
        assert(response[0] == 0x04U);
        assert(response[1] == (uint8_t)(SENS_IBUS_COMMAND_DISCOVERY | address));
        AssertChecksum(response, 4U);
    }
}

static void TestSensorTypes(void)
{
    static const uint8_t expected_types[8] = {
        0x03U, 0x05U, 0x06U, 0x13U, 0x02U, 0x16U, 0x15U, 0xE0U
    };
    HarnessContext_t harness = {0};
    SensIbusResponder_t responder;
    uint8_t request[4];
    uint8_t address;

    harness.accept_send = true;
    responder = NewResponder(&harness);
    for (address = 1U; address <= 8U; address++)
    {
        BuildRequest(SENS_IBUS_COMMAND_TYPE, address, request);
        SensIbusResponder_Feed(&responder, request, sizeof(request));
    }

    assert(harness.response_count == 8U);
    assert(harness.read_count == 0U);
    for (address = 1U; address <= 8U; address++)
    {
        const uint8_t *response = harness.responses[address - 1U];
        assert(harness.lengths[address - 1U] == 6U);
        assert(response[0] == 0x06U);
        assert(response[1] == (uint8_t)(SENS_IBUS_COMMAND_TYPE | address));
        assert(response[2] == expected_types[address - 1U]);
        assert(response[3] == 0x02U);
        AssertChecksum(response, 6U);
    }
}

static void TestMeasurementsUseCompleteSnapshotAndLittleEndian(void)
{
    static const uint16_t values[8] = {
        0x0000U, 0x0001U, 0x1234U, 0x00FFU,
        0xFF00U, 0x7FFFU, 0x8000U, 0xFFFFU
    };
    HarnessContext_t harness = {0};
    SensIbusResponder_t responder;
    uint8_t request[4];
    uint8_t address;

    harness.accept_send = true;
    memcpy(harness.snapshot.values, values, sizeof(values));
    responder = NewResponder(&harness);

    for (address = 1U; address <= 8U; address++)
    {
        BuildRequest(SENS_IBUS_COMMAND_MEASUREMENT, address, request);
        SensIbusResponder_Feed(&responder, request, sizeof(request));
    }

    assert(harness.response_count == 8U);
    assert(harness.read_count == 8U);
    for (address = 1U; address <= 8U; address++)
    {
        const uint8_t *response = harness.responses[address - 1U];
        uint16_t value = values[address - 1U];
        assert(harness.lengths[address - 1U] == 6U);
        assert(response[0] == 0x06U);
        assert(response[1] == (uint8_t)(SENS_IBUS_COMMAND_MEASUREMENT | address));
        assert(response[2] == (uint8_t)(value & 0xFFU));
        assert(response[3] == (uint8_t)(value >> 8U));
        AssertChecksum(response, 6U);
    }
}

static void TestFragmentedAndBackToBackRequests(void)
{
    HarnessContext_t harness = {0};
    SensIbusResponder_t responder;
    uint8_t requests[8];

    harness.accept_send = true;
    responder = NewResponder(&harness);
    BuildRequest(SENS_IBUS_COMMAND_DISCOVERY, 1U, &requests[0]);
    BuildRequest(SENS_IBUS_COMMAND_TYPE, 2U, &requests[4]);

    SensIbusResponder_Feed(&responder, requests, 1U);
    assert(harness.response_count == 0U);
    SensIbusResponder_Feed(&responder, &requests[1], 2U);
    assert(harness.response_count == 0U);
    SensIbusResponder_Feed(&responder, &requests[3], 5U);
    assert(harness.response_count == 2U);
    assert(harness.responses[0][1] == 0x81U);
    assert(harness.responses[1][1] == 0x92U);
}

static void TestErrorsStaySilentAndRecover(void)
{
    HarnessContext_t harness = {0};
    SensIbusResponder_t responder;
    uint8_t stream[24];
    uint8_t valid[4];

    harness.accept_send = true;
    responder = NewResponder(&harness);

    BuildRequest(SENS_IBUS_COMMAND_DISCOVERY, 1U, valid);
    stream[0] = 0x55U;
    stream[1] = 0xAAU;
    BuildRequest(0xB0U, 1U, &stream[2]);
    BuildRequest(SENS_IBUS_COMMAND_DISCOVERY, 9U, &stream[6]);
    BuildRequest(SENS_IBUS_COMMAND_DISCOVERY, 2U, &stream[10]);
    stream[12] ^= 0x01U;
    stream[14] = 0x04U;
    stream[15] = 0x81U;
    memcpy(&stream[16], valid, sizeof(valid));
    BuildRequest(SENS_IBUS_COMMAND_MEASUREMENT, 8U, &stream[20]);

    SensIbusResponder_Feed(&responder, stream, sizeof(stream));

    assert(harness.response_count == 2U);
    assert(harness.responses[0][1] == 0x81U);
    assert(harness.responses[1][1] == 0xA8U);
}

static void TestRejectedSendDoesNotBreakParsing(void)
{
    HarnessContext_t harness = {0};
    SensIbusResponder_t responder;
    uint8_t request[4];

    responder = NewResponder(&harness);
    BuildRequest(SENS_IBUS_COMMAND_DISCOVERY, 1U, request);
    SensIbusResponder_Feed(&responder, request, sizeof(request));
    assert(harness.response_count == 0U);

    harness.accept_send = true;
    BuildRequest(SENS_IBUS_COMMAND_DISCOVERY, 2U, request);
    SensIbusResponder_Feed(&responder, request, sizeof(request));
    assert(harness.response_count == 1U);
    assert(harness.responses[0][1] == 0x82U);
}

int main(void)
{
    TestSequentialDiscoveryAndFirstMissingAddress();
    TestSensorTypes();
    TestMeasurementsUseCompleteSnapshotAndLittleEndian();
    TestFragmentedAndBackToBackRequests();
    TestErrorsStaySilentAndRecover();
    TestRejectedSendDoesNotBreakParsing();
    return 0;
}
