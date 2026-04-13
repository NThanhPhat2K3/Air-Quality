#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t captive_dns_start(void);
esp_err_t captive_dns_stop(void);

#ifdef __cplusplus
}
#endif
