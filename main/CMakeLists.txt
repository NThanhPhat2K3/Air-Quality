idf_component_register(
    SRCS "main.c"
         "app/app_core.c"
         "app/app_state_machine.c"
         "app/dashboard_state.c"
         "drivers/display_hal.c"
         "drivers/encoder_input.c"
         "services/alarm_service.c"
         "services/captive_dns.c"
         "services/connectivity_service.c"
         "services/memory_photo_service.c"
         "ui/ui_flow.c"
         "ui/ui_renderer.c"
    INCLUDE_DIRS "."
                 "app"
                 "drivers"
                 "services"
                 "ui"
    REQUIRES esp_driver_gpio esp_driver_spi esp_timer esp_event esp_netif esp_wifi nvs_flash esp_http_server mdns
    EMBED_FILES "B_hybrid_160x128.rgb565"
    EMBED_TXTFILES "web/index.html" "web/app.css" "web/app.js"
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
