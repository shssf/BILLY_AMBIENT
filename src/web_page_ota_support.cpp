#include <cstring>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ota_support.h"
#include "utils.h"
#include "web_server.h"

static const char* TAG = "ota_support";

static void reboot_task(void* arg)
{
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
}

static void h_get_ota_page(void)
{
  extern const uint8_t html_ota_start[] asm("_binary_ota_page_html_start");
  extern const uint8_t html_ota_end[] asm("_binary_ota_page_html_end");

  const size_t size = html_ota_end - html_ota_start;
  web_send_binary(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(html_ota_start), size);
}

static void h_post_update(void)
{
  esp_err_t result = ESP_OK;
  esp_ota_handle_t ota = (esp_ota_handle_t)0;
  bool ota_opened = false;

  /* Validate Content-Length first. */
  const size_t content_len = web_content_length(); /* provided by web_server.* in your API style */
  if (content_len == 0U)
  {
    web_send(400, "text/plain", "Empty body");
    return;
  }

  /* Resolve target OTA partition. */
  const esp_partition_t* update_part = esp_ota_get_next_update_partition(NULL);
  if (update_part == NULL)
  {
    web_send(500, "text/plain", "No OTA partition");
    return;
  }

  /* Begin OTA write session. */
  result = esp_ota_begin(update_part, OTA_SIZE_UNKNOWN, &ota);
  CHECK_ERR(result);
  if (result != ESP_OK)
  {
    web_send(500, "text/plain", "esp_ota_begin failed");
    return;
  }
  ota_opened = true;

  /* Receive and write body. */
  uint8_t buf[4096];
  size_t remaining = content_len;

  while (remaining > 0U)
  {
    size_t to_read = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
    int r = web_recv(buf, to_read);
    if (r == -2)
    {
      /* Timeout: retry the same chunk. */
      continue;
    }
    if (r <= 0)
    {
      if (ota_opened)
      {
        (void)esp_ota_abort(ota);
        ota_opened = false;
      }
      web_send(500, "text/plain", "recv failed");
      return;
    }

    result = esp_ota_write(ota, (const void*)buf, (size_t)r);
    CHECK_ERR(result);
    if (result != ESP_OK)
    {
      if (ota_opened)
      {
        (void)esp_ota_abort(ota);
        ota_opened = false;
      }
      web_send(500, "text/plain", "esp_ota_write failed");
      return;
    }

    remaining -= (size_t)r;
  }

  /* Finalize and set boot partition. */
  result = esp_ota_end(ota);
  CHECK_ERR(result);
  if (result != ESP_OK)
  {
    /* When esp_ota_end fails, OTA is already closed/invalid; no abort needed. */
    web_send(500, "text/plain", "esp_ota_end failed");
    return;
  }
  ota_opened = false;

  result = esp_ota_set_boot_partition(update_part);
  CHECK_ERR(result);
  if (result != ESP_OK)
  {
    web_send(500, "text/plain", "set_boot_partition failed");
    return;
  }

  web_set_resp_header("Connection", "close");

  web_send(200, "text/plain", "OK. Rebooting in 1s...");
  CHECK_XTASK_OK(xTaskCreate(reboot_task, "ota_reboot", 2048, NULL, 5, NULL)); // <-- now checked
}

void ota_on_started(void)
{
  web_register_get("/ota", h_get_ota_page);
  web_register_post("/update", h_post_update);
}
