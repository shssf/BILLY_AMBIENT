#include <cstdio>

#include "pir312_monitor.h"
#include "web_server.h"

extern const uint8_t html_start[] asm("_binary_pir312_page_html_start");
extern const uint8_t html_end[] asm("_binary_pir312_page_html_end");

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
  len += snprintf(buf + len, sizeof(buf) - len, "]");

  len += snprintf(buf + len,
                  sizeof(buf) - len,
                  ",\"ambient\":%u"
                  ",\"box_left\":%u"
                  ",\"box_left_center\":%u"
                  ",\"box_right_center\":%u"
                  ",\"box_right\":%u",
                  (unsigned)pir312_get_ambient(),
                  (unsigned)pir312_get_box_left(),
                  (unsigned)pir312_get_box_left_center(),
                  (unsigned)pir312_get_box_right_center(),
                  (unsigned)pir312_get_box_right());

  len += snprintf(buf + len, sizeof(buf) - len, "}");

  web_send(200, "application/json; charset=utf-8", buf);
}

static void pir312_page()
{
  const size_t size = html_end - html_start;
  web_send_binary(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(html_start), size);
}

void web_ui_pir312_on_started(void)
{
  web_register_get("/pir312", pir312_page);
  //web_register_get("/pir312/", pir312_page);
  web_register_get("/pir312/status", pir312_status_api);
}