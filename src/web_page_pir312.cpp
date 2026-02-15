#include <cstdio>

#include "light_sensor_support.h"
#include "pir312_monitor.h"
#include "web_server.h"

static void pir312_status_api()
{
  char buf[256];
  size_t len = 0;

  len += snprintf(buf + len, sizeof(buf) - len, "{");

  len += snprintf(buf + len, sizeof(buf) - len, "\"sensors\":[");
  for (int i = 0; i < pir312_count(); ++i)
  {
    const int st = pir312_get_state(i);
    len += snprintf(buf + len, sizeof(buf) - len, "%s%d", (i > 0) ? "," : "", st);
  }
  len += snprintf(buf + len, sizeof(buf) - len, "], \"light_raw\":%d, \"light\":%d", light_sensor_get_value(), light_sensor_is_light());

  len += snprintf(buf + len, sizeof(buf) - len, "}");

  web_send(200, "application/json; charset=utf-8", buf);
}

static void pir312_page()
{
  extern const uint8_t html_pir312_start[] asm("_binary_pir312_page_html_start");
  extern const uint8_t html_pir312_end[] asm("_binary_pir312_page_html_end");

  const size_t size = html_pir312_end - html_pir312_start;
  web_send_binary(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(html_pir312_start), size);
}

void pir312_register_web_route_handlers()
{
  web_register_get("/pir312", pir312_page);
  web_register_get("/pir312/status", pir312_status_api);
}
