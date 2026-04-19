#ifndef ALARM_SERVICE_H
#define ALARM_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
  bool configured;
  bool enabled;
  bool ringing;
  uint8_t hour;
  uint8_t minute;
} alarm_service_state_t;

bool alarm_service_restore_from_nvs(void);
bool alarm_service_save_config(uint8_t hour, uint8_t minute, bool enabled);
bool alarm_service_clear(void);
void alarm_service_sync_clock(const struct tm *clock_info);
void alarm_service_get_state(alarm_service_state_t *out);

#endif
