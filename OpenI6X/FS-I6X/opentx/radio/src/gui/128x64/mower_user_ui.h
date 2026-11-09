#pragma once

// Production FS-i6X presentation only. No channel writes or wire changes.
#include "mower_zh_font.h"
#include "../../thirdparty/mini_print/mini_vsnprintf.h"

enum MowerUserPage { MOWER_USER_HOME, MOWER_USER_MAINTENANCE, MOWER_USER_DEBUG };
static MowerUserPage mowerUserPage = MOWER_USER_HOME;
static uint8_t mowerMaintenanceSelection;
static uint8_t mowerDebugFirstRow;
static const uint8_t MOWER_DEBUG_ROWS = 25;

// Font: 12px Chinese, 6px ASCII; fixed subset in flash, no allocation.
static coord_t mowerZhText(coord_t x, coord_t y, const char *text, bool inverse)
{
  while (*text) {
    const uint8_t first = static_cast<uint8_t>(*text++);
    uint16_t code = first;
    if (first >= 0xE0 && first <= 0xEF && text[0] && text[1]) {
      code = ((first & 15U) << 12) |
          ((static_cast<uint8_t>(text[0]) & 63U) << 6) |
          (static_cast<uint8_t>(text[1]) & 63U);
      text += 2;
    }
    const uint8_t width = code < 128 ? 6 : 12;
    if (x + width > 128 || y < 0 || y + 12 > 64) break;
    unsigned lo=0, hi=sizeof(mowerZhGlyphs)/sizeof(mowerZhGlyphs[0]);
    while (lo < hi) {
      unsigned mid=(lo+hi)/2;
      if (mowerZhGlyphs[mid].code < code) lo=mid+1;
      else hi=mid;
    }
    if (inverse) lcdDrawSolidFilledRect(x, y, width, 12, FORCE);
    if (lo < sizeof(mowerZhGlyphs)/sizeof(mowerZhGlyphs[0]) &&
        mowerZhGlyphs[lo].code == code) {
      const uint8_t *bits = mowerZhBits + mowerZhGlyphs[lo].offset;
      for (uint8_t row=0; row<12; ++row)
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

static const char *mowerConnection(const MowerUiSnapshot &data)
{
  if (!data.telemetry_online) return "未连接";
  if (!(data.valid & MOWER_UI_FAULT)) return "状态未知";
  return data.fault_bits ? "报错" : "已连接";
}

static void mowerUserValue(coord_t x, coord_t y, unsigned value, bool valid,
                           const char *unit="", uint8_t decimals=0)
{
  char text[20];
  if (!valid) mini_snprintf(text, sizeof(text), "--%s", unit);
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
  if (!(runtime & 1U)) return "关";
  const uint8_t gear=(runtime>>1)&3U;
  return gear == 1 ? "低" : gear == 2 ? "中" : gear == 3 ? "高" : "--";
}

static const char *mowerBagCommandName(uint8_t command)
{
  return command == 1 ? "开" : command == 2 ? "关" : "--";
}

static const char *mowerBagDisplay(const MowerUiSnapshot &data)
{
  if (!data.telemetry_online || !(data.valid & MOWER_UI_BAG_CONNECTION)) return "草袋未知";
  if (!data.bag_connected) return "草袋未连接";
  return data.bag_command == 1 ? "草袋开" : data.bag_command == 2 ? "草袋关" : "草袋--";
}

static const char *mowerTransactionName(Exp3UiState state)
{
  switch (state) {
    case EXP3_UI_IDLE: return "空闲";
    case EXP3_UI_PROMPT: return "待确认";
    case EXP3_UI_WAIT: return "等待回执";
    case EXP3_UI_CANCEL_WAIT: return "等待取消";
    case EXP3_UI_ACCEPTED: return "已接受";
    case EXP3_UI_TIMEOUT: return "无回执";
    case EXP3_UI_UNBOUND: return "未就绪";
    default: return "已拒绝";
  }
}

static void mowerUserHome(const MowerUiSnapshot &data, bool known, uint16_t runtime)
{
  mowerZhText(0, 0, mowerConnection(data), data.telemetry_online &&
      (data.valid & MOWER_UI_FAULT) && data.fault_bits);
  mowerZhText(66, 0, "信");
  mowerUserValue(78, 0, data.radio_quality, data.telemetry_online);
  const unsigned quality = data.radio_quality > 100 ? 100 : data.radio_quality;
  for (uint8_t bar=0; bar<4; ++bar) {
    const uint8_t height=3+bar*2;
    if (data.telemetry_online && quality >= (bar+1)*25U)
      lcdDrawSolidFilledRect(111+bar*4, 11-height, 3, height, FORCE);
  }
  mowerZhText(0, 12, "车");
  mowerUserValue(12, 12, data.soc_percent,
      data.telemetry_online && (data.valid & MOWER_UI_SOC), "%");
  mowerZhText(48, 12, "遥");
  mowerUserValue(60, 12, data.radio_battery_100mv, true, "V", 1);
  lcdDrawRect(104, 14, 18, 8);
  lcdDrawSolidFilledRect(122, 16, 2, 4, FORCE);
  // Same battery display range as the existing radio configuration.
  const int low=g_eeGeneral.vBatMin+90;
  const int high=g_eeGeneral.vBatMax+120;
  const int voltage=data.radio_battery_100mv;
  const int fill=high > low ? limit<int>(0, (voltage-low)*14/(high-low), 14) : 0;
  if (fill) lcdDrawSolidFilledRect(106, 16, fill, 4, FORCE);
  uint16_t generation=0U, control=0U;
  bool controlKnown=exp3_board_control(static_cast<uint32_t>(get_tmr10ms())*10U,&generation,&control);
  mowerZhText(0, 24, !controlKnown ? "模式未知" :
      (control&MOWER_CONTROL_API_SELECTED) ? "API" : "遥控");
  mowerZhText(84, 24, !controlKnown ? "--" : (control&MOWER_CONTROL_ARMED) ? "解锁" : "锁定");
  mowerZhText(0, 36, "行走设");
  mowerUserValue(42, 36, mowerTravelSpeedCms(), true, "m/s", 2);
  mowerZhText(90,36,"刀");
  mowerZhText(102, 36, mowerBladeGear(known,runtime));
  mowerZhText(114,36,known && (runtime&1U) ? "开" : "");
  mowerZhText(0, 48, "高度设");
  mowerUserValue(36, 48, data.height_setting, data.height_setting != 0, "/5");
  mowerZhText(60, 48, mowerBagDisplay(data));
}

static void mowerUserDebugRow(uint8_t row, coord_t y, const MowerUiSnapshot &data,
                              bool known, uint16_t runtime)
{
  static const char *const labels[] = {
    "整车电量", "整车电压", "行走设定", "实际速度", "割刀转速",
    "割刀档位", "割刀输出", "高度设定", "草袋指令", "遥控许可",
    "链路状态", "确认事务", "故障字", "停刀回执", "草袋连接"
  };
  static const char *const faults[] = {
    "急停", "碰撞", "倾斜", "遥控超时", "遥控停机锁存",
    "控制权锁存", "电池反馈异常", "轮速反馈过期", "割刀反馈过期", "割刀校验异常"
  };
  if (row >= 15) {
    const uint8_t bit=row-15;
    mowerZhText(0, y, faults[bit]);
    mowerZhText(108, y, !(data.valid & MOWER_UI_FAULT) ? "--" :
        (data.fault_bits & (1U<<bit)) ? "!" : "-");
    return;
  }
  mowerZhText(0, y, labels[row]);
  const coord_t x=54;
  switch (row) {
    case 0: mowerUserValue(x, y, data.soc_percent, data.valid & MOWER_UI_SOC, "%"); break;
    case 1: mowerUserValue(x, y, data.voltage_centivolts, data.valid & MOWER_UI_VOLTAGE, "V", 2); break;
    case 2: mowerUserValue(x, y, mowerTravelSpeedCms(), true, "m/s", 2); break;
    case 3: mowerUserValue(x, y, data.walk_actual_cms, data.valid & MOWER_UI_WALK_ACTUAL, "m/s", 2); break;
    case 4: mowerUserValue(x, y, data.blade_actual_rpm, data.valid & MOWER_UI_BLADE_RPM); break;
    case 5: mowerZhText(x, y, mowerBladeGear(known, runtime)); break;
    case 6: mowerZhText(x, y, !known ? "--" : (runtime&1U) ? "使能" : "关闭"); break;
    case 7: mowerUserValue(x, y, data.height_setting, data.height_setting != 0, "/5"); break;
    case 8: mowerZhText(x, y, mowerBagCommandName(data.bag_command)); break;
    case 9: mowerZhText(x, y, !(data.valid & MOWER_UI_RUN_PERMISSION) ? "--" : data.run_permission ? "允许" : "禁止"); break;
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
    case 13: mowerZhText(x, y, mowerExp3CancelFailed ? "失败" :
        mowerExp3Probe.cancel_pending || mowerExp3Probe.state == EXP3_UI_CANCEL_WAIT ? "等待" : "空闲"); break;
    case 14: mowerZhText(x, y, !data.telemetry_online || !(data.valid & MOWER_UI_BAG_CONNECTION) ?
        "未知" : data.bag_connected ? "已连接" : "未连接"); break;
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
    mowerZhText(0, 0, "维护入口");
    static const char *const entries[] = {"调试信息", "系统设置", "模型设置"};
    for (uint8_t i=0; i<3; ++i)
      mowerZhText(6, 12+i*12, entries[i], i == mowerMaintenanceSelection);
    mowerZhText(0, 48, "退出返回");
  }
  else {
    mowerZhText(0, 0, "调试信息");
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
  mowerExp3DisplayState=mowerControlStep(home ? event : 0);
  const bool modal=mowerExp3DisplayState != EXP3_UI_IDLE &&
      mowerExp3DisplayState != EXP3_UI_ACCEPTED;
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
