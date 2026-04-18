#ifndef MEMORY_PHOTO_SERVICE_H
#define MEMORY_PHOTO_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "display_hal.h"

#define MEMORY_PHOTO_NAMESPACE "memory_photo"
#define MEMORY_PHOTO_KEY_DATA "data"
#define MEMORY_PHOTO_PIXEL_BYTES (TFT_WIDTH * TFT_HEIGHT * 2)

bool memory_photo_service_restore_from_nvs(void);
bool memory_photo_service_save_blob(const uint8_t *data, size_t data_len);
bool memory_photo_service_delete_blob(void);
bool memory_photo_service_snapshot(uint16_t *dest, size_t pixel_count);

#endif
