/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _FLYSKY_IBUS_H
#define _FLYSKY_IBUS_H

#include <stdint.h>
#if defined(PCBI6X_SENS_HIL_DIAG)
struct FlySkySensHilDiagnostics {
  uint32_t rfRxTotal;
  uint32_t rfTypeAa;
  uint32_t rfTypeAc;
  uint32_t rfOtherType;
  uint32_t txIdMatch;
  uint32_t txIdMismatch;
  uint32_t rxIdMatch;
  uint32_t rxIdMismatch;
  uint32_t pendingSet;
  uint32_t telemetryWakeup;
  uint32_t flyskyFrameProcessed;
  uint32_t aaProcessed;
  uint32_t acProcessed;
  uint32_t sensorSlotValid;
  uint32_t setTelemetryValueCalled;
  uint32_t newSensorCreated;
  uint32_t e1Count;          // CRC-valid RF packets with matching bind IDs
  uint32_t e1FirstTick;      // 10ms ticks since transmitter boot
  uint32_t e1SecondTick;
  uint16_t e1FirstRaw;       // wire order: low byte first
  uint16_t e1SecondRaw;
  uint8_t e1FirstType;
  uint8_t e1SecondType;
  uint8_t e1FirstInstance;
  uint8_t e1SecondInstance;
  uint32_t e1Parsed;
  uint32_t e1Set;
  uint32_t e1Matched;
  uint32_t e1DiscoveryOff;
  uint32_t e1Full;
  uint32_t e1Created;
  uint8_t lastPacketType;
  uint8_t lastTxId[4];
  uint8_t lastRxId[4];
  uint8_t expectedTxId[4];
  uint8_t expectedRxId[4];
};

extern volatile FlySkySensHilDiagnostics flySkySensHilDiagnostics;
void resetFlySkySensHilDiagnostics();
#endif

#if defined(MULTIMODULE)
void processFlySkyTelemetryData(uint8_t data);
#endif

#if defined(AFHDS2A)
void processFlySkyTelemetryFrame(uint8_t * frame);
#endif

void flySkySetDefault(int index, uint16_t id, uint8_t subId, uint8_t instance);

// Used by multi protocol
void processFlySkyPacket(const uint8_t * packet);

void processFlySkyPacketAC(const uint8_t * packet);

#endif
