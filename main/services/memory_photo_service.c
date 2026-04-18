#include "memory_photo_service.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "memory_photo";

static SemaphoreHandle_t s_memory_photo_mutex;
static uint16_t s_memory_photo[TFT_WIDTH * TFT_HEIGHT];
static bool s_memory_photo_loaded;
static bool s_memory_photo_cache_checked;

static bool ensure_memory_photo_mutex(void) {
  if (s_memory_photo_mutex != NULL) {
    return true;
  }

  s_memory_photo_mutex = xSemaphoreCreateMutex();
  if (s_memory_photo_mutex == NULL) {
    ESP_LOGE(TAG, "Cannot create memory photo mutex");
    return false;
  }
  return true;
}

static void memory_photo_clear_cache(void) {
  if (!ensure_memory_photo_mutex()) {
    return;
  }

  if (xSemaphoreTake(s_memory_photo_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "Memory photo cache lock timeout during clear");
    return;
  }
  memset(s_memory_photo, 0, sizeof(s_memory_photo));
  s_memory_photo_loaded = false;
  xSemaphoreGive(s_memory_photo_mutex);
}

bool memory_photo_service_restore_from_nvs(void) {
  if (s_memory_photo_cache_checked) {
    return s_memory_photo_loaded;
  }
  if (!ensure_memory_photo_mutex()) {
    return false;
  }

  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(MEMORY_PHOTO_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    s_memory_photo_cache_checked = true;
    memory_photo_clear_cache();
    return false;
  }
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Open memory photo namespace failed: %s",
             esp_err_to_name(ret));
    return false;
  }

  size_t blob_size = 0;
  ret = nvs_get_blob(nvs_handle, MEMORY_PHOTO_KEY_DATA, NULL, &blob_size);
  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(nvs_handle);
    s_memory_photo_cache_checked = true;
    memory_photo_clear_cache();
    return false;
  }
  if (ret != ESP_OK || blob_size != MEMORY_PHOTO_PIXEL_BYTES) {
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Read memory photo size failed: %s", esp_err_to_name(ret));
    } else {
      ESP_LOGW(TAG, "Memory photo blob has unexpected size: %u",
               (unsigned)blob_size);
    }
    nvs_close(nvs_handle);
    s_memory_photo_cache_checked = true;
    memory_photo_clear_cache();
    return false;
  }

  uint8_t *temp = malloc(blob_size);
  if (temp == NULL) {
    nvs_close(nvs_handle);
    ESP_LOGW(TAG, "Allocate temp memory photo buffer failed");
    return false;
  }

  ret = nvs_get_blob(nvs_handle, MEMORY_PHOTO_KEY_DATA, temp, &blob_size);
  nvs_close(nvs_handle);
  if (ret != ESP_OK) {
    free(temp);
    ESP_LOGW(TAG, "Read memory photo blob failed: %s", esp_err_to_name(ret));
    return false;
  }

  if (xSemaphoreTake(s_memory_photo_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
    free(temp);
    ESP_LOGW(TAG, "Memory photo cache lock timeout during restore");
    return false;
  }
  memcpy(s_memory_photo, temp, MEMORY_PHOTO_PIXEL_BYTES);
  s_memory_photo_loaded = true;
  s_memory_photo_cache_checked = true;
  xSemaphoreGive(s_memory_photo_mutex);
  free(temp);
  ESP_LOGI(TAG, "Memory photo restored from NVS");
  return true;
}

bool memory_photo_service_save_blob(const uint8_t *data, size_t data_len) {
  if (data == NULL || data_len != MEMORY_PHOTO_PIXEL_BYTES) {
    return false;
  }
  if (!ensure_memory_photo_mutex()) {
    return false;
  }

  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(MEMORY_PHOTO_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Open memory photo namespace for write failed: %s",
             esp_err_to_name(ret));
    return false;
  }

  ret = nvs_set_blob(nvs_handle, MEMORY_PHOTO_KEY_DATA, data, data_len);
  if (ret == ESP_OK) {
    ret = nvs_commit(nvs_handle);
  }
  nvs_close(nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Save memory photo blob failed: %s", esp_err_to_name(ret));
    return false;
  }

  if (xSemaphoreTake(s_memory_photo_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
    ESP_LOGW(TAG, "Memory photo cache lock timeout during save");
    s_memory_photo_cache_checked = false;
    s_memory_photo_loaded = false;
    return true;
  }
  memcpy(s_memory_photo, data, data_len);
  s_memory_photo_loaded = true;
  s_memory_photo_cache_checked = true;
  xSemaphoreGive(s_memory_photo_mutex);
  return true;
}

bool memory_photo_service_delete_blob(void) {
  if (!ensure_memory_photo_mutex()) {
    return false;
  }

  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(MEMORY_PHOTO_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    s_memory_photo_cache_checked = true;
    memory_photo_clear_cache();
    return true;
  }
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Open memory photo namespace for erase failed: %s",
             esp_err_to_name(ret));
    return false;
  }

  ret = nvs_erase_key(nvs_handle, MEMORY_PHOTO_KEY_DATA);
  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    ret = ESP_OK;
  }
  if (ret == ESP_OK) {
    ret = nvs_commit(nvs_handle);
  }
  nvs_close(nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Erase memory photo blob failed: %s", esp_err_to_name(ret));
    return false;
  }

  s_memory_photo_cache_checked = false;
  s_memory_photo_loaded = false;
  memory_photo_clear_cache();
  return true;
}

bool memory_photo_service_snapshot(uint16_t *dest, size_t pixel_count) {
  if (dest == NULL || pixel_count < (size_t)(TFT_WIDTH * TFT_HEIGHT)) {
    return false;
  }
  if (!ensure_memory_photo_mutex()) {
    return false;
  }
  if (!s_memory_photo_cache_checked) {
    memory_photo_service_restore_from_nvs();
  }
  if (!s_memory_photo_loaded) {
    return false;
  }
  if (xSemaphoreTake(s_memory_photo_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "Memory photo cache lock timeout during snapshot");
    return false;
  }
  memcpy(dest, s_memory_photo, MEMORY_PHOTO_PIXEL_BYTES);
  xSemaphoreGive(s_memory_photo_mutex);
  return true;
}
