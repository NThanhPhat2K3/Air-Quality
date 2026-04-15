#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "captive_dns.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mdns.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "nvs.h"
#include "nvs_flash.h"

// ESP32 <-> ST7735 default wiring
// TFT VCC  -> 3V3
// TFT GND  -> GND
// TFT NC   -> no connect
// TFT CLK  -> GPIO18
// TFT SDA  -> GPIO23
// TFT RS   -> GPIO2   (same as DC / A0)
// TFT RST  -> GPIO4
// TFT CS   -> GPIO5
// This module does not expose BLK/LED, so backlight is always on.

#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK 18
#define PIN_NUM_CS 5
#define PIN_NUM_DC 2
#define PIN_NUM_RST 4
#define PIN_NUM_BL -1

#define TFT_WIDTH 160
#define TFT_HEIGHT 128
// If the image is shifted, try X/Y offset = 1 or 2 depending on your module.
#define TFT_X_OFFSET 0
#define TFT_Y_OFFSET 0
// If colors look swapped or mirrored, try 0xA8 or 0x60.
#define TFT_MADCTL 0xA0

#define DEG_TO_RAD 0.01745329252f
#define RGB565(r, g, b)                                                        \
  (uint16_t)((((r)&0xF8) << 8) | (((g)&0xFC) << 3) | ((b) >> 3))
// 1 = use hybrid runtime UI (black background + data cards).
#define SHOW_BITMAP_UI 1
// 1 = use stubbed sensor values for bring-up/smoke testing before real hardware.
#define APP_SENSOR_SMOKE_TEST 1
// Only used when APP_SENSOR_SMOKE_TEST=1.
// 1 = cycle stub AQI values through all thresholds so the UI can be reviewed.
#define DEMO_AQI_SWEEP 1
#define DEMO_AQI_STEP_SECONDS 6

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
#define WIFI_PROVISIONING_AP_PASS "setup123"
#define WIFI_PROVISIONING_MAX_CONN 4
#define WIFI_PROVISIONING_BODY_MAX_LEN 256
#define WIFI_PROVISIONING_RESTART_DELAY_MS 1500
#define WIFI_FLASH_WRITE_COOLDOWN_MS (30 * 1000)
#define WIFI_SCAN_MAX_RESULTS 24
#define WIFI_SCAN_JSON_MAX_LEN 4096
#define WIFI_STATE_JSON_MAX_LEN 1024
#define WIFI_CONNECT_FAIL_PORTAL_THRESHOLD 3
#define RUNTIME_MDNS_HOSTNAME "aqnode"
#define RUNTIME_MDNS_INSTANCE "AQ Node"

typedef struct {
  char c;
  uint8_t rows[5];
} glyph3x5_t;

typedef struct {
  char c;
  uint8_t rows[7];
} glyph5x7_t;

typedef struct {
  int8_t x;
  int8_t y;
  uint16_t color;
} star_t;

typedef struct {
  struct tm clock;
  int aqi;
  int eco2_ppm;
  int tvoc_ppb;
  int ens_validity;
  int temp_tenths_c;
  int humidity_pct;
} dashboard_state_t;

typedef struct {
  char ssid[33];
  char password[65];
  bool valid;
  bool from_nvs;
} wifi_credentials_t;

typedef struct {
  uint8_t cmd;
  uint8_t data[16];
  uint8_t data_len;
  uint16_t delay_ms;
} lcd_init_cmd_t;

static const char *TAG = "st7735_dashboard";

static spi_device_handle_t s_lcd_spi;
static uint16_t s_framebuffer[TFT_WIDTH * TFT_HEIGHT];
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_wifi_sta_netif;
static esp_netif_t *s_wifi_ap_netif;
static TaskHandle_t s_clock_sync_task_handle;
static TimerHandle_t s_clock_sync_timer;
static httpd_handle_t s_config_http_server;
static int s_wifi_retry_num;
static bool s_time_synced;
static bool s_wifi_should_connect;
static bool s_wifi_driver_ready;
static bool s_wifi_credentials_loaded;
static bool s_provisioning_portal_active;
static bool s_restart_scheduled;
static bool s_mdns_ready;
static int64_t s_last_credentials_write_us;
static int s_wifi_connect_fail_cycles;
static wifi_credentials_t s_wifi_credentials;
static char s_provisioning_ap_ssid[33];
static dashboard_state_t s_latest_dashboard_state;
static bool s_latest_dashboard_state_valid;
static portMUX_TYPE s_dashboard_state_lock = portMUX_INITIALIZER_UNLOCKED;

extern const uint8_t web_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t web_index_html_end[] asm("_binary_index_html_end");
extern const uint8_t web_app_css_start[] asm("_binary_app_css_start");
extern const uint8_t web_app_css_end[] asm("_binary_app_css_end");
extern const uint8_t web_app_js_start[] asm("_binary_app_js_start");
extern const uint8_t web_app_js_end[] asm("_binary_app_js_end");

static void schedule_clock_sync_retry(uint32_t delay_ms);
static void wifi_refresh_credentials_cache(void);
static bool start_provisioning_portal(void);
static void fill_sta_wifi_config(wifi_config_t *wifi_config);
static void ensure_mdns_service(void);
static void snapshot_dashboard_state(const dashboard_state_t *state);
static bool read_dashboard_state_snapshot(dashboard_state_t *out);
static void build_runtime_dashboard_state(dashboard_state_t *state);
static void fill_smoke_test_sensor_state(dashboard_state_t *state);

static const glyph3x5_t kFont3x5[] = {
    {' ', {0x0, 0x0, 0x0, 0x0, 0x0}}, {'%', {0x5, 0x1, 0x2, 0x4, 0x5}},
    {'-', {0x0, 0x0, 0x7, 0x0, 0x0}}, {'.', {0x0, 0x0, 0x0, 0x0, 0x2}},
    {':', {0x0, 0x2, 0x0, 0x2, 0x0}}, {'0', {0x7, 0x5, 0x5, 0x5, 0x7}},
    {'1', {0x2, 0x6, 0x2, 0x2, 0x7}}, {'2', {0x7, 0x1, 0x7, 0x4, 0x7}},
    {'3', {0x7, 0x1, 0x7, 0x1, 0x7}}, {'4', {0x5, 0x5, 0x7, 0x1, 0x1}},
    {'5', {0x7, 0x4, 0x7, 0x1, 0x7}}, {'6', {0x7, 0x4, 0x7, 0x5, 0x7}},
    {'7', {0x7, 0x1, 0x1, 0x1, 0x1}}, {'8', {0x7, 0x5, 0x7, 0x5, 0x7}},
    {'9', {0x7, 0x5, 0x7, 0x1, 0x7}}, {'A', {0x7, 0x5, 0x7, 0x5, 0x5}},
    {'B', {0x6, 0x5, 0x6, 0x5, 0x6}}, {'C', {0x3, 0x4, 0x4, 0x4, 0x3}},
    {'D', {0x6, 0x5, 0x5, 0x5, 0x6}}, {'E', {0x7, 0x4, 0x6, 0x4, 0x7}},
    {'F', {0x7, 0x4, 0x6, 0x4, 0x4}}, {'G', {0x3, 0x4, 0x5, 0x5, 0x3}},
    {'H', {0x5, 0x5, 0x7, 0x5, 0x5}}, {'I', {0x7, 0x2, 0x2, 0x2, 0x7}},
    {'K', {0x5, 0x5, 0x6, 0x5, 0x5}}, {'L', {0x4, 0x4, 0x4, 0x4, 0x7}},
    {'M', {0x5, 0x7, 0x7, 0x5, 0x5}}, {'N', {0x5, 0x7, 0x7, 0x7, 0x5}},
    {'O', {0x7, 0x5, 0x5, 0x5, 0x7}}, {'P', {0x6, 0x5, 0x6, 0x4, 0x4}},
    {'Q', {0x7, 0x5, 0x5, 0x7, 0x1}}, {'R', {0x6, 0x5, 0x6, 0x5, 0x5}},
    {'S', {0x7, 0x4, 0x7, 0x1, 0x7}}, {'T', {0x7, 0x2, 0x2, 0x2, 0x2}},
    {'U', {0x5, 0x5, 0x5, 0x5, 0x7}}, {'V', {0x5, 0x5, 0x5, 0x5, 0x2}},
    {'W', {0x5, 0x5, 0x7, 0x7, 0x5}}, {'X', {0x5, 0x5, 0x2, 0x5, 0x5}},
    {'Y', {0x5, 0x5, 0x2, 0x2, 0x2}},
};

static const glyph5x7_t kFont5x7[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'%', {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'/', {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
};

static const star_t kStars[] = {
    {8, 18, RGB565(30, 50, 90)},      {17, 12, RGB565(80, 180, 255)},
    {22, 73, RGB565(255, 255, 255)},  {29, 24, RGB565(60, 90, 150)},
    {36, 44, RGB565(255, 220, 120)},  {42, 8, RGB565(180, 210, 255)},
    {48, 96, RGB565(90, 140, 220)},   {55, 57, RGB565(60, 170, 255)},
    {61, 16, RGB565(255, 255, 255)},  {68, 84, RGB565(50, 90, 160)},
    {74, 31, RGB565(255, 205, 80)},   {82, 14, RGB565(40, 110, 255)},
    {89, 47, RGB565(140, 180, 255)},  {96, 73, RGB565(255, 255, 255)},
    {104, 12, RGB565(40, 80, 140)},   {110, 54, RGB565(255, 220, 120)},
    {117, 21, RGB565(80, 160, 255)},  {122, 89, RGB565(40, 70, 130)},
    {129, 61, RGB565(255, 255, 255)}, {136, 10, RGB565(70, 150, 255)},
    {142, 32, RGB565(255, 210, 90)},  {148, 73, RGB565(90, 140, 210)},
    {152, 49, RGB565(255, 255, 255)}, {11, 101, RGB565(70, 140, 220)},
    {27, 112, RGB565(40, 90, 170)},   {73, 103, RGB565(255, 220, 120)},
    {99, 102, RGB565(60, 130, 230)},  {145, 101, RGB565(70, 120, 180)},
};

static const lcd_init_cmd_t kLcdInit[] = {
    {0x01, {0}, 0, 150},
    {0x11, {0}, 0, 120},
    {0xB1, {0x01, 0x2C, 0x2D}, 3, 0},
    {0xB2, {0x01, 0x2C, 0x2D}, 3, 0},
    {0xB3, {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6, 0},
    {0xB4, {0x07}, 1, 0},
    {0xC0, {0xA2, 0x02, 0x84}, 3, 0},
    {0xC1, {0xC5}, 1, 0},
    {0xC2, {0x0A, 0x00}, 2, 0},
    {0xC3, {0x8A, 0x2A}, 2, 0},
    {0xC4, {0x8A, 0xEE}, 2, 0},
    {0xC5, {0x0E}, 1, 0},
    {0x20, {0}, 0, 0},
    {0x36, {TFT_MADCTL}, 1, 0},
    {0x3A, {0x05}, 1, 0},
    {0xE0,
     {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D, 0x29, 0x25, 0x2B, 0x39,
      0x00, 0x01, 0x03, 0x10},
     16,
     0},
    {0xE1,
     {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, 0x2E, 0x2E, 0x37, 0x3F,
      0x00, 0x00, 0x02, 0x10},
     16,
     0},
    {0x13, {0}, 0, 10},
    {0x29, {0}, 0, 120},
};

static const uint16_t COLOR_BG = RGB565(0, 0, 0);
static const uint16_t COLOR_WHITE = RGB565(245, 248, 255);
static const uint16_t COLOR_YELLOW = RGB565(255, 220, 60);
static const uint16_t COLOR_CYAN = RGB565(70, 210, 255);

static const uint16_t COLOR_PANEL = RGB565(7, 13, 22);
static const uint16_t COLOR_PANEL_ALT = RGB565(10, 18, 30);
static const uint16_t COLOR_MUTED = RGB565(160, 180, 200);
static const uint16_t COLOR_DIVIDER = RGB565(95, 110, 130);
static const uint16_t COLOR_LIME = RGB565(160, 255, 50);
static const uint16_t COLOR_GREEN = RGB565(60, 220, 90);
static const uint16_t COLOR_ORANGE = RGB565(255, 145, 45);
static const uint16_t COLOR_RED = RGB565(255, 78, 70);

static inline uint16_t swap16(uint16_t value) {
  return (uint16_t)((value << 8) | (value >> 8));
}

static inline int clamp_int(int value, int min_value, int max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static void setup_output_pin(int pin, int level) {
  if (pin < 0) {
    return;
  }
  gpio_reset_pin((gpio_num_t)pin);
  gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)pin, level);
}

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

static void wifi_set_credentials_cache(const char *ssid, const char *password,
                                       bool from_nvs) {
  memset(&s_wifi_credentials, 0, sizeof(s_wifi_credentials));
  if (ssid != NULL) {
    strlcpy(s_wifi_credentials.ssid, ssid, sizeof(s_wifi_credentials.ssid));
  }
  if (password != NULL) {
    strlcpy(s_wifi_credentials.password, password,
            sizeof(s_wifi_credentials.password));
  }
  s_wifi_credentials.valid = s_wifi_credentials.ssid[0] != '\0';
  s_wifi_credentials.from_nvs = from_nvs;
}

static bool wifi_load_credentials_from_nvs(void) {
  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (ret != ESP_OK) {
    return false;
  }

  char ssid[sizeof(s_wifi_credentials.ssid)] = {0};
  char password[sizeof(s_wifi_credentials.password)] = {0};
  size_t ssid_len = sizeof(ssid);
  size_t password_len = sizeof(password);
  bool loaded = false;

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

  wifi_set_credentials_cache(ssid, password, true);
  loaded = true;
  nvs_close(nvs_handle);
  return loaded;
}

static bool wifi_credentials_equal(const char *ssid, const char *password) {
  const char *safe_password = password != NULL ? password : "";
  if (ssid == NULL || ssid[0] == '\0') {
    return false;
  }
  wifi_refresh_credentials_cache();
  if (!s_wifi_credentials.valid) {
    return false;
  }
  return strcmp(s_wifi_credentials.ssid, ssid) == 0 &&
         strcmp(s_wifi_credentials.password, safe_password) == 0;
}

static esp_err_t wifi_save_credentials_to_nvs(const char *ssid,
                                              const char *password,
                                              bool *did_write) {
  const char *safe_password = password != NULL ? password : "";
  int64_t now_us = esp_timer_get_time();

  if (did_write != NULL) {
    *did_write = false;
  }
  if (ssid == NULL || ssid[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }
  if (wifi_credentials_equal(ssid, safe_password)) {
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
  esp_err_t ret = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = nvs_set_str(nvs_handle, WIFI_CREDENTIALS_KEY_SSID, ssid);
  if (ret == ESP_OK) {
    ret = nvs_set_str(nvs_handle, WIFI_CREDENTIALS_KEY_PASS,
                      password != NULL ? password : "");
  }
  if (ret == ESP_OK) {
    ret = nvs_commit(nvs_handle);
  }
  nvs_close(nvs_handle);

  if (ret == ESP_OK) {
    s_last_credentials_write_us = now_us;
    wifi_set_credentials_cache(ssid, safe_password, true);
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
             s_wifi_credentials.ssid);
    s_wifi_credentials_loaded = true;
    return;
  }

  if (!is_placeholder_credentials(WIFI_SSID, WIFI_PASS)) {
    wifi_set_credentials_cache(WIFI_SSID, WIFI_PASS, false);
    ESP_LOGI(TAG, "Wi-Fi credentials loaded from compile-time config (ssid=%s)",
             s_wifi_credentials.ssid);
  } else {
    wifi_set_credentials_cache("", "", false);
    ESP_LOGW(TAG, "No Wi-Fi credentials configured; start provisioning portal");
  }
  s_wifi_credentials_loaded = true;
}

static bool wifi_credentials_configured(void) {
  wifi_refresh_credentials_cache();
  return s_wifi_credentials.valid;
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

  ret = mdns_hostname_set(RUNTIME_MDNS_HOSTNAME);
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
  ESP_LOGI(TAG, "mDNS ready: http://%s.local", RUNTIME_MDNS_HOSTNAME);
}

static bool wifi_link_is_up(void) {
  if (s_wifi_event_group == NULL) {
    return false;
  }
  EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
  return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void snapshot_dashboard_state(const dashboard_state_t *state) {
  if (state == NULL) {
    return;
  }

  portENTER_CRITICAL(&s_dashboard_state_lock);
  s_latest_dashboard_state = *state;
  s_latest_dashboard_state_valid = true;
  portEXIT_CRITICAL(&s_dashboard_state_lock);
}

static bool read_dashboard_state_snapshot(dashboard_state_t *out) {
  if (out == NULL) {
    return false;
  }

  bool valid = false;
  portENTER_CRITICAL(&s_dashboard_state_lock);
  if (s_latest_dashboard_state_valid) {
    *out = s_latest_dashboard_state;
    valid = true;
  }
  portEXIT_CRITICAL(&s_dashboard_state_lock);
  return valid;
}

static void fill_smoke_test_sensor_state(dashboard_state_t *state) {
  int elapsed;
  int temp_wave;
  int hum_wave;
  int eco2_wave;
  int tvoc_wave;

  if (state == NULL) {
    return;
  }

  // This block is intentionally stubbed for smoke testing.
  // When real sensors are wired in, replace this function with hardware reads.
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
  BaseType_t task_ok = xTaskCreate(delayed_restart_task, "cfg_restart", 2048,
                                   NULL, 5, NULL);
  if (task_ok != pdPASS) {
    s_restart_scheduled = false;
    ESP_LOGE(TAG, "Failed to schedule delayed restart");
  }
}

static void url_decode_inplace(char *value) {
  if (value == NULL) {
    return;
  }

  char *read = value;
  char *write = value;
  while (*read != '\0') {
    if (*read == '+') {
      *write++ = ' ';
      read++;
      continue;
    }

    if (*read == '%' && isxdigit((unsigned char)read[1]) &&
        isxdigit((unsigned char)read[2])) {
      int hi = isdigit((unsigned char)read[1]) ? (read[1] - '0')
                                               : (tolower((unsigned char)read[1]) - 'a' + 10);
      int lo = isdigit((unsigned char)read[2]) ? (read[2] - '0')
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
  if (body == NULL || key == NULL || out == NULL || out_len == 0) {
    return false;
  }

  size_t key_len = strlen(key);
  const char *cursor = body;
  while (cursor != NULL && *cursor != '\0') {
    const char *separator = strchr(cursor, '&');
    size_t token_len =
        separator != NULL ? (size_t)(separator - cursor) : strlen(cursor);

    if (token_len > key_len + 1 && strncmp(cursor, key, key_len) == 0 &&
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
  if (dest_size == 0) {
    return;
  }
  if (source == NULL) {
    dest[0] = '\0';
    return;
  }

  size_t write = 0;
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
  if (req == NULL || out == NULL || out_len == 0) {
    return false;
  }
  if (req->content_len <= 0 || (size_t)req->content_len >= out_len) {
    return false;
  }

  int received = 0;
  int remaining = req->content_len;
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

static esp_err_t serve_embedded_file(httpd_req_t *req, const uint8_t *start,
                                     const uint8_t *end,
                                     const char *content_type) {
  if (start == NULL || end == NULL || end < start) {
    return ESP_FAIL;
  }

  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, content_type);
  set_common_http_headers(req);
  size_t data_len = (size_t)(end - start);
  // EMBED_TXTFILES appends a '\0' byte; do not send it to browsers.
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
  return httpd_resp_send(req, "Redirecting to setup portal", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t config_state_get_handler(httpd_req_t *req) {
  char escaped_ssid[160];
  char escaped_ap_ssid[160];
  char runtime_ip[20] = "";
  char payload[WIFI_STATE_JSON_MAX_LEN];

  json_escape(s_wifi_credentials.valid ? s_wifi_credentials.ssid : "",
              escaped_ssid, sizeof(escaped_ssid));
  json_escape(s_provisioning_ap_ssid, escaped_ap_ssid, sizeof(escaped_ap_ssid));

  if (s_wifi_sta_netif != NULL) {
    esp_netif_ip_info_t ip_info = {0};
    if (esp_netif_get_ip_info(s_wifi_sta_netif, &ip_info) == ESP_OK) {
      snprintf(runtime_ip, sizeof(runtime_ip), IPSTR, IP2STR(&ip_info.ip));
    }
  }

  snprintf(payload, sizeof(payload),
           "{\"ok\":true,\"mode\":\"%s\",\"connected\":%s,"
           "\"provisioning\":%s,\"credentialSource\":\"%s\","
           "\"currentSsid\":\"%s\",\"apSsid\":\"%s\",\"apPassword\":\"%s\","
           "\"canStartPortal\":%s,\"runtimeIp\":\"%s\",\"runtimeHost\":\"%s.local\"}",
           s_provisioning_portal_active ? "provisioning" : "runtime",
           wifi_link_is_up() ? "true" : "false",
           s_provisioning_portal_active ? "true" : "false",
           s_wifi_credentials.valid
               ? (s_wifi_credentials.from_nvs ? "nvs" : "build")
               : "none",
           escaped_ssid, escaped_ap_ssid, WIFI_PROVISIONING_AP_PASS,
           s_wifi_credentials.valid ? "true" : "false", runtime_ip,
           RUNTIME_MDNS_HOSTNAME);

  return send_json_response(req, "200 OK", payload);
}

static esp_err_t config_telemetry_get_handler(httpd_req_t *req) {
  dashboard_state_t state = {0};
  if (!read_dashboard_state_snapshot(&state)) {
    build_runtime_dashboard_state(&state);
  }

  char payload[512];
  snprintf(payload, sizeof(payload),
           "{\"ok\":true,\"aqi\":%d,\"eco2\":%d,\"tvoc\":%d,\"ensValidity\":%d,"
           "\"tempC\":%.1f,\"humidity\":%d}",
           state.aqi, state.eco2_ppm, state.tvoc_ppb, state.ens_validity,
           state.temp_tenths_c / 10.0f, state.humidity_pct);
  return send_json_response(req, "200 OK", payload);
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
  if (payload == NULL) {
    free(ap_records);
    return send_json_message(req, "500 Internal Server Error", false,
                             "Out of memory");
  }

  char(*seen_ssids)[33] = calloc(WIFI_SCAN_MAX_RESULTS, sizeof(*seen_ssids));
  if (seen_ssids == NULL) {
    free(payload);
    free(ap_records);
    return send_json_message(req, "500 Internal Server Error", false,
                             "Out of memory");
  }

  int written = snprintf(payload, WIFI_SCAN_JSON_MAX_LEN, "{\"ok\":true,\"items\":[");
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
        escaped_auth, ap_records[i].authmode == WIFI_AUTH_OPEN ? "false"
                                                                : "true");
    if (append < 0 || (size_t)append >= WIFI_SCAN_JSON_MAX_LEN - (size_t)written) {
      free(seen_ssids);
      free(payload);
      free(ap_records);
      return send_json_message(req, "500 Internal Server Error", false,
                               "Scan payload overflow");
    }
    written += append;
    visible_count++;
  }

  int tail = snprintf(payload + written, WIFI_SCAN_JSON_MAX_LEN - (size_t)written,
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

  char ssid[sizeof(s_wifi_credentials.ssid)] = {0};
  char password[sizeof(s_wifi_credentials.password)] = {0};
  bool has_ssid = form_get_value(body, "ssid", ssid, sizeof(ssid));
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

  bool did_write = false;
  esp_err_t ret = wifi_save_credentials_to_nvs(ssid, password, &did_write);
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
    return send_json_message(req, "200 OK", true,
                             "Credentials unchanged. Flash write skipped.");
  }

  ESP_LOGI(TAG, "Wi-Fi credentials saved (ssid=%s). Restarting...", ssid);
  schedule_delayed_restart();
  return send_json_message(req, "200 OK", true,
                           "Saved. Device restarting now.");
}

static esp_err_t config_provisioning_start_post_handler(httpd_req_t *req) {
  if (!wifi_credentials_configured()) {
    if (!start_provisioning_portal()) {
      return send_json_message(req, "500 Internal Server Error", false,
                               "Failed to start setup hotspot");
    }
    return send_json_message(req, "200 OK", true,
                             "Setup hotspot is active.");
  }

  if (!start_provisioning_portal()) {
    return send_json_message(req, "500 Internal Server Error", false,
                             "Failed to start setup hotspot");
  }
  return send_json_message(req, "200 OK", true,
                           "Setup hotspot enabled for Wi-Fi change.");
}

static bool start_config_http_server(void) {
  if (s_config_http_server != NULL) {
    return true;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 24;
  config.stack_size = 6144;
  config.uri_match_fn = httpd_uri_match_wildcard;

  esp_err_t ret = httpd_start(&s_config_http_server, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
    s_config_http_server = NULL;
    return false;
  }

  httpd_uri_t root_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = config_root_get_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t css_uri = {
      .uri = "/app.css*",
      .method = HTTP_GET,
      .handler = config_css_get_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t js_uri = {
      .uri = "/app.js*",
      .method = HTTP_GET,
      .handler = config_js_get_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t captive_android_204_uri = {
      .uri = "/generate_204",
      .method = HTTP_GET,
      .handler = config_captive_redirect_get_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t captive_android_gen_204_uri = {
      .uri = "/gen_204",
      .method = HTTP_GET,
      .handler = config_captive_redirect_get_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t captive_ios_hotspot_uri = {
      .uri = "/hotspot-detect.html",
      .method = HTTP_GET,
      .handler = config_captive_redirect_get_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t captive_ios_library_test_uri = {
      .uri = "/library/test/success.html",
      .method = HTTP_GET,
      .handler = config_captive_redirect_get_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t captive_windows_ncsi_uri = {
      .uri = "/ncsi.txt",
      .method = HTTP_GET,
      .handler = config_captive_redirect_get_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t captive_windows_connect_test_uri = {
      .uri = "/connecttest.txt",
      .method = HTTP_GET,
      .handler = config_captive_redirect_get_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t state_uri = {
      .uri = "/api/state",
      .method = HTTP_GET,
      .handler = config_state_get_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t telemetry_uri = {
      .uri = "/api/telemetry",
      .method = HTTP_GET,
      .handler = config_telemetry_get_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t scan_uri = {
      .uri = "/api/scan",
      .method = HTTP_GET,
      .handler = config_scan_get_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t wifi_uri = {
      .uri = "/api/wifi",
      .method = HTTP_POST,
      .handler = config_wifi_post_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t legacy_wifi_uri = {
      .uri = "/wifi",
      .method = HTTP_POST,
      .handler = config_wifi_post_handler,
      .user_ctx = NULL,
  };
  httpd_uri_t provisioning_start_uri = {
      .uri = "/api/provisioning/start",
      .method = HTTP_POST,
      .handler = config_provisioning_start_post_handler,
      .user_ctx = NULL,
  };

  ret = httpd_register_uri_handler(s_config_http_server, &root_uri);
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server, &css_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server, &js_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server,
                                     &captive_android_204_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server,
                                     &captive_android_gen_204_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server,
                                     &captive_ios_hotspot_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server,
                                     &captive_ios_library_test_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server,
                                     &captive_windows_ncsi_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server,
                                     &captive_windows_connect_test_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server, &state_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server, &telemetry_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server, &scan_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server, &wifi_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server, &legacy_wifi_uri);
  }
  if (ret == ESP_OK) {
    ret = httpd_register_uri_handler(s_config_http_server,
                                     &provisioning_start_uri);
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Register HTTP handlers failed: %s", esp_err_to_name(ret));
    httpd_stop(s_config_http_server);
    s_config_http_server = NULL;
    return false;
  }

  ESP_LOGI(TAG, "Config web server started");
  return true;
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
    return true;
  }

  ensure_provisioning_ap_ssid();

  wifi_config_t ap_config = {
      .ap =
          {
              .channel = 1,
              .max_connection = WIFI_PROVISIONING_MAX_CONN,
              .authmode = WIFI_AUTH_WPA_WPA2_PSK,
              .pmf_cfg =
                  {
                      .capable = true,
                      .required = false,
                  },
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

  s_wifi_should_connect = has_credentials;
  wifi_clear_status_bits();

  esp_err_t ret = esp_wifi_set_mode(has_credentials ? WIFI_MODE_APSTA : WIFI_MODE_AP);
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
      ESP_LOGE(TAG, "esp_wifi_set_config(STA) failed: %s", esp_err_to_name(ret));
      return false;
    }
  }

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

static void ensure_runtime_config_server(void) {
  esp_err_t dns_ret = captive_dns_stop();
  if (dns_ret != ESP_OK) {
    ESP_LOGW(TAG, "Captive DNS stop failed: %s", esp_err_to_name(dns_ret));
  }

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

  // Give DHCP/DNS a moment to settle after STA gets an IP.
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
  // UTC+7 (Ho Chi Minh City): POSIX TZ uses reversed sign semantics.
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

  strlcpy((char *)wifi_config->sta.ssid, s_wifi_credentials.ssid,
          sizeof(wifi_config->sta.ssid));
  strlcpy((char *)wifi_config->sta.password, s_wifi_credentials.password,
          sizeof(wifi_config->sta.password));
  if (s_wifi_credentials.password[0] == '\0') {
    wifi_config->sta.threshold.authmode = WIFI_AUTH_OPEN;
  }
}

static bool wifi_start_and_wait_for_connection(void) {
  if (!wifi_credentials_configured()) {
    return false;
  }

  if (s_wifi_event_group != NULL) {
    EventBits_t current_bits = xEventGroupGetBits(s_wifi_event_group);
    if ((current_bits & WIFI_CONNECTED_BIT) != 0) {
      s_wifi_should_connect = true;
      s_provisioning_portal_active = false;
      ensure_runtime_config_server();
      ESP_LOGI(TAG, "Wi-Fi already connected, reuse existing link");
      return true;
    }
  }

  wifi_config_t wifi_config = {0};
  fill_sta_wifi_config(&wifi_config);

  s_wifi_retry_num = 0;
  s_wifi_should_connect = true;
  wifi_clear_status_bits();
  ESP_LOGI(TAG, "Start Wi-Fi connection flow");

  esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
    s_wifi_should_connect = false;
    return false;
  }

  ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
    s_wifi_should_connect = false;
    return false;
  }

  ret = esp_wifi_start();
  if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
    ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
    s_wifi_should_connect = false;
    return false;
  }
  ensure_mdns_service();
  if (ret == ESP_ERR_WIFI_CONN) {
    ESP_LOGI(TAG, "esp_wifi_start skipped because Wi-Fi is already running");
  }

  ret = esp_wifi_connect();
  if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
    ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
    s_wifi_should_connect = false;
    return false;
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
    return true;
  }

  if ((bits & WIFI_FAIL_BIT) != 0) {
    ESP_LOGW(TAG, "Wi-Fi connect failed after retries");
  } else {
    ESP_LOGW(TAG, "Wi-Fi connect timeout, keep auto-connect enabled");
  }
  return false;
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
  bool connected = false;
  bool synced = false;

  if (!wifi_service_init_once()) {
    return false;
  }

  if (!wifi_credentials_configured()) {
    s_wifi_connect_fail_cycles = 0;
    start_provisioning_portal();
    return false;
  }

  if (s_provisioning_portal_active) {
    // Provisioning mode is a user-driven state; do not auto-switch back.
    return false;
  }

  connected = wifi_start_and_wait_for_connection();
  if (!connected) {
    if (!s_provisioning_portal_active) {
      s_wifi_connect_fail_cycles++;
      ESP_LOGW(TAG, "Wi-Fi connect cycle failed (%d/%d)",
               s_wifi_connect_fail_cycles, WIFI_CONNECT_FAIL_PORTAL_THRESHOLD);
      if (s_wifi_connect_fail_cycles >= WIFI_CONNECT_FAIL_PORTAL_THRESHOLD) {
        ESP_LOGW(TAG,
                 "Auto-enable setup hotspot after repeated connection failures");
        if (start_provisioning_portal()) {
          s_wifi_connect_fail_cycles = 0;
        } else {
          ESP_LOGE(TAG, "Failed to auto-start setup hotspot");
        }
      }
    }
    return false;
  }
  s_wifi_connect_fail_cycles = 0;

  synced = sync_time_tphcm();
  s_time_synced = s_time_synced || synced || is_system_time_valid();
  return synced;
}

static void clock_sync_task(void *arg) {
  (void)arg;

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (s_wifi_event_group != NULL) {
      xEventGroupClearBits(s_wifi_event_group,
                           CLOCK_SYNC_DONE_BIT | CLOCK_SYNC_OK_BIT);
    }

    bool synced = run_clock_sync_cycle();
    if (s_wifi_event_group != NULL) {
      EventBits_t bits = CLOCK_SYNC_DONE_BIT;
      if (synced || is_system_time_valid()) {
        s_time_synced = true;
        bits |= CLOCK_SYNC_OK_BIT;
      }
      xEventGroupSetBits(s_wifi_event_group, bits);
    }

    schedule_clock_sync_retry(s_time_synced ? CLOCK_SYNC_SUCCESS_INTERVAL_MS
                                            : CLOCK_SYNC_RETRY_INTERVAL_MS);
  }
}

static void setup_connectivity_and_clock(void) {
  if (!wifi_service_init_once()) {
    return;
  }

  if (!wifi_credentials_configured()) {
    if (!start_provisioning_portal()) {
      ESP_LOGE(TAG, "Failed to start provisioning portal");
    }
    return;
  }

  if (s_clock_sync_task_handle == NULL) {
    BaseType_t task_ok = xTaskCreate(clock_sync_task, "clock_sync", 4096, NULL,
                                     5, &s_clock_sync_task_handle);
    if (task_ok != pdPASS) {
      ESP_LOGE(TAG, "Failed to create clock sync task");
      s_clock_sync_task_handle = NULL;
      return;
    }
  }

  if (s_clock_sync_timer == NULL) {
    s_clock_sync_timer = xTimerCreate("clock_sync_timer", pdMS_TO_TICKS(1000),
                                      pdFALSE, NULL, clock_sync_timer_callback);
    if (s_clock_sync_timer == NULL) {
      ESP_LOGE(TAG, "Failed to create clock sync timer");
      return;
    }
  }

  xEventGroupClearBits(s_wifi_event_group,
                       CLOCK_SYNC_DONE_BIT | CLOCK_SYNC_OK_BIT);
  xTaskNotifyGive(s_clock_sync_task_handle);

  EventBits_t bits = xEventGroupWaitBits(
      s_wifi_event_group, CLOCK_SYNC_DONE_BIT | CLOCK_SYNC_OK_BIT, pdFALSE,
      pdFALSE, pdMS_TO_TICKS(CLOCK_SYNC_INITIAL_WAIT_MS));
  if ((bits & CLOCK_SYNC_OK_BIT) != 0 || is_system_time_valid()) {
    s_time_synced = true;
  }
}

static void lcd_send_cmd(uint8_t cmd) {
  gpio_set_level((gpio_num_t)PIN_NUM_DC, 0);

  spi_transaction_t transaction = {
      .length = 8,
      .flags = SPI_TRANS_USE_TXDATA,
  };
  transaction.tx_data[0] = cmd;
  ESP_ERROR_CHECK(spi_device_transmit(s_lcd_spi, &transaction));
}

static void lcd_send_data(const uint8_t *data, size_t len) {
  if (len == 0) {
    return;
  }

  gpio_set_level((gpio_num_t)PIN_NUM_DC, 1);

  spi_transaction_t transaction = {
      .length = len * 8,
      .tx_buffer = data,
  };
  ESP_ERROR_CHECK(spi_device_transmit(s_lcd_spi, &transaction));
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  x0 = (uint16_t)(x0 + TFT_X_OFFSET);
  x1 = (uint16_t)(x1 + TFT_X_OFFSET);
  y0 = (uint16_t)(y0 + TFT_Y_OFFSET);
  y1 = (uint16_t)(y1 + TFT_Y_OFFSET);

  uint8_t data[4];

  lcd_send_cmd(0x2A);
  data[0] = x0 >> 8;
  data[1] = x0 & 0xFF;
  data[2] = x1 >> 8;
  data[3] = x1 & 0xFF;
  lcd_send_data(data, sizeof(data));

  lcd_send_cmd(0x2B);
  data[0] = y0 >> 8;
  data[1] = y0 & 0xFF;
  data[2] = y1 >> 8;
  data[3] = y1 & 0xFF;
  lcd_send_data(data, sizeof(data));

  lcd_send_cmd(0x2C);
}

static void lcd_present_framebuffer(void) {
  static uint16_t line_buffer[TFT_WIDTH];

  lcd_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
  for (int y = 0; y < TFT_HEIGHT; ++y) {
    const uint16_t *source = &s_framebuffer[y * TFT_WIDTH];
    for (int x = 0; x < TFT_WIDTH; ++x) {
      line_buffer[x] = swap16(source[x]);
    }
    lcd_send_data((const uint8_t *)line_buffer, sizeof(line_buffer));
  }
}

static void lcd_init(void) {
  spi_bus_config_t buscfg = {
      .mosi_io_num = PIN_NUM_MOSI,
      .miso_io_num = -1,
      .sclk_io_num = PIN_NUM_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = TFT_WIDTH * 2,
  };

  spi_device_interface_config_t devcfg = {
      .clock_speed_hz = 26 * 1000 * 1000,
      .mode = 0,
      .spics_io_num = PIN_NUM_CS,
      .queue_size = 1,
  };

  setup_output_pin(PIN_NUM_DC, 0);
  setup_output_pin(PIN_NUM_RST, 1);
  setup_output_pin(PIN_NUM_BL, 0);

  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &s_lcd_spi));

  gpio_set_level((gpio_num_t)PIN_NUM_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level((gpio_num_t)PIN_NUM_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level((gpio_num_t)PIN_NUM_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(120));

  for (size_t i = 0; i < sizeof(kLcdInit) / sizeof(kLcdInit[0]); ++i) {
    lcd_send_cmd(kLcdInit[i].cmd);
    lcd_send_data(kLcdInit[i].data, kLcdInit[i].data_len);
    if (kLcdInit[i].delay_ms > 0) {
      vTaskDelay(pdMS_TO_TICKS(kLcdInit[i].delay_ms));
    }
  }

  if (PIN_NUM_BL >= 0) {
    gpio_set_level((gpio_num_t)PIN_NUM_BL, 1);
  }
}

static void fb_clear(uint16_t color) {
  // Fast path: memset is hardware-optimised for the all-zero case (black
  // background).
  if (color == 0) {
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    return;
  }
  for (size_t i = 0; i < sizeof(s_framebuffer) / sizeof(s_framebuffer[0]);
       ++i) {
    s_framebuffer[i] = color;
  }
}

static void fb_draw_pixel(int x, int y, uint16_t color) {
  if (x < 0 || x >= TFT_WIDTH || y < 0 || y >= TFT_HEIGHT) {
    return;
  }
  s_framebuffer[y * TFT_WIDTH + x] = color;
}

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color) {
  if (w <= 0 || h <= 0) {
    return;
  }

  int x0 = clamp_int(x, 0, TFT_WIDTH);
  int y0 = clamp_int(y, 0, TFT_HEIGHT);
  int x1 = clamp_int(x + w, 0, TFT_WIDTH);
  int y1 = clamp_int(y + h, 0, TFT_HEIGHT);
  int row_len = x1 - x0;

  for (int yy = y0; yy < y1; ++yy) {
    uint16_t *row = &s_framebuffer[yy * TFT_WIDTH + x0];
    for (int xx = 0; xx < row_len; ++xx) {
      row[xx] = color;
    }
  }
}

static void fb_draw_hline(int x, int y, int w, uint16_t color) {
  fb_fill_rect(x, y, w, 1, color);
}

static void fb_draw_vline(int x, int y, int h, uint16_t color) {
  fb_fill_rect(x, y, 1, h, color);
}

static void fb_draw_rect(int x, int y, int w, int h, uint16_t color) {
  if (w <= 1 || h <= 1) {
    return;
  }

  fb_draw_hline(x, y, w, color);
  fb_draw_hline(x, y + h - 1, w, color);
  fb_draw_vline(x, y, h, color);
  fb_draw_vline(x + w - 1, y, h, color);
}

static void fb_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    fb_draw_pixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static void fb_fill_circle(int cx, int cy, int radius, uint16_t color) {
  int radius_sq = radius * radius;
  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      if ((x * x) + (y * y) <= radius_sq) {
        fb_draw_pixel(cx + x, cy + y, color);
      }
    }
  }
}

static void fb_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                             uint16_t color) {
  int min_x = x0;
  int max_x = x0;
  int min_y = y0;
  int max_y = y0;

  if (x1 < min_x)
    min_x = x1;
  if (x2 < min_x)
    min_x = x2;
  if (x1 > max_x)
    max_x = x1;
  if (x2 > max_x)
    max_x = x2;
  if (y1 < min_y)
    min_y = y1;
  if (y2 < min_y)
    min_y = y2;
  if (y1 > max_y)
    max_y = y1;
  if (y2 > max_y)
    max_y = y2;

  for (int y = min_y; y <= max_y; ++y) {
    for (int x = min_x; x <= max_x; ++x) {
      int w0 = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
      int w1 = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
      int w2 = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
      if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
        fb_draw_pixel(x, y, color);
      }
    }
  }
}

static const uint8_t *font_lookup(char c) {
  for (size_t i = 0; i < sizeof(kFont3x5) / sizeof(kFont3x5[0]); ++i) {
    if (kFont3x5[i].c == c) {
      return kFont3x5[i].rows;
    }
  }
  return kFont3x5[0].rows;
}

static const uint8_t *font5x7_lookup(char c) {
  for (size_t i = 0; i < sizeof(kFont5x7) / sizeof(kFont5x7[0]); ++i) {
    if (kFont5x7[i].c == c) {
      return kFont5x7[i].rows;
    }
  }
  return kFont5x7[0].rows;
}

static int text_width(const char *text, int scale) {
  return (int)strlen(text) * 4 * scale;
}

static int text5x7_width(const char *text, int scale) {
  return (int)strlen(text) * 6 * scale;
}

static void fb_draw_char(int x, int y, char c, uint16_t color, int scale) {
  const uint8_t *rows = font_lookup(c);

  for (int row = 0; row < 5; ++row) {
    for (int col = 0; col < 3; ++col) {
      if (rows[row] & (1 << (2 - col))) {
        fb_fill_rect(x + (col * scale), y + (row * scale), scale, scale, color);
      }
    }
  }
}

static void fb_draw_char5x7(int x, int y, char c, uint16_t color, int scale) {
  const uint8_t *rows = font5x7_lookup(c);

  for (int row = 0; row < 7; ++row) {
    for (int col = 0; col < 5; ++col) {
      if (rows[row] & (1 << (4 - col))) {
        fb_fill_rect(x + (col * scale), y + (row * scale), scale, scale, color);
      }
    }
  }
}

static void fb_draw_text(int x, int y, const char *text, uint16_t color,
                         int scale) {
  // Compute length once to avoid O(n^2) repeated strlen calls.
  int len = (int)strlen(text);
  for (int i = 0; i < len; ++i) {
    fb_draw_char(x + i * 4 * scale, y, text[i], color, scale);
  }
}

static void fb_draw_text5x7(int x, int y, const char *text, uint16_t color,
                            int scale) {
  int len = (int)strlen(text);
  for (int i = 0; i < len; ++i) {
    fb_draw_char5x7(x + i * 6 * scale, y, text[i], color, scale);
  }
}

static void fb_draw_text_shadow(int x, int y, const char *text, uint16_t color,
                                uint16_t shadow_color, int scale) {
  fb_draw_text(x + 1, y + 1, text, shadow_color, scale);
  fb_draw_text(x, y, text, color, scale);
}

static void fb_draw_text5x7_shadow(int x, int y, const char *text,
                                   uint16_t color, uint16_t shadow_color,
                                   int scale) {
  fb_draw_text5x7(x + 1, y + 1, text, shadow_color, scale);
  fb_draw_text5x7(x, y, text, color, scale);
}

static void fb_draw_text_centered(int center_x, int y, const char *text,
                                  uint16_t color, int scale) {
  fb_draw_text(center_x - text_width(text, scale) / 2, y, text, color, scale);
}

static void fb_draw_text5x7_centered(int center_x, int y, const char *text,
                                     uint16_t color, int scale) {
  fb_draw_text5x7(center_x - text5x7_width(text, scale) / 2, y, text, color,
                  scale);
}

static void fb_draw_text_right(int right_x, int y, const char *text,
                               uint16_t color, int scale) {
  fb_draw_text(right_x - text_width(text, scale), y, text, color, scale);
}

static void fb_draw_arc_segment(int cx, int cy, int inner_r, int outer_r,
                                float start_deg, float end_deg,
                                uint16_t color) {
  for (float deg = start_deg; deg <= end_deg; deg += 1.4f) {
    float rad = deg * DEG_TO_RAD;
    int x0 = cx + (int)(cosf(rad) * inner_r);
    int y0 = cy + (int)(sinf(rad) * inner_r);
    int x1 = cx + (int)(cosf(rad) * outer_r);
    int y1 = cy + (int)(sinf(rad) * outer_r);
    fb_draw_line(x0, y0, x1, y1, color);
  }
}

static void draw_starry_background(void) {
  fb_clear(COLOR_BG);

  fb_fill_circle(28, 24, 17, RGB565(7, 18, 34));
  fb_fill_circle(118, 35, 22, RGB565(6, 16, 30));
  fb_fill_circle(95, 92, 26, RGB565(8, 14, 24));

  for (size_t i = 0; i < sizeof(kStars) / sizeof(kStars[0]); ++i) {
    fb_draw_pixel(kStars[i].x, kStars[i].y, kStars[i].color);
  }
}

static uint16_t aqi_color(int aqi) {
  if (aqi <= 1) {
    return COLOR_GREEN;
  }
  if (aqi == 2) {
    return COLOR_LIME;
  }
  if (aqi == 3) {
    return COLOR_YELLOW;
  }
  if (aqi == 4) {
    return COLOR_ORANGE;
  }
  return COLOR_RED;
}

static const char *aqi_label(int aqi) {
  if (aqi <= 1) {
    return "EXCLNT";
  }
  if (aqi == 2) {
    return "GOOD";
  }
  if (aqi == 3) {
    return "MODER";
  }
  if (aqi == 4) {
    return "POOR";
  }
  return "UNHLTY";
}

static const char *ens160_validity_label(int validity) {
  switch (validity) {
  case 0:
    return "READY";
  case 1:
    return "WARMUP";
  case 2:
    return "STARTUP";
  default:
    return "CHECK";
  }
}

static void aqi_subtext(int aqi, const char **line1, const char **line2) {
  if (aqi <= 2) {
    *line1 = "SAFE AIR";
    *line2 = "LOW RISK";
    return;
  }
  if (aqi == 3) {
    *line1 = "OPEN AIR";
    *line2 = "IF NEEDED";
    return;
  }
  if (aqi == 4) {
    *line1 = "VENTILATE";
    *line2 = "SOON";
    return;
  }
  *line1 = "CHECK AIR";
  *line2 = "NOW";
}

static void draw_wifi_arc_band(int cx, int cy, int inner_radius,
                               int outer_radius, float start_deg, float end_deg,
                               uint16_t color) {
  const float center_deg = (start_deg + end_deg) * 0.5f;
  const float half_sweep = (end_deg - start_deg) * 0.5f;

  for (float offset = 0.0f; offset <= half_sweep; offset += 1.0f) {
    float left_rad = (center_deg - offset) * DEG_TO_RAD;
    float right_rad = (center_deg + offset) * DEG_TO_RAD;

    for (int radius = inner_radius; radius <= outer_radius; ++radius) {
      int left_x = cx + (int)lroundf(cosf(left_rad) * radius);
      int left_y = cy + (int)lroundf(sinf(left_rad) * radius);
      int right_x = cx + (int)lroundf(cosf(right_rad) * radius);
      int right_y = cy + (int)lroundf(sinf(right_rad) * radius);

      fb_draw_line(left_x, left_y, right_x, right_y, color);
    }
  }
}

static void draw_wifi_icon(int cx, int cy, uint16_t color) {
  const int base_y = cy + 8;

  draw_wifi_arc_band(cx, base_y, 6, 7, 222.0f, 318.0f, color);
  draw_wifi_arc_band(cx, base_y, 3, 4, 228.0f, 312.0f, color);
  draw_wifi_arc_band(cx, base_y, 1, 2, 238.0f, 302.0f, color);
  fb_draw_pixel(cx, base_y - 1, color);
  fb_draw_line(cx - 1, base_y, cx + 1, base_y, color);
  fb_draw_pixel(cx, base_y + 1, color);
}

static void draw_wifi_offline_icon(int cx, int cy, uint16_t color,
                                   uint16_t slash_color) {
  (void)slash_color;
  draw_wifi_icon(cx, cy, color);
}

static bool is_wifi_connected_for_ui(void) {
  if (s_wifi_event_group == NULL) {
    return false;
  }
  EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
  return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void draw_panel(int x, int y, int w, int h, uint16_t accent) {
  fb_fill_rect(x, y, w, h, COLOR_PANEL);
  fb_draw_rect(x, y, w, h, COLOR_DIVIDER);
  fb_fill_rect(x + 1, y + 1, w - 2, 2, accent);
  fb_fill_rect(x + 1, y + h - 3, w - 2, 2, COLOR_PANEL_ALT);
}

static void draw_metric_card(int x, int y, int w, int h, const char *label,
                             const char *value, const char *unit,
                             uint16_t accent, uint16_t value_color) {
  int label_x = x + (w - text_width(label, 1)) / 2;
  int value_x = x + (w - text_width(value, 2)) / 2;
  int unit_x = x + 4;

  draw_panel(x, y, w, h, accent);
  fb_draw_text(label_x, y + 5, label, COLOR_MUTED, 1);
  fb_draw_text(value_x, y + 12, value, value_color, 2);
  if (unit != NULL && unit[0] != '\0') {
    int unit_w = text_width(unit, 1);
    unit_x = x + w - unit_w - 4;
    fb_draw_text(unit_x, y + h - 7, unit, COLOR_MUTED, 1);
  }
}

static void draw_hybrid_metric_card(int x, int w, const char *label,
                                    const char *value, const char *unit,
                                    int unit_scale, bool show_degree_symbol,
                                    uint16_t value_color) {
  const int y = 95;
  const int h = 29;
  (void)unit_scale;
  int value_w = text_width(value, 2);
  bool compact_unit = (!show_degree_symbol && strlen(value) >= 4);
  int unit_w = compact_unit ? text_width(unit, 1) : text5x7_width(unit, 1);
  int degree_gap = show_degree_symbol ? 5 : 0;
  int unit_total_w = unit_w + degree_gap;
  int unit_left_x = x + w - unit_total_w - 3;
  int value_area_left = x + 4;
  int value_area_right = unit_left_x - 6;
  int value_x =
      value_area_left + ((value_area_right - value_area_left) - value_w) / 2;
  int unit_y = compact_unit ? (y + h - 8) : (y + h - 11);

  if (value_x < value_area_left) {
    value_x = value_area_left;
  }

  fb_fill_rect(x, y, w, h, RGB565(6, 14, 28));
  fb_draw_rect(x, y, w, h, RGB565(30, 92, 146));
  fb_fill_rect(x + 1, y + 1, w - 2, 2, RGB565(45, 128, 195));
  fb_draw_text5x7_centered(x + (w / 2), y + 4, label, COLOR_MUTED, 1);
  fb_draw_text_shadow(value_x, y + 15, value, value_color, RGB565(20, 44, 72),
                      2);
  if (show_degree_symbol) {
    fb_draw_rect(unit_left_x, unit_y + 1, 3, 3, COLOR_CYAN);
  }
  if (compact_unit) {
    fb_draw_text(unit_left_x + degree_gap, unit_y, unit, COLOR_CYAN, 1);
  } else {
    fb_draw_text5x7(unit_left_x + degree_gap, unit_y, unit, COLOR_CYAN, 1);
  }
}

static void draw_hybrid_overlay(const dashboard_state_t *state) {
  char time_text[16];
  char date_text[16];
  char eco2_text[16];
  char temp_text[16];
  char hum_text[16];
  char aqi_text[8];
  char sensor_line[20];
  const char *ens_status = ens160_validity_label(state->ens_validity);
  bool wifi_connected = is_wifi_connected_for_ui();
  int day_display = clamp_int(state->clock.tm_mday, 0, 99);
  int month_display = clamp_int(state->clock.tm_mon + 1, 0, 99);

  int year_display = clamp_int(state->clock.tm_year + 1900, 0, 9999);

  snprintf(time_text, sizeof(time_text), "%02d:%02d:%02d", state->clock.tm_hour,
           state->clock.tm_min, state->clock.tm_sec);
  snprintf(date_text, sizeof(date_text), "%02d/%02d/%04d", day_display,
           month_display, year_display);
  snprintf(eco2_text, sizeof(eco2_text), "%d", state->eco2_ppm);
  snprintf(temp_text, sizeof(temp_text), "%d.%d", state->temp_tenths_c / 10,
           abs(state->temp_tenths_c % 10));
  snprintf(hum_text, sizeof(hum_text), "%d", state->humidity_pct);
  snprintf(aqi_text, sizeof(aqi_text), "%d", state->aqi);
  snprintf(sensor_line, sizeof(sensor_line), "TVOC %d", state->tvoc_ppb);

  uint16_t aqi_col = aqi_color(state->aqi);
  const char *aqi_lbl = aqi_label(state->aqi);
  uint16_t glow = RGB565(16, 34, 55);
  const int left_header_x = 3;
  const int left_header_w = 70;
  const int right_header_x = 73;
  const int right_header_w = 84;
  const int time_x = 6;
  const int time_right_x = time_x + text5x7_width(time_text, 1);
  const int date_right_x = 153;
  const int date_x = date_right_x - text5x7_width(date_text, 1);
  const int wifi_cx = time_right_x + ((date_x - time_right_x) / 2);
  const int wifi_cy = 5;

  fb_clear(COLOR_BG);

  // Top bar: clock (left) + date (right)
  fb_fill_rect(2, 2, 156, 17, RGB565(4, 10, 20));
  fb_draw_rect(2, 2, 156, 17, RGB565(26, 70, 120));
  fb_fill_rect(left_header_x, 3, left_header_w, 15, RGB565(8, 22, 38));
  fb_fill_rect(right_header_x, 3, right_header_w, 15, RGB565(18, 20, 34));
  fb_draw_text5x7_shadow(time_x, 6, time_text, COLOR_CYAN, RGB565(8, 28, 42),
                         1);
  if (wifi_connected) {
    draw_wifi_icon(wifi_cx, wifi_cy, COLOR_LIME);
  } else {
    draw_wifi_offline_icon(wifi_cx, wifi_cy, COLOR_MUTED, COLOR_RED);
  }
  fb_draw_text5x7(date_x, 6, date_text, COLOR_YELLOW, 1);

  // Left panel: compact AQI block matching the proposal proportions.
  {
    const int px = 4, py = 23, pw = 58, ph = 69;
    const int cx = px + pw / 2;
    draw_panel(px, py, pw, ph, aqi_col);
    fb_draw_text5x7_centered(cx, py + 7, "AQI", COLOR_MUTED, 1);
    fb_draw_text_shadow(cx - text_width(aqi_text, 4) / 2, py + 24, aqi_text,
                        aqi_col, glow, 4);
    fb_draw_text5x7_centered(cx, py + 54, "INDEX", COLOR_MUTED, 1);
  }

  // Right panel: clearer hierarchy with one support line and a compact scale.
  {
    static const uint16_t kScaleColors[] = {
        COLOR_GREEN, COLOR_LIME, COLOR_YELLOW, COLOR_ORANGE, COLOR_RED,
    };
    const int px = 66, py = 23, pw = 90, ph = 69;
    const int cx = px + pw / 2;
    draw_panel(px, py, pw, ph, aqi_col);

    fb_draw_text5x7_shadow(cx - text5x7_width(aqi_lbl, 2) / 2, py + 8, aqi_lbl,
                           aqi_col, glow, 2);
    fb_draw_text5x7_centered(cx, py + 31, sensor_line, COLOR_WHITE, 1);
    fb_draw_text5x7_centered(cx, py + 43, ens_status, COLOR_MUTED, 1);

    const int seg_w = 13, seg_h = 6, seg_gap = 2;
    int bar_total = 5 * seg_w + 4 * seg_gap;
    int bar_x = cx - bar_total / 2;
    int bar_y = py + ph - 10;
    for (int s = 0; s < 5; s++) {
      uint16_t sc = (s < state->aqi) ? kScaleColors[s] : RGB565(22, 34, 50);
      fb_fill_rect(bar_x + s * (seg_w + seg_gap), bar_y, seg_w, seg_h, sc);
    }

    {
      int active_index = clamp_int(state->aqi - 1, 0, 4);
      int arrow_x = bar_x + active_index * (seg_w + seg_gap) + (seg_w / 2);
      int arrow_y = bar_y - 1;
      fb_fill_triangle(arrow_x, arrow_y, arrow_x - 2, arrow_y - 3, arrow_x + 2,
                       arrow_y - 3, aqi_col);
    }
  }

  draw_hybrid_metric_card(4, 52, "ECO2", eco2_text, "PPM", 1, false,
                          COLOR_YELLOW);
  draw_hybrid_metric_card(60, 52, "TEMP", temp_text, "C", 2, true,
                          COLOR_YELLOW);
  draw_hybrid_metric_card(116, 40, "HUMI", hum_text, "%", 2, false, COLOR_CYAN);
}

static void draw_boot_screen(int percent, const char *status) {
  char percent_text[8];
  const int shell_x = 6;
  const int shell_y = 8;
  const int shell_w = 148;
  const int shell_h = 112;
  const int shell_cx = shell_x + (shell_w / 2);
  const int bar_x = 18;
  const int bar_y = 92;
  const int bar_w = 124;
  const int bar_h = 9;
  const int card_x = 15;
  const int card_y = 60;
  const int card_w = 128;
  const int card_h = 28;
  int fill_w;
  uint16_t shell_border = RGB565(26, 72, 110);
  uint16_t shell_glow = RGB565(10, 28, 48);
  uint16_t accent = RGB565(70, 230, 255);
  uint16_t accent_soft = RGB565(32, 110, 170);
  uint16_t fill = RGB565(110, 245, 170);
  uint16_t fill_glow = RGB565(42, 120, 96);
  uint16_t dim = RGB565(118, 146, 176);

  percent = clamp_int(percent, 0, 100);
  fill_w = ((bar_w - 2) * percent) / 100;

  fb_clear(COLOR_BG);
  fb_fill_rect(4, 4, TFT_WIDTH - 8, TFT_HEIGHT - 8, RGB565(3, 8, 16));
  fb_draw_rect(1, 1, TFT_WIDTH - 2, TFT_HEIGHT - 2, RGB565(9, 34, 54));
  fb_draw_rect(4, 4, TFT_WIDTH - 8, TFT_HEIGHT - 8, RGB565(12, 48, 75));

  fb_fill_rect(shell_x, shell_y, shell_w, shell_h, shell_glow);
  fb_draw_rect(shell_x, shell_y, shell_w, shell_h, shell_border);
  fb_draw_rect(shell_x + 2, shell_y + 2, shell_w - 4, shell_h - 4,
               RGB565(9, 42, 68));

  fb_fill_rect(shell_x + 10, shell_y + 8, 22, 2, accent);
  fb_fill_rect(shell_x + 36, shell_y + 8, 8, 2, fill);
  fb_fill_rect(shell_x + shell_w - 32, shell_y + 8, 22, 2, accent_soft);

  fb_draw_text5x7_centered(shell_cx, 20, "AIR QUALITY NODE", dim, 1);
  fb_draw_text5x7_shadow(shell_cx - text5x7_width("SYSTEM BOOT", 2) / 2, 33,
                         "SYSTEM BOOT", COLOR_WHITE, RGB565(12, 36, 58), 2);
  fb_fill_rect(shell_cx - 18, 53, 36, 2, accent_soft);
  fb_fill_rect(shell_cx - 10, 53, 20, 2, accent);

  fb_fill_rect(card_x, card_y, card_w, card_h, RGB565(6, 18, 30));
  fb_draw_rect(card_x, card_y, card_w, card_h, accent_soft);
  fb_fill_rect(card_x + 1, card_y + 1, card_w - 2, 2, accent);
  fb_draw_text5x7(22, 66, "ACTIVE STAGE", COLOR_MUTED, 1);
  fb_draw_text5x7_centered(card_x + (card_w / 2), 78, status, COLOR_CYAN, 1);

  fb_fill_rect(bar_x, bar_y, bar_w, bar_h, RGB565(6, 16, 30));
  fb_draw_rect(bar_x, bar_y, bar_w, bar_h, accent_soft);
  if (fill_w > 0) {
    fb_fill_rect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, fill_glow);
    fb_fill_rect(bar_x + 1, bar_y + 1, fill_w, 2, fill);
  }

  snprintf(percent_text, sizeof(percent_text), "%3d%%", percent);
  fb_draw_text5x7(18, 106, "SYNC PROGRESS", COLOR_MUTED, 1);
  fb_draw_text5x7(shell_x + shell_w - 41, 106, percent_text, COLOR_WHITE, 1);
}

#if !SHOW_BITMAP_UI
static const uint16_t COLOR_LIME_SOFT = RGB565(120, 235, 90);
static const uint16_t COLOR_OLIVE = RGB565(68, 95, 30);

static void draw_aqi_scale_bar(int x, int y) {
  const uint16_t colors[] = {
      COLOR_GREEN,  COLOR_LIME,           COLOR_YELLOW,
      COLOR_ORANGE, RGB565(255, 115, 60), COLOR_RED,
  };

  for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
    fb_fill_rect(x + (int)i * 10, y, 8, 6, colors[i]);
  }
}

static void draw_aqi_gauge(const dashboard_state_t *state, int cx, int cy) {
  const uint16_t palette[] = {
      COLOR_GREEN,  COLOR_LIME_SOFT, COLOR_LIME,
      COLOR_YELLOW, COLOR_ORANGE,    COLOR_RED,
  };
  int active_segments = clamp_int((state->aqi * 2) + 1, 1, 10);
  const char *label = aqi_label(state->aqi);
  char value_text[8];

  for (int i = 0; i < 10; ++i) {
    float start_deg = 145.0f + (float)i * 23.5f;
    float end_deg = start_deg + 17.0f;
    uint16_t color = (i < active_segments) ? palette[i / 2] : COLOR_OLIVE;
    fb_draw_arc_segment(cx, cy, 18, 27, start_deg, end_deg, color);
  }

  fb_fill_circle(cx, cy, 16, COLOR_PANEL_ALT);

  fb_draw_text(cx - text_width("AQI", 2) / 2, cy - 18, "AQI",
               aqi_color(state->aqi), 2);
  snprintf(value_text, sizeof(value_text), "%d", state->aqi);
  fb_draw_text(cx - text_width(value_text, 5) / 2, cy - 2, value_text,
               aqi_color(state->aqi), 5);
  fb_draw_text(cx - text_width(label, 2) / 2, cy + 16, label,
               aqi_color(state->aqi), 2);
}

static void draw_dashboard(const dashboard_state_t *state) {
  char time_text[16];
  char date_text[16];
  char co2_text[16];
  char temp_text[16];
  char hum_text[16];
  const char *headline = aqi_label(state->aqi);
  const char *sub1 = NULL;
  const char *sub2 = NULL;
  int header_time_x;
  int header_date_x;
  int headline_x;
  int sub1_x;
  int sub2_x;
  const char *day_name[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

  snprintf(time_text, sizeof(time_text), "%02d:%02d:%02d", state->clock.tm_hour,
           state->clock.tm_min, state->clock.tm_sec);
  snprintf(date_text, sizeof(date_text), "%s %02d-%02d",
           day_name[state->clock.tm_wday], state->clock.tm_mon + 1,
           state->clock.tm_mday);
  snprintf(co2_text, sizeof(co2_text), "%d", state->co2_ppm);
  snprintf(temp_text, sizeof(temp_text), "%d.%d", state->temp_tenths_c / 10,
           abs(state->temp_tenths_c % 10));
  snprintf(hum_text, sizeof(hum_text), "%d", state->humidity_pct);
  aqi_subtext(state->aqi, &sub1, &sub2);
  header_time_x = 10;
  header_date_x = 10;
  headline_x = 118 - text_width(headline, 3) / 2;
  sub1_x = 118 - text_width(sub1, 2) / 2;
  sub2_x = 118 - text_width(sub2, 2) / 2;

  draw_starry_background();

  draw_panel(5, 5, 150, 27, COLOR_CYAN);
  draw_panel(5, 36, 72, 58, aqi_color(state->aqi));
  draw_panel(82, 36, 73, 58, COLOR_LIME);

  fb_draw_text(header_time_x, 9, time_text, COLOR_WHITE, 3);
  fb_draw_text(header_date_x, 21, date_text, COLOR_MUTED, 2);
  draw_wifi_icon(141, 11, COLOR_LIME);

  draw_aqi_gauge(state, 41, 66);

  fb_draw_text(headline_x, 44, headline, aqi_color(state->aqi), 3);
  fb_draw_text(sub1_x, 63, sub1, COLOR_WHITE, 2);
  fb_draw_text(sub2_x, 74, sub2, COLOR_MUTED, 2);
  draw_aqi_scale_bar(89, 85);

  draw_metric_card(5, 99, 46, 24, "CO2", co2_text, "PPM", COLOR_LIME,
                   COLOR_YELLOW);
  draw_metric_card(57, 99, 46, 24, "TEMP", temp_text, "C", COLOR_LIME_SOFT,
                   COLOR_YELLOW);
  draw_metric_card(109, 99, 46, 24, "HUM", hum_text, "%", COLOR_CYAN,
                   COLOR_CYAN);
}

#endif /* !SHOW_BITMAP_UI */

static void build_runtime_dashboard_state(dashboard_state_t *state) {
  static bool initialized = false;
  static time_t base_epoch;

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

  int elapsed = (int)(esp_timer_get_time() / 1000000LL);
  if (!s_time_synced) {
    // If SNTP sync finished after initial timeout, auto-switch to real clock.
    time_t now = 0;
    time(&now);
    if (now >= 1700000000) {
      s_time_synced = true;
      ESP_LOGI(TAG, "Real time became available, switching from demo clock");
    }
  }

  if (s_time_synced) {
    time_t now = 0;
    time(&now);
    localtime_r(&now, &state->clock);
  } else {
    time_t current = base_epoch + elapsed;
    localtime_r(&current, &state->clock);
  }

#if APP_SENSOR_SMOKE_TEST
  fill_smoke_test_sensor_state(state);
#else
  // TODO: Replace this block with real sensor reads when hardware is ready.
  // Keep field names stable so the display and webserver do not need changes.
  state->aqi = 0;
  state->eco2_ppm = 0;
  state->tvoc_ppb = 0;
  state->ens_validity = 3;
  state->temp_tenths_c = 0;
  state->humidity_pct = 0;
#endif
}

void app_main(void) {
  ESP_LOGI(TAG, "Starting ST7735 dashboard demo");
  lcd_init();

  draw_boot_screen(5, "POWER STABLE");
  lcd_present_framebuffer();
  vTaskDelay(pdMS_TO_TICKS(150));

  draw_boot_screen(18, "DISPLAY READY");
  lcd_present_framebuffer();
  vTaskDelay(pdMS_TO_TICKS(150));

  draw_boot_screen(35, "NETWORK STACK");
  lcd_present_framebuffer();
  vTaskDelay(pdMS_TO_TICKS(150));

  draw_boot_screen(52, "WIFI + NTP SYNC");
  lcd_present_framebuffer();
  setup_connectivity_and_clock();

  draw_boot_screen(s_time_synced ? 88 : 76,
                   s_time_synced ? "CLOCK LOCKED" : "OFFLINE MODE");
  lcd_present_framebuffer();
  vTaskDelay(pdMS_TO_TICKS(220));

  draw_boot_screen(100, "DASHBOARD READY");
  lcd_present_framebuffer();
  vTaskDelay(pdMS_TO_TICKS(220));

  while (true) {
    dashboard_state_t state = {0};
    build_runtime_dashboard_state(&state);
    snapshot_dashboard_state(&state);

#if SHOW_BITMAP_UI
    draw_hybrid_overlay(&state);
#else
    draw_dashboard(&state);
#endif
    lcd_present_framebuffer();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
