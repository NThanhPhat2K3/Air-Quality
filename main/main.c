#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
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
// 1 = cycle demo values through AQI thresholds so the full UI can be reviewed.
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
  uint8_t cmd;
  uint8_t data[16];
  uint8_t data_len;
  uint16_t delay_ms;
} lcd_init_cmd_t;

static const char *TAG = "st7735_dashboard";

static spi_device_handle_t s_lcd_spi;
static uint16_t s_framebuffer[TFT_WIDTH * TFT_HEIGHT];
static EventGroupHandle_t s_wifi_event_group;
static int s_wifi_retry_num;
static bool s_time_synced;

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

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  (void)arg;
  (void)event_data;

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
    return;
  }

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_wifi_retry_num < WIFI_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_wifi_retry_num++;
      return;
    }
    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    return;
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    s_wifi_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

static bool wifi_connect_sta(void) {
  if (strlen(WIFI_SSID) == 0 || strcmp(WIFI_SSID, "YOUR_WIFI_SSID") == 0) {
    ESP_LOGW(TAG, "WIFI_SSID not configured, skip Wi-Fi time sync");
    return false;
  }
  if (strcmp(WIFI_PASS, "YOUR_WIFI_PASSWORD") == 0) {
    ESP_LOGW(TAG, "WIFI_PASS is still placeholder, skip Wi-Fi time sync");
    return false;
  }

  s_wifi_event_group = xEventGroupCreate();
  if (s_wifi_event_group == NULL) {
    ESP_LOGE(TAG, "Cannot create Wi-Fi event group");
    return false;
  }

  s_wifi_retry_num = 0;

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler, NULL));

  wifi_config_t wifi_config = {
      .sta =
          {
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
              .pmf_cfg =
                  {
                      .capable = true,
                      .required = false,
                  },
          },
  };
  strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID,
          sizeof(wifi_config.sta.ssid));
  strlcpy((char *)wifi_config.sta.password, WIFI_PASS,
          sizeof(wifi_config.sta.password));
  if (strlen(WIFI_PASS) == 0) {
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  EventBits_t bits = xEventGroupWaitBits(
      s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
      pdMS_TO_TICKS(25000));

  if ((bits & WIFI_CONNECTED_BIT) != 0) {
    ESP_LOGI(TAG, "Wi-Fi connected");
    return true;
  }

  if ((bits & WIFI_FAIL_BIT) != 0) {
    ESP_LOGW(TAG, "Wi-Fi connect failed after retries");
    return false;
  }

  ESP_LOGW(TAG, "Wi-Fi connect timeout");
  return false;
}

static bool sync_time_tphcm(void) {
  esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");

  esp_err_t ret = esp_netif_sntp_init(&sntp_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SNTP init failed: %s", esp_err_to_name(ret));
    return false;
  }

  ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(20000));
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "SNTP sync timeout/error: %s", esp_err_to_name(ret));
    return false;
  }

  time_t now = 0;
  struct tm tm_info = {0};
  time(&now);
  localtime_r(&now, &tm_info);
  ESP_LOGI(TAG, "Time synced (TPHCM): %04d-%02d-%02d %02d:%02d:%02d",
           tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
           tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
  return true;
}

static void apply_timezone_tphcm(void) {
  // UTC+7 (Ho Chi Minh City): POSIX TZ uses reversed sign semantics.
  setenv("TZ", "UTC-7", 1);
  tzset();
}

static void setup_connectivity_and_clock(void) {
  esp_err_t ret = init_nvs_flash_storage();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
    return;
  }

  ret = esp_netif_init();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
    return;
  }

  ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(ret));
    return;
  }

  apply_timezone_tphcm();

  if (!wifi_connect_sta()) {
    return;
  }

  s_time_synced = sync_time_tphcm();
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

  if (x1 < min_x) min_x = x1;
  if (x2 < min_x) min_x = x2;
  if (x1 > max_x) max_x = x1;
  if (x2 > max_x) max_x = x2;
  if (y1 < min_y) min_y = y1;
  if (y2 < min_y) min_y = y2;
  if (y1 > max_y) max_y = y1;
  if (y2 > max_y) max_y = y2;

  for (int y = min_y; y <= max_y; ++y) {
    for (int x = min_x; x <= max_x; ++x) {
      int w0 = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
      int w1 = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
      int w2 = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
      if ((w0 >= 0 && w1 >= 0 && w2 >= 0) ||
          (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
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

static void draw_wifi_icon(int cx, int cy, uint16_t color) {
  fb_draw_arc_segment(cx, cy, 6, 7, 220.0f, 320.0f, color);
  fb_draw_arc_segment(cx, cy, 10, 11, 220.0f, 320.0f, color);
  fb_draw_arc_segment(cx, cy, 14, 15, 220.0f, 320.0f, color);
  fb_fill_circle(cx, cy + 8, 2, color);
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
                                    int unit_scale,
                                    bool show_degree_symbol,
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
  const int date_right_x = 153;
  const int date_x = date_right_x - text5x7_width(date_text, 1);

  fb_clear(COLOR_BG);

  // Top bar: clock (left) + date (right)
  fb_fill_rect(2, 2, 156, 17, RGB565(4, 10, 20));
  fb_draw_rect(2, 2, 156, 17, RGB565(26, 70, 120));
  fb_fill_rect(left_header_x, 3, left_header_w, 15, RGB565(8, 22, 38));
  fb_fill_rect(right_header_x, 3, right_header_w, 15, RGB565(18, 20, 34));
  fb_draw_text5x7_shadow(time_x, 6, time_text, COLOR_CYAN, RGB565(8, 28, 42),
                         1);
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
      fb_fill_triangle(arrow_x, arrow_y, arrow_x - 2, arrow_y - 3,
                       arrow_x + 2, arrow_y - 3, aqi_col);
    }
  }

  draw_hybrid_metric_card(4, 52, "ECO2", eco2_text, "PPM", 1, false,
                          COLOR_YELLOW);
  draw_hybrid_metric_card(60, 52, "TEMP", temp_text, "C", 2, true,
                          COLOR_YELLOW);
  draw_hybrid_metric_card(116, 40, "HUM", hum_text, "%", 2, false,
                          COLOR_CYAN);
}

static void draw_boot_screen(int percent, const char *status) {
  char percent_text[8];
  int bar_x = 14;
  int bar_y = 72;
  int bar_w = 132;
  int bar_h = 14;
  int fill_w;
  uint16_t chrome = RGB565(35, 120, 185);
  uint16_t accent = RGB565(70, 230, 255);
  uint16_t fill = RGB565(110, 245, 170);

  percent = clamp_int(percent, 0, 100);
  fill_w = ((bar_w - 2) * percent) / 100;

  fb_clear(COLOR_BG);
  fb_draw_rect(1, 1, TFT_WIDTH - 2, TFT_HEIGHT - 2, RGB565(14, 46, 74));
  fb_draw_rect(4, 4, TFT_WIDTH - 8, TFT_HEIGHT - 8, chrome);
  fb_draw_hline(8, 14, TFT_WIDTH - 16, RGB565(18, 66, 104));
  fb_draw_hline(8, 48, TFT_WIDTH - 16, RGB565(18, 66, 104));

  fb_draw_text(16, 20, "AIR CORE", accent, 3);
  fb_draw_text(20, 36, "SYSTEM INIT", COLOR_WHITE, 2);
  fb_draw_text(12, 56, status, COLOR_CYAN, 2);

  fb_fill_rect(bar_x, bar_y, bar_w, bar_h, RGB565(6, 16, 30));
  fb_draw_rect(bar_x, bar_y, bar_w, bar_h, chrome);
  if (fill_w > 0) {
    fb_fill_rect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, fill);
  }

  snprintf(percent_text, sizeof(percent_text), "%3d%%", percent);
  fb_draw_text(56, 94, percent_text, COLOR_WHITE, 3);
  fb_draw_text(20, 112, "DO NOT POWER OFF", RGB565(130, 180, 220), 1);
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

static void build_demo_state(dashboard_state_t *state) {
  static bool initialized = false;
  static time_t base_epoch;
#if DEMO_AQI_SWEEP
  static const int kDemoAqiLevels[] = {1, 2, 3, 4, 5};
  static const int kDemoEco2Levels[] = {420, 720, 950, 1300, 1800};
  static const int kDemoTvocLevels[] = {35, 120, 260, 420, 650};
#endif

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
  int temp_wave = (elapsed % 9) - 4;
  int hum_wave = (elapsed % 7) - 3;
  int eco2_wave = (elapsed % 11) - 5;
  int tvoc_wave = (elapsed % 13) - 6;

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

#if DEMO_AQI_SWEEP
  {
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
    build_demo_state(&state);

#if SHOW_BITMAP_UI
    draw_hybrid_overlay(&state);
#else
    draw_dashboard(&state);
#endif
    lcd_present_framebuffer();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
