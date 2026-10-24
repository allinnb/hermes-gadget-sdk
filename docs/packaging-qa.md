# Packaging Q&A — M5Stack StopWatch Port

> Prepared 2026-10-24 in response to questions raised in the packaging hand-off
> (`转交指令-NewMax-GitHub打包-20261024.md`).

---

## 1. 固件源码在哪？改了哪些文件？

**源码仓库：** <https://github.com/allinnb/hermes-gadget-sdk>（public）

本次移植在上游 `Adolanium/hermes-gadget-sdk` 的完整历史基础上新增 StopWatch
支持，而不是另建独立包，便于后续向上游提 PR。

### 新增文件

| 文件 | 说明 |
|------|------|
| `firmware/esp32/main/port_m5ioe1.hpp` | M5IOE1 16-bit I/O expander 驱动接口 |
| `firmware/esp32/main/port_m5ioe1.cpp` | M5IOE1 驱动实现（上电序列、复位、功放使能、振动） |
| `firmware/esp32/main/port_m5pm1.hpp` | M5PM1 PMIC 红键驱动接口 |
| `firmware/esp32/main/port_m5pm1.cpp` | M5PM1 驱动实现（短按息屏/唤醒、长按 Settings、IRQ） |
| `firmware/esp32/boards/m5stack-stopwatch/sdkconfig.defaults` | StopWatch 板级 Kconfig 覆盖 |
| `firmware/esp32/boards/m5stack-stopwatch/board.json` | 板卡元数据（符合贡献规范 schema） |
| `docs/hardware.md`（stopwatch 段） | StopWatch 硬件说明 |
| `docs/supported-hardware.md`（stopwatch 行） | 支持设备列表 |
| `docs/hardware-validation.md`（stopwatch 段） | 实机测试报告（Experimental） |
| `CHANGELOG.md` | 变更记录 |
| `tools/debug-bridge/` | HTTP→串口调试桥及启动器 |

### 修改文件

| 文件 | 修改内容 |
|------|---------|
| `firmware/esp32/main/board.cpp` | 新增 `HG_BOARD_M5STACK_STOPWATCH` 分支及完整 `BoardConfig` |
| `firmware/esp32/main/main.cpp` | 新增 M5IOE1 / M5PM1 条件初始化 |
| `firmware/esp32/main/CMakeLists.txt` | 新增 `port_m5ioe1.cpp`、`port_m5pm1.cpp` |
| `firmware/esp32/main/Kconfig.projbuild` | 新增 `HG_BOARD_M5STACK_STOPWATCH` 选项 |
| `firmware/esp32/platformio.ini` | 新增 `[env:m5stack-stopwatch]` |
| `NOTICE` | 新增 StopWatch 硬件参考资料来源归属 |
| `THIRD_PARTY_NOTICES.md` | 新增 `facebookincubator/muse-gadget-sdk` Apache-2.0 归属 |

---

## 2. 原项目 license 是什么？

上游仓库 `Adolanium/hermes-gadget-sdk` 使用 **MIT License**。

本仓库完整保留该 LICENSE 文件。StopWatch 移植中参考了
`facebookincubator/muse-gadget-sdk`（Apache-2.0）的**硬件事实**（引脚号、
寄存器地址和行为描述），ESP-IDF 驱动代码为本项目原创实现。
归属说明见 `NOTICE` 和 `THIRD_PARTY_NOTICES.md`。

---

## 3. 编译环境

| 项目 | 版本/配置 |
|------|-----------|
| PlatformIO Core | 6.x（`python -m platformio --version` 可查） |
| ESP-IDF | 6.1.0（PlatformIO 自动管理） |
| 目标芯片 | `esp32s3` |
| Flash | 16 MB |
| PSRAM | 8 MB OPI（`CONFIG_SPIRAM=y` + `CONFIG_ESP32S3_SPIRAM_SUPPORT=y`） |
| 控制台 | USB-Serial-JTAG（`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`） |

**`platformio.ini` StopWatch 环境：**

```ini
[env:m5stack-stopwatch]
board = esp32-s3-devkitc-1
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.cmake_extra_args =
    -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/m5stack-stopwatch/sdkconfig.defaults"
```

**构建命令（从 `firmware/esp32/` 目录）：**

```bash
python -m platformio run -e m5stack-stopwatch
```

> ⚠️ Windows 中文路径（含汉字的目录名）会导致 ESP-IDF Kconfig 脚本编码错误。
> 请将仓库 clone 到纯英文路径下构建，例如 `D:\projects\hermes-gadget-sdk`。

**刷机命令：**

```bash
python -m platformio run -e m5stack-stopwatch --target upload --upload-port COM9
```

---

## 4. 硬编码的个人信息（脱敏检查）

仓库中**不含**以下信息：

- Wi-Fi SSID / 密码
- Hermes 服务端 IP / 域名（固件通过 `set server <url>` 命令写入，不在源码中）
- API key / Bearer token
- 个人 Tailscale IP（`100.110.7.45` 仅出现在 `tools/debug-bridge/README.md`
  的示例中，已标注为"replace with your Tailscale IP"）
- 设备 ID（每次 `forget-key` 后重新生成，不在源码中）
- 串口日志（调试过程日志未提交）

发布前建议再用以下命令全局扫描一遍：

```bash
git grep -E "100\.[0-9]+\.[0-9]+\.[0-9]+|sk-|Bearer |password|ssid" -- ':!docs/'
```

---

## 已修复的代码审查问题

| 问题 | 位置 | 状态 |
|------|------|------|
| 红键长按后 `key_held_` 不复位，第二次长按无法关闭 Settings | `port_m5pm1.cpp:137` | ✅ 已修复 |
| `sdkconfig.defaults` 缺 `CONFIG_IDF_TARGET="esp32s3"` | `sdkconfig.defaults` 第1行 | ✅ 已补充 |
| `sdkconfig.defaults` 缺 `CONFIG_SPIRAM=y` 父开关 | `sdkconfig.defaults` 第17行 | ✅ 已补充 |
| M5IOE1 供电使能后缺延时再复位 | `port_m5ioe1.cpp:116-118` | ✅ 确认已有（MODE写入后接10ms delay，复位在delay之后） |

---

*文档由 NewMax 整理，2026-10-24。*
