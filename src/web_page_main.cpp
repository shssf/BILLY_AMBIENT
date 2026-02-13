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

#include "pir312_monitor.h"
#include "utils.h" // for CHECK_ERR
#include "web_server.h"

/* --- Simple runtime stats updated via events (avoid heavy dependencies) --- */
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

/* Append HTML-escaped text (for SSID/hostnames etc). */
static void append_html_escaped(std::string& html, const char* text)
{
  if (text == NULL)
  {
    html += "-";
    return;
  }
  for (const char* p = text; *p != '\0'; ++p)
  {
    const char c = *p;
    if (c == '&')
    {
      html += "&amp;";
    }
    else if (c == '<')
    {
      html += "&lt;";
    }
    else if (c == '>')
    {
      html += "&gt;";
    }
    else if (c == '\"')
    {
      html += "&quot;";
    }
    else if (c == '\'')
    {
      html += "&#39;";
    }
    else
    {
      html.push_back(c);
    }
  }
}

/* Append a 4-column row: th/td + th/td. Values are HTML-escaped. */
static void append_row4(std::string& html, const char* th1, const char* td1, const char* th2, const char* td2)
{
  html += "<tr><th>";
  append_html_escaped(html, th1);
  html += "</th><td>";
  append_html_escaped(html, td1);
  html += "</td><th>";
  append_html_escaped(html, th2);
  html += "</th><td>";
  append_html_escaped(html, td2);
  html += "</td></tr>";
}

/* Append a 2-column row: th/td (td spans 3 columns). Value is HTML-escaped. */
static void append_row2(std::string& html, const char* th, const char* td)
{
  html += "<tr><th>";
  append_html_escaped(html, th);
  html += "</th><td colspan=\"3\">";
  append_html_escaped(html, td);
  html += "</td></tr>";
}

/* Return true if two-letter country code looks printable. */
static bool is_printable_cc(char c0, char c1)
{
  const bool ok0 = (c0 >= 0x20 && c0 <= 0x7E);
  const bool ok1 = (c1 >= 0x20 && c1 <= 0x7E);
  return ok0 && ok1;
}

/* Map flash manufacturer JEDEC ID to a human-readable string (extendable). */
static const char* flash_mfg_str(uint32_t jedec_id)
{
  const uint8_t vendors[3] = {
      (uint8_t)(jedec_id & 0xFF),
      (uint8_t)((jedec_id >> 8) & 0xFF),
      (uint8_t)((jedec_id >> 16) & 0xFF),
  };
  for (int index = 0; index < 3; ++index)
  {
    const uint8_t vendor = vendors[index];
    if (vendor == 0xEF)
      return "Winbond";
    if (vendor == 0xC8)
      return "GigaDevice";
    if (vendor == 0xC2)
      return "MXIC";
    if (vendor == 0x20)
      return "Micron";
    if (vendor == 0x1F)
      return "Adesto";
    if (vendor == 0x9D)
      return "ISSI";
    if (vendor == 0xBF)
      return "Boya";
    if (vendor == 0x68)
      return "BergMicro";
    if (vendor == 0xA1)
      return "Fudan";
  }
  return "Unknown";
}

/* Read flash mode from sdkconfig. */
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

/* Read flash speed (Hz) from sdkconfig. */
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

/* Convert esp_chip_model_t to readable string (best-effort across targets). */
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

/* Convert reset reason to readable string. */
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

/* Convert Wi-Fi disconnect reason (best-effort for classic ESP32 reasons). */
static const char* wifi_disc_reason_str(uint32_t reason)
{
  switch (reason)
  {
  case 1:
    return "UNSPECIFIED";
  case 2:
    return "AUTH_EXPIRE";
  case 3:
    return "AUTH_LEAVE";
  case 4:
    return "ASSOC_EXPIRE";
  case 5:
    return "ASSOC_TOOMANY";
  case 6:
    return "NOT_AUTHED";
  case 7:
    return "NOT_ASSOCED";
  case 8:
    return "ASSOC_LEAVE";
  case 9:
    return "ASSOC_NOT_AUTHED";
  case 10:
    return "DISASSOC_PWRCAP_BAD";
  case 11:
    return "DISASSOC_SUPCHAN_BAD";
  case 13:
    return "IE_INVALID";
  case 14:
    return "MIC_FAILURE";
  case 15:
    return "4WAY_HANDSHAKE_TIMEOUT";
  case 16:
    return "GROUP_KEY_UPDATE_TIMEOUT";
  case 17:
    return "IE_IN_4WAY_DIFFERS";
  case 18:
    return "GROUP_CIPHER_INVALID";
  case 19:
    return "PAIRWISE_CIPHER_INVALID";
  case 20:
    return "AKMP_INVALID";
  case 21:
    return "UNSUPP_RSN_IE_VERSION";
  case 22:
    return "INVALID_RSN_IE_CAP";
  case 23:
    return "802_1X_AUTH_FAILED";
  case 24:
    return "CIPHER_SUITE_REJECTED";
  case 200:
    return "BEACON_TIMEOUT";
  case 201:
    return "NO_AP_FOUND";
  case 202:
    return "AUTH_FAIL (wrong password?)";
  case 203:
    return "ASSOC_FAIL";
  case 204:
    return "HANDSHAKE_TIMEOUT";
  default:
    return "UNKNOWN";
  }
}

/* Format u32 as hex with fixed width. */
static std::string hex_u32(uint32_t value, int width)
{
  char buf[16];
  if (width <= 0)
  {
    width = 8;
  }
  snprintf(buf, sizeof(buf), "%0*X", width, (unsigned)value);
  return std::string(buf);
}

/* Build Wi-Fi runtime strings: protocols ("b/g/n/L") and bandwidth ("HT20"/"HT40"). */
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

/* Human-readable bytes (exact bytes + KiB/MiB). */
static std::string bytes_pretty(size_t bytes)
{
  const double b = (double)bytes;
  char buf[64];
  if (bytes >= (size_t)(1024U * 1024U))
  {
    const double mib = b / (1024.0 * 1024.0);
    snprintf(buf, sizeof(buf), "%u B (%.2f MiB)", (unsigned)bytes, mib);
  }
  else if (bytes >= 1024U)
  {
    const double kib = b / 1024.0;
    snprintf(buf, sizeof(buf), "%u B (%.2f KiB)", (unsigned)bytes, kib);
  }
  else
  {
    snprintf(buf, sizeof(buf), "%u B", (unsigned)bytes);
  }
  return std::string(buf);
}

/* Read active STA hostname. */
static std::string get_active_hostname(void)
{
  esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  const char* hostname = NULL;
  if (sta_netif != NULL)
  {
    if (esp_netif_get_hostname(sta_netif, &hostname) != ESP_OK)
      hostname = NULL;
  }
  const char* safe = (hostname != NULL && hostname[0] != '\0') ? hostname : "-";
  return std::string(safe);
}

/* Read active STA IP/GW/Mask. */
static std::string get_active_ip_bundle(void)
{
  esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  char ip_str[16] = "";
  char gw_str[16] = "";
  char mask_str[16] = "";
  if (sta_netif != NULL)
  {
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK)
    {
      snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
      snprintf(gw_str, sizeof(gw_str), IPSTR, IP2STR(&ip_info.gw));
      snprintf(mask_str, sizeof(mask_str), IPSTR, IP2STR(&ip_info.netmask));
    }
  }
  return std::string(ip_str) + " / " + gw_str + " / " + mask_str;
}

/* Read DNS servers (best-effort). */
static std::string get_dns_bundle(void)
{
  esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  char dns1[16] = "-";
  char dns2[16] = "-";
  if (sta_netif != NULL)
  {
    esp_netif_dns_info_t info;
    if (esp_netif_get_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &info) == ESP_OK)
    {
      if (info.ip.type == ESP_IPADDR_TYPE_V4)
      {
        snprintf(dns1, sizeof(dns1), IPSTR, IP2STR(&info.ip.u_addr.ip4));
      }
    }
    if (esp_netif_get_dns_info(sta_netif, ESP_NETIF_DNS_BACKUP, &info) == ESP_OK)
    {
      if (info.ip.type == ESP_IPADDR_TYPE_V4)
      {
        snprintf(dns2, sizeof(dns2), IPSTR, IP2STR(&info.ip.u_addr.ip4));
      }
    }
  }
  return std::string(dns1) + " / " + dns2;
}

/* Uptime in a compact format. */
static std::string get_uptime_str(void)
{
  const int64_t sec = esp_timer_get_time() / 1000000LL;
  const int64_t days = sec / 86400LL;
  const int64_t hours = (sec / 3600LL) % 24LL;
  const int64_t mins = (sec / 60LL) % 60LL;
  const int64_t secs = sec % 60LL;
  char buf[64];
  snprintf(buf, sizeof(buf), "%lldd %lldh %lldm %llds", (long long)days, (long long)hours, (long long)mins, (long long)secs);
  return std::string(buf);
}

static void append_page_header(std::string& html)
{
  html += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>ESP Inspector</title>";
  html += "<style>";
  html += "body{font-family:system-ui,Segoe UI,Arial,sans-serif;margin:16px;}";
  html += "table{border-collapse:collapse;width:100%;max-width:1100px;}";
  html += "th,td{border:1px solid #ccc;padding:6px 8px;vertical-align:top;}";
  html += "th{background:#f4f4f4;text-align:left;white-space:nowrap;}";
  html += "h1,h2,h3{margin:14px 0 8px;}";
  html += ".mono{font-family:ui-monospace,Consolas,monospace;}";
  html += ".btn{display:inline-block;padding:8px 12px;border-radius:10px;background:#222;color:#fff;text-decoration:none;}";
  html += "</style></head><body>";

  html +=
      "<p>"
      "<a class=\"btn\" href=\"/pir312\">PIR312 trace</a> "
      "<a class=\"btn\" href=\"/pir312/status\">JSON Status</a>"
      "<a class=\"btn\" href=\"/ota\">OTA update</a>"
      "</p>";
  html += "<h1>ESP Inspector</h1>";
  html += "<p class=\"mono\">/ (read-only)</p>";
}

static void append_page_footer(std::string& html)
{
  html += "</body></html>";
}

static void format_mac(const uint8_t mac_bytes[6], char output[20])
{
  static const char* hex_chars = "0123456789ABCDEF";
  int out_index = 0;
  for (int index = 0; index < 6; ++index)
  {
    output[out_index++] = hex_chars[(mac_bytes[index] >> 4) & 0xF];
    output[out_index++] = hex_chars[mac_bytes[index] & 0xF];
    if (index != 5)
    {
      output[out_index++] = ':';
    }
  }
  output[out_index] = '\0';
}

static void append_system_inspector_table(std::string& html)
{
  // STA AP info (SSID/BSSID/RSSI/Channel).
  wifi_ap_record_t ap_record;
  (void)memset(&ap_record, 0, sizeof(ap_record));
  char bssid_str[20] = "-";
  int ap_rssi_dbm = 0;
  int ap_channel = 0;

  if (esp_wifi_sta_get_ap_info(&ap_record) == ESP_OK)
  {
    ap_rssi_dbm = ap_record.rssi;
    ap_channel = ap_record.primary;
    format_mac(ap_record.bssid, bssid_str);
  }
  const char* ssid_str = (ap_record.ssid[0] != 0) ? (const char*)ap_record.ssid : "-";

  // STA MAC.
  char mac_sta_str[20] = "-";
  uint8_t mac_sta_bytes[6];
  if (esp_wifi_get_mac(WIFI_IF_STA, mac_sta_bytes) == ESP_OK)
  {
    format_mac(mac_sta_bytes, mac_sta_str);
  }

  // Wi-Fi runtime radio properties.
  std::string proto_str;
  std::string bw_str;
  double max_tx_dbm = 0.0;
  build_wifi_runtime(proto_str, bw_str, max_tx_dbm);

  wifi_country_t country;
  memset(&country, 0, sizeof(country));
  char country_code[4] = "-";
  if (esp_wifi_get_country(&country) == ESP_OK)
  {
    const char d0 = country.cc[0];
    const char d1 = country.cc[1];
    if (is_printable_cc(d0, d1))
    {
      country_code[0] = d0;
      country_code[1] = d1;
      country_code[2] = '\0';
    }
  }
  const char* country_policy = "-";
  if (country.policy == WIFI_COUNTRY_POLICY_AUTO)
    country_policy = "auto";
  else if (country.policy == WIFI_COUNTRY_POLICY_MANUAL)
    country_policy = "manual";

  char wifi_radio_buf[160];
  snprintf(wifi_radio_buf,
           sizeof(wifi_radio_buf),
           "Proto: %s, BW: %s, MaxTx: %.2f dBm, Country: %s (schan=%u, nchan=%u, policy=%s)",
           proto_str.c_str(),
           bw_str.c_str(),
           max_tx_dbm,
           country_code,
           (unsigned)country.schan,
           (unsigned)country.nchan,
           country_policy);

  // Chip info.
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
#if defined(CONFIG_IDF_TARGET)
  const char* idf_target = CONFIG_IDF_TARGET;
#else
  const char* idf_target = "unknown";
#endif
  const std::string cores_str = std::to_string((unsigned)chip_info.cores);
  const std::string revision_str = std::to_string((unsigned)chip_info.revision);
  const char* idf_version = esp_get_idf_version();
  const esp_reset_reason_t rr = esp_reset_reason();

  // Flash.
  uint32_t flash_size_bytes = 0;
  uint32_t flash_jedec_id = 0;
  (void)esp_flash_get_size(NULL, &flash_size_bytes);
  (void)esp_flash_read_id(NULL, &flash_jedec_id);
  const char* flash_mfg = flash_mfg_str(flash_jedec_id);
  const char* flash_mode = flash_mode_from_sdkconfig();
  const uint32_t flash_speed_hz = flash_speed_hz_from_sdkconfig();
  const std::string jedec_hex = std::string("0x") + hex_u32(flash_jedec_id, 8);

  // Heap.
  const size_t heap_total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  const size_t heap_free = esp_get_free_heap_size();
  const size_t heap_min_free = esp_get_minimum_free_heap_size();
  const size_t heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const size_t heap_internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t heap_spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

  // PSRAM.
  bool psram_initialized = false;
  size_t psram_size_bytes = 0;
#if defined(CONFIG_ESP32_SPIRAM_SUPPORT) || defined(CONFIG_SPIRAM_SUPPORT) || defined(CONFIG_SPIRAM) || defined(CONFIG_ESP_SPIRAM_SUPPORT)
  psram_initialized = esp_psram_is_initialized();
  psram_size_bytes = psram_initialized ? esp_psram_get_size() : 0;
#endif

  // OTA partitions (running/next).
  const esp_partition_t* running_part = esp_ota_get_running_partition();
  const size_t ota_running_size = (running_part != NULL) ? running_part->size : 0;
  const esp_partition_t* next_part = esp_ota_get_next_update_partition(NULL);
  const size_t ota_next_size = (next_part != NULL) ? next_part->size : 0;

  // Security/eFuse (best-effort).
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

  // Wi-Fi disconnect diagnostics.
  char disc_reason_buf[96];
  const uint32_t disc_reason = s_wifi_disconnect_reason;
  snprintf(disc_reason_buf,
           sizeof(disc_reason_buf),
           "%s (%lu), count=%lu",
           wifi_disc_reason_str(disc_reason),
           (unsigned long)disc_reason,
           (unsigned long)s_wifi_disconnect_count);

  // Age since last disconnect.
  std::string disc_age = "-";
  if (s_wifi_last_disconnect_us > 0)
  {
    const int64_t now_us = esp_timer_get_time();
    const int64_t delta_s = (now_us - s_wifi_last_disconnect_us) / 1000000LL;
    disc_age = std::to_string((unsigned)delta_s) + " s ago";
  }

  // Table start.
  html += "<h2>Device & SoC</h2><table>";
  append_row4(html, "Target", idf_target, "Model", chip_model_str(chip_info.model));
  append_row4(html, "Cores", cores_str.c_str(), "Revision", revision_str.c_str());
  append_row4(html, "Hostname", get_active_hostname().c_str(), "STA MAC", mac_sta_str);
  append_row4(html, "IP/GW/Mask", get_active_ip_bundle().c_str(), "DNS (main/backup)", get_dns_bundle().c_str());

  // Wi-Fi summary.
  html += "<tr><th>WiFi link</th><td colspan=\"3\">";
  html += "SSID: ";
  append_html_escaped(html, ssid_str);
  html += ", RSSI: " + std::to_string(ap_rssi_dbm) + " dBm, Channel: " + std::to_string(ap_channel);
  html += ", BSSID: ";
  append_html_escaped(html, bssid_str);
  html += "</td></tr>";

  append_row2(html, "WiFi radio", wifi_radio_buf);
  append_row2(html, "WiFi last disconnect", disc_reason_buf);
  append_row4(html, "Disconnect age", disc_age.c_str(), "Uptime", get_uptime_str().c_str());

  // Flash.
  html += "<tr><th>Flash</th><td colspan=\"3\">";
  html += "Size: " + bytes_pretty((size_t)flash_size_bytes);
  html += ", JEDEC: ";
  append_html_escaped(html, jedec_hex.c_str());
  html += ", Vendor: ";
  append_html_escaped(html, flash_mfg);
  html += ", Mode: ";
  append_html_escaped(html, flash_mode);
  html += ", Speed: " + std::to_string((unsigned)flash_speed_hz) + " Hz";
  html += "</td></tr>";

  // Heap/PSRAM.
  html += "<tr><th>Heap (8-bit)</th><td colspan=\"3\">";
  html += "Total: " + bytes_pretty(heap_total);
  html += ", Free: " + bytes_pretty(heap_free);
  html += ", MinEverFree: " + bytes_pretty(heap_min_free);
  html += ", Largest: " + bytes_pretty(heap_largest);
  html += "</td></tr>";

  html += "<tr><th>Heap (caps)</th><td colspan=\"3\">";
  html += "InternalFree: " + bytes_pretty(heap_internal_free);
  html += ", SPIRAMFree: " + bytes_pretty(heap_spiram_free);
  html += "</td></tr>";

  html += "<tr><th>PSRAM</th><td colspan=\"3\">";
  html += (psram_initialized ? "OK" : "-");
  html += ", Size: " + bytes_pretty(psram_size_bytes);
  html += "</td></tr>";

  // OTA info.
  html += "<tr><th>OTA</th><td colspan=\"3\">";
  html += "Running size: " + bytes_pretty(ota_running_size);
  html += ", Next size: " + bytes_pretty(ota_next_size);
  html += "</td></tr>";

  // Security / eFuse.
  html += "<tr><th>Security</th><td colspan=\"3\">";
  html += "Secure boot: ";
  append_html_escaped(html, secure_boot);
  html += ", Flash encryption: ";
  append_html_escaped(html, flash_enc);
  html += ", JTAG disabled: ";
  append_html_escaped(html, jtag_disabled);
  html += "</td></tr>";

  // Build / reset.
  html += "<tr><th>Build</th><td colspan=\"3\">";
  html += "IDF: ";
  append_html_escaped(html, idf_version);
  html += ", " __DATE__ " " __TIME__;
  html += "</td></tr>";

  html += "<tr><th>Reset</th><td colspan=\"3\">";
  html += "Reason: ";
  append_html_escaped(html, reset_reason_str(rr));
  html += " (";
  html += std::to_string((unsigned)rr);
  html += ")";
  html += "</td></tr>";

#if defined(SOC_TEMPERATURE_SENSOR_SUPPORTED) && (SOC_TEMPERATURE_SENSOR_SUPPORTED)
  // Die temperature.
  float die_temp_c = std::numeric_limits<float>::quiet_NaN();
  {
    temperature_sensor_handle_t temp_handle = NULL;
    temperature_sensor_config_t temp_cfg;
    temp_cfg.range_min = -10;
    temp_cfg.range_max = 80;
    if (temperature_sensor_install(&temp_cfg, &temp_handle) == ESP_OK)
    {
      (void)temperature_sensor_enable(temp_handle);
      float out_c = 0.0f;
      if (temperature_sensor_get_celsius(temp_handle, &out_c) == ESP_OK)
      {
        die_temp_c = out_c;
      }
      (void)temperature_sensor_disable(temp_handle);
      (void)temperature_sensor_uninstall(temp_handle);
    }
  }
  char temp_buf[32];
  if (std::isnan(die_temp_c))
  {
    snprintf(temp_buf, sizeof(temp_buf), "-");
  }
  else
  {
    snprintf(temp_buf, sizeof(temp_buf), "%.1f C", (double)die_temp_c);
  }
  append_row4(html, "Die temperature", temp_buf, "Temp sensor", "supported");
#else
  append_row4(html, "Die temperature", "-", "Temp sensor", "not supported");
#endif

  html += "</table>";
}

static void append_rtos_table(std::string& html)
{
  html += "<h2>RTOS</h2><table>";
  html += "<tr><th>Task</th><th>Prio</th><th>State</th><th>Stack free min</th></tr>";
#if (configUSE_TRACE_FACILITY == 1)
  const UBaseType_t count = uxTaskGetNumberOfTasks();
  TaskStatus_t* list = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * (size_t)count);
  if (list == NULL)
  {
    html += "<tr><td colspan=\"4\">Out of memory</td></tr></table>";
    return;
  }
  const UBaseType_t got = uxTaskGetSystemState(list, count, NULL);
  for (UBaseType_t i = 0; i < got; ++i)
  {
    const TaskStatus_t* ts = &list[i];
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
    const size_t words = (size_t)ts->usStackHighWaterMark;
    const size_t bytes = words * sizeof(StackType_t);

    html += "<tr><td class=\"mono\">";
    append_html_escaped(html, ts->pcTaskName);
    html += "</td><td>";
    html += std::to_string((unsigned)ts->uxCurrentPriority);
    html += "</td><td>";
    append_html_escaped(html, state);
    html += "</td><td>";
    html += bytes_pretty(bytes);
    html += "</td></tr>";
  }
  free(list);
#else
  html += "<tr><td colspan=\"4\">Trace facility disabled (configUSE_TRACE_FACILITY=0)</td></tr>";
#endif
  html += "</table>";
}

static void append_partitions_table(std::string& html)
{
  html += "<h2>Flash partitions</h2><table>";
  html += "<tr><th>Label</th><th>Type</th><th>Subtype</th><th>Addr</th><th>Size</th></tr>";

  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it != NULL)
  {
    const esp_partition_t* part = esp_partition_get(it);

    char addr_buf[16];
    snprintf(addr_buf, sizeof(addr_buf), "0x%08X", (unsigned)part->address);

    html += "<tr><td class=\"mono\">";
    append_html_escaped(html, part->label);
    html += "</td><td>";
    html += std::to_string((unsigned)part->type);
    html += "</td><td>";
    html += std::to_string((unsigned)part->subtype);
    html += "</td><td class=\"mono\">";
    append_html_escaped(html, addr_buf);
    html += "</td><td>";
    html += bytes_pretty((size_t)part->size);
    html += "</td></tr>";

    // move to next; NOTE: esp_partition_next(it) frees the *previous* iterator
    it = esp_partition_next(it);
  }

  // Safe even if 'it' is NULL.
  esp_partition_iterator_release(it);

  html += "</table>";
}

static std::string build_root_html()
{
  std::string html;
  html.reserve(65000);
  append_page_header(html);
  append_system_inspector_table(html);
  append_rtos_table(html);
  append_partitions_table(html);
  append_page_footer(html);
  return html;
}

static void handle_root()
{
  std::string page_html = build_root_html();
  web_send(200, "text/html; charset=utf-8", page_html.c_str());
}

static void handle_favicon()
{
  web_send(200, "image/x-icon", "");
}

void web_on_started()
{
  static bool s_registered = false;
  if (!s_registered)
  {
    CHECK_ERR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_diag_event_handler, NULL));
    s_registered = true;
  }
  web_register_get("/", handle_root);
  web_register_get("/favicon.ico", handle_favicon);
  web_ui_pir312_on_started();
  ota_on_started();
}
