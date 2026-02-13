#pragma once

#include <esp_err.h>
#include <esp_log.h>

#define CHECK_ERR(expr)                                                                                                                    \
  do                                                                                                                                       \
  {                                                                                                                                        \
    esp_err_t err = (expr);                                                                                                                \
    if (err != ESP_OK)                                                                                                                     \
    {                                                                                                                                      \
      ESP_LOGE(TAG, "ERROR: %s failed: %s (%d) in %s", #expr, esp_err_to_name(err), err, __FUNCTION__);                                  \
    }                                                                                                                                      \
  } while (0)

#define CHECK_XTASK_OK(expr)                                                                                                               \
  do                                                                                                                                       \
  {                                                                                                                                        \
    BaseType_t _st = (expr);                                                                                                               \
    if (_st != pdPASS)                                                                                                                     \
    {                                                                                                                                      \
      ESP_LOGE(TAG, #expr " failed: BaseType_t=%ld", (long)_st);                                                                           \
    }                                                                                                                                      \
  } while (0)
  