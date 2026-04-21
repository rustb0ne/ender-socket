# BÁO CÁO BÀI TẬP LỚN
## Môn: Xây Dựng Hệ Thống Nhúng

**Tên dự án:** ENDER SOCKET — Ổ Cắm Thông Minh IoT  
**Nền tảng phần cứng:** ESP32 Dev Module (Dual-Core Xtensa LX6 240MHz)  
**Nền tảng phần mềm:** PlatformIO (Arduino framework) + FreeRTOS + Node.js Web Dashboard  

---

## Mục Lục

1. [Xác Định Bài Toán & Yêu Cầu Hệ Thống](#1-xác-định-bài-toán--yêu-cầu-hệ-thống)
2. [Thiết Kế Hệ Thống](#2-thiết-kế-hệ-thống)
3. [Triển Khai & Lập Trình](#3-triển-khai--lập-trình)
4. [Đánh Giá & Kiểm Thử](#4-đánh-giá--kiểm-thử)
5. [Tính Sáng Tạo & Mở Rộng](#5-tính-sáng-tạo--mở-rộng)
6. [Kết Luận](#6-kết-luận)
7. [Tài Liệu Tham Khảo](#7-tài-liệu-tham-khảo)

---

## 1. Xác Định Bài Toán & Yêu Cầu Hệ Thống

### 1.1. Mô Tả Bài Toán Thực Tế

#### 1.1.1. Bối Cảnh Ứng Dụng

Người dùng sạc pin lithium dung lượng lớn (xe đạp điện, pin dự phòng) qua đêm bằng cách cắm bộ sạc vào ổ cắm thông thường. Điều này tiềm ẩn hai rủi ro:

1. **Sạc quá mức (overcharge):** Bộ sạc không tự ngắt hoặc pin đã đầy nhưng thiết bị vẫn kết nối, sinh nhiệt và giảm tuổi thọ pin.
2. **Tỏa nhiệt không được giám sát:** Tiếp xúc điện kém hoặc bộ sạc lỗi làm ổ cắm nóng lên, không có thiết bị nào phát hiện trong khi người dùng đang ngủ.

**Use-case mục tiêu:** Ender Socket thay thế ổ cắm thông thường, liên tục đo dòng sạc và nhiệt độ phích cắm/SSR, tự động cắt điện khi phát hiện pin đã đầy, và gửi cảnh báo về dashboard khi nhiệt độ vượt ngưỡng nguy hiểm.

#### 1.1.2. Vấn Đề Cần Giải Quyết

| Vấn Đề | Giải Pháp Trong Dự Án |
|--------|----------------------|
| Sạc quá mức | Smart Charging: 4 cơ chế tự ngắt (Full / Unplug / Spike / Timeout) |
| Nhiệt độ tăng cao | SafetyMonitor với 2× DS18B20, cắt relay khi > 60°C, khóa cứng khi > 75°C |
| Không biết trạng thái sạc | MQTT telemetry 1Hz + Web Dashboard real-time |
| Không thể điều khiển từ xa | Lệnh MQTT qua HiveMQ Cloud + ACK xác nhận |
| Cấu hình mạng phức tạp | WiFi Captive Portal qua nút BTN2 giữ 3 giây |
| Safety bị ảnh hưởng bởi network delay | **FreeRTOS Dual-Core**: Safety/Charging trên Core 1 riêng biệt |

### 1.2. Yêu Cầu Chức Năng (Functional Requirements)

Các yêu cầu chức năng sau đây đều được triển khai và có thể truy xuất trực tiếp trong code:

| ID | Yêu Cầu | Triển Khai Trong Code |
|----|---------|----------------------|
| FR-01 | Đo điện áp, dòng, công suất, điện năng (Wh) | `pzem.voltage()`, `pzem.current()`, `pzem.power()`, `pzem.energy()` — `taskChargeSafety` Core 1 |
| FR-02 | Giám sát nhiệt độ 2 DS18B20 (phích cắm + SSR) | `_ds.getTempCByIndex(0/1)` — `SafetyMonitor.cpp`, Core 1 |
| FR-03 | Điều khiển relay SSR tại chỗ qua BTN1 | BTN1 handler → `gCommandQueue` → `taskChargeSafety` thực thi |
| FR-04 | Điều khiển relay SSR từ xa qua MQTT | `mqttCallback()` → `xQueueSend(gCommandQueue, cmd)` → Core 1 |
| FR-05 | Phát hiện pin đầy: dòng < 0.10A liên tục 5 phút | `FULL_CURRENT_A=0.10f`, `FULL_HOLD_MS=5min` — `ChargeManager.cpp` |
| FR-06 | Phát hiện rút phích: dòng giảm đột ngột | `UNPLUG_FROM_A=0.05f`, `UNPLUG_CURRENT_A=0.01f`, 2 mẫu liên tiếp — `ChargeManager.cpp` |
| FR-07 | Phát hiện dòng đột biến (spike detection) | `SPIKE_FACTOR=1.30f`, Rolling Average 300 mẫu — `ChargeManager.cpp` |
| FR-08 | Timeout cứng 8 giờ | `CHARGE_TIMEOUT_MS = 8 * 60 * 60 * 1000` — `config.h` |
| FR-09 | Cảnh báo quá nhiệt > 60°C: ngắt relay | `TEMP_ALERT_C=60.0f`, `s.overheat → forceRelayOff()` — `taskChargeSafety` Core 1 |
| FR-10 | Fire Lock > 75°C: khóa relay vĩnh viễn | `TEMP_FIRE_C=75.0f`, `localFireLocked=true` không bao giờ reset — Core 1 |
| FR-11 | Hiển thị thông số trên LCD 320×240 | `DisplayUI::update(snap)` mỗi 500ms — `taskNetComms` Core 0 |
| FR-12 | Bật/tắt màn hình qua BTN2 | `gUi.toggleDisplay()` điều khiển `GPIO32 (TFT_LED_PIN)` — Core 0 |
| FR-13 | Gửi MQTT telemetry (JSON) mỗi 1 giây | `publishTelemetry(displaySnap)`, `TELEMETRY_PUBLISH_MS=1000` — Core 0 |
| FR-14 | Nhận lệnh điều khiển từ Dashboard qua MQTT | Subscribe `socket/control`, `mqttCallback()` → `gCommandQueue` |
| FR-15 | Publish ACK sau mỗi lệnh (150ms) | `gPendingCmds[2]`, `CMD_VERIFY_MS=150` — `taskNetComms` Core 0 |
| FR-16 | Web Dashboard real-time | `server.js`: Socket.IO broadcast `state_update` — `server.js` |
| FR-17 | Cấu hình WiFi qua Captive Portal | BTN2 giữ ≥ 3000ms → AP "SmartSocket-Setup" — Core 0 |
| FR-18 | Lưu thông tin WiFi vào SPIFFS | `WiFiStorage::saveWiFiCredentials()` → `/wifi_creds.json` — `WiFiStorage.h` |
| FR-19 | BTN1 nhấn đơn: Reset Wh về 0 | `CmdType::RESET_ENERGY` → `gCommandQueue` → Core 1 `pzem.resetEnergy()` |
| FR-20 | BTN1 giữ 2s: Toggle Smart Mode | `CmdType::SMART_MODE` → `gCommandQueue` → Core 1 `charger.setSmartMode()` |

### 1.3. Yêu Cầu Phi Chức Năng (Non-functional Requirements)

#### 1.3.1. Yêu Cầu Thời Gian Thực (Real-time Constraints)

Với kiến trúc FreeRTOS Dual-Core, các tác vụ được phân tầng theo mức độ ưu tiên thời gian thực:

**Core 1 — `taskChargeSafety` (Priority 3 — HIGH):**

| Tác Vụ | Chu Kỳ / Ngưỡng | Cơ Chế |
|--------|----------------|--------|
| Task cycle | 50 ms | `vTaskDelayUntil()` — `CHARGE_TASK_PERIOD_MS` |
| Đọc PZEM-004T | 1.000 ms | millis() gate trong task |
| Chờ DS18B20 chuyển đổi | 800 ms | millis() timer |
| Kiểm tra Safety thresholds | Mỗi task cycle (50ms) | Trong task loop |
| Cập nhật gSharedState | Mỗi task cycle (50ms) | `xSemaphoreTake(gStateMutex, 10ms)` |

**Core 0 — `taskNetComms` (Priority 1 — LOW):**

| Tác Vụ | Chu Kỳ / Ngưỡng | Cơ Chế |
|--------|----------------|--------|
| Task yield | 10 ms | `vTaskDelay()` |
| Cập nhật màn hình LCD | 500 ms | millis() gate — `DISPLAY_REFRESH_MS` |
| Gửi MQTT telemetry | 1.000 ms | millis() gate — `TELEMETRY_PUBLISH_MS` |
| Debounce nút nhấn | 50 ms | `BTN_DEBOUNCE_MS` |
| Verify ACK sau lệnh | 150 ms | `CMD_VERIFY_MS` |
| Retry WiFi | 5.000 ms | `WIFI_RETRY_MS` |
| Retry MQTT | 3.000 ms | `MQTT_RETRY_MS` |

**Lợi ích thời gian thực so với single-loop:** `taskChargeSafety` dùng `vTaskDelayUntil()` để đảm bảo chu kỳ cố định 50ms, không bị drift dù `taskNetComms` đang xử lý WiFi reconnect hay MQTT. Trước đây với single `loop()`, độ trễ WiFiManager hay MQTT blocking có thể gây delay đọc PZEM và kiểm tra safety.

**Phát hiện device offline (frontend):** `app.js` kiểm tra freshness của telemetry:
```javascript
const DEVICE_OFFLINE_TIMEOUT_MS = 5000;  // app.js:8
function isDeviceOnline() {
    return (Date.now() - lastDeviceTelemetryTs) < DEVICE_OFFLINE_TIMEOUT_MS;
}
```
Heartbeat check chạy mỗi 1 giây (`setInterval(..., 1000)` — `app.js:195`).

#### 1.3.2. Yêu Cầu Tài Nguyên (Resource Constraints)

Từ `include/ChargeManager.h`, rolling average buffer chiếm:
- `float _window[ROLLING_WINDOW_SEC]` = `float _window[300]` = **1.200 bytes** (300 × 4 bytes)

**FreeRTOS Stack allocation (từ `config.h`):**
- `taskChargeSafety`: `RTOS_CHARGE_STACK = 8192` bytes
- `taskNetComms`: `RTOS_NETCOMMS_STACK = 10240` bytes (lớn hơn do TLS stack của MQTT)

**Build metrics thực tế (`pio run -e esp32dev`):**
- Flash: **94.9%** (1,243,532 / 1,310,720 bytes) — chủ yếu do TFT_eSPI + TLS
- RAM: **15.4%** (50,576 / 327,680 bytes) — còn `~277KB` headroom cho FreeRTOS stacks và runtime

**Queue sizes:**
- `gCommandQueue`: 6 items × `sizeof(RelayCommand)` = 12 bytes
- `gAlertQueue`: 12 items × `sizeof(AlertMessage)` = 12 × 96 = 1,152 bytes

Từ `include/config.h`:
- SPIFFS dùng tối đa 1 file: `/wifi_creds.json` chứa SSID (max 32 ký tự) + password (max 64 ký tự)
- MQTT Broker: HiveMQ Cloud, `MQTT_PORT 8883` (TLS), `MQTT_CLIENTID "iot-smart-socket-esp32"`

#### 1.3.3. Yêu Cầu Độ Tin Cậy & An Toàn

Các bất biến cứng của hệ thống (không thể vi phạm bằng phần mềm):

1. **Fire Lock không thể reset:** `localFireLocked` trong `taskChargeSafety` chỉ được set `true`, không có code path nào reset về `false`. Relay chỉ có thể mở lại sau khi mất nguồn thiết bị (hard reset).

2. **GPIO relay chỉ thay đổi qua một điểm duy nhất:** Hàm private `ChargeManager::_setRelay(bool on)` là duy nhất gọi `digitalWrite(RELAY_PIN, ...)`. Chỉ `taskChargeSafety` (Core 1) sở hữu `ChargeManager` — không có code trên Core 0 nào ghi vào `GPIO 27`.

3. **Safety monitoring không phụ thuộc mạng:** `taskChargeSafety` chạy độc lập hoàn toàn với `taskNetComms`. Khi WiFi/MQTT mất kết nối (Core 0 bận reconnect), Core 1 vẫn đọc PZEM, đọc DS18B20, kiểm tra ngưỡng nhiệt, và cắt relay nếu cần.

4. **Core isolation:** `taskChargeSafety` được pinned vào **Core 1** với priority cao nhất (3). WiFi/MQTT stack của ESP-IDF chạy trên Core 0 — tách biệt hoàn toàn về scheduler. FreeRTOS đảm bảo Core 1 không bao giờ bị preempt bởi WiFi interrupt handler.

---

## 2. Thiết Kế Hệ Thống

### 2.1. Kiến Trúc Tổng Thể

#### 2.1.1. Block Diagram Hệ Thống

```
╔══════════════════════════════════════════════════════════════════╗
║                    HARDWARE LAYER                               ║
║                                                                  ║
║  ┌─────────────┐  UART/Serial2  ┌────────────────────────────┐  ║
║  │ PZEM-004T   │ ─────────────► │                            │  ║
║  │ v3.0        │  GPIO26/25     │      ESP32 Dev Module      │  ║
║  └─────────────┘                │  Arduino + FreeRTOS        │  ║
║                                 │  PlatformIO env:esp32dev   │  ║
║  ┌─────────────┐  1-Wire        │                            │  ║
║  │ DS18B20 ×2  │ ─────────────► │  ┌─────────────────────┐  │  ║
║  │ (index 0,1) │  GPIO13        │  │  Core 1 (PRO)       │  │  ║
║  └─────────────┘                │  │  taskChargeSafety   │  │  ║
║                                 │  │  Priority: 3 (HIGH) │  │  ║
║  ┌─────────────┐  SPI 40MHz     │  │  - ChargeManager    │  │  ║
║  │ ILI9341 TFT │ ◄────────────  │  │  - SafetyMonitor    │  │  ║
║  │ 320×240     │  GPIO17-23     │  │  - PZEM read @1Hz   │  │  ║
║  └─────────────┘                │  │  - Relay control    │  │  ║
║                                 │  └──────────┬──────────┘  │  ║
║  [TTP223 BTN1] ──── GPIO4  ───► │             │ FreeRTOS    │  ║
║  [TTP223 BTN2] ──── GPIO33 ───► │      gSharedState (mutex) │  ║
║  [SSR Fotek]   ◄─── GPIO27 ───  │      gCommandQueue        │  ║
║                                 │      gAlertQueue          │  ║
║    │             │                                          │  ║
║  [Buzzer]      ◄─── GPIO15 ───  │  ┌──────────▼──────────┐  │  ║
║                                 │  │  Core 0 (APP)       │  │  ║
║                                 │  │  taskNetComms       │  │  ║
║                                 │  │  Priority: 1 (LOW)  │  │  ║
║                                 │  │  - WiFi / MQTT      │  │  ║
║                                 │  │  - DisplayUI (TFT)  │  │  ║
║                                 │  │  - Button handling  │  │  ║
║                                 │  └─────────────────────┘  │  ║
║                                 └────────────────────────────┘  ║
╚══════════════════════════════════════════════════╪═══════════════╝
                                                   │ WiFi 802.11
                                                   ▼
╔══════════════════════════════════════════════════════════════════╗
║          CLOUD LAYER — HiveMQ Cloud Broker                      ║
║                                                                 ║
║  TCP Port: 8883 (TLS)                                           ║
║                                                                  ║
║  Topics (từ config.h và server.js):                             ║
║  socket/telemetry   — ESP32 publish JSON 1Hz (QoS 0)           ║
║  socket/control     — Dashboard publish lệnh điều khiển         ║
║  socket/control/ack — ESP32 publish ACK sau 150ms               ║
║  socket/alert       — ESP32 publish cảnh báo (retain=true)     ║
╚══════════════════════════════════════════════════╪═══════════════╝
                                                   │ MQTT over TLS
                                                   ▼
╔══════════════════════════════════════════════════════════════════╗
║          APPLICATION LAYER — Node.js Server                     ║
║  server.js: Express + MQTT Client + Socket.IO                   ║
║  PORT: process.env.PORT || 3000                                 ║
║  MQTT reconnect period: 5000ms (mqttClient options)            ║
║  Max command history: 50 entries (MAX_HISTORY = 50)            ║
╚══════════════════════════════════════════════════╪═══════════════╝
                                                   │ WebSocket
                                                   ▼
╔══════════════════════════════════════════════════════════════════╗
║          PRESENTATION LAYER — Web Browser                       ║
║  index.html + public/js/app.js                                  ║
║  Socket.IO client, DEVICE_OFFLINE_TIMEOUT_MS = 5000            ║
║  Request state mỗi 30 giây (setInterval 30000ms)               ║
║  Heartbeat check mỗi 1 giây (setInterval 1000ms)               ║
╚══════════════════════════════════════════════════════════════════╝
```

#### 2.1.2. Phân Tầng Phần Mềm Nhúng — FreeRTOS Dual-Core

```
┌─────────────────────────────────────────────────────────────────┐
│   ORCHESTRATION — setup() + FreeRTOS Scheduler                 │
│                                                                 │
│   setup():                                                      │
│   ├─ initGPIO (tất cả pins trước khi tasks start)             │
│   ├─ WiFiStorage::initSPIFS()                                   │
│   ├─ gStateMutex   = xSemaphoreCreateMutex()                   │
│   ├─ gCommandQueue = xQueueCreate(6,  sizeof(RelayCommand))    │
│   ├─ gAlertQueue   = xQueueCreate(12, sizeof(AlertMessage))    │
│   ├─ xTaskCreatePinnedToCore(taskChargeSafety, Core 1, prio=3) │
│   └─ xTaskCreatePinnedToCore(taskNetComms,     Core 0, prio=1) │
│      loop() → vTaskDelete(nullptr)  [tự xóa, không dùng]      │
├──────────────────────────────┬──────────────────────────────────┤
│  CORE 1: taskChargeSafety   │  CORE 0: taskNetComms            │
│  (Priority 3 — HIGH)        │  (Priority 1 — LOW)              │
│  vTaskDelayUntil() 50ms     │  vTaskDelay() 10ms               │
│                              │                                  │
│  ├─ xQueueReceive()          │  ├─ BTN1 / BTN2 polling + FSM   │
│  │   CmdType::RELAY          │  ├─ connectWifiIfNeeded()        │
│  │   CmdType::SMART_MODE     │  ├─ connectMqttIfNeeded()        │
│  │   CmdType::RESET_ENERGY   │  ├─ gMqtt.loop()                │
│  │                           │  │   └─ mqttCallback()           │
│  ├─ readPzem() @1Hz          │  │       └─ xQueueSend(cmd)      │
│  ├─ safety.update()          │  ├─ xQueueReceive(alertQueue)    │
│  ├─ handleSafetyLogic()      │  ├─ netPublishAlert() MQTT       │
│  │   ├─ overheat relay OFF   │  ├─ processPendingAck()          │
│  │   └─ fireLocked = true    │  ├─ SharedState snapshot         │
│  ├─ charger.update()         │  ├─ gUi.update(snap)            │
│  ├─ detect spike alert       │  └─ publishTelemetry(snap)       │
│  └─ xSemaphoreTake(mutex)    │     xSemaphoreTake(mutex)        │
│      write gSharedState      │     read gSharedState            │
│      xSemaphoreGive(mutex)   │     xSemaphoreGive(mutex)        │
├──────────────────────────────┴──────────────────────────────────┤
│   DOMAIN MODULES — Không thay đổi logic cốt lõi               │
│                                                                 │
│  ChargeManager (Core 1)  │  SafetyMonitor (Core 1)             │
│  ├─ commandRelay()        │  ├─ begin() — DS18B20 init          │
│  ├─ forceRelayOff()       │  ├─ update() — async 2-phase       │
│  ├─ setSmartMode()        │  ├─ status() — SafetyStatus         │
│  ├─ update()              │  ├─ setPeriphFault()                │
│  ├─ _pushCurrent()        │  ├─ _updateLedsAndBuzzer()          │
│  ├─ _rollingAvg()         │  └─ _isValidTemp()                  │
│  ├─ _setRelay() GPIO27    │                                     │
│  └─ _resetSession()       │  DisplayUI (Core 0 ONLY)           │
│                            │  ├─ begin() — TFT init (SPI)       │
│  SharedState (inter-core)  │  ├─ update(SharedState snap)       │
│  ├─ PowerReading           │  ├─ toggleDisplay() GPIO32         │
│  ├─ SafetyStatus           │  ├─ _drawStatic() — 1 lần         │
│  ├─ relayOn, smartMode     │  ├─ _drawField() — partial update  │
│  └─ fireLocked, lastAlert  │  └─ _stopReasonStr()               │
├─────────────────────────────────────────────────────────────────┤
│   HARDWARE ABSTRACTION — Libraries (từ platformio.ini)          │
│                                                                 │
│  bodmer/TFT_eSPI @ ^2.5.43         — ILI9341 SPI driver        │
│  mandulaj/PZEM-004T-v30 @ ^1.1.2   — Power meter UART driver   │
│  paulstoffregen/OneWire @ ^2.3.8   — 1-Wire bus driver         │
│  milesburton/DallasTemperature @ ^4.0.5 — DS18B20 driver       │
│  knolleary/PubSubClient @ ^2.8     — MQTT client               │
│  tzapu/WiFiManager @ ^2.0.17       — Captive Portal            │
│  bblanchon/ArduinoJson @ ^7.0.0    — JSON serialization        │
├─────────────────────────────────────────────────────────────────┤
│   HARDWARE — GPIO, UART/Serial2, SPI 40MHz, 1-Wire, WiFi       │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2. Thiết Kế Phần Cứng

#### 2.2.1. Lý Do Chọn ESP32

Dự án yêu cầu đồng thời:
- **Dual-Core Xtensa LX6:** Cho phép tách biệt hoàn toàn safety-critical task (Core 1) khỏi networking stack (Core 0) — không thể thực hiện trên STM32 đơn nhân mà không có RTOS phức tạp hơn.
- WiFi built-in để kết nối MQTT
- RAM đủ cho TLS stack (~50KB) + ứng dụng + FreeRTOS stacks
- 2 UART hardware: `Serial` (USB debug, 115200bps) + `Serial2` (PZEM-004T, remap sang GPIO26/25 qua build flag `USER_SERIAL2_RX=26`, `USER_SERIAL2_TX=25`)
- SPI tốc độ 40MHz cho màn hình TFT (`SPI_FREQUENCY=40000000` trong `platformio.ini`)
- SPIFFS built-in để lưu WiFi credentials
- **FreeRTOS tích hợp sẵn trong ESP-IDF:** Không cần cài thêm, hỗ trợ `xTaskCreatePinnedToCore()`, mutex, queue, semaphore đầy đủ.

Arduino UNO (2KB RAM, không có WiFi, single-core) không đáp ứng được các yêu cầu này cùng lúc.

#### 2.2.2. Phân Công GPIO (Trực Tiếp Từ `config.h`)

| GPIO | Hằng Số | Linh Kiện | Giao Thức | Vai Trò | Core Sở Hữu |
|------|---------|-----------|-----------|---------| ------------|
| 18 | `SPI_SCK_PIN` | SPI Bus | SPI CLK | Clock chung TFT + XPT2046 | Core 0 |
| 19 | `SPI_MISO_PIN` | SPI Bus | SPI MISO | Data vào ESP32 | Core 0 |
| 23 | `SPI_MOSI_PIN` | SPI Bus | SPI MOSI | Data ra từ ESP32 | Core 0 |
| 17 | `TFT_CS_PIN` | ILI9341 | SPI CS | Chip Select màn hình | Core 0 |
| 16 | `TFT_DC_PIN` | ILI9341 | Digital | Data/Command select | Core 0 |
| 5 | `TFT_RST_PIN` | ILI9341 | Digital | Hardware reset | Core 0 |
| 32 | `TFT_LED_PIN` | Backlight | Digital OUT | Bật/tắt đèn nền LCD | Core 0 |
| 21 | `TOUCH_CS_PIN` | XPT2046 | SPI CS | Chip Select cảm ứng | Core 0 |
| 12 | `SD_CS_PIN` | SD Card | SPI CS | Sẵn sàng, chưa sử dụng | — |
| 26 | `PZEM_RX_PIN` | PZEM-004T | UART2 RX | Nhận từ power meter | Core 1 |
| 25 | `PZEM_TX_PIN` | PZEM-004T | UART2 TX | Gửi đến power meter | Core 1 |
| 13 | `ONEWIRE_PIN` | DS18B20 ×2 | 1-Wire | Bus cảm biến nhiệt kép | Core 1 |
| 4 | `TOUCH_BTN_PIN` | TTP223 BTN1 | Digital IN | Nút cảm ứng chính | Core 0 |
| 33 | `TOUCH_BTN2_PIN` | TTP223 BTN2 | Digital IN | Nút cảm ứng phụ | Core 0 |
| 27 | `RELAY_PIN` | SSR Fotek | Digital OUT | Đóng/mở tải AC (active HIGH) | Core 1 (exclusive) |
| 14 | `LED_GREEN_PIN` | LED Xanh | Digital OUT | Trạng thái WiFi | Core 0 |
| 2 | `LED_RED_PIN` | LED Đỏ | Digital OUT | Lỗi / Overheat / Fire | Core 1 |
| 15 | `BUZZER_PIN` | Buzzer | Digital OUT | Cảnh báo Fire Risk | Core 1 |

**Nguyên tắc phân công core:** SPI bus (TFT_eSPI không thread-safe) và các GPIO liên quan đến networking UI chạy trên Core 0. UART/Serial2 (PZEM), OneWire (DS18B20), và RELAY_PIN chạy độc quyền trên Core 1 — không có code nào trên Core 0 gọi vào các peripheral này.

#### 2.2.3. Lý Do Chọn Linh Kiện Chính

**PZEM-004T v3.0:** Module đo điện AC chuyên dụng, giao tiếp Modbus-RTU qua UART 9600bps. Thư viện `mandulaj/PZEM-004T-v30` cung cấp API đơn giản (`pzem.voltage()`, `pzem.current()`, `pzem.power()`, `pzem.energy()`). Cách ly điện an toàn, không cần thiết kế mạch đo dòng AC tự làm nguy hiểm. Với FreeRTOS, object `PZEM004Tv30 pzem` được khởi tạo cục bộ trong `taskChargeSafety` — đảm bảo chỉ một thread duy nhất truy cập Serial2.

**DS18B20:** Cảm biến 1-Wire, 2 sensor trên cùng bus GPIO 13, phân biệt qua index (`getTempCByIndex(0)` = socket, `getTempCByIndex(1)` = SSR). Tiết kiệm GPIO. `setWaitForConversion(false)` cho phép đọc non-blocking (quan trọng cho thiết kế phần mềm real-time trong task).

**SSR Fotek 25DA:** Relay trạng thái rắn, điều khiển bằng 3–32VDC → tương thích trực tiếp với GPIO 3.3V của ESP32. `RELAY_ON` = `HIGH`, `RELAY_OFF` = `LOW` (định nghĩa trong `config.h`).

**TTP223:** Cảm biến cảm ứng điện dung, output `HIGH` khi chạm, đọc qua `digitalRead()` đơn giản trong Core 0.

### 2.3. Thiết Kế Phần Mềm

#### 2.3.1. Mô Hình Lập Lịch: FreeRTOS Preemptive Dual-Core

Thay vì `loop()` cooperative scheduling, firmware dùng **FreeRTOS preemptive multi-tasking** với hai task được pin cứng vào hai core:

```
Core 1 (PRO Core)                    Core 0 (APP Core)
────────────────────────             ────────────────────────
taskChargeSafety [prio=3]            taskNetComms [prio=1]
│                                    │
│  vTaskDelayUntil(&wake, 50ms)      │  vTaskDelay(10ms)
│  ← đảm bảo chu kỳ CỨNG 50ms       │  ← yield, non-blocking
│                                    │
│  [0ms] drain gCommandQueue         │  [0ms] BTN1/BTN2 FSM
│  [1ms] pzem.readAll() nếu @1Hz     │  [2ms] WiFi reconnect
│  [2ms] safety.update()             │  [3ms] MQTT.loop()
│  [3ms] handleSafety (fire/overheat)│  [4ms] drain gAlertQueue
│  [4ms] charger.update()            │  [5ms] ACK verify
│  [5ms] write gSharedState (mutex)  │  [6ms] read gSharedState (mutex)
│                                    │  [7ms] gUi.update(snap)
│  [vTaskDelayUntil: ngủ còn lại]    │  [8ms] publishTelemetry
│                                    │  [vTaskDelay: ngủ còn lại]
```



#### 2.3.3. Máy Trạng Thái Relay / Sạc Thông Minh (ChargeManager FSM)

 chạy trên **Core 1** với chu kỳ 50ms đảm bảo:

```
              commandRelay(ON) qua gCommandQueue
   IDLE ────────────────────────────────────────► CHARGING
  (relay=OFF)                                      (relay=ON)
     ▲                                                  │
     │                                                  │ Kiểm tra mỗi 50ms (vTaskDelayUntil)
     │                                                  ▼
     │                          ┌───────────────────────────────────────┐
     │                          │     ChargeManager::update()           │
     │                          │     (trong taskChargeSafety Core 1)   │
     │                          │                                       │
     │  TIMEOUT                 │  1. now - _chargeStartMs >= 8h?       │
     │  (8 giờ)                 │     → forceRelayOff(TIMEOUT)          │
     │                          │                                       │
     │  FULL                    │  2. SmartMode ON:                     │
     │  (< 0.10A trong 5 phút)  │     a. _lastCurrent > 0.05A          │
     │                          │        && current <= 0.01A (2 lần)?   │
     │  UNPLUG                  │        → forceRelayOff(UNPLUG)        │
     │  (drop đột ngột)         │     b. current < 0.10A >= 5 phút?    │
     │                          │        → forceRelayOff(FULL)          │
     │  SPIKE                   │     c. winCount >= 300               │
     │  (> 1.30× avg 5 phút)    │        && current > avg * 1.30?      │
     │                          │        → forceRelayOff(SPIKE)         │
     │  MANUAL / SAFETY         └───────────────────────────────────────┘
     └──────────────────────────────────────────────────────────────────
```

Sau `forceRelayOff()`: Core 1 ghi `gSharedState.stopReason` và `gSharedState.relayOn=false` — Core 0 sẽ thấy thay đổi trong lần lấy snapshot tiếp theo (tối đa 10ms sau) rồi publish telemetry và cập nhật LCD.

#### 2.3.4. Máy Trạng Thái Nút BTN1 và BTN2

Chạy trên Core 0 trong `taskNetComms`. Thay vì gọi trực tiếp `charger.commandRelay()` hay `charger.setSmartMode()` (unsafe vì charger thuộc Core 1), handler gửi `RelayCommand` vào `gCommandQueue`:

```
   BTN1 (TOUCH_BTN_PIN = GPIO 4) — core 0 handler:
   ├─ heldMs ≤ 600ms:  {CmdType::RESET_ENERGY} → xQueueSend(gCommandQueue)
   │                   → Core 1: pzem.resetEnergy() + pushAlert()
   ├─ heldMs ≥ 2000ms: đọc snap.smartMode từ gSharedState
   │                   → {CmdType::SMART_MODE, !curSmart} → xQueueSend()
   │                   → Core 1: charger.setSmartMode()
   └─ dead zone:        bỏ qua

   BTN2 (TOUCH_BTN2_PIN = GPIO 33) — core 0 handler:
   ├─ heldMs ≤ 800ms:  gUi.toggleDisplay() — GPIO32 (TFT BL) — local Core 0
   ├─ heldMs ≥ 3000ms: startConfigPortal → gConfigMode=true — local Core 0
   └─ dead zone:        bỏ qua
```

#### 2.3.5. Cơ Chế DS18B20 Hai Pha (SafetyMonitor — Core 1)



```
  Pha 1 — Gửi lệnh đo (khi đủ SENSOR_READ_MS = 1000ms):
      _ds.requestTemperatures()  ← trả về NGAY, không block
      _tempRequestedMs = now
      _awaitingTemp = true

  [task tiếp tục 50ms cycle: đọc PZEM, xử lý queue, update charger...]

  Pha 2 — Đọc kết quả (sau khi _awaitingTemp && 800ms đã qua):
      t0 = _ds.getTempCByIndex(0)   ← Sensor index 0 (socket)
      t1 = _ds.getTempCByIndex(1)   ← Sensor index 1 (SSR)
      → kiểm tra _isValidTemp()
      → tính maxT từ các sensor hợp lệ
      → _status.overheat = (maxT > 60.0f) và không có sensorFault
      → _status.fireRisk  = (maxT > 75.0f) và không có sensorFault
      _awaitingTemp = false
```

#### 2.3.6. Luồng Dữ Liệu MQTT

**Telemetry payload (publishTelemetry — Core 0, mỗi 1 giây từ `SharedState` snapshot):**
```json
{
  "voltage": 220.54,
  "current": 1.423,
  "power": 313.58,
  "energy_wh": 1024.3,
  "temp1": 45.1,
  "temp2": 38.6,
  "relay_state": "ON",
  "charge_mode": "ON"
}
```

**Control payload (Dashboard → ESP32, qua `TOPIC_CONTROL`):**
```json
{ "relay": "ON" }
{ "relay": "OFF" }
{ "charge_mode": "ON" }
{ "charge_mode": "OFF" }
```
`mqttCallback()` parse bằng `indexOf()` rồi `xQueueSend(gCommandQueue, cmd, 0)` — gửi vào queue Core 1, không thực thi relay trực tiếp.

**ACK payload (Core 0 publish sau 150ms, `TOPIC_ACK`):**
```json
{
  "command": "relay",
  "requested_value": "ON",
  "actual_value": "ON",
  "timestamp": 1234567890,
  "status": "confirmed"
}
```
ACK verify bằng cách đọc `gSharedState.relayOn` (đã được Core 1 cập nhật) qua mutex.

**Alert payload (Core 1 push vào `gAlertQueue`, Core 0 publish với `retain=true`):**
```json
{ "alert": "FIRE RISK >75C: relay locked until hard reset" }
```

#### 2.3.7. Kiến Trúc Web Dashboard (server.js + app.js)

**Socket.IO Events (từ `server.js` và `app.js`):**

| Event | Hướng | Trigger | Xử Lý |
|-------|-------|---------|-------|
| `state_update` | Server → Browser | MQTT telemetry or alert | `updateMetrics()`, `updateStatus()`, `updateIndicators()` |
| `command_sent` | Server → Browser | Sau khi publish MQTT | `showAlert()`, `addLog()` |
| `command_acked` | Server → Browser | MQTT ACK nhận | `showAlert()`, `addLog()` |
| `alert_received` | Server → Browser | MQTT alert nhận | `showAlert()`, `addLog()` |
| `control_relay` | Browser → Server | User click | `mqttClient.publish(TOPICS.CONTROL, {"relay":"ON/OFF"})` |
| `toggle_smart_mode` | Browser → Server | User click | `mqttClient.publish(TOPICS.CONTROL, {"charge_mode":"ON/OFF"})` |
| `request_state` | Browser → Server | On load, mỗi 30s | `socket.emit('state_update', deviceState)` |
| `command_history` | Server → Browser | On new connection | Gửi `commandHistory[]` |

**REST API (từ `server.js`):**

| Method | Endpoint | Mô Tả |
|--------|----------|-------|
| GET | `/api/status` | Trả `deviceState` JSON hiện tại |
| POST | `/api/control` | Nhận `{command, value}`, điều phối qua `io.emit` |
| GET | `/api/history` | Lịch sử lệnh, query param `?limit=N`, mặc định 50 |
| GET | `/api/command/:id` | Tra cứu lệnh theo ID |
| GET | `/api/command/status/:id` | Trạng thái ACK của lệnh theo ID |


---

## 3. Triển Khai & Lập Trình

### 3.1. Triển Khai Firmware (ESP32)

#### 3.1.1. `config.h`

Tất cả hằng số phần cứng, tham số thuật toán và cấu hình FreeRTOS được định nghĩa một lần trong `include/config.h`. Không có magic number trong `*.cpp`.

```cpp
// Safety thresholds
#define TEMP_ALERT_C    60.0f     // Overheat → relay OFF (recoverable)
#define TEMP_FIRE_C     75.0f     // Fire risk → relay LOCKED (permanent)

// Smart charging parameters
#define CHARGE_TIMEOUT_MS  (8UL * 60UL * 60UL * 1000UL)  // 8 giờ
#define FULL_CURRENT_A      0.10f    // Trickle threshold: 100mA
#define FULL_HOLD_MS       (5UL * 60UL * 1000UL)          // 5 phút liên tục
#define UNPLUG_CURRENT_A    0.01f
#define UNPLUG_FROM_A       0.05f
#define SPIKE_FACTOR        1.30f
#define ROLLING_WINDOW_SEC  300U     // Cửa sổ 5 phút = 300 mẫu × 1s

// Button timing
#define BTN_DEBOUNCE_MS      50UL
#define BTN_SINGLE_MAX_MS   600UL
#define BTN_LONG_MIN_MS    2000UL
#define BTN2_SINGLE_MAX_MS  800UL
#define BTN2_PORTAL_MIN_MS 3000UL

// Timer intervals
#define SENSOR_READ_MS       1000UL
#define DISPLAY_REFRESH_MS    500UL
#define TELEMETRY_PUBLISH_MS 1000UL
#define WIFI_RETRY_MS        5000UL
#define MQTT_RETRY_MS        3000UL

// MQTT
#define TOPIC_TELEMETRY  "socket/telemetry"
#define TOPIC_CONTROL    "socket/control"
#define TOPIC_ALERT      "socket/alert"
#define TOPIC_ACK        "socket/control/ack"
#define MQTT_HOST        "413f71f93ddf45f4a270637f32f088fd.s1.eu.hivemq.cloud"
#define MQTT_PORT         8883
#define MQTT_USE_TLS      true
#define MQTT_CLIENTID    "iot-smart-socket-esp32"

// ── FreeRTOS Task Configuration ──────────────────────────────────────────────
#define RTOS_CHARGE_CORE       1   // PRO core: safety-critical
#define RTOS_NETCOMMS_CORE     0   // APP core: networking + display
#define RTOS_CHARGE_PRIO       3   // High priority
#define RTOS_NETCOMMS_PRIO     1   // Low priority
#define RTOS_CHARGE_STACK      8192
#define RTOS_NETCOMMS_STACK    10240
#define RTOS_CMD_QUEUE_DEPTH   6
#define RTOS_ALERT_QUEUE_DEPTH 12
#define CHARGE_TASK_PERIOD_MS  50  // 20 Hz cycle cho taskChargeSafety
```

#### 3.1.2. `SharedState.h` — Dữ Liệu Dùng Chung Giữa Hai Core

```cpp
// Fixed-size alert: tránh heap allocation trong task context
#define ALERT_TEXT_MAX 96
struct AlertMessage { char text[ALERT_TEXT_MAX]; };

// Lệnh từ Core 0 → Core 1
enum class CmdType : uint8_t { RELAY, SMART_MODE, RESET_ENERGY };
struct RelayCommand { CmdType type; bool value; };

// Snapshot dữ liệu: Core 1 ghi, Core 0 đọc (qua gStateMutex)
struct SharedState {
    PowerReading     power{};
    SafetyStatus     safety{};
    bool             relayOn       = false;
    bool             smartMode     = true;
    ChargeStopReason stopReason    = ChargeStopReason::NONE;
    bool             pzemFault     = true;
    bool             fireLocked    = false;
    // Các field sau do Core 0 ghi:
    bool             wifiConnected = false;
    bool             mqttConnected = false;
    bool             configMode    = false;
    String           lastAlert     = "System boot";
};

extern SemaphoreHandle_t gStateMutex;
extern QueueHandle_t     gCommandQueue;
extern QueueHandle_t     gAlertQueue;
extern SharedState       gSharedState;
```

#### 3.1.3. Smart Charging Algorithm — `ChargeManager.cpp`

Logic không thay đổi, giờ chạy trên Core 1 với `ChargeManager charger` là biến cục bộ của task:

**`_pushCurrent()` — Circular Buffer O(1):**

```cpp
void ChargeManager::_pushCurrent(float current) {
    if (_winCount < ROLLING_WINDOW_SEC) {
        // Giai đoạn warm-up: điền buffer từ đầu
        _window[_winIndex] = current;
        _winSum += current;
        _winCount++;
        _winIndex = (_winIndex + 1U) % ROLLING_WINDOW_SEC;
        return;
    }
    // Buffer đầy: sliding window — bỏ phần tử cũ nhất
    _winSum -= _window[_winIndex];
    _window[_winIndex] = current;
    _winSum += current;
    _winIndex = (_winIndex + 1U) % ROLLING_WINDOW_SEC;
}
```

#### 3.1.4. `taskChargeSafety` — Core 1, Priority 3

```cpp
static void taskChargeSafety(void* /*param*/) {
    // Core-1-local objects — không bao giờ được truy cập từ Core 0
    PZEM004Tv30   pzem(PZEM_SERIAL, PZEM_RX_PIN, PZEM_TX_PIN);
    ChargeManager charger;
    SafetyMonitor safety;

    charger.begin();
    safety.begin();

    bool             localFireLocked = false;
    PowerReading     localPower{};
    ChargeStopReason prevStopReason  = ChargeStopReason::NONE;

    // Helper: push alert vào queue (non-blocking)
    auto pushAlert = [](const char *msg) {
        AlertMessage am{};
        strncpy(am.text, msg, ALERT_TEXT_MAX - 1);
        xQueueSend(gAlertQueue, &am, 0);  // drop nếu queue đầy
    };

    TickType_t xLastWake = xTaskGetTickCount();

    for (;;) {
        // Đảm bảo chu kỳ CỨNG 50ms — không drift dù task bận
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(CHARGE_TASK_PERIOD_MS));

        // 1. Drain command queue từ Core 0
        RelayCommand cmd{};
        while (xQueueReceive(gCommandQueue, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
                case CmdType::RELAY:
                    charger.commandRelay(cmd.value, localFireLocked); break;
                case CmdType::SMART_MODE:
                    charger.setSmartMode(cmd.value); break;
                case CmdType::RESET_ENERGY:
                    pzem.resetEnergy(); pushAlert("Energy counter reset"); break;
            }
        }

        // 2. Đọc PZEM @1Hz
        // ... (millis gate 1000ms)

        // 3. Safety monitor
        safety.setPeripheralFault(localPzemFault);
        safety.update();

        if (s.overheat) charger.forceRelayOff(ChargeStopReason::SAFETY);
        if (s.fireRisk && !localFireLocked) {
            localFireLocked = true;  // PERMANENT — không có code reset
            charger.forceRelayOff(ChargeStopReason::SAFETY);
            pushAlert("FIRE RISK >75C: relay locked until hard reset");
        }

        // 4. Charge algorithm
        charger.update(localPower, localFireLocked);

        // 5. Ghi shared state (mutex tối đa 10ms)
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            gSharedState.power      = localPower;
            gSharedState.safety     = safety.status();
            gSharedState.relayOn    = charger.relayOn();
            gSharedState.smartMode  = charger.smartMode();
            gSharedState.stopReason = charger.lastStopReason();
            gSharedState.fireLocked = localFireLocked;
            xSemaphoreGive(gStateMutex);
        }
    }
}
```

#### 3.1.5. `taskNetComms` — Core 0, Priority 1

```cpp
static void taskNetComms(void* /*param*/) {
    // Core-0-local: WiFi, MQTT, DisplayUI
    gUi.begin();   // TFT init — SPI chỉ được init từ Core 0

    // Kết nối WiFi (ưu tiên saved credentials)
    // ... WiFi.begin() + wm setup

    gMqtt.setServer(MQTT_HOST, MQTT_PORT);
    gMqtt.setCallback(mqttCallback);  // mqttCallback gọi xQueueSend

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));

        // BTN1/BTN2 polling + FSM → xQueueSend(gCommandQueue, ...)
        // WiFi management + WiFiManager portal
        // MQTT connect + mqtt.loop()

        // Drain alert queue từ Core 1
        AlertMessage am{};
        while (xQueueReceive(gAlertQueue, &am, 0) == pdTRUE) {
            netPublishAlert(String(am.text));
        }

        // Đọc snapshot an toàn
        SharedState snap{};
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            snap = gSharedState;
            xSemaphoreGive(gStateMutex);
        }

        gUi.update(snap);          // DisplayUI nhận snapshot
        publishTelemetry(snap);    // JSON từ snapshot
    }
}
```

#### 3.1.6. `setup()` & `loop()` — Khởi Tạo RTOS

```cpp
void setup() {
    Serial.begin(115200);

    // Init GPIO trước khi tasks start
    pinMode(TOUCH_BTN_PIN,  INPUT);
    pinMode(TOUCH_BTN2_PIN, INPUT);
    // ... các pin khác

    WiFiStorage::initSPIFS();

    // Tạo RTOS primitives
    gStateMutex   = xSemaphoreCreateMutex();
    gCommandQueue = xQueueCreate(RTOS_CMD_QUEUE_DEPTH,   sizeof(RelayCommand));
    gAlertQueue   = xQueueCreate(RTOS_ALERT_QUEUE_DEPTH, sizeof(AlertMessage));

    // Kiểm tra tạo thành công
    configASSERT(gStateMutex);
    configASSERT(gCommandQueue);
    configASSERT(gAlertQueue);

    // Spawn tasks — pinned to cores
    xTaskCreatePinnedToCore(
        taskChargeSafety, "ChargeSafety",
        RTOS_CHARGE_STACK, nullptr,
        RTOS_CHARGE_PRIO, &hChargeSafety,
        RTOS_CHARGE_CORE    // Core 1
    );

    xTaskCreatePinnedToCore(
        taskNetComms, "NetComms",
        RTOS_NETCOMMS_STACK, nullptr,
        RTOS_NETCOMMS_PRIO, &hNetComms,
        RTOS_NETCOMMS_CORE  // Core 0
    );
}

void loop() {
    vTaskDelete(nullptr);  // Arduino loop task tự xóa — không dùng
}
```

#### 3.1.7. `DisplayUI` — Refactor Nhận SharedState Snapshot

`DisplayUI::update()` được refactor để không còn tham chiếu trực tiếp đến `ChargeManager` hay `SafetyMonitor` — thay vào đó nhận một `const SharedState& snap` đã được copy an toàn dưới mutex:

```cpp
// DisplayUI.h — constructor mặc định, không nhận reference
class DisplayUI {
public:
    DisplayUI() = default;
    void begin();
    void update(const SharedState &snap);   // thay vì nhiều params riêng lẻ
    void toggleDisplay();
    bool isDisplayOn() const;
private:
    TFT_eSPI _tft{};
    // Không còn ChargeManager& _chg và SafetyMonitor& _safety
};

// DisplayUI.cpp — đọc từ snap thay vì gọi trực tiếp methods
void DisplayUI::update(const SharedState &snap) {
    if (!_displayOn) return;
    // ... render từ snap.power, snap.safety, snap.relayOn, snap.lastAlert
}
```

Điều này bảo đảm thread safety: `DisplayUI` chỉ làm việc với data đã được copy hoàn toàn trước khi vẽ — không có race condition với Core 1 đang cập nhật `gSharedState`.

#### 3.1.8. `SafetyMonitor` — LED và Buzzer Logic (Core 1)

Từ `SafetyMonitor.cpp:_updateLedsAndBuzzer()`:
```cpp
void SafetyMonitor::_updateLedsAndBuzzer() {
    const bool hasFault = _status.sensorFault || _peripheralFault;
    const bool redOn = hasFault || _status.overheat || _status.fireRisk;

    digitalWrite(LED_RED_PIN, redOn ? HIGH : LOW);   // Core 1 sở hữu
    digitalWrite(BUZZER_PIN, _status.fireRisk ? HIGH : LOW);  // Core 1 sở hữu
    // LED_GREEN_PIN được quản lý bởi taskNetComms trên Core 0
}
```

LED đỏ sáng khi **bất kỳ** fault nào: sensorFault (DS18B20 offline), peripheralFault (PZEM lỗi), overheat, hoặc fireRisk.  
Buzzer chỉ kêu khi `fireRisk = true` (T > 75°C).

`LED_RED_PIN` và `BUZZER_PIN` thuộc Core 1; `LED_GREEN_PIN` thuộc Core 0. Các pin khác nhau không chia sẻ GPIO register nên `digitalWrite` từ hai core vào **các pin khác nhau** là an toàn.

#### 3.1.9. WiFi Management (Core 0)

**Chiến lược khởi động WiFi (từ `taskNetComms`):**
```cpp
WiFiCredentials creds{"", ""};
WiFi.mode(WIFI_STA);
if (WiFiStorage::loadWiFiCredentials(creds) && strlen(creds.ssid) > 0) {
    WiFi.begin(creds.ssid, creds.password);  // Ưu tiên saved credentials
} else {
    WiFi.begin(WIFI_SSID, WIFI_PASS);        // Fallback hardcoded
}
```

**WiFiManager Non-blocking:** `wm.setConfigPortalBlocking(false)` + `gWm.process()` mỗi iteration. Trong khi người dùng cấu hình WiFi qua browser trên Core 0, `taskChargeSafety` trên Core 1 hoàn toàn không bị ảnh hưởng — safety monitoring, PZEM reading và relay control tiếp tục chạy bình thường.



#### 3.1.10. `main.cpp` — Cơ Chế ACK Lệnh (Qua Shared State)

Cơ chế ACK được cập nhật để đọc trạng thái thực từ `gSharedState` thay vì từ `charger` trực tiếp:

```cpp
// Pending commands verify (trong taskNetComms, Core 0):
SharedState snap{};
if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    snap = gSharedState;  // Core 1 đã update relayOn/smartMode
    xSemaphoreGive(gStateMutex);
}

if (command == "relay") {
    actual   = snap.relayOn  ? "ON" : "OFF";  // Đọc từ snapshot
    verified = (actual == requested);
}

// Nếu verified → ACK "confirmed"
// Nếu !verified → ACK "failed" + netPublishAlert()
```

### 3.2. Cấu Trúc Code

#### 3.2.1. Tổ Chức File

| File | Dòng | Kích Thước | Chức Năng |
|------|------|-----------|----------|
| `include/config.h` | 116 | 3.0 KB | Hằng số GPIO, ngưỡng, timer, MQTT + **RTOS tunables** |
| `include/SharedState.h` | 60 | 1.8 KB | Struct IPC giữa 2 core: SharedState, RelayCommand, AlertMessage, extern RTOS handles |
| `include/ChargeManager.h` | 54 | 1.3 KB | Interface + `struct PowerReading` + `enum ChargeStopReason` |
| `include/SafetyMonitor.h` | 39 | 1.0 KB | Interface + `struct SafetyStatus` |
| `include/DisplayUI.h` | 26 | 0.8 KB | Interface (refactor: `update(SharedState&)`, no refs to ChargeManager/SafetyMonitor) |
| `include/WiFiStorage.h` | 116 | 3.5 KB | Header-only static class: SPIFFS CRUD |
| `src/main.cpp` | ~310 | 12.0 KB | **RTOS**: setup(), loop(), 2 tasks, MQTT callback, WiFi helpers |
| `src/ChargeManager.cpp` | 124 | 3.3 KB | Thuật toán sạc + circular buffer (không đổi) |
| `src/SafetyMonitor.cpp` | 65 | 1.9 KB | DS18B20 async + LED/buzzer (không đổi) |
| `src/DisplayUI.cpp` | 85 | 3.6 KB | TFT layout (refactor: nhận SharedState snapshot) |
| `web-dashboard/server.js` | 347 | 9.5 KB | Node.js: Express + MQTT + Socket.IO |
| `web-dashboard/public/js/app.js` | 205 | 7.1 KB | Browser: Socket.IO client + DOM |






## 4. Đánh Giá & Kiểm Thử

### 4.1. Kế Hoạch Test Cases

Các test case dưới đây được xây dựng dựa trên hành vi được định nghĩa trong code, bao gồm cả các kịch bản liên quan đến RTOS multi-core.

#### 4.1.1. Nhóm 1 — Đo Lường Điện (PZEM-004T)

| ID | Kịch Bản | Điều Kiện | Kết Quả Mong Đợi | Cách Xác Nhận |
|----|---------|----------|-----------------|--------------| 
| TC-01 | Đo điện bình thường | PZEM kết nối, có tải | LCD & MQTT telemetry không có "null" hoặc "--" | Serial Monitor + Dashboard |
| TC-02 | PZEM mất kết nối | Ngắt dây UART | Alert vào gAlertQueue → Core 0 publish MQTT, LCD "--", LED đỏ | Serial Monitor |
| TC-03 | PZEM phục hồi | Cắm lại UART sau TC-02 | `localPzemFault=false`, LCD hiển thị giá trị, LED đỏ tắt | LCD |
| TC-04 | Reset Wh bằng BTN1 | Nhấn BTN1 < 600ms | `CmdType::RESET_ENERGY` → queue → Core 1: `pzem.resetEnergy()` + pushAlert | LCD Energy = 0 trong ~50ms |

#### 4.1.2. Nhóm 2 — An Toàn Nhiệt (SafetyMonitor — Core 1)

| ID | Kịch Bản | Điều Kiện | Kết Quả Mong Đợi | Cách Xác Nhận |
|----|---------|----------|-----------------|--------------| 
| TC-05 | DS18B20 offline | Ngắt dây sensor | `sensorFault=true`, LCD "--", LED đỏ | Serial Monitor |
| TC-06 | Nhiệt độ bình thường | T < 60°C | LED đỏ TẮT, relay không bị ảnh hưởng | LED vật lý |
| TC-07 | Overheat > 60°C | Gia nhiệt sensor | `overheat=true`, relay OFF, LED đỏ, alert qua gAlertQueue | Relay LED tắt |
| TC-08 | Fire Lock > 75°C | Tiếp tục gia nhiệt | `localFireLocked=true` trên Core 1, relay LOCK, buzzer | Relay không bật lại |
| TC-09 | Lệnh ON sau Fire Lock | Publish `{"relay":"ON"}` sau TC-08 | Core 1 nhận cmd, `commandRelay(true, true)` return ngay | ACK "failed" |

#### 4.1.3. Nhóm 3 — Smart Charging (ChargeManager — Core 1)

| ID | Kịch Bản | Điều Kiện | Kết Quả Mong Đợi | Cách Xác Nhận |
|----|---------|----------|-----------------|--------------| 
| TC-10 | Phát hiện pin đầy | Smart Mode ON, dòng < 0.10A | Sau 5 phút, relay OFF, `gSharedState.stopReason=FULL` | Dashboard + Relay |
| TC-11 | Reset Full timer | Dòng giảm < 0.10A rồi tăng lên | `_lowCurrentStartMs = 0`, không ngắt sớm | Relay vẫn ON |
| TC-12 | Unplug detection | Dòng giảm từ > 0.05A xuống < 0.01A | Sau 2 mẫu (~100ms với 50ms cycle), relay OFF | Dashboard |
| TC-13 | Spike detection | Dòng tăng > 1.30× rolling avg | Relay OFF, `stopReason=SPIKE`, alert qua queue | Serial Monitor |
| TC-14 | Timeout 8 giờ | Relay ON liên tục | Sau 8h, relay OFF, `stopReason=TIMEOUT` | Log |
| TC-15 | Manual Mode bypass | Smart Mode OFF | Không có Full/Unplug/Spike, chỉ Timeout | Relay vẫn ON khi dòng < 0.10A |

#### 4.1.4. Nhóm 4 — MQTT & Điều Khiển Từ Xa

| ID | Kịch Bản | Điều Kiện | Kết Quả Mong Đợi | Cách Xác Nhận |
|----|---------|----------|-----------------|--------------| 
| TC-16 | Relay ON từ MQTT | Publish `{"relay":"ON"}` | mqttCallback → gCommandQueue → Core 1 → relay ON, ACK ~150ms | Dashboard + ACK payload |
| TC-17 | Relay OFF từ MQTT | Publish `{"relay":"OFF"}` | Relay tắt, ACK confirmed | Dashboard |
| TC-18 | Smart mode từ MQTT | Publish `{"charge_mode":"ON"}` | Core 1 setSmartMode, gSharedState cập nhật, LCD, ACK | LCD |
| TC-19 | Format lệnh sai | Publish `relay=ON` (sai format) | `indexOf()` không match, không enqueue, không crash | Serial Monitor: không có log relay |
| TC-20 | ACK verify từ SharedState | Sau bất kỳ lệnh relay | Core 0 đọc `snap.relayOn` từ mutex, JSON ACK đầy đủ | MQTT client |

#### 4.1.5. Nhóm 5 — RTOS Multi-Core & Kết Nối

| ID | Kịch Bản | Điều Kiện | Kết Quả Mong Đợi | Cách Xác Nhận |
|----|---------|----------|-----------------|--------------| 
| TC-21 | Mất WiFi | Tắt router | Core 0: retry; Core 1: hoạt động bình thường, safety active | Serial log "[Core1]" tiếp tục |
| TC-22 | Safety khi mất WiFi | Mất mạng khi T > 60°C | Core 1 cắt relay ngay, gAlertQueue lưu alert chờ WiFi | Relay tắt vật lý |
| TC-23 | WiFi phục hồi | Bật lại router | Core 0 reconnect, drain gAlertQueue, publish alerts bị delay | LCD WiFi/MQTT → CONNECTED |
| TC-24 | Core isolation | Flood MQTT commands đồng thời | Core 1 không bị stall — gCommandQueue buffer, Core 1 drain tuần tự | Relay vẫn respond đúng |
| TC-25 | BTN2 giữ 3s | Giữ BTN2 ≥ 3000ms | Core 0 WiFi portal; Core 1 tiếp tục read PZEM + safety | "SmartSocket-Setup" + relay vẫn active |


---

## 5. Tính Sáng Tạo & Mở Rộng

### 5.1. Điểm Sáng Tạo Kỹ Thuật

#### 5.1.1. Giao Thức ACK Xác Nhận Lệnh Hai Chiều Với RTOS Queue

Hầu hết IoT projects đơn giản gửi lệnh và assume thiết bị đã thực thi (fire-and-forget). Ender Socket triển khai verification loop qua RTOS queue:

```
Dashboard           Core 0 (taskNetComms)      gCommandQueue      Core 1 (taskChargeSafety)
    │                       │                        │                      │
    ├─ control_relay(ON) ──►│                        │                      │
    │                       ├─ xQueueSend(RELAY,ON) ►│                      │
    │                       ├─ addPendingCmd()        │                      │
    │                       │                         │ xQueueReceive() ────►│
    │                       │                         │                      ├─ commandRelay(true)
    │                       │                         │                      ├─ GPIO27=HIGH
    │                       │                         │                      └─ write gSharedState.relayOn=true
    │  [150ms trôi qua]     │                         │
    │                       ├─ xSemaphoreTake(mutex)  │
    │                       ├─ snap = gSharedState    │
    │                       ├─ xSemaphoreGive(mutex)  │
    │                       ├─ actual = snap.relayOn ? "ON" : "OFF"
    │                       ├─ verified = (actual == "ON")
    │                       ├─ mqtt.publish(TOPIC_ACK, ackJSON)
    │◄── command_acked ──────│                         │
```

Nếu relay bị hỏng cơ học, `snap.relayOn=false` sau command → ACK `"status":"failed"` + alert. Dashboard không hiển thị ghost state.

#### 5.1.2. FreeRTOS Dual-Core: Safety-Critical Isolation

Đây là điểm kỹ thuật nổi bật nhất của dự án. Thay vì toàn bộ firmware chạy trên một `loop()` duy nhất (cooperative scheduling dễ bị delay bởi WiFi reconnect), dự án phân tách hai miền quan tâm:

**Core 1 — Safety-Critical Domain:**
- Không bao giờ bị ảnh hưởng bởi WiFi stack, MQTT TLS handshake, hay LCD rendering
- `vTaskDelayUntil()` đảm bảo chu kỳ giám sát an toàn chính xác 50ms
- Relay, DS18B20, PZEM — tất cả peripheral nhạy cảm chỉ truy cập từ đây

**Core 0 — Communication Domain:**
- WiFiManager captive portal không ảnh hưởng đến sampling nhiệt độ
- TFT_eSPI (không thread-safe) chỉ chạy trên một core duy nhất
- MQTT callback chuyển lệnh thành queue item — không thực thi hardware trực tiếp

```
   Scenario: WiFi mất 30 giây + reconnect
   ═══════════════════════════════════════
   Core 0: [WiFi.reconnect()...]......[MQTT reconnect...][drain alerts...][publish]
   Core 1: [50ms]─[50ms]─[50ms]─[50ms]─[50ms]─...─[50ms] ← KHÔNG GIÁN ĐOẠN
                  Safety checks running every 50ms regardless of networking
```

#### 5.1.3. Inter-Core Communication Pattern An Toàn

Thiết kế IPC tránh hoàn toàn shared mutable state không bảo vệ:

- **Queue thay vì global var:** Lệnh relay không được ghi vào biến global rồi Core 1 đọc — dùng `xQueueSend/Receive` có built-in synchronization.
- **Copy-then-release mutex:** Core 0 không giữ mutex trong suốt quá trình render LCD (có thể mất vài ms) — chỉ hold trong thời gian copy struct.
- **Fixed-size buffer trong queue:** `AlertMessage {char[96]}` tránh `new/delete` trong task context, ngăn heap fragmentation.

#### 5.1.4. Multi-criteria Charging Decision

Thay vì đơn giản "tắt sau N giờ", hệ thống kết hợp 4 tiêu chí:

- **Full:** dòng trickle < 0.10A trong 5 phút liên tục → phù hợp pin lithium CC-CV
- **Unplug:** drop đột ngột từ > 0.05A xuống < 0.01A, cần 2 mẫu liên tiếp → tránh false positive từ dòng dao động ngắn
- **Spike:** dòng vượt 130% rolling average 5 phút, chỉ active sau warm-up → phát hiện ngắn mạch cục bộ
- **Timeout:** 8 giờ cứng → safety net cuối cùng cho mọi loại tải

Với FreeRTOS, `ChargeManager::update()` được gọi mỗi 50ms thay vì phụ thuộc vào tốc độ loop — đảm bảo unplug detection (2 mẫu × 50ms = 100ms) và spike detection nhạy hơn.

#### 5.1.5. WiFiManager Non-blocking + Core Isolation

`wm.setConfigPortalBlocking(false)` kết hợp FreeRTOS: trong khi người dùng cấu hình WiFi qua browser (Core 0 đang xử lý HTTP requests), hệ thống vẫn:
- Đọc và kiểm tra cảm biến nhiệt mỗi 50ms (Core 1)
- Điều khiển relay an toàn (Core 1)
- Cập nhật LED/buzzer theo trạng thái safety (Core 1)

### 5.2. Hướng Mở Rộng (Có Nền Tảng Sẵn)

#### 5.2.1. SD Card 

`SD_CS_PIN 12` đã được định nghĩa trong `config.h` và SPI bus đã có. Chỉ cần thêm thư viện `SD` để ghi telemetry lịch sử vào CSV file trong `taskNetComms` (Core 0, cùng SPI bus với TFT).

#### 5.2.2. OTA Firmware Update

ESP32 hỗ trợ `ArduinoOTA`. Với FreeRTOS, OTA có thể chạy trong `taskNetComms` (Core 0):
```cpp
#include <ArduinoOTA.h>
// trong taskNetComms: ArduinoOTA.begin() + ArduinoOTA.handle()
// Core 1 vẫn giám sát safety trong khi OTA đang flash
```

#### 5.2.3. Task Watchdog Timer (TWDT)

ESP-IDF FreeRTOS có Task Watchdog Timer. Có thể đăng ký `taskChargeSafety` vào TWDT — nếu task không "nhịp" trong N giây, hệ thống tự reset. Phù hợp cho production deployment:
```cpp
#include <esp_task_wdt.h>
esp_task_wdt_add(NULL);          // đăng ký task hiện tại
esp_task_wdt_reset();            // gọi mỗi cycle để giữ watchdog
```

#### 5.2.4. Đa Thiết Bị

MQTT topic schema hiện tại cố định (`socket/telemetry`). Đổi thành `socket/{device_id}/telemetry` (thêm `MQTT_CLIENTID` làm device_id) và server.js nhận multiple devices ngay mà không cần thay đổi cơ bản.

---

## 6. Kết Luận

### 6.1. Các Yêu Cầu Đã Hoàn Thành

| Yêu Cầu | Triển Khai | File Chứng Minh |
|---------|-----------|----------------|
| Đo V, I, P, Wh | `readPzem()` — PZEM-004T qua UART2 (Core 1) | `main.cpp:taskChargeSafety` |
| Giám sát nhiệt độ kép | `SafetyMonitor::update()` — DS18B20 async (Core 1) | `SafetyMonitor.cpp` |
| Relay tại chỗ (BTN1) | BTN1 → `gCommandQueue` → Core 1 `commandRelay()` | `main.cpp:taskNetComms` |
| Relay từ xa (MQTT) | `mqttCallback()` → `xQueueSend(gCommandQueue)` → Core 1 | `main.cpp` |
| Phát hiện pin đầy | `FULL_CURRENT_A`, `FULL_HOLD_MS` logic (Core 1) | `ChargeManager.cpp:65` |
| Phát hiện rút phích | `_unplugLowCount >= 2` (Core 1) | `ChargeManager.cpp:59` |
| Spike detection | Rolling avg 300 mẫu × `SPIKE_FACTOR` (Core 1) | `ChargeManager.cpp:76` |
| Timeout 8 giờ | `CHARGE_TIMEOUT_MS` (Core 1) | `ChargeManager.cpp:43` |
| Bảo vệ > 60°C | `TEMP_ALERT_C`, relay OFF (Core 1) | `main.cpp:taskChargeSafety` |
| Fire Lock > 75°C | `localFireLocked=true` permanent (Core 1) | `main.cpp:taskChargeSafety` |
| Hiển thị LCD | `DisplayUI::update(snap)` 500ms (Core 0) | `DisplayUI.cpp` |
| Toggle màn hình | `toggleDisplay()` GPIO32 (Core 0) | `DisplayUI.cpp:22` |
| MQTT Telemetry 1Hz | `publishTelemetry(snap)` từ SharedState (Core 0) | `main.cpp:taskNetComms` |
| MQTT ACK verification | `gPendingCmds`, 150ms, verify từ `snap.relayOn` (Core 0) | `main.cpp:taskNetComms` |
| Web Dashboard | `server.js` + `app.js` | `web-dashboard/` |
| WiFi Captive Portal | WiFiManager non-blocking (Core 0) | `main.cpp:taskNetComms` |
| Lưu WiFi credentials | `WiFiStorage::saveWiFiCredentials()` | `WiFiStorage.h` |
| Hardware interrupt | `Ticker gGreenBlinkTicker` | `main.cpp` |
| **FreeRTOS Dual-Core** | `xTaskCreatePinnedToCore()` × 2, Core 0 & Core 1 | `main.cpp:setup()` |
| **RTOS IPC (Queue)** | `gCommandQueue` + `gAlertQueue` — `xQueueSend/Receive` | `SharedState.h`, `main.cpp` |
| **RTOS Sync (Mutex)** | `gStateMutex` bảo vệ `gSharedState` | `main.cpp` |
| **Core Isolation** | Safety/Charging Core 1; Network/Display Core 0 | `main.cpp` |
| Error handling (12 loại) | Edge detection, queues, mutex timeout | `main.cpp`, `SafetyMonitor.cpp` |

### 6.2. Điểm Cần Cải Thiện Trước Production

1. **Flash headroom:** Hiện 94.9% — cần bỏ smooth font TFT hoặc tối ưu TLS để có headroom cho tính năng mới.
2. **Bảo mật TLS:** Thay `setInsecure()` bằng `setCACert()` với HiveMQ CA certificate thật.
3. **Bảo mật credentials:** Chuyển `WIFI_SSID/PASS`, `MQTT_USERNAME/PASSWORD` ra file `.env` hoặc SPIFFS, không hardcode trong header.
4. **Smart mode persistence:** Lưu `_smartMode` vào SPIFFS để nhớ qua các lần reset.
5. **Control message parser:** Dùng ArduinoJson (đã là dependency) để parse control payload thay vì `indexOf()` fragile.
6. **Task Watchdog Timer:** Đăng ký `taskChargeSafety` vào TWDT để auto-reset nếu core bị stall không mong muốn.

---

## 7. Tài Liệu Tham Khảo

Thư viện được sử dụng (trích từ `platformio.ini` và `web-dashboard/package.json`):

**Firmware (PlatformIO `lib_deps`):**
1. `bodmer/TFT_eSPI @ ^2.5.43` — TFT LCD driver
2. `mandulaj/PZEM-004T-v30 @ ^1.1.2` — Power meter driver
3. `paulstoffregen/OneWire @ ^2.3.8` — 1-Wire bus protocol
4. `milesburton/DallasTemperature @ ^4.0.5` — DS18B20 sensor
5. `knolleary/PubSubClient @ ^2.8` — MQTT client
6. `tzapu/WiFiManager @ ^2.0.17` — WiFi Captive Portal
7. `bblanchon/ArduinoJson @ ^7.0.0` — JSON serialization (dùng trong WiFiStorage)

**RTOS & Platform:**
- `FreeRTOS` — tích hợp trong ESP-IDF / Arduino framework ESP32. APIs được dùng: `xTaskCreatePinnedToCore()`, `xSemaphoreCreateMutex()`, `xQueueCreate()`, `xQueueSend()`, `xQueueReceive()`, `xSemaphoreTake()`, `xSemaphoreGive()`, `vTaskDelay()`, `vTaskDelayUntil()`, `vTaskDelete()`, `configASSERT()`.
- `pioarduino/platform-espressif32` (stable) — ESP32 Arduino framework qua PlatformIO.
- `Ticker` — hardware timer ISR wrapper (built-in Arduino ESP32 framework).

**Web Dashboard (`package.json` dependencies):**
1. `express` — HTTP server framework
2. `mqtt` — MQTT client cho Node.js
3. `socket.io` — WebSocket với fallback
4. `uuid` — Tạo unique MQTT client ID
5. `dotenv` — Load `.env` configuration


