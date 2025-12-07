# Smart Plug - ESP32 Firmware

ESP32 firmware for the Smart Plug IoT Dashboard. Connects to the backend via WebSocket (Socket.IO) for real-time control and monitoring.

## Features

- WiFi connection with auto-reconnect
- Socket.IO connection to backend server
- Device authentication via QR code
- Remote toggle commands (ON/OFF)
- Heartbeat to maintain connection
- Energy telemetry reporting

## Requirements

### Hardware
- ESP32 development board
- Relay module (connected to GPIO 4)
- Optional: Current/Voltage sensor (PZEM-004T, ACS712, etc.)

### Software
- Arduino IDE or PlatformIO
- Required Libraries:
  - **WebSockets** by Markus Sattler (includes SocketIOclient)
  - **ArduinoJson** by Benoit Blanchon

## Installation

1. **Install Libraries**
   - Open Arduino IDE
   - Go to Sketch > Include Library > Manage Libraries
   - Search and install:
     - `WebSockets` by Markus Sattler
     - `ArduinoJson` by Benoit Blanchon

2. **Configure**
   ```bash
   cd SmartPlug
   cp config.example.h config.h
   ```
   Edit `config.h` with your settings:
   - WiFi credentials
   - Server host (your backend URL)
   - Device QR code (from dashboard)

3. **Upload**
   - Open `SmartPlug/SmartPlug.ino` in Arduino IDE
   - Select your ESP32 board
   - Upload

## Configuration

Edit `SmartPlug/config.h`:

```cpp
// WiFi
#define WIFI_SSID          "YourWiFiName"
#define WIFI_PASSWORD      "YourWiFiPassword"

// Server
#define SERVER_HOST        "your-backend.onrender.com"
#define SERVER_PORT        443

// Device (must match dashboard)
#define DEVICE_QR_CODE     "your-device-qr-code"

// Pins
#define LED_PIN            2    // Built-in LED
#define RELAY_PIN          4    // Relay control

// Mode
#define DEMO_MODE          true  // true=30s telemetry, false=1h
```

## Pin Configuration

| Pin | Function |
|-----|----------|
| GPIO 2 | Built-in LED (connection status) |
| GPIO 4 | Relay control |

## Serial Monitor Output

```
========================================
       Smart Plug - ESP32
========================================
Mode: DEMO (30s telemetry)
Simulated Power: 100.0W
========================================
[WiFi] Connecting to YourWiFi
[WiFi] Connected! IP: 192.168.1.100
[SIO] Connecting to your-backend.onrender.com
[SIO] Connected: /socket.io/?EIO=3&transport=websocket
[AUTH] Authenticating...
========================================
[AUTH] SUCCESS!
[AUTH] Device: Living Room Plug
[AUTH] ID: abc-123-def
[AUTH] Power State: OFF
========================================
[HEARTBEAT] Sent
[HEARTBEAT] ACK received
[TELEMETRY] Sent: 0.000833 kWh
```

## Adding Real Sensors

Replace the `calculateEnergy()` function in `SmartPlug.ino`:

```cpp
float calculateEnergy(unsigned long deltaMs) {
  float voltage = readVoltageSensor();  // Your sensor reading
  float current = readCurrentSensor();  // Your sensor reading
  float power = voltage * current;      // Watts
  float hours = deltaMs / 3600000.0;
  return (power * hours) / 1000.0;      // kWh
}
```

## Folder Structure

```
smart-plug-esp32/
├── SmartPlug/
│   ├── SmartPlug.ino       # Main firmware
│   ├── config.h            # Your configuration (gitignored)
│   └── config.example.h    # Configuration template
├── examples/               # Step-by-step examples
└── README.md
```

## License

MIT
