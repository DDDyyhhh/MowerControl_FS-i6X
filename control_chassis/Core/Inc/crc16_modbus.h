#ifndef CRC16_MODBUS_H
#define CRC16_MODBUS_H

#include <stdint.h>

/* CRC-16/MODBUS: reflected polynomial 0xA001, initial value 0xFFFF, no final XOR.
 * Four bits per table lookup (two lookups per byte). The caller sends the
 * result low byte first, as Modbus RTU and the FTESC CAN frames require. */
static inline uint16_t Crc16_Modbus(const uint8_t *data, uint32_t length)
{
    static const uint16_t crc_table[16] = {
        0x0000U, 0xCC01U, 0xD801U, 0x1400U,
        0xF001U, 0x3C00U, 0x2800U, 0xE401U,
        0xA001U, 0x6C00U, 0x7800U, 0xB401U,
        0x5000U, 0x9C01U, 0x8801U, 0x4400U
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

#endif
