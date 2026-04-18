#ifndef DASHBOARD_STATE_H
#define DASHBOARD_STATE_H

#include <stdbool.h>
#include <time.h>

typedef struct {
  struct tm clock;
  int aqi;
  int eco2_ppm;
  int tvoc_ppb;
  int ens_validity;
  int temp_tenths_c;
  int humidity_pct;
} dashboard_state_t;

void dashboard_state_snapshot_store(const dashboard_state_t *state);
bool dashboard_state_snapshot_read(dashboard_state_t *out);
void dashboard_state_build_runtime(dashboard_state_t *state, bool *time_synced);

#endif
