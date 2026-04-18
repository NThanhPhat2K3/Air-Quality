#include "app_core.h"

#include <stdint.h>

#include "connectivity_service.h"
#include "dashboard_state.h"
#include "display_hal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "memory_photo_service.h"
#include "ui_flow.h"
#include "ui_renderer.h"

#define SHOW_BITMAP_UI 1
#define DASHBOARD_SENSOR_REFRESH_MS 1000
#define DISPLAY_FRAME_INTERVAL_MS 20

static const char *TAG = "app_core";

void app_core_run(void) {
  dashboard_state_t state = {0};
  connectivity_ui_status_t wifi_status = {0};
  int64_t last_sensor_refresh_us = 0;

  ESP_LOGI(TAG, "Starting ST7735 dashboard demo");
  lcd_init();

  ui_renderer_draw_boot_screen(5, "POWER STABLE");
  lcd_present_framebuffer();
  vTaskDelay(pdMS_TO_TICKS(150));

  ui_renderer_draw_boot_screen(18, "DISPLAY READY");
  lcd_present_framebuffer();
  vTaskDelay(pdMS_TO_TICKS(150));

  ui_renderer_draw_boot_screen(35, "NETWORK STACK");
  lcd_present_framebuffer();
  vTaskDelay(pdMS_TO_TICKS(150));

  ui_renderer_draw_boot_screen(52, "WIFI + NTP SYNC");
  lcd_present_framebuffer();
  connectivity_service_setup_and_clock();

  ui_renderer_draw_boot_screen(connectivity_service_is_time_synced() ? 88 : 76,
                               connectivity_service_is_time_synced()
                                   ? "CLOCK LOCKED"
                                   : "OFFLINE MODE");
  lcd_present_framebuffer();
  vTaskDelay(pdMS_TO_TICKS(220));

  ui_renderer_draw_boot_screen(100, "DASHBOARD READY");
  lcd_present_framebuffer();
  vTaskDelay(pdMS_TO_TICKS(220));

  bool time_synced = connectivity_service_is_time_synced();
  dashboard_state_build_runtime(&state, &time_synced);
  connectivity_service_set_time_synced(time_synced);
  dashboard_state_snapshot_store(&state);
  last_sensor_refresh_us = esp_timer_get_time();
  ui_flow_init();
  memory_photo_service_restore_from_nvs();

  while (true) {
    int64_t now_us = esp_timer_get_time();
    if ((now_us - last_sensor_refresh_us) >=
        (DASHBOARD_SENSOR_REFRESH_MS * 1000LL)) {
      bool synced = connectivity_service_is_time_synced();
      dashboard_state_build_runtime(&state, &synced);
      connectivity_service_set_time_synced(synced);
      dashboard_state_snapshot_store(&state);
      last_sensor_refresh_us = now_us;
    }

    ui_flow_update_smoke(state.aqi);
    ui_flow_tick();
    connectivity_service_get_ui_status(&wifi_status);

#if SHOW_BITMAP_UI
    local_menu_state_t menu = ui_flow_snapshot();
    ui_renderer_draw_local_screen(&state, &menu, &wifi_status);
#else
    ui_renderer_draw_dashboard(&state, wifi_status.connected);
#endif
    lcd_present_framebuffer();
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_FRAME_INTERVAL_MS));
  }
}
