SET(SOURCES screens/ui_boot.c
    screens/ui_main.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_mitsubishi_logo_png.c
    images/ui_img_background_png.c
    fonts/ui_font_roboto150.c
    fonts/ui_font_robotobold60.c)

add_library(ui ${SOURCES})
