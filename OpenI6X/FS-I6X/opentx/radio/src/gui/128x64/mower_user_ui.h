#pragma once

// Production FS-i6X UI with optional ultrasonic controls.
#include "mower_zh_font.h"
#include "../../../../../../../experiments/ticket03/blade_gear_presets.h"

enum MowerUserPage { MOWER_USER_HOME, MOWER_USER_MAINTENANCE, MOWER_USER_DEBUG };
static MowerUserPage mowerUserPage = MOWER_USER_HOME;
static uint8_t mowerMaintenanceSelection;
static uint8_t mowerDebugFirstRow;
static const uint8_t MOWER_DEBUG_ROWS = 29;

static uint16_t mowerZhNext(const char *&text)
{
  const uint8_t first = static_cast<uint8_t>(*text++);
  uint16_t code = first;
  if (first >= 0xE0) {
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

// Chinese bitmaps share 12px rows with the native 5x7 ASCII font.
static coord_t mowerZhText(coord_t x, coord_t y, const char *text, bool inverse=false)
{
  const LcdFlags pixel = inverse ? ERASE : FORCE;
  if (inverse) lcdDrawSolidFilledRect(x, y, mowerZhTextWidth(text), MOWER_ZH_HEIGHT, FORCE);
  while (*text) {
    const uint16_t code = mowerZhNext(text);
    if (code < 128U) {
      // Inverse ASCII is used by the isolated unknown-gear field.
      lcdDrawChar(x, y+3, code, inverse ? INVERS : 0);
      x += MOWER_ZH_ASCII_WIDTH;
      continue;
    }

    unsigned lo=0, hi=sizeof(mowerZhGlyphs)/sizeof(mowerZhGlyphs[0]);
    while (lo < hi) {
      unsigned mid=(lo+hi)/2;
      if (mowerZhGlyphs[mid].code < code) lo=mid+1;
      else hi=mid;
    }
    unsigned bit = lo * MOWER_ZH_CJK_BITS;
    for (unsigned row=0; row<MOWER_ZH_CJK_BITMAP_HEIGHT; ++row)
      for (unsigned col=0; col<MOWER_ZH_CJK_WIDTH; ++col, ++bit)
        if (mowerZhBits[bit/8] & (1U << (bit%8))) lcdDrawPoint(x+col, y+row, pixel);
    x += MOWER_ZH_CJK_WIDTH;
  }
  return x;
}

static bool mowerApiMode(const MowerUiSnapshot &data)
{
  return (data.valid & MOWER_UI_WORK_MODE) && (data.work_mode == 2 || data.work_mode == 3);
}

static const char *mowerModeName(const MowerUiSnapshot &data)
{
  if (!data.telemetry_online || !(data.valid & MOWER_UI_WORK_MODE)) return "--";
  static const char *const labels[] = {"--", "遥控", "API待机", "API运行", "接管"};
  return labels[data.work_mode];
}

static const char *mowerConnection(const MowerUiSnapshot &data)
{
  if (!data.telemetry_online) return "未连接";
  uint16_t causes = data.fault_bits;
  if (data.fast_status_known) {
    causes = ((data.fast_status & MOWER_FAST_COLLISION) >> 5) |
        ((data.fast_status & MOWER_FAST_EMERGENCY) >> 15) |
        ((data.fast_status & MOWER_FAST_LOCKED) << 6);
  }
  else if (!(data.valid & MOWER_UI_FAULT)) return "未知";
  if (causes & 0x0002U) return "碰撞";
  if (causes & 0x0001U) return "急停";
  if (causes & 0x0400U) return "锁定";
  if ((data.valid & MOWER_UI_FAULT) && !(data.fault_bits & 0x0007U)) {
    if (data.fault_bits & (MOWER_BATTERY_WARNING | MOWER_BATTERY_BLADE_INHIBIT)) return "请充电";
  }
  if (data.fast_status_known) {
    return (data.fast_status & MOWER_FAST_FAULT) ? "报错" : "已连接";
  }
  return (data.fault_bits & MOWER_STATUS_ALARM_MASK) ? "报错" : "已连接";
}

static void mowerUserValue(coord_t x, coord_t y, unsigned value, bool valid,
                           const char *unit="", uint8_t decimals=0)
{
  // At most ten unsigned digits, one decimal point and the current short units.
  // Build backwards so leading fractional zeroes share the same conversion.
  char text[20];
  char *first=text+11, *end=first;
  if (valid) {
    uint8_t digits=0;
    do {
      if (decimals && digits == decimals) *--first='.';
      *--first=char('0'+value%10U);
      value/=10U;
    } while (++digits <= decimals || value);
  }
  else { *--first='-'; *--first='-'; }
  do { *end++=*unit; } while (*unit++);
  mowerZhText(x, y, first);
}

static const uint8_t MOWER_BLADE_GEAR_UNKNOWN = 4U;

static uint8_t mowerBladeGear(bool known, uint16_t runtime)
{
  if (!known) return MOWER_BLADE_GEAR_UNKNOWN;
  if (!(runtime & 1U)) return 0U;
  const uint8_t gear = (runtime >> 1) & 3U;
  return gear ? gear : MOWER_BLADE_GEAR_UNKNOWN;
}

static const char *mowerBladeGearName(uint8_t gear)
{
  static const char *const names[] = {"关", "低", "中", "高", "--"};
  return names[gear];
}

static const char *mowerBagCommandName(uint8_t command)
{
  return command == 1 ? "开" : command == 2 ? "关" : "--";
}

static bool mowerBagFullKnown(const MowerUiSnapshot &data)
{
  return data.telemetry_online && (data.valid & MOWER_UI_BAG_CONNECTION) &&
      data.bag_connected && (data.valid & MOWER_UI_BAG_FULL);
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

#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
static bool mowerUltrasonicKnown(const MowerUiSnapshot &data)
{
  return data.telemetry_online && data.fast_status_known && (data.valid & MOWER_UI_FAULT);
}

static const char *mowerUltrasonicName(const MowerUiSnapshot &data)
{
  return !mowerUltrasonicKnown(data) ? "--" :
      data.fault_bits & MOWER_ULTRASONIC_ENABLED ? "开" : "关";
}
#endif

enum MowerHomeIcon : uint8_t {
  MOWER_ICON_WHEEL, MOWER_ICON_BLADE, MOWER_ICON_HEIGHT, MOWER_ICON_BAG,
  MOWER_ICON_CAR, MOWER_ICON_RADIO, MOWER_ICON_WARNING, MOWER_ICON_REVERSE
};

static void mowerHomeIcon(coord_t x, coord_t y, MowerHomeIcon icon)
{
  static const uint8_t rows[][7] = {
    {0x1c,0x22,0x41,0x7f,0x49,0x2a,0x1c}, // Steering wheel
    {0x06,0x04,0x1c,0x7f,0x1c,0x10,0x30}, // Blade
    {0x12,0x3a,0x12,0x10,0x12,0x3a,0x12}, // Height ruler
    {0x1c,0x04,0x7f,0x41,0x55,0x41,0x3e}, // Grass bag
    {0x1c,0x22,0x7f,0x5d,0x41,0x7f,0x22}, // Chassis
    {0x02,0x02,0x3e,0x22,0x2a,0x22,0x3e}, // Transmitter
    {0x08,0x14,0x14,0x2a,0x22,0x49,0x7f}, // Warning
    {0x08,0x08,0x08,0x49,0x2a,0x1c,0x08}  // Reverse
  };
  const LcdFlags pixel = icon==MOWER_ICON_WARNING ? ERASE : FORCE;
  for (unsigned row=0; row<7; ++row)
    for (unsigned bits=rows[icon][row], col=0; bits; bits>>=1, ++col)
      if (bits & 1U) lcdDrawPoint(x+6-col, y+row, pixel);
}

static void mowerHomeBattery(coord_t x, uint8_t fill)
{
  lcdDrawRect(x, 55, 17, 7);
  lcdDrawSolidFilledRect(x+17, 57, 2, 3, FORCE);
  if (fill) lcdDrawSolidFilledRect(x+2, 57, fill, 3, FORCE);
}

static void mowerHomeReading(coord_t x, uint16_t value, bool valid,
                             const char *unit, LcdFlags precision=0)
{
  if (valid) lcdDrawNumber(x, 26, value, MIDSIZE | precision);
  else lcdDrawText(x, 26, "--", MIDSIZE);
  lcdDrawText(lcdNextPos+2, 31, unit, SMLSIZE);
}

static __attribute__((noinline)) void mowerUserHome(const MowerUiSnapshot &data, bool known, uint16_t runtime)
{
  const bool api = mowerApiMode(data);
  const char *mode = mowerModeName(data);
  mowerZhText(0, 0, mowerConnection(data), true);
  mowerZhText(56-mowerZhTextWidth(mode)/2, 0, mode);
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
  mowerZhText(78, 0, "避障");
  mowerZhText(100, 0, mowerUltrasonicName(data));
#endif
  for (uint8_t bar=0; bar<4; ++bar) {
    const uint8_t height=3+bar*2;
    if (data.telemetry_online && data.radio_quality >= (bar+1)*25U)
      lcdDrawSolidFilledRect(115+bar*3, 11-height, 2, height, FORCE);
  }
  lcdDrawSolidFilledRect(0, 13, 128, 1, FORCE);
  lcdDrawSolidFilledRect(0, 39, 128, 1, FORCE);
  lcdDrawSolidFilledRect(0, 51, 128, 1, FORCE);
  lcdDrawSolidFilledRect(63, 14, 1, 50, FORCE);

  mowerHomeIcon(3, 17, MOWER_ICON_WHEEL);
  mowerZhText(14, 14, api ? "实速" : "设定");
  mowerHomeReading(3, api ? data.walk_actual_cms : mowerTravelSpeedCms(),
      !api || (data.valid & MOWER_UI_WALK_ACTUAL), "m/s", PREC2);
  mowerHomeIcon(68, 17, MOWER_ICON_BLADE);
  mowerZhText(78, 14, "刀设定");
  const uint8_t gear = mowerBladeGear(known, runtime);
  mowerZhText(114, 14, mowerBladeGearName(gear), true);
  // Show the accepted command's mechanical setpoint even without ESC feedback.
  static const uint16_t blade_rpm[] = {0,
      static_cast<uint16_t>(BLADE_FTESC_ERPM_LEVEL_1 / BLADE_FTESC_POLE_PAIRS),
      static_cast<uint16_t>(BLADE_FTESC_ERPM_LEVEL_2 / BLADE_FTESC_POLE_PAIRS),
      static_cast<uint16_t>(BLADE_FTESC_ERPM_LEVEL_3 / BLADE_FTESC_POLE_PAIRS), 0};
  mowerHomeReading(68, blade_rpm[gear], gear != MOWER_BLADE_GEAR_UNKNOWN, "rpm");

  mowerHomeIcon(3, 42, MOWER_ICON_HEIGHT);
  mowerZhText(14, 40, "高");
  mowerUserValue(26, 40, data.height_deci_cm,
      data.telemetry_online && (data.valid & MOWER_UI_HEIGHT_FEEDBACK), "cm", 1);
  mowerHomeIcon(68, 42, MOWER_ICON_BAG);
  if (!data.telemetry_online || !(data.valid & MOWER_UI_BAG_CONNECTION)) {
    mowerZhText(78, 40, "未知");
  }
  else if (!data.bag_connected) {
    mowerZhText(78, 40, "未连接");
  }
  else {
    mowerZhText(78, 40, api ? "API" : mowerBagCommandName(data.bag_command));
    const bool fullKnown = data.valid & MOWER_UI_BAG_FULL;
    mowerZhText(104, 40, !fullKnown ? "--" : data.bag_full ? "已满" : "未满",
        fullKnown && data.bag_full);
  }

  const bool socKnown = data.telemetry_online && (data.valid & MOWER_UI_SOC);
  mowerHomeIcon(3, 55, MOWER_ICON_CAR);
  mowerHomeBattery(14, socKnown ? data.soc_percent*13U/100U : 0U);
  mowerUserValue(35, 52, data.soc_percent, socKnown, "%");
  mowerHomeIcon(70, 55, MOWER_ICON_RADIO);
  const int low=g_eeGeneral.vBatMin+90;
  const int high=g_eeGeneral.vBatMax+120;
  const int voltage=data.radio_battery_100mv;
  mowerHomeBattery(79, high > low ? limit<int>(0, (voltage-low)*13/(high-low), 13) : 0);
  mowerUserValue(98, 52, voltage, true, "V", 1);
}

static void mowerZhPopup(const char *first, const char *second, bool obstacle=false)
{
  lcdDrawSolidFilledRect(0, 14, 128, 37, ERASE);
  lcdDrawSolidFilledRect(15, 16, 100, 34, FORCE);
  lcdDrawSolidFilledRect(16, 31, 96, 16, ERASE);
  const coord_t center = obstacle ? 69 : 64;
  mowerZhText(center-mowerZhTextWidth(first)/2, 18, first, true);
  mowerZhText(center-mowerZhTextWidth(second)/2, 34, second);
  if (obstacle) {
    mowerHomeIcon(35, 21, MOWER_ICON_WARNING);
    mowerHomeIcon(40, 36, MOWER_ICON_REVERSE);
  }
}

static void mowerExp3Overlay(Exp3UiState state)
{
  if (state == EXP3_UI_PROMPT) {
    mowerZhPopup(mowerUnlockDisplay ? "是否解锁" : "确认启动割刀", "按OK确认");
    return;
  }
  const char *notice = state == EXP3_UI_BATTERY_LOW ? "电量过低" :
      state == EXP3_UI_SAFETY ? "安全输入未释放" :
      state == EXP3_UI_OWNER ? "控制权未就绪" :
      state == EXP3_UI_TIMEOUT ? "无回执" :
      state == EXP3_UI_REJECTED ? (mowerUnlockDisplay ? "解锁被拒绝" : "启动被拒绝") : nullptr;
  if (notice) {
    lcdDrawSolidFilledRect(0, 52, 128, 12, ERASE);
    mowerZhText(64-mowerZhTextWidth(notice)/2, 52, notice);
  }
}

static __attribute__((noinline)) void mowerUserDebugRow(uint8_t row, coord_t y, const MowerUiSnapshot &data,
                              bool known, uint16_t runtime)
{
  if (row == 0U) {
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
    const char *state=mowerUltrasonicName(data);
    if (mowerUltrasonicKnown(data) && mowerUltrasonicPending) {
      const bool requested=g_mower_ultrasonic_request == MOWER_ULTRASONIC_ON_US;
      if (!!(data.fault_bits & MOWER_ULTRASONIC_ENABLED) == requested)
        mowerUltrasonicPending=false;
      else state=static_cast<uint32_t>(get_tmr10ms())*10U-mowerUltrasonicSentAt <
          MOWER_ULTRASONIC_CONFIRM_MS ? "待生效" : "未确认";
    }
    mowerZhText(0, y, "超声波", true);
    mowerZhText(54, y, state, true);
#else
    mowerZhText(0, y, "超声波");
    mowerZhText(54, y, "停用");
#endif
    return;
  }
  --row;
  static const char *const labels[] = {
    "BMS原始SOC", "整车电压", "行走设定", "实际速度", "割刀转速",
    "割刀档位", "割刀输出", "高度", "草袋指令", "运行许可",
    "链路状态", "确认事务", "故障字", "停刀回执", "草袋连接"
  };
  static const char *const faults[] = {
    "急停", "碰撞", "倾斜", "遥控超时", "遥控停机锁存",
    "控制权锁存", "电池反馈异常", "割刀反馈过期", "割刀校验异常", "系统锁定"
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
  const coord_t x=54;
  switch (row) {
    case 0: mowerUserValue(66, y, data.soc_percent, data.valid & MOWER_UI_SOC, "%"); break;
    case 1: mowerUserValue(x, y, data.voltage_centivolts, data.valid & MOWER_UI_VOLTAGE, "V", 2); break;
    case 2: mowerUserValue(x, y, mowerTravelSpeedCms(), true, "m/s", 2); break;
    case 3: mowerUserValue(x, y, data.walk_actual_cms, data.valid & MOWER_UI_WALK_ACTUAL, "m/s", 2); break;
    case 4: mowerUserValue(x, y, data.blade_actual_rpm, data.valid & MOWER_UI_BLADE_RPM); break;
    case 5: mowerZhText(x, y, mowerBladeGearName(mowerBladeGear(known, runtime))); break;
    case 6: mowerZhText(x, y, !known ? "--" : (runtime&1U) ? "使能" : "关闭"); break;
    case 7: mowerUserValue(x, y, data.height_deci_cm,
        data.telemetry_online && (data.valid & MOWER_UI_HEIGHT_FEEDBACK), "cm", 1); break;
    case 8: mowerZhText(x, y, mowerBagCommandName(data.bag_command)); break;
    case 9: mowerZhText(x, y, !(data.valid & MOWER_UI_RUN_PERMISSION) ? "--" : data.run_permission ? "允许" : "禁止"); break;
    case 10: mowerZhText(x, y, mowerConnection(data)); break;
    case 11: mowerZhText(x, y, mowerTransactionName(mowerExp3Probe.state)); break;
    case 12: {
      char bits[]="0x0000";
      for (uint8_t digit=0; digit<4; ++digit)
        bits[5-digit]="0123456789ABCDEF"[(data.fault_bits>>(digit*4))&15U];
      mowerZhText(x, y, data.valid & MOWER_UI_FAULT ? bits : "--");
      break;
    }
    case 13: mowerZhText(x, y, mowerExp3CancelFailed ? "失败" :
        mowerExp3Probe.cancel_pending || mowerExp3Probe.state == EXP3_UI_CANCEL_WAIT ? "等待" : "空闲"); break;
    case 25:
      mowerZhText(0, y, "最低单体");
      mowerUserValue(x, y, data.battery_min_mv, data.valid & MOWER_UI_BATTERY_MIN, "V", 3);
      break;
    case 26: {
      mowerZhText(0, y, "保护原因");
      const uint16_t reason=data.battery_reasons;
      const char *label=!(data.valid & MOWER_UI_BATTERY_REASON) ? "未知" :
          (reason & 1U) ? "数据失效" : (reason & 0x20U) ? "电量过低" :
          (reason & 4U) ? "电压过低" : "无";
      mowerZhText(x, y, label);
      break;
    }
    case 27:
      mowerZhText(0, y, "草袋满载");
      mowerZhText(x, y, !mowerBagFullKnown(data) ? "未知" : data.bag_full ? "已满" : "未满");
      break;
    case 14: mowerZhText(x, y, !data.telemetry_online || !(data.valid & MOWER_UI_BAG_CONNECTION) ?
        "未知" : data.bag_connected ? "已连接" : "未连接"); break;
  }
}

static __attribute__((noinline)) void mowerUserRender()
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
    mowerUserValue(90, 0, mowerDebugFirstRow+1, true, "/29");
    for (uint8_t i=0; i<4; ++i)
      mowerUserDebugRow(mowerDebugFirstRow+i, 12+i*12, data, known, runtime);
  }
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
  if (mowerUserPage == MOWER_USER_HOME && mowerExp3DisplayState != EXP3_UI_PROMPT &&
      mowerUltrasonicKnown(data) &&
      (data.fault_bits & (MOWER_ULTRASONIC_ENABLED | MOWER_ULTRASONIC_NEAR)) ==
          (MOWER_ULTRASONIC_ENABLED | MOWER_ULTRASONIC_NEAR))
    mowerZhPopup("前方障碍", "请倒车", true);
#endif
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
#if MOWER_ULTRASONIC_AVOIDANCE_ENABLED
    if (event == EVT_KEY_BREAK(KEY_ENTER) && mowerDebugFirstRow == 0U) {
      MowerUiSnapshot data;
      mowerUiRead(data);
      const uint32_t now=static_cast<uint32_t>(get_tmr10ms())*10U;
      if (mowerUltrasonicKnown(data) && (!mowerUltrasonicPending ||
          now-mowerUltrasonicSentAt >= MOWER_ULTRASONIC_CONFIRM_MS)) {
        g_mower_ultrasonic_request=data.fault_bits & MOWER_ULTRASONIC_ENABLED ?
            MOWER_ULTRASONIC_OFF_US : MOWER_ULTRASONIC_ON_US;
        mowerUltrasonicPending=true;
        mowerUltrasonicSentAt=now;
      }
      killEvents(event);
    }
#endif
    if ((event == EVT_KEY_BREAK(KEY_UP) || event == EVT_KEY_REPT(KEY_UP)) && mowerDebugFirstRow)
      --mowerDebugFirstRow;
    if ((event == EVT_KEY_BREAK(KEY_DOWN) || event == EVT_KEY_REPT(KEY_DOWN)) &&
        mowerDebugFirstRow < MOWER_DEBUG_ROWS-4) ++mowerDebugFirstRow;
  }
  mowerUserRender();
}

