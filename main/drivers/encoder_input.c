#include "encoder_input.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

/*
 * Encoder wiring: KY-040 uses CLK/DT/SW, while many EC11 modules expose the
 * same signals as A/B/KO (or PUSH). Change only the pin macros if needed.
 */
#define ENCODER_PIN_CLK 25
#define ENCODER_PIN_DT 26
#define ENCODER_PIN_SW 27
#define ENCODER_DIRECTION_SIGN 1

#define ENCODER_PROFILE_LEGACY_MODULE 0
#define ENCODER_PROFILE_EC11_MODULE 1

/*
 * Pick the decoder profile here:
 * - ENCODER_PROFILE_EC11_MODULE: balanced for EC11 modules
 * - ENCODER_PROFILE_LEGACY_MODULE: more responsive for older modules
 */
#define ENCODER_INPUT_PROFILE ENCODER_PROFILE_EC11_MODULE

#if ENCODER_INPUT_PROFILE == ENCODER_PROFILE_LEGACY_MODULE
#define ENCODER_STEP_LATCH 2
#define ENCODER_MAX_STEPS_PER_POLL 2
#elif ENCODER_INPUT_PROFILE == ENCODER_PROFILE_EC11_MODULE
/*
 * Many EC11 modules expose only two stable transitions per detent, so using a
 * full 4-edge latch can make end-of-menu items feel unreachable.
 */
#define ENCODER_STEP_LATCH 2
#define ENCODER_MAX_STEPS_PER_POLL 1
#else
#error "Unsupported ENCODER_INPUT_PROFILE"
#endif

#define ENCODER_BUTTON_DEBOUNCE_POLLS 2

typedef struct {
  volatile int pending_steps;
  int8_t transition_accumulator;
  uint8_t last_ab_state;
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

static inline uint8_t encoder_read_ab_state(void) {
  return (uint8_t)((encoder_read_level(ENCODER_PIN_CLK) << 1) |
                   encoder_read_level(ENCODER_PIN_DT));
}

static inline int8_t IRAM_ATTR encoder_transition_delta(uint8_t previous_state,
                                                        uint8_t current_state) {
  switch ((previous_state << 2) | current_state) {
  case 0b0001:
  case 0b0111:
  case 0b1110:
  case 0b1000:
    return 1;
  case 0b0010:
  case 0b1011:
  case 0b1101:
  case 0b0100:
    return -1;
  default:
    return 0;
  }
}

static void IRAM_ATTR encoder_ab_isr(void *arg) {
  uint8_t current_state;
  int8_t delta;

  (void)arg;

  current_state = encoder_read_ab_state();

  portENTER_CRITICAL_ISR(&s_encoder_lock);
  delta = encoder_transition_delta(s_encoder_state.last_ab_state, current_state);
  if (delta != 0) {
    s_encoder_state.transition_accumulator += delta;
    s_encoder_state.last_ab_state = current_state;

    if (s_encoder_state.transition_accumulator >= ENCODER_STEP_LATCH) {
      s_encoder_state.pending_steps += ENCODER_DIRECTION_SIGN;
      s_encoder_state.transition_accumulator -= ENCODER_STEP_LATCH;
    } else if (s_encoder_state.transition_accumulator <= -ENCODER_STEP_LATCH) {
      s_encoder_state.pending_steps -= ENCODER_DIRECTION_SIGN;
      s_encoder_state.transition_accumulator += ENCODER_STEP_LATCH;
    }
  } else {
    s_encoder_state.last_ab_state = current_state;
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

  s_encoder_state.last_ab_state = encoder_read_ab_state();
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
      gpio_set_intr_type((gpio_num_t)ENCODER_PIN_DT, GPIO_INTR_ANYEDGE));
  ESP_ERROR_CHECK(
      gpio_isr_handler_add((gpio_num_t)ENCODER_PIN_CLK, encoder_ab_isr, NULL));
  ESP_ERROR_CHECK(
      gpio_isr_handler_add((gpio_num_t)ENCODER_PIN_DT, encoder_ab_isr, NULL));
}

bool encoder_input_poll(encoder_input_event_t *event) {
  int raw_button_level;

  if (event == NULL) {
    return false;
  }

  event->steps = 0;
  event->button_pressed = false;

  portENTER_CRITICAL(&s_encoder_lock);
  if (s_encoder_state.pending_steps > ENCODER_MAX_STEPS_PER_POLL) {
    event->steps = ENCODER_MAX_STEPS_PER_POLL;
    s_encoder_state.pending_steps -= ENCODER_MAX_STEPS_PER_POLL;
  } else if (s_encoder_state.pending_steps < -ENCODER_MAX_STEPS_PER_POLL) {
    event->steps = -ENCODER_MAX_STEPS_PER_POLL;
    s_encoder_state.pending_steps += ENCODER_MAX_STEPS_PER_POLL;
  } else {
    event->steps = s_encoder_state.pending_steps;
    s_encoder_state.pending_steps = 0;
  }
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
