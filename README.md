# ESP-CONFIG-PAGE

Dynamic configuration web page for the ESP8266 and ESP32 boards using the arduino core.

Features:
- WiFi network management and automatic connection.
- Environment variable management.
- Custom actions that can be triggered from the web page.
- OTA updates.
- File browser and management.

See the examples folder for concrete code examples.

Access the config page in the url http://YOURBOARDIP/config. The default ip while in AP mode is 192.168.1.1.

### Basic Usage

Include the lib package with ```#include "esp-config-page.h"```. Create a WebServer (ESP32) or ESP8266WebServer (ESP8266) instance, call setup function of the package, call begin on the webserver and call the loop function from this package every loop iteration. Code example:

````c++
#include <Arduino.h>
#include "esp-config-page.h"

// Webserver instance
WebServer server(80);

void setup() {
    Serial.begin(115200);
    delay(2500);
    Serial.println("Started.");

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
}
````

This will create a webpage containing a file browser and wireless network settings on the webserver instance that is passed to the setup function.

### Wireless Network Automatic Connection

This feature allows the board to try to connect to a wireless network automatically, if it can't, it will start an access point using the SSID and password provided by you. You will then be able to connect to this AP and enter the configuration webpage for you to configure your desired wireless network.

First, call the ```setAPConfig``` function from the package to set the name and password for the access point created by the board, then call the function ```tryConnectWifi``` to try to connect to the configured network. This call is needed to run the AP if there are no configured credentials and is used in subsequent runs to try to connect to the configured network. You can use the ```isWiFiReady``` function to check if the board's connection is ready in your loop function.

````c++
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
````

### To be finished...
