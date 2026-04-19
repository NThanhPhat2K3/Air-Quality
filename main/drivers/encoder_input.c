#include "encoder_input.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

/* KY-040: CLK, DT, SW. Change these three pins if your wiring differs. */
#define ENCODER_PIN_CLK 25
#define ENCODER_PIN_DT 26
#define ENCODER_PIN_SW 27
#define ENCODER_DIRECTION_SIGN 1

#define ENCODER_BUTTON_DEBOUNCE_POLLS 2

typedef struct {
  volatile int pending_steps;
  int last_clk_level;
  int raw_button_level;
  int stable_button_level;
  uint8_t button_stable_polls;
  bool pending_button_press;
} encoder_input_state_t;

static encoder_input_state_t s_encoder_state;
static portMUX_TYPE s_encoder_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_encoder_isr_service_ready;

static inline int encoder_read_level(int pin) {
  return gpio_get_level((gpio_num_t)pin);
}

static void IRAM_ATTR encoder_clk_isr(void *arg) {
  int clk_level;
  int dt_level;

  (void)arg;

  clk_level = gpio_get_level((gpio_num_t)ENCODER_PIN_CLK);
  dt_level = gpio_get_level((gpio_num_t)ENCODER_PIN_DT);

  portENTER_CRITICAL_ISR(&s_encoder_lock);
  if (clk_level != s_encoder_state.last_clk_level) {
    s_encoder_state.last_clk_level = clk_level;
    if (clk_level == 0) {
      s_encoder_state.pending_steps +=
          (dt_level != 0) ? ENCODER_DIRECTION_SIGN : -ENCODER_DIRECTION_SIGN;
    }
  }
  portEXIT_CRITICAL_ISR(&s_encoder_lock);
}

void encoder_input_init(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << ENCODER_PIN_CLK) | (1ULL << ENCODER_PIN_DT) |
                      (1ULL << ENCODER_PIN_SW),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  memset(&s_encoder_state, 0, sizeof(s_encoder_state));
  ESP_ERROR_CHECK(gpio_config(&io_conf));

  s_encoder_state.last_clk_level = encoder_read_level(ENCODER_PIN_CLK);
  s_encoder_state.raw_button_level = encoder_read_level(ENCODER_PIN_SW);
  s_encoder_state.stable_button_level = s_encoder_state.raw_button_level;

  if (!s_encoder_isr_service_ready) {
    esp_err_t install_err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (install_err == ESP_OK || install_err == ESP_ERR_INVALID_STATE) {
      s_encoder_isr_service_ready = true;
    } else {
      ESP_ERROR_CHECK(install_err);
    }
  }

  ESP_ERROR_CHECK(gpio_set_intr_type((gpio_num_t)ENCODER_PIN_CLK,
                                     GPIO_INTR_ANYEDGE));
  ESP_ERROR_CHECK(
      gpio_isr_handler_add((gpio_num_t)ENCODER_PIN_CLK, encoder_clk_isr, NULL));
}

bool encoder_input_poll(encoder_input_event_t *event) {
  int raw_button_level;

  if (event == NULL) {
    return false;
  }

  event->steps = 0;
  event->button_pressed = false;

  portENTER_CRITICAL(&s_encoder_lock);
  event->steps = s_encoder_state.pending_steps;
  s_encoder_state.pending_steps = 0;
  portEXIT_CRITICAL(&s_encoder_lock);

  raw_button_level = encoder_read_level(ENCODER_PIN_SW);
  if (raw_button_level == s_encoder_state.raw_button_level) {
    if (s_encoder_state.button_stable_polls < ENCODER_BUTTON_DEBOUNCE_POLLS) {
      s_encoder_state.button_stable_polls++;
    }
  } else {
    s_encoder_state.raw_button_level = raw_button_level;
    s_encoder_state.button_stable_polls = 0;
  }

  if (s_encoder_state.button_stable_polls >= ENCODER_BUTTON_DEBOUNCE_POLLS &&
      s_encoder_state.stable_button_level != raw_button_level) {
    s_encoder_state.stable_button_level = raw_button_level;
    if (raw_button_level == 0) {
      s_encoder_state.pending_button_press = true;
    }
  }

  if (s_encoder_state.pending_button_press) {
    event->button_pressed = true;
    s_encoder_state.pending_button_press = false;
  }

  return event->steps != 0 || event->button_pressed;
}
