# ESP32 Advanced Console


[English](#english) | [中文](#中文)

---


<a name="中文"></a>
## 中文
欢迎！
### 项目简介

本项目是基于 [ESP-IDF](https://github.com/espressif/esp-idf) 官方 `console` 示例的增强版命令行框架，专为 ESP32 系列芯片设计。

> **与官方示例的主要区别：**
>   - 拓展的wlan和ble命令
>   - unix风格的文件管理命令
>   - 对 | & ; # 操作符号的支持
>   - 系统管理命令(tasks,kill,suspend,resume,priority,stack)
>   - 一致的组件命名(app_xxx,cmd_yyy,...)
>   - ...

此项目仍在早期阶段，存在大把的Bug。编程千万条，安全第一条。不要在生产环境使用，后果自负（笑）

### 支持的芯片

| ESP32 | ESP32-S2 | ESP32-S3 | ESP32-C3 | ESP32-C6 | 其他 |
| :---: | :------: | :------: | :------: | :------: | :--: |
|  待测    |    待测      |    ✅     |    待测     |    待测      | 待测 |


### 快速开始

#### 环境要求

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) v5.x 或更高版本，推荐使用ESP-IDF v6.1，因为这个版本我编译测试过。
- 支持的目标芯片开发板，至少有2MB的Flash

#### 需要添加的依赖项
- idf.py add-dependency espressif/console_cmd_ping

#### 编译与烧录

```bash
git clone https://github.com/shaochen1850/advanced_console_for_esp32.git
cd advanced_console_for_esp32
idf.py set-target esp32s3  # 根据你的芯片修改
idf.py menuconfig          # 如需通过菜单修改配置
idf.py build flash monitor # 编译，下载，监视/串口
picocom -b 115200 /dev/ttyACM0 # 推荐，请视情况调整参数
```

### 可用命令

连接串口后，输入 `help` 查看所有命令。

| 命令 | 说明 | 示例 |
|------|------|------|
| `help` | 显示帮助信息 | `help` |
| `free` | 查看剩余堆内存 | `free` |
| `version` | 查看芯片与 SDK 版本 | `version` |
| `restart` | 软重启芯片 | `restart` |
| `wlan` | 无线局域网命令 | `wlan connect MyAP ThisisPassword` |
| `echo` | 回显+写入 | `echo "Hi" > /data/newfile.txt` |
| `head` | 从标准输出截取头n行 | `curl 192.168.0.1 \| head -n 20` |
| `ntp`  | 与默认（或指定的）ntp服务器对时 | `ntp pool.ntp.org` |


> 输入help查看完整的命令列表。

### 项目结构

```

.
├── CMakeLists.txt
├── components
│   ├── app_ws2812
│   │   ├── app_ws2812.c
│   │   ├── app_ws2812.h
│   │   ├── CMakeLists.txt
│   │   ├── led_driver.c
│   │   ├── led_driver.h
│   │   ├── led_effects.c
│   │   ├── led_effects.h
│   │   ├── led_strip_encoder.c
│   │   └── led_strip_encoder.h
│   ├── cmd_ble
│   │   ├── CMakeLists.txt
│   │   ├── cmd_ble.c
│   │   └── cmd_ble.h
│   ├── cmd_fs
│   │   ├── CMakeLists.txt
│   │   ├── cmd_fs.c
│   │   └── cmd_fs.h
│   ├── cmd_netutils
│   │   ├── CMakeLists.txt
│   │   ├── cmd_netutils.c
│   │   └── cmd_netutils.h
│   ├── cmd_nvs
│   │   ├── CMakeLists.txt
│   │   ├── cmd_nvs.c
│   │   └── cmd_nvs.h
│   ├── cmd_sysmgmt
│   │   ├── CMakeLists.txt
│   │   ├── cmd_sysmgmt.c
│   │   └── cmd_sysmgmt.h
│   ├── cmd_system
│   │   ├── CMakeLists.txt
│   │   ├── cmd_edit.c
│   │   ├── cmd_head_tail.c
│   │   ├── cmd_system.c
│   │   ├── cmd_system_common.c
│   │   ├── cmd_system.h
│   │   ├── cmd_system_sleep.c
│   │   └── cmd_watch.c
│   └── cmd_wifi
│       ├── CMakeLists.txt
│       ├── cmd_wifi.c
│       └── cmd_wifi.h
├── dependencies.lock
├── LICENSE
├── main
│   ├── CMakeLists.txt
│   ├── console_example_main.c
│   ├── console_settings.c
│   ├── console_settings.h
│   ├── idf_component.yml
│   └── Kconfig.projbuild
├── managed_components(Not contained)
│   ├── espressif__console_cmd_ping
│   │   ├── CHANGELOG.md
│   │   ├── CHECKSUMS.json
│   │   ├── CMakeLists.txt
│   │   ├── console_getaddrinfo.c
│   │   ├── console_getsetdnsserver.c
│   │   ├── console_ping.c
│   │   ├── console_ping.h
│   │   ├── examples
│   │   │   └── ping-basic
│   │   │       ├── CMakeLists.txt
│   │   │       ├── main
│   │   │       │   ├── CMakeLists.txt
│   │   │       │   ├── idf_component.yml
│   │   │       │   └── ping-basic.c
│   │   │       └── pytest_ping_basic.py
│   │   ├── idf_component.yml
│   │   ├── Kconfig.projbuild
│   │   ├── LICENSE
│   │   └── README.md
│   └── espressif__console_simple_init
│       ├── CHANGELOG.md
│       ├── CHECKSUMS.json
│       ├── CMakeLists.txt
│       ├── console_simple_init.c
│       ├── console_simple_init.h
│       ├── examples
│       │   └── console_basic
│       │       ├── CMakeLists.txt
│       │       ├── main
│       │       │   ├── CMakeLists.txt
│       │       │   ├── console_basic.c
│       │       │   └── idf_component.yml
│       │       └── pytest_console_basic.py
│       ├── idf_component.yml
│       ├── LICENSE
│       ├── linker.lf
│       └── README.md
├── partitions_example.csv
├── pytest_console_advanced.py
├── README.md
├── sdkconfig
├── sdkconfig.defaults
└── sdkconfig.old

```

### 配置选项

部分功能可能需要先在 `idf.py menuconfig` 中配置后才能正常工作，例如部分LwIP相关项目，或者硬件特定项目。

### 常见问题

#### 终端不支持转义序列

首先，若你是烧录完直接连接串口，你可以尝试首先进行复位。

如果复位后你仍然看到以下提示，说明当前串口工具不支持 ANSI 转义序列：

```
Your terminal application does not support escape sequences.
```

**解决方案**：使用 [Windows Terminal](https://github.com/microsoft/terminal)、PuTTY、Minicom(Linux,etc.)、Picocom(Linux,etc.) 或 ESP-IDF Monitor。

#### 没有输出或无法进行输入

如果你的开发板有一个CH340(COM)口和一个乐鑫JTAG口(USB)，你应将电脑连接到CH340口(COM)才能正常进行调试。

### 已知问题

在WIFI未准备好时使用命令 (如 ping, curl, ntp) 可能会导致panic发生。

<a name="english"></a>
## English
Welcome!
### Introduction

This project is an enhanced command-line framework based on the official ESP-IDF `console` example, designed for ESP32 series chips.

> **Key differences from the official example:**
>   - Extended wlan and ble commands
>   - Unix-style file command (ls,cd,rm...)
>   - Supports | & ; # symbol
>   - System management commands (tasks,kill,suspend,resume,priority,stack)
>   - Unified component naming (app_xxx,cmd_yyy,...)
>   - ...

This project is at an early stage. It works (most of the time) and may contain bugs. Use at your own risk.

### Supported Targets

| ESP32 | ESP32-S2 | ESP32-S3 | ESP32-C3 | ESP32-C6 | Others |
| :---: | :------: | :------: | :------: | :------: | :----: |
|   TBD   |    TBD    |    ✅     |    TBD     |    TBD     |  TBD   |

### Quick Start

#### Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) v5.x or later, While I recommend esp-idf v6.1 for I already tested it.
- A supported development board, with at least 2MB Flash

#### Depencies
- idf.py add-dependency espressif/console_cmd_ping

#### Build and Flash

```bash
git clone https://github.com/shaochen1850/advanced_console_for_esp32.git
cd advanced_console_for_esp32
idf.py set-target esp32s3  # Change to your target chip
idf.py menuconfig          # Modify config if needed
idf.py build flash monitor # Compile, Flash and Monitor
picocom -b 115200 /dev/ttyACM0 # Recommend, adjust paramater according to the board
```

### Available Commands

After connecting to the serial port, type `help` to see all commands.

| Command | Description | Example |
|---------|-------------|---------|
| `help` | Show help information | `help` |
| `free` | Show remained heap | `free` |
| `version` | Show chip and SDK version | `version` |
| `restart` | Soft reset chip | `restart` |
| `wlan` | WLAN Commands | `wlan connect MyAP ThisisPassword` |
| `echo` | Echo and write | `echo "Hi" > /data/newfile.txt` |
| `head` | Cut n Lines from stdout | `curl 192.168.0.1 \| head -n 20` |
| `ntp`  | Calibrate time with default or dedicated NTP server | `ntp pool.ntp.org` |


> Enter help for full list of commands.

### Project Structure

```
.
├── CMakeLists.txt
├── components
│   ├── app_ws2812
│   │   ├── app_ws2812.c
│   │   ├── app_ws2812.h
│   │   ├── CMakeLists.txt
│   │   ├── led_driver.c
│   │   ├── led_driver.h
│   │   ├── led_effects.c
│   │   ├── led_effects.h
│   │   ├── led_strip_encoder.c
│   │   └── led_strip_encoder.h
│   ├── cmd_ble
│   │   ├── CMakeLists.txt
│   │   ├── cmd_ble.c
│   │   └── cmd_ble.h
│   ├── cmd_fs
│   │   ├── CMakeLists.txt
│   │   ├── cmd_fs.c
│   │   └── cmd_fs.h
│   ├── cmd_netutils
│   │   ├── CMakeLists.txt
│   │   ├── cmd_netutils.c
│   │   └── cmd_netutils.h
│   ├── cmd_nvs
│   │   ├── CMakeLists.txt
│   │   ├── cmd_nvs.c
│   │   └── cmd_nvs.h
│   ├── cmd_sysmgmt
│   │   ├── CMakeLists.txt
│   │   ├── cmd_sysmgmt.c
│   │   └── cmd_sysmgmt.h
│   ├── cmd_system
│   │   ├── CMakeLists.txt
│   │   ├── cmd_edit.c
│   │   ├── cmd_head_tail.c
│   │   ├── cmd_system.c
│   │   ├── cmd_system_common.c
│   │   ├── cmd_system.h
│   │   ├── cmd_system_sleep.c
│   │   └── cmd_watch.c
│   └── cmd_wifi
│       ├── CMakeLists.txt
│       ├── cmd_wifi.c
│       └── cmd_wifi.h
├── dependencies.lock
├── LICENSE
├── main
│   ├── CMakeLists.txt
│   ├── console_example_main.c
│   ├── console_settings.c
│   ├── console_settings.h
│   ├── idf_component.yml
│   └── Kconfig.projbuild
├── managed_components(Not contained)
│   ├── espressif__console_cmd_ping
│   │   ├── CHANGELOG.md
│   │   ├── CHECKSUMS.json
│   │   ├── CMakeLists.txt
│   │   ├── console_getaddrinfo.c
│   │   ├── console_getsetdnsserver.c
│   │   ├── console_ping.c
│   │   ├── console_ping.h
│   │   ├── examples
│   │   │   └── ping-basic
│   │   │       ├── CMakeLists.txt
│   │   │       ├── main
│   │   │       │   ├── CMakeLists.txt
│   │   │       │   ├── idf_component.yml
│   │   │       │   └── ping-basic.c
│   │   │       └── pytest_ping_basic.py
│   │   ├── idf_component.yml
│   │   ├── Kconfig.projbuild
│   │   ├── LICENSE
│   │   └── README.md
│   └── espressif__console_simple_init
│       ├── CHANGELOG.md
│       ├── CHECKSUMS.json
│       ├── CMakeLists.txt
│       ├── console_simple_init.c
│       ├── console_simple_init.h
│       ├── examples
│       │   └── console_basic
│       │       ├── CMakeLists.txt
│       │       ├── main
│       │       │   ├── CMakeLists.txt
│       │       │   ├── console_basic.c
│       │       │   └── idf_component.yml
│       │       └── pytest_console_basic.py
│       ├── idf_component.yml
│       ├── LICENSE
│       ├── linker.lf
│       └── README.md
├── partitions_example.csv
├── pytest_console_advanced.py
├── README.md
├── sdkconfig
├── sdkconfig.defaults
└── sdkconfig.old

```

### Configuration Options

Some options should be configured via `idf.py menuconfig` in order to work properly, e.g. LwIP

### Troubleshooting

#### Terminal does not support escape sequences

You may try reseting the board

If you see the following message after reset, your serial tool does not support ANSI escape sequences:

```
Your terminal application does not support escape sequences.
```

**Solution**: Use [Windows Terminal](https://github.com/microsoft/terminal), PuTTY, Minicom(Linux,etc.), Picocom(Linux,etc.) or ESP-IDF Monitor.

#### No serial output or cannot input command

If your board have a CH340 USB port marked "COM" and a Espressif JTAG debug USB port marked "USB", make sure that you are using CH340 "COM" port.

### Known Bugs

Issuing network-related commands (e.g., ping, curl, ntp) before WiFi is connected may trigger a panic.

## License & Attribution

### 许可证 / License

本项目采用 [Apache License 2.0](LICENSE) 开源。

This project is licensed under the [Apache License 2.0](LICENSE).

你可以在 [这里](https://www.apache.org/licenses/LICENSE-2.0.txt) 查看该协议的原始内容

You may obtain the original content of this license [Here](https://www.apache.org/licenses/LICENSE-2.0.txt).

### 致谢 / Acknowledgments

本项目基于 [Espressif ESP-IDF](https://github.com/espressif/esp-idf) 官方 `examples/system/console` 示例开发。

- 原始示例代码由 [乐鑫信息科技](https://www.espressif.com) 提供，采用 **CC0-1.0 / Unlicense** 许可证。
- ESP-IDF 框架采用 **Apache-2.0** 许可证。
- FreeRTOS 内核采用 **MIT** 许可证。

This project is based on the official Espressif ESP-IDF `examples/system/console` example.

- The original example code is provided by [Espressif Systems](https://www.espressif.com) under the **CC0-1.0 / Unlicense** license.
- The ESP-IDF framework is licensed under **Apache-2.0**.
- The FreeRTOS kernel is licensed under **MIT**.

### 免责声明 / Disclaimer

本软件按"原样"提供，不提供任何明示或暗示的担保。详见 [LICENSE](LICENSE) 文件。

This software is provided "as is", without warranty of any kind. See the [LICENSE](LICENSE) file for details.
