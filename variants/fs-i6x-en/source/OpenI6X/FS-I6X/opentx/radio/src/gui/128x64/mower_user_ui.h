#pragma once

// English FS-i6X presentation only. No channel writes or wire changes.
#include "mower_zh_font.h"
#include "../../thirdparty/mini_print/mini_vsnprintf.h"

enum MowerUserPage { MOWER_USER_HOME, MOWER_USER_MAINTENANCE, MOWER_USER_DEBUG };
static MowerUserPage mowerUserPage = MOWER_USER_HOME;
static uint8_t mowerMaintenanceSelection;
static uint8_t mowerDebugFirstRow;
static const uint8_t MOWER_DEBUG_ROWS = 28;

static uint16_t mowerZhNext(const char *&text)
{
  const uint8_t first = static_cast<uint8_t>(*text++);
  uint16_t code = first;
  if (first >= 0xE0 && first <= 0xEF && text[0] && text[1]) {
    code = ((first & 15U) << 12) |
        ((static_cast<uint8_t>(text[0]) & 63U) << 6) |
        (static_cast<uint8_t>(text[1]) & 63U);
    text += 2;
  }
  return code;
}

static coord_t mowerZhTextWidth(const char *text)
{
  coord_t width=0;
  while (*text)
    width += mowerZhNext(text) < 128 ? MOWER_ZH_ASCII_WIDTH : MOWER_ZH_CJK_WIDTH;
  return width;
}

// 11px type, 12px rows; the generated metrics also describe bitmap packing.
static coord_t mowerZhText(coord_t x, coord_t y, const char *text, bool inverse)
{
  while (*text) {
    const uint16_t code = mowerZhNext(text);
    const uint8_t width = code < 128 ? MOWER_ZH_ASCII_WIDTH : MOWER_ZH_CJK_WIDTH;
    if (x < 0 || x + width > 128 || y < 0 || y + MOWER_ZH_HEIGHT > 64) break;
    unsigned lo=0, hi=sizeof(mowerZhGlyphs)/sizeof(mowerZhGlyphs[0]);
    while (lo < hi) {
      unsigned mid=(lo+hi)/2;
      if (mowerZhGlyphs[mid].code < code) lo=mid+1;
      else hi=mid;
    }
    if (inverse) lcdDrawSolidFilledRect(x, y, width, MOWER_ZH_HEIGHT, FORCE);
    if (lo < sizeof(mowerZhGlyphs)/sizeof(mowerZhGlyphs[0]) &&
        mowerZhGlyphs[lo].code == code) {
      const unsigned offset = lo < MOWER_ZH_ASCII_COUNT ? lo * MOWER_ZH_ASCII_BYTES :
          MOWER_ZH_ASCII_COUNT * MOWER_ZH_ASCII_BYTES +
          (lo - MOWER_ZH_ASCII_COUNT) * MOWER_ZH_CJK_BYTES;
      const uint8_t *bits = mowerZhBits + offset;
      const uint8_t bitmapHeight = code < 128 ? MOWER_ZH_HEIGHT : MOWER_ZH_CJK_BITMAP_HEIGHT;
      for (uint8_t row=0; row<bitmapHeight; ++row)
        for (uint8_t col=0; col<width; ++col) {
          const uint16_t bit=row*width+col;
          if (bits[bit/8] & (1U << (bit%8)))
            lcdDrawPoint(x+col, y+row, inverse ? ERASE : FORCE);
        }
    }
    x += width;
  }
  return x;
}

static void mowerZhCenteredText(coord_t y, const char *text)
{
  mowerZhText((128 - mowerZhTextWidth(text)) / 2, y, text);
}

static const char *mowerConnection(const MowerUiSnapshot &data)
{
  if (!data.telemetry_online) return "Offline";
  uint16_t causes = data.fault_bits;
  if (data.fast_status_known) {
    causes = ((data.fast_status & MOWER_FAST_COLLISION) >> 5) |
        ((data.fast_status & MOWER_FAST_EMERGENCY) >> 15) |
        ((data.fast_status & MOWER_FAST_LOCKED) << 6);
  }
  else if (!(data.valid & MOWER_UI_FAULT)) return "Unknown";
  if (causes & 0x0002U) return "Collision";
  if (causes & 0x0001U) return "E-STOP";
  if (causes & 0x0400U) return "Locked";
  if ((data.valid & MOWER_UI_FAULT) && !(data.fault_bits & 0x0007U)) {
    if (data.fault_bits & (MOWER_BATTERY_WARNING | MOWER_BATTERY_BLADE_INHIBIT)) return "Charge";
  }
  if (data.fast_status_known) {
    return (data.fast_status & MOWER_FAST_FAULT) ? "Fault" : "Online";
  }
  return (data.fault_bits & MOWER_STATUS_ALARM_MASK) ? "Fault" : "Online";
}

static void mowerUserValue(coord_t x, coord_t y, unsigned value, bool valid,
                           const char *unit="", uint8_t decimals=0)
{
  char text[20];
  if (!valid) mini_snprintf(text, sizeof(text), "--%s", unit);
  else if (decimals == 3)
    mini_snprintf(text, sizeof(text), "%d.%03d%s", int(value/1000), int(value%1000), unit);
  else if (decimals == 2)
    mini_snprintf(text, sizeof(text), "%d.%02d%s", int(value/100), int(value%100), unit);
  else if (decimals == 1)
    mini_snprintf(text, sizeof(text), "%d.%d%s", int(value/10), int(value%10), unit);
  else mini_snprintf(text, sizeof(text), "%d%s", int(value), unit);
  mowerZhText(x, y, text);
}

static const char *mowerBladeGear(bool known, uint16_t runtime)
{
  if (!known) return "--";
  if (!(runtime & 1U)) return "OFF";
  const uint8_t gear=(runtime>>1)&3U;
  return gear == 1 ? "LOW" : gear == 2 ? "MID" : gear == 3 ? "HIGH" : "--";
}

static const char *mowerBagCommandName(uint8_t command)
{
  return command == 1 ? "OPEN" : command == 2 ? "CLOSE" : "--";
}

static const char *mowerBagDisplay(const MowerUiSnapshot &data)
{
  if (!data.telemetry_online || !(data.valid & MOWER_UI_BAG_CONNECTION)) return "Bag ?";
  if (!data.bag_connected) return "No bag";
  return data.bag_command == 1 ? "Bag opn" : data.bag_command == 2 ? "Bag cls" : "Bag --";
}

static bool mowerBagFullKnown(const MowerUiSnapshot &data)
{
  return data.telemetry_online && (data.valid & MOWER_UI_BAG_CONNECTION) &&
      data.bag_connected && (data.valid & MOWER_UI_BAG_FULL);
}

static const char *mowerTransactionName(Exp3UiState state)
{
  switch (state) {
    case EXP3_UI_IDLE: return "Idle";
    case EXP3_UI_PROMPT: return "Pending";
    case EXP3_UI_WAIT: return "Wait ACK";
    case EXP3_UI_CANCEL_WAIT: return "Wait stop";
    case EXP3_UI_ACCEPTED: return "Accepted";
    case EXP3_UI_TIMEOUT: return "No reply";
    case EXP3_UI_UNBOUND: return "Not ready";
    default: return "Rejected";
  }
}

static void mowerUserHome(const MowerUiSnapshot &data, bool known, uint16_t runtime)
{
  mowerZhText(0, 0, mowerConnection(data), data.telemetry_online &&
      (data.valid & MOWER_UI_FAULT) && (data.fault_bits & MOWER_STATUS_ALARM_MASK));
  mowerZhText(66, 0, "RF");
  mowerUserValue(84, 0, data.radio_quality, data.telemetry_online);
  const unsigned quality = data.radio_quality > 100 ? 100 : data.radio_quality;
  for (uint8_t bar=0; bar<4; ++bar) {
    const uint8_t height=3+bar*2;
    if (data.telemetry_online && quality >= (bar+1)*25U)
      lcdDrawSolidFilledRect(111+bar*4, 11-height, 3, height, FORCE);
  }
  mowerZhText(0, 12, "CAR");
  mowerUserValue(24, 12, data.soc_percent,
      data.telemetry_online && (data.valid & MOWER_UI_SOC), "%");
  mowerZhText(54, 12, "TX");
  mowerUserValue(72, 12, data.radio_battery_100mv, true, "V", 1);
  lcdDrawRect(104, 14, 18, 8);
  lcdDrawSolidFilledRect(122, 16, 2, 4, FORCE);
  const int low=g_eeGeneral.vBatMin+90;
  const int high=g_eeGeneral.vBatMax+120;
  const int voltage=data.radio_battery_100mv;
  const int fill=high > low ? limit<int>(0, (voltage-low)*14/(high-low), 14) : 0;
  if (fill) lcdDrawSolidFilledRect(106, 16, fill, 4, FORCE);
  mowerZhText(0, 24, "Speed");
  mowerUserValue(42, 24, mowerTravelSpeedCms(), true, "m/s", 2);
  mowerZhText(0, 36, "Blade");
  mowerZhText(42, 36, mowerBladeGear(known, runtime));
  const bool fullKnown=mowerBagFullKnown(data);
  mowerZhText(72, 36, !fullKnown ? "Full ?" :
      data.bag_full ? "Bag full" : "Bag OK", fullKnown && data.bag_full);
  mowerZhText(0, 48, "Height");
  mowerUserValue(42, 48, data.height_deci_cm,
      data.telemetry_online && (data.valid & MOWER_UI_HEIGHT_FEEDBACK), "cm", 1);
  mowerZhText(84, 48, mowerBagDisplay(data));
}

static void mowerUserDebugRow(uint8_t row, coord_t y, const MowerUiSnapshot &data,
                              bool known, uint16_t runtime)
{
  static const char *const labels[] = {
    "BMS SOC", "Voltage", "Speed set", "Speed act", "Blade RPM",
    "Blade gear", "Blade out", "Height", "Bag cmd", "Run allow",
    "Link", "Txn", "Fault hex", "Stop ACK", "Bag link"
  };
  static const char *const faults[] = {
    "E-STOP", "Collision", "Tilt", "Remote timeout", "Remote stop latch",
    "Ownership latch", "Battery feedback", "Blade stale", "Blade CRC", "System locked"
  };
  if (row >= 15 && row < 25) {
    const uint8_t index=row-15;
    const uint8_t bit=index + (index >= 7U); // skip diagnostic-only wheel expiry
    mowerZhText(0, y, faults[index]);
    mowerZhText(108, y, !(data.valid & MOWER_UI_FAULT) ? "--" :
        (data.fault_bits & (1U<<bit)) ? "!" : "-");
    return;
  }
  if (row < 15) mowerZhText(0, y, labels[row]);
  const coord_t x=66;
  switch (row) {
    case 0: mowerUserValue(66, y, data.soc_percent, data.valid & MOWER_UI_SOC, "%"); break;
    case 1: mowerUserValue(x, y, data.voltage_centivolts, data.valid & MOWER_UI_VOLTAGE, "V", 2); break;
    case 2: mowerUserValue(x, y, mowerTravelSpeedCms(), true, "m/s", 2); break;
    case 3: mowerUserValue(x, y, data.walk_actual_cms, data.valid & MOWER_UI_WALK_ACTUAL, "m/s", 2); break;
    case 4: mowerUserValue(x, y, data.blade_actual_rpm, data.valid & MOWER_UI_BLADE_RPM); break;
    case 5: mowerZhText(x, y, mowerBladeGear(known, runtime)); break;
    case 6: mowerZhText(x, y, !known ? "--" : (runtime&1U) ? "Enabled" : "Off"); break;
    case 7: mowerUserValue(x, y, data.height_deci_cm,
        data.telemetry_online && (data.valid & MOWER_UI_HEIGHT_FEEDBACK), "cm", 1); break;
    case 8: mowerZhText(x, y, mowerBagCommandName(data.bag_command)); break;
    case 9: mowerZhText(x, y, !(data.valid & MOWER_UI_RUN_PERMISSION) ? "--" : data.run_permission ? "Allowed" : "Denied"); break;
    case 10: mowerZhText(x, y, mowerConnection(data)); break;
    case 11: mowerZhText(x, y, mowerTransactionName(mowerExp3Probe.state)); break;
    case 12: {
      char bits[10];
      if (data.valid & MOWER_UI_FAULT)
        mini_snprintf(bits, sizeof(bits), "0x%04X", int(data.fault_bits));
      else mini_snprintf(bits, sizeof(bits), "--");
      mowerZhText(x, y, bits);
      break;
    }
    case 13: mowerZhText(x, y, mowerExp3CancelFailed ? "Failed" :
        mowerExp3Probe.cancel_pending || mowerExp3Probe.state == EXP3_UI_CANCEL_WAIT ? "Wait" : "Idle"); break;
    case 25:
      mowerZhText(0, y, "Min cell");
      mowerUserValue(x, y, data.battery_min_mv, data.valid & MOWER_UI_BATTERY_MIN, "V", 3);
      break;
    case 26: {
      mowerZhText(0, y, "Batt cause");
      const uint16_t reason=data.battery_reasons;
      const char *label=!(data.valid & MOWER_UI_BATTERY_REASON) ? "Unknown" :
          (reason & 1U) ? "Data stale" : (reason & 0x20U) ? "Low SOC" :
          (reason & 4U) ? "Low volt" : "None";
      mowerZhText(x, y, label);
      break;
    }
    case 27:
      mowerZhText(0, y, "Bag full");
      mowerZhText(x, y, !mowerBagFullKnown(data) ? "Unknown" : data.bag_full ? "Full" : "Not full");
      break;
    case 14: mowerZhText(x, y, !data.telemetry_online || !(data.valid & MOWER_UI_BAG_CONNECTION) ?
        "Unknown" : data.bag_connected ? "Online" : "Offline"); break;
  }
}

static void mowerUserRender()
{
  MowerUiSnapshot data;
  mowerUiRead(data);
  uint16_t runtime=0;
  const bool known=data.telemetry_online &&
      exp3_board_runtime(static_cast<uint32_t>(get_tmr10ms())*10U, &runtime);
  if (mowerUserPage == MOWER_USER_HOME) mowerUserHome(data, known, runtime);
  else if (mowerUserPage == MOWER_USER_MAINTENANCE) {
    mowerZhText(0, 0, "Maintenance");
    static const char *const entries[] = {"Debug info", "Radio setup", "Model setup"};
    for (uint8_t i=0; i<3; ++i)
      mowerZhText(6, 12+i*12, entries[i], i == mowerMaintenanceSelection);
    mowerZhText(0, 48, "EXIT: back");
  }
  else {
    mowerZhText(0, 0, "Debug info");
    char position[12];
    mini_snprintf(position, sizeof(position), "%d/%d", int(mowerDebugFirstRow+1), int(MOWER_DEBUG_ROWS));
    mowerZhText(90, 0, position);
    for (uint8_t i=0; i<4; ++i)
      mowerUserDebugRow(mowerDebugFirstRow+i, 12+i*12, data, known, runtime);
  }
  mowerExp3Overlay(mowerExp3DisplayState);
}

// All ordinary main-view routes terminate here in the production build.
static void mowerUserMain(event_t event)
{
  g_eeGeneral.view = VIEW_MOWER; // Never persist debug/maintenance as startup view.
  const bool home=mowerUserPage == MOWER_USER_HOME;
  mowerExp3DisplayState=mowerExp3Step(home ? event : 0);
  const bool modal=mowerExp3DisplayState == EXP3_UI_PROMPT;
  if (event == EVT_KEY_LONG(KEY_MENU) || event == EVT_KEY_LONG(KEY_RIGHT))
    killEvents(event); // MENU aliases ENTER; suppress the long-press release.
  if (modal) {
    // A prompt appearing during maintenance returns home without reusing the
    // menu-selection OK. EXIT/navigation never authorize or cancel a command.
    mowerUserPage=MOWER_USER_HOME;
  }
  else if (event == EVT_KEY_LONG(KEY_RIGHT)) {
    mowerUserPage=MOWER_USER_MAINTENANCE;
    mowerMaintenanceSelection=0;
  }
  else if (event == EVT_KEY_BREAK(KEY_EXIT)) {
    mowerUserPage=mowerUserPage == MOWER_USER_DEBUG ? MOWER_USER_MAINTENANCE : MOWER_USER_HOME;
  }
  else if (mowerUserPage == MOWER_USER_MAINTENANCE) {
    if (event == EVT_KEY_BREAK(KEY_UP) && mowerMaintenanceSelection) --mowerMaintenanceSelection;
    if (event == EVT_KEY_BREAK(KEY_DOWN) && mowerMaintenanceSelection < 2) ++mowerMaintenanceSelection;
    if (event == EVT_KEY_BREAK(KEY_ENTER)) {
      killEvents(event);
      if (mowerMaintenanceSelection == 0) {
        mowerUserPage=MOWER_USER_DEBUG;
        mowerDebugFirstRow=0;
      }
      else if (mowerMaintenanceSelection == 1) pushMenu(menuTabGeneral[0]);
      else pushMenu(menuModelSelect);
    }
  }
  else if (mowerUserPage == MOWER_USER_DEBUG) {
    if ((event == EVT_KEY_BREAK(KEY_UP) || event == EVT_KEY_REPT(KEY_UP)) && mowerDebugFirstRow)
      --mowerDebugFirstRow;
    if ((event == EVT_KEY_BREAK(KEY_DOWN) || event == EVT_KEY_REPT(KEY_DOWN)) &&
        mowerDebugFirstRow < MOWER_DEBUG_ROWS-4) ++mowerDebugFirstRow;
  }
  mowerUserRender();
}
