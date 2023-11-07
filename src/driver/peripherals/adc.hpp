#pragma once

// C++
#include <memory>

// ESP-IDF
#include <hal/adc_types.h>

namespace driver::peripherals
{
  class Adc
  {
  private:
    class AdcImpl;
    std::unique_ptr<AdcImpl> impl_;

  public:
    explicit Adc(adc_unit_t unit, adc_channel_t channel);
    ~Adc();

    int read();
    int read_isr();
    int to_voltage();
  };
}
