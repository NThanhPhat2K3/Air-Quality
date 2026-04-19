#include "connectivity_service.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "captive_dns.h"
#include "dashboard_state.h"
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/portmacro.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "mdns.h"
#include "memory_photo_service.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui_flow.h"

#ifndef WIFI_SSID
#define WIFI_SSID "PQ"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "99999999"
#endif

#define WIFI_MAXIMUM_RETRY 10
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define CLOCK_SYNC_DONE_BIT BIT2
#define CLOCK_SYNC_OK_BIT BIT3
#define CLOCK_SYNC_SUCCESS_INTERVAL_MS (6 * 60 * 60 * 1000)
#define CLOCK_SYNC_RETRY_INTERVAL_MS (60 * 1000)
#define CLOCK_SYNC_INITIAL_WAIT_MS (30 * 1000)
#define SNTP_SETTLE_DELAY_MS 3000

#define WIFI_CREDENTIALS_NAMESPACE "wifi_cfg"
#define WIFI_CREDENTIALS_KEY_SSID "ssid"
#define WIFI_CREDENTIALS_KEY_PASS "pass"
#define WIFI_CREDENTIALS_KEY_HIDDEN "hidden"
#define WIFI_PROVISIONING_AP_PASS "setup123"
#define WIFI_PROVISIONING_MAX_CONN 4
#define WIFI_PROVISIONING_BODY_MAX_LEN 256
#define WIFI_PROVISIONING_RESTART_DELAY_MS 1500
#define WIFI_FLASH_WRITE_COOLDOWN_MS (30 * 1000)
#define WIFI_SCAN_MAX_RESULTS 24
#define WIFI_SCAN_JSON_MAX_LEN 4096
#define WIFI_STATE_JSON_MAX_LEN 1024
#define WIFI_CONNECT_FAIL_PORTAL_THRESHOLD 1
#define WIFI_TARGET_MISSING_PORTAL_THRESHOLD 2
#define MEMORY_PHOTO_JSON_MAX_LEN 256
#define RUNTIME_MDNS_INSTANCE "AQ Node"
#define WIFI_TEST_CONNECT_TIMEOUT_MS 15000
#define WIFI_TEST_MAX_RETRY 3
#define WIFI_AUTO_RECONNECT_PROBE_TIMEOUT_MS 15000

typedef struct {
  char ssid[33];
  char password[65];
  bool hidden;
  bool valid;
  bool from_nvs;
} wifi_credentials_t;

typedef enum {
  WIFI_TARGET_SCAN_FOUND = 0,
  WIFI_TARGET_SCAN_NOT_FOUND,
  WIFI_TARGET_SCAN_ERROR,
} wifi_target_scan_result_t;

typedef enum {
  WIFI_CONNECT_RESULT_CONNECTED = 0,
  WIFI_CONNECT_RESULT_TARGET_NOT_FOUND,
  WIFI_CONNECT_RESULT_FAILED,
} wifi_connect_result_t;

static const char *TAG = "connectivity";

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_wifi_sta_netif;
static esp_netif_t *s_wifi_ap_netif;
static TaskHandle_t s_clock_sync_task_handle;
static TimerHandle_t s_clock_sync_timer;
static httpd_handle_t s_config_http_server;
/*
 * These Wi-Fi flags/counters are touched from multiple FreeRTOS contexts.
 * Use _Atomic so individual loads/stores stay race-free and visible across
 * cores.  This does NOT make multi-field state transitions atomic; grouped
 * state still needs explicit locking/snapshot rules.
 */
static _Atomic int s_wifi_retry_num;
static bool s_time_synced;
static _Atomic bool s_wifi_should_connect;
static bool s_wifi_driver_ready;
static bool s_wifi_credentials_loaded;
static _Atomic bool s_provisioning_portal_active;
static _Atomic bool s_provisioning_auto_opened;
static bool s_restart_scheduled;
static bool s_http_server_recycle_scheduled;
static bool s_mdns_ready;
static int64_t s_last_credentials_write_us;
static _Atomic int s_wifi_connect_fail_cycles;
static _Atomic int s_wifi_target_missing_cycles;
static wifi_credentials_t s_wifi_credentials;
static char s_provisioning_ap_ssid[33];
static portMUX_TYPE s_connectivity_state_lock = portMUX_INITIALIZER_UNLOCKED;

extern const uint8_t web_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t web_index_html_end[] asm("_binary_index_html_end");
extern const uint8_t web_app_css_start[] asm("_binary_app_css_start");
extern const uint8_t web_app_css_end[] asm("_binary_app_css_end");
extern const uint8_t web_app_js_start[] asm("_binary_app_js_start");
extern const uint8_t web_app_js_end[] asm("_binary_app_js_end");

typedef enum {
  WIFI_TEST_RESULT_CONNECTED = 0,
  WIFI_TEST_RESULT_WRONG_PASSWORD,
  WIFI_TEST_RESULT_NOT_FOUND,
  WIFI_TEST_RESULT_TIMEOUT,
  WIFI_TEST_RESULT_ERROR,
} wifi_test_result_t;

static void schedule_clock_sync_retry(uint32_t delay_ms);
static void wifi_refresh_credentials_cache(void);
static void wifi_clear_status_bits(void);
static bool start_provisioning_portal(void);
static bool stop_provisioning_portal(void);
static bool start_config_http_server(void);
static void stop_config_http_server(void);
static void schedule_http_server_recycle(uint32_t delay_ms);
static void fill_sta_wifi_config(wifi_config_t *wifi_config);
static void ensure_mdns_service(void);
static wifi_target_scan_result_t wifi_scan_for_target_ssid(void);

static esp_err_t init_nvs_flash_storage(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  return ret;
}

static bool is_placeholder_credentials(const char *ssid, const char *password) {
  if (ssid == NULL || ssid[0] == '\0' || strcmp(ssid, "YOUR_WIFI_SSID") == 0) {
    return true;
  }
  if (password != NULL && strcmp(password, "YOUR_WIFI_PASSWORD") == 0) {
    return true;
  }
  return false;
}

static wifi_credentials_t wifi_credentials_snapshot(void) {
  wifi_credentials_t snap;
  portENTER_CRITICAL(&s_connectivity_state_lock);
  snap = s_wifi_credentials;
  portEXIT_CRITICAL(&s_connectivity_state_lock);
  return snap;
}

static void wifi_set_credentials_cache(const char *ssid, const char *password,
                                       bool hidden, bool from_nvs) {
  portENTER_CRITICAL(&s_connectivity_state_lock);
  memset(&s_wifi_credentials, 0, sizeof(s_wifi_credentials));
  if (ssid != NULL) {
    strlcpy(s_wifi_credentials.ssid, ssid, sizeof(s_wifi_credentials.ssid));
  }
  if (password != NULL) {
    strlcpy(s_wifi_credentials.password, password,
            sizeof(s_wifi_credentials.password));
  }
  s_wifi_credentials.hidden = hidden;
  s_wifi_credentials.valid = s_wifi_credentials.ssid[0] != '\0';
  s_wifi_credentials.from_nvs = from_nvs;
  portEXIT_CRITICAL(&s_connectivity_state_lock);
}

static bool wifi_load_credentials_from_nvs(void) {
  nvs_handle_t nvs_handle;
  esp_err_t ret =
      nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (ret != ESP_OK) {
    return false;
  }

  char ssid[sizeof(s_wifi_credentials.ssid)] = {0};
  char password[sizeof(s_wifi_credentials.password)] = {0};
  uint8_t hidden = 0;
  size_t ssid_len = sizeof(ssid);
  size_t password_len = sizeof(password);

  ret = nvs_get_str(nvs_handle, WIFI_CREDENTIALS_KEY_SSID, ssid, &ssid_len);
  if (ret != ESP_OK || ssid[0] == '\0') {
    nvs_close(nvs_handle);
    return false;
  }

  ret = nvs_get_str(nvs_handle, WIFI_CREDENTIALS_KEY_PASS, password,
                    &password_len);
  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    password[0] = '\0';
  } else if (ret != ESP_OK) {
    nvs_close(nvs_handle);
    return false;
  }

  ret = nvs_get_u8(nvs_handle, WIFI_CREDENTIALS_KEY_HIDDEN, &hidden);
  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    hidden = 0;
  } else if (ret != ESP_OK) {
    nvs_close(nvs_handle);
    return false;
  }

  wifi_set_credentials_cache(ssid, password, hidden != 0, true);
  nvs_close(nvs_handle);
  return true;
}

static bool wifi_credentials_equal(const char *ssid, const char *password,
                                   bool hidden) {
  const char *safe_password = password != NULL ? password : "";
  if (ssid == NULL || ssid[0] == '\0') {
    return false;
  }
  wifi_refresh_credentials_cache();
  wifi_credentials_t creds = wifi_credentials_snapshot();
  if (!creds.valid) {
    return false;
  }
  return strcmp(creds.ssid, ssid) == 0 &&
         strcmp(creds.password, safe_password) == 0 &&
         creds.hidden == hidden;
}

static esp_err_t wifi_save_credentials_to_nvs(const char *ssid,
                                              const char *password, bool hidden,
                                              bool *did_write) {
  const char *safe_password = password != NULL ? password : "";
  int64_t now_us = esp_timer_get_time();

  if (did_write != NULL) {
    *did_write = false;
  }
  if (ssid == NULL || ssid[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }
  if (wifi_credentials_equal(ssid, safe_password, hidden)) {
    ESP_LOGI(TAG, "Credentials unchanged, skip NVS write");
    return ESP_OK;
  }
  if (s_last_credentials_write_us > 0 &&
      (now_us - s_last_credentials_write_us) <
          (WIFI_FLASH_WRITE_COOLDOWN_MS * 1000LL)) {
    ESP_LOGW(TAG, "Credentials write throttled to protect flash");
    return ESP_ERR_INVALID_STATE;
  }

  nvs_handle_t nvs_handle;
  esp_err_t ret =
      nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = nvs_set_str(nvs_handle, WIFI_CREDENTIALS_KEY_SSID, ssid);
  if (ret == ESP_OK) {
    ret = nvs_set_str(nvs_handle, WIFI_CREDENTIALS_KEY_PASS, safe_password);
  }
  if (ret == ESP_OK) {
    ret = nvs_set_u8(nvs_handle, WIFI_CREDENTIALS_KEY_HIDDEN, hidden ? 1 : 0);
  }
  if (ret == ESP_OK) {
    ret = nvs_commit(nvs_handle);
  }
  nvs_close(nvs_handle);

  if (ret == ESP_OK) {
    s_last_credentials_write_us = now_us;
    wifi_set_credentials_cache(ssid, safe_password, hidden, true);
    s_wifi_credentials_loaded = true;
    s_provisioning_portal_active = false;
    if (did_write != NULL) {
      *did_write = true;
    }
  }
  return ret;
}

static void wifi_refresh_credentials_cache(void) {
  if (s_wifi_credentials_loaded) {
    return;
  }

  if (wifi_load_credentials_from_nvs()) {
    ESP_LOGI(TAG, "Wi-Fi credentials loaded from NVS (ssid=%s)",
             wifi_credentials_snapshot().ssid);
    s_wifi_credentials_loaded = true;
    return;
  }

  if (!is_placeholder_credentials(WIFI_SSID, WIFI_PASS)) {
    wifi_set_credentials_cache(WIFI_SSID, WIFI_PASS, false, false);
    ESP_LOGI(TAG, "Wi-Fi credentials loaded from compile-time config (ssid=%s)",
             wifi_credentials_snapshot().ssid);
  } else {
    wifi_set_credentials_cache("", "", false, false);
    ESP_LOGW(TAG, "No Wi-Fi credentials configured; start provisioning portal");
  }
  s_wifi_credentials_loaded = true;
}

static esp_err_t wifi_erase_credentials_nvs(void) {
  nvs_handle_t nvs_handle;
  esp_err_t ret =
      nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    return ret;
  }
  ret = nvs_erase_all(nvs_handle);
  if (ret == ESP_OK) {
    ret = nvs_commit(nvs_handle);
  }
  nvs_close(nvs_handle);

  if (ret == ESP_OK) {
    wifi_set_credentials_cache("", "", false, false);
    s_wifi_credentials_loaded = false;
    ESP_LOGI(TAG, "Wi-Fi credentials erased from NVS");
  }
  return ret;
}

static bool wifi_credentials_configured(void) {
  wifi_refresh_credentials_cache();
  return wifi_credentials_snapshot().valid;
}

static wifi_test_result_t wifi_test_credentials(const char *ssid,
                                                const char *password,
                                                bool hidden) {
  wifi_test_result_t result = WIFI_TEST_RESULT_ERROR;

  if (!s_wifi_driver_ready || s_wifi_event_group == NULL) {
    return WIFI_TEST_RESULT_ERROR;
  }

  wifi_config_t test_config = {0};
  test_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  test_config.sta.pmf_cfg.capable = true;
  test_config.sta.pmf_cfg.required = false;
  strlcpy((char *)test_config.sta.ssid, ssid,
          sizeof(test_config.sta.ssid));
  if (password != NULL) {
    strlcpy((char *)test_config.sta.password, password,
            sizeof(test_config.sta.password));
  }
  if (password == NULL || password[0] == '\0') {
    test_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  }

  /* pre-scan: check target SSID exists WITHOUT disconnecting current WiFi */
  if (!hidden) {
    wifi_scan_config_t pre_scan = {
        .ssid = NULL, .bssid = NULL, .channel = 0,
        .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t scan_ret = esp_wifi_scan_start(&pre_scan, true);
    if (scan_ret == ESP_ERR_WIFI_STATE) {
      esp_wifi_scan_stop();
      scan_ret = esp_wifi_scan_start(&pre_scan, true);
    }
    if (scan_ret == ESP_OK) {
      uint16_t ap_count = 0;
      esp_wifi_scan_get_ap_num(&ap_count);
      if (ap_count > WIFI_SCAN_MAX_RESULTS) ap_count = WIFI_SCAN_MAX_RESULTS;
      wifi_ap_record_t *aps = calloc(ap_count, sizeof(wifi_ap_record_t));
      bool found = false;
      if (aps != NULL) {
        esp_wifi_scan_get_ap_records(&ap_count, aps);
        for (uint16_t i = 0; i < ap_count; ++i) {
          if (strcmp((const char *)aps[i].ssid, ssid) == 0) {
            found = true;
            break;
          }
        }
        free(aps);
      }
      if (!found) {
        ESP_LOGW(TAG, "wifi_test: SSID '%s' not visible in pre-scan, "
                      "skip connect test", ssid);
        return WIFI_TEST_RESULT_NOT_FOUND;
      }
      ESP_LOGI(TAG, "wifi_test: SSID '%s' found in pre-scan, proceed", ssid);
    }
  }

  /* disconnect current STA, keep AP alive for web UI */
  s_wifi_should_connect = false;
  esp_wifi_disconnect();
  vTaskDelay(pdMS_TO_TICKS(300));

  esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "wifi_test: set_mode APSTA failed: %s",
             esp_err_to_name(ret));
    return WIFI_TEST_RESULT_ERROR;
  }

  ret = esp_wifi_set_config(WIFI_IF_STA, &test_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "wifi_test: set_config failed: %s", esp_err_to_name(ret));
    return WIFI_TEST_RESULT_ERROR;
  }

  /* allow only limited retries during the test */
  s_wifi_retry_num = WIFI_MAXIMUM_RETRY - WIFI_TEST_MAX_RETRY;
  s_wifi_should_connect = true;
  wifi_clear_status_bits();

  ret = esp_wifi_connect();
  if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
    ESP_LOGE(TAG, "wifi_test: connect failed: %s", esp_err_to_name(ret));
    s_wifi_should_connect = false;
    return WIFI_TEST_RESULT_ERROR;
  }

  ESP_LOGI(TAG, "wifi_test: testing credentials for SSID '%s'...", ssid);

  EventBits_t bits = xEventGroupWaitBits(
      s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE,
      pdFALSE, pdMS_TO_TICKS(WIFI_TEST_CONNECT_TIMEOUT_MS));

  if ((bits & WIFI_CONNECTED_BIT) != 0) {
    ESP_LOGI(TAG, "wifi_test: credentials verified OK");
    if (s_provisioning_portal_active) {
      schedule_http_server_recycle(400);
    }
    return WIFI_TEST_RESULT_CONNECTED;
  }

  /* test failed — disconnect and restore AP-only state */
  s_wifi_should_connect = false;
  esp_wifi_disconnect();
  s_wifi_retry_num = 0;

  if ((bits & WIFI_FAIL_BIT) != 0) {
    ESP_LOGW(TAG, "wifi_test: connection rejected (wrong password or auth)");
    result = WIFI_TEST_RESULT_WRONG_PASSWORD;
  } else {
    ESP_LOGW(TAG, "wifi_test: connection timed out");
    result = WIFI_TEST_RESULT_TIMEOUT;
  }

  /* restore provisioning AP if it was active, otherwise reconnect STA */
  if (s_provisioning_portal_active) {
    start_provisioning_portal();
  } else {
    /* restore STA to original credentials and reconnect immediately */
    wifi_config_t orig = {0};
    fill_sta_wifi_config(&orig);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &orig);
    s_wifi_should_connect = true;
    wifi_clear_status_bits();
    esp_wifi_connect();
  }
  return result;
}

static bool is_system_time_valid(void) {
  time_t now = 0;
  time(&now);
  return now >= 1700000000;
}

static void wifi_clear_status_bits(void) {
  if (s_wifi_event_group == NULL) {
    return;
  }
  xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
}

static void ensure_mdns_service(void) {
  if (s_mdns_ready) {
    return;
  }

  esp_err_t ret = mdns_init();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
    return;
  }

  ret = mdns_hostname_set(CONNECTIVITY_RUNTIME_HOSTNAME);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "mDNS hostname set failed: %s", esp_err_to_name(ret));
    return;
  }

  ret = mdns_instance_name_set(RUNTIME_MDNS_INSTANCE);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "mDNS instance set failed: %s", esp_err_to_name(ret));
    return;
  }

  ret = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "mDNS HTTP service add failed: %s", esp_err_to_name(ret));
    return;
  }

  s_mdns_ready = true;
  ESP_LOGI(TAG, "mDNS ready: http://%s.local", CONNECTIVITY_RUNTIME_HOSTNAME);
}

bool connectivity_service_is_wifi_connected(void) {
  if (s_wifi_event_group == NULL) {
    return false;
  }
  EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
  return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void delayed_restart_task(void *arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(WIFI_PROVISIONING_RESTART_DELAY_MS));
  esp_restart();
}

static void schedule_delayed_restart(void) {
  if (s_restart_scheduled) {
    return;
  }
  s_restart_scheduled = true;
  BaseType_t task_ok =
      xTaskCreate(delayed_restart_task, "cfg_restart", 2048, NULL, 5, NULL);
  if (task_ok != pdPASS) {
    s_restart_scheduled = false;
    ESP_LOGE(TAG, "Failed to schedule delayed restart");
  }
}

static void url_decode_inplace(char *value) {
  char *read = value;
  char *write = value;

  if (value == NULL) {
    return;
  }

  while (*read != '\0') {
    if (*read == '+') {
      *write++ = ' ';
      read++;
      continue;
    }

    if (*read == '%' && isxdigit((unsigned char)read[1]) &&
        isxdigit((unsigned char)read[2])) {
      int hi = isdigit((unsigned char)read[1])
                   ? (read[1] - '0')
                   : (tolower((unsigned char)read[1]) - 'a' + 10);
      int lo = isdigit((unsigned char)read[2])
                   ? (read[2] - '0')
                   : (tolower((unsigned char)read[2]) - 'a' + 10);
      *write++ = (char)((hi << 4) | lo);
      read += 3;
      continue;
    }

    *write++ = *read++;
  }
  *write = '\0';
}

static bool form_get_value(const char *body, const char *key, char *out,
                           size_t out_len) {
  size_t key_len = strlen(key);
  const char *cursor = body;

  if (body == NULL || key == NULL || out == NULL || out_len == 0) {
    return false;
  }

  while (cursor != NULL && *cursor != '\0') {
    const char *separator = strchr(cursor, '&');
    size_t token_len =
        separator != NULL ? (size_t)(separator - cursor) : strlen(cursor);

    if (token_len >= key_len + 1 && strncmp(cursor, key, key_len) == 0 &&
        cursor[key_len] == '=') {
      size_t value_len = token_len - key_len - 1;
      if (value_len >= out_len) {
        value_len = out_len - 1;
      }
      memcpy(out, cursor + key_len + 1, value_len);
      out[value_len] = '\0';
      url_decode_inplace(out);
      return true;
    }

    cursor = (separator != NULL) ? (separator + 1) : NULL;
  }

  return false;
}

static bool form_value_is_truthy(const char *body, const char *key) {
  char value[16] = {0};

  if (!form_get_value(body, key, value, sizeof(value))) {
    return false;
  }

  return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
         strcmp(value, "on") == 0 || strcmp(value, "yes") == 0;
}

static bool has_control_chars(const char *value) {
  if (value == NULL) {
    return true;
  }
  for (size_t i = 0; value[i] != '\0'; ++i) {
    unsigned char ch = (unsigned char)value[i];
    if (ch < 0x20 || ch == 0x7F) {
      return true;
    }
  }
  return false;
}

static void json_escape(const char *source, char *dest, size_t dest_size) {
  size_t write = 0;

  if (dest_size == 0) {
    return;
  }
  if (source == NULL) {
    dest[0] = '\0';
    return;
  }

  for (size_t i = 0; source[i] != '\0' && write + 1 < dest_size; ++i) {
    unsigned char ch = (unsigned char)source[i];
    const char *escape = NULL;
    char unicode_escape[7] = {0};

    switch (ch) {
    case '\\':
      escape = "\\\\";
      break;
    case '"':
      escape = "\\\"";
      break;
    case '\b':
      escape = "\\b";
      break;
    case '\f':
      escape = "\\f";
      break;
    case '\n':
      escape = "\\n";
      break;
    case '\r':
      escape = "\\r";
      break;
    case '\t':
      escape = "\\t";
      break;
    default:
      break;
    }

    if (escape != NULL) {
      size_t escape_len = strlen(escape);
      if (write + escape_len >= dest_size) {
        break;
      }
      memcpy(dest + write, escape, escape_len);
      write += escape_len;
      continue;
    }

    if (ch < 0x20) {
      if (write + 6 >= dest_size) {
        break;
      }
      snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04X", ch);
      memcpy(dest + write, unicode_escape, 6);
      write += 6;
      continue;
    }

    dest[write++] = (char)ch;
  }
  dest[write] = '\0';
}

static void set_common_http_headers(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(
      req, "Content-Security-Policy",
      "default-src 'self'; script-src 'self'; style-src 'self'; connect-src "
      "'self'; img-src 'self' data:; base-uri 'none'; form-action 'self';");
}

static esp_err_t send_json_response(httpd_req_t *req, const char *status,
                                    const char *json_payload) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  set_common_http_headers(req);
  return httpd_resp_send(req, json_payload, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_json_message(httpd_req_t *req, const char *status,
                                   bool ok, const char *message) {
  char escaped_message[220];
  char payload[300];

  json_escape(message != NULL ? message : "", escaped_message,
              sizeof(escaped_message));
  snprintf(payload, sizeof(payload), "{\"ok\":%s,\"message\":\"%s\"}",
           ok ? "true" : "false", escaped_message);
  return send_json_response(req, status, payload);
}

static bool read_http_body(httpd_req_t *req, char *out, size_t out_len) {
  int received = 0;
  int remaining = req->content_len;

  if (req == NULL || out == NULL || out_len == 0) {
    return false;
  }
  if (req->content_len <= 0 || (size_t)req->content_len >= out_len) {
    return false;
  }

  while (remaining > 0) {
    int ret = httpd_req_recv(req, out + received, remaining);
    if (ret <= 0) {
      return false;
    }
    received += ret;
    remaining -= ret;
  }
  out[received] = '\0';
  return true;
}

static void get_netif_ip_string(esp_netif_t *netif, char *out, size_t out_len) {
  if (out == NULL || out_len == 0) {
    return;
  }

  out[0] = '\0';
  if (netif == NULL) {
    return;
  }

  esp_netif_ip_info_t ip_info = {0};
  if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
    snprintf(out, out_len, IPSTR, IP2STR(&ip_info.ip));
  }
}

static bool read_http_body_bytes(httpd_req_t *req, uint8_t *out,
                                 size_t expected_len) {
  size_t received = 0;

  if (req == NULL || out == NULL || expected_len == 0) {
    return false;
  }
  if (req->content_len != (int)expected_len) {
    ESP_LOGW(TAG, "Unexpected binary upload size: got=%d expected=%u",
             req->content_len, (unsigned)expected_len);
    return false;
  }

  while (received < expected_len) {
    int remaining = (int)(expected_len - received);
    int ret = httpd_req_recv(req, (char *)out + received, remaining);
    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      return false;
    }
    received += (size_t)ret;
  }
  return true;
}

static esp_err_t serve_embedded_file(httpd_req_t *req, const uint8_t *start,
                                     const uint8_t *end,
                                     const char *content_type) {
  size_t data_len;

  if (start == NULL || end == NULL || end < start) {
    return ESP_FAIL;
  }

  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, content_type);
  set_common_http_headers(req);
  data_len = (size_t)(end - start);
  if (data_len > 0 && start[data_len - 1] == '\0') {
    data_len--;
  }
  return httpd_resp_send(req, (const char *)start, data_len);
}

static esp_err_t config_root_get_handler(httpd_req_t *req) {
  return serve_embedded_file(req, web_index_html_start, web_index_html_end,
                             "text/html; charset=utf-8");
}

static esp_err_t config_css_get_handler(httpd_req_t *req) {
  return serve_embedded_file(req, web_app_css_start, web_app_css_end,
                             "text/css; charset=utf-8");
}

static esp_err_t config_js_get_handler(httpd_req_t *req) {
  return serve_embedded_file(req, web_app_js_start, web_app_js_end,
                             "application/javascript; charset=utf-8");
}

static esp_err_t config_captive_redirect_get_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  set_common_http_headers(req);
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  return httpd_resp_send(req, "Redirecting to setup portal",
                         HTTPD_RESP_USE_STRLEN);
}

static esp_err_t config_state_get_handler(httpd_req_t *req) {
  char escaped_ssid[160];
  char escaped_ap_ssid[160];
  char runtime_ip[20] = "";
  char ap_ip[20] = "";
  char escaped_access_url[96];
  char payload[WIFI_STATE_JSON_MAX_LEN];
  wifi_credentials_t creds_snap;
  bool portal_snap;
  const char *access_ip = NULL;
  char access_url[80] = "";

  portENTER_CRITICAL(&s_connectivity_state_lock);
  creds_snap = s_wifi_credentials;
  portal_snap = s_provisioning_portal_active;
  portEXIT_CRITICAL(&s_connectivity_state_lock);

  json_escape(creds_snap.valid ? creds_snap.ssid : "",
              escaped_ssid, sizeof(escaped_ssid));
  json_escape(s_provisioning_ap_ssid, escaped_ap_ssid, sizeof(escaped_ap_ssid));

  get_netif_ip_string(s_wifi_sta_netif, runtime_ip, sizeof(runtime_ip));
  get_netif_ip_string(s_wifi_ap_netif, ap_ip, sizeof(ap_ip));

  access_ip = portal_snap ? ap_ip : runtime_ip;
  if (access_ip[0] != '\0') {
    snprintf(access_url, sizeof(access_url), "http://%s/", access_ip);
  } else if (!portal_snap) {
    snprintf(access_url, sizeof(access_url), "http://%s.local/",
             CONNECTIVITY_RUNTIME_HOSTNAME);
  }
  json_escape(access_url, escaped_access_url, sizeof(escaped_access_url));

  snprintf(payload, sizeof(payload),
           "{\"ok\":true,\"mode\":\"%s\",\"connected\":%s,"
           "\"provisioning\":%s,\"credentialSource\":\"%s\","
           "\"currentSsid\":\"%s\",\"apSsid\":\"%s\",\"apPassword\":\"%s\","
           "\"hiddenSsid\":%s,\"canStartPortal\":%s,\"runtimeIp\":\"%s\","
           "\"runtimeHost\":\"%s.local\",\"apIp\":\"%s\","
           "\"accessIp\":\"%s\",\"accessUrl\":\"%s\"}",
           portal_snap ? "provisioning" : "runtime",
           connectivity_service_is_wifi_connected() ? "true" : "false",
           portal_snap ? "true" : "false",
           creds_snap.valid
               ? (creds_snap.from_nvs ? "nvs" : "build")
               : "none",
           escaped_ssid, escaped_ap_ssid, WIFI_PROVISIONING_AP_PASS,
           creds_snap.hidden ? "true" : "false",
           creds_snap.valid ? "true" : "false", runtime_ip,
           CONNECTIVITY_RUNTIME_HOSTNAME, ap_ip, access_ip, escaped_access_url);

  return send_json_response(req, "200 OK", payload);
}

static esp_err_t config_telemetry_get_handler(httpd_req_t *req) {
  dashboard_state_t state = {0};
  bool synced = connectivity_service_is_time_synced();

  if (!dashboard_state_snapshot_read(&state)) {
    dashboard_state_build_runtime(&state, &synced);
    connectivity_service_set_time_synced(synced);
  }

  char payload[512];
  snprintf(payload, sizeof(payload),
           "{\"ok\":true,\"aqi\":%d,\"eco2\":%d,\"tvoc\":%d,\"ensValidity\":%d,"
           "\"tempC\":%.1f,\"humidity\":%d}",
           state.aqi, state.eco2_ppm, state.tvoc_ppb, state.ens_validity,
           state.temp_tenths_c / 10.0f, state.humidity_pct);
  return send_json_response(req, "200 OK", payload);
}

static esp_err_t config_memory_get_handler(httpd_req_t *req) {
  char payload[MEMORY_PHOTO_JSON_MAX_LEN];
  bool ready = memory_photo_service_restore_from_nvs();

  snprintf(payload, sizeof(payload),
           "{\"ok\":true,\"ready\":%s,\"width\":%d,\"height\":%d,\"bytes\":%d}",
           ready ? "true" : "false", TFT_WIDTH, TFT_HEIGHT,
           MEMORY_PHOTO_PIXEL_BYTES);
  return send_json_response(req, "200 OK", payload);
}

static esp_err_t config_memory_post_handler(httpd_req_t *req) {
  char content_type[80] = {0};

  if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type,
                                  sizeof(content_type)) == ESP_OK) {
    if (strncmp(content_type, "application/octet-stream", 24) != 0) {
      return send_json_message(req, "415 Unsupported Media Type", false,
                               "Upload must use application/octet-stream");
    }
  }

  uint8_t *upload = malloc(MEMORY_PHOTO_PIXEL_BYTES);
  if (upload == NULL) {
    return send_json_message(req, "500 Internal Server Error", false,
                             "Out of memory");
  }

  bool body_ok = read_http_body_bytes(req, upload, MEMORY_PHOTO_PIXEL_BYTES);
  if (!body_ok) {
    free(upload);
    return send_json_message(req, "400 Bad Request", false,
                             "Invalid memory photo payload");
  }

  bool saved =
      memory_photo_service_save_blob(upload, MEMORY_PHOTO_PIXEL_BYTES);
  free(upload);
  if (!saved) {
    return send_json_message(req, "500 Internal Server Error", false,
                             "Failed to save memory photo");
  }

  ui_flow_dispatch(&(ui_flow_event_t){
      .type = UI_FLOW_EVENT_MEMORY_PHOTO_UPDATED,
  });
  ESP_LOGI(TAG, "Memory photo updated successfully");
  return send_json_message(req, "200 OK", true,
                           "Memory photo updated on device.");
}

static esp_err_t config_memory_delete_handler(httpd_req_t *req) {
  if (!memory_photo_service_delete_blob()) {
    return send_json_message(req, "500 Internal Server Error", false,
                             "Failed to clear memory photo");
  }
  ui_flow_dispatch(&(ui_flow_event_t){
      .type = UI_FLOW_EVENT_MEMORY_PHOTO_CLEARED,
  });
  return send_json_message(req, "200 OK", true, "Memory photo cleared.");
}

static const char *wifi_auth_mode_to_text(wifi_auth_mode_t auth_mode) {
  switch (auth_mode) {
  case WIFI_AUTH_OPEN:
    return "OPEN";
  case WIFI_AUTH_WEP:
    return "WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WPA_PSK";
  case WIFI_AUTH_WPA2_PSK:
    return "WPA2_PSK";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WPA_WPA2_PSK";
  case WIFI_AUTH_WPA2_ENTERPRISE:
    return "WPA2_ENTERPRISE";
#ifdef WIFI_AUTH_WPA3_PSK
  case WIFI_AUTH_WPA3_PSK:
    return "WPA3_PSK";
#endif
#ifdef WIFI_AUTH_WPA2_WPA3_PSK
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WPA2_WPA3_PSK";
#endif
#ifdef WIFI_AUTH_WAPI_PSK
  case WIFI_AUTH_WAPI_PSK:
    return "WAPI_PSK";
#endif
#ifdef WIFI_AUTH_OWE
  case WIFI_AUTH_OWE:
    return "OWE";
#endif
  default:
    return "UNKNOWN";
  }
}

static esp_err_t config_scan_get_handler(httpd_req_t *req) {
  wifi_scan_config_t scan_config = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = false,
      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
  };
  esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
  if (ret == ESP_ERR_WIFI_STATE) {
    esp_wifi_scan_stop();
    ret = esp_wifi_scan_start(&scan_config, true);
  }
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Wi-Fi scan start failed: %s", esp_err_to_name(ret));
    return send_json_message(req, "503 Service Unavailable", false,
                             "Wi-Fi scan unavailable");
  }

  uint16_t ap_count = 0;
  ret = esp_wifi_scan_get_ap_num(&ap_count);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Wi-Fi scan ap_num failed: %s", esp_err_to_name(ret));
    return send_json_message(req, "500 Internal Server Error", false,
                             "Failed to read scan result");
  }
  if (ap_count > WIFI_SCAN_MAX_RESULTS) {
    ap_count = WIFI_SCAN_MAX_RESULTS;
  }

  wifi_ap_record_t *ap_records =
      calloc(WIFI_SCAN_MAX_RESULTS, sizeof(wifi_ap_record_t));
  if (ap_records == NULL) {
    return send_json_message(req, "500 Internal Server Error", false,
                             "Out of memory");
  }

  ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Wi-Fi scan records failed: %s", esp_err_to_name(ret));
    free(ap_records);
    return send_json_message(req, "500 Internal Server Error", false,
                             "Failed to load scan records");
  }

  char *payload = malloc(WIFI_SCAN_JSON_MAX_LEN);
  char(*seen_ssids)[33] = calloc(WIFI_SCAN_MAX_RESULTS, sizeof(*seen_ssids));
  if (payload == NULL || seen_ssids == NULL) {
    free(seen_ssids);
    free(payload);
    free(ap_records);
    return send_json_message(req, "500 Internal Server Error", false,
                             "Out of memory");
  }

  int written =
      snprintf(payload, WIFI_SCAN_JSON_MAX_LEN, "{\"ok\":true,\"items\":[");
  if (written < 0 || (size_t)written >= WIFI_SCAN_JSON_MAX_LEN) {
    free(seen_ssids);
    free(payload);
    free(ap_records);
    return send_json_message(req, "500 Internal Server Error", false,
                             "Scan payload overflow");
  }

  int visible_count = 0;
  int seen_count = 0;
  for (uint16_t i = 0; i < ap_count; ++i) {
    if (ap_records[i].ssid[0] == '\0') {
      continue;
    }
    bool duplicate = false;
    for (int j = 0; j < seen_count; ++j) {
      if (strncmp((const char *)ap_records[i].ssid, seen_ssids[j],
                  sizeof(seen_ssids[j])) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }
    strlcpy(seen_ssids[seen_count], (const char *)ap_records[i].ssid,
            sizeof(seen_ssids[seen_count]));
    seen_count++;

    char escaped_ssid[160];
    char escaped_auth[40];
    json_escape((const char *)ap_records[i].ssid, escaped_ssid,
                sizeof(escaped_ssid));
    json_escape(wifi_auth_mode_to_text(ap_records[i].authmode), escaped_auth,
                sizeof(escaped_auth));

    int append = snprintf(
        payload + written, WIFI_SCAN_JSON_MAX_LEN - (size_t)written,
        "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":\"%s\",\"secure\":%s}",
        visible_count > 0 ? "," : "", escaped_ssid, ap_records[i].rssi,
        escaped_auth,
        ap_records[i].authmode == WIFI_AUTH_OPEN ? "false" : "true");
    if (append < 0 ||
        (size_t)append >= WIFI_SCAN_JSON_MAX_LEN - (size_t)written) {
      free(seen_ssids);
      free(payload);
      free(ap_records);
      return send_json_message(req, "500 Internal Server Error", false,
                               "Scan payload overflow");
    }
    written += append;
    visible_count++;
  }

  int tail =
      snprintf(payload + written, WIFI_SCAN_JSON_MAX_LEN - (size_t)written,
               "],\"count\":%d}", visible_count);
  if (tail < 0 || (size_t)tail >= WIFI_SCAN_JSON_MAX_LEN - (size_t)written) {
    free(seen_ssids);
    free(payload);
    free(ap_records);
    return send_json_message(req, "500 Internal Server Error", false,
                             "Scan payload overflow");
  }

  esp_err_t send_ret = send_json_response(req, "200 OK", payload);
  free(seen_ssids);
  free(payload);
  free(ap_records);
  return send_ret;
}

static esp_err_t config_wifi_post_handler(httpd_req_t *req) {
  char body[WIFI_PROVISIONING_BODY_MAX_LEN] = {0};
  char content_type[80] = {0};
  char ssid[sizeof(s_wifi_credentials.ssid)] = {0};
  char password[sizeof(s_wifi_credentials.password)] = {0};

  if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type,
                                  sizeof(content_type)) == ESP_OK) {
    if (strncmp(content_type, "application/x-www-form-urlencoded", 33) != 0) {
      return send_json_message(req, "415 Unsupported Media Type", false,
                               "Unsupported content type");
    }
  }

  if (!read_http_body(req, body, sizeof(body))) {
    return send_json_message(req, "400 Bad Request", false, "Invalid body");
  }

  bool has_ssid = form_get_value(body, "ssid", ssid, sizeof(ssid));
  bool hidden = form_value_is_truthy(body, "hidden");
  form_get_value(body, "password", password, sizeof(password));

  size_t ssid_len = strlen(ssid);
  size_t pass_len = strlen(password);
  if (!has_ssid || ssid_len == 0 || ssid_len > 32) {
    return send_json_message(req, "400 Bad Request", false, "SSID invalid");
  }
  if (pass_len > 64) {
    return send_json_message(req, "400 Bad Request", false,
                             "Password too long");
  }
  if (pass_len > 0 && pass_len < 8) {
    return send_json_message(req, "400 Bad Request", false,
                             "Password must be >=8 or empty");
  }
  if (has_control_chars(ssid) || has_control_chars(password)) {
    return send_json_message(req, "400 Bad Request", false,
                             "Invalid control character in input");
  }

  /* test connection before saving to prevent bricking */
  wifi_test_result_t test = wifi_test_credentials(ssid, password, hidden);
  if (test != WIFI_TEST_RESULT_CONNECTED) {
    const char *reason;
    switch (test) {
    case WIFI_TEST_RESULT_WRONG_PASSWORD:
      reason = "Connection failed. Check your password and try again.";
      break;
    case WIFI_TEST_RESULT_NOT_FOUND:
      reason = "Network not found. Check SSID and try again.";
      break;
    case WIFI_TEST_RESULT_TIMEOUT:
      reason = "Connection timed out. The network may be out of range.";
      break;
    default:
      reason = "Connection test failed. Please try again.";
      break;
    }
    ESP_LOGW(TAG, "Wi-Fi test failed for SSID '%s': %s", ssid, reason);
    return send_json_message(req, "422 Unprocessable Entity", false, reason);
  }

  bool did_write = false;
  esp_err_t ret =
      wifi_save_credentials_to_nvs(ssid, password, hidden, &did_write);
  if (ret == ESP_ERR_INVALID_STATE) {
    return send_json_message(req, "429 Too Many Requests", false,
                             "Please wait a bit before saving again");
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Saving Wi-Fi credentials failed: %s", esp_err_to_name(ret));
    return send_json_message(req, "500 Internal Server Error", false,
                             "Failed to save credentials");
  }

  if (!did_write) {
    if (s_provisioning_portal_active) {
      if (!stop_provisioning_portal()) {
        return send_json_message(req, "500 Internal Server Error", false,
                                 "Connected, but failed to close setup hotspot");
      }
      return send_json_message(req, "200 OK", true,
                               "Connected. Setup hotspot closed.");
    }
    return send_json_message(req, "200 OK", true,
                             "Credentials verified and unchanged.");
  }

  ESP_LOGI(TAG, "Wi-Fi credentials verified and saved (ssid=%s, hidden=%d). "
                "Restarting...",
           ssid, hidden);
  schedule_delayed_restart();
  return send_json_message(req, "200 OK", true,
                           "Connected! Credentials saved. Device restarting.");
}

static esp_err_t config_provisioning_start_post_handler(httpd_req_t *req) {
  s_provisioning_auto_opened = false;
  if (!wifi_credentials_configured()) {
    if (!start_provisioning_portal()) {
      return send_json_message(req, "500 Internal Server Error", false,
                               "Failed to start setup hotspot");
    }
    return send_json_message(req, "200 OK", true, "Setup hotspot is active.");
  }

  if (!start_provisioning_portal()) {
    return send_json_message(req, "500 Internal Server Error", false,
                             "Failed to start setup hotspot");
  }
  return send_json_message(req, "200 OK", true,
                           "Setup hotspot enabled for Wi-Fi change.");
}

static esp_err_t config_provisioning_stop_post_handler(httpd_req_t *req) {
  if (!s_provisioning_portal_active) {
    return send_json_message(req, "200 OK", true,
                             "Provisioning portal is not active.");
  }
  if (!stop_provisioning_portal()) {
    return send_json_message(req, "400 Bad Request", false,
                             "Cannot stop: no Wi-Fi credentials configured");
  }
  return send_json_message(req, "200 OK", true,
                           "Provisioning portal stopped.");
}

static esp_err_t config_wifi_disconnect_post_handler(httpd_req_t *req) {
  if (connectivity_service_is_wifi_connected()) {
    s_wifi_should_connect = false;
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK) {
      return send_json_message(req, "500 Internal Server Error", false,
                               "Failed to disconnect Wi-Fi");
    }
    ESP_LOGI(TAG, "Wi-Fi disconnected by user request");
  }

  if (!start_provisioning_portal()) {
    return send_json_message(req, "500 Internal Server Error", false,
                             "Wi-Fi disconnected, but setup hotspot failed");
  }
  return send_json_message(req, "200 OK", true,
                           "Wi-Fi disconnected. Setup hotspot is active.");
}

static esp_err_t config_wifi_forget_post_handler(httpd_req_t *req) {
  s_wifi_should_connect = false;
  esp_wifi_disconnect();

  esp_err_t ret = wifi_erase_credentials_nvs();
  if (ret != ESP_OK) {
    return send_json_message(req, "500 Internal Server Error", false,
                             "Failed to erase credentials");
  }

  if (!start_provisioning_portal()) {
    return send_json_message(req, "500 Internal Server Error", false,
                             "Failed to start setup hotspot");
  }
  ESP_LOGI(TAG, "Wi-Fi credentials forgotten, provisioning portal started");
  return send_json_message(req, "200 OK", true,
                           "Credentials erased. Setup hotspot is active.");
}

static bool start_config_http_server(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  esp_err_t ret;

  if (s_config_http_server != NULL) {
    return true;
  }

  config.max_uri_handlers = 28;
  config.stack_size = 6144;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;

  ret = httpd_start(&s_config_http_server, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
    s_config_http_server = NULL;
    return false;
  }

  httpd_uri_t uris[] = {
      {.uri = "/", .method = HTTP_GET, .handler = config_root_get_handler},
      {.uri = "/app.css*",
       .method = HTTP_GET,
       .handler = config_css_get_handler},
      {.uri = "/app.js*", .method = HTTP_GET, .handler = config_js_get_handler},
      {.uri = "/generate_204",
       .method = HTTP_GET,
       .handler = config_captive_redirect_get_handler},
      {.uri = "/gen_204",
       .method = HTTP_GET,
       .handler = config_captive_redirect_get_handler},
      {.uri = "/hotspot-detect.html",
       .method = HTTP_GET,
       .handler = config_captive_redirect_get_handler},
      {.uri = "/library/test/success.html",
       .method = HTTP_GET,
       .handler = config_captive_redirect_get_handler},
      {.uri = "/ncsi.txt",
       .method = HTTP_GET,
       .handler = config_captive_redirect_get_handler},
      {.uri = "/connecttest.txt",
       .method = HTTP_GET,
       .handler = config_captive_redirect_get_handler},
      {.uri = "/api/state",
       .method = HTTP_GET,
       .handler = config_state_get_handler},
      {.uri = "/api/telemetry",
       .method = HTTP_GET,
       .handler = config_telemetry_get_handler},
      {.uri = "/api/scan",
       .method = HTTP_GET,
       .handler = config_scan_get_handler},
      {.uri = "/api/wifi",
       .method = HTTP_POST,
       .handler = config_wifi_post_handler},
      {.uri = "/wifi", .method = HTTP_POST, .handler = config_wifi_post_handler},
      {.uri = "/api/provisioning/start",
       .method = HTTP_POST,
       .handler = config_provisioning_start_post_handler},
      {.uri = "/api/provisioning/stop",
       .method = HTTP_POST,
       .handler = config_provisioning_stop_post_handler},
      {.uri = "/api/wifi/disconnect",
       .method = HTTP_POST,
       .handler = config_wifi_disconnect_post_handler},
      {.uri = "/api/wifi/forget",
       .method = HTTP_POST,
       .handler = config_wifi_forget_post_handler},
      {.uri = "/api/memory",
       .method = HTTP_GET,
       .handler = config_memory_get_handler},
      {.uri = "/api/memory/photo",
       .method = HTTP_POST,
       .handler = config_memory_post_handler},
      {.uri = "/api/memory/photo",
       .method = HTTP_DELETE,
       .handler = config_memory_delete_handler},
  };

  for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); ++i) {
    ret = httpd_register_uri_handler(s_config_http_server, &uris[i]);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Register HTTP handlers failed: %s", esp_err_to_name(ret));
      httpd_stop(s_config_http_server);
      s_config_http_server = NULL;
      return false;
    }
  }

  ESP_LOGI(TAG, "Config web server started");
  return true;
}

static void stop_config_http_server(void) {
  if (s_config_http_server == NULL) {
    return;
  }

  httpd_stop(s_config_http_server);
  s_config_http_server = NULL;
  ESP_LOGI(TAG, "Config web server stopped");
}

static void recycle_http_server_task(void *arg) {
  uint32_t delay_ms = (uint32_t)(uintptr_t)arg;

  vTaskDelay(pdMS_TO_TICKS(delay_ms));
  stop_config_http_server();
  start_config_http_server();
  s_http_server_recycle_scheduled = false;
  vTaskDelete(NULL);
}

static void schedule_http_server_recycle(uint32_t delay_ms) {
  if (s_http_server_recycle_scheduled) {
    return;
  }

  s_http_server_recycle_scheduled = true;
  BaseType_t ok =
      xTaskCreate(recycle_http_server_task, "httpd_recycle", 3072,
                  (void *)(uintptr_t)delay_ms, 4, NULL);
  if (ok != pdPASS) {
    s_http_server_recycle_scheduled = false;
    ESP_LOGW(TAG, "Failed to schedule HTTP server recycle");
  }
}

static void ensure_provisioning_ap_ssid(void) {
  if (s_provisioning_ap_ssid[0] != '\0') {
    return;
  }

  uint8_t mac[6] = {0};
  esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (ret == ESP_OK) {
    snprintf(s_provisioning_ap_ssid, sizeof(s_provisioning_ap_ssid),
             "AQNODE-%02X%02X%02X", mac[3], mac[4], mac[5]);
  } else {
    strlcpy(s_provisioning_ap_ssid, "AQNODE-SETUP",
            sizeof(s_provisioning_ap_ssid));
  }
}

static bool start_provisioning_portal(void) {
  bool has_credentials = wifi_credentials_configured();

  if (!s_wifi_driver_ready) {
    return false;
  }
  if (s_provisioning_portal_active && s_config_http_server != NULL) {
    esp_err_t dns_ret = captive_dns_start();
    if (dns_ret != ESP_OK) {
      ESP_LOGW(TAG, "Captive DNS already-portal start failed: %s",
               esp_err_to_name(dns_ret));
    }
    schedule_http_server_recycle(250);
    return true;
  }

  ensure_provisioning_ap_ssid();

  wifi_config_t ap_config = {
      .ap =
          {
              .channel = 1,
              .max_connection = WIFI_PROVISIONING_MAX_CONN,
              .authmode = WIFI_AUTH_WPA_WPA2_PSK,
              .pmf_cfg = {.capable = true, .required = false},
          },
  };

  strlcpy((char *)ap_config.ap.ssid, s_provisioning_ap_ssid,
          sizeof(ap_config.ap.ssid));
  ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
  strlcpy((char *)ap_config.ap.password, WIFI_PROVISIONING_AP_PASS,
          sizeof(ap_config.ap.password));
  if (strlen(WIFI_PROVISIONING_AP_PASS) < 8) {
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.password[0] = '\0';
  }

  wifi_config_t sta_config = {0};
  if (has_credentials) {
    fill_sta_wifi_config(&sta_config);
  }

  /* stop any in-flight STA connection so set_config won't fail */
  s_wifi_should_connect = false;
  esp_wifi_disconnect();
  vTaskDelay(pdMS_TO_TICKS(100));

  wifi_clear_status_bits();

  esp_err_t ret =
      esp_wifi_set_mode(has_credentials ? WIFI_MODE_APSTA : WIFI_MODE_AP);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_mode provisioning failed: %s",
             esp_err_to_name(ret));
    return false;
  }

  ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(ret));
    return false;
  }

  if (has_credentials) {
    ret = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "esp_wifi_set_config(STA) in portal failed: %s, "
                    "continuing AP-only",
               esp_err_to_name(ret));
      /* fall back to AP-only so the user can still reconfigure */
      esp_wifi_set_mode(WIFI_MODE_AP);
      has_credentials = false;
    }
  }

  s_wifi_should_connect = has_credentials;

  ret = esp_wifi_start();
  if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
    ESP_LOGE(TAG, "esp_wifi_start(AP) failed: %s", esp_err_to_name(ret));
    return false;
  }
  ensure_mdns_service();
  if (has_credentials) {
    ret = esp_wifi_connect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
      ESP_LOGW(TAG, "esp_wifi_connect(APSTA) failed: %s", esp_err_to_name(ret));
    }
  }

  if (!start_config_http_server()) {
    return false;
  }
  schedule_http_server_recycle(250);

  esp_err_t dns_ret = captive_dns_start();
  if (dns_ret != ESP_OK) {
    ESP_LOGW(TAG, "Captive DNS start failed: %s", esp_err_to_name(dns_ret));
  }

  s_provisioning_portal_active = true;
  ESP_LOGW(TAG,
           "Provisioning portal active. Connect SSID '%s' (pass '%s') and open "
           "http://192.168.4.1",
           s_provisioning_ap_ssid, WIFI_PROVISIONING_AP_PASS);
  return true;
}

static bool stop_provisioning_portal(void) {
  if (!s_provisioning_portal_active) {
    return true;
  }
  if (!wifi_credentials_configured()) {
    ESP_LOGW(TAG, "Cannot stop provisioning: no credentials configured");
    return false;
  }

  s_provisioning_portal_active = false;
  s_provisioning_auto_opened = false;
  s_wifi_connect_fail_cycles = 0;
  s_wifi_target_missing_cycles = 0;

  captive_dns_stop();

  if (connectivity_service_is_wifi_connected()) {
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "Provisioning portal stopped, AP disabled");
  } else {
    if (s_clock_sync_task_handle != NULL) {
      xTaskNotifyGive(s_clock_sync_task_handle);
      ESP_LOGI(TAG,
               "Provisioning portal stopped, triggering Wi-Fi reconnect");
    } else {
      ESP_LOGI(TAG, "Provisioning portal stopped, restarting device");
      schedule_delayed_restart();
    }
  }
  return true;
}

static void ensure_runtime_config_server(void) {
  esp_err_t dns_ret = captive_dns_stop();
  if (dns_ret != ESP_OK) {
    ESP_LOGW(TAG, "Captive DNS stop failed: %s", esp_err_to_name(dns_ret));
  }

  stop_config_http_server();
  if (!start_config_http_server()) {
    ESP_LOGW(TAG, "Runtime config web server failed to start");
  }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  (void)arg;

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    wifi_clear_status_bits();
    ESP_LOGI(TAG, "Wi-Fi STA started, auto_connect=%d", s_wifi_should_connect);
    if (s_wifi_should_connect) {
      esp_err_t ret = esp_wifi_connect();
      if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "esp_wifi_connect on STA_START failed: %s",
                 esp_err_to_name(ret));
      }
    }
    return;
  }

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t *disconnected =
        (wifi_event_sta_disconnected_t *)event_data;
    if (s_wifi_event_group != NULL) {
      xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    ESP_LOGW(TAG, "Wi-Fi disconnected, auto_connect=%d, retry=%d/%d, reason=%d",
             s_wifi_should_connect, s_wifi_retry_num, WIFI_MAXIMUM_RETRY,
             disconnected != NULL ? disconnected->reason : -1);
    if (!s_wifi_should_connect) {
      ESP_LOGW(TAG, "Skip reconnect because auto-connect is disabled");
      return;
    }
    if (s_wifi_retry_num < WIFI_MAXIMUM_RETRY) {
      esp_err_t ret = esp_wifi_connect();
      if (ret == ESP_OK || ret == ESP_ERR_WIFI_CONN) {
        ESP_LOGI(TAG, "Reconnect attempt %d/%d started", s_wifi_retry_num + 1,
                 WIFI_MAXIMUM_RETRY);
      } else {
        ESP_LOGW(TAG, "esp_wifi_connect retry failed immediately: %s",
                 esp_err_to_name(ret));
      }
      s_wifi_retry_num++;
      return;
    }
    ESP_LOGW(TAG,
             "Wi-Fi reconnect attempts exhausted; wait for next sync cycle");
    schedule_clock_sync_retry(CLOCK_SYNC_RETRY_INTERVAL_MS);
    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    return;
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *got_ip = (ip_event_got_ip_t *)event_data;
    s_wifi_retry_num = 0;
    s_wifi_should_connect = true;
    if (s_wifi_event_group != NULL) {
      xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
      xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    if (got_ip != NULL) {
      ESP_LOGI(TAG, "Wi-Fi got IP: " IPSTR, IP2STR(&got_ip->ip_info.ip));
    }
  }
}

static bool sync_time_tphcm(void) {
  esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.google.com");
  bool sntp_started = false;

  vTaskDelay(pdMS_TO_TICKS(SNTP_SETTLE_DELAY_MS));

  esp_err_t ret = esp_netif_sntp_init(&sntp_cfg);
  if (ret == ESP_ERR_INVALID_STATE) {
    esp_netif_sntp_deinit();
    ret = esp_netif_sntp_init(&sntp_cfg);
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SNTP init failed: %s", esp_err_to_name(ret));
    return false;
  }
  sntp_started = true;

  ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(20000));
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "SNTP sync timeout/error: %s", esp_err_to_name(ret));
    esp_netif_sntp_deinit();
    return false;
  }

  time_t now = 0;
  struct tm tm_info = {0};
  time(&now);
  localtime_r(&now, &tm_info);
  ESP_LOGI(TAG, "Time synced (TPHCM): %04d-%02d-%02d %02d:%02d:%02d",
           tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
           tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
  if (sntp_started) {
    esp_netif_sntp_deinit();
  }
  return true;
}

static void apply_timezone_tphcm(void) {
  setenv("TZ", "UTC-7", 1);
  tzset();
}

static bool wifi_service_init_once(void) {
  if (s_wifi_event_group == NULL) {
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
      ESP_LOGE(TAG, "Cannot create Wi-Fi event group");
      return false;
    }
  }

  esp_err_t ret = init_nvs_flash_storage();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
    return false;
  }

  ret = esp_netif_init();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
    return false;
  }

  ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(ret));
    return false;
  }

  apply_timezone_tphcm();

  if (s_wifi_sta_netif == NULL) {
    s_wifi_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_sta_netif == NULL) {
      ESP_LOGE(TAG, "Cannot create default Wi-Fi STA netif");
      return false;
    }
  }

  if (s_wifi_ap_netif == NULL) {
    s_wifi_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_wifi_ap_netif == NULL) {
      ESP_LOGE(TAG, "Cannot create default Wi-Fi AP netif");
      return false;
    }
  }

  if (!s_wifi_driver_ready) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
      return false;
    }

    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Register WIFI_EVENT handler failed: %s",
               esp_err_to_name(ret));
      return false;
    }

    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Register IP_EVENT handler failed: %s",
               esp_err_to_name(ret));
      return false;
    }

    s_wifi_driver_ready = true;
  }

  ensure_mdns_service();
  wifi_refresh_credentials_cache();
  memory_photo_service_restore_from_nvs();
  return true;
}

static void fill_sta_wifi_config(wifi_config_t *wifi_config) {
  if (wifi_config == NULL) {
    return;
  }

  memset(wifi_config, 0, sizeof(*wifi_config));
  wifi_config->sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_config->sta.pmf_cfg.capable = true;
  wifi_config->sta.pmf_cfg.required = false;

  {
    wifi_credentials_t creds = wifi_credentials_snapshot();
    strlcpy((char *)wifi_config->sta.ssid, creds.ssid,
            sizeof(wifi_config->sta.ssid));
    strlcpy((char *)wifi_config->sta.password, creds.password,
            sizeof(wifi_config->sta.password));
    if (creds.password[0] == '\0') {
      wifi_config->sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
  }
}

static wifi_target_scan_result_t wifi_scan_for_target_ssid(void) {
  wifi_scan_config_t scan_config = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = true,
      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
  };
  wifi_ap_record_t *ap_records = NULL;
  uint16_t ap_count = 0;
  esp_err_t ret;
  wifi_target_scan_result_t result = WIFI_TARGET_SCAN_NOT_FOUND;

  if (!wifi_credentials_configured()) {
    return WIFI_TARGET_SCAN_ERROR;
  }
  wifi_credentials_t scan_creds = wifi_credentials_snapshot();
  if (scan_creds.hidden) {
    ESP_LOGI(TAG, "Target SSID '%s' is marked hidden; skip visibility scan",
             scan_creds.ssid);
    return WIFI_TARGET_SCAN_ERROR;
  }

  ret = esp_wifi_scan_start(&scan_config, true);
  if (ret == ESP_ERR_WIFI_STATE) {
    esp_wifi_scan_stop();
    ret = esp_wifi_scan_start(&scan_config, true);
  }
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Target SSID scan start failed: %s", esp_err_to_name(ret));
    return WIFI_TARGET_SCAN_ERROR;
  }

  ret = esp_wifi_scan_get_ap_num(&ap_count);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Target SSID scan count failed: %s", esp_err_to_name(ret));
    return WIFI_TARGET_SCAN_ERROR;
  }

  if (ap_count == 0) {
    ESP_LOGW(TAG, "No AP found while scanning for target SSID '%s'",
             scan_creds.ssid);
    return WIFI_TARGET_SCAN_NOT_FOUND;
  }

  if (ap_count > WIFI_SCAN_MAX_RESULTS) {
    ap_count = WIFI_SCAN_MAX_RESULTS;
  }

  ap_records = calloc(ap_count, sizeof(wifi_ap_record_t));
  if (ap_records == NULL) {
    ESP_LOGW(TAG, "Target SSID scan allocation failed");
    return WIFI_TARGET_SCAN_ERROR;
  }

  ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Target SSID scan fetch failed: %s", esp_err_to_name(ret));
    result = WIFI_TARGET_SCAN_ERROR;
    goto cleanup;
  }

  for (uint16_t i = 0; i < ap_count; ++i) {
    if (strncmp((const char *)ap_records[i].ssid, scan_creds.ssid,
                sizeof(scan_creds.ssid)) == 0) {
      ESP_LOGI(TAG, "Target SSID '%s' is visible (RSSI=%d)",
               scan_creds.ssid, ap_records[i].rssi);
      result = WIFI_TARGET_SCAN_FOUND;
      goto cleanup;
    }
  }

  ESP_LOGW(TAG, "Target SSID '%s' not visible in current scan",
           scan_creds.ssid);
  result = WIFI_TARGET_SCAN_NOT_FOUND;

cleanup:
  free(ap_records);
  return result;
}

static wifi_connect_result_t wifi_start_and_wait_for_connection(void) {
  if (!wifi_credentials_configured()) {
    return WIFI_CONNECT_RESULT_FAILED;
  }

  if (s_wifi_event_group != NULL) {
    EventBits_t current_bits = xEventGroupGetBits(s_wifi_event_group);
    if ((current_bits & WIFI_CONNECTED_BIT) != 0) {
      s_wifi_should_connect = true;
      s_provisioning_portal_active = false;
      ensure_runtime_config_server();
      ESP_LOGI(TAG, "Wi-Fi already connected, reuse existing link");
      return WIFI_CONNECT_RESULT_CONNECTED;
    }
  }

  wifi_config_t wifi_config = {0};
  fill_sta_wifi_config(&wifi_config);

  s_wifi_retry_num = 0;
  s_wifi_should_connect = false;
  wifi_clear_status_bits();
  ESP_LOGI(TAG, "Start Wi-Fi connection flow");

  esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
    s_wifi_should_connect = false;
    return WIFI_CONNECT_RESULT_FAILED;
  }

  ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
    s_wifi_should_connect = false;
    return WIFI_CONNECT_RESULT_FAILED;
  }

  ret = esp_wifi_start();
  if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
    ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
    s_wifi_should_connect = false;
    return WIFI_CONNECT_RESULT_FAILED;
  }
  ensure_mdns_service();
  if (ret == ESP_ERR_WIFI_CONN) {
    ESP_LOGI(TAG, "esp_wifi_start skipped because Wi-Fi is already running");
  }

  wifi_target_scan_result_t scan_result = wifi_scan_for_target_ssid();
  if (scan_result == WIFI_TARGET_SCAN_NOT_FOUND) {
    s_wifi_should_connect = false;
    if (s_wifi_event_group != NULL) {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    return WIFI_CONNECT_RESULT_TARGET_NOT_FOUND;
  }
  if (scan_result == WIFI_TARGET_SCAN_ERROR) {
    ESP_LOGW(TAG,
             "Target SSID scan was inconclusive; continue with STA connect");
  }

  s_wifi_should_connect = true;
  ret = esp_wifi_connect();
  if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
    ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
    s_wifi_should_connect = false;
    return WIFI_CONNECT_RESULT_FAILED;
  }
  if (ret == ESP_ERR_WIFI_CONN) {
    ESP_LOGI(TAG, "esp_wifi_connect called while STA is already connecting");
  }

  EventBits_t bits = xEventGroupWaitBits(
      s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
      pdMS_TO_TICKS(25000));

  if ((bits & WIFI_CONNECTED_BIT) != 0) {
    s_provisioning_portal_active = false;
    ensure_runtime_config_server();
    ESP_LOGI(TAG, "Wi-Fi connected");
    return WIFI_CONNECT_RESULT_CONNECTED;
  }

  if ((bits & WIFI_FAIL_BIT) != 0) {
    ESP_LOGW(TAG, "Wi-Fi connect failed after retries");
  } else {
    ESP_LOGW(TAG, "Wi-Fi connect timeout");
  }

  /* stop STA so it doesn't block future set_config calls */
  s_wifi_should_connect = false;
  esp_wifi_disconnect();
  return WIFI_CONNECT_RESULT_FAILED;
}

static void schedule_clock_sync_retry(uint32_t delay_ms) {
  if (s_clock_sync_timer == NULL) {
    return;
  }

  ESP_LOGI(TAG, "Schedule next Wi-Fi/SNTP retry in %lu ms",
           (unsigned long)delay_ms);
  if (xTimerChangePeriod(s_clock_sync_timer, pdMS_TO_TICKS(delay_ms), 0) !=
      pdPASS) {
    ESP_LOGW(TAG, "Failed to schedule next clock sync in %lu ms",
             (unsigned long)delay_ms);
  }
}

static void clock_sync_timer_callback(TimerHandle_t timer) {
  (void)timer;
  if (s_clock_sync_task_handle != NULL) {
    xTaskNotifyGive(s_clock_sync_task_handle);
  }
}

static bool run_clock_sync_cycle(void) {
  wifi_connect_result_t connect_result = WIFI_CONNECT_RESULT_FAILED;
  bool synced = false;

  if (!wifi_service_init_once()) {
    return false;
  }

  if (!wifi_credentials_configured()) {
    s_wifi_connect_fail_cycles = 0;
    s_wifi_target_missing_cycles = 0;
    start_provisioning_portal();
    return false;
  }

  if (s_provisioning_portal_active) {
    if (!s_provisioning_auto_opened) {
      return false;
    }

    /* Auto-opened portal: probe STA reconnect while keeping AP alive */
    ESP_LOGI(TAG, "Auto-portal active: probing STA reconnect...");
    s_wifi_retry_num = 0;
    s_wifi_should_connect = true;
    wifi_clear_status_bits();

    esp_err_t probe_ret = esp_wifi_connect();
    if (probe_ret != ESP_OK && probe_ret != ESP_ERR_WIFI_CONN) {
      ESP_LOGW(TAG, "STA probe connect failed: %s",
               esp_err_to_name(probe_ret));
      s_wifi_should_connect = false;
      return false;
    }

    EventBits_t probe_bits = xEventGroupWaitBits(
        s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE,
        pdFALSE, pdMS_TO_TICKS(WIFI_AUTO_RECONNECT_PROBE_TIMEOUT_MS));

    if (!(probe_bits & WIFI_CONNECTED_BIT)) {
      ESP_LOGI(TAG, "STA probe failed, portal stays active");
      s_wifi_should_connect = false;
      return false;
    }

    /* STA reconnected — close auto-portal */
    ESP_LOGI(TAG, "STA reconnected while portal active, auto-closing portal");
    s_provisioning_portal_active = false;
    s_provisioning_auto_opened = false;
    captive_dns_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    ensure_runtime_config_server();
    /* fall through to time sync */
  }

  connect_result = wifi_start_and_wait_for_connection();
  if (connect_result != WIFI_CONNECT_RESULT_CONNECTED) {
    if (connect_result == WIFI_CONNECT_RESULT_TARGET_NOT_FOUND) {
      s_wifi_target_missing_cycles++;
      s_wifi_connect_fail_cycles = 0;
      ESP_LOGW(TAG, "Configured SSID missing in scan (%d/%d)",
               s_wifi_target_missing_cycles,
               WIFI_TARGET_MISSING_PORTAL_THRESHOLD);
      if (s_wifi_target_missing_cycles >=
          WIFI_TARGET_MISSING_PORTAL_THRESHOLD) {
        ESP_LOGW(TAG, "Configured SSID stayed missing across scans; switch to "
                      "setup hotspot");
        if (!s_provisioning_portal_active && start_provisioning_portal()) {
          s_provisioning_auto_opened = true;
          s_wifi_target_missing_cycles = 0;
        } else if (!s_provisioning_portal_active) {
          ESP_LOGE(TAG,
                   "Failed to start setup hotspot after repeated scan miss");
        }
      }
      return false;
    }

    s_wifi_target_missing_cycles = 0;
    if (!s_provisioning_portal_active) {
      s_wifi_connect_fail_cycles++;
      ESP_LOGW(TAG, "Wi-Fi connect cycle failed (%d/%d)",
               s_wifi_connect_fail_cycles, WIFI_CONNECT_FAIL_PORTAL_THRESHOLD);
      if (s_wifi_connect_fail_cycles >= WIFI_CONNECT_FAIL_PORTAL_THRESHOLD) {
        ESP_LOGW(
            TAG,
            "Auto-enable setup hotspot after repeated connection failures");
        if (start_provisioning_portal()) {
          s_provisioning_auto_opened = true;
          s_wifi_connect_fail_cycles = 0;
        } else {
          ESP_LOGE(TAG, "Failed to auto-start setup hotspot");
        }
      }
    }
    return false;
  }
  s_wifi_connect_fail_cycles = 0;
  s_wifi_target_missing_cycles = 0;

  synced = sync_time_tphcm();
  portENTER_CRITICAL(&s_connectivity_state_lock);
  s_time_synced = s_time_synced || synced || is_system_time_valid();
  portEXIT_CRITICAL(&s_connectivity_state_lock);
  return synced;
}

static void clock_sync_task(void *arg) {
  (void)arg;

  while (true) {
    bool cycle_has_valid_time;
    bool portal_active;
    bool runtime_connected;
    uint32_t next_retry_ms;

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (s_wifi_event_group != NULL) {
      xEventGroupClearBits(s_wifi_event_group,
                           CLOCK_SYNC_DONE_BIT | CLOCK_SYNC_OK_BIT);
    }

    bool synced = run_clock_sync_cycle();
    if (s_wifi_event_group != NULL) {
      EventBits_t bits = CLOCK_SYNC_DONE_BIT;
      if (synced || is_system_time_valid()) {
        portENTER_CRITICAL(&s_connectivity_state_lock);
        s_time_synced = true;
        portEXIT_CRITICAL(&s_connectivity_state_lock);
        bits |= CLOCK_SYNC_OK_BIT;
      }
      xEventGroupSetBits(s_wifi_event_group, bits);
    }

    cycle_has_valid_time = synced || is_system_time_valid();
    portal_active = s_provisioning_portal_active;
    runtime_connected =
        connectivity_service_is_wifi_connected() && !portal_active;
    next_retry_ms =
        (runtime_connected && cycle_has_valid_time)
            ? CLOCK_SYNC_SUCCESS_INTERVAL_MS
            : CLOCK_SYNC_RETRY_INTERVAL_MS;
    schedule_clock_sync_retry(next_retry_ms);
  }
}

void connectivity_service_setup_and_clock(void) {
  if (!wifi_service_init_once()) {
    return;
  }

  /* Always create clock_sync_task + timer so reconnect/probe works
     even when credentials are added later via the provisioning portal. */
  if (s_clock_sync_task_handle == NULL) {
    BaseType_t task_ok = xTaskCreate(clock_sync_task, "clock_sync", 4096, NULL,
                                     5, &s_clock_sync_task_handle);
    if (task_ok != pdPASS) {
      ESP_LOGE(TAG, "Failed to create clock sync task");
      s_clock_sync_task_handle = NULL;
    }
  }

  if (s_clock_sync_timer == NULL) {
    s_clock_sync_timer = xTimerCreate("clock_sync_timer", pdMS_TO_TICKS(1000),
                                      pdFALSE, NULL, clock_sync_timer_callback);
    if (s_clock_sync_timer == NULL) {
      ESP_LOGE(TAG, "Failed to create clock sync timer");
    }
  }

  if (!wifi_credentials_configured()) {
    if (!start_provisioning_portal()) {
      ESP_LOGE(TAG, "Failed to start provisioning portal");
    }
    return;
  }

  xEventGroupClearBits(s_wifi_event_group,
                       CLOCK_SYNC_DONE_BIT | CLOCK_SYNC_OK_BIT);
  xTaskNotifyGive(s_clock_sync_task_handle);

  EventBits_t bits = xEventGroupWaitBits(
      s_wifi_event_group, CLOCK_SYNC_DONE_BIT | CLOCK_SYNC_OK_BIT, pdFALSE,
      pdFALSE, pdMS_TO_TICKS(CLOCK_SYNC_INITIAL_WAIT_MS));
  if ((bits & CLOCK_SYNC_OK_BIT) != 0 || is_system_time_valid()) {
    portENTER_CRITICAL(&s_connectivity_state_lock);
    s_time_synced = true;
    portEXIT_CRITICAL(&s_connectivity_state_lock);
  }
}

bool connectivity_service_is_time_synced(void) {
  bool synced;
  portENTER_CRITICAL(&s_connectivity_state_lock);
  synced = s_time_synced;
  portEXIT_CRITICAL(&s_connectivity_state_lock);
  return synced;
}

void connectivity_service_set_time_synced(bool time_synced) {
  portENTER_CRITICAL(&s_connectivity_state_lock);
  s_time_synced = time_synced;
  portEXIT_CRITICAL(&s_connectivity_state_lock);
}

bool connectivity_service_start_provisioning(void) {
  s_provisioning_auto_opened = false;
  return start_provisioning_portal();
}

bool connectivity_service_disconnect_wifi(void) {
  if (connectivity_service_is_wifi_connected()) {
    s_wifi_should_connect = false;
    if (esp_wifi_disconnect() != ESP_OK) {
      return false;
    }
  }
  return start_provisioning_portal();
}

bool connectivity_service_stop_provisioning(void) {
  return stop_provisioning_portal();
}

bool connectivity_service_forget_credentials(void) {
  s_wifi_should_connect = false;
  esp_wifi_disconnect();
  if (wifi_erase_credentials_nvs() != ESP_OK) {
    return false;
  }
  return start_provisioning_portal();
}

void connectivity_service_get_ui_status(connectivity_ui_status_t *out) {
  if (out == NULL) {
    return;
  }

  memset(out, 0, sizeof(*out));
  wifi_refresh_credentials_cache();
  out->connected = connectivity_service_is_wifi_connected();

  portENTER_CRITICAL(&s_connectivity_state_lock);
  out->provisioning_portal_active = s_provisioning_portal_active;
  out->credentials_valid = s_wifi_credentials.valid;
  if (out->provisioning_portal_active && s_provisioning_ap_ssid[0] != '\0') {
    strlcpy(out->ssid, s_provisioning_ap_ssid, sizeof(out->ssid));
  } else if (!out->connected) {
    strlcpy(out->ssid, "DISCONNECTED", sizeof(out->ssid));
  } else {
    strlcpy(out->ssid, s_wifi_credentials.valid ? s_wifi_credentials.ssid : "NONE",
            sizeof(out->ssid));
  }
  portEXIT_CRITICAL(&s_connectivity_state_lock);

  if (out->provisioning_portal_active) {
    get_netif_ip_string(s_wifi_ap_netif, out->runtime_ip, sizeof(out->runtime_ip));
  } else {
    get_netif_ip_string(s_wifi_sta_netif, out->runtime_ip,
                        sizeof(out->runtime_ip));
  }

  if (out->runtime_ip[0] == '\0') {
    strlcpy(out->runtime_ip, "NO IP", sizeof(out->runtime_ip));
  }
}
