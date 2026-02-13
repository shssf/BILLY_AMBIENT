#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <stdbool.h>
#include <string.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_softap.h>

#include "utils.h"
#include "wifi_support.h"

static const char* TAG = "wifi_support";

static char s_hostname[32] = "billy_led";
static const char* s_setup_ap_ssid = "billy_led_setup";
static const char* s_setup_ap_pass = "12345678";
static const char* s_prov_pop = "abcd1234";
static bool got_ip = false;

static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  if (base == WIFI_EVENT)
  {
    switch (id)
    {
    case WIFI_EVENT_STA_START:
      ESP_LOGI(TAG, "STA start -> connect");
      CHECK_ERR(esp_wifi_connect());
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      ESP_LOGW(TAG, "STA disconnected -> reconnect");
      CHECK_ERR(esp_wifi_connect());
      got_ip = false;
      break;
    default:
      break;
    }
  }
  else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
  {
    ip_event_got_ip_t* e = (ip_event_got_ip_t*)data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&e->ip_info.ip));
    got_ip = true;
  }
}

static void wifi_init_common(void)
{
  /* Init NVS (required by Wi‑Fi) */
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    CHECK_ERR(nvs_flash_erase());
    CHECK_ERR(nvs_flash_init());
  }

  /* Init TCP/IP stack and default event loop */
  CHECK_ERR(esp_netif_init());
  CHECK_ERR(esp_event_loop_create_default());

  /* Register common event handlers once */
  static bool s_reg = false;
  if (!s_reg)
  {
    CHECK_ERR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    CHECK_ERR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    s_reg = true;
  }

  ESP_LOGI(TAG, "WiFi common done.");
}

bool wifi_start(void)
{
  wifi_init_common();

  /* Initialize Wi‑Fi driver BEFORE using provisioning manager (required by ESP‑IDF) */
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  CHECK_ERR(esp_wifi_init(&cfg));

  /* Create default netifs so SoftAP transport has AP interface and later STA uses its interface */
  static esp_netif_t* s_ap = NULL;
  static esp_netif_t* s_sta = NULL;
  if (!s_ap)
    s_ap = esp_netif_create_default_wifi_ap();
  if (!s_sta)
    s_sta = esp_netif_create_default_wifi_sta();

  /* Provisioning manager (SoftAP scheme) */
  wifi_prov_mgr_config_t prov_cfg = {};
  prov_cfg.scheme = wifi_prov_scheme_softap;
  prov_cfg.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;
  ESP_LOGI(TAG, "Provisioning: SoftAP ssid='%s', pop='%s'", s_setup_ap_ssid, s_prov_pop);
  CHECK_ERR(wifi_prov_mgr_init(prov_cfg));

  bool provisioned = false;
  CHECK_ERR(wifi_prov_mgr_is_provisioned(&provisioned));
  if (!provisioned)
  {
    /* Start SoftAP portal; credentials will be stored to NVS by the manager */
    ESP_LOGI(TAG, "Starting provisioning portal: ssid='%s', pass='%s'", s_setup_ap_ssid, s_setup_ap_pass);
    wifi_prov_security_t sec = WIFI_PROV_SECURITY_1; /* PoP-based session */
    CHECK_ERR(wifi_prov_mgr_start_provisioning(sec, s_prov_pop, s_setup_ap_ssid, s_setup_ap_pass));
    /* Keep SoftAP alive; do NOT start STA now */
    return false; /* means: provisioning portal is running */
  }

  ESP_LOGI(TAG, "Already provisioned; starting STA...");

  /* Bring up STA using stored credentials */
  if (s_sta)
  {
    CHECK_ERR(esp_netif_set_hostname(s_sta, s_hostname));
  }
  CHECK_ERR(esp_wifi_set_mode(WIFI_MODE_STA));
  CHECK_ERR(esp_wifi_start());

  ESP_LOGI(TAG, "Initialization done.");
  return true;
}

bool wifi_is_connected(void)
{
  return got_ip;
}

const char* wifi_get_hostname(void)
{
  return s_hostname;
}

void wifi_set_hostname(const char* name)
{
  if (name && *name)
  {
    strncpy(s_hostname, name, sizeof(s_hostname) - 1);
    s_hostname[sizeof(s_hostname) - 1] = '\0';
  }
}

const char* wifi_get_ap_password(void)
{
  return s_setup_ap_pass;
}

void wifi_reset(void)
{
  ESP_LOGW(TAG, "Erasing NVS (WiFi credentials) and rebooting...");
  CHECK_ERR(nvs_flash_erase());
  CHECK_ERR(nvs_flash_init());
  esp_restart();
}

// std::string wifi_get_ip(void)
// {
//   esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
//   char ip_str[16] = "-";

//   if (sta != NULL)
//   {
//     esp_netif_ip_info_t ip_info = {};
//     if (esp_netif_get_ip_info(sta, &ip_info) == ESP_OK)
//     {
//       snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
//     }
//   }
//   return std::string(ip_str);
// }
