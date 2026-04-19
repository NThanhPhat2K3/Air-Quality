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

typedef struct {
  bool visible;
  local_screen_t active_screen;
  int selected_index;
  int highlight_y_q8;
  int highlight_velocity_q8;
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

#endif
