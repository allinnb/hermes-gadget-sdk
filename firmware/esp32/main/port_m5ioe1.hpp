// M5IOE1 I/O expander driver for M5Stack StopWatch.
// Address: 0x4F on I2C_NUM_0 (GPIO47 SDA, GPIO48 SCL).
// Protocol: 16-bit register space over standard I2C.
// Pin label convention: M5Stack labels pins PYG1–PYG16; the register bit index
// is (PYG_label - 1), i.e.  IOE_PIN(n) = BIT(n-1).
//
// Registers used here (from M5IOE1 datasheet + M5Unified M5IOE1_Class.inl):
//   0x03  MODE   — direction: 1 = output, 0 = input  (16-bit, write both bytes)
//   0x05  OUT    — output latch value
//   0x07  IN     — read actual pin levels
//   0x13  OD     — open-drain enable
//   0x1B  PWM1_DUTY — vibration motor PWM (bit15 = enable)
//   0x23  I2C_CFG
//
// Power-on sequence (enforced by M5IOE1::power_sequence()):
//   1. PYG3  (IOE_AUDIO) + PYG8 (IOE_L3B)  → HIGH  (codec + AMOLED supply)
//   2. Wait 20 ms (AMOLED supply settle, LCD_POWER_UP_MS)
//   3. PYG4  (IOE_TP_RST)  → LOW 10 ms → HIGH  (touch reset)
//   4. PYG5  (IOE_LCD_RST) → LOW 10 ms → HIGH  (AMOLED reset)
//   5. Wait 120 ms (CO5300 wake, LCD_RESET_MS)
//   6. Write CST820 reg 0xFE = 0x01 (disable auto-sleep) via shared I2C
//   7. PYG10 (IOE_SPK)  → HIGH  (AW8737A amplifier enable, deferred to codec pa)
//
// PYG9 (IOE_MOTOR) — vibration motor — left LOW (not driven in this port).
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <cstdint>

namespace hgp {

class M5IOE1 {
 public:
  static constexpr uint8_t kAddr = 0x4F;

  // Pin bit masks (1-based PYG label → 0-based bit index)
  static constexpr uint16_t IOE_AUDIO    = 1u << 2;   // PYG3  — ES8311 + MIC supply
  static constexpr uint16_t IOE_TP_RST   = 1u << 3;   // PYG4  — CST820 reset (active-low pulse)
  static constexpr uint16_t IOE_LCD_RST  = 1u << 4;   // PYG5  — AMOLED reset (active-low pulse)
  static constexpr uint16_t IOE_L3B      = 1u << 7;   // PYG8  — AMOLED 3V3 supply
  static constexpr uint16_t IOE_MOTOR    = 1u << 8;   // PYG9  — vibration motor PWM
  static constexpr uint16_t IOE_SPK      = 1u << 9;   // PYG10 — AW8737A amp enable

  // Registers (16-bit, little-endian)
  static constexpr uint8_t REG_MODE      = 0x03;
  static constexpr uint8_t REG_OUT       = 0x05;
  static constexpr uint8_t REG_IN        = 0x07;
  static constexpr uint8_t REG_OD        = 0x13;
  static constexpr uint8_t REG_PWM_DUTY  = 0x1B;
  static constexpr uint8_t REG_I2C_CFG  = 0x23;

  // Initialise, configure directions and run power-on sequence.
  // Call once after the I2C bus master is created, before amoled/touch/codec begin().
  // Returns ESP_OK on success.
  esp_err_t begin(i2c_master_bus_handle_t bus);

  // Set IOE_SPK high (amplifier on) or low (off).
  // Called from the pa_gpio callback set up in port_codec.cpp.
  esp_err_t set_amp(bool on);

  // Optional: pulse the vibration motor for duration_ms milliseconds.
  esp_err_t vibrate(uint16_t duty_pct, uint32_t duration_ms);

 private:
  i2c_master_dev_handle_t dev_ = nullptr;

  esp_err_t write_reg16(uint8_t reg, uint16_t val);
  esp_err_t read_reg16(uint8_t reg, uint16_t& val);
  esp_err_t set_bits(uint8_t reg, uint16_t mask, bool on);

  // Write CST820 register 0xFE = 0x01 to disable auto-sleep.
  // Requires the shared bus handle (cst820 shares I2C_NUM_0).
  esp_err_t cst820_disable_autosleep(i2c_master_bus_handle_t bus);
};

// Singleton accessor (instantiated in port_m5ioe1.cpp).
M5IOE1& m5ioe1();

}  // namespace hgp
