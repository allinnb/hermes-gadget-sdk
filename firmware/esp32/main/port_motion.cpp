// The motion task: reads the board's accelerometer at 10 Hz on the shared I2C
// bus, maps the axes into the device frame and keeps EMA-smoothed milli-g in
// atomics. It posts no events: the app task polls samples() and forwards only
// new values, so a sensor that stops answering goes stale in the core.
#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include <algorithm>
#include <cmath>

#include "esp_log.h"
#include "freertos/task.h"

namespace hgp {
namespace {

const char* TAG = "hg.motion";
constexpr uint32_t kPeriodMs = 100;  // 10 Hz, inside the core's 5-25 Hz window

}  // namespace

bool MotionInput::begin(const ImuConfig& cfg, i2c_master_bus_handle_t bus) {
  if (!bus || !cfg.enabled) return false;
  cfg_ = cfg;
  if (i2c_master_probe(bus, cfg.addr, 50) != ESP_OK) {
    ESP_LOGW(TAG, "no IMU at 0x%02x; face-down sleep stays off", cfg.addr);
    return false;
  }

  if (cfg.chip == ImuChip::Qmi8658) {
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = cfg.addr;
    dev.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &dev, &dev_) != ESP_OK) return false;
    qmi_ = std::make_unique<hg::Qmi8658>(
        [this](uint8_t reg, uint8_t* data, size_t size) {
          return i2c_master_transmit_receive(dev_, &reg, 1, data, size, 50) == ESP_OK;
        },
        [this](uint8_t reg, uint8_t value) {
          const uint8_t buf[2] = {reg, value};
          return i2c_master_transmit(dev_, buf, sizeof(buf), 50) == ESP_OK;
        },
        // The reset's 15 ms boot time. pdMS_TO_TICKS rounds down (one tick at
        // 100 Hz) and the current tick is partly gone: two more ticks make the
        // wait at least `ms`.
        [](uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms) + 2); });
    if (!qmi_->begin()) {
      qmi_.reset();
      i2c_master_bus_rm_device(dev_);
      dev_ = nullptr;
      ESP_LOGW(TAG, "the QMI8658 at 0x%02x answered but did not start", cfg.addr);
      return false;
    }
    name_ = "qmi8658";
  } else {
#if CONFIG_HG_BOARD_M5STACK_STOPWATCH
    bmi270_driver_config_t driver = {};
    driver.addr = cfg.addr;
    driver.interface = BMI270_USE_I2C;
    driver.i2c_bus = bus;
    if (bmi270_create(&driver, &bmi_) != ESP_OK || !bmi_) {
      bmi_ = nullptr;
      ESP_LOGW(TAG, "the BMI270 at 0x%02x answered but did not initialise", cfg.addr);
      return false;
    }
    bmi270_config_t sensor = {};
    sensor.acce_odr = BMI270_ACC_ODR_25_HZ;
    sensor.acce_range = BMI270_ACC_RANGE_4_G;
    sensor.gyro_odr = BMI270_GYR_ODR_25_HZ;
    sensor.gyro_range = BMI270_GYR_RANGE_500_DPS;
    if (bmi270_start(bmi_, &sensor) != ESP_OK) {
      bmi270_delete(bmi_);
      bmi_ = nullptr;
      ESP_LOGW(TAG, "the BMI270 at 0x%02x did not start", cfg.addr);
      return false;
    }
    name_ = "bmi270";
#else
    ESP_LOGW(TAG, "BMI270 is only enabled in the StopWatch build");
    return false;
#endif
  }

  if (xTaskCreate(&MotionInput::task, "hg-motion", 3072, this, 5, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "could not start the motion task; face-down sleep stays off");
    qmi_.reset();
    if (dev_) {
      i2c_master_bus_rm_device(dev_);
      dev_ = nullptr;
    }
#if CONFIG_HG_BOARD_M5STACK_STOPWATCH
    if (bmi_) {
      bmi270_stop(bmi_);
      bmi270_delete(bmi_);
      bmi_ = nullptr;
    }
#endif
    name_ = "none";
    return false;
  }
  ESP_LOGI(TAG, "%s ready", name_);
  return true;
}

bool MotionInput::read(int16_t mg[3]) {
  if (qmi_) return qmi_->read(mg);
#if CONFIG_HG_BOARD_M5STACK_STOPWATCH
  if (bmi_) {
    float g[3] = {};
    if (bmi270_get_acce_data(bmi_, &g[0], &g[1], &g[2]) != ESP_OK) return false;
    for (int i = 0; i < 3; ++i) {
      const long value = std::lround(g[i] * 1000.0f);
      mg[i] = static_cast<int16_t>(std::clamp<long>(value, INT16_MIN, INT16_MAX));
    }
    return true;
  }
#endif
  return false;
}

void MotionInput::latest(int16_t& x, int16_t& y, int16_t& z) const {
  x = x_.load(std::memory_order_relaxed);
  y = y_.load(std::memory_order_relaxed);
  z = z_.load(std::memory_order_relaxed);
}

void MotionInput::task(void* arg) {
  auto* self = static_cast<MotionInput*>(arg);
  int32_t smooth[3] = {0, 0, 0};
  bool have = false;
  for (;;) {
    int16_t mg[3];
    if (self->read(mg)) {
      const int16_t src[3] = {mg[0], mg[1], mg[2]};
      const int8_t from[3] = {self->cfg_.x_from, self->cfg_.y_from, self->cfg_.z_from};
      const int8_t sign[3] = {self->cfg_.x_sign, self->cfg_.y_sign, self->cfg_.z_sign};
      for (int i = 0; i < 3; ++i) {
        const int32_t mapped = src[from[i]] * sign[i];
        smooth[i] = have ? (smooth[i] + mapped) / 2 : mapped;  // EMA, alpha 0.5
      }
      self->x_.store(static_cast<int16_t>(smooth[0]), std::memory_order_relaxed);
      self->y_.store(static_cast<int16_t>(smooth[1]), std::memory_order_relaxed);
      self->z_.store(static_cast<int16_t>(smooth[2]), std::memory_order_relaxed);
      self->samples_.fetch_add(1, std::memory_order_release);  // publishes the sample above
      have = true;
    }  // a failed read skips the sample
    vTaskDelay(pdMS_TO_TICKS(kPeriodMs));
  }
}

}  // namespace hgp
