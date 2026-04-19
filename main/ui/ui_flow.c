#include "ui_flow.h"

#include <stdlib.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define UI_FLOW_MENU_SMOKE_AUTOPLAY 0
#define UI_FLOW_MENU_SMOKE_STEP_MS 1400
#define UI_FLOW_MENU_SMOKE_WIFI_HOLD_MS 5000
#define UI_FLOW_MENU_SMOKE_MONITOR_RETURN_MS 1800
#define UI_FLOW_OVERLAY_OPEN_STEP_Q8 52
#define UI_FLOW_OVERLAY_CLOSE_STEP_Q8 44
#define UI_FLOW_HIGHLIGHT_SNAP_Q8 40
#define UI_FLOW_HIGHLIGHT_MIN_STEP_Q8 160
#define UI_FLOW_HIGHLIGHT_MAX_VELOCITY_Q8 3072
#define UI_FLOW_HIGHLIGHT_ROTATE_KICK_Q8 448

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

typedef enum {
  WIFI_ACTION_OPEN_PORTAL = 0,
  WIFI_ACTION_DISCONNECT,
  WIFI_ACTION_STOP_PORTAL,
  WIFI_ACTION_FORGET,
  WIFI_ACTION_HOME,
  WIFI_ACTION_COUNT,
} wifi_action_index_t;

static local_menu_state_t s_local_menu = {
    .visible = false,
    .requested_visible = false,
    .wifi_actions_visible = false,
    .active_screen = LOCAL_SCREEN_MONITOR,
    .selected_index = 0,
    .wifi_action_selected = 0,
    .highlight_y_q8 = 0,
    .highlight_velocity_q8 = 0,
    .overlay_progress_q8 = 0,
    .pulse_phase = 0,
};
static portMUX_TYPE s_local_menu_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_menu_smoke_demo_started;
static int64_t s_menu_smoke_demo_started_us;
static menu_smoke_phase_t s_menu_smoke_demo_phase =
    MENU_SMOKE_PHASE_MENU_MONITOR;
static local_control_action_t s_pending_control_action =
    LOCAL_CONTROL_ACTION_NONE;

static void ui_flow_stop_demo_locked(void) {
  s_menu_smoke_demo_started = false;
  s_menu_smoke_demo_started_us = 0;
  s_menu_smoke_demo_phase = MENU_SMOKE_PHASE_MENU_MONITOR;
}

static int clamp_int(int value, int min_value, int max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static int wrap_index(int value, int count) {
  if (count <= 0) {
    return 0;
  }
  while (value < 0) {
    value += count;
  }
  while (value >= count) {
    value -= count;
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
  s_local_menu.requested_visible = show_menu;
  s_local_menu.wifi_actions_visible = false;
  s_local_menu.wifi_action_selected = WIFI_ACTION_OPEN_PORTAL;
  if (show_menu) {
    s_local_menu.visible = true;
  }
  if (s_local_menu.highlight_y_q8 == 0 || !show_menu) {
    s_local_menu.highlight_y_q8 = menu_highlight_target_y_q8(index);
    s_local_menu.highlight_velocity_q8 = 0;
  }
}

void ui_flow_init(void) {
  portENTER_CRITICAL(&s_local_menu_lock);
  s_local_menu.visible = false;
  s_local_menu.requested_visible = false;
  s_local_menu.wifi_actions_visible = false;
  s_local_menu.active_screen = LOCAL_SCREEN_MONITOR;
  s_local_menu.selected_index = LOCAL_SCREEN_MONITOR;
  s_local_menu.wifi_action_selected = WIFI_ACTION_OPEN_PORTAL;
  s_local_menu.highlight_y_q8 = 0;
  s_local_menu.highlight_velocity_q8 = 0;
  s_local_menu.overlay_progress_q8 = 0;
  s_local_menu.pulse_phase = 0;
  ui_flow_stop_demo_locked();
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
  ui_flow_stop_demo_locked();
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

void ui_flow_handle_encoder_rotate(int steps) {
  int velocity_kick_q8;

  if (steps == 0) {
    return;
  }

  portENTER_CRITICAL(&s_local_menu_lock);
  ui_flow_stop_demo_locked();
  if (s_local_menu.visible) {
    s_local_menu.selected_index = clamp_int(s_local_menu.selected_index + steps,
                                            0, LOCAL_SCREEN_COUNT - 1);
    velocity_kick_q8 = steps * UI_FLOW_HIGHLIGHT_ROTATE_KICK_Q8;
    s_local_menu.highlight_velocity_q8 =
        clamp_int(s_local_menu.highlight_velocity_q8 + velocity_kick_q8,
                  -UI_FLOW_HIGHLIGHT_MAX_VELOCITY_Q8,
                  UI_FLOW_HIGHLIGHT_MAX_VELOCITY_Q8);
  } else if (s_local_menu.active_screen == LOCAL_SCREEN_WIFI &&
             s_local_menu.wifi_actions_visible) {
    s_local_menu.wifi_action_selected = wrap_index(
        s_local_menu.wifi_action_selected + steps, WIFI_ACTION_COUNT);
  }
  portEXIT_CRITICAL(&s_local_menu_lock);
}

void ui_flow_handle_encoder_press(void) {
  portENTER_CRITICAL(&s_local_menu_lock);
  ui_flow_stop_demo_locked();

  if (s_local_menu.active_screen == LOCAL_SCREEN_WIFI && !s_local_menu.visible) {
    if (!s_local_menu.wifi_actions_visible) {
      s_local_menu.wifi_actions_visible = true;
      s_local_menu.wifi_action_selected = WIFI_ACTION_OPEN_PORTAL;
      portEXIT_CRITICAL(&s_local_menu_lock);
      return;
    }

    switch ((wifi_action_index_t)s_local_menu.wifi_action_selected) {
    case WIFI_ACTION_OPEN_PORTAL:
      s_pending_control_action = LOCAL_CONTROL_ACTION_START_PORTAL;
      break;
    case WIFI_ACTION_DISCONNECT:
      s_pending_control_action = LOCAL_CONTROL_ACTION_DISCONNECT_WIFI;
      break;
    case WIFI_ACTION_STOP_PORTAL:
      s_pending_control_action = LOCAL_CONTROL_ACTION_STOP_PORTAL;
      break;
    case WIFI_ACTION_FORGET:
      s_pending_control_action = LOCAL_CONTROL_ACTION_FORGET_WIFI;
      break;
    case WIFI_ACTION_HOME:
    default:
      s_local_menu.active_screen = LOCAL_SCREEN_MONITOR;
      break;
    }
    s_local_menu.wifi_actions_visible = false;
    portEXIT_CRITICAL(&s_local_menu_lock);
    return;
  }

  if (!s_local_menu.requested_visible) {
    s_local_menu.visible = true;
    s_local_menu.requested_visible = true;
    s_local_menu.selected_index = LOCAL_SCREEN_MONITOR;
    s_local_menu.highlight_y_q8 =
        menu_highlight_target_y_q8(s_local_menu.selected_index);
    s_local_menu.highlight_velocity_q8 = 0;
    portEXIT_CRITICAL(&s_local_menu_lock);
    return;
  }

  s_local_menu.active_screen = (local_screen_t)clamp_int(
      s_local_menu.selected_index, 0, LOCAL_SCREEN_COUNT - 1);
  s_local_menu.requested_visible = false;
  portEXIT_CRITICAL(&s_local_menu_lock);
}

bool ui_flow_take_pending_action(local_control_action_t *out_action) {
  bool has_action = false;

  if (out_action == NULL) {
    return false;
  }

  portENTER_CRITICAL(&s_local_menu_lock);
  if (s_pending_control_action != LOCAL_CONTROL_ACTION_NONE) {
    *out_action = s_pending_control_action;
    s_pending_control_action = LOCAL_CONTROL_ACTION_NONE;
    has_action = true;
  }
  portEXIT_CRITICAL(&s_local_menu_lock);
  return has_action;
}

void ui_flow_tick(void) {
  int overlay_target_q8;
  int target_y_q8;
  int diff;
  int spring_q8;
  int next_y_q8;

  portENTER_CRITICAL(&s_local_menu_lock);
  overlay_target_q8 = s_local_menu.requested_visible ? 256 : 0;
  if (s_local_menu.overlay_progress_q8 != overlay_target_q8) {
    int overlay_step_q8 = s_local_menu.requested_visible
                              ? UI_FLOW_OVERLAY_OPEN_STEP_Q8
                              : UI_FLOW_OVERLAY_CLOSE_STEP_Q8;
    if (s_local_menu.overlay_progress_q8 < overlay_target_q8) {
      s_local_menu.overlay_progress_q8 = clamp_int(
          s_local_menu.overlay_progress_q8 + overlay_step_q8, 0, 256);
    } else {
      s_local_menu.overlay_progress_q8 = clamp_int(
          s_local_menu.overlay_progress_q8 - overlay_step_q8, 0, 256);
    }
  }
  if (!s_local_menu.requested_visible && s_local_menu.overlay_progress_q8 == 0) {
    s_local_menu.visible = false;
  } else if (s_local_menu.overlay_progress_q8 > 0) {
    s_local_menu.visible = true;
  }

  target_y_q8 = menu_highlight_target_y_q8(s_local_menu.selected_index);
  if (s_local_menu.highlight_y_q8 == 0) {
    s_local_menu.highlight_y_q8 = target_y_q8;
    s_local_menu.highlight_velocity_q8 = 0;
  } else {
    diff = target_y_q8 - s_local_menu.highlight_y_q8;
    if (abs(diff) <= UI_FLOW_HIGHLIGHT_SNAP_Q8 &&
        abs(s_local_menu.highlight_velocity_q8) <= UI_FLOW_HIGHLIGHT_SNAP_Q8) {
      s_local_menu.highlight_y_q8 = target_y_q8;
      s_local_menu.highlight_velocity_q8 = 0;
    } else {
      spring_q8 = diff / 2;
      if (abs(spring_q8) < UI_FLOW_HIGHLIGHT_MIN_STEP_Q8) {
        spring_q8 = (diff > 0) ? UI_FLOW_HIGHLIGHT_MIN_STEP_Q8
                               : -UI_FLOW_HIGHLIGHT_MIN_STEP_Q8;
      }
      s_local_menu.highlight_velocity_q8 =
          (s_local_menu.highlight_velocity_q8 * 22) / 32;
      s_local_menu.highlight_velocity_q8 += spring_q8;
      s_local_menu.highlight_velocity_q8 =
          clamp_int(s_local_menu.highlight_velocity_q8,
                    -UI_FLOW_HIGHLIGHT_MAX_VELOCITY_Q8,
                    UI_FLOW_HIGHLIGHT_MAX_VELOCITY_Q8);
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
      s_local_menu.requested_visible = false;
      s_local_menu.selected_index = LOCAL_SCREEN_MONITOR;
      s_local_menu.overlay_progress_q8 = 0;
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
  s_local_menu.requested_visible = true;
  s_local_menu.overlay_progress_q8 = 256;

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
    s_local_menu.requested_visible = false;
    s_local_menu.overlay_progress_q8 = 0;
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
    s_local_menu.requested_visible = false;
    s_local_menu.overlay_progress_q8 = 0;
    s_local_menu.active_screen = LOCAL_SCREEN_MEMORY;
    break;
  case MENU_SMOKE_PHASE_RETURN_MONITOR:
  default:
    index = LOCAL_SCREEN_MONITOR;
    s_local_menu.visible = false;
    s_local_menu.requested_visible = false;
    s_local_menu.overlay_progress_q8 = 0;
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
