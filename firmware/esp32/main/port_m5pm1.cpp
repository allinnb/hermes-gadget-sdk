// M5PM1 power-management driver for M5Stack StopWatch.
// Register facts are from M5PM1 v1.0.6 and M5Stack's StopWatch BSP.
#include "port_m5pm1.hpp"

#include "driver/gpio.h"
#include "esp_log.h"

namespace hgp {
namespace {

constexpr char kTag[] = "hg.m5pm1";
constexpr gpio_num_t kIrqGpio = GPIO_NUM_12;

constexpr uint8_t kRegPowerCfg = 0x06;
constexpr uint8_t kRegI2cCfg = 0x09;
constexpr uint8_t kRegWatchdog = 0x0A;
constexpr uint8_t kRegSystemCommand = 0x0C;
constexpr uint8_t kRegGpioOpenDrain = 0x13;
constexpr uint8_t kRegGpioFunction0 = 0x16;
constexpr uint8_t kRegBatteryMv = 0x22;
constexpr uint8_t kRegVinMv = 0x24;
constexpr uint8_t kRegIrqStatus1 = 0x40;
constexpr uint8_t kRegIrqStatus2 = 0x41;
constexpr uint8_t kRegIrqStatus3 = 0x42;
constexpr uint8_t kRegIrqMask1 = 0x43;
constexpr uint8_t kRegIrqMask2 = 0x44;
constexpr uint8_t kRegIrqMask3 = 0x45;
constexpr uint8_t kRegButtonStatus = 0x48;
constexpr uint8_t kRegButtonCfg1 = 0x49;

constexpr uint8_t kPowerLed = 1u << 4;
constexpr uint8_t kButtonPressed = 1u << 0;
constexpr uint8_t kSingleResetDisable = 1u << 0;
constexpr uint8_t kClickDelayMask = 0x06;
constexpr uint8_t kLongDelayMask = 0x18;
constexpr uint8_t kLongDelay4s = 0x18;
constexpr uint8_t kDoubleDelayMask = 0x60;
constexpr uint8_t kDoubleDelay250ms = 0x20;
constexpr uint8_t kIrqPin = 1u << 1;
constexpr uint8_t kGpio1FunctionMask = 0x0C;
constexpr uint8_t kGpio1IrqFunction = 0x04;
constexpr uint8_t kButtonIrqAll = 0x07;
constexpr uint8_t kButtonWakeIrq = 1u << 1;
constexpr uint8_t kShutdown = 0xA1;
constexpr uint32_t kPollMs = 20;

}  // namespace

bool M5PM1Power::read8(uint8_t reg, uint8_t& value) {
  return dev_ && i2c_master_transmit_receive(dev_, &reg, 1, &value, 1, 50) == ESP_OK;
}

bool M5PM1Power::read16(uint8_t reg, uint16_t& value) {
  uint8_t data[2]{};
  if (!dev_ || i2c_master_transmit_receive(dev_, &reg, 1, data, sizeof(data), 50) != ESP_OK) return false;
  value = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  return true;
}

bool M5PM1Power::write8(uint8_t reg, uint8_t value) {
  const uint8_t data[] = {reg, value};
  return dev_ && i2c_master_transmit(dev_, data, sizeof(data), 50) == ESP_OK;
}

bool M5PM1Power::update8(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t current = 0;
  if (!read8(reg, current)) return false;
  const uint8_t next = static_cast<uint8_t>((current & ~mask) | (value & mask));
  return next == current || write8(reg, next);
}

void M5PM1Power::clear_irqs() {
  for (uint8_t reg = kRegIrqStatus1; reg <= kRegIrqStatus3; ++reg) write8(reg, 0);
}

bool M5PM1Power::begin(i2c_master_bus_handle_t bus) {
  if (!bus || i2c_master_probe(bus, kAddr, 50) != ESP_OK) return false;

  i2c_device_config_t cfg{};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = kAddr;
  cfg.scl_speed_hz = 100000;
  if (i2c_master_bus_add_device(bus, &cfg, &dev_) != ESP_OK) return false;

  gpio_config_t irq{};
  irq.pin_bit_mask = 1ULL << kIrqGpio;
  irq.mode = GPIO_MODE_INPUT;
  irq.pull_up_en = GPIO_PULLUP_ENABLE;
  if (gpio_config(&irq) != ESP_OK) return false;

  // The PMIC keeps configuration over an ESP32 reset. Restore known-safe key
  // behavior on every boot: no single-click reset, software hold at 1.5 s,
  // and the PMIC's emergency long action delayed to 4 s.
  bool ok = write8(kRegI2cCfg, 0) && write8(kRegWatchdog, 0);
  ok = ok && update8(kRegButtonCfg1,
                     kSingleResetDisable | kClickDelayMask | kLongDelayMask | kDoubleDelayMask,
                     kSingleResetDisable | kLongDelay4s | kDoubleDelay250ms);
  ok = ok && update8(kRegIrqMask1, 0x1F, 0x1F);
  ok = ok && update8(kRegIrqMask2, 0x3F, 0x3F);
  ok = ok && update8(kRegIrqMask3, kButtonIrqAll, kButtonWakeIrq);
  ok = ok && update8(kRegGpioOpenDrain, kIrqPin, kIrqPin);
  ok = ok && update8(kRegGpioFunction0, kGpio1FunctionMask, kGpio1IrqFunction);
  clear_irqs();

  bool down = false;
  if (!ok || !key_pressed(down)) {
    ESP_LOGE(kTag, "M5PM1 initialization failed");
    return false;
  }
  key_down_ = down;
  key_armed_ = !down;
  ESP_LOGI(kTag, "M5PM1 ready; red key=%s, IRQ GPIO12=%d", down ? "down" : "up", gpio_get_level(kIrqGpio));
  return true;
}

bool M5PM1Power::key_pressed(bool& pressed) {
  uint8_t status = 0;
  if (!read8(kRegButtonStatus, status)) return false;
  pressed = (status & kButtonPressed) != 0;
  return true;
}

void M5PM1Power::poll(hg::App& app, uint32_t now_ms) {
  if (!dev_ || now_ms - polled_at_ < kPollMs) return;
  polled_at_ = now_ms;

  bool down = false;
  if (!key_pressed(down)) return;
  if (!key_armed_) {
    key_down_ = down;
    if (!down) key_armed_ = true;
    return;
  }

  if (down && !key_down_) {
    key_down_ = true;
    key_held_ = false;
    key_down_at_ = now_ms;
    ESP_LOGI(kTag, "red key down");
  } else if (down && !key_held_ && now_ms - key_down_at_ >= kSettingsHoldMs) {
    key_held_ = true;
    if (app.settings_open()) {
      app.close_settings();
      ESP_LOGI(kTag, "red key hold: Settings closed");
    } else if (app.open_settings()) {
      ESP_LOGI(kTag, "red key hold: Settings opened");
    }
  } else if (!down && key_down_) {
    key_down_ = false;
    if (!key_held_) {
      app.on_power_key();
      ESP_LOGI(kTag, "red key short press: display toggled");
    }
  }

  // Reading the live key can leave a completed click latched on IRQ. Clear it
  // after release; the driver derives gestures from the live state itself.
  if (!down && gpio_get_level(kIrqGpio) == 0) clear_irqs();
}

std::optional<hg::PowerStatus> M5PM1Power::read() {
  uint16_t battery_mv = 0, vin_mv = 0;
  if (!read16(kRegBatteryMv, battery_mv) || !read16(kRegVinMv, vin_mv)) return std::nullopt;
  hg::PowerStatus status;
  if (battery_mv > 2000 && battery_mv < 5000) {
    status.battery_present = true;
    status.battery_mv = battery_mv;
  } else {
    status.battery_present = false;
  }
  status.external_power = vin_mv > 4000;
  return status;
}

std::optional<bool> M5PM1Power::external_power() {
  uint16_t vin_mv = 0;
  if (!read16(kRegVinMv, vin_mv)) return std::nullopt;
  return vin_mv > 4000;
}

bool M5PM1Power::power_off() {
  ESP_LOGI(kTag, "requesting M5PM1 shutdown");
  // Keep the LED off if USB continues to power the board.
  update8(kRegPowerCfg, kPowerLed, 0);
  return write8(kRegSystemCommand, kShutdown);
}

}  // namespace hgp
