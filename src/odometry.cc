#include "odometry.h"

// C++
#include <cmath>
#include <limits>

// Project
#include "config.h"
#include "driver/driver.h"

/**
 * @brief マウスの自己位置を推定する。
 * 速度はエンコーダーから算出。
 * 加速度はIMUから取得。
 * 角速度はIMUから取得。
 * 角加速度はIMUから算出。
 * @details
 * 以下の記事を参考にした。
 * https://www.mech.tohoku-gakuin.ac.jp/rde/contents/course/robotics/wheelrobot.html
 * https://rikei-tawamure.com/entry/2020/05/22/232227
 */
namespace odometry {
/**
 * @brief 車輪から得られる車体情報を管理する
 */
class Wheel {
 private:
  //! 車輪の直径 [mm]
  const float tire_diameter_;

  //! エンコーダーの回転方向を反転するか
  const bool invert_;

  //! 初期化後かどうか
  bool reset_{true};

  //! エンコーダーの分解能
  uint16_t resolution_{0};

  //! エンコーダーの分解能の半分
  uint16_t resolution_half_{0};

  //! 分解能あたりの角度 [rad]
  float angle_per_resolution_{0};

  //! 一つ前の観測角度
  uint16_t previous_{0};

  //! 車輪の角加速度 [rad/s^2]
  float angular_acceleration_{0.0f};

  //! 車輪の角速度 [rad/s]
  float angular_velocity_{0.0f};

  //! 車輪位置の移動速度 [mm/s]
  float velocity_{0.0f};

  /**
   * @brief 車輪エンコーダーの更新周期の差分を計算し角速度に変換する。
   * @param current 最新の観測角度 [rad]
   * @param delta_us 更新周期 [us]
   * @return 車輪の角速度 [rad/s]
   */
  [[nodiscard]] float calculate_angular_velocity(uint16_t current,
                                                 uint32_t delta_us) const {
    // センサ値更新間隔(delta_us)での観測値の変化量を計算
    auto delta = current - previous_;
    if (std::abs(delta) >= resolution_half_) {
      if (previous_ >= resolution_half_) {
        delta += resolution_;
      } else {
        delta -= resolution_;
      }
    }
    // 角度に変換
    auto angle = static_cast<float>(delta) * angle_per_resolution_;
    // 1msでの変化量に換算する
    return (angle / static_cast<float>(delta_us)) * 1000'000.0f;
  }

 public:
  /**
   * @param resolution エンコーダーの分解能
   * @param tire_diameter 車輪の直径 [mm]
   * @param invert エンコーダーの回転方向を反転する
   */
  explicit Wheel(uint16_t resolution, float tire_diameter, bool invert)
      : tire_diameter_(tire_diameter),
        invert_(invert),
        resolution_(resolution),
        resolution_half_(resolution / 2),
        angle_per_resolution_((2.0f * std::numbers::pi_v<float>) /
                              static_cast<float>(resolution)) {}
  ~Wheel() = default;

  /**
   * @brief 車輪情報を更新する
   * @param current 最新の観測角度
   * @param delta_us 更新周期 [us]
   */
  void update(uint16_t current, uint32_t delta_us) {
    // 回転方向を反転
    if (invert_) {
      current = resolution_ - current;
    }
    if (reset_) [[unlikely]] {
      previous_ = current;
      reset_ = false;
    }

    auto angular_velocity = calculate_angular_velocity(current, delta_us);
    angular_acceleration_ = (angular_velocity - angular_acceleration_) /
                            static_cast<float>(delta_us) * 1000'000.0f;
    velocity_ = angular_velocity * (tire_diameter_ / 2.0f);
    angular_velocity_ = angular_velocity;
    previous_ = current;
  }

  /**
   * @brief リセット
   */
  void reset() {
    reset_ = true;
    angular_velocity_ = 0.0f;
    angular_acceleration_ = 0.0f;
    velocity_ = 0.0f;
  }

  [[nodiscard]] float angular_velocity() const { return angular_velocity_; }
  [[nodiscard]] float angular_acceleration() const {
    return angular_acceleration_;
  }
  [[nodiscard]] float velocity() const { return velocity_; }
};

class Odometry::OdometryImpl {
 private:
  //! センサ値を取得するためのドライバクラス
  driver::Driver &dri_;

  //! 車輪間隔
  const float wheel_track_width_;

  //! 車輪
  Wheel left_, right_;
  WheelsPair wheel_ang_accel_{};
  WheelsPair wheel_ang_vel_{};
  WheelsPair wheel_vel_{};

  //! 車体並進速度 [mm/s]
  float velocity_{0.0f};

  //! 車体角速度 [rad/s]
  float angular_velocity_{0.0f};

  //! 車体角度 [rad]
  float angle_{0.0f};

  //! 車体位置 [mm]
  float x_{0.0f}, y_{0.0f};

 public:
  explicit OdometryImpl(driver::Driver &dri, config::Config &conf)
      : dri_(dri),
        wheel_track_width_(conf.wheel_track_width),
        left_(dri_.encoder_left->resolution(), conf.tire_diameter, false),
        right_(dri_.encoder_right->resolution(), conf.tire_diameter, true) {}
  ~OdometryImpl() = default;

  /**
   * @brief リセット
   */
  void reset() {
    // 左
    left_.reset();
    // 右
    right_.reset();
    // 車体
    angle_ = 0.0f;
    x_ = 0.0f;
    y_ = 0.0f;
  }

  /**
   * @brief 車体情報を更新する
   * @param delta_us 更新周期
   */
  void update(uint32_t delta_us) {
    // 左
    left_.update(dri_.encoder_left->raw(), delta_us);
    // 右
    right_.update(dri_.encoder_right->raw(), delta_us);

    wheel_ang_vel_.left = left_.angular_velocity();
    wheel_ang_accel_.left = left_.angular_acceleration();
    wheel_ang_vel_.right = right_.angular_velocity();
    wheel_ang_accel_.right = right_.angular_acceleration();

    // 中心速度 [mm/s]
    wheel_vel_.left = left_.velocity();
    wheel_vel_.right = right_.velocity();
    velocity_ = (wheel_vel_.left + wheel_vel_.right) / 2.0f;

    // 中心角速度 [rad/s]
    angular_velocity_ =
        (wheel_vel_.left - wheel_vel_.right) / wheel_track_width_;
    // 中心角度 [rad]
    auto angle = angle_ + angular_velocity_ / 1000.0f;

    // x, yの位置を推定
    if (std::fabs(wheel_vel_.left - wheel_vel_.right) <=
        std::numeric_limits<float>::epsilon()) {
      // 直線運動
      auto a = velocity_ * static_cast<float>(delta_us) / 1000'000.0f;
      x_ += a * std::cos(angle);
      y_ += a * std::sin(angle);
    } else {
      // 曲線近似
      auto delta = (angle - angle_) / 2.0f;
      auto a = 2.0f * velocity_ / angular_velocity_ * std::sin(delta);
      auto b = angle + delta;
      x_ += a * std::cos(b);
      y_ += a * std::sin(b);
    }
    angle_ = angle;
  }

  const WheelsPair &wheels_angular_acceleration() { return wheel_ang_accel_; }
  const WheelsPair &wheels_angular_velocity() { return wheel_ang_vel_; }
  const WheelsPair &wheels_velocity() { return wheel_vel_; }
  [[nodiscard]] float angle() const { return angle_; }
  [[nodiscard]] float x() const { return x_; }
  [[nodiscard]] float y() const { return y_; }
};

Odometry::Odometry(driver::Driver &dri, config::Config &conf)
    : impl_(new OdometryImpl(dri, conf)) {}
Odometry::~Odometry() = default;

void Odometry::reset() { return impl_->reset(); }
void Odometry::update(uint32_t delta_us) { return impl_->update(delta_us); }

float Odometry::angle() { return impl_->angle(); }
float Odometry::x() { return impl_->x(); }
float Odometry::y() { return impl_->y(); }

const WheelsPair &Odometry::wheels_angular_velocity() {
  return impl_->wheels_angular_velocity();
}
const WheelsPair &Odometry::wheels_velocity() {
  return impl_->wheels_velocity();
}
const WheelsPair &Odometry::wheels_angular_acceleration() {
  return impl_->wheels_angular_acceleration();
}
}  // namespace odometry