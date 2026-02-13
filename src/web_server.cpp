#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>

#include "utils.h"
#include "web_server.h"

static const char* TAG = "web_server";

// --- HTTP server context ---
static httpd_handle_t s_server = NULL;
static httpd_req_t* s_cur_req = NULL;

// --- Internal trampoline to call user handler (void func(void)) ---
static esp_err_t call_user_handler(httpd_req_t* req)
{
  s_cur_req = req;
  http_handler_fn fn = (http_handler_fn)req->user_ctx;
  if (fn != NULL)
  {
    (*fn)();
  }
  s_cur_req = NULL;
  return ESP_OK;
}

static void register_route(const char* uri, httpd_method_t method, http_handler_fn fn)
{
  if (s_server == NULL || uri == NULL || fn == NULL)
  {
    ESP_LOGW(TAG, "register_route: invalid state/args");
    return;
  }
  httpd_uri_t u = {};
  u.uri = uri;
  u.method = method;
  u.handler = call_user_handler;
  u.user_ctx = (void*)fn;
  CHECK_ERR(httpd_register_uri_handler(s_server, &u));
  ESP_LOGI(TAG, "Registered route: %s %s", (method == HTTP_GET ? "GET" : "POST"), uri);
}

extern "C" bool web_start()
{
  if (s_server != NULL)
    return true;

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 80;
  cfg.lru_purge_enable = true;
  cfg.stack_size = 12288;
  cfg.uri_match_fn = httpd_uri_match_wildcard;

  CHECK_ERR(httpd_start(&s_server, &cfg));
  if (s_server == NULL)
  {
    ESP_LOGE(TAG, "httpd_start failed (NULL server)");
    return false;
  }

  web_on_started();

  ESP_LOGI(TAG, "HTTP server started on port %d", cfg.server_port);
  return true;
}

extern "C" void web_stop(void)
{
  if (s_server != NULL)
  {
    httpd_stop(s_server);
    s_server = NULL;
  }
}

extern "C" bool web_is_running(void)
{
  return s_server != NULL;
}

void web_register_get(const char* uri, http_handler_fn handler)
{
  register_route(uri, HTTP_GET, handler);
}

void web_register_post(const char* uri, http_handler_fn handler)
{
  register_route(uri, HTTP_POST, handler);
}

void web_send(int code, const char* content_type, const char* body)
{
  if (s_cur_req == NULL)
  {
    ESP_LOGW(TAG, "web_send: no current request");
    return;
  }
  if (content_type != NULL && content_type[0] != '\0')
  {
    CHECK_ERR(httpd_resp_set_type(s_cur_req, content_type));
  }
  switch (code)
  {
  case 200:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "200 OK"));
    break;
  case 302:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "302 Found"));
    break;
  case 400:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "400 Bad Request"));
    break;
  case 401:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "401 Unauthorized"));
    break;
  case 404:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "404 Not Found"));
    break;
  case 500:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "500 Internal Server Error"));
    break;
  default:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "200 OK"));
    break;
  }
  const char* text = (body != NULL) ? body : "";
  CHECK_ERR(httpd_resp_send(s_cur_req, text, HTTPD_RESP_USE_STRLEN));
}

void web_send_binary(int code, const char* content_type, const void* data, size_t size)
{
  if (s_cur_req == NULL)
  {
    ESP_LOGW(TAG, "web_send_binary: no current request");
    return;
  }

  if (content_type != NULL && content_type[0] != '\0')
  {
    CHECK_ERR(httpd_resp_set_type(s_cur_req, content_type));
  }

  switch (code)
  {
  case 200:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "200 OK"));
    break;
  case 206:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "206 Partial Content"));
    break;
  case 302:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "302 Found"));
    break;
  case 400:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "400 Bad Request"));
    break;
  case 401:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "401 Unauthorized"));
    break;
  case 404:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "404 Not Found"));
    break;
  case 416:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "416 Range Not Satisfiable"));
    break;
  case 500:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "500 Internal Server Error"));
    break;
  default:
    CHECK_ERR(httpd_resp_set_status(s_cur_req, "200 OK"));
    break;
  }

  if (size > 0)
  {
    char len_buf[16];
    int n = snprintf(len_buf, sizeof(len_buf), "%u", (unsigned)size);
    if (n > 0)
    {
      CHECK_ERR(httpd_resp_set_hdr(s_cur_req, "Content-Length", len_buf));
    }
  }

  const char* payload = (const char*)data;
  CHECK_ERR(httpd_resp_send(s_cur_req, (payload != NULL) ? payload : "", (ssize_t)size));
}

int web_recv(void* buf, size_t maxlen)
{
  extern httpd_req_t* s_cur_req;
  if (s_cur_req == NULL || buf == NULL || maxlen == 0U)
  {
    return -1;
  }
  int r = httpd_req_recv(s_cur_req, (char*)buf, (ssize_t)maxlen);
  if (r == HTTPD_SOCK_ERR_TIMEOUT)
  {
    return -2; /* caller may retry */
  }
  return r;
}

size_t web_content_length()
{
  extern httpd_req_t* s_cur_req;
  if (s_cur_req == NULL)
  {
    return 0U;
  }
  if (s_cur_req->content_len <= 0)
  {
    return 0U;
  }
  return s_cur_req->content_len;
}

bool web_set_resp_header(const char* name, const char* value)
{
  extern httpd_req_t* s_cur_req;
  if (s_cur_req == NULL || name == NULL || value == NULL)
  {
    return false;
  }
  return httpd_resp_set_hdr(s_cur_req, name, value) == ESP_OK;
}
