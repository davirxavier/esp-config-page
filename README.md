# ESP-CONFIG-PAGE

Dynamic configuration web page for the ESP8266 and ESP32 boards using the arduino core.

Features:
- WiFi network management and automatic connection.
- Environment variable management.
- Custom actions that can be triggered from the web page.
- OTA updates.
- File browser and management.

See the examples folder for concrete code examples.

### Basic Usage

Include the lib package with ```#include "esp-config-page.h"```. Create a WebServer (ESP32) or ESP8266WebServer (ESP8266) instance, call setup function of the package, call begin on the webserver and call the loop function from this package every loop iteration. Code example:

This will create a webpage containing a file browser and wireless network settings on the webserver instance that is passed to the setup function.

https://github.com/davirxavier/esp-config-page/blob/ff86ded96d05f1b95f9293b2793273fb78927f96/examples/simple-usage.cpp#L1-L24

### Wireless Network Automatic Connection

This feature allows the board to try to connect to a wireless network automatically, if it can't, it will start an access point using the SSID and password provided by you. You will then be able to connect to this AP and enter the configuration webpage to configured your desired wireless network.

First, call the ```setAPConfig``` function from the package to set the name and password for the access point created by the board, then call the function ```tryConnectWifi``` to try to connect to the configured network. This call is needed to run the AP if there are no configured credentials and is used in subsequent runs to try to connect to the configured network. You can use the ```isWiFiReady``` function to check if the board's connection is ready in your loop function.

https://github.com/davirxavier/esp-config-page/blob/e1c0983d408ee44f1dff3c726b4f5daed17c35ec/examples/wifi-autoconnect.cpp#L1-L34

### To be finished...
