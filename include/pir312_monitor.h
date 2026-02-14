#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pir312_init();
void pir312_dump_status();

#ifdef __cplusplus
}
#endif

bool pir312_get_state(int index);
int pir312_count();

void web_ui_pir312_on_started();
