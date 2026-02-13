#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool web_start(void);
void web_stop(void);
bool web_is_running(void);

#ifdef __cplusplus
}
#endif

typedef void (*http_handler_fn)(void);

void web_on_started(void);
void web_register_get(const char* uri, http_handler_fn handler);
void web_register_post(const char* uri, http_handler_fn handler);
void web_send(int code, const char* content_type, const char* body);
void web_send_binary(int code, const char* content_type, const void* data, size_t size);
int web_recv(void* buf, size_t maxlen);
size_t web_content_length();
bool web_set_resp_header(const char* name, const char* value);
