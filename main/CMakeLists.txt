idf_component_register(
    SRCS "main.c" "captive_dns.c" "ui_flow.c" "display_hal.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_spi esp_timer esp_event esp_netif esp_wifi nvs_flash esp_http_server mdns
    EMBED_FILES "B_hybrid_160x128.rgb565"
    EMBED_TXTFILES "web/index.html" "web/app.css" "web/app.js"
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
