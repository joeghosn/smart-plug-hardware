/*
 * Smart Plug Configuration
 *
 * Copy this file to config.h and update with your settings.
 * DO NOT commit config.h with real credentials!
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// WIFI CONFIGURATION
// ============================================
#define WIFI_SSID          "YOUR_WIFI_SSID"
#define WIFI_PASSWORD      "YOUR_WIFI_PASSWORD"

// ============================================
// SERVER CONFIGURATION
// ============================================
#define SERVER_HOST        "smart-plug-backend-b4c0.onrender.com"
#define SERVER_PORT        443
#define USE_SSL            true

// ============================================
// DEVICE CONFIGURATION
// ============================================
// QR Code must match the device registered in your dashboard
#define DEVICE_QR_CODE     "YOUR_DEVICE_QR_CODE"

// ============================================
// PIN CONFIGURATION
// ============================================
#define LED_PIN            2    // Built-in LED (connection status)
#define RELAY_PIN          4    // Relay control GPIO

// ============================================
// TIMING CONFIGURATION
// ============================================
// Set to true for demo mode (30s telemetry), false for production (1h telemetry)
#define DEMO_MODE          true

#define HEARTBEAT_INTERVAL_MS       25000      // 25 seconds
#define TELEMETRY_INTERVAL_DEMO_MS  30000      // 30 seconds (demo)
#define TELEMETRY_INTERVAL_PROD_MS  3600000    // 1 hour (production)
#define WIFI_RECONNECT_INTERVAL_MS  10000      // 10 seconds

// ============================================
// SIMULATION SETTINGS
// ============================================
// Simulated power consumption in Watts (for testing without real sensors)
// Examples: LED=10W, Fan=50W, TV=100W, Heater=1500W
#define SIMULATED_POWER_WATTS  100.0

#endif // CONFIG_H
