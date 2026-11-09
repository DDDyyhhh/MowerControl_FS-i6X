SET(SOURCES screens/ui_Screen1.c
    screens/ui_ScreenSettings.c
    screens/ui_ScreenPairConfirm.c
    screens/ui_ScreenSafe.c
    screens/ui_ScreenOpMode.c
    screens/ui_ScreenLock.c
    screens/ui_ScreenPinChange.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    ui_events.c
    images/ui_img_car_logo_png.c
    images/ui_img_lock_png.c
    images/ui_img_handle_logo2_png.c
    images/ui_img_blade_png.c
    images/ui_img_unlock_png.c
    images/ui_img_1251936629.c
    images/ui_img_settings_png.c
    images/ui_img_grassbag_png.c
    images/ui_img_cruise_png.c
    images/ui_img_open_png.c
    images/ui_img_pair_png.c
    images/ui_img_shuangshou1_png.c
    images/ui_img_bingno_png.c
    images/ui_img_shuangshou2_png.c
    images/ui_img_danshou1_png.c
    images/ui_img_danshou2_png.c
    fonts/ui_font_Font12s4bpp.c
    fonts/ui_font_Font16zh4bpp.c
    fonts/ui_font_Font20s4bpp.c
    fonts/ui_font_Font20zh4bpp.c)

add_library(ui ${SOURCES})
