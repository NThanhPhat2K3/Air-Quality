#ifndef CONNECTIVITY_SERVICE_H
#define CONNECTIVITY_SERVICE_H

#include <stdbool.h>

#define CONNECTIVITY_RUNTIME_HOSTNAME "aqnode"

typedef struct {
  bool connected;
  bool provisioning_portal_active;
  bool credentials_valid;
  char ssid[33];
  char runtime_ip[20];
} connectivity_ui_status_t;

void connectivity_service_setup_and_clock(void);
bool connectivity_service_is_time_synced(void);
void connectivity_service_set_time_synced(bool time_synced);
bool connectivity_service_is_wifi_connected(void);
void connectivity_service_get_ui_status(connectivity_ui_status_t *out);
bool connectivity_service_disconnect_wifi(void);
bool connectivity_service_stop_provisioning(void);
bool connectivity_service_forget_credentials(void);

#endif
