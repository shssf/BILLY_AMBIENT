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

static volatile int pir_state[PIR_COUNT];
static volatile uint64_t ambient = 0;
static volatile uint64_t box_left = 0;
static volatile uint64_t box_left_center = 0;
static volatile uint64_t box_right_center = 0;
static volatile uint64_t box_right = 0;

int pir312_count(void)
{
  return PIR_COUNT;
}

static void IRAM_ATTR pir_isr(void* arg)
{
  const int index = (int)arg;
  const int level = gpio_get_level(pir_pins[index]);
  pir_state[index] = level;
  uint64_t cur_time = esp_timer_get_time();

  if (level > 0)
  {
    ambient = cur_time;

    if (index == 1)
    {
      box_left = cur_time;
    }
    if (index == 2)
    {
      box_left_center = cur_time;
    }
    if (index == 3)
    {
      box_right_center = cur_time;
    }
    if (index == 4)
    {
      box_right = cur_time;
    }
  }
}

extern "C" void pir312_init(void)
{
  CHECK_ERR(gpio_install_isr_service(0));

  for (int i = 0; i < pir312_count(); ++i)
  {
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

int pir312_get_state(int index)
{
  return (index < pir312_count()) ? pir_state[index] : 0;
}

uint64_t pir312_get_ambient(void)
{
  return ((esp_timer_get_time() - ambient) < TIMEOUT_US) ? 1 : 0;
}

uint64_t pir312_get_box_left(void)
{
  return ((esp_timer_get_time() - box_left) < TIMEOUT_US) ? 1 : 0;
}

uint64_t pir312_get_box_left_center(void)
{
  return ((esp_timer_get_time() - box_left_center) < TIMEOUT_US) ? 1 : 0;
}

uint64_t pir312_get_box_right_center(void)
{
  return ((esp_timer_get_time() - box_right_center) < TIMEOUT_US) ? 1 : 0;
}

uint64_t pir312_get_box_right(void)
{
  return ((esp_timer_get_time() - box_right) < TIMEOUT_US) ? 1 : 0;
}

extern "C" void pir312_dump_status()
{
  ESP_LOGI(TAG,
           "[%d,%d,%d,%d,%d,%d], ambient=%llu, boxes: L=%llu, LC=%llu, RC=%llu, R=%llu",
           pir312_get_state(0),
           pir312_get_state(1),
           pir312_get_state(2),
           pir312_get_state(3),
           pir312_get_state(4),
           pir312_get_state(5),
           pir312_get_ambient(),
           pir312_get_box_left(),
           pir312_get_box_left_center(),
           pir312_get_box_right_center(),
           pir312_get_box_right());
}
