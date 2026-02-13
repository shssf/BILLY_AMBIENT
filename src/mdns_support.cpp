#include <esp_log.h>
#include <mdns.h>

#include "mdns_support.h"
#include "utils.h" // for CHECK_ERR

static const char* TAG = "mdns_support";
static bool s_mdns_running = false;

int mdns_start(const char* hostname, const char* instance)
{
  if (s_mdns_running)
  {
    return 0;
  }

  esp_err_t err = mdns_init();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "mdns_init failed: %s (%d)", esp_err_to_name(err), (int)err);
    return -1;
  }
  s_mdns_running = true;

  if (hostname && *hostname)
  {
    CHECK_ERR(mdns_hostname_set(hostname));
  }
  if (instance && *instance)
  {
    CHECK_ERR(mdns_instance_name_set(instance));
  }

  CHECK_ERR(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
  ESP_LOGI(TAG, "mDNS started: host=%s.local, instance=%s", hostname, instance);
  return 0;
}

void mdns_stop(void)
{
  if (s_mdns_running)
  {
    mdns_free();
    s_mdns_running = false;
    ESP_LOGI(TAG, "mDNS stopped");
  }
}
