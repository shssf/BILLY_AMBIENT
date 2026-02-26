#include <algorithm>
#include <cmath>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <led_strip.h>
#include <led_strip_rmt.h>
#include <stdint.h>

#include "light_sensor_support.h"
#include "pir312_monitor.h"
#include "utils.h"
#include "ws2812b_support.h"

static const char* TAG = "WS2812B";

struct RgbType
{
  uint32_t r;
  uint32_t g;
  uint32_t b;
};

#define LED_PIN           GPIO_NUM_13
#define LED_COUNT         84
#define SEG_COUNT         4
#define SEG_LENGTH        21
#define DIMMING_INCREMENT 5

static led_strip_handle_t s_strip = NULL;
static const RgbType colors[SEG_COUNT + 1] = {
    {150, 0, 255}, // left-left closet
    {150, 0, 255}, // left-center closet
    {150, 0, 255}, // right-center closet
    {150, 0, 255}, // right-right closet
    {50,  0, 10 }  // ambient
};
static uint32_t dimming_step[SEG_COUNT + 1] = {0, 0, 0, 0, 0};

static inline float clamp01f(float x)
{
  if (std::isnan(x))
  {
    return 0.0f;
  }

  return std::clamp<float>(x, 0.0f, 1.0f);
}

/* sRGB -> linear light */
static inline float srgb_to_linear(int32_t color)
{
  float result = 0.0f;
  float color_f = static_cast<float>(color);

  /* Normalize input sRGB to [0..1] */
  const float rs = color_f / 255.0f;

  result = clamp01f(rs);
  if (result <= 0.04045f)
  {
    result /= 12.92f;
  }
  else
  {
    result = powf((result + 0.055f) / 1.055f, 2.4f);
  }
  return result;
}

/* linear light -> sRGB */
static inline int32_t linear_to_srgb(float value)
{
  float result = 0.0f;

  value = clamp01f(value);
  if (value <= 0.0031308f)
  {
    result = 12.92f * value;
  }
  else
  {
    result = 1.055f * powf(value, 1.0f / 2.4f) - 0.055f;
  }

  int32_t res = lroundf(clamp01f(result) * 255.0f);
  res = std::clamp<int32_t>(res, 0, 255);

  return res;
}

static RgbType dimmer(const uint32_t id)
{
  const RgbType& input = colors[id];
  const uint32_t& step = dimming_step[id];

  if (!step)
  {
    return input;
  }

  if (step >= 255)
  {
    return {0, 0, 0};
  }

  /* Convert "step" to brightness factor [0..1] where 1 = full, 0 = black */
  const float k = 1.0f - (static_cast<float>(step) / 255.0f);

  /* sRGB -> linear */
  float rl = srgb_to_linear(input.r);
  float gl = srgb_to_linear(input.g);
  float bl = srgb_to_linear(input.b);

  /* Scale brightness in linear domain */
  rl *= k;
  gl *= k;
  bl *= k;

  /* linear -> sRGB and pack to 8-bit */
  RgbType out;
  out.r = linear_to_srgb(rl);
  out.g = linear_to_srgb(gl);
  out.b = linear_to_srgb(bl);
  return out;
}

void update_dimming(const bool sensor_status, const uint32_t id)
{
  if (sensor_status)
  {
    dimming_step[id] = 1;
  }
  else
  {
    dimming_step[id] += DIMMING_INCREMENT;
  }
}

void stop_dimming(const RgbType& dimmed, const uint32_t id)
{
  if (dimmed.r == 0 && dimmed.g == 0 && dimmed.b == 0)
  {
    dimming_step[id] = 0;
  }
}

static void ws2812b_led_task(void* arg)
{
  // color composer https://www.figma.com/color-wheel/
  // ambient RGB: 50, 0, 10

  if (s_strip)
  {
    CHECK_ERR(led_strip_clear(s_strip));
  }

  for (;;)
  {
    if (s_strip)
    {
      if (!light_sensor_is_light())
      {
        const bool s1 = pir312_get_state(0); // left-left guard sensor
        const bool s2 = pir312_get_state(1); // left-left closet
        const bool s3 = pir312_get_state(2); // left-center closet
        const bool s4 = pir312_get_state(3); // right-center closet
        const bool s5 = pir312_get_state(4); // right-right closet
        const bool s6 = pir312_get_state(5); // right-rigth guard sensor
        const bool any_active = s1 || s2 || s3 || s4 || s5 || s6;

        if (any_active || dimming_step[4])
        {
          const uint32_t id = 4;
          update_dimming(any_active, id);
          const RgbType dimmed = dimmer(id);
          for (int i = 0; i < LED_COUNT; ++i)
          {
            CHECK_ERR(led_strip_set_pixel(s_strip, i, dimmed.r, dimmed.g, dimmed.b));
          }
          stop_dimming(dimmed, id);
        }
        else
        {
          CHECK_ERR(led_strip_clear(s_strip));
        }

        if (s2 || dimming_step[0])
        {
          const uint32_t id = 0;
          update_dimming(s2, id);
          const RgbType dimmed = dimmer(id);
          for (int i = 0; i < SEG_LENGTH; ++i)
          {
            CHECK_ERR(led_strip_set_pixel(s_strip, (id * SEG_LENGTH) + i, dimmed.r, dimmed.g, dimmed.b));
          }
          stop_dimming(dimmed, id);
        }
        if (s3 || dimming_step[1])
        {
          const uint32_t id = 1;
          update_dimming(s3, id);
          const RgbType dimmed = dimmer(id);
          for (int i = 0; i < SEG_LENGTH; ++i)
          {
            CHECK_ERR(led_strip_set_pixel(s_strip, (id * SEG_LENGTH) + i, dimmed.r, dimmed.g, dimmed.b));
          }
          stop_dimming(dimmed, id);
        }
        if (s4 || dimming_step[2])
        {
          const uint32_t id = 2;
          update_dimming(s4, id);
          const RgbType dimmed = dimmer(id);
          for (int i = 0; i < SEG_LENGTH; ++i)
          {
            CHECK_ERR(led_strip_set_pixel(s_strip, (id * SEG_LENGTH) + i, dimmed.r, dimmed.g, dimmed.b));
          }
          stop_dimming(dimmed, id);
        }
        if (s5 || dimming_step[3])
        {
          const uint32_t id = 3;
          update_dimming(s5, id);
          const RgbType dimmed = dimmer(id);
          for (int i = 0; i < SEG_LENGTH; ++i)
          {
            CHECK_ERR(led_strip_set_pixel(s_strip, (id * SEG_LENGTH) + i, dimmed.r, dimmed.g, dimmed.b));
          }
          stop_dimming(dimmed, id);
        }
      }
      else
      {
        CHECK_ERR(led_strip_clear(s_strip));
      }
      CHECK_ERR(led_strip_refresh(s_strip));
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // 0.1 sec
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
  //rmt_cfg.flags.with_dma = true;
  rmt_cfg.flags.with_dma = false;

  CHECK_ERR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
  ESP_LOGI(TAG, "INIT: LED strip created on GPIO %d (%d px)", LED_PIN, LED_COUNT);

  CHECK_ERR(led_strip_clear(s_strip));
  CHECK_ERR(led_strip_refresh(s_strip));

  CHECK_XTASK_OK(xTaskCreatePinnedToCore(ws2812b_led_task, "ws2812b_led_task", 4096, NULL, 5, NULL, 1));
  ESP_LOGI(TAG, "Initialization done.");
}
