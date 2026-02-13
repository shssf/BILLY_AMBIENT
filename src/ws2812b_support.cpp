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

// --- Hardware / layout ---
#define LED_PIN         GPIO_NUM_13
#define LED_COUNT       120
#define LIGHT_THRESHOLD 500

// Optional segment mapping (keep as-is)
#define SEG_LEFT_START         0
#define SEG_LEFT_END           29
#define SEG_LEFT_CENTER_START  30
#define SEG_LEFT_CENTER_END    59
#define SEG_RIGHT_CENTER_START 60
#define SEG_RIGHT_CENTER_END   89
#define SEG_RIGHT_START        90
#define SEG_RIGHT_END          119

static led_strip_handle_t s_strip = NULL;

static void set_pixel_rgb(int idx, uint8_t r, uint8_t g, uint8_t b)
{
  if (idx < 0 || idx >= LED_COUNT || !s_strip)
  {
    return;
  }
  CHECK_ERR(led_strip_set_pixel(s_strip, (uint32_t)idx, r, g, b));
}

static void ws2812b_led_task(void* arg)
{
  ESP_LOGI(TAG, "INIT: LED task starting");

  adc_oneshot_unit_handle_t adc_handle = (adc_oneshot_unit_handle_t)arg;

  for (;;)
  {
    int light = 0;
    if (adc_handle != NULL)
    {
      CHECK_ERR(adc_oneshot_read(adc_handle, ADC_CHANNEL_6, &light));
    }

    const bool is_bright = (light > LIGHT_THRESHOLD);
    if (is_bright)
    {
      if (s_strip)
        CHECK_ERR(led_strip_clear(s_strip));
    }
    else
    {
      const uint8_t low_b = 25;
      const uint8_t high_b = 255;

      if (pir312_get_ambient())
      {
        for (int i = 0; i < LED_COUNT; ++i)
          set_pixel_rgb(i, low_b, low_b, low_b);
      }
      else
      {
        if (s_strip)
          CHECK_ERR(led_strip_clear(s_strip));
      }

      if (pir312_get_box_left())
      {
        for (int i = SEG_LEFT_START; i <= SEG_LEFT_END; ++i)
          set_pixel_rgb(i, high_b, high_b, high_b);
      }
      if (pir312_get_box_left_center())
      {
        for (int i = SEG_LEFT_CENTER_START; i <= SEG_LEFT_CENTER_END; ++i)
          set_pixel_rgb(i, high_b, high_b, high_b);
      }
      if (pir312_get_box_right_center())
      {
        for (int i = SEG_RIGHT_CENTER_START; i <= SEG_RIGHT_CENTER_END; ++i)
          set_pixel_rgb(i, high_b, high_b, high_b);
      }
      if (pir312_get_box_right())
      {
        for (int i = SEG_RIGHT_START; i <= SEG_RIGHT_END; ++i)
          set_pixel_rgb(i, high_b, high_b, high_b);
      }
    }

    if (s_strip)
      CHECK_ERR(led_strip_refresh(s_strip));
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void ws2812b_led_init()
{
  led_strip_config_t strip_cfg = {.strip_gpio_num = (int)LED_PIN,
                                  .max_leds = LED_COUNT,
                                  .led_model = LED_MODEL_WS2812,
                                  .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
                                  .flags = {.invert_out = false}};

  led_strip_rmt_config_t rmt_cfg = {
      .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10 * 1000 * 1000, .mem_block_symbols = 64, .flags = {.with_dma = false}};

  CHECK_ERR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
  ESP_LOGI(TAG, "INIT: LED strip created on GPIO %d (%d px)", (int)LED_PIN, (int)LED_COUNT);

  CHECK_ERR(led_strip_clear(s_strip));
  CHECK_ERR(led_strip_refresh(s_strip));

  CHECK_XTASK_OK(xTaskCreatePinnedToCore(ws2812b_led_task, "ws2812b_led_task", 4096, NULL, 5, NULL, 1));
  ESP_LOGI(TAG, "Initialization done.");
}
