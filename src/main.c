//#include <esp_event.h>
//#include <esp_log.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_provisioning/manager.h>

#include "light_sensor_support.h"
#include "mdns_support.h"
#include "pir312_monitor.h"
#include "utils.h"
#include "web_server.h"
#include "wifi_support.h"
#include "ws2812b_support.h"

static const char* TAG = "main";
static bool s_services_started = false;

static void async_wifi_handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && !s_services_started)
  {
    wifi_prov_mgr_deinit();
    mdns_start(wifi_get_hostname(), "ESP32 Device");
    if (!web_is_running())
    {
      web_start();
    }
    s_services_started = true;
  }
}

static void connect_monitor_task(void* arg)
{
  for (;;)
  {
    bool wifi = wifi_is_connected();
    bool web = web_is_running();

    ESP_LOGI("main", "Status: wifi=%d, web=%d", wifi, web);
    pir312_dump_status();
    light_sensor_dump();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void app_main(void)
{
  esp_log_level_set("*", ESP_LOG_INFO);
  ESP_LOGI(TAG, "INIT: app_main starting");

  pir312_init();
  light_sensor_init();
  ws2812b_led_init();
  wifi_start();

  CHECK_ERR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &async_wifi_handler, NULL));
  ESP_LOGI(TAG, "INIT: Event handlers done");

  CHECK_XTASK_OK(xTaskCreatePinnedToCore(connect_monitor_task, "monitor_task", 4096, NULL, 5, NULL, 0));

  ESP_LOGI(TAG, "exit.");
}