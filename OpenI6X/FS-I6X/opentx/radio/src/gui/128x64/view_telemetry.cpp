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

// =============================================================================
// 遥测页面（主界面按键进入）
// 屏幕结构（自上而下）：
//   第 0 行 (y=0..7) 顶栏，整行反白：T1 时间或模型名 | TX电压 | 会话计时
//                     —— 见 lcd.cpp 的 drawTelemetryTopBar()
//   中部 (y=9..54)  自定义遥测屏幕，两种类型：
//     数字屏：2 列 x 4 行，标签+数值，x=63 竖线分隔
//     条形屏：最多 4 根横向条形图，x=25..126
//   底行 (y=57..63) RSSI 状态栏（RQly 数值 + 信号条，上方 y=55 有分隔线）
// UP/DOWN 切换多个遥测屏幕；长按 ENTER 弹出重置菜单；EXIT 返回主界面。
// =============================================================================
#define STATUS_BAR_Y     (7*FH+1)
uint8_t selectedTelemView = 0;

#define BAR_LEFT    25
#define BAR_WIDTH   100

// 底部 RSSI 状态栏：
//   有遥测流：y=55 画整宽分隔线；"RQly : 99" 小字右对齐至屏幕中线附近 (x≈62)，
//   右侧 (65,57) 起画 38x7 空框，从右往左填充实心条，长度 ∝ RSSI（4*rssi/11 像素），
//   低于告警阈值时填充改为点状
//   无遥测流：底行整行反白，居中显示闪烁的 "NO DATA"
void displayRssiLine()
{
  if (TELEMETRY_STREAMING()) {
    lcdDrawSolidHorizontalLine(0, 55, 128, 0); // separator
    uint8_t rssi;
    rssi = TELEMETRY_RSSI(); // min((uint8_t)99, TELEMETRY_RSSI());
    lcdDrawNumber(LCD_W/2 -2, STATUS_BAR_Y, rssi, LEADING0 | RIGHT | SMLSIZE, 2);
    lcdDrawText(lcdLastLeftPos,STATUS_BAR_Y, "RQly : ", RIGHT | SMLSIZE);
    lcdDrawRect(65, 57, 38, 7);
    uint8_t v = 4*rssi/11;
    lcdDrawFilledRect(66+36-v, 58, v, 5, (rssi < g_model.rfAlarms.warning) ? DOTTED : SOLID);
  }
  else {
    lcdDrawText(LCD_W/2, STATUS_BAR_Y, STR_NODATA, BLINK|CENTERED);
    lcdInvertLastLine();
  }
}

// 将 value 在 [min,max] 区间线性映射到 0..99（条形图填充像素数）
uint8_t barCoord(int16_t value, int16_t min, int16_t max)
{
  if (value <= min)
    return 0;
  else if (value >= max)
    return BAR_WIDTH-1;
  else
    return ((int32_t)(BAR_WIDTH-1) * (value - min)) / (max - min);
}

// 条形遥测屏：最多 4 根横向条形图（i=3..0，从下往上排布）。
//   第一根 y=11，之后每根间隔 11px（条高 5 + 间隙 6）；未配置的槽位会压缩间距。
//   每根：左端 (x=0) 小字信号源名 + 条形图区域 x=25..126，
//   条内每 25% 画一格刻度竖线；通道源按扩展量程换算。
//   末尾调用 displayRssiLine() 画底部 RSSI 栏；返回值表示是否还有空闲槽位。
bool displayGaugesTelemetryScreen(FrSkyScreenData & screen)
{
  // Custom Screen with gauges
  uint8_t barHeight = 5;
  for (int8_t i=3; i>=0; i--) {
    FrSkyBarData & bar = screen.bars[i];
    source_t source = bar.source;
    getvalue_t barMin = bar.barMin;
    getvalue_t barMax = bar.barMax;
    if (source <= MIXSRC_LAST_CH) {
      barMin = calc100toRESX(barMin);
      barMax = calc100toRESX(barMax);
    }
    if (source) {
      uint8_t y = barHeight+6+i*(barHeight+6);
      drawSource(0, y+barHeight/2-3, source, SMLSIZE);
      lcdDrawRect(BAR_LEFT, y, BAR_WIDTH+1, barHeight+2);
      getvalue_t value = getValue(source);
      uint8_t thresholdX = 0;
      uint8_t width = (barMin < barMax) ? barCoord(value, barMin, barMax) : 99 - barCoord(value, barMax, barMin);
      uint8_t barShade = SOLID;
      lcdDrawFilledRect(BAR_LEFT+1, y+1, width, barHeight, barShade);
      for (uint8_t j=24; j<99; j+=25) {
        if (j>thresholdX || j>width) {
          lcdDrawSolidVerticalLine(j*BAR_WIDTH/100+BAR_LEFT+1, y+1, barHeight);
        }
      }
      if (thresholdX) {
        lcdDrawVerticalLine(BAR_LEFT+1+thresholdX, y-2, barHeight+3, DOTTED);
        lcdDrawSolidHorizontalLine(BAR_LEFT+thresholdX, y-2, 3);
      }
    }
    else {
      barHeight += 2;
    }
  }
  displayRssiLine();
  return barHeight < 13;
}

// 数字遥测屏：2 列 x 4 行网格，x=63 画竖线分隔 (y=8..56)。
//   每个字段 = 标签(小字，左列 x=0 / 右列 x=65) + 数值(右对齐)。
//   行坐标 y=9/25/41（数值 MIDSIZE 中字）；第 4 行 (y≈57) 为小字行，
//   通常让位给底部 RSSI 栏（若配置了字段则继续在此显示数值）。
//   计时器字段显示为 "T1/T2" + DBLSIZE 时间；GPS 传感器用 DBLSIZE 专用显示；
//   日期时间传感器显示日期；数据超时的传感器数值反白闪烁。
bool displayNumbersTelemetryScreen(FrSkyScreenData & screen)
{
  // Custom Screen with numbers
  uint8_t fields_count = 0;
  lcdDrawSolidVerticalLine(63, 8, 48);
  for (uint32_t i=0; i<4; i++) {
    for (uint8_t j=0; j<NUM_LINE_ITEMS; j++) {
      source_t field = screen.lines[i].sources[j];
      if (field > 0) {
        fields_count++;
      }
      if (i==3) {
        if (!TELEMETRY_STREAMING() || (screen.lines[i].sources[0] == 0 && screen.lines[i].sources[1] == 0)) {
          displayRssiLine();
          return fields_count;
        }
      }
      if (field) {
        LcdFlags att = (i==3 ? RIGHT|NO_UNIT : RIGHT|MIDSIZE|NO_UNIT);
        coord_t pos[] = {0, 65, 130};
        if (field >= MIXSRC_FIRST_TIMER && field <= MIXSRC_LAST_TIMER && i!=3) {
          // there is not enough space on LCD for displaying "Tmr1" or "Tmr2" and still see the - sign, we write "T1" or "T2" instead
          drawStringWithIndex(pos[j], 1+FH+2*FH*i, "T", field-MIXSRC_FIRST_TIMER+1, 0);
          drawTimerWithMode(pos[j+1] + 2, 1+FH+2*FH*i, field - MIXSRC_FIRST_TIMER, RIGHT | DBLSIZE);
          continue;
        }
        else if (field >= MIXSRC_FIRST_TELEM && isGPSSensor(1+(field-MIXSRC_FIRST_TELEM)/3) && telemetryItems[(field-MIXSRC_FIRST_TELEM)/3].isAvailable()) {
          // we don't display GPS name, no space for it
          att = RIGHT|DBLSIZE|NO_UNIT;  //DBLSIZE ensure the telem screen specific display for GPS is used
        }
        else {
          drawSource(pos[j], 1+FH+2*FH*i, field, 0);
        }

        if (field >= MIXSRC_FIRST_TELEM) {
          TelemetryItem & telemetryItem = telemetryItems[(field-MIXSRC_FIRST_TELEM)/3]; // TODO macro to convert a source to a telemetry index
          if (!telemetryItem.isAvailable()) {
            continue;
          }
          else if (telemetryItem.isOld()) {
            att |= INVERS|BLINK;
          }
        }

        if (isSensorUnit(1+(field-MIXSRC_FIRST_TELEM)/3, UNIT_DATETIME) && field >= MIXSRC_FIRST_TELEM) {
          drawTelemScreenDate(pos[j+1]-36, 6+FH+2*FH*i, field, SMLSIZE|NO_UNIT);
        }
        else {
          drawSourceValue(pos[j+1]-2, (i==3 ? 1+FH+2*FH*i:FH+2*FH*i), field, att);
        }
      }
    }
  }
  lcdInvertLastLine();
  return fields_count;
}

bool displayCustomTelemetryScreen(uint8_t index)
{
#if defined(TELEMETRY_FRSKY)
  FrSkyScreenData & screen = g_model.frsky.screens[index];

  if (IS_BARS_SCREEN(selectedTelemView)) {
    return displayGaugesTelemetryScreen(screen);
  }

  displayNumbersTelemetryScreen(screen);
#endif
  return true;
}

void inavRun(event_t event);

// 遥测屏幕统一调度入口：
//   1) 若当前屏幕类型为脚本屏（i6X 编译时为 INAV 屏）则交给脚本/INAV 绘制
//   2) 类型为 NONE 返回 false（由调用方换下一个屏幕索引）
//   3) 否则画顶栏 drawTelemetryTopBar()，再绘制第 selectedTelemView 号自定义屏幕
bool displayTelemetryScreen()
{
#if defined(TELEMETRY_FRSKY)
#if defined(LUA)
  if (TELEMETRY_SCREEN_TYPE(selectedTelemView) == TELEMETRY_SCREEN_TYPE_SCRIPT) {
    uint8_t state = isTelemetryScriptAvailable(selectedTelemView);
    switch (state) {
      case SCRIPT_OK:
        return true;  // contents will be drawed by Lua Task
      case SCRIPT_NOFILE:
        return false;  // requested lua telemetry screen not available
      case SCRIPT_SYNTAX_ERROR:
      case SCRIPT_PANIC:
      case SCRIPT_KILLED:
        luaError(lsScripts, state, false);
        return true;
    }
    return false;
  }
#elif defined(PCBI6X_INAV)
  if (TELEMETRY_SCREEN_TYPE(selectedTelemView) == TELEMETRY_SCREEN_TYPE_SCRIPT) {
    inavRun(0xff);
    return true;
  }
#endif

  if (TELEMETRY_SCREEN_TYPE(selectedTelemView) == TELEMETRY_SCREEN_TYPE_NONE) {
    return false;
  }

  drawTelemetryTopBar();

  if (selectedTelemView < MAX_TELEMETRY_SCREENS) {
    return displayCustomTelemetryScreen(selectedTelemView);
  }


#endif
  return true;
}

enum NavigationDirection {
  NAVIGATION_DIRECTION_NONE,
  NAVIGATION_DIRECTION_UP,
  NAVIGATION_DIRECTION_DOWN
};
#define decrTelemetryScreen() direction = NAVIGATION_DIRECTION_UP
#define incrTelemetryScreen() direction = NAVIGATION_DIRECTION_DOWN

#if defined(PCBXLITE)
#define EVT_KEY_PREVIOUS_VIEW          EVT_KEY_LONG(KEY_LEFT)
#define EVT_KEY_NEXT_VIEW              EVT_KEY_LONG(KEY_RIGHT)
#elif defined(PCBX7)
#define EVT_KEY_PREVIOUS_VIEW          EVT_KEY_LONG(KEY_PAGE)
#define EVT_KEY_NEXT_VIEW              EVT_KEY_BREAK(KEY_PAGE)
#else
#define EVT_KEY_PREVIOUS_VIEW          EVT_KEY_FIRST(KEY_UP)
#define EVT_KEY_NEXT_VIEW              EVT_KEY_FIRST(KEY_DOWN)
#endif

// 遥测页面主循环：
//   EXIT：返回主界面；UP/DOWN（部分机型为 PAGE 键）切换上一个/下一个遥测屏幕；
//   长按 ENTER：弹出"重置遥测 / 重置飞行"菜单。
//   内部 for 循环逐个尝试屏幕索引（空屏幕跳过），直到画出一个有效屏幕；
//   若所有屏幕都为空：画顶栏 + 屏幕中部 (y=24) 居中显示 "No Telemetry Screens"，
//   并调用 displayRssiLine() 显示底栏。
void menuViewTelemetry(event_t event)
{
#if defined(TELEMETRY_FRSKY)
  enum NavigationDirection direction = NAVIGATION_DIRECTION_NONE;

  switch (event) {
    case EVT_KEY_FIRST(KEY_EXIT):
#if defined(LUA)
    case EVT_KEY_LONG(KEY_EXIT):
#endif
      killEvents(event);
      chainMenu(menuMainView);
      break;

    case EVT_KEY_PREVIOUS_VIEW:
      if (IS_KEY_LONG(EVT_KEY_PREVIOUS_VIEW)) {
        killEvents(event);
      }
      decrTelemetryScreen();
      break;

    case EVT_KEY_NEXT_VIEW:
      incrTelemetryScreen();
      break;

    case EVT_KEY_LONG(KEY_ENTER):
      killEvents(event);
      POPUP_MENU_START(onMainViewMenu, 2, STR_RESET_TELEMETRY, STR_RESET_FLIGHT);
      break;
  }

  for (int i=0; i<=TELEMETRY_SCREEN_TYPE_MAX; i++) {
    if (direction == NAVIGATION_DIRECTION_UP) {
      if (selectedTelemView-- == 0)
        selectedTelemView = TELEMETRY_VIEW_MAX;
    }
    else if (direction == NAVIGATION_DIRECTION_DOWN) {
      if (selectedTelemView++ == TELEMETRY_VIEW_MAX)
        selectedTelemView = 0;
    }
    else {
      direction = NAVIGATION_DIRECTION_DOWN;
    }
    if (displayTelemetryScreen()) {
      return;
    }
  }

  drawTelemetryTopBar();
  lcdDrawText(LCD_W / 2, 3 * FH, "No Telemetry Screens", CENTERED);
  displayRssiLine();
#endif
}

#undef EVT_KEY_PREVIOUS_VIEW
#undef EVT_KEY_NEXT_VIEW
