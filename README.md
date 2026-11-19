# INT4089 Plant Watering (Group 9)

EdUHK INT4089 Technologies in STEM and AI Education.

Group 9: LAU HIN YAU, WANG ZIRUI, XU RUIYI.

ESP32 automatic plant watering, light tracking, and a local Mosquitto IoT dashboard.

## Hardware

- ESP32-WROOM-DA (fqbn `esp32:esp32:esp32da`)
- Pins: DHT11 = 4, LED = 12, buzzer = 15, pump INA/INB = 18/19, soil = 33, water = 34, light = 35
- LCD I2C 0x27, SDA = 21, SCL = 22 (hd44780 library)
- Tracker LDRs: A1 = GPIO32, A2 = GPIO35, A3 = GPIO36, A4 = GPIO39 (3.3 V only)
- Pan servo = GPIO25, tilt servo = GPIO26 (external 6–12 V, common GND)
- USB serial: COM11 (CH340)

## Firmware

- `wifi/wifi.ino` — IoT version (Wi-Fi, MQTT, BLE provisioning, tracker)
- `wifi.ino` — same sketch at the project root
- `auto.ino` — local automatic watering only (no Wi-Fi)
- `lcd_test/lcd_test.ino` — LCD test sketch

Wi-Fi credentials in this repository are placeholders. Fill `defaultSsid` and `defaultPassword` on your own machine, or provision them over Bluetooth. Do not commit real passwords.

Compile and upload with partition `min_spiffs` (the default 1.2 MB app partition is too small):

```text
esp32:esp32:esp32da:PartitionScheme=min_spiffs
```

Upload to COM11. Relax the servo horns before the first upload.

## Wi-Fi and MQTT

The ESP32, the computer running Mosquitto, and the phone must be on the same 2.4 GHz network.

`MQTT_HOST` must be the computer's current Wi-Fi IPv4 address (not `127.0.0.1`, and not a phone IP). Update it after changing networks.

BLE device name: `INT4089-ESP32`. To push this computer's current Wi-Fi and MQTT host automatically:

```text
python tools/ble_sync_wifi.py
```

Or open the dashboard in computer Chrome at [http://127.0.0.1:8080/](http://127.0.0.1:8080/) and use Web Bluetooth. The browser cannot read the saved Wi-Fi password; type it in the page, or use the script.

Mosquitto listens on TCP 1883 (`allow_anonymous`) and WebSocket 9001.

MQTT topics:

- Subscribe: `pump`, `auto`, `tracker`, `pan_set`, `tilt_set`
- Publish: `wendu`, `shidu`, `light`, `soil`, `water`, `ldr_a1`–`ldr_a4`, `pan`, `tilt`, `tracker_state`

Automatic watering runs only after `auto_on`. Tracking runs only after `tracker_on`.

## Dashboard: connect to the ESP32

The dashboard does not talk to the ESP32 over USB. The web page connects to Mosquitto with WebSocket port **9001**, and the ESP32 connects to the same broker with MQTT port **1883**. All three must be on the same 2.4 GHz Wi-Fi.

1. Start the Mosquitto Broker service. Check that ports 1883 and 9001 are listening:
   ```text
   Get-Service mosquitto
   netstat -an | findstr ":1883 :9001"
   ```
2. Run `ipconfig` and copy the **Wireless LAN adapter Wi-Fi** IPv4 address (`PC_IP`). This is the broker host. Do not use `127.0.0.1` or a phone IP.
3. Set the ESP32 `MQTT_HOST` to that `PC_IP`. After changing networks, run `python tools/ble_sync_wifi.py`, or use Web Bluetooth in the dashboard. A re-upload is not required just to change the network.
4. Start the dashboard server (keep this window open, and run only one copy; two servers on 8080 cause `ERR_EMPTY_RESPONSE`):
   ```text
   cd dashboard
   python -m http.server 8080 --bind 0.0.0.0
   ```
5. On the computer, open Chrome at [http://127.0.0.1:8080/](http://127.0.0.1:8080/). On a phone, open `http://PC_IP:8080/` (same Wi-Fi).
6. In the page, set broker host to `PC_IP` and port `9001` (`ws://PC_IP:9001`). Do not set the broker to `127.0.0.1`. Connect, then sensor values should appear.

Labels: **T** = temperature °C, **H** = humidity %, **L** = light, **S** = soil %, **D** = water level.

Controls: pump (`pump_on` / `pump_off`), auto watering (`auto_on` / `auto_off`), tracker (`tracker_on` / `tracker_off`), and pan/tilt sliders (`pan_set` / `tilt_set`; GPIO25 = pan, GPIO26 = tilt). Auto watering and tracking run only when their modes are on.

Web Bluetooth works in computer Chrome at [http://127.0.0.1:8080/](http://127.0.0.1:8080/). The browser cannot read the saved Wi-Fi password; type it in the page, or use `ble_sync_wifi.py`. iPhone does not support Web Bluetooth. Windows Settings may show `INT4089-ESP32` as Not connected after a sync; that is normal once the GATT session has ended.

If MQTT shows disconnected, the broker host is usually a stale IP, or Mosquitto / firewall port 9001 is not open. If a phone cannot load the page, use `http://PC_IP:8080/` and `--bind 0.0.0.0`.

A dashboard screenshot is in `dashboard/dashboard-shot.png`. Presentation slides are in `INT4089_Group9_Plant_Watering.pptx`. Wiring notes are in `WIRING_接線說明.md`. The same steps in Chinese, plus phone MQTT App setup, are in `MQTT_App_Setup.md`.

## Not included

Course lecture PDFs, assignment worksheets, backup sketches, and build artifacts are kept out of this repository.
