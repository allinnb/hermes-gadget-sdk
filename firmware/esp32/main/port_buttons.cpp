// Active-low push buttons, polled from the app task with a short debounce.
#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include "driver/gpio.h"
#include "esp_log.h"

namespace hgp {
namespace {
constexpr char kTag[] = "hg.buttons";
constexpr uint8_t kDebounceTicks = 3;  // 3 polls at ~10 ms

const char* name(hg::Button id) {
  switch (id) {
    case hg::Button::Talk: return "talk";
    case hg::Button::Cancel: return "cancel";
    case hg::Button::Up: return "up";
    case hg::Button::Down: return "down";
  }
  return "unknown";
}
}

void Buttons::begin(const ButtonConfig& cfg) {
  const int gpios[4] = {cfg.talk, cfg.cancel, cfg.up, cfg.down};
  const hg::Button ids[4] = {hg::Button::Talk, hg::Button::Cancel, hg::Button::Up, hg::Button::Down};
  for (int i = 0; i < 4; ++i) {
    buttons_[i].gpio = gpios[i];
    buttons_[i].id = ids[i];
    if (gpios[i] < 0) continue;
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << gpios[i];
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    const esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "%s GPIO%d configuration failed: %s", name(buttons_[i].id), gpios[i], esp_err_to_name(err));
      buttons_[i].gpio = -1;
      continue;
    }
    // Seed the debouncer from the real pin level. This avoids a false edge at
    // boot and records enough evidence to diagnose board revisions in the log.
    buttons_[i].pressed = gpio_get_level(static_cast<gpio_num_t>(gpios[i])) == 0;
    ESP_LOGI(kTag, "%s GPIO%d ready, initial=%s", name(buttons_[i].id), gpios[i],
             buttons_[i].pressed ? "down" : "up");
  }
}

void Buttons::poll(hg::App& app) {
  for (auto& b : buttons_) {
    if (b.gpio < 0) continue;
    bool down = gpio_get_level(static_cast<gpio_num_t>(b.gpio)) == 0;
    if (down == b.pressed) {
      b.stable = 0;
      continue;
    }
    if (++b.stable >= kDebounceTicks) {
      b.pressed = down;
      b.stable = 0;
      ESP_LOGI(kTag, "%s GPIO%d %s", name(b.id), b.gpio, down ? "down" : "up");
      app.on_button(b.id, down);
      if (on_change) on_change(b.id, down);
    }
  }
}

}  // namespace hgp
