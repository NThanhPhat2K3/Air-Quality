#include "alarm_service.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define ALARM_NAMESPACE "alarm_cfg"
#define ALARM_KEY_HOUR "hour"
#define ALARM_KEY_MINUTE "minute"
#define ALARM_KEY_ENABLED "enabled"
#define ALARM_KEY_CONFIGURED "configured"

static const char *TAG = "alarm_service";

typedef struct {
  bool configured;
  bool enabled;
  bool ringing;
  uint8_t hour;
  uint8_t minute;
  int last_trigger_yday;
  int last_trigger_minute;
} alarm_service_cache_t;

static SemaphoreHandle_t s_alarm_mutex;
static bool s_alarm_cache_loaded;
static alarm_service_cache_t s_alarm_cache = {
    .configured = false,
    .enabled = false,
    .ringing = false,
    .hour = 6,
    .minute = 30,
    .last_trigger_yday = -1,
    .last_trigger_minute = -1,
};

static bool ensure_alarm_mutex(void) {
  if (s_alarm_mutex != NULL) {
    return true;
  }

  s_alarm_mutex = xSemaphoreCreateMutex();
  if (s_alarm_mutex == NULL) {
    ESP_LOGE(TAG, "Cannot create alarm mutex");
    return false;
  }
  return true;
}

static void alarm_service_copy_out(alarm_service_state_t *out) {
  out->configured = s_alarm_cache.configured;
  out->enabled = s_alarm_cache.enabled;
  out->ringing = s_alarm_cache.ringing;
  out->hour = s_alarm_cache.hour;
  out->minute = s_alarm_cache.minute;
}

bool alarm_service_restore_from_nvs(void) {
  nvs_handle_t nvs_handle;
  esp_err_t ret;
  uint8_t hour = 6;
  uint8_t minute = 30;
  uint8_t enabled = 0;
  uint8_t configured = 0;

  if (s_alarm_cache_loaded) {
    return s_alarm_cache.configured;
  }
  if (!ensure_alarm_mutex()) {
    return false;
  }

  ret = nvs_open(ALARM_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    s_alarm_cache_loaded = true;
    return false;
  }
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Open alarm namespace failed: %s", esp_err_to_name(ret));
    return false;
  }

  ret = nvs_get_u8(nvs_handle, ALARM_KEY_HOUR, &hour);
  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "Read alarm hour failed: %s", esp_err_to_name(ret));
  }
  ret = nvs_get_u8(nvs_handle, ALARM_KEY_MINUTE, &minute);
  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "Read alarm minute failed: %s", esp_err_to_name(ret));
  }
  ret = nvs_get_u8(nvs_handle, ALARM_KEY_ENABLED, &enabled);
  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "Read alarm enabled failed: %s", esp_err_to_name(ret));
  }
  ret = nvs_get_u8(nvs_handle, ALARM_KEY_CONFIGURED, &configured);
  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "Read alarm configured failed: %s", esp_err_to_name(ret));
  }
  nvs_close(nvs_handle);

  if (xSemaphoreTake(s_alarm_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "Alarm cache lock timeout during restore");
    return false;
  }
  s_alarm_cache.hour = hour % 24;
  s_alarm_cache.minute = minute % 60;
  s_alarm_cache.enabled = (enabled != 0);
  s_alarm_cache.configured = (configured != 0);
  s_alarm_cache.ringing = false;
  s_alarm_cache.last_trigger_yday = -1;
  s_alarm_cache.last_trigger_minute = -1;
  s_alarm_cache_loaded = true;
  xSemaphoreGive(s_alarm_mutex);
  return s_alarm_cache.configured;
}

bool alarm_service_save_config(uint8_t hour, uint8_t minute, bool enabled) {
  nvs_handle_t nvs_handle;
  esp_err_t ret;

  if (!ensure_alarm_mutex()) {
    return false;
  }

  ret = nvs_open(ALARM_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Open alarm namespace for write failed: %s",
             esp_err_to_name(ret));
    return false;
  }

  ret = nvs_set_u8(nvs_handle, ALARM_KEY_HOUR, hour % 24);
  if (ret == ESP_OK) {
    ret = nvs_set_u8(nvs_handle, ALARM_KEY_MINUTE, minute % 60);
  }
  if (ret == ESP_OK) {
    ret = nvs_set_u8(nvs_handle, ALARM_KEY_ENABLED, enabled ? 1 : 0);
  }
  if (ret == ESP_OK) {
    ret = nvs_set_u8(nvs_handle, ALARM_KEY_CONFIGURED, 1);
  }
  if (ret == ESP_OK) {
    ret = nvs_commit(nvs_handle);
  }
  nvs_close(nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Save alarm config failed: %s", esp_err_to_name(ret));
    return false;
  }

  if (xSemaphoreTake(s_alarm_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "Alarm cache lock timeout during save");
    return true;
  }
  s_alarm_cache.hour = hour % 24;
  s_alarm_cache.minute = minute % 60;
  s_alarm_cache.enabled = enabled;
  s_alarm_cache.configured = true;
  s_alarm_cache.ringing = false;
  s_alarm_cache.last_trigger_yday = -1;
  s_alarm_cache.last_trigger_minute = -1;
  s_alarm_cache_loaded = true;
  xSemaphoreGive(s_alarm_mutex);
  return true;
}

bool alarm_service_clear(void) {
  nvs_handle_t nvs_handle;
  esp_err_t ret;

  if (!ensure_alarm_mutex()) {
    return false;
  }

  ret = nvs_open(ALARM_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    ret = ESP_OK;
  }
  if (ret == ESP_OK) {
    ret = nvs_erase_all(nvs_handle);
    if (ret == ESP_OK) {
      ret = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
  } else {
    ESP_LOGW(TAG, "Open alarm namespace for erase failed: %s",
             esp_err_to_name(ret));
    return false;
  }

  if (xSemaphoreTake(s_alarm_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "Alarm cache lock timeout during clear");
    return true;
  }
  s_alarm_cache.configured = false;
  s_alarm_cache.enabled = false;
  s_alarm_cache.ringing = false;
  s_alarm_cache.hour = 6;
  s_alarm_cache.minute = 30;
  s_alarm_cache.last_trigger_yday = -1;
  s_alarm_cache.last_trigger_minute = -1;
  s_alarm_cache_loaded = true;
  xSemaphoreGive(s_alarm_mutex);
  return true;
}

void alarm_service_sync_clock(const struct tm *clock_info) {
  int minute_of_day;
  bool should_ring;

  if (clock_info == NULL || !ensure_alarm_mutex()) {
    return;
  }
  if (!s_alarm_cache_loaded) {
    alarm_service_restore_from_nvs();
  }
  if (xSemaphoreTake(s_alarm_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return;
  }

  minute_of_day = (clock_info->tm_hour * 60) + clock_info->tm_min;
  should_ring = s_alarm_cache.configured && s_alarm_cache.enabled &&
                clock_info->tm_hour == s_alarm_cache.hour &&
                clock_info->tm_min == s_alarm_cache.minute;

  if (should_ring) {
    if (s_alarm_cache.last_trigger_yday != clock_info->tm_yday ||
        s_alarm_cache.last_trigger_minute != minute_of_day) {
      s_alarm_cache.last_trigger_yday = clock_info->tm_yday;
      s_alarm_cache.last_trigger_minute = minute_of_day;
    }
    s_alarm_cache.ringing = true;
  } else {
    s_alarm_cache.ringing = false;
  }

  xSemaphoreGive(s_alarm_mutex);
}

void alarm_service_get_state(alarm_service_state_t *out) {
  if (out == NULL || !ensure_alarm_mutex()) {
    return;
  }
  if (!s_alarm_cache_loaded) {
    alarm_service_restore_from_nvs();
  }
  if (xSemaphoreTake(s_alarm_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    memset(out, 0, sizeof(*out));
    return;
  }
  alarm_service_copy_out(out);
  xSemaphoreGive(s_alarm_mutex);
}
