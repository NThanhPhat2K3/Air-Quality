#ifndef APP_STATE_MACHINE_H
#define APP_STATE_MACHINE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct app_state_machine app_state_machine_t;

void app_state_machine_init(app_state_machine_t *sm);
void app_state_machine_tick(app_state_machine_t *sm);
bool app_state_machine_is_running(const app_state_machine_t *sm);

struct app_state_machine {
  int phase;
  bool phase_initialized;
  int64_t phase_started_us;
  int64_t last_sensor_refresh_us;
};

#endif
