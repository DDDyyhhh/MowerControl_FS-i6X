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

// 通道监视器布局（两列，一屏显示全部通道；行高仍为 7px，共 8 行，y=9,16,...,58）：
//   左列 CH1..CH8、右列 CH9..CH16，每列 64px 宽
//   每格 = 通道名/编号(左, SMLSIZE) | 居中双向窄条(12px) | 数值(右对齐, SMLSIZE)
// 注：旧版单列布局的 "OVR"/"INV" 属性标记在两列下已无空间，故省略
constexpr coord_t CHANNEL_COLUMN_WIDTH = 64;   // 每列宽
constexpr coord_t CHANNEL_BAR_X        = 26;   // 窄条相对列起点的 x
constexpr coord_t CHANNEL_BAR_WIDTH    = 12;   // 窄条宽
constexpr coord_t CHANNEL_BAR_HEIGHT   = 5;    // 窄条高
constexpr coord_t CHANNEL_VALUE_RIGHT  = CHANNEL_COLUMN_WIDTH - 1;  // 数值右对齐位置（相对列起点）

#if defined(NAVIGATION_X7)
#define EVT_KEY_PREVIOUS_VIEW          EVT_KEY_LONG(KEY_PAGE)
#define EVT_KEY_NEXT_VIEW              EVT_KEY_BREAK(KEY_PAGE)
#define EVT_KEY_NEXT_PAGE              EVT_ROTARY_RIGHT
#define EVT_KEY_PREVIOUS_PAGE          EVT_ROTARY_LEFT
#elif defined(NAVIGATION_XLITE)
#define EVT_KEY_PREVIOUS_VIEW          EVT_KEY_BREAK(KEY_UP)
#define EVT_KEY_NEXT_VIEW              EVT_KEY_BREAK(KEY_DOWN)
#define EVT_KEY_NEXT_PAGE              EVT_KEY_BREAK(KEY_RIGHT)
#define EVT_KEY_PREVIOUS_PAGE          EVT_KEY_BREAK(KEY_LEFT)
#else
#define EVT_KEY_PREVIOUS_VIEW          EVT_KEY_BREAK(KEY_UP)
#define EVT_KEY_NEXT_VIEW              EVT_KEY_BREAK(KEY_DOWN)
#define EVT_KEY_NEXT_PAGE              EVT_KEY_BREAK(KEY_RIGHT)
#define EVT_KEY_PREVIOUS_PAGE          EVT_KEY_BREAK(KEY_LEFT)
#endif

// false = 显示通道最终输出（channelOutputs），true = 显示混控器输出（ex_chans）；
// 在通道监视器页面按 ENTER 短按切换
static bool mixersView = false;

// 通道监视器页面（主界面切到"通道监视"视图时由此函数绘制）：
//   标题行（第 0 行整行反白）：居中显示 "通道监视器" 或 "混控器监视器"
//   主体 8 行 x 2 列 = 一屏显示全部 16 个通道（左列 CH1-8，右列 CH9-16）
//   数据：channelOutputs[]（最终输出）或 ex_chans[]（限幅前），ENTER 短按切换
void menuChannelsViewCommon(event_t event)
{
  switch (event) {
    case EVT_KEY_FIRST(KEY_ENTER):
      mixersView = !mixersView;
      break;
  }

  // Screen title
  lcdDrawText(LCD_W / 2, 0, mixersView ? STR_MIXERS_MONITOR : STR_CHANNELS_MONITOR, CENTERED);
  lcdInvertLine(0);

  for (uint8_t col = 0; col < 2; col++) {
    for (uint8_t line = 0; line < 8; line++) {
      const uint8_t ch = col * 8 + line;   // 左列 CH1..CH8，右列 CH9..CH16
      if (ch >= MAX_OUTPUT_CHANNELS)
        continue;

      const coord_t x0 = col * CHANNEL_COLUMN_WIDTH;
      const uint8_t y = 9 + line * 7;
      const int32_t val = mixersView ? ex_chans[ch] : channelOutputs[ch];

      // 通道名（有自定义名则显示名字，否则显示 CH 编号）
      if (ZLEN(g_model.limitData[ch].name)) {
        lcdDrawSizedText(x0, y, g_model.limitData[ch].name, sizeof(g_model.limitData[ch].name), ZCHAR | SMLSIZE);
      }
      else {
        putsChn(x0, y, ch + 1, SMLSIZE);
      }

      // 居中双向窄条（±512 量程）
      drawGauge(x0 + CHANNEL_BAR_X, y + 1, CHANNEL_BAR_WIDTH, CHANNEL_BAR_HEIGHT, val, 512 * 2);

      // 数值（右对齐）
      drawChannelValue(x0 + CHANNEL_VALUE_RIGHT, y, ch, val, SMLSIZE | RIGHT);
    }
  }
}

// 通道监视器的菜单入口版（从菜单打开时使用，与主视图版共用绘制函数）：
//   EXIT：返回上级菜单；RIGHT：显示下一组 8 个通道（i6X 无上一页键）
void menuChannelsView(event_t event)
{
  switch (event) {
    case EVT_KEY_BREAK(KEY_EXIT):
      popMenu();
      break;

    case EVT_KEY_NEXT_PAGE:
      g_eeGeneral.view = (g_eeGeneral.view + (4 * ALTERNATE_VIEW) + ALTERNATE_VIEW) % (4 * ALTERNATE_VIEW);
      break;
#if !defined(PCBI6X)
    case EVT_KEY_PREVIOUS_PAGE:
      g_eeGeneral.view = (g_eeGeneral.view + (4 * ALTERNATE_VIEW) - ALTERNATE_VIEW) % (4 * ALTERNATE_VIEW);
      break;
#endif
  }

  menuChannelsViewCommon(event);
}