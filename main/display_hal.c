#include "display_hal.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK 18
#define PIN_NUM_CS 5
#define PIN_NUM_DC 2
#define PIN_NUM_RST 4
#define PIN_NUM_BL -1

#define TFT_X_OFFSET 0
#define TFT_Y_OFFSET 0
#define TFT_MADCTL 0xA0

typedef struct {
  uint8_t cmd;
  uint8_t data[16];
  uint8_t data_len;
  uint16_t delay_ms;
} lcd_init_cmd_t;

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

static spi_device_handle_t s_lcd_spi;
static uint16_t s_framebuffer[TFT_WIDTH * TFT_HEIGHT];

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

static void lcd_send_cmd(uint8_t cmd) {
  spi_transaction_t transaction = {
      .length = 8,
      .flags = SPI_TRANS_USE_TXDATA,
  };

  gpio_set_level((gpio_num_t)PIN_NUM_DC, 0);
  transaction.tx_data[0] = cmd;
  ESP_ERROR_CHECK(spi_device_transmit(s_lcd_spi, &transaction));
}

static void lcd_send_data(const uint8_t *data, size_t len) {
  spi_transaction_t transaction = {
      .length = len * 8,
      .tx_buffer = data,
  };

  if (len == 0) {
    return;
  }

  gpio_set_level((gpio_num_t)PIN_NUM_DC, 1);
  ESP_ERROR_CHECK(spi_device_transmit(s_lcd_spi, &transaction));
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1,
                           uint16_t y1) {
  uint8_t data[4];

  x0 = (uint16_t)(x0 + TFT_X_OFFSET);
  x1 = (uint16_t)(x1 + TFT_X_OFFSET);
  y0 = (uint16_t)(y0 + TFT_Y_OFFSET);
  y1 = (uint16_t)(y1 + TFT_Y_OFFSET);

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

void lcd_present_framebuffer(void) {
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

void lcd_init(void) {
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

void fb_clear(uint16_t color) {
  if (color == 0) {
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    return;
  }

  for (size_t i = 0; i < sizeof(s_framebuffer) / sizeof(s_framebuffer[0]);
       ++i) {
    s_framebuffer[i] = color;
  }
}

void fb_draw_pixel(int x, int y, uint16_t color) {
  if (x < 0 || x >= TFT_WIDTH || y < 0 || y >= TFT_HEIGHT) {
    return;
  }
  s_framebuffer[y * TFT_WIDTH + x] = color;
}

void fb_fill_rect(int x, int y, int w, int h, uint16_t color) {
  int x0;
  int y0;
  int x1;
  int y1;
  int row_len;

  if (w <= 0 || h <= 0) {
    return;
  }

  x0 = clamp_int(x, 0, TFT_WIDTH);
  y0 = clamp_int(y, 0, TFT_HEIGHT);
  x1 = clamp_int(x + w, 0, TFT_WIDTH);
  y1 = clamp_int(y + h, 0, TFT_HEIGHT);
  row_len = x1 - x0;

  for (int yy = y0; yy < y1; ++yy) {
    uint16_t *row = &s_framebuffer[yy * TFT_WIDTH + x0];
    for (int xx = 0; xx < row_len; ++xx) {
      row[xx] = color;
    }
  }
}

void fb_draw_rect(int x, int y, int w, int h, uint16_t color) {
  if (w <= 1 || h <= 1) {
    return;
  }

  fb_fill_rect(x, y, w, 1, color);
  fb_fill_rect(x, y + h - 1, w, 1, color);
  fb_fill_rect(x, y, 1, h, color);
  fb_fill_rect(x + w - 1, y, 1, h, color);
}

void fb_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
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

void fb_fill_circle(int cx, int cy, int radius, uint16_t color) {
  int radius_sq = radius * radius;

  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      if ((x * x) + (y * y) <= radius_sq) {
        fb_draw_pixel(cx + x, cy + y, color);
      }
    }
  }
}

void fb_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
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

uint16_t *fb_data(void) { return s_framebuffer; }
