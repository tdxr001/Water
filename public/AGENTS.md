# U575 水表防拆项目工作记录

更新时间：2026-05-13  
当前工程目录：`C:\Users\zhengzhibo\Desktop\U575-TEST`

## 0. 最新进度

本轮已完成：

2026-05-13 当前状态：已清理本轮旧的构建/烧录/MQTT 运行日志；Git 目录扫描确认只保留当前有效 `.git`，未发现旧的残留 Git 仓库目录。准备提交并同步 GitHub。

2026-05-11 当前状态：低功耗 INT1 正式链路已跑通，当前代码状态保持为正式低功耗版本。后续新增代码要求优先模块化封装，详见第 9 节。

2026-05-05 当前状态：低功耗 INT1 正式链路已跑通，且当前烧录/代码状态保持为正式低功耗版本。

2026-05-03 16:24 低功耗 INT1 正式链路已跑通：

1. 当前固件已关闭轮询诊断模式：

```c
#define APP_MOTION_POLL_DIAG 0
```

2. LSM6DSR wake-up 中断配置已增强：

- 加速度 ODR 调到 417Hz / +/-2g，用于更可靠捕捉短促晃动。
- `TAP_CFG0=0x51`，启用锁存中断、读源寄存器清除和 slope/high-pass 路径。
- wake-up 阈值调到约 62mg。
- `MD1_CFG=0x20`，wake-up 事件输出到 INT1。

3. 主循环恢复为正式低功耗流程：开机发送 `BOOT` 和 `SENSOR_READY`，随后进入 `STOP2`；LSM6DSR INT1 拉起 PA5/EXTI5 后唤醒 MCU 并发送：

```text
ALERT:water_meter_tamper,source=int1,wake=0x...,acc_mg=...,gyro_dps=...,gyro_peak=...
```

4. 实测静止 35 秒无新 TCP 发送，说明没有持续误报。
5. 最终完整验证：静止进入低功耗后晃动传感器，串口确认网络调试助手方向收到两条低功耗 INT1 告警数据：

```text
AT+CIPSTART="TCP","172.20.10.6",8080
CONNECT
OK
AT+CIPSEND=102
Recv 102 bytes
SEND OK
CLOSED

AT+CIPSEND=103
Recv 103 bytes
SEND OK
CLOSED
```

结论：当前工程已达到目标流程：低功耗待机后，晃动传感器触发 LSM6DSR INT1，STM32 从 STOP2 唤醒，通过 ESP8266 发送告警，网络调试助手接收并显示。

2026-05-03 12:00 追加联调结果（历史诊断阶段，当前已关闭轮询诊断）：

1. 增加 `APP_MOTION_POLL_DIAG` 诊断模式：当前固件会连续读取 LSM6DSR 三轴加速度，检测到明显晃动后直接发送：

```text
ALERT:water_meter_tamper,source=poll,acc_delta=...,acc_mg=...
```

2. 将 ESP8266 连接流程优化为优先复用已联网状态，先 `CIPSTART`，失败后才重新 `CWJAP`，减少连续告警时反复断开/重连 WiFi 带来的延迟。
3. 实测用户晃动传感器后，串口确认多条告警数据已成功发送到正在监听的 `NetAssist 172.20.10.6:8080`：

```text
AT+CIPSTART="TCP","172.20.10.6",8080
CONNECT
OK
AT+CIPSEND=75
Recv 75 bytes
SEND OK
CLOSED
```

结论：当前调试固件已经达到“晃动传感器，网络调试助手收到告警”的验证目标。注意当前为轮询诊断模式，便于先验证传感器数据链路和网络告警流畅性；纯 `STOP2 + INT1(PA5)` 低功耗唤醒模式此前 90 秒晃动未触发，需要继续核对 `LSM6DSR INT1 -> STM32 PA5` 接线或再调 LSM6DSR 中断配置。

1. 在 `ESP8266_TCPClient_Connect()` 和 `ESP8266_SendAlert()` 中加入 TCP 连接/发送重试，降低 `CIPSTART ERROR` 或 WiFi 刚获取 IP 未稳定导致的丢包概率。
2. 在正式流程中新增一次开机传感器自检上报：LSM6DSR 初始化和 INT1 配置成功后，读取一次三轴加速度并发送：

```text
SENSOR_READY:lsm6dsr,who=0x6B,acc_mg=...
```

3. Keil 编译通过：

```text
"U575-TEST\U575-TEST.axf" - 0 Error(s), 0 Warning(s).
```

4. 烧录通过：

```text
Erase Done.
Programming Done.
Verify OK.
Application running ...
```

5. 电脑端确认 `NetAssist` 正在监听 `172.20.10.6:8080`，并在串口日志中确认两次 TCP 发送成功：

```text
AT+CIPSTART="TCP","172.20.10.6",8080
CONNECT
OK
AT+CIPSEND=60
Recv 60 bytes
SEND OK
...
AT+CIPSEND=54
Recv 54 bytes
SEND OK
CLOSED
```

结论：`BOOT` 和 `SENSOR_READY` 数据已由 ESP8266 发到电脑端 TCP Server。当前工程已进入预设流程：开机上报状态/传感器自检数据，然后配置 LSM6DSR INT1，进入 `STOP2` 等待井盖异常震动唤醒并发送 `ALERT`。

## 1. 项目目标

本项目使用 STM32U575 主控板、华清远见 FS-STM32U5 底板自带 ESP8266 模块，以及外接 LSM6DSR 六轴传感器，实现水表井盖防拆/防异常震动告警。

预期工作流程：

1. STM32U575 初始化 LSM6DSR 和 ESP8266。
2. LSM6DSR 放在井盖上，平时由加速度 wake-up 中断监测异常震动。
3. STM32U575 平时进入 `STOP2` 低功耗模式。
4. LSM6DSR 的 `INT1` 触发 STM32 `PA5/EXTI5`，唤醒 MCU。
5. MCU 醒来后读取加速度和短时开启陀螺仪，判断是否异常。
6. 若异常，通过底板 ESP8266 连接 WiFi，并向电脑端 TCP Server 发送告警字符串。

## 2. 当前硬件连接信息

LSM6DSR 传感器模块当前按以下方式连接：

| LSM6DSR 模块 | STM32U575 / 电源 | 说明 |
| --- | --- | --- |
| `GND` | `GND` | 共地 |
| `3.3V` | `3.3V` | 传感器供电 |
| `SCL` | `PB6` | I2C1 SCL |
| `SDA` | `PB3` | I2C1 SDA |
| `SDO` | `3.3V` | 地址选择，代码使用 `0xD6`，对应 7 位地址 `0x6B` |
| `INT1` | `PA5` | 必须连接，用于唤醒 STM32 |

ESP8266 是华清远见底板自带模块，不需要另接外部 ESP8266。当前工程使用：

| 功能 | STM32 引脚 / 外设 |
| --- | --- |
| ESP8266 串口 | `UART5` |
| UART5 TX | `PC12` |
| UART5 RX | `PD2` |
| 调试串口 printf | `USART1`，`PA9/PA10` |

## 3. 当前网络配置

ESP8266 当前配置在 `Core\Inc\bsp_esp8266.h`：

```c
#define User_ESP8266_SSID     "ESP"
#define User_ESP8266_PWD      "12345678"
#define User_ESP8266_TCPServer_IP     "172.20.10.6"
#define User_ESP8266_TCPServer_PORT   "8080"
```

当前电脑 WLAN IPv4 是 `172.20.10.6`，ESP8266 在调试时获取到的地址曾显示为 `172.20.10.2`。

电脑端接收方式：

1. 电脑连接 WiFi：`ESP`，密码：`12345678`。
2. 打开网络调试助手。
3. 选择 `TCP Server`。
4. 监听端口：`8080`。
5. 监听地址建议选择 `0.0.0.0`。

## 4. 当前代码状态

当前工程是外层工程：

```text
C:\Users\zhengzhibo\Desktop\U575-TEST
```

本次通读时，当前目录下可见：

```text
Core
Drivers
Hardware
MDK-ARM
U575-TEST.ioc
```

注意：之前调试时曾使用过嵌套工程路径：

```text
C:\Users\zhengzhibo\Desktop\U575-TEST\U575-TEST\MDK-ARM\U575-TEST.uvprojx
```

但本次通读当前文件夹时，没有再看到该嵌套 `U575-TEST` 子目录。继续操作前应以当前实际存在的工程目录为准。

### 4.1 主程序状态

文件：`Core\Src\main.c`

当前关键宏：

```c
#define APP_NET_TEST_ONLY 0
#define APP_MOTION_POLL_DIAG 0
```

含义：

- `0`：正式低功耗防拆逻辑。
- `1`：只测试 ESP8266 网络链路，不进入低功耗，不依赖传感器。
- `APP_MOTION_POLL_DIAG=0`：关闭轮询诊断模式，使用正式 `STOP2 + INT1(PA5)` 低功耗唤醒流程。

当前正式逻辑：

1. 初始化 GPIO、I2C1、ICACHE、USART1、UART5。
2. 初始化 ESP8266。
3. 先尝试发送：

```text
BOOT:U575 tamper firmware online,server=172.20.10.6:8080
```

4. 初始化 LSM6DSR。
5. 如果 LSM6DSR 初始化失败，尝试发送：

```text
ERROR:LSM6DSR init failed,who_am_i=0xXX,check wiring/address
```

6. 配置 LSM6DSR wake-up 到 INT1。
7. 进入 `STOP2`。
8. `PA5/EXTI5` 唤醒后读取加速度和陀螺仪，发送：

```text
ALERT:water_meter_tamper,source=int1,wake=0x...,acc_mg=...,gyro_dps=...,gyro_peak=...
```

### 4.2 LSM6DSR 驱动状态

文件：

```text
Hardware\Inc\lsm6dsr.h
Hardware\Src\lsm6dsr.c
```

已实现：

- `LSM6DSR_Init()`
- `LSM6DSR_Read_ID()`
- `LSM6DSR_Config_Wakeup_INT1()`
- `LSM6DSR_Read_Accel()`
- `LSM6DSR_Read_Gyro()`
- `LSM6DSR_Enable_Gyro()`
- `LSM6DSR_Disable_Gyro()`
- `LSM6DSR_Read_Wakeup_Source()`

关键参数：

```c
#define LSM6DSR_TAP_CFG0          0x56
#define LSM6DSR_WAKEUP_THS_62MG   0x02
#define LSM6DSR_GYRO_ALERT_DPS    120
#define LSM6DSR_GYRO_SAMPLE_COUNT 4
```

当前 LSM6DSR wake-up 配置重点：

- `CTRL1_XL=0x60`：加速度 417Hz / +/-2g，用于可靠捕捉短促晃动。
- `TAP_CFG0=0x51`：启用锁存中断、读源寄存器清除和 slope/high-pass 路径。
- `WAKE_UP_THS=0x02`：wake-up 阈值约 62mg。
- `MD1_CFG=0x20`：wake-up 事件输出到 INT1。

传感器预期 `WHO_AM_I` 为 `0x6B`。

### 4.3 ESP8266 驱动状态

文件：

```text
Core\Inc\bsp_esp8266.h
Core\Src\bsp_esp8266.c
```

已新增/保留：

- `ESP8266_TCPClient_Connect()`
- `ESP8266_SendAlert()`
- `ESP8266_Close_TCP()`
- `ESP8266_RxCpltCallback()`
- `ESP8266_IdleCallback()`
- 保留旧测试函数 `ESP8266_STA_TCPClient_Test()`

已补齐：

- UART5 接收完成回调。
- UART5 IDLE 中断处理。
- `printf` 重定向到 USART1，避免半主机模式影响脱机运行。

## 5. 已验证结果

### 5.1 编译和烧录

Keil 编译曾通过：

```text
0 Error(s), 0 Warning(s)
```

烧录曾成功：

```text
Erase Done.
Programming Done.
Verify OK.
Application running ...
```

### 5.2 网络链路验证

在 `APP_NET_TEST_ONLY = 1` 的网络单项诊断模式下，电脑 TCP Server 曾收到：

```text
NET_TEST:ESP8266 online,server=172.20.10.6:8080
```

连接来源：

```text
172.20.10.2
```

结论：

- ESP8266 串口 AT 通。
- ESP8266 能连接 WiFi `ESP / 12345678`。
- 电脑 IP `172.20.10.6` 和端口 `8080` 可用。
- TCP 链路本身可以打通。

### 5.3 UART4/COM4 抓到的 ESP8266 日志

曾在 `COM4` 监听到 ESP8266 AT 日志：

```text
AT
OK
AT+CWMODE=1
OK
AT+CWJAP="ESP","12345678"
WIFI DISCONNECT
WIFI CONNECTED
WIFI GOT IP
AT+CIPMUX=0
OK
AT+CIPSTART="TCP","172.20.10.6",8080
CONNECT
OK
AT+CIPSEND=51
Recv 51 bytes
SEND OK
AT+CIPCLOSE
CLOSED
OK
```

也多次看到：

```text
AT+CIPSTART="TCP","172.20.10.6",8080
ERROR
CLOSED
```

结论：ESP8266 TCP 连接有时能成功，有时会在刚获取 IP 或服务器未监听时失败。正式逻辑中如果只发一次，容易错过；需要增加连接/发送重试。

## 6. 当前未完成问题与注意项

### 6.0 低功耗唤醒链路（已处理）

`STOP2 + LSM6DSR INT1 -> PA5/EXTI5 -> ESP8266 TCP ALERT` 已验证通过。当前不再依赖 `APP_MOTION_POLL_DIAG` 轮询诊断。

### 6.1 正式版 BOOT 偶尔收不到（已处理）

正式版中 `BOOT` 已被放到 LSM6DSR 初始化之前，但测试中仍出现未收到 TCP 数据的情况。

已知原因倾向：

- 不是 WiFi/TCP 完全不通，因为网络诊断版已收到 `NET_TEST`。
- 更可能是 `ESP8266_SendAlert()` 只尝试一次，`CIPSTART` 刚好返回 `ERROR` 后就放弃。
- 需要在 `ESP8266_SendAlert()` 或 `ESP8266_TCPClient_Connect()` 中加入重试机制。

### 6.2 COM4 后来消失

曾经可以通过 `COM4` 抓到 UART5/ESP8266 AT 日志，后来 `mode` 看不到 COM 端口。

可能原因：

- USB 串口设备重新枚举。
- 开发板 USB 连接变化。
- Keil/ST-LINK 占用或系统端口状态刷新。

处理建议：

1. 拔插开发板 USB。
2. 重新执行 `mode` 查看 COM 端口。
3. 如需串口日志，优先找能输出 ESP8266 AT 回包的端口。

## 7. 下一步建议

### 7.1 优先改 ESP8266 发送重试（已完成）

建议修改 `ESP8266_SendAlert()`：

- 每条消息最多尝试 3 到 5 次。
- 每次失败后 `AT+CIPCLOSE`。
- 每次重试间隔 1 到 2 秒。
- `ESP8266_JoinAP()` 后增加短暂延时，等待 `WIFI GOT IP` 稳定。

预期效果：

- 正式版开机 `BOOT` 更稳定。
- 后续传感器触发 `ALERT` 也更稳定。

### 7.2 再验证 LSM6DSR（已完成基础验证）

当 `BOOT` 能稳定收到后，再看是否收到：

```text
ERROR:LSM6DSR init failed,who_am_i=...
```

如果收到该错误：

- 检查 `SCL -> PB6`。
- 检查 `SDA -> PB3`。
- 检查 `SDO -> 3.3V`。
- 检查 3.3V 和 GND。
- 用万用表确认模块供电。
- 如果 `WHO_AM_I` 不是 `0x6B`，优先怀疑 I2C 地址或接线。

### 7.3 再验证 PA5 中断（已完成）

当前已完成以下条件验证：

1. `BOOT` 能稳定收到。
2. LSM6DSR 初始化不报错。
3. LSM6DSR INT1 已接到 STM32 PA5。

测试方式：

1. 打开网络调试助手 TCP Server，端口 `8080`。
2. 烧录正式版。
3. 等待 `BOOT`。
4. 晃动/敲击 LSM6DSR。
5. 已收到：

```text
ALERT:water_meter_tamper,source=int1,wake=0x...,acc_mg=...,gyro_dps=...,gyro_peak=...
```

## 8. 给后续接手者的注意事项

1. 不要再改 `D:\STM32\...` 下的工程，用户明确要求只处理桌面工程。
2. 当前服务器 IP 是本机当前 WLAN 地址 `172.20.10.6`，如果电脑换网络，必须重新查 `ipconfig` 并更新 `bsp_esp8266.h`。
3. 如果网络调试助手没有提前监听，ESP8266 的 `CIPSTART` 很容易返回 `ERROR`。
4. 正式项目中不建议长期使用 `APP_NET_TEST_ONLY = 1`；它只是网络诊断开关。
5. 当前正式低功耗验证应保持 `APP_NET_TEST_ONLY=0`、`APP_MOTION_POLL_DIAG=0`。
6. ESP8266 连接/发送重试已完成；后续如需优化，可关注误报率、告警节流、设备编号、时间戳和更正式的服务器协议。

## 9. 后续代码组织要求

与代码有关的新增工作，尤其是新增不同功能模块时，尽量以独立 `.c` / `.h` 文件形式封装，保持工程结构清晰，避免继续把逻辑堆到 `main.c`。

建议原则：

1. 单一职责：一个模块只负责一类功能，例如传感器驱动、告警判定、网络上报、低功耗管理、协议封包等。
2. 头文件只暴露必要接口和配置宏，内部状态、静态辅助函数留在 `.c` 文件里。
3. `main.c` 只保留系统初始化、主流程调度和少量应用入口逻辑。
4. 新模块命名尽量直观，例如：

```text
Hardware/Inc/lsm6dsr.h
Hardware/Src/lsm6dsr.c
Core/Inc/app_alarm.h
Core/Src/app_alarm.c
Core/Inc/app_lowpower.h
Core/Src/app_lowpower.c
Core/Inc/app_protocol.h
Core/Src/app_protocol.c
```

5. 如果后续新增云端/MQTT/HTTP/设备编号/时间戳/告警节流等功能，优先作为独立模块加入，再由 `main.c` 调用。
