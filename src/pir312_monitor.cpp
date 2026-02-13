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
#define TIMEOUT_US (300LL * 1000000LL) // 5 min

static const gpio_num_t pir_pins[PIR_COUNT] = {
    GPIO_NUM_17,
    GPIO_NUM_23,
    GPIO_NUM_19,
    GPIO_NUM_18,
    GPIO_NUM_16,
    GPIO_NUM_27,
};

static volatile int pir_state[PIR_COUNT];
static volatile int ambient = 0;
static volatile int box_left = 0;
static volatile int box_left_center = 0;
static volatile int box_right_center = 0;
static volatile int box_right = 0;

static esp_timer_handle_t ambient_timer = NULL;
static esp_timer_handle_t box_timers[4] = {NULL};

static void reset_var(void* arg)
{
  volatile int* var = (volatile int*)arg;
  *var = 0;
}

// Avoid CHECK_ERR here: may be called from ISR; logging is not ISR-safe
static void reset_timer(esp_timer_handle_t timer, volatile int* var)
{
  if (timer)
  {
    CHECK_ERR(esp_timer_stop(timer));
    CHECK_ERR(esp_timer_start_once(timer, TIMEOUT_US));
  }
  *var = 1;
}

int pir312_count(void)
{
  return PIR_COUNT;
}

static void IRAM_ATTR pir_isr(void* arg)
{
  const int index = (int)arg;
  const int level = gpio_get_level(pir_pins[index]);
  pir_state[index] = level;

  // Treat motion as level == 0 (keep original logic)
  if (level == 0 && 0)
  {
    reset_timer(ambient_timer, &ambient);
    if (index == 0 || index == 1)
    {
      reset_timer(box_timers[0], &box_left);
    }
    else if (index == 2)
    {
      reset_timer(box_timers[1], &box_left_center);
    }
    else if (index == 3)
    {
      reset_timer(box_timers[2], &box_right_center);
    }
    else if (index == 4 || index == 5)
    {
      reset_timer(box_timers[3], &box_right);
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

  esp_timer_create_args_t args = {};
  args.arg = (void*)&ambient;
  args.callback = reset_var;
  args.name = "var_ambient";
  CHECK_ERR(esp_timer_create(&args, &ambient_timer));

  const char* box_names[4] = {"left", "left_center", "right_center", "right"};
  volatile int* box_vars[4] = {&box_left, &box_left_center, &box_right_center, &box_right};
  for (int i = 0; i < 4; ++i)
  {
    args.name = box_names[i];
    args.arg = (void*)box_vars[i];
    CHECK_ERR(esp_timer_create(&args, &box_timers[i]));
  }

  CHECK_ERR(esp_timer_dump(stdout));
  ESP_LOGI(TAG, "pir312_init done.");
}

int pir312_get_state(int index)
{
  return (index < pir312_count()) ? pir_state[index] : 0;
}

// Public getters used by other modules
uint8_t pir312_get_ambient(void)
{
  return (uint8_t)ambient;
}

uint8_t pir312_get_box_left(void)
{
  return (uint8_t)box_left;
}

uint8_t pir312_get_box_left_center(void)
{
  return (uint8_t)box_left_center;
}

uint8_t pir312_get_box_right_center(void)
{
  return (uint8_t)box_right_center;
}

uint8_t pir312_get_box_right(void)
{
  return (uint8_t)box_right;
}

extern "C" void pir312_dump_status()
{
  ESP_LOGI(TAG,
           "[%d,%d,%d,%d,%d,%d], ambient=%u, boxes: L=%u, LC=%u, RC=%u, R=%u",
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
