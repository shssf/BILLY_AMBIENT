#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <led_strip.h>
#include <led_strip_rmt.h>
#include <stdint.h>
#include <string.h>

#include "pir312_monitor.h"
#include "utils.h"
#include "ws2812b_support.h"

static const char* TAG = "WS2812B";

#define LED_PIN    GPIO_NUM_13
#define LED_COUNT  28
#define SEG_COUNT  4
#define SEG_LENGTH 7

static led_strip_handle_t s_strip = NULL;

static void ws2812b_led_task(void* arg)
{
  const uint32_t ambient = 25;
  const uint32_t high_b = 255;

  for (;;)
  {
    if (s_strip)
    {
      CHECK_ERR(led_strip_clear(s_strip));
      if (pir312_get_state(0) || pir312_get_state(5))
      {
        for (int i = 0; i < LED_COUNT; ++i)
          CHECK_ERR(led_strip_set_pixel(s_strip, i, ambient, ambient, ambient));
      }

      for (int k = 0; k < 4; ++k)
      {
        if (pir312_get_state(k + 1))
        {
          int offset = (k * SEG_LENGTH);
          for (int i = 0; i < SEG_LENGTH; ++i)
          {
            CHECK_ERR(led_strip_set_pixel(s_strip, offset + i, 0, high_b, 0));
          }
        }
      }
      CHECK_ERR(led_strip_refresh(s_strip));
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // 1 sec
  }
}

void ws2812b_led_init()
{
  led_strip_config_t strip_cfg = {};
  strip_cfg.strip_gpio_num = LED_PIN;
  strip_cfg.max_leds = LED_COUNT;
  strip_cfg.led_model = LED_MODEL_WS2812;
  strip_cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
  strip_cfg.flags = {};
  strip_cfg.flags.invert_out = false;

  led_strip_rmt_config_t rmt_cfg = {};
  rmt_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
  rmt_cfg.resolution_hz = 10 * 1000 * 1000;
  rmt_cfg.mem_block_symbols = 64;
  rmt_cfg.flags = {};
  rmt_cfg.flags.with_dma = false;

  CHECK_ERR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
  ESP_LOGI(TAG, "INIT: LED strip created on GPIO %d (%d px)", (int)LED_PIN, (int)LED_COUNT);

  CHECK_ERR(led_strip_clear(s_strip));
  CHECK_ERR(led_strip_refresh(s_strip));

  CHECK_XTASK_OK(xTaskCreatePinnedToCore(ws2812b_led_task, "ws2812b_led_task", 4096, NULL, 5, NULL, 1));
  ESP_LOGI(TAG, "Initialization done.");
}
