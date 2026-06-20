SET(SOURCES ui_comp_panelcomponent.c
    ui_comp.c
    ui_main.c
    ui_theme_manager.c
    ui_themes.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_img_background_png.c
    fonts/ui_font_roboto150.c
    fonts/ui_font_robotobold120.c
    fonts/ui_font_robotobold60.c
    fonts/ui_font_robotobold80.c
    fonts/ui_font_robotoregular20.c
    fonts/ui_font_robotoregular28.c
    fonts/ui_font_robotoregular50.c)

add_library(ui ${SOURCES})
