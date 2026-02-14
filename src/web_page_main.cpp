#include <cmath>
#include <cstdio>
#include <cstring>
#include <driver/gpio.h>
#include <esp_chip_info.h>
#include <esp_event.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_psram.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <limits>
#include <sdkconfig.h>
#include <string>

#include "ota_support.h"
#include "pir312_monitor.h"
#include "utils.h"
#include "web_server.h"

static const char* TAG = "WEB PAGE MAIN";

#if defined(SOC_TEMPERATURE_SENSOR_SUPPORTED) && (SOC_TEMPERATURE_SENSOR_SUPPORTED)
#include <driver/temperature_sensor.h>
#endif
#if __has_include(<esp_efuse.h>)
#include <esp_efuse.h>
#endif
#if __has_include(<esp_efuse_table.h>)
#include <esp_efuse_table.h>
#endif
#if __has_include(<esp_secure_boot.h>)
#include <esp_secure_boot.h>
#endif
#if __has_include(<esp_flash_encrypt.h>)
#include <esp_flash_encrypt.h>
#endif

// --- Simple runtime stats updated via events (avoid heavy dependencies) ---
static uint32_t s_wifi_disconnect_count = 0;
static uint32_t s_wifi_disconnect_reason = 0;
static int64_t s_wifi_last_disconnect_us = 0;

static void wifi_diag_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  (void)arg;
  if (base != WIFI_EVENT)
  {
    return;
  }
  if (id == WIFI_EVENT_STA_DISCONNECTED)
  {
    const wifi_event_sta_disconnected_t* ev = (const wifi_event_sta_disconnected_t*)data;
    ++s_wifi_disconnect_count;
    s_wifi_disconnect_reason = (uint32_t)(ev != NULL ? ev->reason : 0U);
    s_wifi_last_disconnect_us = esp_timer_get_time();
  }
}

// ---------- Utilities ----------
static void format_mac(const uint8_t mac_bytes[6], char output[20])
{
  static const char* hex = "0123456789ABCDEF";
  int out = 0;
  for (int i = 0; i < 6; ++i)
  {
    output[out++] = hex[(mac_bytes[i] >> 4) & 0xF];
    output[out++] = hex[mac_bytes[i] & 0xF];
    if (i != 5)
      output[out++] = ':';
  }
  output[out] = '\0';
}

static const char* chip_model_str(esp_chip_model_t model)
{
  switch (model)
  {
  case CHIP_ESP32:
    return "ESP32";
  case CHIP_ESP32S2:
    return "ESP32-S2";
  case CHIP_ESP32S3:
    return "ESP32-S3";
  case CHIP_ESP32C3:
    return "ESP32-C3";
  case CHIP_ESP32C2:
    return "ESP32-C2";
  case CHIP_ESP32C6:
    return "ESP32-C6";
  case CHIP_ESP32H2:
    return "ESP32-H2";
  default:
    return "Unknown";
  }
}

static const char* reset_reason_str(esp_reset_reason_t reason)
{
  switch (reason)
  {
  case ESP_RST_POWERON:
    return "Power-on reset";
  case ESP_RST_EXT:
    return "External reset";
  case ESP_RST_SW:
    return "Software reset";
  case ESP_RST_PANIC:
    return "Panic reset";
  case ESP_RST_INT_WDT:
    return "Interrupt watchdog";
  case ESP_RST_TASK_WDT:
    return "Task watchdog";
  case ESP_RST_WDT:
    return "Other watchdog";
  case ESP_RST_DEEPSLEEP:
    return "Deep-sleep reset";
  case ESP_RST_BROWNOUT:
    return "Brownout (power drop)";
  case ESP_RST_SDIO:
    return "SDIO reset";
  default:
    return "Unknown";
  }
}

static const char* flash_mfg_str(uint32_t jedec_id)
{
  const uint8_t v0 = (uint8_t)(jedec_id & 0xFF);
  const uint8_t v1 = (uint8_t)((jedec_id >> 8) & 0xFF);
  const uint8_t v2 = (uint8_t)((jedec_id >> 16) & 0xFF);
  const uint8_t arr[3] = {v0, v1, v2};
  for (int i = 0; i < 3; ++i)
  {
    uint8_t v = arr[i];
    if (v == 0xEF)
      return "Winbond";
    if (v == 0xC8)
      return "GigaDevice";
    if (v == 0xC2)
      return "MXIC";
    if (v == 0x20)
      return "Micron";
    if (v == 0x1F)
      return "Adesto";
    if (v == 0x9D)
      return "ISSI";
    if (v == 0xBF)
      return "Boya";
    if (v == 0x68)
      return "BergMicro";
    if (v == 0xA1)
      return "Fudan";
  }
  return "Unknown";
}

static const char* flash_mode_from_sdkconfig(void)
{
#if defined(CONFIG_ESPTOOLPY_FLASHMODE_QIO)
  return "QIO";
#elif defined(CONFIG_ESPTOOLPY_FLASHMODE_QOUT)
  return "QOUT";
#elif defined(CONFIG_ESPTOOLPY_FLASHMODE_DIO)
  return "DIO";
#elif defined(CONFIG_ESPTOOLPY_FLASHMODE_DOUT)
  return "DOUT";
#else
  return "unknown";
#endif
}

static uint32_t flash_speed_hz_from_sdkconfig(void)
{
#if defined(CONFIG_ESPTOOLPY_FLASHFREQ_80M)
  return 80000000U;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_40M)
  return 40000000U;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_26M)
  return 26000000U;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_20M)
  return 20000000U;
#else
  return 0U;
#endif
}

static void build_wifi_runtime(std::string& proto_str, std::string& bw_str, double& tx_dbm)
{
  uint8_t proto_mask = 0;
  wifi_bandwidth_t bandwidth = WIFI_BW_HT20;
  int8_t quarter_dbm = 0;
  (void)esp_wifi_get_protocol(WIFI_IF_STA, &proto_mask);
  (void)esp_wifi_get_bandwidth(WIFI_IF_STA, &bandwidth);
  (void)esp_wifi_get_max_tx_power(&quarter_dbm);

  tx_dbm = (double)quarter_dbm * 0.25;
  proto_str.clear();
  if ((proto_mask & WIFI_PROTOCOL_11B) != 0)
    proto_str += "b/";
  if ((proto_mask & WIFI_PROTOCOL_11G) != 0)
    proto_str += "g/";
  if ((proto_mask & WIFI_PROTOCOL_11N) != 0)
    proto_str += "n/";
  if ((proto_mask & WIFI_PROTOCOL_LR) != 0)
    proto_str += "L/";
  if (!proto_str.empty())
    proto_str.pop_back();
  else
    proto_str = "unknown";
  bw_str = (bandwidth == WIFI_BW_HT40) ? "HT40" : "HT20";
}

static std::string get_active_hostname(void)
{
  esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  const char* hostname = NULL;
  if (sta != NULL)
  {
    if (esp_netif_get_hostname(sta, &hostname) != ESP_OK)
      hostname = NULL;
  }
  const char* safe = (hostname != NULL && hostname[0] != '\0') ? hostname : "-";
  return std::string(safe);
}

// ---------- JSON helpers ----------
static void json_escape_append(std::string& out, const char* s)
{
  if (s == NULL)
  {
    out += "null";
    return;
  }
  out.push_back('"');
  for (const unsigned char* p = (const unsigned char*)s; *p != '\0'; ++p)
  {
    unsigned char c = *p;
    if (c == '"')
      out += "\\\"";
    else if (c == '\\')
      out += "\\\\";
    else if (c == '\b')
      out += "\\b";
    else if (c == '\f')
      out += "\\f";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '\t')
      out += "\\t";
    else if (c < 0x20)
    {
      char buf[7];
      (void)snprintf(buf, sizeof(buf), "\\u%04X", (unsigned)c);
      out += buf;
    }
    else
      out.push_back((char)c);
  }
  out.push_back('"');
}

static void json_append_kv_str(std::string& out, const char* key, const char* val, bool last)
{
  out.push_back('"');
  out += key;
  out += "\":";
  json_escape_append(out, val);
  if (!last)
    out.push_back(',');
}

static void json_append_kv_num_u(std::string& out, const char* key, unsigned long long val, bool last)
{
  out.push_back('"');
  out += key;
  out += "\":";
  char buf[32];
  (void)snprintf(buf, sizeof(buf), "%llu", val);
  out += buf;
  if (!last)
    out.push_back(',');
}

static void json_append_kv_num_i(std::string& out, const char* key, long long val, bool last)
{
  out.push_back('"');
  out += key;
  out += "\":";
  char buf[32];
  (void)snprintf(buf, sizeof(buf), "%lld", val);
  out += buf;
  if (!last)
    out.push_back(',');
}

static void json_append_kv_num_f(std::string& out, const char* key, double val, bool last, int prec)
{
  out.push_back('"');
  out += key;
  out += "\":";
  char fmt[8];
  (void)snprintf(fmt, sizeof(fmt), "%%.%df", prec);
  char buf[64];
  (void)snprintf(buf, sizeof(buf), fmt, val);
  out += buf;
  if (!last)
    out.push_back(',');
}

static std::string build_inspect_json()
{
  const int64_t t0_us = esp_timer_get_time();

  std::string j;
  j.reserve(32768);
  j.push_back('{');

  // soc
  j += "\"soc\":{";
#if defined(CONFIG_IDF_TARGET)
  json_append_kv_str(j, "target", CONFIG_IDF_TARGET, false);
#else
  json_append_kv_str(j, "target", "unknown", false);
#endif
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  json_append_kv_str(j, "model", chip_model_str(chip_info.model), false);
  json_append_kv_num_u(j, "cores", (unsigned long long)chip_info.cores, false);
  json_append_kv_num_u(j, "revision", (unsigned long long)chip_info.revision, true);
  j.push_back('}');
  j.push_back(',');

  // net
  j += "\"net\":{";
  std::string host = get_active_hostname();
  json_append_kv_str(j, "hostname", host.c_str(), false);

  char mac_sta_str[20] = "-";
  uint8_t mac_sta[6];
  if (esp_wifi_get_mac(WIFI_IF_STA, mac_sta) == ESP_OK)
  {
    format_mac(mac_sta, mac_sta_str);
  }
  json_append_kv_str(j, "mac_sta", mac_sta_str, false);

  char ip[16] = "", gw[16] = "", mask[16] = "";
  {
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta != NULL)
    {
      esp_netif_ip_info_t info;
      if (esp_netif_get_ip_info(sta, &info) == ESP_OK)
      {
        (void)snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
        (void)snprintf(gw, sizeof(gw), IPSTR, IP2STR(&info.gw));
        (void)snprintf(mask, sizeof(mask), IPSTR, IP2STR(&info.netmask));
      }
    }
  }
  json_append_kv_str(j, "ip", (ip[0] ? ip : "-"), false);
  json_append_kv_str(j, "gw", (gw[0] ? gw : "-"), false);
  json_append_kv_str(j, "mask", (mask[0] ? mask : "-"), false);

  char dns1[16] = "-", dns2[16] = "-";
  {
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta != NULL)
    {
      esp_netif_dns_info_t dinfo;
      if (esp_netif_get_dns_info(sta, ESP_NETIF_DNS_MAIN, &dinfo) == ESP_OK && dinfo.ip.type == ESP_IPADDR_TYPE_V4)
      {
        (void)snprintf(dns1, sizeof(dns1), IPSTR, IP2STR(&dinfo.ip.u_addr.ip4));
      }
      if (esp_netif_get_dns_info(sta, ESP_NETIF_DNS_BACKUP, &dinfo) == ESP_OK && dinfo.ip.type == ESP_IPADDR_TYPE_V4)
      {
        (void)snprintf(dns2, sizeof(dns2), IPSTR, IP2STR(&dinfo.ip.u_addr.ip4));
      }
    }
  }
  json_append_kv_str(j, "dns1", dns1, false);
  json_append_kv_str(j, "dns2", dns2, true);
  j.push_back('}');
  j.push_back(',');

  // wifi
  j += "\"wifi\":{";
  wifi_ap_record_t ap;
  (void)memset(&ap, 0, sizeof(ap));
  (void)esp_wifi_sta_get_ap_info(&ap);
  int rssi_dbm = ap.rssi;
  int channel = ap.primary;
  char bssid[20] = "-";
  format_mac(ap.bssid, bssid);
  json_append_kv_str(j, "ssid", (ap.ssid[0] ? (const char*)ap.ssid : "-"), false);
  json_append_kv_num_i(j, "rssi", (long long)rssi_dbm, false);
  json_append_kv_num_u(j, "channel", (unsigned long long)channel, false);
  json_append_kv_str(j, "bssid", bssid, false);

  std::string proto, bw;
  double max_tx_dbm = 0.0;
  build_wifi_runtime(proto, bw, max_tx_dbm);
  json_append_kv_str(j, "proto", proto.c_str(), false);
  json_append_kv_str(j, "bw", bw.c_str(), false);
  json_append_kv_num_f(j, "max_tx_dbm", max_tx_dbm, false, 2);

  wifi_country_t country;
  (void)memset(&country, 0, sizeof(country));
  (void)esp_wifi_get_country(&country);
  char cc[3] = "-";
  if (country.cc[0] >= 0x20 && country.cc[1] >= 0x20)
  {
    cc[0] = country.cc[0];
    cc[1] = country.cc[1];
    cc[2] = '\0';
  }
  json_append_kv_str(j, "country", cc, false);

  int64_t age_s = 0;
  char age_buf[32];
  if (s_wifi_last_disconnect_us > 0)
  {
    const int64_t now_us = esp_timer_get_time();
    age_s = (now_us - s_wifi_last_disconnect_us) / 1000000LL;
    (void)snprintf(age_buf, sizeof(age_buf), "%lld s ago", (long long)age_s);
  }
  else
  {
    (void)snprintf(age_buf, sizeof(age_buf), "-");
  }
  json_append_kv_str(j, "last_disc_age", age_buf, true);
  j.push_back('}');
  j.push_back(',');

  // flash
  j += "\"flash\":{";
  uint32_t flash_size = 0, jedec_id = 0;
  (void)esp_flash_get_size(NULL, &flash_size);
  (void)esp_flash_read_id(NULL, &jedec_id);
  char jedec_hex[16];
  (void)snprintf(jedec_hex, sizeof(jedec_hex), "0x%08X", (unsigned)jedec_id);
  json_append_kv_num_u(j, "size", (unsigned long long)flash_size, false);
  json_append_kv_str(j, "jedec_hex", jedec_hex, false);
  json_append_kv_str(j, "vendor", flash_mfg_str(jedec_id), false);
  json_append_kv_str(j, "mode", flash_mode_from_sdkconfig(), false);
  json_append_kv_num_u(j, "speed_hz", (unsigned long long)flash_speed_hz_from_sdkconfig(), true);
  j.push_back('}');
  j.push_back(',');

  // heap
  j += "\"heap\":{";
  size_t heap_total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  size_t heap_free = esp_get_free_heap_size();
  size_t heap_min_free = esp_get_minimum_free_heap_size();
  size_t heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  size_t heap_internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t heap_spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  json_append_kv_num_u(j, "total", (unsigned long long)heap_total, false);
  json_append_kv_num_u(j, "free", (unsigned long long)heap_free, false);
  json_append_kv_num_u(j, "min_free", (unsigned long long)heap_min_free, false);
  json_append_kv_num_u(j, "largest", (unsigned long long)heap_largest, false);
  json_append_kv_num_u(j, "internal_free", (unsigned long long)heap_internal_free, false);
  json_append_kv_num_u(j, "spiram_free", (unsigned long long)heap_spiram_free, true);
  j.push_back('}');
  j.push_back(',');

  // psram
  j += "\"psram\":{";
  bool psram_ok = false;
  size_t psram_sz = 0;
#if defined(CONFIG_ESP32_SPIRAM_SUPPORT) || defined(CONFIG_SPIRAM_SUPPORT) || defined(CONFIG_SPIRAM) || defined(CONFIG_ESP_SPIRAM_SUPPORT)
  psram_ok = esp_psram_is_initialized();
  psram_sz = psram_ok ? esp_psram_get_size() : 0;
#endif
  json_append_kv_str(j, "state", psram_ok ? "OK" : "-", false);
  json_append_kv_num_u(j, "size", (unsigned long long)psram_sz, true);
  j.push_back('}');
  j.push_back(',');

  // ota
  j += "\"ota\":{";
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
  json_append_kv_num_u(j, "running_size", (unsigned long long)(running ? running->size : 0), false);
  json_append_kv_num_u(j, "next_size", (unsigned long long)(next ? next->size : 0), true);
  j.push_back('}');
  j.push_back(',');

  // sec
  j += "\"sec\":{";
  const char* flash_enc = "-";
#if __has_include(<esp_flash_encrypt.h>)
  flash_enc = (esp_flash_encryption_enabled() ? "enabled" : "disabled");
#endif
  const char* secure_boot = "-";
#if __has_include(<esp_secure_boot.h>)
  secure_boot = (esp_secure_boot_enabled() ? "enabled" : "disabled");
#endif
  const char* jtag_disabled = "-";
#if defined(ESP_EFUSE_DISABLE_JTAG) && __has_include(<esp_efuse.h>)
  jtag_disabled = (esp_efuse_read_field_bit(ESP_EFUSE_DISABLE_JTAG) ? "yes" : "no");
#endif
  json_append_kv_str(j, "secure_boot", secure_boot, false);
  json_append_kv_str(j, "flash_enc", flash_enc, false);
  json_append_kv_str(j, "jtag_disabled", jtag_disabled, true);
  j.push_back('}');
  j.push_back(',');

  // build
  j += "\"build\":{";
  json_append_kv_str(j, "idf", esp_get_idf_version(), false);
  json_append_kv_str(j, "date", __DATE__, false);
  json_append_kv_str(j, "time", __TIME__, true);
  j.push_back('}');
  j.push_back(',');

  // misc
  j += "\"misc\":{";
  {
    int64_t sec = esp_timer_get_time() / 1000000LL;
    int64_t days = sec / 86400LL;
    int64_t hrs = (sec / 3600LL) % 24LL;
    int64_t mins = (sec / 60LL) % 60LL;
    int64_t s = sec % 60LL;
    char buf[64];
    (void)snprintf(buf, sizeof(buf), "%lldd %lldh %lldm %llds", (long long)days, (long long)hrs, (long long)mins, (long long)s);
    json_append_kv_str(j, "uptime", buf, false);
  }
  const esp_reset_reason_t rr = esp_reset_reason();
  json_append_kv_str(j, "reset_reason", reset_reason_str(rr), false);
  json_append_kv_num_u(j, "reset_code", (unsigned long long)rr, true);
  j.push_back('}');
  j.push_back(',');

  // rtos
  j += "\"rtos\":{\"tasks\":[";
#if (configUSE_TRACE_FACILITY == 1)
  {
    UBaseType_t count = uxTaskGetNumberOfTasks();
    TaskStatus_t* list = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * (size_t)count);
    if (list != NULL)
    {
      UBaseType_t got = uxTaskGetSystemState(list, count, NULL);
      for (UBaseType_t i = 0; i < got; ++i)
      {
        const TaskStatus_t* ts = &list[i];
        if (i != 0)
          j.push_back(',');
        j.push_back('{');
        j += "\"name\":";
        json_escape_append(j, ts->pcTaskName);
        j.push_back(',');
        j += "\"prio\":";
        {
          char b[16];
          (void)snprintf(b, sizeof(b), "%u", (unsigned)ts->uxCurrentPriority);
          j += b;
        }
        j.push_back(',');
        const char* state = "unknown";
        if (ts->eCurrentState == eRunning)
          state = "running";
        else if (ts->eCurrentState == eReady)
          state = "ready";
        else if (ts->eCurrentState == eBlocked)
          state = "blocked";
        else if (ts->eCurrentState == eSuspended)
          state = "suspended";
        else if (ts->eCurrentState == eDeleted)
          state = "deleted";
        j += "\"state\":";
        json_escape_append(j, state);
        j.push_back(',');
        size_t bytes = (size_t)ts->usStackHighWaterMark * sizeof(StackType_t);
        j += "\"stack_min\":";
        {
          char b[32];
          (void)snprintf(b, sizeof(b), "%u", (unsigned)bytes);
          j += b;
        }
        j.push_back('}');
      }
      free(list);
    }
  }
#endif
  j += "]}"; // close rtos
  j.push_back(',');

  // partitions
  j += "\"partitions\":[";
  bool first = true;
  {
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL)
    {
      const esp_partition_t* p = esp_partition_get(it);
      if (!first)
        j.push_back(',');
      first = false;
      j.push_back('{');
      j += "\"label\":";
      json_escape_append(j, p->label);
      j.push_back(',');
      j += "\"type\":";
      {
        char b[16];
        (void)snprintf(b, sizeof(b), "%u", (unsigned)p->type);
        j += b;
      }
      j.push_back(',');
      j += "\"subtype\":";
      {
        char b[16];
        (void)snprintf(b, sizeof(b), "%u", (unsigned)p->subtype);
        j += b;
      }
      j.push_back(',');
      char addr[16];
      (void)snprintf(addr, sizeof(addr), "0x%08X", (unsigned)p->address);
      j += "\"addr\":";
      json_escape_append(j, addr);
      j.push_back(',');
      j += "\"size\":";
      {
        char b[32];
        (void)snprintf(b, sizeof(b), "%u", (unsigned)p->size);
        j += b;
      }
      j.push_back('}');
      it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
  }
  j.push_back(']');

  // timing last
  const int64_t t1_us = esp_timer_get_time();
  const long long dt_ms = (long long)((t1_us - t0_us) / 1000LL);
  j.push_back(',');
  j += "\"generation_time_ms\":";
  {
    char b[32];
    (void)snprintf(b, sizeof(b), "%lld", dt_ms);
    j += b;
  }

  j.push_back('}');
  return j;
}

// ---------- HTTP handlers ----------
static void handle_root()
{
  extern const uint8_t html_main_start[] asm("_binary_main_page_html_start");
  extern const uint8_t html_main_end[] asm("_binary_main_page_html_end");
  const size_t size = html_main_end - html_main_start;
  web_send_binary(200, "text/html; charset=utf-8", html_main_start, size);
}

static void handle_favicon()
{
  web_send(200, "image/x-icon", "");
}

static void handle_style_css()
{
  extern const uint8_t html_style_start[] asm("_binary_style_css_start");
  extern const uint8_t html_style_end[] asm("_binary_style_css_end");
  const size_t size = html_style_end - html_style_start;
  web_send_binary(200, "text/css; charset=utf-8", html_style_start, size);
}

static void handle_hw_details()
{
  std::string body = build_inspect_json();
  web_send(200, "application/json; charset=utf-8", body.c_str());
}

void web_on_started()
{
  static bool s_registered = false;
  if (!s_registered)
  {
    CHECK_ERR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_diag_event_handler, NULL));
    s_registered = true;
  }

  // Routes
  web_register_get("/", handle_root);
  web_register_get("/hw_details", handle_hw_details);
  web_register_get("/favicon.ico", handle_favicon);
  web_register_get("/style.css", handle_style_css);

  web_ui_pir312_on_started();
  ota_on_started();
}
