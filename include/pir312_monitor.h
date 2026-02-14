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

/* Flag getters used by other modules (C ABI) */
uint64_t pir312_get_ambient(void);
uint64_t pir312_get_box_left(void);
uint64_t pir312_get_box_left_center(void);
uint64_t pir312_get_box_right_center(void);
uint64_t pir312_get_box_right(void);

int pir312_get_state(int index);
int pir312_count(void);

/* Provided by web_ui_page_pir312.cpp (C++ linkage) */
void web_ui_pir312_on_started(void);
