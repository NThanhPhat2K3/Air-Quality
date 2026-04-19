#include "app_core.h"

#include "app_state_machine.h"
#include "connectivity_service.h"
#include "display_hal.h"
#include "encoder_input.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_flow.h"
#define DISPLAY_FRAME_INTERVAL_MS 12

static const char *TAG = "app_core";

void app_core_run(void) {
  app_state_machine_t app_sm;
  TickType_t last_wake_tick = xTaskGetTickCount();

  ESP_LOGI(TAG, "Starting ST7735 dashboard demo");
  lcd_init();
  encoder_input_init();
  app_state_machine_init(&app_sm);

  while (true) {
    encoder_input_event_t encoder_event;
    local_control_action_t control_action;

    if (app_state_machine_is_running(&app_sm) &&
        encoder_input_poll(&encoder_event)) {
      if (encoder_event.steps != 0) {
        ui_flow_handle_encoder_rotate(encoder_event.steps);
      }
      if (encoder_event.button_pressed) {
        ui_flow_handle_encoder_press();
      }
    }

    if (app_state_machine_is_running(&app_sm) &&
        ui_flow_take_pending_action(&control_action)) {
      switch (control_action) {
      case LOCAL_CONTROL_ACTION_START_PORTAL:
        connectivity_service_start_provisioning();
        break;
      case LOCAL_CONTROL_ACTION_DISCONNECT_WIFI:
        connectivity_service_disconnect_wifi();
        break;
      case LOCAL_CONTROL_ACTION_STOP_PORTAL:
        connectivity_service_stop_provisioning();
        break;
      case LOCAL_CONTROL_ACTION_FORGET_WIFI:
        connectivity_service_forget_credentials();
        break;
      case LOCAL_CONTROL_ACTION_NONE:
      default:
        break;
      }
    }

    app_state_machine_tick(&app_sm);
    vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(DISPLAY_FRAME_INTERVAL_MS));
  }
}
