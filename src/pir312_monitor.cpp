#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdio.h>

#include "pir312_monitor.h"
#include "utils.h"

static const char* TAG = "PIR AM312";

#define PIR_COUNT  6
#define TIMEOUT_US (10LL * 1000000LL) // 10sec // 5 min

static const gpio_num_t pir_pins[PIR_COUNT] = {
    GPIO_NUM_27,
    GPIO_NUM_16,
    GPIO_NUM_18,
    GPIO_NUM_19,
    GPIO_NUM_23,
    GPIO_NUM_17,
};

static volatile uint64_t pir_state[PIR_COUNT];

int pir312_count(void)
{
  return PIR_COUNT;
}

static void IRAM_ATTR pir_isr(void* arg)
{
  const int index = (int)arg;
  const int level = gpio_get_level(pir_pins[index]);
  uint64_t cur_time = esp_timer_get_time();

  if (level > 0)
  {
    pir_state[index] = cur_time;
  }
}

extern "C" void pir312_init(void)
{
  CHECK_ERR(gpio_install_isr_service(0));

  uint64_t cur_time = esp_timer_get_time();
  for (int i = 0; i < pir312_count(); ++i)
  {
    // init
    pir_state[i] = cur_time;

    //create handlers
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pir_pins[i];
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.intr_type = GPIO_INTR_ANYEDGE;
    CHECK_ERR(gpio_config(&cfg));
    CHECK_ERR(gpio_isr_handler_add(pir_pins[i], pir_isr, (void*)i));
  }

  ESP_LOGI(TAG, "pir312_init done.");
}

bool pir312_get_state(int index)
{
  bool result = false;

  if (index < pir312_count())
  {
    if ((esp_timer_get_time() - pir_state[index]) < TIMEOUT_US)
    {
      result = true;
    }
  }

  return result;
}

extern "C" void pir312_dump_status()
{
  ESP_LOGI(TAG,
           "[%d,%d,%d,%d,%d,%d]",
           pir312_get_state(0),
           pir312_get_state(1),
           pir312_get_state(2),
           pir312_get_state(3),
           pir312_get_state(4),
           pir312_get_state(5));
}
