#ifndef UI_FLOW_H
#define UI_FLOW_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  LOCAL_SCREEN_MONITOR = 0,
  LOCAL_SCREEN_WIFI,
  LOCAL_SCREEN_ALARM,
  LOCAL_SCREEN_GAME,
  LOCAL_SCREEN_MEMORY,
  LOCAL_SCREEN_COUNT,
} local_screen_t;

typedef enum {
  LOCAL_CONTROL_ACTION_NONE = 0,
  LOCAL_CONTROL_ACTION_START_PORTAL,
  LOCAL_CONTROL_ACTION_DISCONNECT_WIFI,
  LOCAL_CONTROL_ACTION_STOP_PORTAL,
  LOCAL_CONTROL_ACTION_FORGET_WIFI,
} local_control_action_t;

typedef struct {
  bool visible;
  bool requested_visible;
  bool wifi_actions_visible;
  bool alarm_editor_visible;
  bool alarm_edit_adjusting;
  local_screen_t active_screen;
  int selected_index;
  int wifi_action_selected;
  int alarm_edit_selected;
  uint8_t alarm_edit_hour;
  uint8_t alarm_edit_minute;
  bool alarm_edit_enabled;
  int highlight_y_q8;
  int highlight_velocity_q8;
  int overlay_progress_q8;
  uint8_t pulse_phase;
} local_menu_state_t;

typedef enum {
  UI_FLOW_EVENT_SET_SCREEN = 0,
  UI_FLOW_EVENT_MEMORY_PHOTO_UPDATED,
  UI_FLOW_EVENT_MEMORY_PHOTO_CLEARED,
} ui_flow_event_type_t;

typedef struct {
  ui_flow_event_type_t type;
  local_screen_t screen;
  bool show_menu;
} ui_flow_event_t;

void ui_flow_init(void);
local_menu_state_t ui_flow_snapshot(void);
void ui_flow_tick(void);
void ui_flow_update_smoke(int aqi);
void ui_flow_dispatch(const ui_flow_event_t *event);
void ui_flow_handle_encoder_rotate(int steps);
void ui_flow_handle_encoder_press(void);
bool ui_flow_take_pending_action(local_control_action_t *out_action);

#endif
