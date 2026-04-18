#include "ui_flow.h"

#include <stdlib.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define UI_FLOW_MENU_SMOKE_AUTOPLAY 1
#define UI_FLOW_MENU_SMOKE_STEP_MS 1400
#define UI_FLOW_MENU_SMOKE_WIFI_HOLD_MS 5000
#define UI_FLOW_MENU_SMOKE_MONITOR_RETURN_MS 1800

typedef enum {
  MENU_SMOKE_PHASE_MENU_MONITOR = 0,
  MENU_SMOKE_PHASE_MENU_WIFI,
  MENU_SMOKE_PHASE_SCREEN_WIFI,
  MENU_SMOKE_PHASE_MENU_RETURN_WIFI,
  MENU_SMOKE_PHASE_MENU_ALARM,
  MENU_SMOKE_PHASE_MENU_GAME,
  MENU_SMOKE_PHASE_MENU_MEMORY,
  MENU_SMOKE_PHASE_SCREEN_MEMORY,
  MENU_SMOKE_PHASE_RETURN_MONITOR,
  MENU_SMOKE_PHASE_COUNT,
} menu_smoke_phase_t;

static local_menu_state_t s_local_menu = {
    .visible = false,
    .active_screen = LOCAL_SCREEN_MONITOR,
    .selected_index = 0,
    .highlight_y_q8 = 0,
    .highlight_velocity_q8 = 0,
    .pulse_phase = 0,
};
static portMUX_TYPE s_local_menu_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_menu_smoke_demo_started;
static int64_t s_menu_smoke_demo_started_us;
static menu_smoke_phase_t s_menu_smoke_demo_phase =
    MENU_SMOKE_PHASE_MENU_MONITOR;

static int clamp_int(int value, int min_value, int max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static int menu_highlight_target_y_q8(int index) {
  const int row_y[] = {36, 54, 72, 90, 108};
  int safe_index = clamp_int(index, 0, LOCAL_SCREEN_COUNT - 1);
  return row_y[safe_index] << 8;
}

static void ui_flow_apply_screen(local_screen_t screen, bool show_menu) {
  int index = clamp_int((int)screen, 0, LOCAL_SCREEN_COUNT - 1);

  s_local_menu.active_screen = (local_screen_t)index;
  s_local_menu.selected_index = index;
  s_local_menu.visible = show_menu;
  if (s_local_menu.highlight_y_q8 == 0 || !show_menu) {
    s_local_menu.highlight_y_q8 = menu_highlight_target_y_q8(index);
    s_local_menu.highlight_velocity_q8 = 0;
  }
}

void ui_flow_init(void) {
  portENTER_CRITICAL(&s_local_menu_lock);
  s_local_menu.visible = false;
  s_local_menu.active_screen = LOCAL_SCREEN_MONITOR;
  s_local_menu.selected_index = LOCAL_SCREEN_MONITOR;
  s_local_menu.highlight_y_q8 = 0;
  s_local_menu.highlight_velocity_q8 = 0;
  s_local_menu.pulse_phase = 0;
  s_menu_smoke_demo_started = false;
  s_menu_smoke_demo_started_us = 0;
  s_menu_smoke_demo_phase = MENU_SMOKE_PHASE_MENU_MONITOR;
  portEXIT_CRITICAL(&s_local_menu_lock);
}

local_menu_state_t ui_flow_snapshot(void) {
  local_menu_state_t snapshot;

  portENTER_CRITICAL(&s_local_menu_lock);
  snapshot = s_local_menu;
  portEXIT_CRITICAL(&s_local_menu_lock);
  return snapshot;
}

void ui_flow_dispatch(const ui_flow_event_t *event) {
  if (event == NULL) {
    return;
  }

  portENTER_CRITICAL(&s_local_menu_lock);
  switch (event->type) {
  case UI_FLOW_EVENT_SET_SCREEN:
    ui_flow_apply_screen(event->screen, event->show_menu);
    break;
  case UI_FLOW_EVENT_MEMORY_PHOTO_UPDATED:
    ui_flow_apply_screen(LOCAL_SCREEN_MEMORY, false);
    break;
  case UI_FLOW_EVENT_MEMORY_PHOTO_CLEARED:
    ui_flow_apply_screen(LOCAL_SCREEN_MONITOR, false);
    break;
  default:
    break;
  }
  portEXIT_CRITICAL(&s_local_menu_lock);
}

void ui_flow_tick(void) {
  int target_y_q8;
  int diff;
  int accel_q8;
  int next_y_q8;

  portENTER_CRITICAL(&s_local_menu_lock);
  target_y_q8 = menu_highlight_target_y_q8(s_local_menu.selected_index);
  if (s_local_menu.highlight_y_q8 == 0) {
    s_local_menu.highlight_y_q8 = target_y_q8;
    s_local_menu.highlight_velocity_q8 = 0;
  } else {
    diff = target_y_q8 - s_local_menu.highlight_y_q8;
    if (abs(diff) <= 2 && abs(s_local_menu.highlight_velocity_q8) <= 2) {
      s_local_menu.highlight_y_q8 = target_y_q8;
      s_local_menu.highlight_velocity_q8 = 0;
    } else {
      accel_q8 = diff / 3;
      if (accel_q8 == 0) {
        accel_q8 = (diff > 0) ? 1 : -1;
      }
      s_local_menu.highlight_velocity_q8 += accel_q8;
      s_local_menu.highlight_velocity_q8 =
          (s_local_menu.highlight_velocity_q8 * 27) / 32;
      s_local_menu.highlight_velocity_q8 =
          clamp_int(s_local_menu.highlight_velocity_q8, -520, 520);
      next_y_q8 =
          s_local_menu.highlight_y_q8 + s_local_menu.highlight_velocity_q8;

      if ((diff > 0 && next_y_q8 > target_y_q8 &&
           s_local_menu.highlight_velocity_q8 > 0) ||
          (diff < 0 && next_y_q8 < target_y_q8 &&
           s_local_menu.highlight_velocity_q8 < 0)) {
        s_local_menu.highlight_y_q8 = target_y_q8;
        s_local_menu.highlight_velocity_q8 = 0;
      } else {
        s_local_menu.highlight_y_q8 = next_y_q8;
      }
    }
  }
  s_local_menu.pulse_phase = (uint8_t)(s_local_menu.pulse_phase + 1);
  portEXIT_CRITICAL(&s_local_menu_lock);
}

void ui_flow_update_smoke(int aqi) {
#if UI_FLOW_MENU_SMOKE_AUTOPLAY
  int64_t now_us;
  int index;
  int phase_duration_ms;

  if (!s_menu_smoke_demo_started && aqi < 5) {
    return;
  }

  now_us = esp_timer_get_time();
  if (!s_menu_smoke_demo_started) {
    s_menu_smoke_demo_started = true;
    s_menu_smoke_demo_started_us = now_us;
    s_menu_smoke_demo_phase = MENU_SMOKE_PHASE_MENU_MONITOR;
  }

  phase_duration_ms = UI_FLOW_MENU_SMOKE_STEP_MS;
  if (s_menu_smoke_demo_phase == MENU_SMOKE_PHASE_SCREEN_WIFI ||
      s_menu_smoke_demo_phase == MENU_SMOKE_PHASE_SCREEN_MEMORY) {
    phase_duration_ms = UI_FLOW_MENU_SMOKE_WIFI_HOLD_MS;
  } else if (s_menu_smoke_demo_phase == MENU_SMOKE_PHASE_RETURN_MONITOR) {
    phase_duration_ms = UI_FLOW_MENU_SMOKE_MONITOR_RETURN_MS;
  }

  if (((now_us - s_menu_smoke_demo_started_us) / 1000LL) >= phase_duration_ms) {
    menu_smoke_phase_t next_phase =
        (menu_smoke_phase_t)(((int)s_menu_smoke_demo_phase + 1) %
                             MENU_SMOKE_PHASE_COUNT);
    if (s_menu_smoke_demo_phase == MENU_SMOKE_PHASE_RETURN_MONITOR && aqi < 5) {
      portENTER_CRITICAL(&s_local_menu_lock);
      s_local_menu.active_screen = LOCAL_SCREEN_MONITOR;
      s_local_menu.visible = false;
      s_local_menu.selected_index = LOCAL_SCREEN_MONITOR;
      portEXIT_CRITICAL(&s_local_menu_lock);
      s_menu_smoke_demo_started = false;
      s_menu_smoke_demo_started_us = 0;
      s_menu_smoke_demo_phase = MENU_SMOKE_PHASE_MENU_MONITOR;
      return;
    }
    s_menu_smoke_demo_phase = next_phase;
    s_menu_smoke_demo_started_us = now_us;
  }

  portENTER_CRITICAL(&s_local_menu_lock);
  s_local_menu.active_screen = LOCAL_SCREEN_MONITOR;
  s_local_menu.visible = true;

  switch (s_menu_smoke_demo_phase) {
  case MENU_SMOKE_PHASE_MENU_MONITOR:
    s_local_menu.active_screen = LOCAL_SCREEN_MONITOR;
    index = LOCAL_SCREEN_MONITOR;
    break;
  case MENU_SMOKE_PHASE_MENU_WIFI:
    index = LOCAL_SCREEN_WIFI;
    break;
  case MENU_SMOKE_PHASE_SCREEN_WIFI:
    index = LOCAL_SCREEN_WIFI;
    s_local_menu.visible = false;
    s_local_menu.active_screen = LOCAL_SCREEN_WIFI;
    break;
  case MENU_SMOKE_PHASE_MENU_RETURN_WIFI:
    index = LOCAL_SCREEN_WIFI;
    break;
  case MENU_SMOKE_PHASE_MENU_ALARM:
    index = LOCAL_SCREEN_ALARM;
    break;
  case MENU_SMOKE_PHASE_MENU_GAME:
    index = LOCAL_SCREEN_GAME;
    break;
  case MENU_SMOKE_PHASE_MENU_MEMORY:
    index = LOCAL_SCREEN_MEMORY;
    break;
  case MENU_SMOKE_PHASE_SCREEN_MEMORY:
    index = LOCAL_SCREEN_MEMORY;
    s_local_menu.visible = false;
    s_local_menu.active_screen = LOCAL_SCREEN_MEMORY;
    break;
  case MENU_SMOKE_PHASE_RETURN_MONITOR:
  default:
    index = LOCAL_SCREEN_MONITOR;
    s_local_menu.visible = false;
    s_local_menu.active_screen = LOCAL_SCREEN_MONITOR;
    break;
  }

  s_local_menu.selected_index = index;
  if (s_local_menu.highlight_y_q8 == 0) {
    s_local_menu.highlight_y_q8 = menu_highlight_target_y_q8(index);
    s_local_menu.highlight_velocity_q8 = 0;
  }
  portEXIT_CRITICAL(&s_local_menu_lock);
#else
  (void)aqi;
#endif
}
