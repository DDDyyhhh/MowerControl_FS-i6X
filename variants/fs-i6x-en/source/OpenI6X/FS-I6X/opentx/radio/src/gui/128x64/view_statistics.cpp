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

#define STATS_1ST_COLUMN               1
#define STATS_2ND_COLUMN               7*FW+FW/2
#define STATS_3RD_COLUMN               14*FW+FW/2
#define STATS_LABEL_WIDTH              3*FW

// =============================================================================
// 统计页面：三列计时表 + 底部油门轨迹图
//   第1列 x=1：  "SES" 本次会话计时 (y=9)；"TOT" 累计总计时 (y=17，超1小时带小时位)
//   第2列 x=43： "THR" 油门开启累计时长 (y=1)；"TH%" 油门开启比例折算 (y=9)
//   第3列 x=87： "TM1/TM2" 模型计时器 (y=1/9/17，超1小时带小时位)
//   底部 (x=5, y=28..60)：油门轨迹直方图——每 0.5s 一根 1px 宽竖条，
//                         高 0..32px 表示油门量，每 6 根画一个刻度点
// UP/DOWN 切换到调试页；长按 MENU 重置总计时；EXIT 返回主界面。
// =============================================================================
void menuStatisticsView(event_t event)
{
  TITLE(STR_MENUSTAT);

  switch (event) {
    case EVT_KEY_FIRST(KEY_UP):
#if defined(PCBX7)
    case EVT_KEY_BREAK(KEY_PAGE):
#endif
      chainMenu(menuStatisticsDebug);
      break;

    case EVT_KEY_FIRST(KEY_DOWN):
#if defined(PCBX7)
    case EVT_KEY_LONG(KEY_PAGE):
      killEvents(event);
      chainMenu(menuStatisticsDebug2);
#else
      chainMenu(menuStatisticsDebug);
#endif
      break;

#if !defined(PCBTARANIS)
    case EVT_KEY_LONG(KEY_MENU): // historical
#endif
#if !defined(PCBI6X)
    case EVT_KEY_LONG(KEY_ENTER):
#endif
      g_eeGeneral.globalTimer = 0;
      storageDirty(EE_GENERAL);
      sessionTimer = 0;
      break;
    case EVT_KEY_FIRST(KEY_EXIT):
      chainMenu(menuMainView);
      break;
  }

  // Session and Total timers
  lcdDrawText(STATS_1ST_COLUMN, FH*1+1, "SES", BOLD);
  drawTimer(STATS_1ST_COLUMN + STATS_LABEL_WIDTH, FH*1+1, sessionTimer, 0, 0);
  lcdDrawText(STATS_1ST_COLUMN, FH*2+1, "TOT", BOLD);
  drawTimer(STATS_1ST_COLUMN + STATS_LABEL_WIDTH, FH*2+1, g_eeGeneral.globalTimer + sessionTimer, TIMEHOUR, 0);

  // Throttle special timers
  lcdDrawText(STATS_2ND_COLUMN, FH*0+1, "THR", BOLD);
  drawTimer(STATS_2ND_COLUMN + STATS_LABEL_WIDTH, FH*0+1, s_timeCumThr, 0, 0);
  lcdDrawText(STATS_2ND_COLUMN, FH*1+1, "TH%", BOLD);
  drawTimer(STATS_2ND_COLUMN + STATS_LABEL_WIDTH, FH*1+1, s_timeCum16ThrP/16, 0, 0);

  // Timers
  for (int i=0; i<TIMERS; i++) {
    drawStringWithIndex(STATS_3RD_COLUMN, FH*i+1, "TM", i+1, BOLD);
    if (timersStates[i].val > 3600)
      drawTimer(STATS_3RD_COLUMN + STATS_LABEL_WIDTH, FH*i+1, timersStates[i].val, TIMEHOUR, 0);
    else
      drawTimer(STATS_3RD_COLUMN + STATS_LABEL_WIDTH, FH*i+1, timersStates[i].val, 0, 0);
  }

#if defined(THRTRACE)
  const coord_t x = 5;
  const coord_t y = 60;
  lcdDrawSolidHorizontalLine(x-3, y, MAXTRACE+3+3);
  lcdDrawSolidVerticalLine(x, y-32, 32+3);
  for (coord_t i=0; i<MAXTRACE; i+=6) {
    lcdDrawSolidVerticalLine(x+i+6, y-1, 3);
  }

  uint16_t traceRd = s_traceWr > MAXTRACE ? s_traceWr - MAXTRACE : 0;
  for (coord_t i=1; i<=MAXTRACE && traceRd<s_traceWr; i++, traceRd++) {
    uint8_t h = s_traceBuf[traceRd % MAXTRACE];
    lcdDrawSolidVerticalLine(x+i, y-h, h);
  }
#endif
}

#define MENU_DEBUG_COL1_OFS          (11*FW-3)
#define MENU_DEBUG_COL2_OFS          (17*FW)

// 调试统计页（i6X 为单页版，自上而下逐行显示，行高 8px 从 y=9 开始）：
//   "Tlm RX Err"   遥测接收错误计数（启用 TLM_ERRORS 时）
//   "Free Mem"     剩余内存字节数（仅 Taranis 编译）
//   "Mixer max"    单次混算最长耗时（ms，两位小数）
//   "Free Stack"   菜单任务 / 混算任务剩余栈字节数
//   底行 (y=57) 整行反白显示重置提示。
//   长按 ENTER 重置计时；短按 ENTER 清零混算耗时记录。
void menuStatisticsDebug(event_t event)
{
#if defined(PCBI6X_SENS_HIL_DIAG)
  static uint8_t hilPage = 0;
  TITLE("SENS HIL");

  switch (event) {
    case EVT_KEY_FIRST(KEY_UP):
      hilPage = (hilPage + 4) % 5;
      break;
    case EVT_KEY_FIRST(KEY_DOWN):
      hilPage = (hilPage + 1) % 5;
      break;
    case EVT_KEY_FIRST(KEY_ENTER):
    case EVT_KEY_LONG(KEY_ENTER):
      resetFlySkySensHilDiagnostics();
      killEvents(event);
      break;
    case EVT_KEY_FIRST(KEY_EXIT):
      chainMenu(menuMainView);
      break;
  }

  const volatile FlySkySensHilDiagnostics &diag = flySkySensHilDiagnostics;
  uint8_t y = FH + 1;
  if (hilPage == 0) {
    lcdDrawText(0, y, "RX");
    lcdDrawNumber(7*FW, y, diag.rfRxTotal, RIGHT);
    lcdDrawText(8*FW, y, "OTH");
    lcdDrawNumber(LCD_W, y, diag.rfOtherType, RIGHT);
    y += FH;
    lcdDrawText(0, y, "AA");
    lcdDrawNumber(7*FW, y, diag.rfTypeAa, RIGHT);
    lcdDrawText(8*FW, y, "AC");
    lcdDrawNumber(LCD_W, y, diag.rfTypeAc, RIGHT);
    y += FH;
    lcdDrawText(0, y, "TX+");
    lcdDrawNumber(7*FW, y, diag.txIdMatch, RIGHT);
    lcdDrawText(8*FW, y, "TX-");
    lcdDrawNumber(LCD_W, y, diag.txIdMismatch, RIGHT);
    y += FH;
    lcdDrawText(0, y, "RX+");
    lcdDrawNumber(7*FW, y, diag.rxIdMatch, RIGHT);
    lcdDrawText(8*FW, y, "RX-");
    lcdDrawNumber(LCD_W, y, diag.rxIdMismatch, RIGHT);
    y += FH;
    lcdDrawText(0, y, "PEND");
    lcdDrawNumber(7*FW, y, diag.pendingSet, RIGHT);
    lcdDrawText(8*FW, y, "WAKE");
    lcdDrawNumber(LCD_W, y, diag.telemetryWakeup, RIGHT);
    y += FH;
    lcdDrawText(0, y, "FRAME");
    lcdDrawNumber(LCD_W, y, diag.flyskyFrameProcessed, RIGHT);
  }
  else if (hilPage == 1) {
    lcdDrawText(0, y, "AAP");
    lcdDrawNumber(7*FW, y, diag.aaProcessed, RIGHT);
    lcdDrawText(8*FW, y, "ACP");
    lcdDrawNumber(LCD_W, y, diag.acProcessed, RIGHT);
    y += FH;
    lcdDrawText(0, y, "SLOT");
    lcdDrawNumber(LCD_W, y, diag.sensorSlotValid, RIGHT);
    y += FH;
    lcdDrawText(0, y, "SET");
    lcdDrawNumber(LCD_W, y, diag.setTelemetryValueCalled, RIGHT);
    y += FH;
    lcdDrawText(0, y, "NEW");
    lcdDrawNumber(LCD_W, y, diag.newSensorCreated, RIGHT);
    y += FH;
    lcdDrawText(0, y, "TYPE");
    lcdDrawHexNumber(8*FW, y, diag.lastPacketType, LEFT);
  }
  else if (hilPage == 2) {
    lcdDrawText(0, y, "LTX");
    lcdDrawHexNumber(LCD_W-4*FWNUM-1, y,
      (diag.lastTxId[0] << 8) | diag.lastTxId[1], LEFT);
    lcdDrawHexNumber(LCD_W, y,
      (diag.lastTxId[2] << 8) | diag.lastTxId[3], RIGHT);
    y += FH;
    lcdDrawText(0, y, "ETX");
    lcdDrawHexNumber(LCD_W-4*FWNUM-1, y,
      (diag.expectedTxId[0] << 8) | diag.expectedTxId[1], LEFT);
    lcdDrawHexNumber(LCD_W, y,
      (diag.expectedTxId[2] << 8) | diag.expectedTxId[3], RIGHT);
    y += FH;
    lcdDrawText(0, y, "LRX");
    lcdDrawHexNumber(LCD_W-4*FWNUM-1, y,
      (diag.lastRxId[0] << 8) | diag.lastRxId[1], LEFT);
    lcdDrawHexNumber(LCD_W, y,
      (diag.lastRxId[2] << 8) | diag.lastRxId[3], RIGHT);
    y += FH;
    lcdDrawText(0, y, "ERX");
    lcdDrawHexNumber(LCD_W-4*FWNUM-1, y,
      (diag.expectedRxId[0] << 8) | diag.expectedRxId[1], LEFT);
    lcdDrawHexNumber(LCD_W, y,
      (diag.expectedRxId[2] << 8) | diag.expectedRxId[3], RIGHT);
  }
  else if (hilPage == 3) {
    lcdDrawText(0, y, "E1 RF");
    lcdDrawNumber(LCD_W, y, diag.e1Count, RIGHT);
    y += FH;
    lcdDrawText(0, y, "T1 10ms");
    if (diag.e1Count) lcdDrawNumber(LCD_W, y, diag.e1FirstTick, RIGHT);
    y += FH;
    lcdDrawText(0, y, "T2 10ms");
    if (diag.e1Count >= 2) lcdDrawNumber(LCD_W, y, diag.e1SecondTick, RIGHT);
    y += FH;
    lcdDrawText(0, y, "DT 10ms");
    if (diag.e1Count >= 2)
      lcdDrawNumber(LCD_W, y, diag.e1SecondTick - diag.e1FirstTick, RIGHT);
    y += FH;
    if (diag.e1Count) {
      lcdDrawText(0, y, diag.e1FirstType == 0xAA ? "1 AA I" : "1 AC I");
      lcdDrawHexNumber(8*FW, y, diag.e1FirstInstance, LEFT);
      lcdDrawHexNumber(LCD_W-4*FWNUM-1, y, diag.e1FirstRaw, LEFT);
    }
    y += FH;
    if (diag.e1Count >= 2) {
      lcdDrawText(0, y, diag.e1SecondType == 0xAA ? "2 AA I" : "2 AC I");
      lcdDrawHexNumber(8*FW, y, diag.e1SecondInstance, LEFT);
      lcdDrawHexNumber(LCD_W-4*FWNUM-1, y, diag.e1SecondRaw, LEFT);
    }
  }
  else {
    lcdDrawText(0, y, allowNewSensors ? "DISC ON" : "DISC OFF");
    lcdDrawText(8*FW, y, availableTelemetryIndex() >= 0 ? "FREE Y" : "FREE N");
    y += FH;
    lcdDrawText(0, y, "RF");
    lcdDrawNumber(7*FW, y, diag.e1Count, RIGHT);
    lcdDrawText(8*FW, y, "PAR");
    lcdDrawNumber(LCD_W, y, diag.e1Parsed, RIGHT);
    y += FH;
    lcdDrawText(0, y, "SET");
    lcdDrawNumber(7*FW, y, diag.e1Set, RIGHT);
    lcdDrawText(8*FW, y, "MAT");
    lcdDrawNumber(LCD_W, y, diag.e1Matched, RIGHT);
    y += FH;
    lcdDrawText(0, y, "OFF");
    lcdDrawNumber(7*FW, y, diag.e1DiscoveryOff, RIGHT);
    lcdDrawText(8*FW, y, "FULL");
    lcdDrawNumber(LCD_W, y, diag.e1Full, RIGHT);
    y += FH;
    lcdDrawText(0, y, "NEW");
    lcdDrawNumber(LCD_W, y, diag.e1Created, RIGHT);
  }

  lcdDrawNumber(LCD_W, 7*FH+1, hilPage + 1, RIGHT | INVERS);
#else
  TITLE(STR_MENUDEBUG);

  switch (event) {
    case EVT_KEY_LONG(KEY_ENTER):
      g_eeGeneral.globalTimer = 0;
      sessionTimer = 0;
      storageDirty(EE_GENERAL);
      killEvents(event);
      break;

    case EVT_KEY_FIRST(KEY_ENTER):
      maxMixerDuration = 0;
      break;

    case EVT_KEY_FIRST(KEY_UP):
#if defined(PCBX7)
    case EVT_KEY_BREAK(KEY_PAGE):
#endif
#if !defined(PCBI6X) // single debug screen
      chainMenu(menuStatisticsDebug2);
      return;
#endif

    case EVT_KEY_FIRST(KEY_DOWN):
#if defined(PCBX7)
    case EVT_KEY_LONG(KEY_PAGE):
#endif
      killEvents(event);
      chainMenu(menuStatisticsView);
      break;

    case EVT_KEY_FIRST(KEY_EXIT):
      chainMenu(menuMainView);
      break;
  }

  uint8_t y = FH + 1;

#if defined(PCBI6X) && defined(TLM_ERRORS) // single debug screen
  lcdDrawTextAlignedLeft(y, "Tlm RX Err");
  lcdDrawNumber(MENU_DEBUG_COL1_OFS + FW, y, telemetryErrors, RIGHT);
  y += FH;
#endif

#if defined(PCBTARANIS)
#if !defined(SIMU) && defined(DEBUG)
  lcdDrawTextAlignedLeft(y, "Usb");
  lcdDrawNumber(MENU_DEBUG_COL1_OFS, y, charsWritten, LEFT);
  lcdDrawText(lcdLastRightPos, y, " ");
  lcdDrawNumber(lcdLastRightPos, y, APP_Tx_ptr_in, LEFT);
  lcdDrawText(lcdLastRightPos, y, " ");
  lcdDrawNumber(lcdLastRightPos, y, APP_Rx_ptr_out, LEFT);
  lcdDrawText(lcdLastRightPos, y, " ");
  lcdDrawNumber(lcdLastRightPos, y, usbWraps, LEFT);
  y += FH;
#endif
  lcdDrawTextAlignedLeft(y, "Free Mem");
  lcdDrawNumber(MENU_DEBUG_COL1_OFS, y, availableMemory(), LEFT);
  lcdDrawText(lcdLastRightPos, y, "b");
  y += FH;
#endif

  lcdDrawTextAlignedLeft(y, STR_TMIXMAXMS);
  lcdDrawNumber(MENU_DEBUG_COL1_OFS, y, DURATION_MS_PREC2(maxMixerDuration), PREC2|LEFT);
  lcdDrawText(lcdLastRightPos, y, "ms");
  y += FH;

  lcdDrawTextAlignedLeft(y, STR_FREESTACKMINB);
  lcdDrawNumber(MENU_DEBUG_COL1_OFS, y, menusStack.available(), LEFT);
  lcdDrawText(lcdLastRightPos, y, "/");
  lcdDrawNumber(lcdLastRightPos, y, mixerStack.available(), LEFT);

  lcdDrawText(4*FW, 7*FH+1, STR_MENUTORESET);
  lcdInvertLastLine();
#endif
}

#if defined(STM32) && !defined(PCBI6X)
void menuStatisticsDebug2(event_t event)
{
  TITLE(STR_MENUDEBUG);

  switch (event) {
    case EVT_KEY_FIRST(KEY_ENTER):
    //   telemetryErrors  = 0;
      break;

    case EVT_KEY_FIRST(KEY_UP):
#if defined(PCBX7)
    case EVT_KEY_BREAK(KEY_PAGE):
#endif
      chainMenu(menuStatisticsView);
      return;

    case EVT_KEY_FIRST(KEY_DOWN):
#if defined(PCBX7)
    case EVT_KEY_LONG(KEY_PAGE):
#endif
      killEvents(event);
      chainMenu(menuStatisticsDebug);
      break;

    case EVT_KEY_FIRST(KEY_EXIT):
      chainMenu(menuMainView);
      break;
  }

  lcdDrawText(4*FW, 7*FH+1, STR_MENUTORESET);
  lcdInvertLastLine();
}
#endif
