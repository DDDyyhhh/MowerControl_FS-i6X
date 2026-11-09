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
#include "model_init.h"

void clearInputs()
{
  memset(g_model.expoData, 0, sizeof(g_model.expoData));
}

void setDefaultInputs() 
{
  for (int i = 0; i < NUM_STICKS; i++) {
    uint8_t stick_index = channelOrder(i + 1);
    ExpoData *expo = expoAddress(i);
    expo->srcRaw = MIXSRC_Rud - 1 + stick_index;
    expo->curve.type = CURVE_REF_EXPO;
    expo->chn = i;
    expo->weight = 100;
    expo->mode = 3;  // TODO constant

#if defined(TRANSLATIONS_CZ)
    for (int c = 0; c < 4; c++) {
      g_model.inputNames[i][c] = char2zchar(STR_INPUTNAMES[1 + 4 * (stick_index - 1) + c]);
    }
#else
    for (int c = 0; c < 3; c++) {
      g_model.inputNames[i][c] = char2zchar(STR_VSRCRAW[2 + 4 * stick_index + c]);
    }
#if LEN_INPUT_NAME > 3
    g_model.inputNames[i][3] = '\0';
#endif
#endif
  }

  storageDirty(EE_MODEL);
}

#if defined(PCBI6X)
// 默认"直通"表：第 i 项 → CH(NUM_STICKS+1+i)
//   索引 0..3 → CH5..CH8  ← SA / SB / SC / SD          （开关，-100/0/+100）
//   索引 4..5 → CH9..CH10 ← P1 / P2（面板丝印 VRA/VRB）（电位器，连续 -100..+100）
// 单源 + weight=100 + 无条件(swtch=0) + 无曲线 = 纯直通，不做任何混合
// 增减项：直接在表里增删，后面的通道号会自动顺延
static const uint16_t defaultDirectMixes[] = {
  MIXSRC_SA, MIXSRC_SB, MIXSRC_SC, MIXSRC_SD,   // → CH5 .. CH8
  MIXSRC_POT1, MIXSRC_POT2,                     // → CH9 .. CH10
};
#define DEFAULT_DIRECT_MIX_COUNT (sizeof(defaultDirectMixes) / sizeof(defaultDirectMixes[0]))

// 该通道是否已有任何混控（有则不补，避免覆盖用户自己的配置）
static bool channelHasMix(uint8_t destCh)
{
  for (uint8_t i = 0; i < MAX_MIXERS; i++) {
    MixData *mix = mixAddress(i);
    if (mix->srcRaw && mix->destCh == destCh)
      return true;
  }
  return false;
}

// 按 destCh 升序插入一条混控（保持同一通道的混控相邻、列表整体有序）
static bool insertDirectMix(uint8_t destCh, uint16_t src)
{
  uint8_t idx = MAX_MIXERS;
  for (uint8_t i = 0; i < MAX_MIXERS; i++) {
    MixData *mix = mixAddress(i);
    if (mix->srcRaw == 0 || mix->destCh > destCh) {
      idx = i;
      break;
    }
  }
  if (idx >= MAX_MIXERS)
    return false;  // 混控已满

  memmove(mixAddress(idx + 1), mixAddress(idx), (MAX_MIXERS - idx - 1) * sizeof(MixData));
  MixData *mix = mixAddress(idx);
  memclear(mix, sizeof(MixData));
  mix->destCh = destCh;
  mix->srcRaw = src;
  mix->weight = 100;
  return true;
}

// 给"一条混控都没有"的目标通道补上默认直通（已有混控的通道保持原样）
// 由 postModelLoad() 调用 → 已有模型无需手动配置即可让开关/电位器直通到通道
void ensureDefaultDirectMixes()
{
  bool changed = false;
  for (uint8_t n = 0; n < DEFAULT_DIRECT_MIX_COUNT; n++) {
    uint8_t destCh = NUM_STICKS + n;
    LimitData *ld = limitAddress(destCh);

    // (1) 限幅修复：半轴被裁掉时复位为满量程
    //     存储语义（见 myeeprom.h 的 LIMIT_MIN/LIMIT_MAX）：
    //       min 存 0 = -100.0%，min 存 1000 = 0.0%  → min > 0 表示"负半轴被裁"
    //       max 存 0 = +100.0%，max 存 -1000 = 0.0% → max < 0 表示"正半轴被裁"
    //     被裁的通道只有半程输出（例如只剩 0~+100），这里统一复位。
    if (ld->min > 0 || ld->max < 0) {
      ld->min       = 0;      // -100.0%
      ld->max       = 0;      // +100.0%
      ld->offset    = 0;
      ld->ppmCenter = 0;
      ld->revert    = false;
      ld->curve     = 0;
      changed = true;
    }

    // (2) 混控补齐：该通道一条混控都没有时补上直通（已有混控则保留原样）
    if (channelHasMix(destCh))
      continue;
    if (!insertDirectMix(destCh, defaultDirectMixes[n]))
      break;
    changed = true;
  }
  if (changed)
    storageDirty(EE_MODEL);
}
#if defined(PCBI6X_TICKET03_HIL_MODEL)
bool configureTicket03HilModel()
{
  // This function only runs after loading a copy into an unused model slot.
  // Replace every existing CH11/CH12 mix in that copy; keep all other channels.
  // Check capacity and ordering before changing anything, so a full or malformed
  // mixer table cannot truncate a different channel.
  uint8_t retained = 0;
  uint8_t previousCh = 0;
  bool seenEmpty = false;
  for (uint8_t i = 0; i < MAX_MIXERS; i++) {
    MixData *mix = mixAddress(i);
    if (!mix->srcRaw) {
      seenEmpty = true;
      continue;
    }
    if (seenEmpty || mix->destCh < previousCh)
      return false;
    previousCh = mix->destCh;
    if (mix->destCh != 10 && mix->destCh != 11)
      retained++;
  }
  if (retained > MAX_MIXERS - 2)
    return false;

  uint8_t next = 0;
  for (uint8_t i = 0; i < MAX_MIXERS && mixAddress(i)->srcRaw; i++) {
    MixData *mix = mixAddress(i);
    if (mix->destCh != 10 && mix->destCh != 11) {
      if (next != i)
        *mixAddress(next) = *mix;
      next++;
    }
  }
  memclear(mixAddress(next), (MAX_MIXERS - next) * sizeof(MixData));
  if (!insertDirectMix(10, MIXSRC_POT1) || !insertDirectMix(11, MIXSRC_POT2))
    return false;
  // A spare channel could have stored limits despite having no mix.
  memclear(limitAddress(10), sizeof(LimitData));
  memclear(limitAddress(11), sizeof(LimitData));
  str2zchar(g_model.header.name, TICKET03_HIL_MODEL_NAME, sizeof(g_model.header.name));
  return true;
}
#endif

#endif

void setDefaultMixes()
{
  for (int i = 0; i < NUM_STICKS; i++) {
    MixData *mix = mixAddress(i);
    mix->destCh = i;
    mix->weight = 100;
    mix->srcRaw = i + 1;
  }

#if defined(PCBI6X)
  // 新建模型：直接写入默认直通混控（开关 SA..SD → CH5..CH8，电位器 P1/P2 → CH9/CH10）
  for (uint32_t i = 0; i < DEFAULT_DIRECT_MIX_COUNT && (NUM_STICKS + i) < MAX_MIXERS; i++) {
    MixData *mix = mixAddress(NUM_STICKS + i);
    mix->destCh = NUM_STICKS + i;
    mix->weight = 100;
    mix->srcRaw = defaultDirectMixes[i];
  }
#endif
}

void setDefaultGVars()
{
#if defined(FLIGHT_MODES) && defined(GVARS)
  for (int fmIdx = 1; fmIdx < MAX_FLIGHT_MODES; fmIdx++) {
    for (int gvarIdx = 0; gvarIdx < MAX_GVARS; gvarIdx++) {
      g_model.flightModeData[fmIdx].gvars[gvarIdx] = GVAR_MAX + 1;
    }
  }
#endif
}

void setDefaultRSSIValues()
{
  // https://www.multi-module.org/using-the-module/protocol-details/flysky-afhds2a
  // AFHDS2A suggested OpenTX RSSI alarm threshold settings:
  g_model.rfAlarms.warning = 70;
  g_model.rfAlarms.critical = 50;
}

void setVendorSpecificModelDefaults(uint8_t id)
{
#if defined(PCBI6X)
  // g_model.moduleData[INTERNAL_MODULE].rfProtocol = RF_I6X_PROTO_OFF; // not needed
#endif
}

void applyDefaultTemplate() {
  setDefaultInputs();
  setDefaultMixes();
  setDefaultGVars();
  setDefaultRSSIValues();
}

void setModelDefaults(uint8_t id)
{
  memset(&g_model, 0, sizeof(g_model));
  applyDefaultTemplate();

  setVendorSpecificModelDefaults(id);
}
