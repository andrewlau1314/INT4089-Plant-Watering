# MQTT + 手機 App 設定（INT4089 Auto Watering）

**English labels:** Mosquitto broker · MQTT · IoT MQTT Panel · same Wi‑Fi

本文件說明如何用 **本機 Eclipse Mosquitto**（唔使 bemfa）同手機 App 遙控自動澆水 ESP32。

> 課堂 Demo：`allow_anonymous true`（唔使帳密）。正式／公開網絡請改用密碼。

---

## 0. 架構（Architecture）

```
手機 MQTT App  ──►  筆電 Mosquitto (:1883)  ──►  ESP32 (wifi.ino)
      ▲                      │
      └──── 訂閱感測數值 ◄────┘
```

**必須同一 Wi‑Fi：** 手機、筆電、ESP32 全部連同一個熱點／網絡（例如 SSID `@@`）。

---

## 1. 啟動 Mosquitto（PC）

1. 安裝路徑：`C:\Program Files\mosquitto\`
2. Windows 服務名：**Mosquitto Broker**（`mosquitto`），預設 Automatic，開機會開。
3. 檢查服務：
   - `Win + R` → `services.msc` → 搵 **Mosquitto Broker** → 狀態應為 **Running**
   - 或 PowerShell：`Get-Service mosquitto`
4. 手動重啟（如需要）：
   ```powershell
   Restart-Service mosquitto
   ```
5. 確認埠口：
   ```powershell
   netstat -an | findstr ":1883"
   ```
   應見到：`TCP  0.0.0.0:1883  ... LISTENING`

本專案設定（已寫入 `mosquitto.conf`）：
- `listener 1883 0.0.0.0`（允許區網裝置連入）
- `allow_anonymous true`（Demo 用）
- 防火牆規則：**Mosquitto MQTT 1883**（Inbound TCP 1883）

---

## 2. 搵筆電 IP（`ipconfig`）

ESP32 同手機 App 都要填 **筆電而家嘅 IPv4**（唔係 127.0.0.1）。

1. 開 **Command Prompt** 或 PowerShell
2. 輸入：
   ```text
   ipconfig
   ```
3. 睇 **Wireless LAN adapter Wi-Fi**（或你連緊嗰個 adapter）下面嘅：
   ```text
   IPv4 Address. . . . . . . . . . . : 10.x.x.x
   ```
4. 抄低呢個 IP。

**注意：**
- 換網絡／開 hotspot `@@` 之後，IP **會變**，要重新 `ipconfig`，再改 sketch 同 App。
- 目前設定範例用過：`10.209.163.170`（校園 Wi‑Fi）。Demo 當日用 hotspot 請改成當下 IP。

---

## 3. ESP32：改 MQTT_HOST 同 Upload

檔案：
- `C:\EDU2628\INT4089\wifi\wifi.ino`（Arduino 開呢個資料夾）
- 根目錄亦有同步副本：`C:\EDU2628\INT4089\wifi.ino`
- bemfa 舊版備份：`wifi.ino.bemfa.bak`

喺 sketch 頂部改：

```cpp
#define MQTT_HOST "你的筆電IPv4"   // 例如 "192.168.43.1"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "esp32-watering"
```

Wi‑Fi 仍然係：
- SSID：`@@`
- Password：`YOUR_WIFI_PASSWORD`

Upload：
- Board：`esp32:esp32:esp32da`（或你平常用嗰個）
- Port：COM11（以 Device Manager 為準）
- Serial Monitor：115200，應見到 `MQTT_HOST=...` 同 `connected OK`

**Topics（保持唔變）：**
| 方向 | Topic | Payload / 內容 |
|------|-------|----------------|
| 訂閱 | `pump` | `pump_on` / `pump_off` |
| 訂閱 | `auto` | `auto_on` / `auto_off` |
| 發佈 | `wendu` | 溫度 |
| 發佈 | `shidu` | 濕度 |
| 發佈 | `light` | 光線 |
| 發佈 | `soil` | 土壤 % |
| 發佈 | `water` | 水位 |

---

## 4. Android：IoT MQTT Panel

1. Google Play 搜尋安裝 **IoT MQTT Panel**（或 MQTT Dashboard）。
2. 新增 Connection / Broker：
   - **Address / Host：** 筆電 IPv4（同上一步）
   - **Port：** `1883`
   - **Client ID：** 任意，例如 `phone-panel`
   - Username / Password：留空（Demo anonymous）
3. 連線成功後，建立 Dashboard 掣（Publish）：

| 按鈕名稱 | Topic | Payload | QoS |
|----------|-------|---------|-----|
| Pump ON | `pump` | `pump_on` | 0 |
| Pump OFF | `pump` | `pump_off` | 0 |
| Auto ON | `auto` | `auto_on` | 0 |
| Auto OFF | `auto` | `auto_off` | 0 |

4. 建立 Subscribe / 顯示面板（睇感測）：

| 面板 | Topic |
|------|-------|
| Temp | `wendu` |
| Humi | `shidu` |
| Light | `light` |
| Soil | `soil` |
| Water | `water` |

5. 按 **Pump ON** 應開泵；**Auto ON** 後會跟土壤／水位自動澆水（同 `auto.ino` 邏輯）。

---

## 5. iPhone 替代（brief）

App Store 可試：
- **MQTT Dashboard**
- **MyMQTT**
- **MQTTool**

設定同 Android：**Host = 筆電 IP，Port = 1883，無帳密**；Publish / Subscribe 用上表同一 topic 同 payload。

---

## 6. 同一 Wi‑Fi 要求（Same Wi‑Fi）

| 裝置 | 應連 |
|------|------|
| 筆電（跑 Mosquitto） | hotspot `@@` 或同一 router |
| ESP32 | `@@`（sketch 內 SSID） |
| 手機 | 同一個 `@@` |

唔得嘅情況：
- 筆電連校園網、手機連 4G → MQTT 連唔到
- 只填 `127.0.0.1` 喺手機／ESP32 → 只係筆電自己，ESP32／手機會失敗

建議 Demo：手機開 **個人熱點 `@@`**，筆電同 ESP32 都連呢個熱點，再 `ipconfig` 更新 `MQTT_HOST`。

---

## 7. 快速排錯（Troubleshooting）

| 症狀 | 檢查 |
|------|------|
| LCD / Serial：`WiFi FAILED` | SSID/密碼；ESP32 同 hotspot 距離；20s timeout |
| Serial：`MQTT failed` / `rc=` | `MQTT_HOST` 係咪最新 `ipconfig`；Mosquitto 服務 Running；防火牆 1883 |
| 手機連唔到 broker | 手機同 PC 同一 Wi‑Fi；關 VPN；確認規則 **Mosquitto MQTT 1883** |
| 掣無反應 | Topic / payload 大小寫要完全一樣：`pump_on` 唔係 `Pump_On` |
| 有數值但泵唔動 | 先試手動 `pump_on`；自動要先 `auto_on`，土壤 <30% 同有水 |
| 換咗網絡之後全滅 | 重新 `ipconfig` → 改 `MQTT_HOST` → 重新 Upload → App 改 Host |

本機自測（筆電）：
```powershell
& "C:\Program Files\mosquitto\mosquitto_sub.exe" -h 127.0.0.1 -t pump
# 另一視窗：
& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h 127.0.0.1 -t pump -m pump_on
```

---

## 8. Demo 當日 Checklist

1. [ ] 開 hotspot `@@`，筆電／ESP32／手機都連上  
2. [ ] `ipconfig` → 更新 `MQTT_HOST` → Upload `wifi`  
3. [ ] `Get-Service mosquitto` = Running  
4. [ ] 手機 App 填新 IP:1883，試 Pump ON/OFF  
5. [ ] 訂閱 `wendu`/`soil` 見到數值  
6. [ ] Auto ON → 示範自動澆水  

---

*INT4089 Group Project · Mosquitto replaces bemfa.com · Port 1883*
