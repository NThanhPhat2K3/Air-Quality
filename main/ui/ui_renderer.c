#include "ui_renderer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "display_hal.h"
#include "memory_photo_service.h"

#define DEG_TO_RAD 0.01745329252f

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
static const uint16_t COLOR_LIME_SOFT = RGB565(120, 235, 90);
static const uint16_t COLOR_OLIVE = RGB565(68, 95, 30);
static const uint16_t COLOR_QR_BG = RGB565(250, 252, 255);
static const uint16_t COLOR_QR_FG = RGB565(8, 18, 30);

#define QR_VERSION 3
#define QR_SIZE 29
#define QR_DATA_CODEWORDS 55
#define QR_EC_CODEWORDS 15
#define QR_ALIGNMENT_CENTER 22

static int clamp_int(int value, int min_value, int max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static void sanitize_display_text(const char *source, char *dest,
                                  size_t dest_size) {
  size_t write = 0;

  if (dest == NULL || dest_size == 0) {
    return;
  }
  if (source == NULL) {
    dest[0] = '\0';
    return;
  }

  while (source[0] != '\0' && write + 1 < dest_size) {
    unsigned char ch = (unsigned char)*source++;
    if (ch >= 'a' && ch <= 'z') {
      ch = (unsigned char)(ch - ('a' - 'A'));
    }
    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' ||
        ch == '-' || ch == '.' || ch == '/' || ch == ':') {
      dest[write++] = (char)ch;
    } else {
      dest[write++] = '-';
    }
  }
  dest[write] = '\0';
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

static void sanitize_url_text(const char *source, char *dest, size_t dest_size) {
  size_t write = 0;

  if (dest == NULL || dest_size == 0) {
    return;
  }
  if (source == NULL) {
    dest[0] = '\0';
    return;
  }

  while (source[0] != '\0' && write + 1 < dest_size) {
    unsigned char ch = (unsigned char)*source++;
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == ':' || ch == '/' || ch == '.' ||
        ch == '-' || ch == '_') {
      dest[write++] = (char)ch;
    }
  }
  dest[write] = '\0';
}

static void truncate_text_to_width(const char *source, char *dest,
                                   size_t dest_size, int max_width,
                                   int glyph_width) {
  size_t src_len;
  int max_chars;

  if (dest == NULL || dest_size == 0) {
    return;
  }
  if (source == NULL || max_width <= 0 || glyph_width <= 0) {
    dest[0] = '\0';
    return;
  }

  src_len = strlen(source);
  max_chars = max_width / glyph_width;
  if (max_chars <= 0) {
    dest[0] = '\0';
    return;
  }

  if ((int)src_len <= max_chars && src_len + 1 <= dest_size) {
    strlcpy(dest, source, dest_size);
    return;
  }

  if (max_chars <= 3) {
    size_t dots = (size_t)max_chars < dest_size - 1 ? (size_t)max_chars
                                                    : dest_size - 1;
    for (size_t i = 0; i < dots; ++i) {
      dest[i] = '.';
    }
    dest[dots] = '\0';
    return;
  }

  if ((size_t)max_chars >= dest_size) {
    max_chars = (int)dest_size - 1;
  }

  memcpy(dest, source, (size_t)(max_chars - 3));
  memcpy(dest + (max_chars - 3), "...", 4);
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

static void fb_draw_text5x7_centered(int center_x, int y, const char *text,
                                     uint16_t color, int scale) {
  fb_draw_text5x7(center_x - text5x7_width(text, scale) / 2, y, text, color,
                  scale);
}

static uint8_t gf_mul(uint8_t a, uint8_t b) {
  uint8_t result = 0;

  while (b != 0) {
    if (b & 1U) {
      result ^= a;
    }
    if (a & 0x80U) {
      a = (uint8_t)((a << 1) ^ 0x1DU);
    } else {
      a <<= 1;
    }
    b >>= 1;
  }
  return result;
}

static void build_qr_generator_poly(uint8_t *poly, int degree) {
  uint8_t next[QR_EC_CODEWORDS + 1] = {0};

  memset(poly, 0, (size_t)(degree + 1));
  poly[0] = 1;

  for (int i = 0; i < degree; ++i) {
    memset(next, 0, sizeof(next));
    for (int j = 0; j <= i; ++j) {
      uint8_t alpha = 1;
      for (int k = 0; k < i; ++k) {
        alpha = gf_mul(alpha, 2);
      }
      next[j] ^= poly[j];
      next[j + 1] ^= gf_mul(poly[j], alpha);
    }
    memcpy(poly, next, (size_t)(degree + 1));
  }
}

static bool build_qr_codewords(const char *text, uint8_t *codewords) {
  uint8_t bits[QR_DATA_CODEWORDS * 8] = {0};
  uint8_t data[QR_DATA_CODEWORDS] = {0};
  uint8_t ec[QR_EC_CODEWORDS] = {0};
  uint8_t generator[QR_EC_CODEWORDS + 1] = {0};
  size_t len;
  int bit_count = 0;

  if (text == NULL || codewords == NULL) {
    return false;
  }

  len = strlen(text);
  if (len > 53) {
    return false;
  }

#define PUSH_BITS(value, width)                                                 \
  do {                                                                          \
    for (int _bit = (width) - 1; _bit >= 0; --_bit) {                           \
      bits[bit_count++] = (uint8_t)(((value) >> _bit) & 1U);                   \
    }                                                                           \
  } while (0)

  PUSH_BITS(0x4, 4);
  PUSH_BITS((unsigned)len, 8);
  for (size_t i = 0; i < len; ++i) {
    PUSH_BITS((unsigned char)text[i], 8);
  }

  for (int i = 0; i < 4 && bit_count < (QR_DATA_CODEWORDS * 8); ++i) {
    bits[bit_count++] = 0;
  }
  while ((bit_count % 8) != 0) {
    bits[bit_count++] = 0;
  }

  for (int i = 0; i < bit_count / 8; ++i) {
    uint8_t byte = 0;
    for (int bit = 0; bit < 8; ++bit) {
      byte = (uint8_t)((byte << 1) | bits[i * 8 + bit]);
    }
    data[i] = byte;
  }
  for (int i = bit_count / 8; i < QR_DATA_CODEWORDS; ++i) {
    data[i] = (i % 2 == 0) ? 0xEC : 0x11;
  }

  build_qr_generator_poly(generator, QR_EC_CODEWORDS);
  for (int i = 0; i < QR_DATA_CODEWORDS; ++i) {
    uint8_t factor = (uint8_t)(data[i] ^ ec[0]);
    memmove(ec, ec + 1, (size_t)(QR_EC_CODEWORDS - 1));
    ec[QR_EC_CODEWORDS - 1] = 0;
    if (factor == 0) {
      continue;
    }
    for (int j = 0; j < QR_EC_CODEWORDS; ++j) {
      ec[j] ^= gf_mul(generator[j + 1], factor);
    }
  }

  memcpy(codewords, data, QR_DATA_CODEWORDS);
  memcpy(codewords + QR_DATA_CODEWORDS, ec, QR_EC_CODEWORDS);
#undef PUSH_BITS
  return true;
}

static void qr_set_module(bool modules[QR_SIZE][QR_SIZE],
                          bool assigned[QR_SIZE][QR_SIZE], int x, int y,
                          bool value) {
  modules[y][x] = value;
  assigned[y][x] = true;
}

static void qr_reserve_module(bool modules[QR_SIZE][QR_SIZE],
                              bool assigned[QR_SIZE][QR_SIZE], int x, int y,
                              bool value) {
  if (!assigned[y][x]) {
    modules[y][x] = value;
    assigned[y][x] = true;
  }
}

static void qr_draw_finder(bool modules[QR_SIZE][QR_SIZE],
                           bool assigned[QR_SIZE][QR_SIZE], int left, int top) {
  for (int y = -1; y <= 7; ++y) {
    for (int x = -1; x <= 7; ++x) {
      int xx = left + x;
      int yy = top + y;
      bool outer;
      bool border;
      bool inner;

      if (xx < 0 || yy < 0 || xx >= QR_SIZE || yy >= QR_SIZE) {
        continue;
      }

      outer = x >= 0 && x <= 6 && y >= 0 && y <= 6;
      border = x == 0 || x == 6 || y == 0 || y == 6;
      inner = x >= 2 && x <= 4 && y >= 2 && y <= 4;
      qr_set_module(modules, assigned, xx, yy, outer && (border || inner));
    }
  }
}

static void qr_draw_alignment(bool modules[QR_SIZE][QR_SIZE],
                              bool assigned[QR_SIZE][QR_SIZE], int center_x,
                              int center_y) {
  for (int y = -2; y <= 2; ++y) {
    for (int x = -2; x <= 2; ++x) {
      int distance = abs(x) > abs(y) ? abs(x) : abs(y);
      qr_set_module(modules, assigned, center_x + x, center_y + y,
                    distance != 1);
    }
  }
}

static bool build_qr_matrix(const char *text, bool modules[QR_SIZE][QR_SIZE]) {
  bool assigned[QR_SIZE][QR_SIZE] = {{false}};
  uint8_t codewords[QR_DATA_CODEWORDS + QR_EC_CODEWORDS] = {0};
  uint8_t bit_stream[(QR_DATA_CODEWORDS + QR_EC_CODEWORDS) * 8] = {0};
  int bit_index = 0;
  int direction = -1;
  int format_data = (0x1 << 3);
  int remainder;
  int format;

  memset(modules, 0, sizeof(bool) * QR_SIZE * QR_SIZE);
  if (!build_qr_codewords(text, codewords)) {
    return false;
  }

  qr_draw_finder(modules, assigned, 0, 0);
  qr_draw_finder(modules, assigned, QR_SIZE - 7, 0);
  qr_draw_finder(modules, assigned, 0, QR_SIZE - 7);
  qr_draw_alignment(modules, assigned, QR_ALIGNMENT_CENTER, QR_ALIGNMENT_CENTER);

  for (int i = 8; i < QR_SIZE - 8; ++i) {
    qr_set_module(modules, assigned, i, 6, (i % 2) == 0);
    qr_set_module(modules, assigned, 6, i, (i % 2) == 0);
  }
  qr_set_module(modules, assigned, 8, (4 * QR_VERSION) + 9, true);

  for (int i = 0; i < 9; ++i) {
    if (i != 6) {
      qr_reserve_module(modules, assigned, 8, i, false);
      qr_reserve_module(modules, assigned, i, 8, false);
    }
  }
  for (int i = 0; i < 8; ++i) {
    qr_reserve_module(modules, assigned, QR_SIZE - 1 - i, 8, false);
    qr_reserve_module(modules, assigned, 8, QR_SIZE - 1 - i, false);
  }

  for (int i = 0; i < (QR_DATA_CODEWORDS + QR_EC_CODEWORDS); ++i) {
    for (int bit = 7; bit >= 0; --bit) {
      bit_stream[(i * 8) + (7 - bit)] = (uint8_t)((codewords[i] >> bit) & 1U);
    }
  }

  bit_index = 0;
  for (int x = QR_SIZE - 1; x > 0; x -= 2) {
    if (x == 6) {
      --x;
    }
    for (int step = 0; step < QR_SIZE; ++step) {
      int y = direction == -1 ? (QR_SIZE - 1 - step) : step;
      for (int dx = 0; dx < 2; ++dx) {
        int xx = x - dx;
        bool value;

        if (assigned[y][xx]) {
          continue;
        }

        value = bit_stream[bit_index++] == 1U;
        if (((xx + y) % 2) == 0) {
          value = !value;
        }
        modules[y][xx] = value;
        assigned[y][xx] = true;
      }
    }
    direction *= -1;
  }

  remainder = format_data << 10;
  while (remainder >= (1 << 10)) {
    int shift = (int)floor(log2((double)remainder)) - 10;
    remainder ^= (0x537 << shift);
  }
  format = ((format_data << 10) | remainder) ^ 0x5412;

  for (int i = 0; i <= 5; ++i) {
    modules[i][8] = ((format >> i) & 1) != 0;
  }
  modules[7][8] = ((format >> 6) & 1) != 0;
  modules[8][8] = ((format >> 7) & 1) != 0;
  modules[8][7] = ((format >> 8) & 1) != 0;
  for (int i = 9; i < 15; ++i) {
    modules[8][14 - i] = ((format >> i) & 1) != 0;
  }

  for (int i = 0; i < 8; ++i) {
    modules[8][QR_SIZE - 1 - i] = ((format >> i) & 1) != 0;
  }
  for (int i = 8; i < 15; ++i) {
    modules[QR_SIZE - 15 + i][8] = ((format >> i) & 1) != 0;
  }

  return true;
}

static void draw_qr_block(int x, int y, int size, const char *text) {
  bool modules[QR_SIZE][QR_SIZE];
  const int quiet_zone = 4;
  const int full_modules = QR_SIZE + (quiet_zone * 2);
  int draw_size;
  int offset_x;
  int offset_y;

  fb_fill_rect(x, y, size, size, COLOR_QR_BG);

  if (text == NULL || text[0] == '\0' || !build_qr_matrix(text, modules)) {
    fb_draw_text5x7_centered(x + (size / 2), y + 18, "QR", COLOR_QR_FG, 1);
    fb_draw_text5x7_centered(x + (size / 2), y + 30, "WAIT", COLOR_MUTED, 1);
    return;
  }

  draw_size = size;
  offset_x = x + ((size - draw_size) / 2);
  offset_y = y + ((size - draw_size) / 2);

  for (int row = 0; row < QR_SIZE; ++row) {
    for (int col = 0; col < QR_SIZE; ++col) {
      int x0;
      int y0;
      int x1;
      int y1;

      if (!modules[row][col]) {
        continue;
      }

      x0 = offset_x + (((col + quiet_zone) * draw_size) / full_modules);
      y0 = offset_y + (((row + quiet_zone) * draw_size) / full_modules);
      x1 = offset_x + ((((col + quiet_zone) + 1) * draw_size) / full_modules);
      y1 = offset_y + ((((row + quiet_zone) + 1) * draw_size) / full_modules);
      fb_fill_rect(x0, y0, x1 - x0, y1 - y0, COLOR_QR_FG);
    }
  }
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

static void draw_panel(int x, int y, int w, int h, uint16_t accent) {
  fb_fill_rect(x, y, w, h, COLOR_PANEL);
  fb_draw_rect(x, y, w, h, COLOR_DIVIDER);
  fb_fill_rect(x + 1, y + 1, w - 2, 2, accent);
  fb_fill_rect(x + 1, y + h - 3, w - 2, 2, COLOR_PANEL_ALT);
}

static void draw_hybrid_metric_card(int x, int w, const char *label,
                                    const char *value, const char *unit,
                                    bool show_degree_symbol,
                                    uint16_t value_color) {
  const int y = 95;
  const int h = 29;
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

static void draw_hybrid_overlay(const dashboard_state_t *state,
                                bool wifi_connected,
                                bool provisioning_portal_active) {
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
  const int time_right_x = time_x + text5x7_width(time_text, 1);
  const int date_right_x = 153;
  const int date_x = date_right_x - text5x7_width(date_text, 1);
  const int wifi_cx = time_right_x + ((date_x - time_right_x) / 2);
  const int wifi_cy = 5;

  fb_clear(COLOR_BG);
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
  if (provisioning_portal_active) {
    fb_fill_rect(wifi_cx - 11, wifi_cy + 1, 2, 6, COLOR_CYAN);
    fb_fill_rect(wifi_cx - 11, wifi_cy + 9, 2, 2, COLOR_CYAN);
  }

  {
    const int px = 4, py = 23, pw = 58, ph = 69;
    const int cx = px + pw / 2;
    draw_panel(px, py, pw, ph, aqi_col);
    fb_draw_text5x7_centered(cx, py + 7, "AQI", COLOR_MUTED, 1);
    fb_draw_text_shadow(cx - text_width(aqi_text, 4) / 2, py + 24, aqi_text,
                        aqi_col, glow, 4);
    fb_draw_text5x7_centered(cx, py + 54, "INDEX", COLOR_MUTED, 1);
  }

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

    int active_index = clamp_int(state->aqi - 1, 0, 4);
    int arrow_x = bar_x + active_index * (seg_w + seg_gap) + (seg_w / 2);
    int arrow_y = bar_y - 1;
    fb_fill_triangle(arrow_x, arrow_y, arrow_x - 2, arrow_y - 3, arrow_x + 2,
                     arrow_y - 3, aqi_col);
  }

  draw_hybrid_metric_card(4, 52, "ECO2", eco2_text, "PPM", false,
                          COLOR_YELLOW);
  draw_hybrid_metric_card(60, 52, "TEMP", temp_text, "C", true, COLOR_YELLOW);
  draw_hybrid_metric_card(116, 40, "HUMI", hum_text, "%", false, COLOR_CYAN);
}

static void draw_local_header(const char *title, const char *subtitle) {
  fb_clear(COLOR_BG);
  fb_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, RGB565(0, 0, 0));
  fb_fill_rect(5, 5, 150, 18, RGB565(3, 10, 18));
  fb_draw_rect(5, 5, 150, 18, RGB565(24, 78, 126));
  fb_fill_rect(6, 6, 46, 2, COLOR_CYAN);
  fb_draw_text5x7(10, 10, title, COLOR_WHITE, 1);
  fb_draw_text5x7(10, 26, subtitle, COLOR_MUTED, 1);
}

static void draw_wifi_action_menu(const local_menu_state_t *menu,
                                  const connectivity_ui_status_t *wifi_status) {
  static const char *kActions[] = {
      "OPEN PORTAL", "DISCONNECT", "STOP PORTAL", "FORGET WIFI", "HOME"};
  int panel_x = 14;
  int panel_y = 30;
  int panel_w = 132;
  int panel_h = 82;
  uint16_t accent = wifi_status->provisioning_portal_active
                        ? COLOR_CYAN
                        : (wifi_status->connected ? COLOR_LIME : COLOR_ORANGE);

  if (!menu->wifi_actions_visible) {
    return;
  }

  fb_fill_rect(panel_x, panel_y, panel_w, panel_h, RGB565(4, 10, 18));
  fb_draw_rect(panel_x, panel_y, panel_w, panel_h, RGB565(28, 74, 116));
  fb_draw_rect(panel_x + 2, panel_y + 2, panel_w - 4, panel_h - 4,
               RGB565(8, 26, 42));
  fb_fill_rect(panel_x + 8, panel_y + 8, 40, 2, accent);
  fb_draw_text5x7(panel_x + 10, panel_y + 14, "WIFI ACTIONS", COLOR_WHITE, 1);

  for (int i = 0; i < 5; ++i) {
    int row_y = panel_y + 24 + (i * 11);
    bool selected = (i == menu->wifi_action_selected);
    if (selected) {
      fb_fill_rect(panel_x + 8, row_y - 1, panel_w - 16, 10, RGB565(8, 24, 38));
      fb_draw_rect(panel_x + 8, row_y - 1, panel_w - 16, 10, accent);
      fb_fill_rect(panel_x + 10, row_y + 2, 3, 3, accent);
    }
    fb_draw_text5x7(panel_x + 18, row_y + 1, kActions[i],
                    selected ? COLOR_WHITE : COLOR_MUTED, 1);
  }
}

static void draw_wifi_screen(const connectivity_ui_status_t *wifi_status,
                             const local_menu_state_t *menu) {
  char ssid_text[40];
  char ip_text[20];
  char ssid_short[40];
  char url_text[48];
  const bool portal_active = wifi_status->provisioning_portal_active;
  const bool can_open_qr = portal_active || wifi_status->connected;
  const uint16_t accent = portal_active
                              ? COLOR_CYAN
                              : (wifi_status->connected ? COLOR_LIME : COLOR_ORANGE);

  draw_local_header("WIFI ACCESS", "");
  fb_fill_rect(104, 8, 44, 10, RGB565(8, 18, 28));
  fb_draw_rect(104, 8, 44, 10, accent);
  fb_fill_circle(109, 13, 1, accent);
  fb_draw_text(114, 11, portal_active ? "PORTAL" : "RUNTIME", accent, 1);
  sanitize_display_text(wifi_status->ssid, ssid_text, sizeof(ssid_text));
  sanitize_display_text(wifi_status->runtime_ip, ip_text, sizeof(ip_text));
  truncate_text_to_width(ssid_text, ssid_short, sizeof(ssid_short), 148, 4);
  if (!can_open_qr) {
    url_text[0] = '\0';
  } else if (strcmp(ip_text, "NO IP") == 0) {
    sanitize_url_text("http://aqnode.local/", url_text, sizeof(url_text));
  } else {
    snprintf(url_text, sizeof(url_text), "http://%s/", wifi_status->runtime_ip);
    sanitize_url_text(url_text, url_text, sizeof(url_text));
  }

  draw_qr_block(42, 33, 76, url_text);

  fb_draw_text5x7_centered(80, 121, ssid_short, RGB565(230, 238, 246), 1);
  draw_wifi_action_menu(menu, wifi_status);
}

static void draw_alarm_screen(void) {
  draw_local_header("ALARM PANEL", "SAFE STUB");
  draw_panel(6, 34, 148, 28, COLOR_ORANGE);
  fb_draw_text5x7(12, 40, "ALARM 1", COLOR_MUTED, 1);
  fb_draw_text5x7(88, 40, "06:30", COLOR_WHITE, 1);
  fb_draw_text5x7(12, 50, "MODE DAILY", COLOR_CYAN, 1);
  draw_panel(6, 67, 148, 24, COLOR_CYAN);
  fb_draw_text5x7(12, 73, "ALARM 2", COLOR_MUTED, 1);
  fb_draw_text5x7(88, 73, "21:00", COLOR_WHITE, 1);
  fb_draw_text5x7(12, 82, "MODE IDLE", COLOR_MUTED, 1);
  draw_panel(6, 96, 148, 27, COLOR_LIME);
  fb_draw_text5x7(12, 102, "NEXT STEP", COLOR_MUTED, 1);
  fb_draw_text5x7(12, 112, "SYNC WITH REAL RTC", COLOR_WHITE, 1);
}

static void draw_game_screen(void) {
  draw_local_header("GAME HUB", "PLACEHOLDER");
  draw_panel(6, 34, 148, 26, COLOR_CYAN);
  fb_draw_text5x7(12, 40, "NEXT STEP", COLOR_MUTED, 1);
  fb_draw_text5x7(78, 40, "MINI GAME", COLOR_WHITE, 1);
  fb_draw_text5x7(12, 50, "SNAKE / TAP / REACT", COLOR_CYAN, 1);
  draw_panel(6, 65, 148, 24, COLOR_ORANGE);
  fb_draw_text5x7(12, 71, "INPUT", COLOR_MUTED, 1);
  fb_draw_text5x7(60, 71, "USE BUTTON EVENT", COLOR_WHITE, 1);
  draw_panel(6, 94, 148, 29, COLOR_LIME);
  fb_draw_text5x7(12, 100, "STATUS", COLOR_MUTED, 1);
  fb_draw_text5x7(12, 110, "RESERVED FOR GAME LOOP", COLOR_WHITE, 1);
  fb_draw_text5x7(12, 118, "SAFE STUB FOR NOW", COLOR_CYAN, 1);
}

static void draw_memory_screen(void) {
  if (memory_photo_service_snapshot(fb_data(), TFT_WIDTH * TFT_HEIGHT)) {
    return;
  }

  draw_local_header("MEMORY VIEW", "UPLOAD PHOTO");
  draw_panel(8, 35, 144, 40, COLOR_CYAN);
  fb_draw_text5x7(16, 44, "NO MEMORY PHOTO", COLOR_WHITE, 1);
  fb_draw_text5x7(16, 56, "OPEN WEB TAB TO", COLOR_MUTED, 1);
  fb_draw_text5x7(16, 66, "LOAD RGB565 IMAGE", COLOR_CYAN, 1);
  draw_panel(8, 82, 144, 38, COLOR_LIME);
  fb_draw_text5x7(16, 91, "TIP", COLOR_MUTED, 1);
  fb_draw_text5x7(16, 103, "WEB WILL SCALE IT", COLOR_WHITE, 1);
  fb_draw_text5x7(16, 113, "TO 160X128 FOR YOU", COLOR_LIME, 1);
}

static void draw_menu_overlay(const local_menu_state_t *menu) {
  static const char *kMenuItems[LOCAL_SCREEN_COUNT] = {
      "MONITOR", "WIFI CONFIG", "ALARM", "GAME", "MEMORY"};
  int progress_q8 = clamp_int(menu->overlay_progress_q8, 0, 256);
  int y_offset = -(((256 - progress_q8) * (TFT_HEIGHT + 8)) / 256);
  int highlight_y = menu->highlight_y_q8 >> 8;
  int glow_y = highlight_y - 2;
  int pulse = 6 + ((menu->pulse_phase >> 5) & 0x07);
  int indicator_y = highlight_y + 5;

  if (progress_q8 <= 0) {
    return;
  }

  fb_fill_rect(0, y_offset, TFT_WIDTH, TFT_HEIGHT, RGB565(0, 0, 0));
  fb_draw_rect(0, y_offset, TFT_WIDTH, TFT_HEIGHT, RGB565(14, 44, 74));
  fb_draw_rect(3, y_offset + 3, TFT_WIDTH - 6, TFT_HEIGHT - 6,
               RGB565(8, 26, 42));
  fb_fill_rect(12, y_offset + 12, 54, 2, COLOR_CYAN);
  fb_draw_text5x7(14, y_offset + 20, "MAIN MENU", COLOR_WHITE, 2);
  fb_fill_rect(16, y_offset + glow_y, 128, 20, RGB565(2, 10, 18));
  fb_fill_rect(17, y_offset + highlight_y - 1, 126, 18, RGB565(3, 14, 24));
  fb_fill_rect(18, y_offset + highlight_y, 124, 16, RGB565(4, 18, 30));
  fb_draw_rect(18, y_offset + highlight_y, 124, 16, COLOR_CYAN);
  fb_draw_rect(19, y_offset + highlight_y + 1, 122, 14, RGB565(24, 130, 196));
  fb_fill_rect(18, y_offset + highlight_y, pulse, 16, COLOR_CYAN);
  fb_fill_rect(28, y_offset + indicator_y, 5, 5, COLOR_LIME);

  for (int i = 0; i < LOCAL_SCREEN_COUNT; ++i) {
    int row_y = 36 + (i * 18);
    int distance = abs((row_y << 8) - menu->highlight_y_q8) >> 8;
    int x_nudge = clamp_int((16 - distance) / 2, 0, 6);
    uint16_t text_color = (distance <= 6) ? COLOR_WHITE : COLOR_MUTED;
    fb_draw_text5x7(42 + x_nudge, y_offset + row_y + 4, kMenuItems[i],
                    text_color, 1);
  }
}

void ui_renderer_draw_local_screen(const dashboard_state_t *state,
                                   const local_menu_state_t *menu,
                                   const connectivity_ui_status_t *wifi_status) {
  switch (menu->active_screen) {
  case LOCAL_SCREEN_MONITOR:
    draw_hybrid_overlay(state, wifi_status->connected,
                        wifi_status->provisioning_portal_active);
    break;
  case LOCAL_SCREEN_WIFI:
    draw_wifi_screen(wifi_status, menu);
    break;
  case LOCAL_SCREEN_ALARM:
    draw_alarm_screen();
    break;
  case LOCAL_SCREEN_GAME:
    draw_game_screen();
    break;
  case LOCAL_SCREEN_MEMORY:
    draw_memory_screen();
    break;
  default:
    draw_hybrid_overlay(state, wifi_status->connected,
                        wifi_status->provisioning_portal_active);
    break;
  }

  if (menu->visible || menu->overlay_progress_q8 > 0) {
    draw_menu_overlay(menu);
  }
}

void ui_renderer_draw_boot_screen(int percent, const char *status) {
  char percent_text[12];
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

void ui_renderer_draw_dashboard(const dashboard_state_t *state,
                                bool wifi_connected,
                                bool provisioning_portal_active) {
  char time_text[16];
  char date_text[16];
  char co2_text[16];
  char temp_text[16];
  char hum_text[16];
  const char *headline = aqi_label(state->aqi);
  const char *sub1 = NULL;
  const char *sub2 = NULL;
  int headline_x;
  int sub1_x;
  int sub2_x;
  const char *day_name[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

  snprintf(time_text, sizeof(time_text), "%02d:%02d:%02d", state->clock.tm_hour,
           state->clock.tm_min, state->clock.tm_sec);
  snprintf(date_text, sizeof(date_text), "%s %02d-%02d",
           day_name[state->clock.tm_wday], state->clock.tm_mon + 1,
           state->clock.tm_mday);
  snprintf(co2_text, sizeof(co2_text), "%d", state->eco2_ppm);
  snprintf(temp_text, sizeof(temp_text), "%d.%d", state->temp_tenths_c / 10,
           abs(state->temp_tenths_c % 10));
  snprintf(hum_text, sizeof(hum_text), "%d", state->humidity_pct);
  aqi_subtext(state->aqi, &sub1, &sub2);
  headline_x = 118 - text_width(headline, 3) / 2;
  sub1_x = 118 - text_width(sub1, 2) / 2;
  sub2_x = 118 - text_width(sub2, 2) / 2;

  draw_starry_background();
  draw_panel(5, 5, 150, 27, COLOR_CYAN);
  draw_panel(5, 36, 72, 58, aqi_color(state->aqi));
  draw_panel(82, 36, 73, 58, COLOR_LIME);
  fb_draw_text(10, 9, time_text, COLOR_WHITE, 3);
  fb_draw_text(10, 21, date_text, COLOR_MUTED, 2);
  if (wifi_connected) {
    draw_wifi_icon(141, 11, COLOR_LIME);
  } else {
    draw_wifi_offline_icon(141, 11, COLOR_MUTED, COLOR_RED);
  }
  if (provisioning_portal_active) {
    fb_fill_rect(130, 12, 2, 6, COLOR_CYAN);
    fb_fill_rect(130, 20, 2, 2, COLOR_CYAN);
  }
  draw_aqi_gauge(state, 41, 66);
  fb_draw_text(headline_x, 44, headline, aqi_color(state->aqi), 3);
  fb_draw_text(sub1_x, 63, sub1, COLOR_WHITE, 2);
  fb_draw_text(sub2_x, 74, sub2, COLOR_MUTED, 2);
  draw_aqi_scale_bar(89, 85);
  draw_panel(5, 99, 46, 24, COLOR_LIME);
  draw_panel(57, 99, 46, 24, COLOR_LIME_SOFT);
  draw_panel(109, 99, 46, 24, COLOR_CYAN);
  fb_draw_text(13, 104, "CO2", COLOR_MUTED, 1);
  fb_draw_text(65, 104, "TEMP", COLOR_MUTED, 1);
  fb_draw_text(118, 104, "HUM", COLOR_MUTED, 1);
  fb_draw_text(12, 111, co2_text, COLOR_YELLOW, 2);
  fb_draw_text(61, 111, temp_text, COLOR_YELLOW, 2);
  fb_draw_text(119, 111, hum_text, COLOR_CYAN, 2);
}
