#ifndef UI_RENDERER_H
#define UI_RENDERER_H

#include <stdbool.h>

#include "connectivity_service.h"
#include "dashboard_state.h"
#include "ui_flow.h"

void ui_renderer_draw_boot_screen(int percent, const char *status);
void ui_renderer_draw_local_screen(const dashboard_state_t *state,
                                   const local_menu_state_t *menu,
                                   const connectivity_ui_status_t *wifi_status);
void ui_renderer_draw_dashboard(const dashboard_state_t *state,
                                bool wifi_connected,
                                bool provisioning_portal_active);

#endif
