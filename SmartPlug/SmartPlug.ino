/*
 * Smart Plug - ESP32 Firmware
 *
 * Features:
 * - WiFi connection with auto-reconnect
 * - Socket.IO connection to backend
 * - Device authentication via QR code
 * - Remote toggle commands (ON/OFF)
 * - Heartbeat to maintain connection
 * - Energy telemetry reporting
 *
 * Required Libraries (install via Arduino Library Manager):
 * - WebSockets by Markus Sattler (includes SocketIOclient)
 * - ArduinoJson by Benoit Blanchon
 *
 * Setup:
 * 1. Update the configuration section below with your settings
 * 2. Upload to ESP32
 */

#include <WiFi.h>
#include <SocketIOclient.h>
#include <ArduinoJson.h>

// ============================================
// CONFIGURATION - UPDATE THESE VALUES
// ============================================

// WiFi Settings
#define WIFI_SSID          "YOUR_WIFI_SSID"
#define WIFI_PASSWORD      "YOUR_WIFI_PASSWORD"

// Server Settings
#define SERVER_HOST        "smart-plug-backend-b4c0.onrender.com"
#define SERVER_PORT        443

// Device Settings - QR code must match device registered in dashboard
#define DEVICE_QR_CODE     "YOUR_DEVICE_QR_CODE"

// Pin Configuration
#define LED_PIN            2    // Built-in LED (connection status)
#define RELAY_PIN          25   // Relay control GPIO (D25, active LOW)
#define VOLTAGE_SENSOR_PIN 33   // Voltage sensor analog input
#define CURRENT_SENSOR_PIN 34   // ACS712 current sensor analog input

// Sensor Calibration
#define ACS712_SENSITIVITY   0.185   // V/A for 5A module (use 0.100 for 20A, 0.066 for 30A)
#define ADC_VREF             3.3     // ESP32 ADC reference voltage
#define ADC_RESOLUTION       4095.0  // 12-bit ADC
#define VOLTAGE_SCALE_FACTOR 253.0   // Calibrated: 38.3V reading -> 220V display
#define SENSOR_SAMPLES       50      // Samples for averaging
#define CURRENT_THRESHOLD    0.1     // Ignore current below this (noise filter)

// Timing Configuration
#define DEMO_MODE          true  // true = 30s telemetry, false = 1h telemetry
#define HEARTBEAT_INTERVAL_MS       25000      // 25 seconds
#define TELEMETRY_INTERVAL_DEMO_MS  30000      // 30 seconds (demo)
#define TELEMETRY_INTERVAL_PROD_MS  3600000    // 1 hour (production)
#define WIFI_RECONNECT_INTERVAL_MS  10000      // 10 seconds

// ============================================
// GLOBALS
// ============================================
SocketIOclient socketIO;

bool isAuthenticated = false;
bool relayState = false;

unsigned long lastHeartbeatTime = 0;
unsigned long lastTelemetryTime = 0;
unsigned long lastWiFiCheckTime = 0;

float sessionEnergyKWh = 0.0;
float acs712ZeroPoint = 0.0;  // Calibrated at startup

const unsigned long TELEMETRY_INTERVAL_MS = DEMO_MODE ? TELEMETRY_INTERVAL_DEMO_MS : TELEMETRY_INTERVAL_PROD_MS;

// ============================================
// SENSOR READING
// ============================================
float readADCAverage(int pin) {
  long total = 0;
  for (int i = 0; i < SENSOR_SAMPLES; i++) {
    total += analogRead(pin);
    delayMicroseconds(100);
  }
  return (float)total / SENSOR_SAMPLES;
}

float readVoltage() {
  float adcValue = readADCAverage(VOLTAGE_SENSOR_PIN);
  float sensorVoltage = (adcValue / ADC_RESOLUTION) * ADC_VREF;
  // Scale 5V DC to 220V AC equivalent
  float scaledVoltage = sensorVoltage * VOLTAGE_SCALE_FACTOR;
  return scaledVoltage;
}

float readCurrent() {
  float adcValue = readADCAverage(CURRENT_SENSOR_PIN);
  float sensorVoltage = (adcValue / ADC_RESOLUTION) * ADC_VREF;
  // Calculate current using calibrated zero point
  float current = (sensorVoltage - acs712ZeroPoint) / ACS712_SENSITIVITY;
  current = abs(current);
  // Filter noise
  if (current < CURRENT_THRESHOLD) {
    current = 0.0;
  }
  return current;
}

void calibrateCurrentSensor() {
  Serial.println("[CAL] Calibrating current sensor (keep relay OFF)...");
  delay(500);
  float adcValue = readADCAverage(CURRENT_SENSOR_PIN);
  acs712ZeroPoint = (adcValue / ADC_RESOLUTION) * ADC_VREF;
  Serial.print("[CAL] Zero point: ");
  Serial.print(acs712ZeroPoint, 3);
  Serial.println("V");
}

float readPower() {
  float voltage = readVoltage();
  float current = readCurrent();
  float power = voltage * current;
  Serial.print("[SENSOR] V=");
  Serial.print(voltage, 1);
  Serial.print("V I=");
  Serial.print(current, 3);
  Serial.print("A P=");
  Serial.print(power, 1);
  Serial.println("W");
  return power;
}

// ============================================
// RELAY CONTROL
// ============================================
void setRelay(bool state) {
  relayState = state;
  digitalWrite(RELAY_PIN, state ? HIGH : LOW);  // Normal logic: ON=HIGH, OFF=LOW
  digitalWrite(LED_PIN, state ? HIGH : LOW);    // LED mirrors relay state
  Serial.print("[RELAY] ");
  Serial.println(state ? "ON" : "OFF");
}

// ============================================
// ENERGY CALCULATION
// ============================================
float calculateEnergy(unsigned long deltaMs) {
  if (!relayState) {
    return 0.0;
  }

  float power = readPower();
  float hours = deltaMs / 3600000.0;
  return (power * hours) / 1000.0;  // kWh
}

// ============================================
// SOCKET.IO COMMUNICATION
// ============================================
void sendAuthentication() {
  Serial.println("[AUTH] Authenticating...");

  String payload = "[\"device:authenticate\",{\"qrCode\":\"";
  payload += DEVICE_QR_CODE;
  payload += "\"}]";

  socketIO.sendEVENT(payload);
}

void sendHeartbeat() {
  if (!isAuthenticated) return;

  StaticJsonDocument<128> doc;
  JsonArray arr = doc.to<JsonArray>();
  arr.add("device:heartbeat");

  JsonObject data = arr.createNestedObject();
  data["timestamp"] = millis();

  String payload;
  serializeJson(doc, payload);
  socketIO.sendEVENT(payload);

  Serial.println("[HEARTBEAT] Sent");
}

void sendTelemetry(float energyKWh) {
  if (!isAuthenticated) return;

  StaticJsonDocument<128> doc;
  JsonArray arr = doc.to<JsonArray>();
  arr.add("device:telemetry");

  JsonObject data = arr.createNestedObject();
  data["energy"] = energyKWh;

  String payload;
  serializeJson(doc, payload);
  socketIO.sendEVENT(payload);

  Serial.print("[TELEMETRY] Sent: ");
  Serial.print(energyKWh, 6);
  Serial.println(" kWh");
}

void sendCommandResponse(const char* commandId, bool success, const char* powerState) {
  StaticJsonDocument<256> doc;
  JsonArray arr = doc.to<JsonArray>();
  arr.add("command:response");

  JsonObject data = arr.createNestedObject();
  data["commandId"] = commandId;
  data["success"] = success;
  data["powerState"] = powerState;

  String payload;
  serializeJson(doc, payload);
  socketIO.sendEVENT(payload);
}

// ============================================
// EVENT HANDLERS
// ============================================
void handleEvent(const char* payload) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("[ERROR] JSON parse failed: ");
    Serial.println(error.c_str());
    return;
  }

  const char* eventName = doc[0];
  JsonObject data = doc[1];

  // --- AUTH SUCCESS ---
  if (strcmp(eventName, "auth:success") == 0) {
    const char* deviceId = data["deviceId"];
    const char* name = data["name"];
    const char* powerState = data["powerState"];

    Serial.println("========================================");
    Serial.println("[AUTH] SUCCESS!");
    Serial.print("[AUTH] Device: ");
    Serial.println(name);
    Serial.print("[AUTH] ID: ");
    Serial.println(deviceId);
    Serial.print("[AUTH] Power State: ");
    Serial.println(powerState);
    Serial.println("========================================");

    isAuthenticated = true;
    digitalWrite(LED_PIN, HIGH);

    setRelay(strcmp(powerState, "ON") == 0);

    lastHeartbeatTime = millis();
    lastTelemetryTime = millis();
    return;
  }

  // --- AUTH ERROR ---
  if (strcmp(eventName, "auth:error") == 0) {
    const char* message = data["message"];
    Serial.print("[AUTH] FAILED: ");
    Serial.println(message);
    isAuthenticated = false;
    return;
  }

  // --- TOGGLE COMMAND ---
  if (strcmp(eventName, "command:toggle") == 0) {
    const char* commandId = data["commandId"];
    const char* powerState = data["powerState"];

    Serial.println("========================================");
    Serial.print("[CMD] TOGGLE -> ");
    Serial.println(powerState);
    Serial.println("========================================");

    setRelay(strcmp(powerState, "ON") == 0);
    sendCommandResponse(commandId, true, powerState);
    return;
  }

  // --- HEARTBEAT ACK ---
  if (strcmp(eventName, "heartbeat:ack") == 0) {
    Serial.println("[HEARTBEAT] ACK received");
    return;
  }

  // --- DEVICE REMOVED ---
  if (strcmp(eventName, "device:removed") == 0) {
    Serial.println("[WARN] Device removed from account!");
    isAuthenticated = false;
    setRelay(false);
    digitalWrite(LED_PIN, LOW);
    return;
  }
}

void socketIOEvent(socketIOmessageType_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case sIOtype_DISCONNECT:
      Serial.println("[SIO] Disconnected");
      isAuthenticated = false;
      digitalWrite(LED_PIN, LOW);
      break;

    case sIOtype_CONNECT:
      Serial.print("[SIO] Connected: ");
      Serial.println((char*)payload);
      delay(100);
      sendAuthentication();
      break;

    case sIOtype_EVENT:
      handleEvent((char*)payload);
      break;

    case sIOtype_ACK:
    case sIOtype_ERROR:
    case sIOtype_BINARY_EVENT:
    case sIOtype_BINARY_ACK:
      break;
  }
}

// ============================================
// WIFI MANAGEMENT
// ============================================
void connectWiFi() {
  Serial.print("[WiFi] Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("[WiFi] Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("[WiFi] Connection failed!");
  }
}

void checkWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Connection lost, reconnecting...");
    connectWiFi();
  }
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("       Smart Plug - ESP32");
  Serial.println("========================================");
  Serial.print("Mode: ");
  Serial.println(DEMO_MODE ? "DEMO (30s telemetry)" : "PRODUCTION (1h telemetry)");
  Serial.println("Sensors: ACS712 + Voltage (5V->220V)");
  Serial.println("========================================");

  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW);   // Start OFF (normal logic: LOW=OFF)

  // Calibrate current sensor while relay is OFF (no current flowing)
  calibrateCurrentSensor();

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[SIO] Connecting to ");
    Serial.println(SERVER_HOST);

    socketIO.beginSSL(SERVER_HOST, SERVER_PORT, "/socket.io/?EIO=3transport=websocket");
    socketIO.onEvent(socketIOEvent);
    socketIO.setReconnectInterval(10000);
  }
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  unsigned long now = millis();

  if (now - lastWiFiCheckTime >= WIFI_RECONNECT_INTERVAL_MS) {
    checkWiFi();
    lastWiFiCheckTime = now;
  }

  socketIO.loop();

  if (isAuthenticated) {
    // Heartbeat
    if (now - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS) {
      sendHeartbeat();
      lastHeartbeatTime = now;
    }

    // Telemetry
    if (now - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
      unsigned long deltaMs = now - lastTelemetryTime;
      float energyKWh = calculateEnergy(deltaMs);

      if (energyKWh > 0) {
        sessionEnergyKWh += energyKWh;
        sendTelemetry(energyKWh);

        Serial.print("[STATS] Session total: ");
        Serial.print(sessionEnergyKWh, 4);
        Serial.println(" kWh");
      } else {
        Serial.println("[TELEMETRY] Relay OFF - skipping");
      }

      lastTelemetryTime = now;
    }
  }
}
