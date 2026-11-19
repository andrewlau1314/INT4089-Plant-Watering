# INT4089 × HS-E11 追光器接線說明

目標：把 HS-E11 嘅兩隻舵機同 HS-S06B 四路光敏，接到現有 ESP32-WROOM-DA（自動澆水），唔再用 UNO 同 HS-UNO-EXP1 擴展板。

## 重要安全事項

- 唔好把 HS-UNO-EXP1 插去 ESP32，亦唔好沿用套件嗰條 5P 排線硬插；請用杜邦線逐條對絲印：GND、VCC、A1、A2、A3、A4。
- 四路光敏 VCC 接 ESP32 嘅 **3.3V**（模組支援 3.3V／5V）。如果接 5V，模擬輸出可能超過 3.3V，會燒壞 ESP32。
- 兩隻舵機嘅紅線（VCC）**唔好**接 ESP32 嘅 3.3V 或開發板 5V。用套件說明書講嘅 DC 6–12V 電源／電池盒（經舵機電源或 5V 穩壓）供電，並且 **電源負極一定要同 ESP32 GND 共地**。
- 裝舵機臂前，先 upload 呢份程式等舵機歸位到 90°，然後斷電再裝臂，避免堵轉燒舵機。
- 原本插喺 GPIO35 嘅單路光敏要拔走，呢個腳改接四路光敏嘅 A2。

## 現有澆水系統（保持不動）

| 功能 | GPIO |
| --- | --- |
| DHT11 | 4 |
| LED | 12 |
| 蜂鳴器 | 15 |
| 水泵 INA | 18 |
| 水泵 INB | 19 |
| LCD SDA / SCL | 21 / 22 |
| 土壤 | 33 |
| 水位 | 34 |

## 新增接線

官方 HS-S06B 方位：A1＝左下、A2＝左上、A3＝右下、A4＝右上。光照越強，模擬讀數越細。只用不被 Wi‑Fi 干擾嘅 ADC1 腳；GPIO34 已用嚟測水位，所以唔跟官方示例用 34。

| HS-E11 線 | 接到 ESP32 | 備註 |
| --- | --- | --- |
| 光敏 GND | GND | 同舵機電源負極、ESP32 GND 共地 |
| 光敏 VCC | 3.3V | 唔好接 5V |
| A1 左下 | GPIO32 | ADC1 |
| A2 左上 | GPIO35 | 頂替舊單路光敏 |
| A3 右下 | GPIO36（VP） | ADC1，只有輸入 |
| A4 右上 | GPIO39（VN） | ADC1，只有輸入 |
| 底盤舵機訊號（通常橙／黃） | GPIO25 | 水平／左右 |
| 面板舵機訊號（通常橙／黃） | GPIO26 | 垂直／上下 |
| 舵機紅線 | 外置 6–12V 電源嘅 5V／舵機正極 | 唔經 ESP32 |
| 舵機啡／黑線 | 外置電源負極 **同** ESP32 GND | 必須共地 |

太陽能板同燈珠維持套件原本接法，唔使接入 ESP32。

## 上傳同 dashboard

1. Arduino IDE 安裝程式庫 **ESP32Servo**。Board 選 ESP32-WROOM-DA（COM11 CH340）。電腦 USB 只連 ESP32，唔好連套件 UNO。因為加咗藍牙，要用分割表 **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**（`arduino-cli`：`esp32:esp32:esp32da:PartitionScheme=min_spiffs`）；預設 1.2MB 會 compile 唔過。
2. `MQTT_HOST` 必須係跑 Mosquitto 嗰部電腦嘅 Wi‑Fi IPv4（`ipconfig`）。呢個唔係手機 IP，亦唔可以填 `127.0.0.1`。換網絡之後如果用藍牙同步過，會以 ESP32 flash 入面保存嘅 IP 為準。
3. 先斷開舵機臂或確定無機械阻力，再 Upload。未確認前唔好 upload。
4. 用新嘅 `dashboard/index.html` 取代舊 dashboard。瀏覽器 Broker host 填電腦 Wi‑Fi IP，port `9001`。如果用電腦開頁，可用 `http://127.0.0.1:8080/`；手機要同電腦同一 Wi‑Fi，開 `http://電腦IP:8080/`。
5. Dashboard 可開關自動追光，同埋喺手動模式用滑桿調角度。

## 藍牙同步 IP（唔使每次改程式再 upload）

ESP32 廣播名稱 **INT4089-ESP32**（Nordic UART）。配對後：

- 傳送 `MQTT_HOST=電腦Wi-Fi-IP`（或只傳送嗰個 IPv4）→ ESP32 保存並改連 Mosquitto。
- 傳送 `GET` → ESP32 回 `ESP32_IP=...` 同 `MQTT_HOST=...`。
- `PHONE_IP=...` 只係確認／顯示，**唔會**改 MQTT broker。

網頁「藍牙配對／同步 IP」要用電腦 Chrome 開 `http://127.0.0.1:8080/`（localhost 先算安全來源）。手機瀏覽器開 `http://電腦IP:8080/` 通常會被擋住 Web Bluetooth；改用 App **Serial Bluetooth Terminal** 連 INT4089-ESP32，再傳送 `MQTT_HOST=電腦IP`。iPhone 不支援網頁藍牙。

如果舵機轉錯方向，喺 `wifi.ino` 把 `REVERSE_PAN` 或 `REVERSE_TILT` 改成 `1` 再 upload。
