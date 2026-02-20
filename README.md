# ESP-CONFIG-PAGE

A dynamic, fully modular configuration web page for the ESP8266 and ESP32 boards running the Arduino core.

Features:
- Wi-Fi network management with automatic reconnection. Includes support for multiple saved networks.
- Environment variable management.
- Custom actions that can be triggered from the web UI.
- OTA firmware and filesystem updates (HTTP).
- OTA firmware and filesystem updates (WebSockets).
- File browser and management.
- Log monitoring.
- (and completely free of charge 😉)

Requirements:
- **LittleFS** is required. Many core features depend on persistent storage.

Supported boards:
- **ESP32**
- **ESP8266**

Access the config page in the url http://YOURBOARDIP/config. The default ip while in AP mode is 192.168.1.1 or 192.168.4.1.

![Image containing the libraries complete web interface.](https://github.com/davirxavier/esp-config-page/blob/main/images/full.png?raw=true)

![Image containing the libraries complete file browser interface.](https://github.com/davirxavier/esp-config-page/blob/main/images/files.png?raw=true)

### Basic Usage

To get started, include the library with:

```c++
#include "esp-config-page.h"
```

Then, create a WebServer (ESP32) or ESP8266WebServer (ESP8266) instance, initialize the configuration modules, start the webserver, and call the library’s loop function in your main loop.

Example (ESP32):

````c++
#include <Arduino.h>

// Enable logging to Serial (optional)
#define ESP_CONFIG_PAGE_ENABLE_LOGGING
#include "esp-config-page.h"

// Webserver instance
WebServer server(80);

void setup() {
    Serial.begin(115200);
    delay(2500);
    Serial.println("Started.");

    // Setup webpage modules
    ESP_CONFIG_PAGE::initModules(&server, "admin", "admin", "ESP32-TEST3");

    // Start webserver
    server.begin();
    Serial.println("Setup complete.");
}

void loop() {
    // Handle server requests and config page modules
    server.handleClient();
    ESP_CONFIG_PAGE::loop();
}
````

### Wireless Network Automatic Connection

This feature allows the board to automatically attempt to connect to a saved Wi-Fi network.  
If it fails, the board will start an access point (AP) using the SSID and password you provide.  
You can then connect to this AP and use the configuration web page to set your desired Wi-Fi network.

To use this feature:

1. Call `setAPConfig()` to set the SSID and password for the fallback AP.
2. `isWiFiReady()` in your loop to check if the board has successfully connected to a network.

````c++
#include <Arduino.h>

// Enable logging to Serial
#define ESP_CONFIG_PAGE_ENABLE_LOGGING
#include "esp-config-page.h"

// Webserver instance
WebServer server(80);

void setup() {
    Serial.begin(115200);
    delay(2500);
    Serial.println("Started.");

    // Set fallback AP credentials
    ESP_CONFIG_PAGE::setAPConfig("ESP32-TEST", "123");

    // Initialize modules
    ESP_CONFIG_PAGE::initModules(&server, "admin", "admin", "ESP32-TEST3");

    // Start webserver
    server.begin();
    Serial.println("Setup complete.");
}

void loop() {
    server.handleClient();
    ESP_CONFIG_PAGE::loop();

    if (ESP_CONFIG_PAGE::isWiFiReady()) {
        // Connection is ready
    }
}
````

If the board cannot connect to any saved networks, it will create an AP that you can join to access the web interface and configure your Wi-Fi network:

![List of wireless networks near the board, prompting the user for the one to connect.](https://github.com/davirxavier/esp-config-page/blob/main/images/wireless.png?raw=true)

Every network you add credentials for will be saved on the board, and it will automatically attempt to reconnect to any of the saved networks if the connection is lost.

### OTA Updates

This library lets you update your board remotely over Wi-Fi — whether it’s connected to an existing network or running in AP mode.

OTA support works out of the box with the minimal example; no extra configuration is required.  
However, you can customize how OTA behaves by defining any of the options below **before** including the library:

````c++
#include <Arduino.h>

// Enable logging to Serial for easier debugging
#define ESP_CONFIG_PAGE_ENABLE_LOGGING

// Use WebSockets instead of HTTP for OTA updates.
// Recommended for boards with unstable Wi-Fi reception where connections drop frequently.
// With plain HTTP, any disconnect will abort the update. WebSockets, however, can
// automatically reconnect and resume the transfer.
// Disabled by default because it requires additional system resources for the dedicated
// WebSocket server.
#define ESP32_CONP_OTA_USE_WEBSOCKETS

// Maximum buffer size for OTA data when using WebSockets.
// Larger buffers improve upload speed, but always require this specified amount of RAM to be free when receiving the update.
#define ESP_CONP_WS_BUFFER_SIZE (16 * 1024)

// Use the ESP-IDF OTA API instead of Arduino’s default implementation.
// Useful when Arduino is used as an ESP-IDF component, as the Arduino OTA library
// can be unreliable in that setup (at least in my experience). 
// It can also be used in normal Arduino projects,
// though it typically offers little benefit if you're not using the full ESP-IDF environment.
#define ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA

#include "esp-config-page.h"

// Webserver instance
WebServer server(80);

void setup() {
    Serial.begin(115200);
    delay(2500);
    Serial.println("Started.");

    // Setup webpage modules
    ESP_CONFIG_PAGE::initModules(&server, "admin", "admin", "ESP32-TEST3");

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

### Environment Variables Configuration

This library allows you to define environment variables for your board, which can be modified through the web UI. These variables could be any setting you need to configure without uploading new code each time.

To use this feature:

1. Define each environment variable as a pointer to `ESP_CONFIG_PAGE::EnvVar`.
2. Add the variables to the configuration page using `ESP_CONFIG_PAGE::addEnvVar`.
3. Create an instance of a storage class to persist the variables. By default, the library includes the `LittleFSEnvVarStorage` class, which saves variables to the LittleFS storage on your board. You can also implement your own subclass of `EnvVarStorage` for custom storage solutions.
4. Call `ESP_CONFIG_PAGE::setAndUpdateEnvVarStorage()` with the storage instance to persist and recover variables automatically.

Once set up, you can access the stored values via the `value` property of the `EnvVar` instance. These values will automatically update when changed from the web interface.

Example (ESP32):

````c++
#include <Arduino.h>

#define ESP_CONFIG_PAGE_ENABLE_LOGGING
#include "esp-config-page.h"

WebServer server(80);

// Define environment variables
ESP_CONFIG_PAGE::EnvVar *user = new ESP_CONFIG_PAGE::EnvVar{"MY_SERVICE_USER", ""};
ESP_CONFIG_PAGE::EnvVar *password = new ESP_CONFIG_PAGE::EnvVar{"MY_SERVICE_PASSWORD", ""};

void setup() {
    Serial.begin(115200);
    delay(2500);
    Serial.println("Started.");

    ESP_CONFIG_PAGE::setAPConfig("ESP32-TEST", "123");

    // Add environment variables to the config page
    ESP_CONFIG_PAGE::addEnvVar(user);
    ESP_CONFIG_PAGE::addEnvVar(password);

    // Create storage for the environment variables (using LittleFS)
    auto *storage = new ESP_CONFIG_PAGE::LittleFSEnvVarStorage("/env");

    // Set up the storage and load saved environment variables, if any
    ESP_CONFIG_PAGE::setAndUpdateEnvVarStorage(storage);

    ESP_CONFIG_PAGE::initModules(&server, "admin", "admin", "ESP32-TEST3");

    server.on("/test-auth", HTTP_POST, []() {
        String body = server.arg("plain");

        // Use environment variable values anywhere in your code
        // This example checks if the provided credentials match the stored values
        if (body.equals(user->value + ";" + password->value)) {
            server.send(200, "text/plain", "OK");
        } else {
            server.send(401, "text/plain", "INCORRECT AUTH");
        }
    });

    server.begin();
    Serial.println("Setup complete.");
}

void loop() {
    server.handleClient();
    ESP_CONFIG_PAGE::loop();
}
````

This will display a list of fields on the configuration page, allowing you to modify the environment variables at any time.

![Image showing fields containing the environment variables that were defined in the code.](https://github.com/davirxavier/esp-config-page/blob/main/images/envvars.png?raw=true)

### Custom Actions

This library allows you to execute custom code remotely by clicking a button on the board's web UI.

To add a custom action to the webpage, simply call the `ESP_CONFIG_PAGE::addCustomAction` function. Pass the action’s name and a lambda or function that will be executed when the corresponding button is clicked in the web interface.

Example:

This example will set pin 5 to `HIGH` when the button on the web interface is clicked:

````c++
#include <Arduino.h>

#define ESP_CONFIG_PAGE_ENABLE_LOGGING
#include "esp-config-page.h"

// Webserver instance
WebServer server(80);

void setup() {
    Serial.begin(115200);
    delay(2500);
    Serial.println("Started.");

    ESP_CONFIG_PAGE::setAPConfig("ESP32-TEST", "123");

    ESP_CONFIG_PAGE::initModules(&server, "admin", "admin", "ESP32-TEST3");

    // Add a custom action to the webpage
    // The action will appear as a button, and clicking it will trigger the function defined below
    ESP_CONFIG_PAGE::addCustomAction("some-action", [](ESP8266WebServer &server) {
        Serial.println("Triggering pin.");
        digitalWrite(5, HIGH);
        server.send(200);
    });

    // Start webserver
    server.begin();
    Serial.println("Setup complete.");
}

void loop() {
    server.handleClient();
    ESP_CONFIG_PAGE::loop();
}
````

![Image depicting the settings web page, containing an action button.](https://raw.githubusercontent.com/davirxavier/esp-config-page/refs/heads/main/images/actions.png?raw=true)

### Change Enabled Modules

By default, all configuration modules are enabled, but you can enable only the modules you need in order to save storage space. To do this, use the build script included with this package.

1. **Run the Build Script**:
   The script is located in the `buildtool` folder within the library's root directory (or inside `.pio/build` if using PlatformIO). This script allows you to enable specific modules by passing the desired module names as arguments.

   For example, to enable all available modules, run:

   ```bash
   python update_modules.py --wireless --ca --env --files --ota --logging --md5
   ```

Or, to enable just the wireless configuration module, for example:

````
python update_modules.py --wireless
````

After running the script, it will automatically update the HTML file of the configuration page to include only the modules you selected.

2. **Update the initModules Call**:
   You will need to modify the initModules function call in your code to pass only the modules you want to enable. For example:

```c++
ESP_CONFIG_PAGE::Modules enabledModules[] = {ESP_CONFIG_PAGE::WIRELESS};
ESP_CONFIG_PAGE::initModules(enabledModules, 1, &server, "admin", "admin", "ESP32-TEST3");
```

**Requirements**:
- The Python script requires Rust and Cargo to be installed on your system to run. 

This helps to free up some flash space, which is especially useful when using an ESP8266. For the ESP32, not so much, it just has a lot more flash available.