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

void pir312_register_web_route_handlers();
