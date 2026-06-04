# NMEA2000 src 组件使用手册

本文档面向本仓库 `components/NMEA2000/src` 下的 NMEA2000 协议栈源码，说明它在 ESP-IDF 工程中的集成方式、核心类、消息收发、PGN 编解码、设备信息、地址声明、转发、设备发现、组功能和调试要点。

本文档只覆盖 `components/NMEA2000/src` 这个通用协议栈组件。ESP32 TWAI/CAN 硬件驱动后端位于兄弟组件 `components/NMEA2000_ESP32`，本工程实际使用时通常需要二者配合。

## 1. 组件定位

`components/NMEA2000/src` 是一个跨平台 NMEA 2000 协议栈核心库，主要负责：

- NMEA2000 节点生命周期管理。
- ISO 地址声明和动态源地址处理。
- 单帧、Fast Packet、ISO Transport Protocol 多包消息组包和拆包。
- 常见 NMEA2000 PGN 的 Set/Parse 编解码。
- 产品信息、配置描述、支持的 PGN 列表、心跳等标准系统 PGN。
- Actisense 二进制格式和 SeaSmart `$PCDIN` 文本格式转换。
- 设备列表发现和产品/配置信息查询。
- PGN 126208 Group Function 默认处理和扩展处理。

它不直接操作 ESP32 CAN 控制器。`tNMEA2000` 是抽象基类，真正的 CAN 发送、接收和打开总线由子类实现。本仓库 ESP32 子类是 `tNMEA2000_esp32`。

## 2. 文件地图

核心文件：

- `NMEA2000.h/.cpp`：协议栈主类 `tNMEA2000`，节点模式、地址声明、收发、转发、系统 PGN、组功能入口。
- `N2kMsg.h/.cpp`：消息容器 `tN2kMsg`，二进制字段写入/读取工具。
- `N2kMessages.h/.cpp`：标准 PGN 的 `SetN2k...` 和 `ParseN2k...` 函数。
- `N2kTypes.h`、`NMEA2000StdTypes.h`：枚举类型和标准数据字典值。
- `N2kCANMsg.h`：内部接收缓存项，应用通常不直接使用。
- `N2kTimer.h/.cpp`：毫秒计时、同步调度器、普通调度器。
- `N2kDeviceList.h/.cpp`：总线设备发现和设备信息缓存。
- `N2kGroupFunction.h/.cpp`：PGN 126208 Group Function 基类和通用处理。
- `N2kGroupFunctionDefaultHandlers.h/.cpp`：60928、126464、126993、126996、126998 默认组功能处理器。
- `ActisenseReader.h/.cpp`：从流读取 Actisense 格式并转换为 `tN2kMsg`。
- `Seasmart.h/.cpp`：`tN2kMsg` 与 `$PCDIN` SeaSmart 句互转。
- `N2kMaretron.h/.cpp`：Maretron 私有/厂商 PGN 辅助函数。
- `N2kCZone.h/.cpp`：CZone 厂商 PGN 解析辅助函数。
- `N2kStream.h/.cpp`：跨平台流接口。Arduino 下映射到 `Stream`，非 Arduino 下需自行实现。
- `RingBuffer.h/.tpp`：环形缓冲和优先级环形缓冲模板。
- `NMEA2000_CAN.h`：Arduino/多平台自动选择 CAN 后端的包装头。本 ESP-IDF 工程通常直接包含 `NMEA2000_esp32.h`。
- `NMEA2000_CompilerDefns.h`：编译期开关。
- `CMakeLists.txt`：host 单元测试用构建逻辑；ESP-IDF 下会直接返回。

ESP-IDF 注册发生在 `components/NMEA2000/CMakeLists.txt`：

```cmake
idf_component_register(SRC_DIRS src INCLUDE_DIRS src REQUIRES esp_timer driver arduino-esp32)
```

## 3. 最小 ESP-IDF 用法

在本仓库 ESP32 工程中，推荐直接使用 `tNMEA2000_esp32`：

```cpp
#include "NMEA2000_esp32.h"
#include "N2kMessages.h"

static tNMEA2000_esp32 NMEA2000(GPIO_NUM_22, GPIO_NUM_21);

void init_n2k() {
    NMEA2000.SetProductInformation(
        "00000001",
        100,
        "NMEA0183 GNSS gateway",
        "0.1.0",
        "0.1.0"
    );

    NMEA2000.SetDeviceInformation(
        1,      // UniqueNumber, 21 bit
        132,    // DeviceFunction
        25,     // DeviceClass
        2046    // ManufacturerCode
    );

    static const unsigned long TxPGNs[] = {
        129025L, // Position Rapid Update
        129026L, // COG/SOG Rapid Update
        129029L, // GNSS Position Data
        0
    };
    NMEA2000.ExtendTransmitMessages(TxPGNs);

    NMEA2000.SetMode(tNMEA2000::N2km_ListenAndNode, 22);
    NMEA2000.Open();
}

void tick_n2k() {
    NMEA2000.ParseMessages();
}

void send_position(double latDeg, double lonDeg) {
    tN2kMsg msg;
    SetN2kLatLonRapid(msg, latDeg, lonDeg);
    NMEA2000.SendMsg(msg);
}
```

关键规则：

- 先设置产品信息、设备信息、发送/接收 PGN 列表和模式，再调用 `Open()`。
- `ParseMessages()` 必须高频周期调用，推荐在任务循环或定时事件里 5 ms 到 20 ms 一次。
- NMEA2000 标准速率是 250 kbit/s，`tNMEA2000_esp32` 默认就是 250 kbit/s。
- `SendMsg()` 只负责排队发送；底层是否真正发出还受 CAN 队列、地址声明状态和总线状态影响。

本仓库已有示例：

- `main/nmea0183_to_n2k.cpp` 的 `ConfigureGatewayNmea2000()` 设置产品信息、设备信息、发送 PGN 和节点模式。
- `main/App/DataProc/DP_NMEA2000.cpp` 初始化 `tNMEA2000_esp32`，定时调用 `ParseMessages()`，收到 GNSS NMEA0183 后调用 `SendMsg()`。

## 4. tNMEA2000 主类

`tNMEA2000` 是协议栈入口。应用通常操作其派生类对象，例如 `tNMEA2000_esp32`。

### 4.1 节点模式

`SetMode(tN2kMode mode, uint8_t source)` 设置协议栈工作模式：

- `N2km_ListenAndNode`：既监听总线又作为 NMEA2000 节点参与地址声明并发送消息。传感器、网关通常用这个模式。
- `N2km_NodeOnly`：作为节点发送和处理系统消息，不做通用转发监听用途。
- `N2km_ListenOnly`：只监听，不参与地址声明，不作为标准节点发送。
- `N2km_SendOnly`：只发送，通常用于特殊桥接或测试场景。

推荐生产设备使用 `N2km_ListenAndNode`。

`source` 是初始源地址，范围 0..251。库会执行地址声明，如果冲突会自动选择其他地址。不要把源地址当作长期唯一设备 ID，长期识别应使用 64 位 NAME。

### 4.2 打开和主循环

```cpp
bool ok = NMEA2000.Open();
NMEA2000.ParseMessages();
```

- `Open()` 初始化内部缓冲、CAN 后端、默认组功能处理器，并启动地址声明。
- `ParseMessages()` 非阻塞读取 CAN 帧，完成单帧/Fast Packet/TP 拆包，处理系统消息，调用用户消息回调，并发送内部待发送帧。
- 如果长时间不调用 `ParseMessages()`，接收队列会堆积，Fast Packet 可能超时，地址声明、ISO 请求、心跳和组功能响应也会延迟。

`Restart()` 可重新初始化地址声明流程，但一般应用不需要频繁调用。

### 4.3 产品信息

每个作为节点的设备都应设置产品信息：

```cpp
NMEA2000.SetProductInformation(
    "serial",
    100,
    "Model ID",
    "Software version",
    "Model version",
    1,
    2101,
    0
);
```

字段含义：

- `ModelSerialCode`：序列号，最长 32 字符。
- `ProductCode`：厂商产品代码。
- `ModelID`：型号名，最长 32 字符。
- `SwCode`：软件版本，最长 32 字符。
- `ModelVersion`：硬件/型号版本，最长 32 字符。
- `LoadEquivalency`：LEN，单位为 50 mA。
- `N2kVersion`：支持的 NMEA2000 标准版本。
- `CertificationLevel`：认证级别。

也可以构造 `tNMEA2000::tProductInformation` 后调用重载版本。

### 4.4 设备 NAME / 设备信息

```cpp
NMEA2000.SetDeviceInformation(
    uniqueNumber,
    deviceFunction,
    deviceClass,
    manufacturerCode,
    industryGroup,
    systemInstance,
    deviceInstance
);
```

NAME 是 NMEA2000 用于地址声明优先级和设备唯一识别的 64 位字段，由以下值组成：

- `UniqueNumber`：厂商内部唯一号，21 bit。
- `ManufacturerCode`：NMEA 分配的厂商码，11 bit。
- `DeviceFunction`：设备功能。
- `DeviceClass`：设备类别。
- `IndustryGroup`：行业组，船舶设备通常为 Marine。
- `SystemInstance`：系统实例。
- `DeviceInstance`：设备实例。

本仓库示例使用：

```cpp
nmea2000.SetDeviceInformation(1, 132, 25, 2046);
```

如果要发布可认证或可与多厂家设备长期共存的产品，应使用正式分配的 Manufacturer Code、Product Code 和稳定唯一号。

### 4.5 发送和接收 PGN 列表

设备应声明自己发送和接收的 PGN：

```cpp
static const unsigned long TxPGNs[] = {129025L, 129026L, 129029L, 0};
static const unsigned long RxPGNs[] = {59904L, 126208L, 0};

NMEA2000.ExtendTransmitMessages(TxPGNs);
NMEA2000.ExtendReceiveMessages(RxPGNs);
```

数组必须以 `0` 结束。

相关 API：

- `ExtendTransmitMessages(PGNs, iDev)`：追加发送 PGN。
- `ExtendReceiveMessages(PGNs, iDev)`：追加接收 PGN。
- `SetSingleFrameMessages(PGNs)`：声明库应按单帧处理的 PGN。
- `SetFastPacketMessages(PGNs)`：声明库应按 Fast Packet 处理的 PGN。
- `ExtendSingleFrameMessages(PGNs)` / `ExtendFastPacketMessages(PGNs)`：扩展默认识别列表。
- `IsTxPGN(PGN, iDev)`：检查设备是否声明发送某 PGN。

通常只需要设置 Tx/Rx PGN。N2kMessages 中已覆盖的标准 PGN，库已经知道哪些是单帧或 Fast Packet。

### 4.6 缓冲配置

必须在 `Open()` 前设置：

```cpp
NMEA2000.SetN2kCANMsgBufSize(8);
NMEA2000.SetN2kCANSendFrameBufSize(40);
NMEA2000.SetN2kCANReceiveFrameBufSize(50);
```

含义：

- `SetN2kCANMsgBufSize()`：协议层正在组装的 N2K 消息数量，影响 Fast Packet/TP 同时接收能力。
- `SetN2kCANSendFrameBufSize()`：CAN 发送帧缓存。
- `SetN2kCANReceiveFrameBufSize()`：CAN 接收帧缓存。

ESP32 后端在 `InitCANFrameBuffers()` 中会保证接收至少 50 帧、发送全局队列约 36 帧，协议层保留 4 帧发送缓存。

### 4.7 心跳

`SetHeartbeatIntervalAndOffset(interval, offset, iDev)` 配置 PGN 126993 心跳周期和偏移。

```cpp
NMEA2000.SetHeartbeatIntervalAndOffset(60000, 0);
```

`SendHeartbeat()` 通常由库内部调度。旧 API `SetHeartbeatInterval()` 已标记为 deprecated。

### 4.8 多设备支持

一个物理控制器可以模拟多个 NMEA2000 逻辑设备：

```cpp
NMEA2000.SetDeviceCount(2);
NMEA2000.SetProductInformation(product0, 0);
NMEA2000.SetProductInformation(product1, 1);
NMEA2000.SetDeviceInformation(..., 0);
NMEA2000.SetDeviceInformation(..., 1);
NMEA2000.SendMsg(msg, 1);
```

每个设备有自己的 NAME、源地址、发送 PGN 列表和地址声明状态。`DeviceIndex` 从 0 开始。必须在 `Open()` 前设置设备数和各设备信息。

## 5. tN2kMsg 消息容器

`tN2kMsg` 保存一条完整 NMEA2000 消息：

- `Priority`：CAN 优先级，0 最高，7 最低。
- `PGN`：Parameter Group Number。
- `Source`：源地址。
- `Destination`：目标地址，广播为 255。
- `DataLen`：数据长度。
- `Data[223]`：数据区。223 字节来自 Fast Packet 最大长度。
- `MsgTime`：接收时间戳，毫秒。

常用方法：

- `Clear()`：清空消息。
- `Init(priority, pgn, source, destination)`：初始化元信息。
- `SetPGN(pgn)`：设置 PGN。
- `ForceSource(source)`：强制源地址。普通发送不建议使用，让库填当前设备源地址即可。
- `CheckDestination()`：检查 PGN 是否可点对点，否则自动置广播。
- `IsValid()`：`PGN != 0 && DataLen > 0`。
- `GetRemainingDataLength(index)`：解析时检查剩余数据。
- `GetAvailableDataLength()`：剩余可写空间。

### 5.1 字段写入

直接构造自定义 PGN 时使用：

```cpp
tN2kMsg msg;
msg.Init(3, 130000L, 0, 255);
msg.AddByte(1);
msg.Add2ByteUInt(1234);
msg.Add4ByteUDouble(12.34, 0.01);
msg.AddVarStr("text");
NMEA2000.SendMsg(msg);
```

常用写入 API：

- `AddByte()`
- `Add2ByteInt()` / `Add2ByteUInt()`
- `Add3ByteInt()`
- `Add4ByteUInt()`
- `AddUInt64()`
- `Add1ByteDouble()` / `Add1ByteUDouble()`
- `Add2ByteDouble()` / `Add2ByteUDouble()`
- `Add3ByteDouble()` / `Add3ByteUDouble()`
- `Add4ByteDouble()` / `Add4ByteUDouble()`
- `Add8ByteDouble()`
- `AddFloat()`
- `AddStr()`
- `AddAISStr()`
- `AddVarStr()`
- `AddBuf()`

NMEA2000 字段通常是小端定点数。`precision` 表示分辨率，例如 `0.01` 表示写入值会按 0.01 单位缩放为整数。

### 5.2 字段读取

解析自定义 PGN 时使用：

```cpp
int index = 0;
uint8_t instance = msg.GetByte(index);
double voltage = msg.Get2ByteUDouble(0.01, index);
```

常用读取 API：

- `GetByte(index)`
- `Get2ByteInt(index)` / `Get2ByteUInt(index)`
- `Get3ByteUInt(index)`
- `Get4ByteUInt(index)`
- `GetUInt64(index)`
- `Get1ByteDouble()` / `Get1ByteUDouble()`
- `Get2ByteDouble()` / `Get2ByteUDouble()`
- `Get3ByteDouble()` / `Get3ByteUDouble()`
- `Get4ByteDouble()` / `Get4ByteUDouble()`
- `Get8ByteDouble()`
- `GetFloat()`
- 字符串和 buffer 读取函数，按 `N2kMsg.h` 的具体声明使用。

`index` 会随读取自动前进。解析前应检查 `PGN`，解析后可以根据返回值判断数据长度是否足够。

### 5.3 Not Available 值

库定义了统一 NA 常量：

- `N2kDoubleNA`
- `N2kFloatNA`
- `N2kUInt8NA` / `N2kInt8NA`
- `N2kUInt16NA` / `N2kInt16NA`
- `N2kUInt32NA` / `N2kInt32NA`
- `N2kUInt64NA` / `N2kInt64NA`

判断使用 `N2kIsNA(value)`。发送未知值时不要随便填 0，应填对应 NA 值。

## 6. 标准 PGN 编解码

`N2kMessages.h` 提供大量 `SetN2kPGNxxxxx()`、`ParseN2kPGNxxxxx()`，并为常见消息提供更易读别名。

一般模式：

```cpp
tN2kMsg msg;
SetN2kWaterDepth(msg, sid, depthBelowTransducer, offset, range);
NMEA2000.SendMsg(msg);
```

接收解析：

```cpp
void onN2kMessage(const tN2kMsg& msg) {
    if (msg.PGN == 128267L) {
        unsigned char sid;
        double depth;
        double offset;
        double range;
        if (ParseN2kWaterDepth(msg, sid, depth, offset, range)) {
            // depth unit: m
        }
    }
}
```

### 6.1 单位约定

库 API 尽量使用 SI 单位和 NMEA2000 标准单位：

- 角度：弧度，部分函数名/注释会说明。可用 `DegToRad()`、`RadToDeg()` 转换。
- 经纬度：度。
- 温度：Kelvin。可用 `CToKelvin()`、`KelvinToC()`、`FToKelvin()`、`KelvinToF()`。
- 压力：Pa。可用 `mBarToPascal()`、`PascalTomBar()`、`hPAToPascal()`、`PascalTohPA()`。
- 速度：m/s。可用 `KnotsToms()`、`msToKnots()`、`msToMPH()`。
- 电流：A。
- 电压：V。
- 距离/深度/高度：m。
- 时间：日期为自 1970-01-01 起的天数，时间为当天 UTC 秒数。

不要把显示单位直接传入 API，例如节、摄氏度、度角通常需要先转换。

### 6.2 常用 PGN 快速表

系统和时间：

| PGN | 别名 | 说明 |
| --- | --- | --- |
| 126992 | `SetN2kSystemTime` / `ParseN2kSystemTime` | 系统日期和 UTC 时间 |
| 126993 | 内部/组功能处理 | 心跳 |
| 126996 | `SetN2kPGN126996` / `ParseN2kPGN126996` | 产品信息 |
| 126998 | `SetN2kPGN126998` / `ParseN2kPGN126998` | 配置信息 |

姿态、航向和船速：

| PGN | 别名 | 说明 |
| --- | --- | --- |
| 127245 | `SetN2kRudder` / `ParseN2kRudder` | 舵角 |
| 127250 | `SetN2kTrueHeading`、`SetN2kMagneticHeading` / `ParseN2kHeading` | 航向 |
| 127251 | `SetN2kRateOfTurn` / `ParseN2kRateOfTurn` | 转向率 |
| 127252 | `SetN2kHeave` / `ParseN2kHeave` | Heave |
| 127257 | `SetN2kAttitude` / `ParseN2kAttitude` | Yaw/Pitch/Roll |
| 127258 | `SetN2kMagneticVariation` / `ParseN2kMagneticVariation` | 磁偏角 |
| 128000 | `SetN2kLeeway` / `ParseN2kLeeway` | Leeway |
| 128259 | `SetN2kBoatSpeed` / `ParseN2kBoatSpeed` | 船速 |
| 128267 | `SetN2kWaterDepth` / `ParseN2kWaterDepth` | 水深 |
| 128275 | `SetN2kDistanceLog` / `ParseN2kDistanceLog` | 航程日志 |

GNSS、导航和 AIS：

| PGN | 别名 | 说明 |
| --- | --- | --- |
| 129025 | `SetN2kLatLonRapid` / `ParseN2kPositionRapid` | 经纬度快速更新 |
| 129026 | `SetN2kCOGSOGRapid` / `ParseN2kCOGSOGRapid` | COG/SOG 快速更新 |
| 129029 | `SetN2kGNSS` / `ParseN2kGNSS` | GNSS 位置数据 |
| 129033 | `SetN2kLocalOffset` / `ParseN2kLocalOffset` | 本地时间偏移 |
| 129038 | `SetN2kAISClassAPosition` / `ParseN2kAISClassAPosition` | AIS A 类位置 |
| 129039 | `SetN2kAISClassBPosition` / `ParseN2kAISClassBPosition` | AIS B 类位置 |
| 129041 | `SetN2kAISAtoNReport` / `ParseN2kAISAtoNReport` | AIS AtoN |
| 129283 | `SetN2kXTE` / `ParseN2kXTE` | Cross Track Error |
| 129284 | `SetN2kNavigationInfo` / `ParseN2kNavigationInfo` | 导航信息 |
| 129285 | `SetN2kRouteWPInfo` | 航线/航点信息 |
| 129539 | `SetN2kGNSSDOPData` / `ParseN2kGNSSDOPData` | GNSS DOP |
| 129540 | `SetN2kGNSSSatellitesInView` / `ParseN2kPGNSatellitesInView` | 可见卫星 |
| 129794 | `SetN2kAISClassAStatic` / `ParseN2kAISClassAStatic` | AIS A 类静态 |
| 129802 | `SetN2kAISSafetyRelatedBroadcastMsg` / `ParseN2kAISSafetyRelatedBroadcastMsg` | AIS 安全广播 |
| 129809 | `SetN2kAISClassBStaticPartA` / `ParseN2kAISClassBStaticPartA` | AIS B 静态 A |
| 129810 | `SetN2kAISClassBStaticPartB` / `ParseN2kAISClassBStaticPartB` | AIS B 静态 B |
| 130074 | `SetN2kWaypointList` | 航点列表 |

发动机、电气和液位：

| PGN | 别名 | 说明 |
| --- | --- | --- |
| 127488 | `SetN2kEngineParamRapid` / `ParseN2kEngineParamRapid` | 发动机快速参数 |
| 127489 | `SetN2kEngineDynamicParam` / `ParseN2kEngineDynamicParam` | 发动机动态参数 |
| 127493 | `SetN2kTransmissionParameters` / `ParseN2kTransmissionParameters` | 变速箱参数 |
| 127497 | `SetN2kEngineTripParameters` / `ParseN2kEngineTripParameters` | 发动机行程参数 |
| 127501 | `SetN2kBinaryStatus` / `ParseN2kBinaryStatus` | 二进制开关状态 |
| 127502 | `SetN2kSwitchbankControl` / `ParseN2kSwitchbankControl` | 开关组控制 |
| 127505 | `SetN2kFluidLevel` / `ParseN2kFluidLevel` | 液位 |
| 127506 | `SetN2kDCStatus` / `ParseN2kDCStatus` | DC 详细状态 |
| 127507 | `SetN2kChargerStatus` / `ParseN2kChargerStatus` | 充电器状态 |
| 127508 | `SetN2kDCBatStatus` / `ParseN2kDCBatStatus` | 电池状态 |
| 127510 | `SetN2kChargerConf` / `ParseN2kChargerConf` | 充电器配置 |
| 127513 | `SetN2kBatConf` / `ParseN2kBatConf` | 电池配置 |
| 127750 | `SetN2kDCConvStatus` / `ParseN2kDCConvStatus` | DC 转换器状态 |
| 127751 | `SetN2kDCVoltageCurrent` / `ParseN2kDCVoltageCurrent` | DC 电压/电流 |

环境：

| PGN | 别名 | 说明 |
| --- | --- | --- |
| 130306 | `SetN2kWindSpeed` / `ParseN2kWindSpeed` | 风速风向 |
| 130310 | `SetN2kOutsideEnvironmentalParameters` / `ParseN2kOutsideEnvironmentalParameters` | 外部环境 |
| 130311 | `SetN2kEnvironmentalParameters` / `ParseN2kEnvironmentalParameters` | 环境参数 |
| 130312 | `SetN2kTemperature` / `ParseN2kTemperature` | 温度 |
| 130313 | `SetN2kHumidity` / `ParseN2kHumidity` | 湿度 |
| 130314 | `SetN2kPressure` / `ParseN2kPressure` | 压力 |
| 130315 | `SetN2kSetPressure` | 设定压力 |
| 130316 | `SetN2kTemperatureExt` / `ParseN2kTemperatureExt` | 扩展温度 |
| 130323 | `SetN2kMeteorlogicalStationData` / `ParseN2kMeteorlogicalStationData` | 气象站数据 |
| 130576 | `SetN2kTrimTab` / `ParseN2kTrimTab` | Trim tab |
| 130577 | `SetN2kDirectionData` / `ParseN2kDirectionData` | 方向数据 |

厂商扩展：

- `N2kMaretron.h`：Maretron 高精度温度、流量、行程容量等 PGN。
- `N2kCZone.h`：CZone switch status PGN 解析。

## 7. 发送流程

推荐发送标准 PGN：

```cpp
tN2kMsg msg;
SetN2kGNSS(
    msg,
    1,
    daysSince1970,
    secondsSinceMidnight,
    latitudeDeg,
    longitudeDeg,
    altitudeM,
    N2kGNSSt_GPS,
    N2kGNSSm_GNSSfix,
    satellites,
    hdop,
    N2kDoubleNA,
    geoidalSeparationM,
    0
);

if (!NMEA2000.SendMsg(msg)) {
    // 发送队列满、CAN 未打开、地址声明未就绪等情况都可能失败
}
```

发送注意事项：

- `SetN2k...` 会设置 PGN、默认优先级和数据字段。
- 如需改变优先级，应在 `SetN2k...` 后修改 `msg.Priority`。
- 普通应用不要手动设置 `msg.Source`，`SendMsg()` 会用当前设备源地址。
- 点对点消息要设置 `msg.Destination`，但只有 PGN 低字节为 0 的 PDU1 PGN 支持目标地址。
- 大于 8 字节的消息由库自动拆为 Fast Packet 或 ISO TP，前提是 PGN 被识别为对应类型。
- 多逻辑设备发送时使用 `SendMsg(msg, deviceIndex)`。

## 8. 接收流程

有两种接收方式。

### 8.1 单个函数回调

```cpp
void HandleN2kMessage(const tN2kMsg& msg) {
    switch (msg.PGN) {
        case 129025L: {
            double lat;
            double lon;
            if (ParseN2kPositionRapid(msg, lat, lon)) {
                // use lat/lon
            }
            break;
        }
    }
}

NMEA2000.SetMsgHandler(HandleN2kMessage);
```

### 8.2 多个处理器

继承 `tNMEA2000::tMsgHandler`：

```cpp
class MyHandler : public tNMEA2000::tMsgHandler {
public:
    MyHandler(tNMEA2000* n2k) : tMsgHandler(0, n2k) {}

    void HandleMsg(const tN2kMsg& msg) override {
        // parse msg
    }
};

static MyHandler handler(&NMEA2000);
NMEA2000.AttachMsgHandler(&handler);
```

如果多个模块都要处理总线消息，优先使用 `AttachMsgHandler()`，避免全局单回调被覆盖。

### 8.3 只处理已知 PGN

```cpp
NMEA2000.SetHandleOnlyKnownMessages(true);
```

启用后，库只把已声明为单帧/Fast Packet/TP 或系统已知的 PGN 交给应用处理。调试和网关通常不建议开启，以免漏掉厂商 PGN。

## 9. 转发和日志

`tNMEA2000` 可把收到的消息转发为 Actisense 或文本格式到 `N2kStream`。

```cpp
NMEA2000.SetForwardStream(&stream);
NMEA2000.SetForwardType(tNMEA2000::fwdt_Actisense);
NMEA2000.EnableForward(true);
```

相关开关：

- `EnableForward(true)`：启用转发。
- `SetForwardType(type)`：设置转发格式。
- `SetForwardSystemMessages(true)`：是否转发地址声明、ISO 请求等系统消息。
- `SetForwardOnlyKnownMessages(true)`：只转发已知消息。
- `SetForwardOwnMessages(true)`：是否转发本节点自己发送的消息。

`N2kStream` 在 Arduino 下是 `Stream`。在 ESP-IDF 纯 C++ 环境中，如果不用 Arduino `Stream`，需要实现 `read()`、`peek()`、`write()`。

## 10. ActisenseReader

`tActisenseReader` 用于从串口、USB、TCP 等流读取 Actisense 二进制消息，并转成 `tN2kMsg`：

```cpp
tActisenseReader reader;
reader.SetReadStream(&stream);

tN2kMsg msg;
if (reader.GetMessageFromStream(msg)) {
    NMEA2000.SendMsg(msg);
}
```

也可以注册回调：

```cpp
reader.SetMsgHandler(onActisenseMessage);
reader.ParseMessages();
```

格式支持：

- `0x10 0x02 0x93 ... 0x10 0x03`
- `0x10 0x02 0x94 ... 0x10 0x03`

`SetDefaultSource()` 用于对没有源地址的 Actisense 数据请求类消息补源地址。

## 11. SeaSmart `$PCDIN`

`Seasmart.h` 提供两个函数：

```cpp
char buffer[128];
size_t n = N2kToSeasmart(msg, timestamp, buffer, sizeof(buffer));

uint32_t ts;
tN2kMsg parsed;
bool ok = SeasmartToN2k(buffer, ts, parsed);
```

用途：

- 把 NMEA2000 消息转换为 NMEA0183 风格文本句，便于 TCP/日志/网关传输。
- 从 SeaSmart 文本恢复 `tN2kMsg`。

`N2kToSeasmart()` 的 buffer 至少需要 `30 + 2 * msg.DataLen` 字节。

## 12. 设备列表 tN2kDeviceList

`tN2kDeviceList` 是一个消息处理器，用于维护总线设备清单，并自动请求对端：

- ISO Address Claim / NAME。
- Product Information。
- Configuration Information。
- 支持的 Tx/Rx PGN 列表。

用法：

```cpp
static tN2kDeviceList DeviceList(&NMEA2000);
NMEA2000.AttachMsgHandler(&DeviceList);

if (DeviceList.ReadResetIsListUpdated()) {
    uint8_t count = DeviceList.Count();
}

const tNMEA2000::tDevice* dev = DeviceList.FindDeviceBySource(22);
if (dev != nullptr) {
    uint64_t name = dev->GetName();
}
```

查找方法：

- `FindDeviceBySource(source)`
- `FindDeviceByName(name)`
- `FindDeviceByIDs(manufacturerCode, uniqueNumber)`
- `FindDeviceByProduct(manufacturerCode, productCode, source)`
- `GetDeviceLastMessageTime(source)`
- `Count()`
- `ReadResetIsListUpdated()`

注意：

- 设备源地址可能变化，长期追踪要用 NAME。
- 新设备接入后，设备列表通常几秒内稳定。
- 产品信息和配置描述请求最多重试有限次数，不保证所有设备都会响应。

## 13. Group Function PGN 126208

NMEA2000 使用 PGN 126208 做请求、命令、读字段、写字段等标准化配置。

库支持的功能码：

- `N2kgfc_Request`
- `N2kgfc_Command`
- `N2kgfc_Acknowledge`
- `N2kgfc_Read`
- `N2kgfc_ReadReply`
- `N2kgfc_Write`
- `N2kgfc_WriteReply`

默认处理器覆盖系统 PGN：

- `tN2kGroupFunctionHandlerForPGN60928`
- `tN2kGroupFunctionHandlerForPGN126464`
- `tN2kGroupFunctionHandlerForPGN126993`
- `tN2kGroupFunctionHandlerForPGN126996`
- `tN2kGroupFunctionHandlerForPGN126998`

如果设备要支持对自定义 PGN 的标准请求或配置，需要继承 `tN2kGroupFunctionHandler`：

```cpp
class MyPGNHandler : public tN2kGroupFunctionHandler {
public:
    MyPGNHandler(tNMEA2000* n2k) : tN2kGroupFunctionHandler(n2k, 130000L) {}

protected:
    bool HandleRequest(
        const tN2kMsg& msg,
        uint32_t transmissionInterval,
        uint16_t transmissionIntervalOffset,
        uint8_t numberOfParameterPairs,
        int deviceIndex
    ) override {
        // 检查请求字段，发送对应 PGN，或返回 acknowledge。
        return true;
    }
};

static MyPGNHandler myHandler(&NMEA2000);
NMEA2000.AddGroupFunctionHandler(&myHandler);
```

认证设备通常不能只依赖默认 unsupported 响应，需要按自身 PGN 实现合规响应。

## 14. 定时发送

`N2kTimer.h` 提供 `tN2kSyncScheduler`，适合周期发送 PGN：

```cpp
static tN2kSyncScheduler PosScheduler;

void onOpen() {
    PosScheduler.SetPeriod(1000);
    PosScheduler.SetOffset(10);
    PosScheduler.UpdateNextTime();
}

void loop() {
    NMEA2000.ParseMessages();

    if (PosScheduler.IsTime()) {
        PosScheduler.UpdateNextTime();
        send_position(...);
    }
}

NMEA2000.SetOnOpen(onOpen);
```

要点：

- 在 `SetOnOpen()` 回调里启用调度器，避免总线未打开时开始计时。
- 不同 PGN 使用不同 offset，避免同一时刻塞满发送队列。
- 对 Fast Packet 消息，offset 间隔应比单帧更大。

普通非同步场景可用 `tN2kScheduler`。

## 15. ESP32 后端注意事项

虽然本手册主体是 `src`，但本仓库实际运行离不开 `tNMEA2000_esp32`：

```cpp
tNMEA2000_esp32 n2k(GPIO_NUM_22, GPIO_NUM_21);
n2k.CAN_SetSpeed(CAN_SPEED_250KBPS);
n2k.Open();
```

默认引脚：

- TX：`GPIO_NUM_22`
- RX：`GPIO_NUM_21`

可通过构造函数或宏 `ESP32_CAN_TX_PIN`、`ESP32_CAN_RX_PIN` 修改。

后端状态：

```cpp
tNMEA2000_esp32::tCANStatus status;
if (n2k.GetCANStatus(status)) {
    // status.TxDoneCount, status.TxFailCount, status.RxFrameCount, ...
}
```

注意：

- 同一进程只允许一个 `tNMEA2000_esp32` 实例占用 TWAI。
- `CAN_SetSpeed()` 必须在 `Open()` 前调用。
- NMEA2000 总线需要外部 CAN 收发器、正确终端电阻和 12 V NMEA2000 网络供电；ESP32 内部 TWAI 不是物理层收发器。
- 标准 NMEA2000 使用扩展 29-bit CAN ID 和 250 kbit/s。

## 16. 常见接入模式

### 16.1 只监听总线

```cpp
NMEA2000.SetMode(tNMEA2000::N2km_ListenOnly);
NMEA2000.SetMsgHandler(onN2kMessage);
NMEA2000.Open();
```

适合诊断工具。不作为标准节点发送数据。

### 16.2 传感器节点

```cpp
NMEA2000.SetProductInformation(...);
NMEA2000.SetDeviceInformation(...);
NMEA2000.ExtendTransmitMessages(TxPGNs);
NMEA2000.SetMode(tNMEA2000::N2km_ListenAndNode, preferredSource);
NMEA2000.Open();
```

周期读取传感器，调用对应 `SetN2k...` 和 `SendMsg()`。

### 16.3 NMEA0183 到 NMEA2000 网关

本仓库当前实现采用该模式：

1. NMEA0183 parser 从 `GNSS_NMEA` 数据源读取 RMC/GGA 等句子。
2. 构造 `tGatewayN2kMessages`。
3. 调用：
   - `SetN2kLatLonRapid()`
   - `SetN2kCOGSOGRapid()`
   - `SetN2kGNSS()`
4. 通过 `tNMEA2000_esp32::SendMsg()` 发送到 CAN 总线。
5. 定时调用 `ParseMessages()` 维持协议栈。

## 17. 编译期开关

`NMEA2000_CompilerDefns.h` 里有若干开关，可在工程编译参数中定义：

- `N2K_NO_ISO_MULTI_PACKET_SUPPORT`：关闭 ISO TP 多包支持。一般不建议，可能影响超长系统/配置消息。
- `N2K_NO_GROUP_FUNCTION_SUPPORT`：关闭 PGN 126208 组功能支持。认证或标准节点不建议关闭。

调试相关宏可在相应文件中查看，例如 `RingBuffer.h` 支持：

- `RING_BUFFER_ERROR_DEBUG`
- `RING_BUFFER_DEBUG`
- `RING_BUFFER_INIT_DEBUG`

ESP32 后端支持测试宏：

- `NMEA2000_ESP32_TWAI_SELF_TEST`
- `NMEA2000_ESP32_TWAI_LOOPBACK`

## 18. 调试建议

基础检查：

- `Open()` 返回值是否为 true。
- `ParseMessages()` 是否被持续调用。
- CAN TX/RX 引脚是否与硬件一致。
- 总线速率是否 250 kbit/s。
- CAN 收发器供电、STB/EN 引脚、终端电阻和 NMEA2000 网络供电是否正确。
- 是否有另一个 `tNMEA2000_esp32` 实例占用 TWAI。

发送失败排查：

- `SendMsg()` 返回 false：检查 `Open()`、地址声明是否完成、发送队列是否满。
- ESP32 后端 `GetCANStatus()` 查看 `TxFailCount`、`LastErrorFlags`、`TxErrorCount`、`BusErrorCount`。
- 总线无 ACK 通常表示没有其他节点、物理层未连接、速率错误或收发器异常。

接收失败排查：

- 检查 `RxFrameCount` 是否增长。
- 如果有 CAN 帧但没有应用回调，检查 `SetHandleOnlyKnownMessages()`、PGN 类型识别、Fast Packet 是否完整。
- 监听类应用使用 `N2km_ListenOnly`，节点类应用使用 `N2km_ListenAndNode`。

数据错误排查：

- 检查单位：角度是否应为弧度，温度是否应为 Kelvin，速度是否应为 m/s。
- 检查 NA 值，不要把未知字段填 0。
- 检查 SID 是否在同一次采样相关 PGN 中一致。
- 检查实例字段，如电池实例、温度实例、设备实例。

## 19. 实践规范

- 使用标准 `SetN2k...` / `ParseN2k...`，只有缺失 PGN 时才手写字段。
- 初始化顺序固定为：产品信息、设备信息、PGN 列表、模式、回调、`Open()`。
- 主循环或任务里始终调用 `ParseMessages()`。
- 周期发送用 `tN2kSyncScheduler`，不要在同一 tick 内发送大量 Fast Packet。
- 对外发布设备时使用真实 Manufacturer Code、Product Code 和稳定 Unique Number。
- 源地址只作为当前会话地址，业务绑定设备应使用 NAME。
- 声明 Tx/Rx PGN 列表，方便其他设备通过 126464 查询能力。
- 对自定义可配置 PGN，实现对应 Group Function handler。
- 对长字符串遵守库中长度限制，产品信息字段最长 32 字符，配置字段建议不超过 70 字符。

## 20. 快速问题索引

### 应该包含哪个头文件？

ESP-IDF ESP32 工程中使用：

```cpp
#include "NMEA2000_esp32.h"
#include "N2kMessages.h"
```

只写协议无关工具或 host 测试时使用：

```cpp
#include "NMEA2000.h"
#include "N2kMsg.h"
#include "N2kMessages.h"
```

### 什么时候使用 `NMEA2000_CAN.h`？

`NMEA2000_CAN.h` 是 Arduino/多平台自动选择后端的包装。本仓库已修正它的全局对象风险：默认只声明 `extern tNMEA2000 &NMEA2000;`，只有在某一个源文件 include 前定义 `NMEA2000_CAN_DEFINE_GLOBAL` 时才创建全局对象。

```cpp
#define NMEA2000_CAN_DEFINE_GLOBAL
#include <NMEA2000_CAN.h>
```

在本 ESP-IDF 工程里仍推荐直接使用 `NMEA2000_esp32.h` 并显式创建 `tNMEA2000_esp32`，这样更清晰，也更适合控制引脚、状态和生命周期。

### 发送 GNSS 至少要哪些 PGN？

常见组合：

- 129025：经纬度快速更新。
- 129026：COG/SOG 快速更新。
- 129029：GNSS 完整位置数据。

本仓库就是这个组合。

### 经纬度单位是什么？

`SetN2kLatLonRapid()` 和 `SetN2kGNSS()` 的 Latitude/Longitude 使用度，不是弧度。

### COG/Heading 单位是什么？

多数角度字段使用弧度。使用 `DegToRad()` 和 `RadToDeg()` 转换。

### 温度为什么显示不对？

多数温度字段使用 Kelvin。摄氏度发送前使用 `CToKelvin(c)`。

### 如何知道地址是否改变？

调用：

```cpp
if (NMEA2000.ReadResetAddressChanged()) {
    uint8_t source = NMEA2000.GetN2kSource();
}
```

### 如何知道设备信息是否被远端修改？

调用：

```cpp
if (NMEA2000.ReadResetDeviceInformationChanged()) {
    // 保存新的实例/配置
}
```

配置描述变化可用 `ReadResetInstallationDescriptionChanged()`。

## 21. 相关源码入口

阅读源码时建议按以下顺序：

1. `NMEA2000.h`：先看 public API 和模式、设备信息、发送接收入口。
2. `NMEA2000.cpp`：看 `Open()`、`ParseMessages()`、`SendMsg()`、地址声明、Fast Packet/TP 实现。
3. `N2kMsg.h/.cpp`：理解字段编解码。
4. `N2kMessages.h/.cpp`：查具体 PGN 的参数、单位、默认优先级。
5. `N2kTypes.h` 和 `NMEA2000StdTypes.h`：查枚举取值。
6. `N2kGroupFunctionDefaultHandlers.*`：查标准系统 PGN 的组功能响应。
7. `N2kDeviceList.*`：查如何维护总线设备清单。
8. `components/NMEA2000_ESP32/NMEA2000_esp32.*`：查 ESP32 TWAI 后端。
