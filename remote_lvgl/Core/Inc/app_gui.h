#ifndef __APP_GUI_H
#define __APP_GUI_H

#include "main.h"
#include <stdbool.h>
void App_GUI_Main(void);
void Update_OpMode_UI_Highlight(uint8_t selected_mode);
void Update_Ultrasonic_UI_State(void);
void Enable_Cruise_Control(void);
#endif