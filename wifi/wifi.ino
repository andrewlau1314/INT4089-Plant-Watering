/*
 * INT4089 Auto Watering + MQTT (Mosquitto) + HS-E11 solar tracker
 * Phone + ESP32 + PC must be on the SAME WiFi (e.g. hotspot @@).
 * MQTT_HOST must be the PC IPv4 that runs Mosquitto (`ipconfig` Wi-Fi adapter).
 * Do not set MQTT_HOST to the phone IP — the phone is not the broker.
 * Broker: Eclipse Mosquitto on PC, port 1883.
 * Topics (existing): pump / auto (subscribe); wendu shidu light soil water (publish).
 * Topics (tracker):  tracker / pan_set / tilt_set (subscribe);
 *                    ldr_a1 ldr_a2 ldr_a3 ldr_a4 pan tilt (publish).
 * light is now overall brightness 0-100 (higher = brighter), from the 4 LDRs.
 *
 * Library: install "ESP32Servo" from Arduino Library Manager.
 * Compile/upload fqbn must be:
 *   esp32:esp32:esp32da:PartitionScheme=min_spiffs
 * (BLE + Wi-Fi does not fit the default 1.2MB app partition.)
 * Arduino IDE: Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS).
 * BLE Nordic UART, name INT4089-ESP32:
 *   GET or connect  -> ESP32_IP=..., MQTT_HOST=..., WIFI_SSID=...
 *   MQTT_HOST=x.x.x.x or a bare IPv4 -> save as broker IP in flash, reconnect MQTT
 *   PHONE_IP=x.x.x.x -> acknowledged only; does not change MQTT_HOST
 *   WIFI_SSID=... / WIFI_PASS=... -> save Wi-Fi in flash and join that network
 *     (password is never printed on Serial or BLE notify)
 * Default MQTT_HOST / Wi-Fi below are used if nothing is saved yet.
 */
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <DHT.h>
#include <SimpleTimer.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ===== EDIT THIS: PC IPv4 when on same WiFi as ESP32 =====
// 請改成你電腦而家正在用嘅 MQTT_HOST，唔好照抄舊 IP。
#define MQTT_HOST "192.168.0.40"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "esp32-watering"

// WiFi defaults (overridden by values saved via BLE, if any)
const char* defaultSsid = "@@";
const char* defaultPassword = "YOUR_WIFI_PASSWORD";
char ssidBuf[33];
char passBuf[65];
char mqttHostBuf[48] = MQTT_HOST;
const char* mqtt_server = mqttHostBuf;
const int mqtt_port = MQTT_PORT;

// BLE Nordic UART: write MQTT_HOST=PC_IP (broker). WIFI_SSID / WIFI_PASS join that network.
#define BLE_NAME               "INT4089-ESP32"
#define BLE_NUS_SERVICE_UUID   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_NUS_RX_UUID        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_NUS_TX_UUID        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
Preferences prefs;
BLECharacteristic *bleTxChar = nullptr;
volatile bool mqttHostChanged = false;
volatile bool wifiCredsChanged = false;
volatile bool bleNeedHello = false;
volatile unsigned long bleHelloAt = 0;
String bleRxBuf;

// Existing watering pins
#define   dht11Pin      4
#define   ledPin       12
#define   buzzerPin    15
#define   INAPin       18
#define   INBPin       19
#define   soilPin      33
#define   waterPin     34

// HS-S06B four-LDR (official: A1 bottom-left, A2 top-left, A3 bottom-right, A4 top-right)
// ADC1 only so Wi-Fi stays reliable. GPIO35 used to be the single LDR.
#define   ldrA1Pin     32   // 左下
#define   ldrA2Pin     35   // 左上
#define   ldrA3Pin     36   // 右下  (VP)
#define   ldrA4Pin     39   // 右上  (VN)

// Servos: signal only. Power from external 6-12V supply, common GND with ESP32.
#define   panPin       25   // 底盤／水平
#define   tiltPin      26   // 面板／垂直

// Tracker tuning. If a servo moves the wrong way, set the matching REVERSE to 1.
#define   REVERSE_PAN       0
#define   REVERSE_TILT      0
#define   TRACK_DEADZONE    80    // 12-bit ADC; raise if it jitters
#define   TRACK_DARK        3200  // all LDRs darker than this → stop hunting
#define   TRACK_INTERVAL_MS 50
#define   PAN_MIN           15
#define   PAN_MAX           165
#define   TILT_MIN          25
#define   TILT_MAX          155
#define   SERVO_INIT        90

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(dht11Pin, 11);
hd44780_I2Cexp lcd;
SimpleTimer timer;
Servo panServo;
Servo tiltServo;

boolean state = false;          // watering auto
boolean trackerState = false;   // solar tracker off until dashboard/MQTT tracker_on
int Light;
float Water,data,Humi,Temp,Soil,i,j;

int ldrA1, ldrA2, ldrA3, ldrA4;
int panAngle = SERVO_INIT;
int tiltAngle = SERVO_INIT;
int lastPanWritten = -1;
int lastTiltWritten = -1;
unsigned long lastTrackMs = 0;
unsigned long lastLcdMs = 0;
unsigned long panHoldUntil = 0;
unsigned long tiltHoldUntil = 0;
const unsigned long SERVO_HOLD_MS = 800;

bool setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssidBuf);
  lcd.setCursor(0,0);
  lcd.print("WiFi connecting");
  lcd.setCursor(0,1);
  lcd.print(ssidBuf);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  // Do not WiFi.setSleep(false): it breaks Wi-Fi + BLE coexistence (abort in coex_core_enable).
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(ssidBuf, passBuf);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println();
      Serial.println("WiFi FAILED (timeout)");
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("WiFi FAILED");
      lcd.setCursor(0,1);
      lcd.print("check SSID/PWD");
      return false;
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WiFi OK");
  lcd.setCursor(0,1);
  lcd.print(WiFi.localIP());
  delay(800);
  return true;
}


bool isValidIpv4(const String& s) {
  int a, b, c, d;
  char extra;
  if (s.length() < 7 || s.length() > 15) return false;
  if (sscanf(s.c_str(), "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) != 4) return false;
  return a >= 0 && a <= 255 && b >= 0 && b <= 255 && c >= 0 && c <= 255 && d >= 0 && d <= 255;
}

bool isUsableBrokerIp(const String& s) {
  if (!isValidIpv4(s)) return false;
  if (s == "127.0.0.1" || s == "0.0.0.0" || s == "255.255.255.255") return false;
  return true;
}

String currentEspIp() {
  return (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "0.0.0.0";
}

void bleNotify(const String& msg);

void bleNotifyIps() {
  bleNotify(String("ESP32_IP=") + currentEspIp() + "\n");
  bleNotify(String("MQTT_HOST=") + mqttHostBuf + "\n");
  bleNotify(String("WIFI_SSID=") + ssidBuf + "\n");
}

void loadPrefs() {
  prefs.begin("int4089", false);
  String savedHost = prefs.getString("mqtt_host", "");
  if (isUsableBrokerIp(savedHost)) {
    strncpy(mqttHostBuf, savedHost.c_str(), sizeof(mqttHostBuf) - 1);
    mqttHostBuf[sizeof(mqttHostBuf) - 1] = '\0';
  }
  String savedSsid = prefs.getString("wifi_ssid", "");
  String savedPass = prefs.getString("wifi_pass", "");
  if (savedSsid.length() > 0 && savedSsid.length() < sizeof(ssidBuf)) {
    strncpy(ssidBuf, savedSsid.c_str(), sizeof(ssidBuf) - 1);
    ssidBuf[sizeof(ssidBuf) - 1] = '\0';
    if (savedPass.length() >= sizeof(passBuf)) savedPass = savedPass.substring(0, sizeof(passBuf) - 1);
    strncpy(passBuf, savedPass.c_str(), sizeof(passBuf) - 1);
    passBuf[sizeof(passBuf) - 1] = '\0';
  } else {
    strncpy(ssidBuf, defaultSsid, sizeof(ssidBuf) - 1);
    ssidBuf[sizeof(ssidBuf) - 1] = '\0';
    strncpy(passBuf, defaultPassword, sizeof(passBuf) - 1);
    passBuf[sizeof(passBuf) - 1] = '\0';
  }
}

void bleNotify(const String& msg) {
  Serial.print(msg);
  if (!msg.endsWith("\n")) Serial.println();
  if (!bleTxChar) return;
  bleTxChar->setValue((uint8_t*)msg.c_str(), msg.length());
  bleTxChar->notify();
}

void applyMqttHost(String ip) {
  ip.trim();
  ip.replace("\r", "");
  ip.replace("\n", "");
  ip.trim();
  if (!isUsableBrokerIp(ip)) {
    bleNotify(String("ERR invalid MQTT_HOST: ") + ip + "\n");
    bleNotifyIps();
    return;
  }
  if (String(mqttHostBuf) == ip) {
    bleNotify(String("OK MQTT_HOST=") + mqttHostBuf + "\n");
    bleNotify(String("ESP32_IP=") + currentEspIp() + "\n");
    return;
  }
  strncpy(mqttHostBuf, ip.c_str(), sizeof(mqttHostBuf) - 1);
  mqttHostBuf[sizeof(mqttHostBuf) - 1] = '\0';
  prefs.putString("mqtt_host", mqttHostBuf);
  mqttHostChanged = true;
  bleNotify(String("OK MQTT_HOST=") + mqttHostBuf + "\n");
  bleNotify(String("ESP32_IP=") + currentEspIp() + "\n");
}

void handleBleLine(String line) {
  line.replace("\r", "");
  line.replace("\n", "");
  String trimmed = line;
  trimmed.trim();
  if (!trimmed.length()) return;

  String upper = trimmed;
  upper.toUpperCase();
  if (upper == "GET" || upper == "?" || upper == "IP?" || upper == "IP") {
    bleNotifyIps();
    return;
  }
  if (upper.startsWith("PHONE_IP=")) {
    String phoneIp = line.substring(line.indexOf('=') + 1);
    phoneIp.trim();
    if (phoneIp.length() && !isValidIpv4(phoneIp)) {
      bleNotify(String("ERR invalid PHONE_IP: ") + phoneIp + "\n");
    } else {
      bleNotify(String("OK PHONE_IP=") + phoneIp + "\n");
    }
    // Phone IP is for display / pairing only. Mosquitto runs on the PC.
    bleNotifyIps();
    return;
  }
  if (upper.startsWith("MQTT_HOST=")) {
    applyMqttHost(line.substring(line.indexOf('=') + 1));
    return;
  }
  if (isValidIpv4(line)) {
    applyMqttHost(line);
    return;
  }
  if (upper.startsWith("WIFI_SSID=")) {
    String val = line.substring(line.indexOf('=') + 1);
    val.trim();
    if (val.length() == 0 || val.length() >= (int)sizeof(ssidBuf)) {
      bleNotify("ERR invalid WIFI_SSID\n");
      return;
    }
    strncpy(ssidBuf, val.c_str(), sizeof(ssidBuf) - 1);
    ssidBuf[sizeof(ssidBuf) - 1] = '\0';
    prefs.putString("wifi_ssid", ssidBuf);
    bleNotify(String("OK WIFI_SSID=") + ssidBuf + "\n");
    return;
  }
  if (upper.startsWith("WIFI_PASS=") || upper.startsWith("WIFI_PW=")) {
    String val = line.substring(line.indexOf('=') + 1);
    // keep leading/trailing spaces in password; only strip CR
    val.replace("\r", "");
    if (val.length() >= (int)sizeof(passBuf)) {
      bleNotify("ERR invalid WIFI_PASS\n");
      return;
    }
    strncpy(passBuf, val.c_str(), sizeof(passBuf) - 1);
    passBuf[sizeof(passBuf) - 1] = '\0';
    prefs.putString("wifi_pass", passBuf);
    bleNotify("OK WIFI_PASS=****\n");
    if (strlen(ssidBuf) > 0) {
      wifiCredsChanged = true;
    }
    return;
  }
  if (upper == "WIFI_APPLY" || upper == "WIFI_CONNECT") {
    if (strlen(ssidBuf) == 0) {
      bleNotify("ERR missing WIFI_SSID\n");
      return;
    }
    wifiCredsChanged = true;
    bleNotify(String("OK WIFI_APPLY SSID=") + ssidBuf + "\n");
    return;
  }
  if (upper.startsWith("WIFI_PASS") || upper.startsWith("WIFI_PW")) {
    bleNotify("ERR invalid WIFI_PASS\n");
    return;
  }
  bleNotify("ERR unknown command\n");
}

class BleRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    String v = String(c->getValue().c_str());
    if (!v.length()) return;
    bleRxBuf += v;
    int n;
    while ((n = bleRxBuf.indexOf('\n')) >= 0 || (n = bleRxBuf.indexOf('\r')) >= 0) {
      String line = bleRxBuf.substring(0, n);
      bleRxBuf = bleRxBuf.substring(n + 1);
      line.trim();
      if (line.length()) handleBleLine(line);
    }
    String bare = bleRxBuf;
    bare.trim();
    String bareUpper = bare;
    bareUpper.toUpperCase();
    if (isValidIpv4(bare) || bareUpper.startsWith("PHONE_IP=") || bareUpper.startsWith("MQTT_HOST=") ||
        bareUpper.startsWith("WIFI_SSID=") || bareUpper.startsWith("WIFI_PASS=") || bareUpper.startsWith("WIFI_PW=") ||
        bareUpper == "GET" || bareUpper == "?" || bareUpper == "IP?" || bareUpper == "IP" ||
        bareUpper == "WIFI_APPLY" || bareUpper == "WIFI_CONNECT") {
      handleBleLine(bare);
      bleRxBuf = "";
    }
    if (bleRxBuf.length() > 200) bleRxBuf = "";
  }
};

class BleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s) {
    Serial.println("BLE phone connected");
    // Notify after the phone has subscribed to TX; immediate notify is often dropped.
    bleNeedHello = true;
    bleHelloAt = millis() + 400;
  }
  void onDisconnect(BLEServer *s) {
    Serial.println("BLE phone disconnected");
    bleNeedHello = false;
    BLEDevice::startAdvertising();
  }
};

void setupBle() {
  BLEDevice::init(BLE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new BleServerCallbacks());
  BLEService *svc = server->createService(BLE_NUS_SERVICE_UUID);
  bleTxChar = svc->createCharacteristic(BLE_NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  bleTxChar->addDescriptor(new BLE2902());
  BLECharacteristic *rx = svc->createCharacteristic(BLE_NUS_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new BleRxCallbacks());
  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_NUS_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising as INT4089-ESP32");
}

void reconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("MQTT skip: no WiFi");
    return;
  }
  int tries = 0;
  while (!client.connected() && tries < 5) {
    Serial.print("Attempting MQTT ");
    Serial.print(mqtt_server);
    Serial.print(":");
    Serial.print(mqtt_port);
    Serial.print(" ...");
    if (client.connect(MQTT_CLIENT_ID)) {
      Serial.println(" connected OK");
      client.subscribe("pump");
      client.subscribe("auto");
      client.subscribe("tracker");
      client.subscribe("pan_set");
      client.subscribe("tilt_set");
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("MQTT connected");
      delay(500);
    } else {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      Serial.println(" retry in 1s");
      tries++;
      delay(1000);
    }
  }
  if (!client.connected()) {
    Serial.println("MQTT failed (give up this round)");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("MQTT failed");
  }
}

void ensurePanAttached() {
  if (panServo.attached()) return;
  panServo.setPeriodHertz(50);
  panServo.setTimerWidth(16);
  int ch = panServo.attach(panPin, 500, 2500);
  lastPanWritten = -1;
  Serial.print("re-attach pan ch=");
  Serial.println(ch);
}

void ensureTiltAttached() {
  if (tiltServo.attached()) return;
  tiltServo.setPeriodHertz(50);
  tiltServo.setTimerWidth(16);
  int ch = tiltServo.attach(tiltPin, 500, 2500);
  lastTiltWritten = -1;
  Serial.print("re-attach tilt ch=");
  Serial.println(ch);
}

void writePan(bool force = false) {
  panAngle = constrain(panAngle, PAN_MIN, PAN_MAX);
  if (!force && panServo.attached() && panAngle == lastPanWritten) return;
  ensurePanAttached();
  panServo.write(panAngle);
  lastPanWritten = panAngle;
  panHoldUntil = trackerState ? 0 : (millis() + SERVO_HOLD_MS);
}

void writeTilt(bool force = false) {
  tiltAngle = constrain(tiltAngle, TILT_MIN, TILT_MAX);
  if (!force && tiltServo.attached() && tiltAngle == lastTiltWritten) return;
  ensureTiltAttached();
  tiltServo.write(tiltAngle);
  lastTiltWritten = tiltAngle;
  tiltHoldUntil = trackerState ? 0 : (millis() + SERVO_HOLD_MS);
}

void writeServos(bool force = false) {
  writePan(force);
  writeTilt(force);
}

void releaseIdleServos() {
  if (trackerState) return;
  if (panHoldUntil && millis() >= panHoldUntil && panServo.attached()) {
    panServo.detach();
    panHoldUntil = 0;
    Serial.println("detach pan (idle, stop jitter)");
  }
  if (tiltHoldUntil && millis() >= tiltHoldUntil && tiltServo.attached()) {
    tiltServo.detach();
    tiltHoldUntil = 0;
    Serial.println("detach tilt (idle, stop jitter)");
  }
}

void publishTrackerState() {
  if (!client.connected()) return;
  client.publish("tracker_state", trackerState ? "on" : "off");
}

void callback(char* rtopic, byte* payload, unsigned int length) {
  String msg = "";
  String topic = rtopic;
  for (unsigned int i = 0; i < length; i++) {
    char c = (char)payload[i];
    if (c == '\r' || c == '\n') continue;
    msg += c;
  }
  msg.trim();
  if (topic == "pump") {
    if (msg == "pump_on") {
      analogWrite(INAPin, 100);
      analogWrite(INBPin, 255);
    } else if (msg == "pump_off") {
      analogWrite(INAPin, 255);
      analogWrite(INBPin, 255);
    }
  }
  else if (topic == "auto") {
    if (msg == "auto_on") {
     state = true;
    }
    else if (msg == "auto_off"){
      analogWrite(INAPin, 255);
      analogWrite(INBPin, 255);
      digitalWrite(ledPin,LOW);
      digitalWrite(buzzerPin,LOW);
      state = false;
    }
  }
  else if (topic == "tracker") {
    if (msg == "tracker_on") {
      trackerState = true;
      writeServos(true);
    } else if (msg == "tracker_off") {
      trackerState = false;
      writeServos(true);
    }
    Serial.print("MQTT tracker -> ");
    Serial.println(trackerState ? "on" : "off");
    publishTrackerState();
  }
  else if (topic == "pan_set") {
    trackerState = false;
    panAngle = msg.toInt();
    Serial.print("MQTT pan_set -> ");
    Serial.println(panAngle);
    writePan();
    publishTrackerState();
  }
  else if (topic == "tilt_set") {
    trackerState = false;
    tiltAngle = msg.toInt();
    Serial.print("MQTT tilt_set -> ");
    Serial.println(tiltAngle);
    writeTilt();
    publishTrackerState();
  }
}

void publish_sensor_data() {
  if (!client.connected()) return;

  if (isnan(Temp) || isnan(Humi)) {
    Serial.println("Failed to read from DHT sensor!");
  } else {
    client.publish("wendu", String(Temp).c_str());
    client.publish("shidu", String(Humi).c_str());
  }
  client.publish("light", String(Light).c_str());
  client.publish("soil", String(j).c_str());
  client.publish("water", String(data).c_str());
  client.publish("ldr_a1", String(ldrA1).c_str());
  client.publish("ldr_a2", String(ldrA2).c_str());
  client.publish("ldr_a3", String(ldrA3).c_str());
  client.publish("ldr_a4", String(ldrA4).c_str());
  client.publish("pan", String(panAngle).c_str());
  client.publish("tilt", String(tiltAngle).c_str());
}

void IO_Init() {
  pinMode(waterPin, INPUT);
  pinMode(soilPin, INPUT);
  pinMode(ldrA1Pin, INPUT);
  pinMode(ldrA2Pin, INPUT);
  pinMode(ldrA3Pin, INPUT);
  pinMode(ldrA4Pin, INPUT);
  analogSetPinAttenuation(ldrA1Pin, ADC_11db);
  analogSetPinAttenuation(ldrA2Pin, ADC_11db);
  analogSetPinAttenuation(ldrA3Pin, ADC_11db);
  analogSetPinAttenuation(ldrA4Pin, ADC_11db);
  analogReadResolution(12);
  pinMode(ledPin,OUTPUT);
  pinMode(buzzerPin,OUTPUT);
  pinMode(INAPin,OUTPUT);
  pinMode(INBPin,OUTPUT);
  digitalWrite(ledPin,LOW);
  digitalWrite(buzzerPin,LOW);
  // Attach servos before analogWrite so pump PWM does not steal LEDC channels.
  // Use timers 2/3 only; 0/1 are more likely to clash with Wi-Fi.
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  panServo.setPeriodHertz(50);
  tiltServo.setPeriodHertz(50);
  panServo.setTimerWidth(16);
  tiltServo.setTimerWidth(16);
  int panCh = panServo.attach(panPin, 500, 2500);
  int tiltCh = tiltServo.attach(tiltPin, 500, 2500);
  Serial.print("Servo pan GPIO");
  Serial.print(panPin);
  Serial.print(" attach=");
  Serial.println(panCh);
  Serial.print("Servo tilt GPIO");
  Serial.print(tiltPin);
  Serial.print(" attach=");
  Serial.println(tiltCh);
  panAngle = SERVO_INIT;
  tiltAngle = SERVO_INIT;
  lastPanWritten = -1;
  lastTiltWritten = -1;
  writeServos(true);

  analogWrite(INAPin,255);
  analogWrite(INBPin,255);
}

void scanI2C() {
  Serial.println("I2C scan...");
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      count++;
    }
  }
  if (count == 0) Serial.println("No I2C devices found");
}

void Lcd()
{
  int status = lcd.begin(16, 2);
  if (status) {
    Serial.print("LCD begin failed, status=");
    Serial.println(status);
    hd44780::fatalError(status);
  }
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("LCD OK hd44780");
  lcd.setCursor(0,1);
  lcd.print("Water+Tracker");
  delay(1200);
  lcd.clear();
}

void Display()
{
  lcd.setCursor(0,0);
  lcd.print("T:");
  lcd.print(Temp);
  lcd.setCursor(8,0);
  lcd.print("H:");
  lcd.print(Humi);
  lcd.setCursor(0,1);
  lcd.print("L:");
  lcd.print(Light);
  lcd.print("%  ");
  lcd.setCursor(8,1);
  lcd.print("S:");
  lcd.print(j,1);
  lcd.setCursor(15,1);
  lcd.print("%");
}

void readLdrs() {
  ldrA1 = analogRead(ldrA1Pin); // 左下
  ldrA2 = analogRead(ldrA2Pin); // 左上
  ldrA3 = analogRead(ldrA3Pin); // 右下
  ldrA4 = analogRead(ldrA4Pin); // 右上
  int avg = (ldrA1 + ldrA2 + ldrA3 + ldrA4) / 4;
  // HS-S06B: smaller raw value = brighter. Publish 0-100 brightness.
  Light = map(4095 - constrain(avg, 0, 4095), 0, 4095, 0, 100);
}

void updateTracker() {
  readLdrs();
  if (!trackerState) return;

  int avgAll = (ldrA1 + ldrA2 + ldrA3 + ldrA4) / 4;
  if (avgAll > TRACK_DARK) return; // too dark, don't hunt

  int avgL = (ldrA1 + ldrA2) / 2; // 左
  int avgR = (ldrA3 + ldrA4) / 2; // 右
  int avgT = (ldrA2 + ldrA4) / 2; // 上
  int avgB = (ldrA1 + ldrA3) / 2; // 下

  // Move toward the brighter (smaller) side.
  if (abs(avgL - avgR) > TRACK_DEADZONE) {
    int dir = (avgL < avgR) ? 1 : -1; // left brighter → increase pan
    if (REVERSE_PAN) dir = -dir;
    panAngle += dir;
  }
  if (abs(avgT - avgB) > TRACK_DEADZONE) {
    int dir = (avgT < avgB) ? 1 : -1; // top brighter → increase tilt
    if (REVERSE_TILT) dir = -dir;
    tiltAngle += dir;
  }
  writeServos();  // skips unchanged axis
}

void light(){
   // Light is now 0-100 brightness. Turn fill LED on when dark.
   if(Light > 20)
   {
   digitalWrite(ledPin,LOW);
   }
   else
   {
   digitalWrite(ledPin,HIGH);
   }
}

void soil(){
    if (j < 30){
      if (data > 2){
        analogWrite(INAPin,0);
        analogWrite(INBPin,255);
        digitalWrite(buzzerPin,LOW);
      }
      else{
        analogWrite(INAPin,255);
        analogWrite(INBPin,255);
        digitalWrite(buzzerPin,HIGH);
      }
    }
    else if(j >= 30)
    {
      if (data > 2){
        analogWrite(INAPin,255);
        analogWrite(INBPin,255);
        digitalWrite(buzzerPin,LOW);
     }
       else {
        analogWrite(INAPin,255);
        analogWrite(INBPin,255);
        digitalWrite(buzzerPin,HIGH);
      }
     }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  loadPrefs();
  Serial.println("Boot (Mosquitto MQTT + tracker + BLE)");
  Serial.print("WIFI_SSID=");
  Serial.println(ssidBuf);
  Serial.print("MQTT_HOST=");
  Serial.println(mqtt_server);
  Serial.print("MQTT_PORT=");
  Serial.println(mqtt_port);

  Wire.begin(21, 22);
  scanI2C();

  Lcd();

  IO_Init();  // centre servos at 90° before Wi-Fi (safer mechanically)

  bool wifiOk = setup_wifi();
  setupBle();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  if (wifiOk) {
    reconnect();
  } else {
    Serial.println("Skip MQTT until WiFi works");
  }
  dht.begin();
  timer.setInterval(500L, publish_sensor_data);
}

void loop() {
  Temp = dht.readTemperature();
  Humi = dht.readHumidity();
  Soil = analogRead(soilPin);
  i = Soil/4095;
  j = (1-i) * 100;
  Water = analogRead(waterPin);
  data = Water/2000*4;

  if (millis() - lastTrackMs >= TRACK_INTERVAL_MS) {
    lastTrackMs = millis();
    updateTracker();
  } else if (!trackerState) {
    readLdrs();
  }
  releaseIdleServos();

  if (bleNeedHello && millis() >= bleHelloAt) {
    bleNeedHello = false;
    bleNotifyIps();
  }
  if (wifiCredsChanged) {
    wifiCredsChanged = false;
    Serial.print("WiFi creds updated via BLE, joining ");
    Serial.println(ssidBuf);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("BLE WiFi join");
    lcd.setCursor(0, 1);
    lcd.print(ssidBuf);
    if (client.connected()) client.disconnect();
    bool wifiOk = setup_wifi();
    if (wifiOk) {
      client.setServer(mqtt_server, mqtt_port);
      reconnect();
      bleNotify(String("OK WIFI_JOIN=") + ssidBuf + "\n");
    } else {
      bleNotify(String("ERR WIFI_JOIN=") + ssidBuf + "\n");
    }
    bleNotifyIps();
  }
  if (mqttHostChanged) {
    mqttHostChanged = false;
    Serial.print("MQTT_HOST updated via BLE: ");
    Serial.println(mqtt_server);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("BLE IP paired");
    lcd.setCursor(0, 1);
    lcd.print(mqtt_server);
    client.setServer(mqtt_server, mqtt_port);
    if (client.connected()) client.disconnect();
    delay(200);
  }
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  timer.run();

  if (millis() - lastLcdMs >= 500) {
    lastLcdMs = millis();
    Display();
  }

  if (state){
    light();
    soil();
  }
}
