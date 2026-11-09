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

#pragma once

void clearInputs();
void setDefaultInputs();

#define DEFAULT_MODEL_IDX 0

// Reset the global model to full defaults
void setModelDefaults(uint8_t id = DEFAULT_MODEL_IDX);

#if defined(PCBI6X)
// 给尚未接混控的通道补上默认直通（开关/电位器 → CH5..CH10）
// 在 postModelLoad() 中调用，使已有模型也自动生效
void ensureDefaultDirectMixes();
#if defined(PCBI6X_TICKET03_HIL_MODEL)
#define TICKET03_HIL_MODEL_NAME "T03HIL"
bool configureTicket03HilModel();
#endif
#endif
