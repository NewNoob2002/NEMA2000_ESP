# NMEA2000 CAN 调试与功能测试指南

本文档说明本项目中 NMEA2000/CAN 通道的调试方法，以及如何使用 CAN 调试工具发送测试报文、采集证据并支持后续功能测试和单元测试设计。

对应英文版：`Document/NMEA2000_CAN_Debugging_Guide.md`

## 测试团队摘要

本项目在 ESP32 TWAI 上暴露一个 NMEA2000 网关节点。测试团队需要验证三层内容：

1. 总线启动：250 kbit/s classic CAN、29-bit 扩展帧、收发器工作正常、终端电阻正确、NMEA2000 地址声明成功。
2. 网关输出：在 GNSS fix 有效且新鲜时，DUT 周期发送 PGN `129025`、`129026`、`129029`。
3. 网关控制：CAN 调试工具可以发送 ISO Request PGN `59904` 和 Group Function PGN `126208`，用于验证设备身份、PGN 列表和可配置发送周期。

最重要的发送报文如下：

| 用途 | CAN ID | Data |
| --- | --- | --- |
| 请求地址声明 | `0x18EA1650` | `00 EE 00 FF FF FF FF FF` |
| 请求 PGN 列表 | `0x18EA1650` | `00 EE 01 FF FF FF FF FF` |
| 请求产品信息 | `0x18EA1650` | `14 F0 01 FF FF FF FF FF` |
| 设置 PGN `129025` 为 500 ms，第 1 帧 | `0x0DED1650` | `00 0B 00 01 F8 01 F4 01` |
| 设置 PGN `129025` 为 500 ms，第 2 帧 | `0x0DED1650` | `01 00 00 FF FF 00 FF FF` |
| 禁止 PGN `129025`，第 1 帧 | `0x0DED1650` | `00 0B 00 01 F8 01 00 00` |
| 禁止 PGN `129025`，第 2 帧 | `0x0DED1650` | `01 00 00 FF FF 00 FF FF` |
| 恢复 PGN `129025`，第 1 帧 | `0x0DED1650` | `00 0B 00 01 F8 01 FE FF` |
| 恢复 PGN `129025`，第 2 帧 | `0x0DED1650` | `01 FF FF FF FF 00 FF FF` |

这些 ID 假设 DUT 源地址为 `0x16`，测试工具源地址为 `0x50`。执行点对点测试前，必须先从 PGN `60928` 地址声明中确认 DUT 的实际源地址。

## 项目 CAN 通道

本固件是基于 ESP-IDF 的 NMEA0183 GNSS 到 NMEA2000 网关。当前 NMEA2000 处理器位于 `main/App/DataProc/DP_NMEA2000.cpp`，底层 CAN 适配器使用 `components/NMEA2000/src/NMEA2000_esp32.cpp` 中的 ESP32 TWAI 驱动。

运行流程：

1. `DATA_PROC_INIT_DEF(NMEA2000)` 调用 `ConfigureGatewayNmea2000()` 配置网关。
2. `tNMEA2000_esp32::Open()` 启动 ESP32 TWAI 节点。
3. 10 ms 定时器周期调用 `nmea2000.ParseMessages()`。
4. 有效 UM980 GNSS 数据由 `BuildGatewayN2kMessages()` 转换为 NMEA2000 报文。
5. 到期的 NMEA2000 报文通过 `nmea2000.SendMsg()` 发送。

默认总线配置：

| 项目 | 值 |
| --- | --- |
| 物理控制器 | ESP32 片上 TWAI 控制器 |
| CAN 模式 | Classic CAN 2.0，29-bit 扩展帧 |
| CAN-FD | 不使用 |
| 波特率 | 250 kbit/s |
| 采样点 | 87.5 percent |
| TX 引脚 | GPIO 22，除非覆盖 `ESP32_CAN_TX_PIN` |
| RX 引脚 | GPIO 21，除非覆盖 `ESP32_CAN_RX_PIN` |
| NMEA2000 节点模式 | `N2km_ListenAndNode` |
| 首选源地址 | 22，但地址声明可能改变实际地址 |

首选源地址在 `main/nmea0183_to_n2k.cpp` 中配置：

```cpp
nmea2000.SetMode(tNMEA2000::N2km_ListenAndNode, 22);
```

十进制 `22` 等于十六进制 `0x16`。这是首选地址，不是绝对固定地址；运行时实际源地址必须通过 NMEA2000 地址声明结果确认。

## 网关预期发送的 PGN

网关声明并周期发送以下 GNSS PGN：

| PGN | 名称 | 源码接口 | 优先级 | 默认周期 | 默认 offset |
| --- | --- | --- | --- | --- | --- |
| 129025 | Position, Rapid Update | `SetN2kLatLonRapid()` | 2 | 1000 ms | 0 ms |
| 129026 | COG/SOG, Rapid Update | `SetN2kCOGSOGRapid()` | 2 | 1000 ms | 20 ms |
| 129029 | GNSS Position Data | `SetN2kGNSS()` | 3 | 1000 ms | 40 ms |

只有当 `ReadGatewayGnssData()` 和 `BuildGatewayN2kMessages()` 都成功时，固件才会发送这些报文。GNSS fix 超过 3000 ms 未更新时会被判定为 stale fix，并停止发送 GNSS 数据 PGN。

网关还支持对这三个发送 PGN 的 NMEA2000 Group Function 请求：

| 请求值 | 预期行为 |
| --- | --- |
| `N2k_KEEP_TRANSMISSION_INTERVAL` | 保持当前发送周期 |
| `N2k_RESTORE_TRANSMISSION_INTERVAL` | 恢复默认周期和默认 offset |
| `0` | 禁止该 PGN 发送 |
| `100` 到 `60000` ms | 设置新的发送周期 |
| Offset 字段不是 `0xffff` | 按 10 ms 单位应用 offset |

点对点 Group Function 请求会返回 acknowledge。广播请求会被处理，但不会返回 acknowledge。

## PGN、CAN ID 与 Payload 转换关系

NMEA2000 使用 29-bit 扩展 CAN ID。库中的转换函数是 `components/NMEA2000/src/NMEA2000.cpp` 里的 `N2ktoCanID()`。

CAN ID 字段：

| Bits | 字段 | 含义 |
| --- | --- | --- |
| 28..26 | Priority | 数值越小，CAN 仲裁优先级越高 |
| 24 | Data Page | PGN 的一部分 |
| 23..16 | PF | PDU format |
| 15..8 | PS 或 destination | PDU2 为 PGN 低字节；PDU1 为目标地址 |
| 7..0 | Source | 地址声明后的设备源地址 |

对于 PDU2 PGN，`PF >= 240`，PGN 已包含低字节，目标地址隐含为 global：

```text
CAN ID = (priority << 26) | (PGN << 8) | source
```

DUT 源地址为 `0x16` 时：

| PGN | Priority | CAN ID | 说明 |
| --- | --- | --- | --- |
| `129025` / `0x1F801` | 2 | `0x09F80116` | Position, Rapid Update |
| `129026` / `0x1F802` | 2 | `0x09F80216` | COG/SOG, Rapid Update |
| `129029` / `0x1F805` | 3 | `0x0DF80516` | GNSS Position Data，fast-packet |

对于 PDU1 PGN，`PF < 240`，PGN 低字节必须为 `00`，目标地址会插入 CAN ID：

```text
CAN ID = (priority << 26) | (PGN << 8) | (destination << 8) | source
```

测试工具源地址为 `0x50`、DUT 目标地址为 `0x16` 时：

| PGN | Priority | CAN ID | 用途 |
| --- | --- | --- | --- |
| `59904` / `0xEA00` | 6 | `0x18EA1650` | ISO Request |
| `126208` / `0x1ED00` | 3 | `0x0DED1650` | NMEA Group Function |

Payload 转换由 PGN 决定：

| PGN | 编码函数 | Payload 布局 |
| --- | --- | --- |
| `129025` | `SetN2kLatLonRapid()` | Latitude 4 字节，比例 `1e-7`；longitude 4 字节，比例 `1e-7` |
| `129026` | `SetN2kCOGSOGRapid()` | SID、COG reference、COG 2 字节比例 `0.0001 rad`、SOG 2 字节比例 `0.01 m/s`、reserved bytes |
| `129029` | `SetN2kGNSS()` | Date/time、latitude、longitude、altitude、GNSS type/method、satellites、DOP values；通过 fast-packet 发送 |
| `59904` | `SetN2kPGNISORequest()` | Requested PGN，3 字节小端 |
| `126208` | Group Function handler | Function code、target PGN、interval、offset、parameter-pair count |

Schedule 配置只改变对应 PGN 是否发送、发送周期和 offset；GNSS payload 内容仍然来自 UM980 数据路径。

## CAN 调试工具设置

使用支持 Classic CAN 扩展帧、250 kbit/s 的 CAN 适配器，例如 PCAN、Kvaser、CANable/candleLight、ValueCAN 或 Linux SocketCAN 适配器。

物理连接要求：

1. ESP32 TWAI 引脚和总线之间必须接 NMEA2000/CAN 收发器。
2. 正确连接 CAN-H、CAN-L 和 GND。
3. 总线上必须只有两个 120 ohm 终端电阻。
4. 如果使用真实 NMEA2000 线束，需要给 backbone 供电。
5. 被动抓包时建议调试工具使用 silent/listen-only 模式；只有注入测试报文时才切换为主动发送。

Linux SocketCAN 示例：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 250000 sample-point 0.875
sudo ip link set can0 up
candump -tz -x can0
```

Windows 厂商工具通道配置：

```text
Bitrate: 250000
Frame type: Extended 29-bit
CAN-FD: Disabled
Listen-only: Enabled for baseline capture
Timestamping: Enabled
```

## 基线抓包流程

1. 编译并烧录固件。
2. 打开串口 monitor，确认以下日志：

```text
NMEA2000 gateway configured
TWAI node started on TX=22 RX=21 bitrate=250000
NMEA2000 gateway opened
```

3. 在 GNSS 数据有效前启动 CAN 分析仪。
4. 至少抓取 60 秒总线数据。
5. 确认设备发出地址声明和产品/配置相关报文。
6. GNSS fixed 后，确认周期性出现 PGN `129025`、`129026`、`129029`。

通过标准：

| 检查项 | 预期结果 |
| --- | --- |
| 总线打开 | 串口日志出现 `NMEA2000 gateway opened` |
| CAN 状态 | 没有重复 TWAI error 日志 |
| 帧格式 | 只有 Classic CAN 扩展帧 |
| PGN 129025 | 有效 GNSS fix 后约 1 Hz |
| PGN 129026 | 约 1 Hz，比 129025 晚约 20 ms |
| PGN 129029 | 约 1 Hz，比 129025 晚约 40 ms |
| payload 有效性 | 位置、COG/SOG、UTC/date、fix method、卫星数与 GNSS 输入一致 |
| 错误计数 | TX fail 不持续增长，无 bus-off 或接收溢出 |

## 需要发送的测试报文

以下原始 CAN 示例使用这些假设：

| 符号 | 值 | 含义 |
| --- | --- | --- |
| DUT address | `0x16` | 网关源地址，固件首选地址为十进制 22 |
| Tester address | `0x50` | CAN 调试工具使用的源地址 |
| CAN type | Extended classic CAN | 29-bit ID，不使用 CAN-FD |
| Padding | `FF` | 如果工具要求 8 字节固定长度，未使用字节填 `FF` |

如果网关地址声明后实际地址不再是 `0x16`，需要把 CAN ID 中的 `16` 替换为 PGN `60928` 中观察到的实际源地址。

### ISO Request 冒烟测试

使用 PGN `59904` 请求标准设备信息。这些都是单帧报文。

| 测试 | CAN ID | 发送数据 | 预期响应 |
| --- | --- | --- | --- |
| 请求地址声明，PGN 60928 | `0x18EA1650` | `00 EE 00 FF FF FF FF FF` | 网关回复 PGN `60928` |
| 请求发送/接收 PGN 列表，PGN 126464 | `0x18EA1650` | `00 EE 01 FF FF FF FF FF` | 回复 PGN `126464`，发送列表包含 `129025`、`129026`、`129029` |
| 请求产品信息，PGN 126996 | `0x18EA1650` | `14 F0 01 FF FF FF FF FF` | 回复 PGN `126996`，model 文本为 `GNSS NMEA2000 gateway` |
| 请求配置信息，PGN 126998 | `0x18EA1650` | `16 F0 01 FF FF FF FF FF` | 回复 PGN `126998`，或在不可用时回复 ISO acknowledgement |

ISO Request 的 CAN ID 计算：

```text
priority 6, PGN 59904, destination 0x16, source 0x50
CAN ID = 0x18EA1650
```

### Group Function 报文格式

网关支持 PGN `126208` Request Group Function，用于配置 PGN `129025`、`129026`、`129029` 的发送周期。逻辑 payload 为 11 字节：

```text
Byte0      Group Function Code = 0x00
Byte1-3    Target PGN，小端
Byte4-7    TransmissionInterval，小端 uint32，单位 ms
Byte8-9    TransmissionIntervalOffset，小端 uint16，单位 10 ms
Byte10     NumberOfParameterPairs = 0x00
```

由于 payload 为 11 字节，原始 CAN 调试工具必须按 NMEA2000 Fast Packet 拆成两帧发送。在上述假设下，两帧都使用 CAN ID `0x0DED1650`：

```text
priority 3, PGN 126208, destination 0x16, source 0x50
CAN ID = 0x0DED1650
```

每帧第 1 个 data byte 是 fast-packet frame counter。第 0 帧的第 2 个 data byte 是逻辑 payload 长度 `0B`。

常用编码：

| 值 | 字节 |
| --- | --- |
| PGN `129025` | `01 F8 01` |
| PGN `129026` | `02 F8 01` |
| PGN `129029` | `05 F8 01` |
| interval `0 ms`，禁止发送 | `00 00 00 00` |
| interval `500 ms` | `F4 01 00 00` |
| interval `1000 ms` | `E8 03 00 00` |
| interval `2000 ms` | `D0 07 00 00` |
| interval 恢复默认，`0xfffffffe` | `FE FF FF FF` |
| interval 保持当前，`0xffffffff` | `FF FF FF FF` |
| offset 保持当前 | `FF FF` |
| offset `0 ms` | `00 00` |
| offset `20 ms` | `02 00` |
| offset `40 ms` | `04 00` |

### 周期配置测试

发送点对点 Group Function Request，配置 PGN `129025`、`129026` 或 `129029` 的发送周期，然后用 CAN 分析仪测量周期。

| 刺激 | 发送帧 | 预期结果 |
| --- | --- | --- |
| 设置 PGN `129025` 为 500 ms，保持 offset | `0x0DED1650  00 0B 00 01 F8 01 F4 01` 然后 `0x0DED1650  01 00 00 FF FF 00 FF FF` | PGN `129025` 约 500 ms 周期发送 |
| 设置 PGN `129025` 为 1000 ms，offset 0 ms | `0x0DED1650  00 0B 00 01 F8 01 E8 03` 然后 `0x0DED1650  01 00 00 00 00 00 FF FF` | PGN `129025` 约 1000 ms 周期发送 |
| 设置 PGN `129026` 为 1000 ms，offset 20 ms | `0x0DED1650  00 0B 00 02 F8 01 E8 03` 然后 `0x0DED1650  01 00 00 02 00 00 FF FF` | PGN `129026` 约 1000 ms 周期发送，并相对 `129025` 延后 |
| 设置 PGN `129029` 为 2000 ms，offset 40 ms | `0x0DED1650  00 0B 00 05 F8 01 D0 07` 然后 `0x0DED1650  01 00 00 04 00 00 FF FF` | PGN `129029` 约 2000 ms 周期发送 |
| 禁止 PGN `129025` | `0x0DED1650  00 0B 00 01 F8 01 00 00` 然后 `0x0DED1650  01 00 00 FF FF 00 FF FF` | PGN `129025` 停止发送 |
| 恢复 PGN `129025` 默认配置 | `0x0DED1650  00 0B 00 01 F8 01 FE FF` 然后 `0x0DED1650  01 FF FF FF FF 00 FF FF` | PGN `129025` 恢复约 1000 ms 周期 |

### Offset 测试

发送带有效 `TransmissionIntervalOffset` 的点对点 Group Function Request。固件将 offset 字段解释为 10 ms 单位。

预期结果：下一次首帧发送时间为 `now + offset * 10 ms`，后续发送按配置周期运行。

### 无效周期测试

发送低于 100 ms 或高于 60000 ms 的 Group Function Request。

预期结果：设备回复 `126208` Acknowledge，且 transmission/priority error code 为 `1`，表示 transmission interval or priority not supported。PGN 周期不应改变。

PGN `129025`、interval `50 ms` 的示例：

```text
0x0DED1650  00 0B 00 01 F8 01 32 00
0x0DED1650  01 00 00 FF FF 00 FF FF
```

### 参数对拒绝测试

当前固件只接受 `NumberOfParameterPairs = 0`。如果要测试拒绝逻辑，发送有效 interval，但把逻辑 payload 最后 1 字节设置为 `01`。

PGN `129025`、interval `1000 ms`、带 1 个不支持参数对的示例：

```text
0x0DED1650  00 0B 00 01 F8 01 E8 03
0x0DED1650  01 00 00 FF FF 01 FF FF
```

预期结果：设备回复 `126208` Acknowledge，并带 parameter error。配置周期不能改变。

### Acknowledge 解码

对于点对点 PGN `126208` 请求，将网关响应解码为 NMEA2000 Group Function Acknowledge：

```text
Byte0      Group Function Code = 0x02，Acknowledge
Byte1-3    Target PGN，小端
Byte4      低 4 bit：PGN error code；高 4 bit：transmission/priority error code
Byte5      NumberOfParameterPairs
```

重要错误码：

| 字段 | 值 | 含义 |
| --- | --- | --- |
| PGN error code | `0` | Acknowledge，无 PGN 错误 |
| PGN error code | `1` | PGN not supported |
| PGN error code | `4` | Request or command not supported |
| Transmission/priority error code | `0` | Acknowledge，无 interval/priority 错误 |
| Transmission/priority error code | `1` | Transmission interval or priority not supported |
| Parameter error code | `0` | Acknowledge，无参数错误 |
| Parameter error code | `5` | Request or command parameter not supported |

有些 CAN 工具会显示 NMEA2000 fast-packet 包装字节，有些工具会直接显示解码后的逻辑 PGN payload。优先使用 NMEA2000 decoder 视图。

### 广播请求测试

将 CAN ID 中的目标地址从 `0x16` 改为 `0xFF`：

```text
0x0DEDFF50  00 0B 00 01 F8 01 E8 03
0x0DEDFF50  01 00 00 00 00 00 FF FF
```

预期结果：配置生效，但因为请求是广播，网关不会发送 PGN `126208` acknowledge。

### GNSS Fix 丢失测试

屏蔽或断开 GNSS 输入，或者提供 stale data。

预期结果：网关仍会解析 NMEA2000 总线报文，但由于转换路径拒绝无效 GNSS 数据，PGN `129025`、`129026`、`129029` 停止发送。

## 抓包解码要点

NMEA2000 使用 29-bit CAN ID 编码 priority、PGN、destination 和 source。优先使用分析仪自带的 NMEA2000 decoder。如果手动解码，至少检查：

| 字段 | 校验 |
| --- | --- |
| IDE | 必须是 extended |
| RTR | 必须为 false |
| FDF/BRS | 必须为 false |
| Source | 通常为首选地址 22，除非地址声明改变 |
| Priority | PGN 129025/129026 为 2，PGN 129029 为 3 |
| PGN | 应为网关预期 PGN 或必要的 NMEA2000 管理 PGN |
| DLC | 单帧为 8；fast-packet 分段也使用 classic 8-byte CAN payload |

启动和网络交互中常见的管理 PGN：

| PGN | 用途 |
| --- | --- |
| 59904 | ISO Request |
| 60928 | ISO Address Claim |
| 126208 | NMEA group function |
| 126464 | Transmit/receive PGN list |
| 126996 | Product information |
| 126998 | Configuration information |

## TWAI 健康状态指标

ESP32 适配器在 `tNMEA2000_esp32::tCANStatus` 中记录健康计数：

| 计数器 | 含义 |
| --- | --- |
| `TxDoneCount` | TWAI 发送成功次数 |
| `TxFailCount` | 发送失败次数 |
| `RxFrameCount` | 驱动接受到的 classic extended frame 数 |
| `ErrorCount` | TWAI error callback 次数 |
| `StateChangeCount` | TWAI 状态变化次数 |
| `LastErrorFlags` | 最近一次 TWAI error flags |
| `LastState` | 最近一次 TWAI error state |
| `TxErrorCount` | 当前 TWAI TX error counter |
| `RxErrorCount` | 当前 TWAI RX error counter |
| `BusErrorCount` | 控制器 bus error 记录 |

常见故障线索：

| 现象 | 可能原因 | 检查 |
| --- | --- | --- |
| ACK error 或 TX fail | 无其他 active node、缺少终端电阻、波特率错误、收发器断开 | 增加第二个节点/分析仪，检查 250 kbit/s 和终端电阻 |
| bus-off recovery 日志 | 接线错误、CAN-H/CAN-L 反接、收发器损坏、总线短路 | 检查接线并用示波器看差分信号 |
| 无 RX frame | 分析仪 silent-only 本身可行，但真实总线应能看到其他 NMEA2000 节点 | 检查 backbone 供电和分析仪通道 |
| 无 GNSS PGN | GNSS fix 无效或 stale | 检查 UM980 串口日志和 fix age |
| 启动打开失败 | TWAI node 创建或配置失败 | 检查 ESP-IDF target 是否支持 TWAI，引脚是否有效 |

## 功能测试矩阵

新增或验证依赖 NMEA2000 通道的模块时，使用以下矩阵：

| Test ID | 范围 | 刺激 | 预期 CAN 证据 | 预期固件证据 |
| --- | --- | --- | --- | --- |
| N2K-BOOT-001 | 启动 | 连接分析仪后启动 | 出现地址声明和管理报文 | `NMEA2000 gateway opened` |
| N2K-GNSS-001 | 位置输出 | 提供有效 GNSS fix | PGN 129025 约 1 Hz | 发送失败计数不持续增长 |
| N2K-GNSS-002 | COG/SOG 输出 | 移动设备或回放速度/航向数据 | PGN 129026 约 1 Hz，解码 COG/SOG 与输入一致 | 无转换错误 |
| N2K-GNSS-003 | GNSS 详细输出 | 提供有效 UTC/date/fix/satellites | PGN 129029 约 1 Hz，字段与输入一致 | GNSS 数据被接受 |
| N2K-GNSS-004 | stale fix 拒绝 | 停止 GNSS 更新超过 3000 ms | PGN 129025/129026/129029 停止 | converter 拒绝 stale fix |
| N2K-GF-001 | 周期命令 | 请求 PGN 129025 周期为 500 ms | 点对点 acknowledge，PGN 129025 约 500 ms | schedule 更新 |
| N2K-GF-002 | 禁止命令 | 请求 PGN 129026 interval 为 0 | 点对点 acknowledge，PGN 129026 停止 | schedule disabled |
| N2K-GF-003 | 恢复命令 | 请求 restore interval | 点对点 acknowledge，PGN 恢复 1000 ms | schedule restored |
| N2K-GF-004 | 无效周期 | 请求 PGN 129025 周期为 50 ms | 点对点 acknowledge 报 interval unsupported，周期不变 | schedule 不改变 |
| N2K-GF-005 | 不支持参数对 | 请求中 `NumberOfParameterPairs = 1` | 点对点 acknowledge 报 parameter error，周期不变 | schedule 不改变 |
| N2K-GF-006 | 广播 group request | 发送 PGN 126208 到 destination `0xFF` | 配置改变，无 acknowledge | schedule 更新 |
| N2K-ISO-001 | ISO request | 用 PGN 59904 请求 PGN 126996 | 产品信息响应 | device info 可用 |
| N2K-ISO-002 | PGN list | 用 PGN 59904 请求 PGN 126464 | 发送列表包含 129025、129026、129029 | 设备声明预期 PGN |
| N2K-ERR-001 | 物理故障 | 短暂断开 CAN-H 或 CAN-L | 分析仪看到错误，设备可能进入恢复 | TWAI error/state 日志，然后恢复 |

## 单元测试目标

当前仓库还没有专用单元测试框架。如果后续引入 host-side test target，优先覆盖以下目标：

| 单元 | 测试输入 | 预期结果 |
| --- | --- | --- |
| `ReadGatewayGnssData()` | 有效 UM980 fix 且 fix age 新鲜 | 返回 true；填充 latitude、longitude、altitude、COG、SOG、date、time、satellites 和 GNSS method |
| `ReadGatewayGnssData()` | fix age 大于 3000 ms | 返回 false |
| `ReadGatewayGnssData()` | RTK fixed、RTK float、DGNSS、normal fix、no fix | 映射到对应 `tN2kGNSSmethod` |
| `BuildGatewayN2kMessages()` | 完整 `tGatewayGnssData` | 构建 PGN `129025`、`129026`、`129029` |
| `BuildGatewayN2kMessages()` | 缺少 position、time、date、speed/course 或 fix | 返回 false |
| `tGatewayPgnGroupFunctionHandler::HandleRequest()` | Interval `0` | 禁止对应 schedule |
| `tGatewayPgnGroupFunctionHandler::HandleRequest()` | Interval `500`，offset keep-current | 应用 500 ms 周期 |
| `tGatewayPgnGroupFunctionHandler::HandleRequest()` | Restore default interval | 恢复默认周期和 offset |
| `tGatewayPgnGroupFunctionHandler::HandleRequest()` | Interval 低于 100 ms 或高于 60000 ms | 拒绝请求，schedule 不改变 |
| `tGatewayPgnGroupFunctionHandler::HandleRequest()` | 参数对数量非 0 | 拒绝请求，schedule 不改变 |

## 遗漏项检查与开放项

本文档已经覆盖总线设置、预期 PGN、CAN ID、原始刺激报文、周期检查、负向测试、单元测试目标和证据格式。剩余内容不是固件文档遗漏，而是每次测试运行前需要由测试团队确认或记录的实验条件：

| 项目 | 状态 | 测试团队动作 |
| --- | --- | --- |
| DUT 实际源地址 | 运行时相关 | 抓取 PGN `60928`；如果不是 `0x16`，替换点对点 CAN ID 中的 `16` |
| CAN 适配器型号/通道 | 实验室相关 | 记录适配器名称、序列号、通道、驱动/工具版本 |
| 物理总线拓扑 | 实验室相关 | 记录终端电阻、线束、backbone 供电、收发器板 |
| GNSS 刺激源 | 实验室相关 | 记录 live sky、simulator、serial replay 或 UM980 canned data |
| PGN `129029` 的精确解码值 | 输入相关 | 每次运行都与已知 GNSS 输入进行对比 |
| 自动化单元测试框架 | 仓库中尚未提供 | 在声明自动化单元测试覆盖前，需要新增 host-side tests |
| `tCANStatus` 的运行时直接读取接口 | 当前没有 CLI/API 暴露 | 除非新增 debug endpoint，否则使用串口日志和 CAN 证据 |
| 厂商 CAN 工具命令语法 | 工具相关 | 将本文档中的 raw ID/data 转换为 PCAN/Kvaser/Vector 等工具的发送格式 |

测试文档交付建议门槛：

1. 一份 baseline boot 和地址声明原始 CAN 抓包。
2. 一份有效 GNSS 输出至少 60 秒的原始 CAN 抓包。
3. 一份覆盖 interval change、disable、restore、invalid interval、parameter-pair rejection、broadcast request 的抓包。
4. 一份与 CAN 抓包固件 commit 对应的串口日志。
5. 每一种硬件/测试工具组合都填写一份 evidence template。

## 证据模板

每次验证都保存原始抓包和简短解码报告。

```text
Test run:
Firmware commit:
ESP-IDF version:
Target hardware:
CAN adapter:
Channel:
Bitrate:
Sample point:
Listen-only:
Bus termination:
Capture file:
Serial log:

Observed frames:
- PGN:
  Source:
  Priority:
  DLC:
  Period min/avg/max:
  Payload summary:
  Pass/fail:

TWAI status:
- TxDoneCount:
- TxFailCount:
- RxFrameCount:
- ErrorCount:
- StateChangeCount:
- LastState:
- TxErrorCount:
- RxErrorCount:
- BusErrorCount:

Result:
Notes:
```

## 维护说明

以下内容发生变化时，需要同步更新本文档和英文版：

1. CAN 引脚、波特率、采样点或 TWAI 模式。
2. `main/nmea0183_to_n2k.cpp` 中的发送 PGN 列表。
3. `main/App/DataProc/DP_NMEA2000.cpp` 中的调度逻辑。
4. `components/NMEA2000/src/NMEA2000_esp32.*` 中的驱动行为或状态计数。
5. 测试工具、适配器或证据格式要求。
