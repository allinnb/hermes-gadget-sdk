// M5PM1 power-management driver for M5Stack StopWatch.
// The red key is internal to the PMIC; ESP32 GPIO12 is only its open-drain IRQ.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "driver/i2c_master.h"
#include "hg/app.hpp"
#include "hg/hal.hpp"

namespace hgp {

class M5PM1Power final : public hg::Power {
 public:
  static constexpr uint8_t kAddr = 0x6E;
  static constexpr uint32_t kSettingsHoldMs = 1500;

  // Configures the PMIC key and GPIO12 IRQ output. The driver still polls the
  // live PMIC key state, so key operation does not depend on IRQ delivery.
  bool begin(i2c_master_bus_handle_t bus);

  std::optional<hg::PowerStatus> read() override;
  bool power_off() override;
  std::optional<bool> external_power() override;

  // Run on the app task every loop. A short red-key press sleeps/wakes the
  // display; a 1.5 s hold opens or closes Settings. The PMIC retains its 4 s
  // emergency shutdown behavior.
  void poll(hg::App& app, uint32_t now_ms);
  // false on the initial press, true when the 1.5 s Settings hold fires.
  std::function<void(bool)> on_key_action;

 private:
  bool read8(uint8_t reg, uint8_t& value);
  bool read16(uint8_t reg, uint16_t& value);
  bool write8(uint8_t reg, uint8_t value);
  bool update8(uint8_t reg, uint8_t mask, uint8_t value);
  bool key_pressed(bool& pressed);
  void clear_irqs();

  i2c_master_dev_handle_t dev_ = nullptr;
  bool key_down_ = false;
  bool key_held_ = false;
  bool key_armed_ = false;
  uint32_t key_down_at_ = 0;
  uint32_t polled_at_ = 0;
};

}  // namespace hgp
