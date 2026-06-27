idf_component_register(
    SRCS
        "app_main.c"
        "config_store.c"
        "provisioning.c"
        "agent_client.c"
        "audio_pipeline.c"
        "ui/ui.c"
        "ui/assets_logo.c"
        "miniapps/miniapp.c"
        "miniapps/miniapp_stopwatch.c"
        "miniapps/miniapp_qr.c"
    INCLUDE_DIRS "." "ui" "board" "miniapps"
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_server      # Captive-Portal (Task 3)
        json
        esp_timer
        driver
        waveshare_bsp        # Board-Treiber-Layer (Task 1)
)
