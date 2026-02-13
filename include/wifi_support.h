#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool wifi_start();

bool wifi_is_connected();

const char* wifi_get_hostname();
void wifi_set_hostname(const char* name);

const char* wifi_get_ap_password();

void wifi_reset();

#ifdef __cplusplus
} /* extern "C" */
#endif


