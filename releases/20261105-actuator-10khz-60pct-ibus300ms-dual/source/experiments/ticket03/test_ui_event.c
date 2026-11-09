#include "exp3_ui_event.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* On i6X KEY_ENTER aliases KEY_MENU; a short OK also opens the main menu. */
    const uint16_t ok_break = 0x20U;
    const uint16_t context_menu = ok_break;
    assert(exp3_ui_main_event(context_menu, ok_break, EXP3_UI_PROMPT) == 0U);
    assert(exp3_ui_main_event(context_menu, ok_break, EXP3_UI_WAIT) == 0U);
    assert(exp3_ui_main_event(context_menu, ok_break, EXP3_UI_UNBOUND) == 0U);
    assert(exp3_ui_main_event(context_menu, ok_break, EXP3_UI_ACCEPTED) == 0U);
    assert(exp3_ui_main_event(context_menu, ok_break, EXP3_UI_IDLE) == context_menu);
    assert(exp3_ui_main_event(0x21U, ok_break, EXP3_UI_PROMPT) == 0x21U);
    puts("PASS i6X OK does not open main context menu during Experiment3 prompt");
    return 0;
}
