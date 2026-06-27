# waveshare_bsp — Board-Treiber-Layer, implementiert main/board/board.h.
idf_component_register(
    SRCS
        "bsp_i2c.c"
        "bsp_display.c"
        "bsp_audio.c"
        "bsp_buttons.c"
        "bsp_power.cpp"
    INCLUDE_DIRS "."
    # board.h lebt in main/board und ist der stabile Vertrag — nur als Header
    # einbinden, KEINE Link-Abhängigkeit auf main (kein Zirkel).
    PRIV_INCLUDE_DIRS "../../main/board"
    REQUIRES
        driver
        esp_lcd
        esp_timer
        lvgl
        esp_lvgl_port
        esp_codec_dev
        esp_lcd_co5300
        esp_lcd_touch
        esp_lcd_touch_cst9217
        XPowersLib
)
