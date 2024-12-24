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

    // Define environment variables
    auto *user = new ESP_CONFIG_PAGE::EnvVar{"MY_CLOUD_TOKEN", ""};
    auto *password = new ESP_CONFIG_PAGE::EnvVar{"MY_CLOUD_DEVIDE_ID", ""};

    // Add the variables to the config page
    ESP_CONFIG_PAGE::addEnvVar(user);
    ESP_CONFIG_PAGE::addEnvVar(password);

    // Create the storage instance for your environment variables
    // The project already includes a LittleFS storage, but you can create a new EnvVarStorage implementation and store
    // your variables however you like
    auto *storage = new ESP_CONFIG_PAGE::LittleFSEnvVarStorage("env.cfg");

    // Setup storage and recover saved environment variables, if there are any
    ESP_CONFIG_PAGE::setAndUpdateEnvVarStorage(storage);

    // Setup webpage function
    ESP_CONFIG_PAGE::setup(server, "admin", "admin", "ESP32-TEST");

    server.on("/test-auth", HTTP_POST, [userEv, passEv]() {
        String body = server.arg("plain");

        // Use the environment variables values anywhere you like (to verify auth like this, for example).
        // The package will automatically update the values inside the EnvVar instances you created.
        if (body.equals(userEv->value + ";" + passEv->value)) {
            server.send(200, "text/plain", "OK");
        } else {
            server.send(401, "text/plain", "INCORRECT AUTH");
        }
    });

    // Begin webserver
    server.begin();
    Serial.println("Setup complete.");
}

void loop() {
    // Call update every loop
    server.handleClient();
    ESP_CONFIG_PAGE::loop();
}