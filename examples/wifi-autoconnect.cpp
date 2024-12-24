#include <Arduino.h>
#include "esp-config-page.h"

// Webserver instance
WebServer server(80);

void setup() {
    Serial.begin(115200);
    delay(2500);
    Serial.println("Started.");

    // Set AP SSID and password for when the board can't connect to your network
    ESP_CONFIG_PAGE::setAPConfig("ESP32-TEST", "drx13246");

    // Try to reconnect automatically if you already configured your board
    ESP_CONFIG_PAGE::tryConnectWifi(false);

    // Setup webpage function
    ESP_CONFIG_PAGE::setup(server, "admin", "admin", "ESP32-TEST");

    // Begin webserver
    server.begin();
    Serial.println("Setup complete.");
}

void loop() {
    // Call update every loop
    server.handleClient();
    ESP_CONFIG_PAGE::loop();

    if (ESP_CONFIG_PAGE::isWiFiReady()) {
        // Connection is ready
    }
}