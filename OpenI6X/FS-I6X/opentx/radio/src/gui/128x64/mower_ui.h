#pragma once

#if defined(PCBI6X) && defined(PCBI6X_MOWER_UI_SHELL)
#include <stdint.h>

enum MowerUiField : uint16_t {
  MOWER_UI_SOC = 1U << 0,
  MOWER_UI_VOLTAGE = 1U << 1,
  MOWER_UI_WALK_SET = 1U << 2,
  MOWER_UI_WALK_ACTUAL = 1U << 3,
  MOWER_UI_BLADE_SET = 1U << 4,
  MOWER_UI_BLADE_RPM = 1U << 5,
  MOWER_UI_HEIGHT_TARGET = 1U << 6,
  MOWER_UI_HEIGHT_ACTION = 1U << 7,
  MOWER_UI_WORK_MODE = 1U << 8,
  MOWER_UI_RUN_PERMISSION = 1U << 9,
  MOWER_UI_FAULT = 1U << 10,
  MOWER_UI_BAG_CONNECTION = 1U << 11
};

// Read-only presentation snapshot. Each bit says that its value was observed;
// absent or stale values display as unknown, never as a running/allowed state.
struct MowerUiSnapshot {
  uint16_t valid;
  uint16_t soc_percent;
  uint16_t voltage_centivolts;
  uint16_t walk_set_cms;
  uint16_t walk_actual_cms;
  uint16_t blade_set_rpm;
  uint16_t blade_actual_rpm;
  uint16_t height_target;
  uint16_t height_action;
  uint16_t work_mode;
  uint16_t run_permission;
  uint16_t fault_bits;
  uint16_t radio_battery_100mv;
  uint8_t radio_quality;
  bool telemetry_online;
  bool mock;
  bool bag_connected;    // PD10 feedback, guarded by MOWER_UI_BAG_CONNECTION
  // Local transmitted settings only; these do not imply controller acceptance.
  uint8_t height_setting; // 1..5, zero is unknown
  uint8_t bag_command;    // 1=open, 2=close, zero is unknown
};

// Match the existing chassis CH9 decoder without modifying its contract.
static inline uint8_t mowerUiHeightSetting(int32_t pulse, uint8_t current)
{
  if (pulse < 900 || pulse > 2100) return 0;
  if (current < 1 || current > 5) {
    return pulse < 1200 ? 1 : pulse < 1400 ? 2 :
        pulse < 1600 ? 3 : pulse < 1800 ? 4 : 5;
  }
  if (pulse < 1175) return 1;
  if (current > 2 && pulse < 1375) return 2;
  if (current > 3 && pulse < 1575) return 3;
  if (current > 4 && pulse < 1775) return 4;
  if (pulse > 1825) return 5;
  if (current < 4 && pulse > 1625) return 4;
  if (current < 3 && pulse > 1425) return 3;
  if (current < 2 && pulse > 1225) return 2;
  return current;
}

static inline uint8_t mowerUiBagCommand(int32_t pulse)
{
  return pulse >= 900 && pulse <= 1120 ? 1 :
      pulse >= 1880 && pulse <= 2100 ? 2 : 0;
}

// Provider may be substituted for a display demo; it cannot authorize a command.
typedef void (*MowerUiProvider)(MowerUiSnapshot &snapshot);
void mowerUiReadLive(MowerUiSnapshot &snapshot);
void mowerUiSetProvider(MowerUiProvider provider); // nullptr restores live provider
#endif
