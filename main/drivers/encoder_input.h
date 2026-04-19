#ifndef ENCODER_INPUT_H
#define ENCODER_INPUT_H

#include <stdbool.h>

typedef struct {
  int steps;
  bool button_pressed;
} encoder_input_event_t;

void encoder_input_init(void);
bool encoder_input_poll(encoder_input_event_t *event);

#endif
