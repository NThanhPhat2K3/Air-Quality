#ifndef CONNECTIVITY_SERVICE_H
#define CONNECTIVITY_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#define CONNECTIVITY_RUNTIME_HOSTNAME "aqnode"
#define CONNECTIVITY_SAVED_NETWORKS_MAX 5

typedef struct {
  bool connected;
  bool provisioning_portal_active;
  bool credentials_valid;
  char ssid[33];
  char runtime_ip[20];
} connectivity_ui_status_t;

typedef struct {
  char ssid[33];
  bool hidden;
  bool active;
} connectivity_saved_network_t;

void connectivity_service_setup_and_clock(void);
bool connectivity_service_is_time_synced(void);
void connectivity_service_set_time_synced(bool time_synced);
bool connectivity_service_is_wifi_connected(void);
void connectivity_service_get_ui_status(connectivity_ui_status_t *out);
bool connectivity_service_start_provisioning(void);
bool connectivity_service_disconnect_wifi(void);
bool connectivity_service_stop_provisioning(void);
bool connectivity_service_forget_credentials(void);
size_t connectivity_service_get_saved_networks(connectivity_saved_network_t *out,
                                              size_t max_items);
bool connectivity_service_use_saved_network_index(size_t index);

#endif
