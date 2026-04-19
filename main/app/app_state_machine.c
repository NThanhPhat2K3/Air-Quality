#include "app_state_machine.h"

#include <string.h>

#include "alarm_service.h"
#include "display_hal.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "ui_flow.h"
#include "ui_renderer.h"

#define SHOW_BITMAP_UI 1
#define DASHBOARD_SENSOR_REFRESH_MS 1000

typedef enum {
  APP_PHASE_BOOT_POWER_STABLE = 0,
  APP_PHASE_BOOT_DISPLAY_READY,
  APP_PHASE_BOOT_NETWORK_STACK,
  APP_PHASE_BOOT_WIFI_AND_TIME_SYNC,
  APP_PHASE_BOOT_CLOCK_STATUS,
  APP_PHASE_BOOT_DASHBOARD_READY,
  APP_PHASE_RUNNING,
} app_phase_t;

static void app_phase_transition(app_state_machine_t *sm, app_phase_t next) {
  sm->phase = (int)next;
  sm->phase_initialized = false;
  sm->phase_started_us = esp_timer_get_time();
}

static bool app_phase_elapsed(const app_state_machine_t *sm,
                              uint32_t duration_ms) {
  return (esp_timer_get_time() - sm->phase_started_us) >=
         ((int64_t)duration_ms * 1000LL);
}

static void app_dashboard_refresh(app_state_machine_t *sm, bool force_refresh) {
  int64_t now_us = esp_timer_get_time();

  if (sm == NULL) {
    return;
  }

  if (!force_refresh &&
      (now_us - sm->last_sensor_refresh_us) <
          (DASHBOARD_SENSOR_REFRESH_MS * 1000LL)) {
    return;
  }

  sm->time_synced = connectivity_service_is_time_synced();
  dashboard_state_build_runtime(&sm->dashboard, &sm->time_synced);
  connectivity_service_set_time_synced(sm->time_synced);
  dashboard_state_snapshot_store(&sm->dashboard);
  sm->last_sensor_refresh_us = now_us;
}

static void app_enter_boot_phase(app_state_machine_t *sm, int percent,
                                 const char *status) {
  (void)sm;
  ui_renderer_draw_boot_screen(percent, status);
  lcd_present_framebuffer();
}

static void app_enter_running_phase(app_state_machine_t *sm) {
  (void)sm;
  app_dashboard_refresh(sm, true);
  alarm_service_restore_from_nvs();
  ui_flow_init();
}

void app_state_machine_init(app_state_machine_t *sm) {
  if (sm == NULL) {
    return;
  }

  memset(sm, 0, sizeof(*sm));
  sm->phase = APP_PHASE_BOOT_POWER_STABLE;
  sm->phase_started_us = esp_timer_get_time();
}

bool app_state_machine_is_running(const app_state_machine_t *sm) {
  return sm != NULL && sm->phase == APP_PHASE_RUNNING;
}

void app_state_machine_tick(app_state_machine_t *sm) {
  if (sm == NULL) {
    return;
  }

  switch ((app_phase_t)sm->phase) {
  case APP_PHASE_BOOT_POWER_STABLE:
    if (!sm->phase_initialized) {
      app_enter_boot_phase(sm, 5, "POWER STABLE");
      sm->phase_initialized = true;
    }
    if (app_phase_elapsed(sm, 150)) {
      app_phase_transition(sm, APP_PHASE_BOOT_DISPLAY_READY);
    }
    break;

  case APP_PHASE_BOOT_DISPLAY_READY:
    if (!sm->phase_initialized) {
      app_enter_boot_phase(sm, 18, "DISPLAY READY");
      sm->phase_initialized = true;
    }
    if (app_phase_elapsed(sm, 150)) {
      app_phase_transition(sm, APP_PHASE_BOOT_NETWORK_STACK);
    }
    break;

  case APP_PHASE_BOOT_NETWORK_STACK:
    if (!sm->phase_initialized) {
      app_enter_boot_phase(sm, 35, "NETWORK STACK");
      sm->phase_initialized = true;
    }
    if (app_phase_elapsed(sm, 150)) {
      app_phase_transition(sm, APP_PHASE_BOOT_WIFI_AND_TIME_SYNC);
    }
    break;

  case APP_PHASE_BOOT_WIFI_AND_TIME_SYNC:
    if (!sm->phase_initialized) {
      app_enter_boot_phase(sm, 52, "WIFI + NTP SYNC");
      connectivity_service_setup_and_clock();
      sm->phase_initialized = true;
      app_phase_transition(sm, APP_PHASE_BOOT_CLOCK_STATUS);
    }
    break;

  case APP_PHASE_BOOT_CLOCK_STATUS:
    if (!sm->phase_initialized) {
      bool time_synced = connectivity_service_is_time_synced();
      app_enter_boot_phase(sm, time_synced ? 88 : 76,
                           time_synced ? "CLOCK LOCKED" : "OFFLINE MODE");
      sm->phase_initialized = true;
    }
    if (app_phase_elapsed(sm, 220)) {
      app_phase_transition(sm, APP_PHASE_BOOT_DASHBOARD_READY);
    }
    break;

  case APP_PHASE_BOOT_DASHBOARD_READY:
    if (!sm->phase_initialized) {
      app_enter_boot_phase(sm, 100, "DASHBOARD READY");
      sm->phase_initialized = true;
    }
    if (app_phase_elapsed(sm, 220)) {
      app_phase_transition(sm, APP_PHASE_RUNNING);
    }
    break;

  case APP_PHASE_RUNNING:
    if (!sm->phase_initialized) {
      app_enter_running_phase(sm);
      sm->phase_initialized = true;
    }

    app_dashboard_refresh(sm, false);
    alarm_service_sync_clock(&sm->dashboard.clock);
    ui_flow_update_smoke(sm->dashboard.aqi);
    ui_flow_tick();
    connectivity_service_get_ui_status(&sm->wifi_status);

#if SHOW_BITMAP_UI
    {
      local_menu_state_t menu = ui_flow_snapshot();
      ui_renderer_draw_local_screen(&sm->dashboard, &menu,
                                    &sm->wifi_status);
    }
#else
    ui_renderer_draw_dashboard(&sm->dashboard,
                               sm->wifi_status.connected,
                               sm->wifi_status.provisioning_portal_active);
#endif
    lcd_present_framebuffer();
    break;
  }
}
