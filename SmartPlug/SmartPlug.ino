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
#define RELAY_PIN          4    // Relay control GPIO

// Timing Configuration
#define DEMO_MODE          true  // true = 30s telemetry, false = 1h telemetry
#define HEARTBEAT_INTERVAL_MS       25000      // 25 seconds
#define TELEMETRY_INTERVAL_DEMO_MS  30000      // 30 seconds (demo)
#define TELEMETRY_INTERVAL_PROD_MS  3600000    // 1 hour (production)
#define WIFI_RECONNECT_INTERVAL_MS  10000      // 10 seconds

// Simulation Settings (for testing without real power sensors)
#define SIMULATED_POWER_WATTS  100.0  // Examples: LED=10W, Fan=50W, TV=100W

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

const unsigned long TELEMETRY_INTERVAL_MS = DEMO_MODE ? TELEMETRY_INTERVAL_DEMO_MS : TELEMETRY_INTERVAL_PROD_MS;

// ============================================
// RELAY CONTROL
// ============================================
void setRelay(bool state) {
  relayState = state;
  digitalWrite(RELAY_PIN, state ? HIGH : LOW);
  Serial.print("[RELAY] ");
  Serial.println(state ? "ON" : "OFF");
}

// ============================================
// ENERGY CALCULATION
// ============================================
float calculateEnergy(unsigned long deltaMs) {
  /*
   * Currently using SIMULATED energy data.
   *
   * For real sensors (PZEM-004T, ACS712, CT Clamp), replace with:
   *
   * float voltage = readVoltageSensor();
   * float current = readCurrentSensor();
   * float power = voltage * current;  // Watts
   * float hours = deltaMs / 3600000.0;
   * return (power * hours) / 1000.0;  // kWh
   */

  if (!relayState) {
    return 0.0;
  }

  float hours = deltaMs / 3600000.0;
  return (SIMULATED_POWER_WATTS * hours) / 1000.0;
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
  Serial.print("Simulated Power: ");
  Serial.print(SIMULATED_POWER_WATTS);
  Serial.println("W");
  Serial.println("========================================");

  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW);

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[SIO] Connecting to ");
    Serial.println(SERVER_HOST);

    socketIO.beginSSL(SERVER_HOST, SERVER_PORT, "/socket.io/?EIO=3&transport=websocket");
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
