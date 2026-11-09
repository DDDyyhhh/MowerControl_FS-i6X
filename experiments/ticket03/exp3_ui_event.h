#ifndef TICKET03_EXP3_UI_EVENT_H
#define TICKET03_EXP3_UI_EVENT_H

#include "exp3_probe.h"

#if EXP3_ENABLED
/* i6X aliases KEY_ENTER to KEY_MENU. Consume OK before the main-view menu switch. */
static inline uint16_t exp3_ui_main_event(uint16_t event, uint16_t ok_break,
                                          Exp3UiState state)
{
    return event == ok_break && state != EXP3_UI_IDLE ? 0U : event;
}
#endif

#endif
