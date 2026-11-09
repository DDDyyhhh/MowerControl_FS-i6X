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

#include "opentx.h"
#if defined(__arm__) && defined(__GNUC__) && defined(PCBI6X_MOWER_BLADE_OK)
// GCC15 size optimization keeps the production UI within the physical 128KiB.
#pragma GCC optimize ("Oz")
#endif
#if defined(PCBI6X) && defined(PCBI6X_MOWER_UI_SHELL)
#include "mower_ui.h"
#if defined(PCBI6X_EXP3_PWM_HIL) || defined(PCBI6X_MOWER_BLADE_OK)
#include "../../../../../../../experiments/ticket03/exp3_board.h"
#include "../../../../../../../experiments/ticket03/exp3_ui_event.h"
#endif
#endif

// =============================================================================
// 主界面（128x64 像素，原点在左上角；FW=6=标准字体宽，FH=8=标准行高）
// 屏幕布局总览：
//   (0,0) ───────────────────────────── (127,0)
//    │ !异常标记  电池图标/电压(x=0..23,y=0..14)      计时器1(大字,右对齐x=125,y=16) │
//    │ 飞行模式名(34,16)                                            │
//    │ RSSI信号条(34..50,y=24..32)                                 │
//    │ [微调] 开关列(x=10)   （摇杆框已隐藏，中部空出 31..98, y≈25..56）   开关列(x=97) │
//    │ RX电压(小字,x=8,y=51)  电位器条(x≈64)        会话计时(53,57) │
//   (0,63) ──────────────────────────── (127,63)
// =============================================================================
#define BIGSIZE       DBLSIZE
#if defined (PCBTARANIS) || defined(PCBI6X)
  #define LBOX_CENTERX  (LCD_W/4 + 14)     // 左摇杆方框中心 x=46
  #define RBOX_CENTERX  (3*LCD_W/4 - 13)   // 右摇杆方框中心 x=83
#else
  #define LBOX_CENTERX  (LCD_W/4 + 10)
  #define RBOX_CENTERX  (3*LCD_W/4 - 10)
#endif
#define MODELNAME_X   (2*FW-2)               // 模型名 左上角 (10, 0)，大字（DBLSIZE，高16px）
#define MODELNAME_Y   (0)
#define PHASE_X       (6*FW-2)               // 飞行模式名称 (34, 16)，标准字
#define PHASE_Y       (2*FH)
#define PHASE_FLAGS   0
// 发射机电池电压显示区：BATTGRAPH 下整块最左侧 = VBATT_X-26，所以取 26 时块左边界正好贴屏幕左边 x=0
//   块内布局（相对 VBATT_X=26）：反色底 x=0..23，图标 x=1..21，正极凸头 x=22，数字右对齐到 x=18
//   想让整块往右移，就把此值加大相同的量（原值 6*FW-1=35 → 左边界 9）
#define VBATT_X       (26)
#define VBATT_Y       (0)                    // 垂直位置：上移到屏幕最顶部（0..15 行）。原值 2*FH=16
#define VBATTUNIT_Y   (FH)                   // 非 BATTGRAPH 时 "V" 单位的位置，跟随 VBATT_Y 上移
#define REBOOT_X      (2)                    // 左上角 (2,1)：异常关机 '!' 标记
#define BAR_HEIGHT    (BOX_WIDTH-1l) // don't remove the l here to force 16bits maths on 9X
#define TRIM_LEN      21                     // 微调条长度（半程像素数）
#define TRIM_LH_X     (TRIM_LEN+4)           // 左水平微调 中点 x=25, y=60
#define TRIM_LV_X     3                      // 左垂直微调 x=3, y=10..52（中点31）
#define TRIM_RV_X     (LCD_W-4)              // 右垂直微调 x=124, y=10..52
#define TRIM_RH_X     (LCD_W-TRIM_LEN-5)     // 右水平微调 中点 x=102, y=60
#define TRIM_LH_NEG   (TRIM_LH_X+3*FW+1)     // 微调数值提示（TINSIZE）显示位置
#define TRIM_LH_POS   (TRIM_LH_X-4*FW+3)
#define TRIM_RH_NEG   (TRIM_RH_X+3*FW+1)
#define TRIM_RH_POS   (TRIM_RH_X-4*FW+3)
#define RSSSI_X       (30)                   // RSSI 信号条：4根阶梯条，实际画在 (34..49, 24..32)
#define RSSSI_Y       (31)
#if defined(PCBI6X)
  #define RSSI_MAX      100
#else
  #define RSSI_MAX      105
#endif
#define CLOCK_X       53                     // 会话计时器（本次开机累计时长）(53, 57)，小字
#define CLOCK_Y       57


// 信号强度（RSSI）阶梯条：4 根由低到高的实心条，画在屏幕中部 (34..49, 24..32)。
// RSSI 越高、点亮的条越多；低于告警阈值时不显示任何条。
// 另：XJT 内置模块外置天线启用时在电压值下方显示 "E" 小字（i6X 不适用）。
void drawExternalAntennaAndRSSI()
{
#if defined(INTERNAL_MODULE_PXX1) && defined(EXTERNAL_ANTENNA)
  if (isModuleXJT(INTERNAL_MODULE) && isExternalAntennaEnabled()) {
    lcdDrawText(VBATT_X-1, VBATT_Y+8, "E", TINSIZE);
  }
#endif

  if (TELEMETRY_RSSI() > 0) {
    auto warningRSSI = g_model.rfAlarms.warning;
    int8_t value = TELEMETRY_RSSI() - warningRSSI;
    uint8_t step = (RSSI_MAX - warningRSSI) / 4;
    for (uint8_t i = 1; i < 5; i++) {
      if (value > step * (i - 1)) {
        lcdDrawFilledRect(RSSSI_X + i * 4, RSSSI_Y - 2 * i + 1, 3, 2 * i - 1, SOLID, 0);
      }
    }
  }
}

// 电位器（POT/滑条）竖条：画在屏幕底部中间，底边 y=56..63，从 x=LCD_W/2 起每 5px 一根。
// 条高表示电位器当前量程位置（居中约半高）。i6X 只有 1 个电位器。
void drawPotsBars()
{
  // Optimization by Mike Blandford
  for (uint8_t x=LCD_W/2 - (NUM_POTS+NUM_SLIDERS-1) * 5 / 2, i=NUM_STICKS; i<NUM_STICKS+NUM_POTS+NUM_SLIDERS; x+=5, i++) {
    if (IS_POT_SLIDER_AVAILABLE(i)) {
      uint8_t len = ((calibratedAnalogs[i]+RESX)*BAR_HEIGHT/(RESX*2))+1l;  // calculate once per loop
      V_BAR(x, LCD_H-8, len);
    }
  }
}

// 摇杆画面主体：
//   - 左摇杆框：中心 (46,40)，方框约 31x31（y≈25..56），框内圆点 = 左摇杆/油门位置
//   - 右摇杆框：中心 (83,40)，同上
//   - 底部中间：电位器竖条（见 drawPotsBars）
// 注意：主界面已改为只调 drawPotsBars()（不显示摇杆框），
//       本函数目前仅由校准页 menuRadioCalibration 调用（那里需要摇杆指示）。
void doMainScreenGraphics()
{
  int16_t calibStickVert = calibratedAnalogs[CONVERT_MODE(1)];
  if (g_model.throttleReversed && CONVERT_MODE(1) == THR_STICK)
    calibStickVert = -calibStickVert;
  drawStick(LBOX_CENTERX, calibratedAnalogs[CONVERT_MODE(0)], calibStickVert);

  calibStickVert = calibratedAnalogs[CONVERT_MODE(2)];
  if (g_model.throttleReversed && CONVERT_MODE(2) == THR_STICK)
    calibStickVert = -calibStickVert;
  drawStick(RBOX_CENTERX, calibratedAnalogs[CONVERT_MODE(3)], calibStickVert);

  drawPotsBars();
}

// 四个微调滑条（副翼/升降/油门/方向，按模式转换）：
//   垂直微调：左 x=3、右 x=124，竖线 y=10..52，滑块 7x7 方框随微调值上下移动
//   水平微调：左 x=25、右 x=102，横线 y=60，滑块左右移动
//   超出范围显示为缺口；模型设置了 displayTrims 时在旁边以小字显示微调数值
void displayTrims(uint8_t phase)
{
  static uint8_t x[4] = {TRIM_LH_X, TRIM_LV_X, TRIM_RV_X, TRIM_RH_X};
  static uint8_t vert[4] = {0, 1, 1, 0};

  for (uint8_t i = 0; i < 4; i++) {

    if (getRawTrimValue(phase, i).mode == TRIM_MODE_NONE)
      continue;

    coord_t ym;
    uint8_t stickIndex = CONVERT_MODE(i);
    coord_t xm = x[stickIndex];
    uint8_t att = ROUND;
    int16_t val = getTrimValue(phase, i);

    int16_t dir = val;
    bool exttrim = false;
    if (val < TRIM_MIN || val > TRIM_MAX) {
      exttrim = true;
    }
    val = (val * TRIM_LEN) / TRIM_MAX;
    if (val < -TRIM_LEN) {
      val = -TRIM_LEN;
    }
    else if (val > TRIM_LEN) {
      val = TRIM_LEN;
    }

    if (vert[i]) {
      ym = 31;
      lcdDrawSolidVerticalLine(xm, ym - TRIM_LEN, TRIM_LEN * 2 + 1);
      if (i!=2 || !g_model.thrTrim) {
        lcdDrawSolidVerticalLine(xm-1, ym-1,  3);
        lcdDrawSolidVerticalLine(xm+1, ym-1,  3);
      }
      ym -= val;
      lcdDrawFilledRect(xm-3, ym-3, 7, 7, SOLID, att|ERASE);
      if (dir >= 0) {
        lcdDrawSolidHorizontalLine(xm-1, ym-1,  3);
      }
      if (dir <= 0) {
        lcdDrawSolidHorizontalLine(xm-1, ym+1,  3);
      }
      if (exttrim) {
        lcdDrawSolidHorizontalLine(xm-1, ym,  3);
      }
      if (g_model.displayTrims != DISPLAY_TRIMS_NEVER && dir != 0) {
        if (g_model.displayTrims == DISPLAY_TRIMS_ALWAYS || (trimsDisplayTimer > 0 && (trimsDisplayMask & (1<<i)))) {
          lcdDrawNumber(dir>0 ? 12 : 40, xm-2, -abs(dir/5), TINSIZE|VERTICAL);
        }
      }
    }
    else {
      ym = 60;
      lcdDrawSolidHorizontalLine(xm - TRIM_LEN, ym, TRIM_LEN * 2 + 1);
      lcdDrawSolidHorizontalLine(xm-1, ym-1,  3);
      lcdDrawSolidHorizontalLine(xm-1, ym+1,  3);
      xm += val;
      lcdDrawFilledRect(xm-3, ym-3, 7, 7, SOLID, att|ERASE);
      if (dir >= 0) {
        lcdDrawSolidVerticalLine(xm+1, ym-1,  3);
      }
      if (dir <= 0) {
        lcdDrawSolidVerticalLine(xm-1, ym-1,  3);
      }
      if (exttrim) {
        lcdDrawSolidVerticalLine(xm, ym-1,  3);
      }
      if (g_model.displayTrims != DISPLAY_TRIMS_NEVER && dir != 0) {
        if (g_model.displayTrims == DISPLAY_TRIMS_ALWAYS || (trimsDisplayTimer > 0 && (trimsDisplayMask & (1<<i)))) {
          lcdDrawNumber((stickIndex==0 ? (dir>0 ? TRIM_LH_POS : TRIM_LH_NEG) : (dir>0 ? TRIM_RH_POS : TRIM_RH_NEG)), ym-2, -abs(dir/5), TINSIZE);
        }
      }
    }
    lcdDrawSquare(xm-3, ym-3, 7, att);
  }
}

// 发射机电池电压：显示在屏幕最顶部一行、紧贴左边 (x=0..23, y=0..14)。
//   BATTGRAPH 版本：电压数字(右对齐 x=18,y=1) + 电池图标条（21px 宽，x=1..21，y=9..13）；
//   非 BATTGRAPH 版本：大字电压值 + "V" 单位；低压告警时闪烁反白。
//   注意：这是发射机自己的电压，不是接收机电压（接收机电压见下方 drawReceiverVoltage）。
void displayBattVoltage()
{
#if defined(BATTGRAPH)
  putsVBat(VBATT_X-8, VBATT_Y+1, RIGHT);
  lcdDrawSolidFilledRect(VBATT_X-25, VBATT_Y+9, 21, 5);
  lcdDrawSolidVerticalLine(VBATT_X-4, VBATT_Y+10, 3);
  uint8_t count = GET_TXBATT_BARS();
  for (uint32_t i=0; i<count; i+=2)
    lcdDrawSolidVerticalLine(VBATT_X-24+i, VBATT_Y+10, 3);
  if (!IS_TXBATT_WARNING() || BLINK_ON_PHASE)
    lcdDrawSolidFilledRect(VBATT_X-26, VBATT_Y, 24, 15);
#else
  LcdFlags att = (IS_TXBATT_WARNING() ? BLINK|INVERS : 0) | BIGSIZE;
  putsVBat(VBATT_X-1, VBATT_Y, att|NO_UNIT);
  lcdDrawChar(VBATT_X, VBATTUNIT_Y, 'V');
#endif
}

#define displayVoltageOrAlarm() displayBattVoltage()

#if defined(PCBI6X)
// 接收机电压（遥测回传）：小字显示在屏幕左下角 (8, 51..57)。
//   内容：传感器标签（如 "A1"）+ 电压值（如 "7.4V"），共约 40px 宽。
//   数据来源：iBUS 传感器 id=0x00（接收机内部电压，连接后自动注册为 A1）；
//             没有 A1 时回退到第一个可用的电压类传感器。
//   条件：遥测在流（TELEMETRY_STREAMING）才显示；数据超时老化时闪烁。
//   位置刻意避开：左侧开关列(y≤50)、垂直微调条(x=3)、水平微调线(y=60)、
//                会话计时器(x=53 起)。
// Falls back to the first available voltage sensor if A1 is not present.
static int16_t findRxBattSensorIndex()
{
  int16_t firstVolt = -1;
  for (uint8_t i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
    TelemetrySensor & sensor = g_model.telemetrySensors[i];
    if (sensor.id == 0 || sensor.unit != UNIT_VOLTS || !telemetryItems[i].isAvailable())
      continue;
    if (sensor.id == 0x00)  // iBUS internal (receiver) voltage
      return i;
    if (firstVolt < 0)
      firstVolt = i;
  }
  return firstVolt;
}

void drawReceiverVoltage()
{
  if (!TELEMETRY_STREAMING())
    return;
  int16_t index = findRxBattSensorIndex();
  if (index < 0)
    return;
  source_t source = MIXSRC_FIRST_TELEM + 3 * index;
  LcdFlags att = SMLSIZE | (telemetryItems[index].isOld() ? BLINK : 0);
  drawSource(8, 51, source, att);
  drawSourceValue(22, 51, source, att);
}
#endif
#if defined(PCBI6X) && defined(PCBI6X_MOWER_UI_SHELL)
static MowerUiProvider mowerProvider = nullptr;

void mowerUiSetProvider(MowerUiProvider provider)
{
  mowerProvider = provider;
}

// The eight existing SENS measurements are addressed by their published
// FlySky sensor type, not their user-editable display label or discovery order.
// GCC15 Thumb/LTO may reuse a call-clobbered output pointer across this call.
// Keep the full AAPCS boundary for this telemetry accessor.
#if defined(__arm__) && defined(__GNUC__)
__attribute__((noipa))
#endif
static int32_t mowerSensor(uint16_t id)
{
  if (!TELEMETRY_STREAMING())
    return -1;
  int32_t value = -1;
  for (uint8_t i = 0; i < MAX_TELEMETRY_SENSORS; ++i) {
    const TelemetrySensor &sensor = g_model.telemetrySensors[i];
    TelemetryItem &item = telemetryItems[i];
    if (sensor.type == TELEM_TYPE_CUSTOM && sensor.id == id &&
        item.isAvailable() && !item.isOld() &&
        item.value >= 0 && item.value <= 65535) {
      if (value >= 0)
        return -1; // Ambiguous live ID; zero remains a legitimate reading.
      value = item.value;
    }
  }
  return value;
}

void mowerUiReadLive(MowerUiSnapshot &snapshot)
{
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.bag_command = mowerUiBagCommand(channelOutputs[5] / 2 + PPM_CH_CENTER(5));
  snapshot.radio_battery_100mv = g_vbat100mV;
  snapshot.radio_quality = TELEMETRY_RSSI();
  int32_t value;
  if ((value = mowerSensor(0x03)) >= 0) {
    snapshot.voltage_centivolts = value;
    snapshot.valid |= MOWER_UI_VOLTAGE;
  }
  if ((value = mowerSensor(0x06)) >= 0 && value <= 100) {
    snapshot.soc_percent = value;
    snapshot.valid |= MOWER_UI_SOC;
  }
#if defined(PCBI6X_MOWER_BLADE_OK)
  if (TELEMETRY_STREAMING() && exp3_board_battery(
      static_cast<uint32_t>(get_tmr10ms())*10U, &snapshot.battery_min_mv, &snapshot.battery_reasons)) {
    snapshot.valid |= MOWER_UI_BATTERY_REASON;
    if (snapshot.battery_min_mv > 0U) snapshot.valid |= MOWER_UI_BATTERY_MIN;
  }
#endif
  if ((value = mowerSensor(0x13)) >= 0) {
    snapshot.walk_actual_cms = value;
    snapshot.valid |= MOWER_UI_WALK_ACTUAL;
  }
  if ((value = mowerSensor(0x02)) >= 0) {
    snapshot.blade_actual_rpm = value;
    snapshot.valid |= MOWER_UI_BLADE_RPM;
  }
  if ((value = mowerSensor(0x16)) >= 0 && value <= 1) {
    snapshot.work_mode = value;
    snapshot.valid |= MOWER_UI_WORK_MODE;
  }
  if ((value = mowerSensor(0x15)) >= 0 && value <= 1) {
    snapshot.run_permission = value;
    snapshot.valid |= MOWER_UI_RUN_PERMISSION;
  }
  if ((value = mowerSensor(0xE0)) >= 0) {
    snapshot.fault_bits = value;
    snapshot.valid |= MOWER_UI_FAULT;
  }
  // The chassis transmits zero for an unavailable source. Fresh RF packets
  // alone must not turn missing BMS/encoder/blade feedback into a real zero.
  const bool faultKnown = (snapshot.valid & MOWER_UI_FAULT) != 0;
  if (!faultKnown || (snapshot.fault_bits & 0x0040U))
    snapshot.valid &= static_cast<uint16_t>(~(MOWER_UI_SOC | MOWER_UI_VOLTAGE | MOWER_UI_BATTERY_MIN));
  if (!faultKnown || (snapshot.fault_bits & 0x0080U))
    snapshot.valid &= static_cast<uint16_t>(~MOWER_UI_WALK_ACTUAL);
  if (!faultKnown || (snapshot.fault_bits & 0x0300U))
    snapshot.valid &= static_cast<uint16_t>(~MOWER_UI_BLADE_RPM);
  // Receiver RSSI alone is not evidence of a live chassis. Require a fresh
  // board status measurement, and retain per-field unknowns independently.
  snapshot.telemetry_online = (snapshot.valid & (MOWER_UI_WORK_MODE |
      MOWER_UI_RUN_PERMISSION | MOWER_UI_FAULT)) != 0;
#if defined(PCBI6X_MOWER_BLADE_OK)
  if (TELEMETRY_STREAMING() && exp3_board_height(
      static_cast<uint32_t>(get_tmr10ms())*10U, &snapshot.height_deci_cm)) {
    snapshot.valid |= MOWER_UI_HEIGHT_FEEDBACK;
    snapshot.telemetry_online = true;
  }
  if (TELEMETRY_STREAMING() && exp3_board_bag_connection(
      static_cast<uint32_t>(get_tmr10ms())*10U, &snapshot.bag_connected)) {
    snapshot.valid |= MOWER_UI_BAG_CONNECTION;
    snapshot.telemetry_online = true;
    if (snapshot.bag_connected && exp3_board_bag_full(
        static_cast<uint32_t>(get_tmr10ms())*10U, &snapshot.bag_full))
      snapshot.valid |= MOWER_UI_BAG_FULL;
  }
#endif
#if defined(PCBI6X_MOWER_BLADE_OK)
  snapshot.fast_status_known = TELEMETRY_STREAMING() && exp3_board_runtime(
      static_cast<uint32_t>(get_tmr10ms())*10U, &snapshot.fast_status);
  if (snapshot.fast_status_known) {
    snapshot.telemetry_online = true;
    snapshot.run_permission = (snapshot.fast_status & MOWER_FAST_RUN) ? 1U : 0U;
    snapshot.valid |= MOWER_UI_RUN_PERMISSION;
    if (snapshot.fast_status & MOWER_FAST_WHEEL_STALE)
      snapshot.valid &= static_cast<uint16_t>(~MOWER_UI_WALK_ACTUAL);
  }
#endif
  // Height is a controller estimate, never a local CH9 target or encoder reading.
  // Bag position remains unavailable; CH6 is a target only.
}

#if defined(PCBI6X_MOWER_UI_MOCK)
static void mowerUiReadMock(MowerUiSnapshot &snapshot)
{
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.mock = true;
  snapshot.telemetry_online = true;
  snapshot.radio_battery_100mv = 78;
  snapshot.radio_quality = 77;
  snapshot.valid = MOWER_UI_SOC | MOWER_UI_VOLTAGE |
      MOWER_UI_WALK_SET | MOWER_UI_WALK_ACTUAL |
      MOWER_UI_BLADE_SET | MOWER_UI_BLADE_RPM |
      MOWER_UI_HEIGHT_TARGET | MOWER_UI_HEIGHT_ACTION |
      MOWER_UI_WORK_MODE | MOWER_UI_RUN_PERMISSION | MOWER_UI_FAULT;
  snapshot.soc_percent = 82;
  snapshot.voltage_centivolts = 4860;
  snapshot.walk_set_cms = 80;
  snapshot.walk_actual_cms = 73;
  snapshot.blade_set_rpm = 3200;
  snapshot.blade_actual_rpm = 3075;
  snapshot.height_deci_cm = 70;
  snapshot.valid |= MOWER_UI_HEIGHT_FEEDBACK;
  snapshot.height_target = 3;
  snapshot.height_action = 1;
  snapshot.work_mode = 1;
  snapshot.run_permission = 0; // sample never grants authority
  snapshot.fault_bits = 0x0008;
}
#endif

static void mowerUiRead(MowerUiSnapshot &snapshot)
{
  if (mowerProvider) {
    mowerProvider(snapshot);
  }
#if defined(PCBI6X_MOWER_UI_MOCK)
  else {
    mowerUiReadMock(snapshot);
  }
#else
  else {
    mowerUiReadLive(snapshot);
  }
#endif
}

static void mowerValue(coord_t x, coord_t y, const char *label,
                       uint16_t value, bool valid)
{
  lcdDrawText(x, y, label, SMLSIZE);
  if (valid)
    lcdDrawNumber(x + 54, y, value, SMLSIZE | RIGHT);
  else
    lcdDrawText(x + 35, y, "--", SMLSIZE);
}

// CH10 knob setting, not controller acceptance or measured speed. Use the
// exact AFHDS2A packet pulse conversion, including model channel center.
static uint16_t mowerTravelSpeedCms()
{
  int32_t pulse = channelOutputs[9] / 2 + PPM_CH_CENTER(9);
  if (pulse <= 1000) return 40;
  if (pulse >= 2000) return 180;
  return 40 + ((pulse - 1000) * 140 + 500) / 1000;
}

static void mowerSpeedSetting()
{
  lcdDrawText(0, 24, "SETm/s", SMLSIZE);
  lcdDrawNumber(64, 24, mowerTravelSpeedCms(), SMLSIZE | RIGHT | PREC2);
}

static void mowerIcon()
{
  // 16x10 pixel monochrome mower silhouette, no bitmap or RAM allocation.
  lcdDrawSolidHorizontalLine(56, 47, 16);
  lcdDrawSolidHorizontalLine(59, 44, 9);
  lcdDrawSolidVerticalLine(59, 45, 3);
  lcdDrawSolidVerticalLine(68, 45, 3);
  lcdDrawSolidVerticalLine(57, 48, 3);
  lcdDrawSolidVerticalLine(70, 48, 3);
  lcdDrawRect(59, 52, 3, 3);
  lcdDrawRect(67, 52, 3, 3);
}

#if defined(PCBI6X_EXP3_PWM_HIL) || defined(PCBI6X_MOWER_BLADE_OK)
static Exp3UiProbe mowerExp3Probe;
static uint32_t mowerExp3Controller;
static uint32_t mowerExp3Hmi;
static bool mowerExp3Initialized;
static Exp3UiState mowerExp3DisplayState;
static bool mowerExp3Bound;
#if defined(PCBI6X_MOWER_BLADE_OK)
static bool mowerExp3CancelFailed;
static Exp3UiProbe mowerUnlockProbe;
static uint32_t mowerActionSequence;
static bool mowerUnlockDisplay;

#endif

static Exp3UiState mowerExp3Step(event_t event)
{
#if defined(PCBI6X_MOWER_BLADE_OK)
  Exp3BoardStatus status;
  exp3_board_status(&status);
  mowerExp3Bound = status.bound;
  const uint32_t now = static_cast<uint32_t>(get_tmr10ms()) * 10U;
  if (!status.bound || !mowerExp3Initialized || status.controller != mowerExp3Controller || status.hmi != mowerExp3Hmi) {
    exp3_ui_probe_init(&mowerExp3Probe);
    exp3_ui_probe_init(&mowerUnlockProbe);
    exp3_ui_probe_bind(&mowerExp3Probe,status.controller,status.hmi,EXP3_PWM_CONTEXT,1U,0U);
    exp3_ui_probe_bind(&mowerUnlockProbe,status.controller,status.hmi,EXP3_UNLOCK_CONTEXT,1U,0U);
    mowerExp3Controller=status.controller; mowerExp3Hmi=status.hmi;
    mowerExp3Initialized=true; mowerActionSequence=0U;
  }
  uint16_t runtime=0U, authority=0U;
  const bool known=exp3_board_runtime(now,&runtime) && exp3_board_authority(now,&authority);
  const bool unlock=!!(authority & MOWER_AUTH_UNLOCK);
  if (event==EVT_KEY_BREAK(KEY_ENTER) && mowerActionSequence >= 65535U) {
    exp3_board_restart_session(now);
    return EXP3_UI_TIMEOUT;
  }
  Exp3UiState displays[2];
  Exp3UiProbe *probes[2]={&mowerUnlockProbe,&mowerExp3Probe};
  for (unsigned i=0;i<2U;++i) {
    Exp3UiProbe *probe=probes[i];
    Exp3UiInput input={};
    exp3_board_status(&status);
    input.now_ms=now;
    input.ch4_us=static_cast<uint16_t>(limit<int32_t>(0,channelOutputs[3]/2+PPM_CH_CENTER(3),65535));
    input.bound=status.bound; input.transport_waiting=status.waiting; input.cancel_ready=status.cancel_ready;
    input.authority=authority; input.authority_valid=known;
    input.start_inhibited=!known || unlock != (i==0U);
    input.ok_pressed=event==EVT_KEY_BREAK(KEY_ENTER) && !input.start_inhibited;
    input.matches=status.matches; input.last_result=status.last_result;
    probe->next_action=mowerActionSequence;
    Exp3UiOutput output=exp3_ui_probe_step(probe,&input);
    if (output.submit_kind!=EXP3_UI_SUBMIT_NONE) {
      if (output.submit_kind==EXP3_UI_SUBMIT_CONFIRM) {
        mowerActionSequence=output.message.action;
        probe->runtime_baseline=runtime;
      }
      if (!exp3_board_submit(&output.message)) {
        if (output.submit_kind==EXP3_UI_SUBMIT_CANCEL) {
          probe->cancel_pending=1U; probe->state=EXP3_UI_CANCEL_WAIT;
        } else probe->state=EXP3_UI_REJECTED;
        output.state=probe->state;
      }
    }
    displays[i]=output.state;
  }
  mowerExp3CancelFailed=status.cancel_failed;
  if (!status.bound && status.cancel_failed && mowerUnlockProbe.ch4_start) return EXP3_UI_TIMEOUT;
  /* Matching Motion completion and a fresh runtime transition are both needed.
   * An ordinary runtime packet never completes or releases a transaction. */
  bool completed=mowerUnlockProbe.completed && !unlock &&
      !mowerExp3Probe.center_seen && !mowerExp3Probe.start_armed &&
      mower_auth_phase(authority)==MOWER_AUTH_CH4;
  for (unsigned i=0;i<2U;++i) {
    Exp3UiState state=probes[i]->state;
    if (state==EXP3_UI_WAIT || state==EXP3_UI_CANCEL_WAIT || state==EXP3_UI_REJECTED ||
        state==EXP3_UI_TIMEOUT || state==EXP3_UI_ACCEPTED) {
      mowerUnlockDisplay=i==0U;
      if (state==EXP3_UI_ACCEPTED) {
        const unsigned delta = ((runtime >> MOWER_FAST_HEARTBEAT_SHIFT) -
            (probes[i]->runtime_baseline >> MOWER_FAST_HEARTBEAT_SHIFT)) & MOWER_FAST_HEARTBEAT_MASK;
        if (probes[i]->completed || (known && delta && delta < MOWER_FAST_HEARTBEAT_FORWARD_LIMIT && (runtime & MOWER_FAST_RUN) &&
            !(runtime & MOWER_FAST_STOP_MASK) && (i==0U || (runtime&1U)))) {
          probes[i]->completed=1U; completed=true; continue;
        }
        if (static_cast<uint32_t>(now-probes[i]->sent_at)>=MOWER_AUTH_LIFETIME_MS) {
          exp3_board_restart_session(now);
          return EXP3_UI_TIMEOUT;
        }
        return EXP3_UI_WAIT;
      }
      if (state==EXP3_UI_CANCEL_WAIT && probes[i]->completed) continue;
      return state;
    }
  }
  if (completed) return EXP3_UI_IDLE;
  mowerUnlockDisplay=unlock;
  return displays[unlock?0U:1U];
#else

  Exp3BoardStatus status;
  Exp3UiInput input = {};
  Exp3UiOutput output;
  int32_t ch4 = channelOutputs[3] / 2 + PPM_CH_CENTER(3);
  exp3_board_status(&status);
  mowerExp3Bound = status.bound;
  if (!mowerExp3Initialized || status.controller != mowerExp3Controller ||
      status.hmi != mowerExp3Hmi) {
    exp3_ui_probe_init(&mowerExp3Probe);
    exp3_ui_probe_bind(&mowerExp3Probe, status.controller, status.hmi,
                       EXP3_PWM_CONTEXT, EXP3_PWM_TARGET, 1U);
    mowerExp3Controller = status.controller;
    mowerExp3Hmi = status.hmi;
    mowerExp3Initialized = true;
#if defined(PCBI6X_MOWER_BLADE_OK)
    mowerExp3WaitState = EXP3_UI_IDLE;
    exp3_ui_probe_init(&mowerUnlockProbe);
    exp3_ui_probe_bind(&mowerUnlockProbe, status.controller, status.hmi,
                       EXP3_UNLOCK_CONTEXT, EXP3_UNLOCK_TARGET, 1U);
    mowerActionSequence = 0U;
    mowerUnlockWaitState = EXP3_UI_IDLE;
    mowerUnlockNeedsCenter = true;
    mowerUnlockWasLocked = false;
    mowerUnlockCompleted = false;
    mowerBatteryNotice = mowerBatteryWasLow = false;
    mowerBladeDisplayCompleted = false;
    mowerBladePreviousRuntimeKnown = false;
#endif
  }
  if (ch4 < 0)
    ch4 = 0;
  else if (ch4 > 65535)
    ch4 = 65535;
  input.ch4_us = static_cast<uint16_t>(ch4);
  input.ok_pressed = (event == EVT_KEY_BREAK(KEY_ENTER));
  input.bound = status.bound;
  input.transport_waiting = status.waiting;
  input.cancel_ready = status.cancel_ready;
#if defined(PCBI6X_MOWER_BLADE_OK)
  mowerExp3CancelFailed = status.cancel_failed;
#endif
  input.now_ms = static_cast<uint32_t>(get_tmr10ms()) * 10U;
#if defined(PCBI6X_MOWER_BLADE_OK)
  uint16_t runtime = 0U;
  const bool runtimeKnown = exp3_board_runtime(input.now_ms, &runtime);
  input.ok_pressed = input.ok_pressed && runtimeKnown;
  const bool lockKnown = runtimeKnown;
  const bool locked = lockKnown && (runtime & MOWER_FAST_LOCKED);
  MowerUiSnapshot batteryData;
  mowerUiRead(batteryData);
  const bool lowBattery = batteryData.telemetry_online &&
      (batteryData.valid & MOWER_UI_FAULT) &&
      (batteryData.fault_bits & MOWER_BATTERY_BLADE_INHIBIT);
  const bool startPosition = ch4 >= 900 && ch4 <= 1100;
  const bool newStart = startPosition && !mowerExp3Probe.ch4_start && mowerExp3Probe.center_seen;
  const bool lowAttempt = newStart || (startPosition && event == EVT_KEY_BREAK(KEY_ENTER)) ||
      (!mowerBatteryWasLow && (mowerExp3Probe.state == EXP3_UI_PROMPT || mowerExp3Probe.state == EXP3_UI_WAIT));
  if (runtimeKnown && !locked && lowBattery && lowAttempt) {
    mowerBatteryNotice = true;
    mowerBatteryNoticeStarted = input.now_ms;
  }
  if (!runtimeKnown || locked || !lowBattery) mowerBatteryNotice = false;
  mowerBatteryWasLow = lowBattery;
  if (lowBattery) {
    // Discard the intent; only drain a Cancel for an already submitted action.
    if ((mowerExp3Probe.state == EXP3_UI_WAIT || mowerExp3Probe.state == EXP3_UI_ACCEPTED ||
         mowerExp3Probe.state == EXP3_UI_TIMEOUT) && mowerExp3Probe.pending.action != 0U)
      mowerExp3Probe.cancel_pending = 1U;
    mowerExp3Probe.start_armed = 0U;
  }
  if (!lockKnown || !locked || !mowerUnlockWasLocked)
    mowerUnlockNeedsCenter = true;
  if (locked && ch4 >= 1400 && ch4 <= 1600)
    mowerUnlockNeedsCenter = false;
  mowerUnlockWasLocked = locked;
  input.ok_pressed = input.ok_pressed && !locked;
  input.start_inhibited = !runtimeKnown || locked || lowBattery;
  mowerExp3Probe.next_action = mowerActionSequence;
  if (mowerExp3Probe.ch4_start && !(ch4 >= 900 && ch4 <= 1100) &&
      (mowerExp3Probe.state == EXP3_UI_WAIT || mowerExp3Probe.state == EXP3_UI_ACCEPTED ||
       mowerExp3Probe.state == EXP3_UI_TIMEOUT)) {
    mowerBladeDisplayCompleted = false;
    mowerBladeRuntimeBaseline = mowerBladePreviousRuntimeKnown ? mowerBladePreviousRuntime : runtime;
  }
  mowerBladePreviousRuntimeKnown = runtimeKnown;
  if (runtimeKnown) mowerBladePreviousRuntime = runtime;
#endif
  input.matches = status.matches;
  input.last_result = status.last_result;
  output = exp3_ui_probe_step(&mowerExp3Probe, &input);
#if defined(PCBI6X_MOWER_BLADE_OK)
  if (output.submit_kind == EXP3_UI_SUBMIT_CONFIRM) {
    mowerActionSequence = output.message.action;
    mowerBladeDisplayCompleted = false;
    mowerBladeRuntimeBaseline = runtime;
  }
#endif
  if (output.submit_kind != EXP3_UI_SUBMIT_NONE &&
      !exp3_board_submit(&output.message)) {
    if (output.submit_kind == EXP3_UI_SUBMIT_CANCEL) {
      mowerExp3Probe.cancel_pending = 1U;
      mowerExp3Probe.state = EXP3_UI_CANCEL_WAIT;
    }
    else {
      mowerExp3Probe.state = EXP3_UI_PROMPT;
    }
  }
#if defined(PCBI6X_MOWER_BLADE_OK)
  // Count every physical CH4 edge, but discard its prompt when feedback is stale.
  // Recovery while CH4 is held left must not resurrect that start intent.
  if (!runtimeKnown || locked || lowBattery) {
    if (mowerExp3Probe.state != EXP3_UI_WAIT &&
        mowerExp3Probe.state != EXP3_UI_ACCEPTED &&
        mowerExp3Probe.state != EXP3_UI_TIMEOUT)
      mowerExp3Probe.start_armed = 0U;
    if (mowerExp3Probe.state == EXP3_UI_PROMPT ||
        mowerExp3Probe.state == EXP3_UI_UNBOUND)
      mowerExp3Probe.state = EXP3_UI_IDLE;
  }
  /* The two intents have separate physical generations and share one wire action
   * sequence/mailbox. Keep tracking both even when one is not permitted. */
  exp3_board_status(&status);
  Exp3UiInput unlockInput = input;
  unlockInput.bound = status.bound;
  unlockInput.transport_waiting = status.waiting ||
      output.submit_kind != EXP3_UI_SUBMIT_NONE;
  unlockInput.cancel_ready = status.cancel_ready &&
      output.submit_kind == EXP3_UI_SUBMIT_NONE;
  unlockInput.start_inhibited = !locked || mowerUnlockNeedsCenter;
  unlockInput.ok_pressed = event == EVT_KEY_BREAK(KEY_ENTER) &&
      !unlockInput.start_inhibited;
  mowerUnlockProbe.next_action = mowerActionSequence;
  const bool unlockInFlight = mowerUnlockProbe.state == EXP3_UI_WAIT ||
      mowerUnlockProbe.state == EXP3_UI_ACCEPTED || mowerUnlockProbe.state == EXP3_UI_TIMEOUT;
  if (lockKnown && !locked && (unlockInFlight ||
      mowerUnlockProbe.state == EXP3_UI_CANCEL_WAIT)) mowerUnlockCompleted = true;
  if ((!locked || mowerUnlockNeedsCenter) && !unlockInFlight)
    mowerUnlockProbe.start_armed = 0U;
  Exp3UiOutput unlockOutput = exp3_ui_probe_step(&mowerUnlockProbe, &unlockInput);
  if (!locked || mowerUnlockNeedsCenter) {
    if (!unlockInFlight) mowerUnlockProbe.start_armed = 0U;
    if (mowerUnlockProbe.state == EXP3_UI_PROMPT ||
        mowerUnlockProbe.state == EXP3_UI_UNBOUND)
      mowerUnlockProbe.state = EXP3_UI_IDLE;
  }
  if (unlockOutput.submit_kind == EXP3_UI_SUBMIT_CONFIRM) {
    mowerActionSequence = unlockOutput.message.action;
    mowerUnlockCompleted = false;
  }
  if (mowerUnlockProbe.state == EXP3_UI_ACCEPTED) mowerUnlockCompleted = true;
  if (unlockOutput.submit_kind != EXP3_UI_SUBMIT_NONE &&
      !exp3_board_submit(&unlockOutput.message)) {
    if (unlockOutput.submit_kind == EXP3_UI_SUBMIT_CANCEL) {
      mowerUnlockProbe.cancel_pending = 1U;
      mowerUnlockProbe.state = EXP3_UI_CANCEL_WAIT;
    }
    else mowerUnlockProbe.state = EXP3_UI_REJECTED;
  }
  if (mowerUnlockProbe.state == EXP3_UI_REJECTED) {
    mowerUnlockNeedsCenter = true;
    mowerUnlockProbe.start_armed = 0U;
  }
  const Exp3UiState unlockState = mowerUnlockProbe.state;
  if (unlockState != mowerUnlockWaitState) {
    mowerUnlockWaitState = unlockState;
    mowerUnlockWaitStarted = input.now_ms;
  }
  mowerUnlockDisplay = unlockState != EXP3_UI_IDLE && unlockState != EXP3_UI_ACCEPTED;
  // Once unlock is observed, keep receipt/cancel drainage silent even on CH4
  // release. The transaction stays busy and cannot authorize a new OK.
  if (mowerUnlockCompleted && (unlockState == EXP3_UI_WAIT ||
      unlockState == EXP3_UI_CANCEL_WAIT)) mowerUnlockDisplay = false;
  if (mowerUnlockDisplay) {
    if ((unlockState == EXP3_UI_WAIT || unlockState == EXP3_UI_CANCEL_WAIT) &&
        static_cast<uint32_t>(input.now_ms - mowerUnlockWaitStarted) >= 2000U)
      return EXP3_UI_IDLE;
    return unlockState;
  }
  if (mowerBatteryNotice) {
    if (static_cast<uint32_t>(input.now_ms - mowerBatteryNoticeStarted) < 2000U)
      return EXP3_UI_BATTERY_LOW;
    mowerBatteryNotice = false;
  }
  const Exp3UiState state = mowerExp3Probe.state;
  if (runtimeKnown) {
    const uint16_t delta = ((runtime >> MOWER_FAST_HEARTBEAT_SHIFT) -
        (mowerBladeRuntimeBaseline >> MOWER_FAST_HEARTBEAT_SHIFT)) & MOWER_FAST_HEARTBEAT_MASK;
    if (delta && delta < MOWER_FAST_HEARTBEAT_FORWARD_LIMIT &&
        ((state == EXP3_UI_CANCEL_WAIT && !(runtime & 1U)) ||
         (mowerExp3Probe.pending.action && (runtime & 1U) &&
          (state == EXP3_UI_WAIT || state == EXP3_UI_ACCEPTED || state == EXP3_UI_TIMEOUT))))
      mowerBladeDisplayCompleted = true;
  }
  if (state != mowerExp3WaitState) {
    mowerExp3WaitState = state;
    mowerExp3WaitStarted = input.now_ms;
  }
  // A fresh center-to-start edge can show the next prompt immediately while
  // the old Cancel completes. Keep the probe in CANCEL_WAIT: OK is ignored
  // until that transaction drains, and is never queued for later execution.
  if (runtimeKnown && mowerExp3Probe.start_armed &&
      state == EXP3_UI_CANCEL_WAIT)
    return EXP3_UI_PROMPT;
  // Runtime changes close only the display. Matching receipts still own busy
  // state, and a new physical edge plus a new OK are required for every start.
  if (mowerBladeDisplayCompleted &&
      (state == EXP3_UI_CANCEL_WAIT || state == EXP3_UI_WAIT || state == EXP3_UI_TIMEOUT))
    return EXP3_UI_IDLE;
  // Bound only the modal display. Never clear/reissue the pending transaction.
  if ((state == EXP3_UI_WAIT || state == EXP3_UI_CANCEL_WAIT) &&
      static_cast<uint32_t>(input.now_ms - mowerExp3WaitStarted) >= 2000U)
    return EXP3_UI_IDLE;
#endif
  return mowerExp3Probe.state;
#endif
}

#if defined(PCBI6X_MOWER_BLADE_OK)
void mowerUiBladePoll()
{
  // The GUI task tracks CH4 even while a radio/model menu is open.
  // Confirm still requires an explicit OK on a mower page.
  mowerExp3DisplayState = mowerExp3Step(0);
}
#endif

#if defined(PCBI6X_MOWER_BLADE_OK)
static coord_t mowerZhText(coord_t x, coord_t y, const char *text, bool inverse=false);
static void mowerZhCenteredText(coord_t y, const char *text);
#endif

static void mowerExp3Overlay(Exp3UiState state)
{
#if defined(PCBI6X_MOWER_BLADE_OK)
  if (state == EXP3_UI_IDLE || state == EXP3_UI_ACCEPTED) return;
  if (state != EXP3_UI_PROMPT) {
    const char *notice = state == EXP3_UI_BATTERY_LOW ? "电量过低" :
        state == EXP3_UI_SAFETY ? "安全输入未释放" :
        state == EXP3_UI_OWNER ? "控制权未就绪" :
        state == EXP3_UI_TIMEOUT ? "无回执" :
        state == EXP3_UI_REJECTED ? (mowerUnlockDisplay ? "解锁被拒绝" : "启动被拒绝") : 0;
    if (notice) mowerZhCenteredText(52, notice);
    return;
  }
  // Keep the whole status row; clear whole body rows instead of half glyphs.
  lcdDrawSolidFilledRect(0, 12, 128, 52, ERASE);
  lcdDrawRect(5, 18, 118, 31);
  mowerZhCenteredText(20, mowerUnlockDisplay ? "是否解锁" : "确认启动割刀");
  mowerZhCenteredText(34, "按OK确认");

#else

#if defined(PCBI6X_MOWER_BLADE_OK)
  if (state == EXP3_UI_ACCEPTED) return; // The home snapshot supplies current runtime state.
#endif
  if (state == EXP3_UI_IDLE)
    return;
  lcdDrawSolidFilledRect(5, 18, 118, 31, ERASE);
  lcdDrawRect(5, 18, 118, 31);
  if (state == EXP3_UI_PROMPT) {
    lcdDrawText(14, 23, "START BLADE?", INVERS);
#if defined(PCBI6X_MOWER_BLADE_OK)
    if (mowerExp3Probe.cancel_pending || mowerExp3Probe.state == EXP3_UI_CANCEL_WAIT)
      lcdDrawText(15, 37, mowerExp3CancelFailed ||
          static_cast<uint32_t>(get_tmr10ms()*10U - mowerExp3Probe.sent_at) >= 10000U ?
          "STOP NO REPLY" : "STOPPING...", SMLSIZE);
    else
#endif
      lcdDrawText(75, 37, "> OK", BLINK);
  }
  else if (state == EXP3_UI_WAIT)
    lcdDrawText(27, 29, "WAIT RESULT", INVERS);
  else if (state == EXP3_UI_CANCEL_WAIT)
    lcdDrawText(27, 29, "WAIT CANCEL", INVERS);
  else if (state == EXP3_UI_ACCEPTED) {
    lcdDrawText(22, 23, "TEST ACCEPTED", INVERS);
    lcdDrawText(22, 37, "PWM VIA CH7", SMLSIZE);
  }
  else if (state == EXP3_UI_UNBOUND)
    lcdDrawText(21, 29, MOWER_BLADE_OK_ENABLED ? "LINK NOT READY" : "EXP3 UNBOUND", INVERS);
  else if (state == EXP3_UI_TIMEOUT)
    lcdDrawText(28, 29, MOWER_BLADE_OK_ENABLED ? "NO REPLY" : "EXP3 TIMEOUT", INVERS);
  else
    lcdDrawText(24, 29, MOWER_BLADE_OK_ENABLED ? "START DENIED" : "EXP3 REJECT", INVERS);
#endif
}
#endif

static void mowerPage(uint8_t page, event_t event)
{
  MowerUiSnapshot data;
  mowerUiRead(data);
  lcdDrawText(0, 0, data.mock ? "MOWER DEMO" : "MOWER", INVERS);
#if defined(PCBI6X_EXP3_PWM_HIL) || defined(PCBI6X_MOWER_BLADE_OK)
  lcdDrawText(34, 0, mowerExp3Bound ? (MOWER_BLADE_OK_ENABLED ? "READY" : "E3 READY") : (MOWER_BLADE_OK_ENABLED ? "SYNC" : "E3 SYNC"), SMLSIZE);
#endif
  lcdDrawText(76, 0, "TX", SMLSIZE);
  lcdDrawNumber(111, 0, data.radio_battery_100mv, SMLSIZE | RIGHT);
  lcdDrawText(112, 0, "dV", SMLSIZE);
  lcdDrawText(76, 8, "RQ", SMLSIZE);
  lcdDrawNumber(120, 8, data.radio_quality, SMLSIZE | RIGHT);
  if (page == VIEW_MOWER) {
    mowerValue(0, 8, "SOC%", data.soc_percent, data.valid & MOWER_UI_SOC);
    mowerValue(0, 16, "BAT cV", data.voltage_centivolts, data.valid & MOWER_UI_VOLTAGE);
    mowerSpeedSetting();
    mowerValue(68, 24, "ACT", data.walk_actual_cms, data.valid & MOWER_UI_WALK_ACTUAL);
#if defined(PCBI6X_MOWER_BLADE_OK)
    {
      uint16_t runtime=0U;
      bool known=exp3_board_runtime(static_cast<uint32_t>(get_tmr10ms())*10U, &runtime);
      lcdDrawText(0, 32, "BLADE OUTPUT", SMLSIZE);
      lcdDrawText(67, 32, !known ? "UNKNOWN" : (runtime&1U) ? "ENABLED" : "OFF", SMLSIZE);
      lcdDrawText(0, 40, "CH7 GEAR", SMLSIZE);
      const char *gear = !known ? "--" : !(runtime&1U) ? "OFF" :
          ((runtime>>1)&3U)==1U ? "LOW" : ((runtime>>1)&3U)==2U ? "MID" : "HIGH";
      lcdDrawText(67, 40, gear, SMLSIZE);
    }
#else
    mowerValue(0, 32, "B SET", data.blade_set_rpm, data.valid & MOWER_UI_BLADE_SET);
    mowerValue(68, 32, "RPM", data.blade_actual_rpm, data.valid & MOWER_UI_BLADE_RPM);
    mowerValue(0, 40, "H TGT", data.height_target, data.valid & MOWER_UI_HEIGHT_TARGET);
    lcdDrawText(78, 40, "MOVE", SMLSIZE);
    if (data.valid & MOWER_UI_HEIGHT_ACTION)
      lcdDrawNumber(120, 40, data.height_action, SMLSIZE | RIGHT);
    else
      lcdDrawText(108, 40, "--", SMLSIZE);
#endif
    lcdDrawText(0, 48, "LINK", SMLSIZE);
    lcdDrawText(23, 48, data.telemetry_online ? "TLM" : "--", SMLSIZE);
    lcdDrawText(78, 48, "RUN", SMLSIZE);
    lcdDrawText(100, 48, !(data.valid & MOWER_UI_RUN_PERMISSION) ? "--" :
                data.run_permission ? "YES" : "NO", SMLSIZE);
    lcdDrawText(0, 56, "FAULT", SMLSIZE);
    if (data.valid & MOWER_UI_FAULT)
      lcdDrawNumber(46, 56, data.fault_bits, SMLSIZE | RIGHT);
    else
      lcdDrawText(34, 56, "--", SMLSIZE);
    if ((data.valid & MOWER_UI_FAULT) && data.fault_bits) {
      const char *reason = "FAULT!";
      if (data.fault_bits & 0x0001U)
        reason = "E-STOP";
      else if (data.fault_bits & 0x0002U)
        reason = "BUMP";
      else if (data.fault_bits & 0x0004U)
        reason = "TILT";
      else if (data.fault_bits & 0x0008U)
        reason = "IBUS";
      lcdDrawText(44, 48, reason, SMLSIZE | INVERS);
    }
    else {
      mowerIcon();
    }
  }
  else if (page == VIEW_MOWER_DATA) {
    lcdDrawText(0, 8, "DATA", INVERS);
    mowerValue(0, 16, "SOC%", data.soc_percent, data.valid & MOWER_UI_SOC);
    mowerValue(67, 16, "cV", data.voltage_centivolts, data.valid & MOWER_UI_VOLTAGE);
    mowerSpeedSetting();
    mowerValue(67, 24, "ACT", data.walk_actual_cms, data.valid & MOWER_UI_WALK_ACTUAL);
    mowerValue(0, 32, "B SET", data.blade_set_rpm, data.valid & MOWER_UI_BLADE_SET);
    mowerValue(67, 32, "RPM", data.blade_actual_rpm, data.valid & MOWER_UI_BLADE_RPM);
    mowerValue(0, 40, "H TGT", data.height_target, data.valid & MOWER_UI_HEIGHT_TARGET);
    mowerValue(67, 40, "MOVE", data.height_action, data.valid & MOWER_UI_HEIGHT_ACTION);
    lcdDrawText(0, 48, "MODE", SMLSIZE);
    lcdDrawText(35, 48, !(data.valid & MOWER_UI_WORK_MODE) ? "--" :
                data.work_mode ? "REMOTE" : "OFF", SMLSIZE);
    lcdDrawText(0, 56, "BAG", SMLSIZE);
    lcdDrawText(35, 56, "UNKNOWN", SMLSIZE);
  }
  else {
    lcdDrawText(0, 8, "FAULT DETAILS", INVERS);
    if (data.valid & MOWER_UI_FAULT) {
      for (uint8_t i = 0; i < 10; ++i) {
        uint8_t column = i / 5;
        uint8_t row = i % 5;
        coord_t x = column ? 66 : 0;
        coord_t y = 16 + row * 8;
        static const char *const names[10] = {
          "E-STOP", "BUMP", "TILT", "IBUS", "R-STOP",
          "OWNER", "BMS", "WHEEL", "BLADE", "CRC"
        };
        lcdDrawText(x, y, names[i], SMLSIZE);
        lcdDrawText(x + 45, y, (data.fault_bits & (1U << i)) ? "!" : "-", SMLSIZE);
      }
    }
    else
      lcdDrawText(0, 24, "FAULT UNKNOWN", SMLSIZE);
    lcdDrawText(0, 56, "LEFT/RIGHT  PAGE", SMLSIZE);
  }
#if defined(PCBI6X_EXP3_PWM_HIL) || defined(PCBI6X_MOWER_BLADE_OK)
  mowerExp3Overlay(mowerExp3DisplayState);
#else
  (void)event;
#endif
}
#if defined(PCBI6X_MOWER_BLADE_OK)
#include "mower_user_ui.h"
#endif
#endif

#if defined(PCBX7)
  #define EVT_KEY_CONTEXT_MENU           EVT_KEY_LONG(KEY_ENTER)
  #define EVT_KEY_NEXT_VIEW              EVT_KEY_BREAK(KEY_PAGE)
  #define EVT_KEY_NEXT_PAGE              EVT_ROTARY_RIGHT
  #define EVT_KEY_PREVIOUS_PAGE          EVT_ROTARY_LEFT
  #define EVT_KEY_MODEL_MENU             EVT_KEY_BREAK(KEY_MENU)
  #define EVT_KEY_GENERAL_MENU           EVT_KEY_LONG(KEY_MENU)
  #define EVT_KEY_TELEMETRY              EVT_KEY_LONG(KEY_PAGE)
#elif defined(PCBXLITE)
  #define EVT_KEY_CONTEXT_MENU           EVT_KEY_LONG(KEY_ENTER)
  #define EVT_KEY_PREVIOUS_VIEW          EVT_KEY_BREAK(KEY_UP)
  #define EVT_KEY_NEXT_VIEW              EVT_KEY_BREAK(KEY_DOWN)
  #define EVT_KEY_NEXT_PAGE              EVT_KEY_BREAK(KEY_RIGHT)
  #define EVT_KEY_PREVIOUS_PAGE          EVT_KEY_BREAK(KEY_LEFT)
  #define EVT_KEY_MODEL_MENU             EVT_KEY_LONG(KEY_RIGHT)
  #define EVT_KEY_GENERAL_MENU           EVT_KEY_LONG(KEY_LEFT)
  #define EVT_KEY_TELEMETRY              EVT_KEY_LONG(KEY_DOWN)
  #define EVT_KEY_STATISTICS             EVT_KEY_LONG(KEY_UP)
#else
  #define EVT_KEY_CONTEXT_MENU           EVT_KEY_BREAK(KEY_MENU)
  #define EVT_KEY_PREVIOUS_VIEW          EVT_KEY_BREAK(KEY_UP)
  #define EVT_KEY_NEXT_VIEW              EVT_KEY_BREAK(KEY_DOWN)
  #define EVT_KEY_NEXT_PAGE              EVT_KEY_BREAK(KEY_RIGHT)
  #define EVT_KEY_PREVIOUS_PAGE          EVT_KEY_BREAK(KEY_LEFT)
  #if defined(PCBI6X)
    #define EVT_KEY_GENERAL_MENU           EVT_KEY_LONG(KEY_RIGHT)
    #define EVT_KEY_MODEL_MENU             EVT_KEY_LONG(KEY_MENU)
  #else
    #define EVT_KEY_GENERAL_MENU           EVT_KEY_LONG(KEY_LEFT)
    #define EVT_KEY_MODEL_MENU             EVT_KEY_LONG(KEY_RIGHT)
    #define EVT_KEY_LAST_MENU              EVT_KEY_LONG(KEY_MENU)
  #endif
  #define EVT_KEY_TELEMETRY              EVT_KEY_LONG(KEY_DOWN)
  #define EVT_KEY_STATISTICS             EVT_KEY_LONG(KEY_UP)
#endif

void onMainViewMenu(const char *result)
{
  if (result == STR_RESET_TIMER1) {
    timerReset(0);
  }
  else if (result == STR_RESET_TIMER2) {
    timerReset(1);
  }
#if TIMERS > 2
  else if (result == STR_RESET_TIMER3) {
    timerReset(2);
  }
#endif
#if defined(SDCARD)
  else if (result == STR_VIEW_NOTES) {
    pushModelNotes();
  }
#endif
  else if (result == STR_RESET_SUBMENU) {
    POPUP_MENU_START(onMainViewMenu, 5, STR_RESET_FLIGHT, STR_RESET_TIMER1, STR_RESET_TIMER2, STR_RESET_TIMER3, STR_RESET_TELEMETRY);
  }
#if defined(TELEMETRY_FRSKY)
  else if (result == STR_RESET_TELEMETRY) {
    telemetryReset();
  }
#endif
  else if (result == STR_RESET_FLIGHT) {
    flightReset();
  }
  else if (result == STR_STATISTICS) {
    chainMenu(menuStatisticsView);
  }
#if defined(PWR_BUTTON_SWITCH) && !defined(PWR_BUTTON_EMULATED)
  else if (result == STR_SAVEALLDATA) {
    watchdogSuspend(200); // 2s
    saveAllData();
  }
#endif
#if !defined(PCBI6X)
  else if (result == STR_ABOUT_US) {
    chainMenu(menuAboutView);
  }
#endif
}

// =============================================================================
// 主界面菜单循环（开机默认显示的界面）
// 按 DOWN 短按切换视图（view_base 取值）：
//   VIEW_INPUTS        : 摇杆+电位器+开关（默认）
//   VIEW_INPUTS|ALT    : 逻辑开关状态
//   VIEW_OUTPUTS_VALUES: 通道数值（可翻页）
//   VIEW_OUTPUTS_BARS  : 通道条形图（可翻页）
//   VIEW_TIMER2        : 第二计时器（大字）
//   VIEW_CHAN_MONITOR  : 通道监视器（此视图不画顶栏/底栏公共信息）
// 公共信息（除通道监视器外所有视图都画）：模型名、电池电压、计时器1、
//   飞行模式名、微调、RSSI 条、RX 电压、会话计时。
// =============================================================================
void menuMainView(event_t event)
{
#if defined(PCBI6X) && defined(PCBI6X_MOWER_UI_SHELL) && defined(PCBI6X_MOWER_BLADE_OK)
  mowerUserMain(event);
  return;
#endif
  uint8_t view = g_eeGeneral.view;
  uint8_t view_base = view & 0x0f;

  #define CHANNELS_PAGES ((MAX_OUTPUT_CHANNELS + 8 - 1) / 8)
  #define CHANNELS_SEGMENT_WIDTH (54 / CHANNELS_PAGES)

#if defined(PCBI6X_EXP3_PWM_HIL) || defined(PCBI6X_MOWER_BLADE_OK)
  {
    bool mowerPageVisible = view_base <= VIEW_MOWER_FAULT;
    // Keep CH4 edge/cancel state current on every main-view page. Only the
    // mower pages may turn an OK key event into an experimental Confirm.
    mowerExp3DisplayState = mowerExp3Step(mowerPageVisible ? event : 0);
    if (mowerPageVisible)
      event = exp3_ui_main_event(event, EVT_KEY_BREAK(KEY_ENTER),
                                 mowerExp3DisplayState);
  }
#endif

  switch (event) {
    case EVT_ENTRY:
      killEvents(KEY_EXIT);
      killEvents(KEY_UP);
      killEvents(KEY_DOWN);
      break;

    /* TODO if timer2 is OFF, it's possible to use this timer2 as in er9x...
    case EVT_KEY_BREAK(KEY_MENU):
      if (view_base == VIEW_TIMER2) {
        Timer2_running = !Timer2_running;
        AUDIO_KEY_PRESS();
      }
    break;
    */

    case EVT_KEY_NEXT_PAGE:
    case EVT_KEY_PREVIOUS_PAGE:
#if defined(PCBI6X) && defined(PCBI6X_MOWER_UI_SHELL)
      if (view_base <= VIEW_MOWER_FAULT) {
        g_eeGeneral.view = (view_base + 3 +
            (event == EVT_KEY_NEXT_PAGE ? 1 : -1)) % 3;
        break;
      }
#endif
      if (view_base == VIEW_INPUTS)
        g_eeGeneral.view ^= ALTERNATE_VIEW;
      else
        g_eeGeneral.view = (g_eeGeneral.view + (CHANNELS_PAGES*ALTERNATE_VIEW) +
                           ((event==EVT_KEY_PREVIOUS_PAGE) ? -ALTERNATE_VIEW : ALTERNATE_VIEW)) %
                          (CHANNELS_PAGES*ALTERNATE_VIEW);
      break;

    case EVT_KEY_CONTEXT_MENU:
      killEvents(event);
#if defined(PCBI6X)
      if (view_base == VIEW_CHAN_MONITOR) break;
#endif

#if defined(PWR_BUTTON_SWITCH) && !defined(PWR_BUTTON_EMULATED)
      POPUP_MENU_START(onMainViewMenu, 3, STR_SAVEALLDATA, STR_RESET_SUBMENU, STR_STATISTICS);
#else
      POPUP_MENU_START(onMainViewMenu, 2, STR_RESET_SUBMENU, STR_STATISTICS);
#endif
      break;

#if MENUS_LOCK != 2 /*no menus*/
#if defined(EVT_KEY_LAST_MENU)
    case EVT_KEY_LAST_MENU:
      pushMenu(lastPopMenu());
      killEvents(event);
      break;
#endif

    CASE_EVT_ROTARY_BREAK
    case EVT_KEY_MODEL_MENU:
      pushMenu(menuModelSelect);
      killEvents(event);
      break;

    CASE_EVT_ROTARY_LONG
    case EVT_KEY_GENERAL_MENU:
      pushMenu(menuTabGeneral[0]);
      killEvents(event);
      break;
#endif

#if defined(EVT_KEY_PREVIOUS_VIEW)
      // TODO try to split those 2 cases on 9X
    case EVT_KEY_PREVIOUS_VIEW:
    case EVT_KEY_NEXT_VIEW:
      // TODO try to split those 2 cases on 9X
      g_eeGeneral.view = (event == EVT_KEY_PREVIOUS_VIEW ? (view_base == VIEW_COUNT - 1 ? 0 : view_base + 1) : (view_base == 0 ? VIEW_COUNT - 1 : view_base - 1));
      // storageDirty(EE_GENERAL); // do not immediate store view change to prevent eeprom wear
      break;
#else
    case EVT_KEY_NEXT_VIEW:
      g_eeGeneral.view = (view_base == 0 ? VIEW_COUNT - 1 : view_base - 1);
      // storageDirty(EE_GENERAL); // do not immediate store view change to prevent eeprom wear
      break;
#endif

#if defined(EVT_KEY_STATISTICS)
    case EVT_KEY_STATISTICS:
      chainMenu(menuStatisticsView);
      killEvents(event);
      break;
#endif

    case EVT_KEY_TELEMETRY:
#if defined(TELEMETRY_FRSKY)
      chainMenu(menuViewTelemetry);
#else
      chainMenu(menuStatisticsDebug);
#endif
      killEvents(event);
      break;

    case EVT_KEY_FIRST(KEY_EXIT):
#if defined(GVARS) && !defined(PCBSTD)
      if (gvarDisplayTimer > 0) {
        gvarDisplayTimer = 0;
      }
#endif
      break;
  }

  switch (view_base) {
#if defined(PCBI6X) && defined(PCBI6X_MOWER_UI_SHELL)
    case VIEW_MOWER:
    case VIEW_MOWER_DATA:
    case VIEW_MOWER_FAULT:
      mowerPage(view_base, event);
      break;
#endif
    case VIEW_CHAN_MONITOR:
      menuChannelsViewCommon(event);
      break;
    case VIEW_OUTPUTS_VALUES:
    case VIEW_OUTPUTS_BARS:
      // ---- 通道页（按 RIGHT/LEFT 翻页，每页 8 个通道）----
      // y=34 处画一条点状滚动条，指示当前页码
      // scroll bar
      lcdDrawHorizontalLine(38, 34, 54, DOTTED);
      lcdDrawSolidHorizontalLine(38 + (g_eeGeneral.view / ALTERNATE_VIEW) * CHANNELS_SEGMENT_WIDTH, 34, CHANNELS_SEGMENT_WIDTH, SOLID);
      for (uint32_t i=0; i<8; i++) {
        uint8_t x0, y0;
        uint8_t chan = 8 * (g_eeGeneral.view / ALTERNATE_VIEW) + i;
        int16_t val = channelOutputs[chan];

        if (view_base == VIEW_OUTPUTS_VALUES) {
          // 通道数值视图：4列x2行 文本，列x≈27/54/81/108，行 y=40/48
          x0 = (i % 4 * 9 + 3) * FW / 2;
          y0 = i / 4 * FH + 40;
          drawChannelValue(x0 + 4 * FW, y0, chan, val, RIGHT);
        }
        else {
          // 通道条形视图：中点竖线+点状底轨，左4条以 x=34 为中心，右4条以 x=94 为中心，
          // 行 y=38/43/48/53，条长按 ±512(或扩展限幅) 比例绘制
          constexpr coord_t WBAR2 =  (50/2);
          x0 = i<4 ? LCD_W/4+2 : LCD_W*3/4-2;
          y0 = 38+(i%4)*5;

          const uint16_t lim = (g_model.extendedLimits ? (512 * (long)LIMIT_EXT_PERCENT / 100) : 512) * 2;
          int8_t len = (abs(val) * WBAR2 + lim/2) / lim;

          if (len>WBAR2)
            len = WBAR2; // prevent bars from going over the end - comment for debugging
          lcdDrawHorizontalLine(x0-WBAR2, y0, WBAR2*2+1, DOTTED);
          lcdDrawSolidVerticalLine(x0, y0-2,5 );
          if (val > 0)
            x0 += 1;
          else
            x0 -= len;
          lcdDrawSolidHorizontalLine(x0, y0+1, len);
          lcdDrawSolidHorizontalLine(x0, y0-1, len);
        }
      }
      break;

    case VIEW_TIMER2:
      // 第二计时器：大字右对齐显示在 (87..125, y=40..47)
      drawTimerWithMode(87, 5 * FH, 1, RIGHT | DBLSIZE);
      break;

    case VIEW_INPUTS:
      if (view == VIEW_INPUTS) {
        // ---- 摇杆框 + 电位器竖条 ----
        // 不想显示两个摇杆框时，用下面这行替代 doMainScreenGraphics()（只保留电位器条）
        // 想恢复摇杆框：把下面一行换成 doMainScreenGraphics();
        //drawPotsBars();
        // doMainScreenGraphics();   // 左/右摇杆方框，(46,40) 与 (83,40)

        // Switches
#if defined(PCBTARANIS)
        uint8_t switches = min((int)NUM_SWITCHES, 6);
        for (int i = 0; i < switches; ++i) {
          if (SWITCH_EXISTS(i)) {
            uint8_t x = 2 * FW - 2, y = 4 * FH + i * FH + 1;
            if (i >= switches / 2) {
              x = 16 * FW + 1;
              y -= (switches / 2) * FH;
            }
            getvalue_t val = getValue(MIXSRC_FIRST_SWITCH + i);
            getvalue_t sw = ((val < 0) ? 3 * i + 1 : ((val == 0) ? 3 * i + 2 : 3 * i + 3));
            drawSwitch(x, y, sw, 0, false);
          }
        }
#elif defined(PCBI6X)
        // 开关状态显示：i6X 的 6 个开关分两列——
        //   左列 x=10，右列 x=97；行 y=33/41/49（第3个开关在右列底部 y=49）
        //   数组按 [SA,SD,SB,SC,S?,S?] 顺序对应每个开关的 (x,y) 坐标
        static const uint8_t x[NUM_SWITCHES] = {2 * FW - 2, 2 * FW - 2, 16 * FW + 1, 16 * FW + 1, 2 * FW - 2, 16 * FW + 1};
        static const uint8_t y[NUM_SWITCHES] = {4 * FH + 1, 5 * FH + 1,  4 * FH + 1,  5 * FH + 1, 6 * FH + 1,  6 * FH + 1};
        uint8_t switches = min((int)NUM_SWITCHES, 6);
        for (int i = 0; i < switches; ++i) {
          if (SWITCH_EXISTS(i)) {
            getvalue_t val = getValue(MIXSRC_FIRST_SWITCH + i);
            getvalue_t sw = ((val < 0) ? 3 * i + 1 : ((val == 0) ? 3 * i + 2 : 3 * i + 3));
            drawSwitch(x[i], y[i], sw, 0, false);
          }
        }
#endif

      }
      else {
        // Logical Switches
        uint8_t index = 0;
        uint8_t y = LCD_H - 20;
        for (uint8_t line = 0; line < 2; line++) {
          for (uint8_t column = 0; column < MAX_LOGICAL_SWITCHES / 2; column++) {
            int8_t len = getSwitch(SWSRC_SW1 + index) ? 10 : 1;
            uint8_t x = (16 + 3 * column);
            lcdDrawSolidVerticalLine(x - 1, y - len, len);
            lcdDrawSolidVerticalLine(x, y - len, len);
            index++;
          }
          y += 12;
        }
      }
      break;
  }

  if (view_base != VIEW_CHAN_MONITOR
#if defined(PCBI6X) && defined(PCBI6X_MOWER_UI_SHELL)
      && view_base != VIEW_MOWER && view_base != VIEW_MOWER_DATA &&
      view_base != VIEW_MOWER_FAULT
#endif
     ) {
    // ---- 以下为各视图共用的"公共信息"（除通道监视器外都画）----

    // 飞行模式名称：标准字显示在 (34, 16)
    uint8_t mode = mixerCurrentFlightMode;
    lcdDrawSizedText(PHASE_X, PHASE_Y, g_model.flightModeData[mode].name, sizeof(g_model.flightModeData[mode].name), ZCHAR | PHASE_FLAGS);

    // 模型名：大字显示在左上角 (10, 0)
    // 不想显示模型名时把下面这行注释掉即可（模型名未设置时会显示 "MODEL 01" 这类编号）
    // putsModelName(MODELNAME_X, MODELNAME_Y, g_model.header.name, g_eeGeneral.currModel, BIGSIZE);

    // 发射机电池电压（或低压告警）：屏幕最顶部、贴左边 (x=0..23, y=0..14)
    displayVoltageOrAlarm();

    // 计时器 1：大字右对齐显示在右上角 (125 右对齐, y=16..31)，下方小字显示计时模式
    drawTimerWithMode(125, 2 * FH, 0, RIGHT | DBLSIZE);

    // 四个微调滑条：屏幕四边（见 displayTrims 注释）
   // displayTrims(mode);

    // RSSI 信号条：屏幕中部 (34..49, 24..32)；有外置天线时另显示 "E"
    drawExternalAntennaAndRSSI();

#if defined(PCBI6X)
    // 接收机电压（遥测回传）：小字显示在左下角 (8, 51..57)
    drawReceiverVoltage();
#endif

    // 异常关机标记：左上角 (2,1) 显示闪烁的 '!'
    if (isAsteriskDisplayed()) {
      lcdDrawChar(REBOOT_X, 1, '!', INVERS | BLINK);
    }

#if defined(PWR_BUTTON_SWITCH) && !defined(PWR_BUTTON_EMULATED)
    // Add square icon in case of pending or ongoing eeprom write
    if (storageDirtyMsk || eepromIsWriting()) {
      lcdDrawRect(REBOOT_X + 2, 0 * FH, 4, 4);
    }
#endif
  }

#if defined(GVARS)
  if (gvarDisplayTimer > 0) {
    gvarDisplayTimer--;
    warningText = STR_GLOBAL_VAR;
    showMessageBox(warningText); // drawMessageBox(warningText);
    lcdDrawSizedText(16, 5 * FH, g_model.gvars[gvarLastChanged].name, LEN_GVAR_NAME, ZCHAR);
    lcdDrawText(16 + 6 * FW, 5 * FH, "[", BOLD);
    drawGVarValue(lcdLastRightPos, 5 * FH, gvarLastChanged, GVAR_VALUE(gvarLastChanged, getGVarFlightMode(mixerCurrentFlightMode, gvarLastChanged)),
                  LEFT | BOLD);
    if (g_model.gvars[gvarLastChanged].unit) {
      lcdDrawText(lcdLastRightPos, 5 * FH, "%", BOLD);
    }
    lcdDrawText(lcdLastRightPos, 5 * FH, "]", BOLD);
    warningText = nullptr;
  }
#endif

  if (view_base != VIEW_CHAN_MONITOR
#if defined(PCBI6X) && defined(PCBI6X_MOWER_UI_SHELL)
      && view_base != VIEW_MOWER && view_base != VIEW_MOWER_DATA &&
      view_base != VIEW_MOWER_FAULT
#endif
     ) {
    // 会话计时器（本次开机累计时长）：小字显示在底部中间 (53, 57)
    drawTimer(CLOCK_X, CLOCK_Y, sessionTimer, LEFT);
  }
}

#undef EVT_KEY_CONTEXT_MENU
#undef EVT_KEY_PREVIOUS_VIEW
#undef EVT_KEY_NEXT_VIEW
#undef EVT_KEY_NEXT_PAGE
#undef EVT_KEY_PREVIOUS_PAGE
#undef EVT_KEY_MODEL_MENU
#undef EVT_KEY_GENERAL_MENU
#undef EVT_KEY_LAST_MENU
#undef EVT_KEY_TELEMETRY
#undef EVT_KEY_STATISTICS
