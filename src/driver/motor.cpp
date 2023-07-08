#include "motor.h"

#include <driver/gpio.h>
#include <bdc_motor.h>

#define BDC_MCPWM_TIMER_RESOLUTION_HZ 80'000'000 // 80MHz
#define BDC_MCPWM_FREQ_HZ 100'000                // 100kHz
#define BDC_MCPWM_DUTY_TICK_MAX (BDC_MCPWM_TIMER_RESOLUTION_HZ / BDC_MCPWM_FREQ_HZ)

#define AIN1 GPIO_NUM_42
#define AIN2 GPIO_NUM_41

#define BIN1 GPIO_NUM_40
#define BIN2 GPIO_NUM_38

namespace driver::motor
{
  static bdc_motor_handle_t bdc_position[NUMS] = {};
  static esp_err_t (*bdc_direction[])(bdc_motor_handle_t) = {
      bdc_motor_forward,
      bdc_motor_reverse,
  };

  void init()
  {
    bdc_motor_config_t motor_cfg = {};
    motor_cfg.pwm_freq_hz = BDC_MCPWM_FREQ_HZ;

    bdc_motor_mcpwm_config_t mcpwm_cfg = {};
    mcpwm_cfg.resolution_hz = BDC_MCPWM_TIMER_RESOLUTION_HZ;

    // left
    motor_cfg.pwma_gpio_num = AIN1;
    motor_cfg.pwmb_gpio_num = AIN2;
    mcpwm_cfg.group_id = 0;

    ESP_ERROR_CHECK(bdc_motor_new_mcpwm_device(&motor_cfg, &mcpwm_cfg, &bdc_position[LEFT]));

    // right
    motor_cfg.pwma_gpio_num = BIN1;
    motor_cfg.pwmb_gpio_num = BIN2;
    mcpwm_cfg.group_id = 1;

    ESP_ERROR_CHECK(bdc_motor_new_mcpwm_device(&motor_cfg, &mcpwm_cfg, &bdc_position[RIGHT]));

    // enable
    ESP_ERROR_CHECK(bdc_motor_enable(bdc_position[LEFT]));
    ESP_ERROR_CHECK(bdc_motor_enable(bdc_position[RIGHT]));

    brake();
  }

  void brake()
  {
    brake(LEFT);
    brake(RIGHT);
  }
  void brake(position pos)
  {
    ESP_ERROR_CHECK(bdc_motor_brake(bdc_position[pos]));
  }

  void coast()
  {
    coast(LEFT);
    coast(RIGHT);
  }
  void coast(position pos)
  {
    ESP_ERROR_CHECK(bdc_motor_coast(bdc_position[pos]));
  }

  void speed(position pos, float duty)
  {
    direction dir = duty < 0.0f ? REVERSE : FORWARD;
    ESP_ERROR_CHECK(bdc_direction[dir](bdc_position[pos]));
    ESP_ERROR_CHECK(bdc_motor_set_speed(bdc_position[pos], (uint32_t)(((float)(BDC_MCPWM_DUTY_TICK_MAX)) * duty)));
  }
}
