#include "ui_flow.h"

#include <stdlib.h>

#include "alarm_service.h"
#include "connectivity_service.h"
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
#define UI_FLOW_WIFI_NOTICE_TICKS 220

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
  WIFI_ACTION_SAVED_WIFI,
  WIFI_ACTION_DISCONNECT,
  WIFI_ACTION_STOP_PORTAL,
  WIFI_ACTION_FORGET,
  WIFI_ACTION_HOME,
  WIFI_ACTION_COUNT,
} wifi_action_index_t;

typedef enum {
  ALARM_EDIT_HOUR = 0,
  ALARM_EDIT_MINUTE,
  ALARM_EDIT_ENABLED,
  ALARM_EDIT_SAVE,
  ALARM_EDIT_CLEAR,
  ALARM_EDIT_HOME,
  ALARM_EDIT_COUNT,
} alarm_edit_index_t;

typedef enum {
  WIFI_NOTICE_NONE = 0,
  WIFI_NOTICE_SAVED_CONNECTING,
  WIFI_NOTICE_SAVED_OK,
  WIFI_NOTICE_SAVED_FAIL,
} wifi_notice_kind_t;

#define GAME_LANE_COUNT 3
#define GAME_OBSTACLE_COUNT 4
#define GAME_OBSTACLE_OFFSCREEN_Y 140

static local_menu_state_t s_local_menu = {
    .visible = false,
    .requested_visible = false,
    .wifi_actions_visible = false,
    .wifi_saved_visible = false,
    .alarm_editor_visible = false,
    .alarm_edit_adjusting = false,
    .active_screen = LOCAL_SCREEN_MONITOR,
    .selected_index = 0,
    .wifi_action_selected = 0,
    .wifi_saved_selected = 0,
    .alarm_edit_selected = ALARM_EDIT_HOUR,
    .alarm_edit_hour = 6,
    .alarm_edit_minute = 30,
    .alarm_edit_enabled = false,
    .highlight_y_q8 = 0,
    .highlight_velocity_q8 = 0,
    .overlay_progress_q8 = 0,
    .pulse_phase = 0,
    .wifi_notice_kind = WIFI_NOTICE_NONE,
    .wifi_notice_timer = 0,
    .game_running = false,
    .game_over = false,
    .game_player_lane = 1,
    .game_spawn_timer = 24,
    .game_score = 0,
    .game_obstacle_y = {GAME_OBSTACLE_OFFSCREEN_Y, GAME_OBSTACLE_OFFSCREEN_Y,
                        GAME_OBSTACLE_OFFSCREEN_Y, GAME_OBSTACLE_OFFSCREEN_Y},
    .game_obstacle_lane = {-1, -1, -1, -1},
};
static portMUX_TYPE s_local_menu_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_menu_smoke_demo_started;
static int64_t s_menu_smoke_demo_started_us;
static menu_smoke_phase_t s_menu_smoke_demo_phase =
    MENU_SMOKE_PHASE_MENU_MONITOR;
static local_control_action_t s_pending_control_action =
    LOCAL_CONTROL_ACTION_NONE;
static int s_pending_control_value;

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

static void game_reset_locked(void) {
  s_local_menu.game_running = false;
  s_local_menu.game_over = false;
  s_local_menu.game_player_lane = 1;
  s_local_menu.game_spawn_timer = 24;
  s_local_menu.game_score = 0;
  for (int i = 0; i < GAME_OBSTACLE_COUNT; ++i) {
    s_local_menu.game_obstacle_y[i] = GAME_OBSTACLE_OFFSCREEN_Y;
    s_local_menu.game_obstacle_lane[i] = -1;
  }
}

static void game_start_locked(void) {
  game_reset_locked();
  s_local_menu.game_running = true;
}

static void game_try_spawn_locked(void) {
  int slot = -1;
  int lane;

  for (int i = 0; i < GAME_OBSTACLE_COUNT; ++i) {
    if (s_local_menu.game_obstacle_lane[i] < 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    return;
  }

  lane = (int)((s_local_menu.pulse_phase + s_local_menu.game_score + slot) %
               GAME_LANE_COUNT);
  s_local_menu.game_obstacle_lane[slot] = (int8_t)lane;
  s_local_menu.game_obstacle_y[slot] = 18;
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
  s_local_menu.wifi_saved_visible = false;
  s_local_menu.alarm_editor_visible = false;
  s_local_menu.alarm_edit_adjusting = false;
  s_local_menu.wifi_action_selected = WIFI_ACTION_OPEN_PORTAL;
  s_local_menu.wifi_saved_selected = 0;
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
  s_local_menu.wifi_saved_visible = false;
  s_local_menu.alarm_editor_visible = false;
  s_local_menu.alarm_edit_adjusting = false;
  s_local_menu.active_screen = LOCAL_SCREEN_MONITOR;
  s_local_menu.selected_index = LOCAL_SCREEN_MONITOR;
  s_local_menu.wifi_action_selected = WIFI_ACTION_OPEN_PORTAL;
  s_local_menu.wifi_saved_selected = 0;
  s_local_menu.alarm_edit_selected = ALARM_EDIT_HOUR;
  s_local_menu.alarm_edit_hour = 6;
  s_local_menu.alarm_edit_minute = 30;
  s_local_menu.alarm_edit_enabled = false;
  s_local_menu.highlight_y_q8 = 0;
  s_local_menu.highlight_velocity_q8 = 0;
  s_local_menu.overlay_progress_q8 = 0;
  s_local_menu.pulse_phase = 0;
  s_local_menu.wifi_notice_kind = WIFI_NOTICE_NONE;
  s_local_menu.wifi_notice_timer = 0;
  game_reset_locked();
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
  case UI_FLOW_EVENT_WIFI_NOTICE:
    s_local_menu.wifi_notice_kind = (uint8_t)clamp_int(event->value, 0, 3);
    s_local_menu.wifi_notice_timer =
        s_local_menu.wifi_notice_kind == WIFI_NOTICE_NONE
            ? 0
            : UI_FLOW_WIFI_NOTICE_TICKS;
    break;
  default:
    break;
  }
  portEXIT_CRITICAL(&s_local_menu_lock);
}

void ui_flow_handle_encoder_rotate(int steps) {
  int velocity_kick_q8;
  size_t saved_count = connectivity_service_get_saved_networks(NULL, 0);

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
    if (s_local_menu.wifi_saved_visible) {
      int item_count = (int)saved_count + 1;
      s_local_menu.wifi_saved_selected = wrap_index(
          s_local_menu.wifi_saved_selected + steps, item_count);
    } else {
      s_local_menu.wifi_action_selected = wrap_index(
          s_local_menu.wifi_action_selected + steps, WIFI_ACTION_COUNT);
    }
  } else if (s_local_menu.active_screen == LOCAL_SCREEN_ALARM &&
             s_local_menu.alarm_editor_visible) {
    if (s_local_menu.alarm_edit_adjusting) {
      switch ((alarm_edit_index_t)s_local_menu.alarm_edit_selected) {
      case ALARM_EDIT_HOUR:
        s_local_menu.alarm_edit_hour =
            (uint8_t)wrap_index(s_local_menu.alarm_edit_hour + steps, 24);
        break;
      case ALARM_EDIT_MINUTE:
        s_local_menu.alarm_edit_minute =
            (uint8_t)wrap_index(s_local_menu.alarm_edit_minute + steps, 60);
        break;
      case ALARM_EDIT_ENABLED:
        if (steps != 0) {
          s_local_menu.alarm_edit_enabled = !s_local_menu.alarm_edit_enabled;
        }
        break;
      case ALARM_EDIT_SAVE:
      case ALARM_EDIT_CLEAR:
      case ALARM_EDIT_HOME:
      default:
        s_local_menu.alarm_edit_adjusting = false;
        s_local_menu.alarm_edit_selected = wrap_index(
            s_local_menu.alarm_edit_selected + steps, ALARM_EDIT_COUNT);
        break;
      }
    } else {
      s_local_menu.alarm_edit_selected = wrap_index(
          s_local_menu.alarm_edit_selected + steps, ALARM_EDIT_COUNT);
    }
  } else if (s_local_menu.active_screen == LOCAL_SCREEN_GAME) {
    int next_lane =
        clamp_int((int)s_local_menu.game_player_lane + steps, 0, GAME_LANE_COUNT - 1);
    s_local_menu.game_player_lane = (uint8_t)next_lane;
  }
  portEXIT_CRITICAL(&s_local_menu_lock);
}

void ui_flow_handle_encoder_press(void) {
  alarm_service_state_t alarm_state = {0};
  size_t saved_count = connectivity_service_get_saved_networks(NULL, 0);
  bool should_save_alarm = false;
  bool should_clear_alarm = false;
  uint8_t save_hour = 6;
  uint8_t save_minute = 30;
  bool save_enabled = false;

  alarm_service_get_state(&alarm_state);

  portENTER_CRITICAL(&s_local_menu_lock);
  ui_flow_stop_demo_locked();

  if (s_local_menu.active_screen == LOCAL_SCREEN_WIFI && !s_local_menu.visible) {
    if (!s_local_menu.wifi_actions_visible) {
      s_local_menu.wifi_actions_visible = true;
      s_local_menu.wifi_saved_visible = false;
      s_local_menu.wifi_action_selected = WIFI_ACTION_OPEN_PORTAL;
      portEXIT_CRITICAL(&s_local_menu_lock);
      return;
    }

    if (s_local_menu.wifi_saved_visible) {
      if (s_local_menu.wifi_saved_selected >= (int)saved_count) {
        s_local_menu.wifi_saved_visible = false;
        s_local_menu.wifi_saved_selected = 0;
        portEXIT_CRITICAL(&s_local_menu_lock);
        return;
      }

      s_pending_control_action = LOCAL_CONTROL_ACTION_USE_SAVED_WIFI;
      s_pending_control_value = s_local_menu.wifi_saved_selected;
      s_local_menu.wifi_saved_visible = false;
      s_local_menu.wifi_actions_visible = false;
      portEXIT_CRITICAL(&s_local_menu_lock);
      return;
    }

    switch ((wifi_action_index_t)s_local_menu.wifi_action_selected) {
    case WIFI_ACTION_OPEN_PORTAL:
      s_pending_control_action = LOCAL_CONTROL_ACTION_START_PORTAL;
      break;
    case WIFI_ACTION_SAVED_WIFI:
      s_local_menu.wifi_saved_visible = true;
      s_local_menu.wifi_saved_selected = 0;
      portEXIT_CRITICAL(&s_local_menu_lock);
      return;
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
    s_local_menu.wifi_saved_visible = false;
    portEXIT_CRITICAL(&s_local_menu_lock);
    return;
  }

  if (s_local_menu.active_screen == LOCAL_SCREEN_ALARM && !s_local_menu.visible) {
    if (!s_local_menu.alarm_editor_visible) {
      s_local_menu.alarm_editor_visible = true;
      s_local_menu.alarm_edit_hour = alarm_state.hour;
      s_local_menu.alarm_edit_minute = alarm_state.minute;
      s_local_menu.alarm_edit_enabled = alarm_state.enabled;
      s_local_menu.alarm_edit_selected = ALARM_EDIT_HOUR;
      s_local_menu.alarm_edit_adjusting = true;
      portEXIT_CRITICAL(&s_local_menu_lock);
      return;
    }

    if (s_local_menu.alarm_edit_adjusting) {
      switch ((alarm_edit_index_t)s_local_menu.alarm_edit_selected) {
      case ALARM_EDIT_HOUR:
      case ALARM_EDIT_MINUTE:
      case ALARM_EDIT_ENABLED:
        s_local_menu.alarm_edit_adjusting = false;
        portEXIT_CRITICAL(&s_local_menu_lock);
        return;
      case ALARM_EDIT_SAVE:
      case ALARM_EDIT_CLEAR:
      case ALARM_EDIT_HOME:
      default:
        break;
      }
    }

    switch ((alarm_edit_index_t)s_local_menu.alarm_edit_selected) {
    case ALARM_EDIT_HOUR:
    case ALARM_EDIT_MINUTE:
    case ALARM_EDIT_ENABLED:
      s_local_menu.alarm_edit_adjusting = true;
      break;
    case ALARM_EDIT_SAVE:
      save_hour = s_local_menu.alarm_edit_hour;
      save_minute = s_local_menu.alarm_edit_minute;
      save_enabled = s_local_menu.alarm_edit_enabled;
      should_save_alarm = true;
      s_local_menu.alarm_editor_visible = false;
      s_local_menu.alarm_edit_adjusting = false;
      break;
    case ALARM_EDIT_CLEAR:
      should_clear_alarm = true;
      s_local_menu.alarm_editor_visible = false;
      s_local_menu.alarm_edit_adjusting = false;
      break;
    case ALARM_EDIT_HOME:
    default:
      s_local_menu.alarm_editor_visible = false;
      s_local_menu.alarm_edit_adjusting = false;
      s_local_menu.active_screen = LOCAL_SCREEN_MONITOR;
      break;
    }
    portEXIT_CRITICAL(&s_local_menu_lock);
    if (should_save_alarm) {
      alarm_service_save_config(save_hour, save_minute, save_enabled);
    } else if (should_clear_alarm) {
      alarm_service_clear();
    }
    return;
  }

  if (s_local_menu.active_screen == LOCAL_SCREEN_GAME && !s_local_menu.visible) {
    if (!s_local_menu.game_running) {
      game_start_locked();
      portEXIT_CRITICAL(&s_local_menu_lock);
      return;
    }
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

bool ui_flow_take_pending_action(local_control_action_t *out_action,
                                 int *out_value) {
  bool has_action = false;

  if (out_action == NULL || out_value == NULL) {
    return false;
  }

  portENTER_CRITICAL(&s_local_menu_lock);
  if (s_pending_control_action != LOCAL_CONTROL_ACTION_NONE) {
    *out_action = s_pending_control_action;
    *out_value = s_pending_control_value;
    s_pending_control_action = LOCAL_CONTROL_ACTION_NONE;
    s_pending_control_value = 0;
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

  if (s_local_menu.wifi_notice_timer > 0) {
    s_local_menu.wifi_notice_timer--;
    if (s_local_menu.wifi_notice_timer == 0) {
      s_local_menu.wifi_notice_kind = WIFI_NOTICE_NONE;
    }
  }

  if (s_local_menu.active_screen == LOCAL_SCREEN_GAME && s_local_menu.game_running &&
      !s_local_menu.visible && s_local_menu.overlay_progress_q8 == 0) {
    int player_y = 102;
    int speed = 3 + (int)(s_local_menu.game_score / 6);
    if (speed > 6) {
      speed = 6;
    }

    if (s_local_menu.game_spawn_timer > 0) {
      s_local_menu.game_spawn_timer--;
    } else {
      game_try_spawn_locked();
      s_local_menu.game_spawn_timer =
          (uint8_t)clamp_int(24 - (int)(s_local_menu.game_score / 3), 10, 24);
    }

    for (int i = 0; i < GAME_OBSTACLE_COUNT; ++i) {
      if (s_local_menu.game_obstacle_lane[i] < 0) {
        continue;
      }

      s_local_menu.game_obstacle_y[i] += speed;

      if (s_local_menu.game_obstacle_lane[i] == (int8_t)s_local_menu.game_player_lane &&
          s_local_menu.game_obstacle_y[i] >= (player_y - 10) &&
          s_local_menu.game_obstacle_y[i] <= (player_y + 10)) {
        s_local_menu.game_running = false;
        s_local_menu.game_over = true;
      }

      if (s_local_menu.game_obstacle_y[i] > 118) {
        s_local_menu.game_obstacle_lane[i] = -1;
        s_local_menu.game_obstacle_y[i] = GAME_OBSTACLE_OFFSCREEN_Y;
        s_local_menu.game_score++;
      }
    }
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
