#ifndef BYTE_ORDER_H
#define BYTE_ORDER_H

#include <stdint.h>

/* Big-endian (network order) field helpers for CAN payloads. They work on
 * byte arrays only, so the result never depends on host endianness or on
 * signed right shifts. Signed values are passed through their two's
 * complement bit pattern, e.g. ByteOrder_PutU16BE(buf, (uint16_t)value). */
static inline void ByteOrder_PutU16BE(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static inline void ByteOrder_PutU32BE(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static inline uint16_t ByteOrder_GetU16BE(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static inline uint32_t ByteOrder_GetU32BE(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

#endif
