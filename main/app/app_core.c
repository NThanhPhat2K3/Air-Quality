#include "app_core.h"

#include "app_state_machine.h"
#include "display_hal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define DISPLAY_FRAME_INTERVAL_MS 20

static const char *TAG = "app_core";

void app_core_run(void) {
  app_state_machine_t app_sm;

  ESP_LOGI(TAG, "Starting ST7735 dashboard demo");
  lcd_init();
  app_state_machine_init(&app_sm);

  while (true) {
    app_state_machine_tick(&app_sm);
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_FRAME_INTERVAL_MS));
  }
}
