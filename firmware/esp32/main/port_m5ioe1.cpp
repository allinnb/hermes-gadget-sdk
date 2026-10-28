// M5IOE1 I/O expander driver — implementation.
// See port_m5ioe1.hpp for the register map and power-on sequence description.
#include "port_m5ioe1.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace hgp {
namespace {

const char* TAG = "hg.m5ioe1";

// CST820 I2C address (same shared bus as M5IOE1)
constexpr uint8_t kCst820Addr = 0x15;
constexpr uint8_t kCst820RegNoAutoSleep = 0xFE;
constexpr uint8_t kCst820NoAutoSleepVal = 0x01;

// Supply-settle and reset timing (from muse-gadget-sdk board_m5stack_stopwatch.c)
constexpr uint32_t kSupplySettleMs  = 20;   // 3V3_L3B settle before releasing AMOLED reset
constexpr uint32_t kResetPulseMs   = 10;   // reset active duration
constexpr uint32_t kLcdWakeMs      = 120;  // CO5300 wake after reset release

M5IOE1 s_instance;

}  // namespace

M5IOE1& m5ioe1() { return s_instance; }

// ── Private helpers ──────────────────────────────────────────────────────────

esp_err_t M5IOE1::write_reg16(uint8_t reg, uint16_t val) {
  uint8_t buf[3] = {reg, static_cast<uint8_t>(val & 0xFF),
                         static_cast<uint8_t>((val >> 8) & 0xFF)};
  const esp_err_t err = i2c_master_transmit(dev_, buf, sizeof(buf), 50);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "write_reg16 reg=0x%02x err=0x%x", reg, err);
  return err;
}

esp_err_t M5IOE1::read_reg16(uint8_t reg, uint16_t& val) {
  uint8_t buf[2] = {};
  const esp_err_t err =
      i2c_master_transmit_receive(dev_, &reg, 1, buf, 2, 50);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "read_reg16 reg=0x%02x err=0x%x", reg, err);
    return err;
  }
  val = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
  return ESP_OK;
}

esp_err_t M5IOE1::set_bits(uint8_t reg, uint16_t mask, bool on) {
  uint16_t cur = 0;
  esp_err_t err = read_reg16(reg, cur);
  if (err != ESP_OK) return err;
  const uint16_t next = on ? (cur | mask) : (cur & ~mask);
  if (next == cur) return ESP_OK;
  return write_reg16(reg, next);
}

esp_err_t M5IOE1::cst820_disable_autosleep(i2c_master_bus_handle_t bus) {
  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address  = kCst820Addr;
  dev_cfg.scl_speed_hz    = 400000;
  i2c_master_dev_handle_t cst_dev = nullptr;
  esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &cst_dev);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "cst820_disable_autosleep: add_device err=0x%x", err);
    return err;
  }
  const uint8_t cmd[2] = {kCst820RegNoAutoSleep, kCst820NoAutoSleepVal};
  err = i2c_master_transmit(cst_dev, cmd, sizeof(cmd), 50);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "cst820_disable_autosleep: transmit err=0x%x", err);
  i2c_master_bus_rm_device(cst_dev);
  return err;
}

// ── Public API ───────────────────────────────────────────────────────────────

esp_err_t M5IOE1::begin(i2c_master_bus_handle_t bus) {
  // Add M5IOE1 to the shared I2C bus
  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address  = kAddr;
  dev_cfg.scl_speed_hz    = 100000;  // conservative; M5 muse uses 100 kHz for IOE
  esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c_master_bus_add_device failed: 0x%x", err);
    return err;
  }

  // Pins controlled by this port. Preserve every other M5IOE1 pin because the
  // PMIC and factory firmware may have configured unrelated board functions.
  constexpr uint16_t kOutputMask =
      IOE_AUDIO | IOE_TP_RST | IOE_LCD_RST | IOE_L3B | IOE_MOTOR | IOE_SPK;

  // Match M5Stack's bring-up sequence. Disable the expander's I2C idle sleep,
  // stop motor PWM, and use push-pull outputs for all six controlled pins.
  const uint8_t i2c_cfg[2] = {REG_I2C_CFG, 0x00};
  err = i2c_master_transmit(dev_, i2c_cfg, sizeof(i2c_cfg), 50);
  if (err != ESP_OK) return err;
  err = write_reg16(REG_PWM_DUTY, 0x0000);
  if (err != ESP_OK) return err;
  // M5Unified configures StopWatch's G9 motor on PWM1 at 2 kHz. The source
  // reference is pinned in docs/hardware.md; this register stores Hz LE.
  err = write_reg16(REG_PWM_FREQ, 2000);
  if (err != ESP_OK) return err;
  err = set_bits(REG_OD, kOutputMask, false);
  if (err != ESP_OK) return err;

  // Preload safe output levels before changing direction: supplies and resets
  // high, motor and amplifier low. MODE uses 1 for output on M5IOE1.
  err = set_bits(REG_OUT, IOE_AUDIO | IOE_TP_RST | IOE_LCD_RST | IOE_L3B, true);
  if (err != ESP_OK) return err;
  err = set_bits(REG_OUT, IOE_MOTOR | IOE_SPK, false);
  if (err != ESP_OK) return err;
  err = set_bits(REG_MODE, kOutputMask, true);
  if (err != ESP_OK) return err;
  vTaskDelay(pdMS_TO_TICKS(kResetPulseMs));

  // Reset touch and panel together, as in M5Stack's StopWatch BSP.
  ESP_LOGI(TAG, "enabling audio + AMOLED supplies; resetting touch + panel");
  err = set_bits(REG_OUT, IOE_TP_RST | IOE_LCD_RST, false);
  if (err != ESP_OK) return err;
  vTaskDelay(pdMS_TO_TICKS(kResetPulseMs));
  err = set_bits(REG_OUT, IOE_TP_RST | IOE_LCD_RST, true);
  if (err != ESP_OK) return err;
  vTaskDelay(pdMS_TO_TICKS(kLcdWakeMs));

  // Disable CST820 auto-sleep after reset; failure is non-fatal because the
  // generic CST816-compatible driver can still poll it.
  cst820_disable_autosleep(bus);

  // Enable the AW8737A. Its enable is an expander pin, not an ESP32 GPIO;
  // the shared codec speaker currently accepts only native GPIO PA controls.
  err = set_bits(REG_OUT, IOE_SPK, true);
  if (err != ESP_OK) return err;

  ready_ = true;
  ESP_LOGI(TAG, "M5IOE1 power sequence complete");
  return ESP_OK;
}

esp_err_t M5IOE1::set_amp(bool on) {
  return set_bits(REG_OUT, IOE_SPK, on);
}

esp_err_t M5IOE1::pulse_vibration(uint16_t duty_pct, uint32_t duration_ms, uint32_t now_ms) {
  if (!ready_) return ESP_ERR_INVALID_STATE;
  if (duty_pct == 0 || duration_ms == 0) return stop_vibration();
  if (duty_pct > 100) duty_pct = 100;
  // PWM1_DUTY: bit15 = enable; the low 12 bits hold duty (0–4095).
  const uint16_t duty_raw = static_cast<uint16_t>(duty_pct * 4095u / 100u);
  const esp_err_t err = write_reg16(REG_PWM_DUTY, 0x8000u | duty_raw);
  if (err == ESP_OK) vibration_off_at_ = now_ms + duration_ms;
  return err;
}

void M5IOE1::tick(uint32_t now_ms) {
  if (vibration_off_at_ != 0 && static_cast<int32_t>(now_ms - vibration_off_at_) >= 0)
    stop_vibration();
}

esp_err_t M5IOE1::stop_vibration() {
  if (!ready_) return ESP_ERR_INVALID_STATE;
  const esp_err_t err = write_reg16(REG_PWM_DUTY, 0x0000);
  // Keep the expired deadline on failure so tick() retries instead of leaving
  // the motor running indefinitely after a transient I2C error.
  if (err == ESP_OK) vibration_off_at_ = 0;
  return err;
}

}  // namespace hgp
