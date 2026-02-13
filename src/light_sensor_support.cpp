#include <esp_adc/adc_oneshot.h>

#include "light_sensor_support.h"
#include "utils.h"

static const char* TAG = "Light Sensor";
static const int light_threshold = 2000; // limits are: 900 with light, 4095 with dark
static const adc_channel_t channel = ADC_CHANNEL_6;
static adc_oneshot_unit_handle_t handle = nullptr;

void light_sensor_init()
{
  if (handle)
  {
    ESP_LOGI(TAG, "Already initialized.");
    return;
  }

  adc_oneshot_unit_init_cfg_t init_cfg = {};
  init_cfg.unit_id = ADC_UNIT_1;

  CHECK_ERR(adc_oneshot_new_unit(&init_cfg, &handle));

  adc_oneshot_chan_cfg_t chan_cfg = {};
  chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  chan_cfg.atten = ADC_ATTEN_DB_12;

  CHECK_ERR(adc_oneshot_config_channel(handle, channel, &chan_cfg));

  ESP_LOGI(TAG, "Initialization done.");
}

static inline int read_sensor()
{
  int result = 0;

  if (handle)
  {
    int avg_iter = 8;
    uint64_t avg_sum = 0;
    int good_count = 0;
    while (avg_iter)
    {
      int raw_data = 0;
      esp_err_t status = adc_oneshot_read(handle, channel, &raw_data);
      CHECK_ERR(status);
      if (status == ESP_OK)
      {
        avg_sum += raw_data;
        ++good_count;
      }
      --avg_iter;
    }
    result = good_count ? avg_sum / good_count : 0;
  }

  return result;
}

bool light_sensor_is_light()
{
    const int raw = read_sensor();
    return raw < light_threshold;
}

extern "C" void light_sensor_dump(void)
{
  const int avg = read_sensor();
  const float volts = (avg / 4095.0f) * 3.3f;

  ESP_LOGI(TAG, "light=%d (%.3fV) (ADC1_CH6, atten=12dB), ON=%d", avg, volts, !light_sensor_is_light());
}
