#include "dashboard_state.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define APP_SENSOR_SMOKE_TEST 1
#define DEMO_AQI_SWEEP 1
#define DEMO_AQI_STEP_SECONDS 6

static const char *TAG = "dashboard_state";

static dashboard_state_t s_latest_dashboard_state;
static bool s_latest_dashboard_state_valid;
static portMUX_TYPE s_dashboard_state_lock = portMUX_INITIALIZER_UNLOCKED;

static void fill_smoke_test_sensor_state(dashboard_state_t *state) {
  int elapsed;
  int temp_wave;
  int hum_wave;
  int eco2_wave;
  int tvoc_wave;

  if (state == NULL) {
    return;
  }

  elapsed = (int)(esp_timer_get_time() / 1000000LL);
  temp_wave = (elapsed % 9) - 4;
  hum_wave = (elapsed % 7) - 3;
  eco2_wave = (elapsed % 11) - 5;
  tvoc_wave = (elapsed % 13) - 6;

#if DEMO_AQI_SWEEP
  {
    static const int kDemoAqiLevels[] = {1, 2, 3, 4, 5};
    static const int kDemoEco2Levels[] = {420, 720, 950, 1300, 1800};
    static const int kDemoTvocLevels[] = {35, 120, 260, 420, 650};
    int stage = (elapsed / DEMO_AQI_STEP_SECONDS) %
                (int)(sizeof(kDemoAqiLevels) / sizeof(kDemoAqiLevels[0]));
    state->aqi = kDemoAqiLevels[stage];
    state->eco2_ppm = kDemoEco2Levels[stage] + (eco2_wave * 2);
    state->tvoc_ppb = kDemoTvocLevels[stage] + (tvoc_wave * 4);
    state->ens_validity = 0;
  }
#else
  state->aqi = 2;
  state->eco2_ppm = 742 + (eco2_wave * 4);
  state->tvoc_ppb = 145 + (tvoc_wave * 6);
  if (elapsed < 20) {
    state->ens_validity = 2;
  } else if (elapsed < 40) {
    state->ens_validity = 1;
  } else {
    state->ens_validity = 0;
  }
#endif

  state->temp_tenths_c = 290 + temp_wave;
  state->humidity_pct = 63 + hum_wave;
}

void dashboard_state_snapshot_store(const dashboard_state_t *state) {
  if (state == NULL) {
    return;
  }

  portENTER_CRITICAL(&s_dashboard_state_lock);
  s_latest_dashboard_state = *state;
  s_latest_dashboard_state_valid = true;
  portEXIT_CRITICAL(&s_dashboard_state_lock);
}

bool dashboard_state_snapshot_read(dashboard_state_t *out) {
  bool valid = false;

  if (out == NULL) {
    return false;
  }

  portENTER_CRITICAL(&s_dashboard_state_lock);
  if (s_latest_dashboard_state_valid) {
    *out = s_latest_dashboard_state;
    valid = true;
  }
  portEXIT_CRITICAL(&s_dashboard_state_lock);
  return valid;
}

void dashboard_state_build_runtime(dashboard_state_t *state, bool *time_synced) {
  static bool initialized;
  static time_t base_epoch;
  bool synced = (time_synced != NULL) ? *time_synced : false;

  if (state == NULL) {
    return;
  }

  if (!initialized) {
    struct tm demo_start = {
        .tm_year = 124,
        .tm_mon = 3,
        .tm_mday = 22,
        .tm_hour = 14,
        .tm_min = 45,
        .tm_sec = 23,
    };
    base_epoch = mktime(&demo_start);
    initialized = true;
  }

  if (!synced) {
    time_t now = 0;
    time(&now);
    if (now >= 1700000000) {
      synced = true;
      ESP_LOGI(TAG, "Real time became available, switching from demo clock");
    }
  }

  if (synced) {
    time_t now = 0;
    time(&now);
    localtime_r(&now, &state->clock);
  } else {
    int elapsed = (int)(esp_timer_get_time() / 1000000LL);
    time_t current = base_epoch + elapsed;
    localtime_r(&current, &state->clock);
  }

#if APP_SENSOR_SMOKE_TEST
  fill_smoke_test_sensor_state(state);
#else
  state->aqi = 0;
  state->eco2_ppm = 0;
  state->tvoc_ppb = 0;
  state->ens_validity = 3;
  state->temp_tenths_c = 0;
  state->humidity_pct = 0;
#endif

  if (time_synced != NULL) {
    *time_synced = synced;
  }
}
