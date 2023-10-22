#include "encoder.hpp"

// C++
#include <numbers>

// ESP-IDF
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_intr_alloc.h>

// Project
#include "base.hpp"

namespace driver::hardware
{
  class Encoder::AS5050AImpl final : DriverBase
  {
  private:
    peripherals::Spi &spi_;
    int index_;
    uint8_t *tx_buffer_;
    uint8_t *rx_buffer_;
    uint16_t angle_;

    static constexpr size_t BUFFER_SIZE = 2;

    // 分解能あたりの角度
    static constexpr uint16_t RESOLUTION = 1024; // 10 bit
    static constexpr float RESOLUTION_PER_RADIAN = (2.0f * std::numbers::pi_v<float>) / static_cast<float>(RESOLUTION);
    static constexpr float RESOLUTION_PER_DEGREE = 360.f / static_cast<float>(RESOLUTION);

    // 送信データにパリティを付与
    static constexpr uint16_t REG_MASTER_RESET = 0x33A5;
    static constexpr uint16_t REG_ANGULAR_DATA = 0x3FFF;

    static constexpr uint16_t command_frame(uint16_t reg, bool is_reading)
    {
      reg = (reg << 1) | (is_reading ? 0x8000 : 0x0000);
      return (reg | __builtin_parity(reg));
    }
    static bool verify_frame(uint16_t res)
    {
      bool parity_ok = (res & 0x0001) == __builtin_parity(res >> 1);
      bool command_ok = (res & 0x0002) != 0x0002;
      return parity_ok && command_ok;
    }

  public:
    explicit AS5050AImpl(peripherals::Spi &spi, gpio_num_t spics_io_num) : spi_(spi), angle_(0)
    {
      // 転送用バッファを確保
      tx_buffer_ = reinterpret_cast<uint8_t *>(heap_caps_calloc(BUFFER_SIZE, sizeof(uint8_t), MALLOC_CAP_DMA));
      rx_buffer_ = reinterpret_cast<uint8_t *>(heap_caps_calloc(BUFFER_SIZE, sizeof(uint8_t), MALLOC_CAP_DMA));

      // デバイスを追加
      index_ = spi_.add(0, 0, 1, SPI_MASTER_FREQ_10M, spics_io_num, 1);
      auto trans = spi_.transaction(index_);
      // リセット
      trans->flags = 0;
      trans->tx_buffer = tx_buffer_;
      trans->rx_buffer = rx_buffer_;
      trans->length = 16;
      tx_buffer_[0] = command_frame(REG_MASTER_RESET, false) >> 8;
      tx_buffer_[1] = command_frame(REG_MASTER_RESET, false) & 0xFF;
      spi_.transmit(index_);

      // 角度を取得
      tx_buffer_[0] = command_frame(REG_ANGULAR_DATA, true) >> 8;
      tx_buffer_[1] = command_frame(REG_ANGULAR_DATA, true) & 0xFF;
      spi_.transmit(index_);
    }
    ~AS5050AImpl()
    {
      free(tx_buffer_);
      free(rx_buffer_);
    }

    bool update() override
    {
      bool ret = spi_.transmit(index_);
      uint16_t res = rx_buffer_[0] << 8 | rx_buffer_[1];
      if (verify_frame(res))
        angle_ = (res >> 2) & 0x3FF;

      return ret;
    }

    [[nodiscard]] float radian() const
    {
      return static_cast<float>(angle_) * RESOLUTION_PER_RADIAN;
    }

    [[nodiscard]] float degree() const
    {
      return static_cast<float>(angle_) * RESOLUTION_PER_DEGREE;
    }
  };

  Encoder::Encoder(peripherals::Spi &spi, gpio_num_t spics_io_num) : impl_(new AS5050AImpl(spi, spics_io_num))
  {
  }
  Encoder::~Encoder() = default;

  bool Encoder::update()
  {
    return impl_->update();
  }
  float Encoder::radian()
  {
    return impl_->radian();
  }
  float Encoder::degree()
  {
    return impl_->degree();
  }
}
