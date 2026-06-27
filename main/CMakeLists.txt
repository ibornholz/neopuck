idf_component_register(
    SRCS
        "app_main.c"
        "config_store.c"
        "provisioning.c"
        "agent_client.c"
        "audio_pipeline.c"
        "ui/ui.c"
        "ui/assets_logo.c"
    INCLUDE_DIRS "." "ui" "board"
    REQUIRES
        nvs_flash
        esp_wifi
        wifi_provisioning
        esp_websocket_client
        json
        esp_lcd
        esp_timer
        driver
        waveshare_bsp        # offizielles Waveshare-BSP als Component
)
