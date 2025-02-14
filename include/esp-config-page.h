//
// Created by xav on 7/25/24.
//

#ifndef DX_ESP_CONFIG_PAGE_H
#define DX_ESP_CONFIG_PAGE_H

#include <Arduino.h>
#include "config-html.h"
#include "LittleFS.h"

#ifdef ESP32
#include "WebServer.h"
#include "Update.h"
#elif ESP8266
#include "ESP8266WebServer.h"
#endif

#include "LittleFS.h"
#include "WiFiUdp.h"

#define ENABLE_LOGGING

#ifdef ENABLE_LOGGING
#define LOGH() Serial.print("[ESP-CONFIG-PAGE] ")
#define LOG(str) LOGH(); Serial.print(str)
#define LOGN(str) LOGH(); Serial.println(str)
#define LOGF(str, p...) LOGH(); Serial.printf(str, p)
#else
#define LOG(str)
#define LOGN(str)
#define LOGF(str, p...)
#endif

namespace newer_std {
    template<class T> struct _Unique_if {
        typedef std::unique_ptr<T> _Single_object;
    };

    template<class T> struct _Unique_if<T[]> {
        typedef std::unique_ptr<T[]> _Unknown_bound;
    };

    template<class T, size_t N> struct _Unique_if<T[N]> {
        typedef void _Known_bound;
    };

    template<class T, class... Args>
    typename _Unique_if<T>::_Single_object
    make_unique(Args&&... args) {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }

    template<class T>
    typename _Unique_if<T>::_Unknown_bound
    make_unique(size_t n) {
        typedef typename std::remove_extent<T>::type U;
        return std::unique_ptr<T>(new U[n]());
    }

    template<class T, class... Args>
    typename _Unique_if<T>::_Known_bound
    make_unique(Args&&...) = delete;
}

namespace ESP_CONFIG_PAGE {

#ifdef ESP32
    using WEBSERVER_T = WebServer;
#elif ESP8266
    using WEBSERVER_T = ESP8266WebServer;
#endif

    enum REQUEST_TYPE {
        CONFIG_PAGE,
        SAVE,
        CUSTOM_ACTIONS,
        FILES,
        DOWNLOAD_FILE,
        DELETE_FILE,
        OTA_END,
        OTA_WRITE_FIRMWARE,
        OTA_WRITE_FILESYSTEM,
        INFO,
        WIFI_LIST,
        WIFI_SET
    };

    struct EnvVar {
        /**
         * @param key - environment variable name and search key, should be unique between all environment variables.
         * @param value - initial value for the environment variable.
         */
        EnvVar(const String key, String value) : key(key), value(value) {};
        const String key;
        String value;
    };

    class EnvVarStorage {
    public:
        EnvVarStorage() {}
        virtual void saveVars(EnvVar **envVars, uint8_t count);
        virtual uint8_t countVars();
        virtual void recoverVars(std::shared_ptr<ESP_CONFIG_PAGE::EnvVar> envVars[]);
    };

    struct CustomAction {
        const String key;
        std::function<void(WEBSERVER_T &server)> handler;
    };

    EnvVar **envVars;
    uint8_t envVarCount = 0;
    uint8_t maxEnvVars = 0;

    CustomAction **customActions;
    uint8_t customActionsCount;
    uint8_t maxCustomActions;

    String name;
    void (*saveEnvVarsCallback)(EnvVar **envVars, uint8_t envVarCount) = NULL;
    EnvVarStorage *envVarStorage = NULL;

    const char escapeChars[] = {':', ';', '+', '\0'};
    const char escaper = '|';

    String wifiSsid = "";
    String wifiPass = "";
    String apSsid = "ESP";
    String apPass = "12345678";
    IPAddress apIp = IPAddress(192, 168, 1, 1);
    String dnsName = "esp-config.local";
    int lastConnectionError = -1;
    bool apStarted = false;
    bool connected = false;

    int sizeWithEscaping(const char *str) {
        int len = strlen(str);
        int escapedCount = 0;

        for (int i = 0; i < len; i++) {
            const char c = str[i];
            if (strchr(escapeChars, c)) {
                escapedCount++;
            }
        }

        return escapedCount + len + 1;
    }

    int caSize() {
        int infoSize = 0;

        for (uint8_t i = 0; i < customActionsCount; i++) {
            CustomAction *ca = customActions[i];
            infoSize += sizeWithEscaping(ca->key.c_str()) + 4;
        }

        return infoSize;
    }

    int envSize() {
        int infoSize = 0;

        for (uint8_t i = 0; i < envVarCount; i++) {
            EnvVar *ev = envVars[i];
            infoSize += sizeWithEscaping(ev->key.c_str()) + sizeWithEscaping(ev->value.c_str()) + 4;
        }

        return infoSize;
    }

    void unescape(char buf[], const char *source) {
        unsigned int len = strlen(source);
        bool escaped = false;
        int bufIndex = 0;

        for (unsigned int i = 0; i < len; i++) {
            const char c = source[i];

            if (c == escaper && !escaped) {
                escaped = true;
            } else if (strchr(escapeChars, c) && !escaped) {
                buf[bufIndex] = c;
                bufIndex++;
            } else {
                escaped = false;
                buf[bufIndex] = c;
                bufIndex++;
            }
        }

        buf[bufIndex] = '\0';
    }

    void escape(char buf[], const char *source) {
        int len = strlen(source);
        int bufIndex = 0;

        for (int i = 0; i < len; i++) {
            if (strchr(escapeChars, source[i])) {
                buf[bufIndex] = escaper;
                bufIndex++;
            }

            buf[bufIndex] = source[i];
            bufIndex++;
        }

        buf[bufIndex] = '\0';
    }

    uint8_t countChar(const char str[], const char separator) {
        uint8_t delimiterCount = 0;
        bool escaped = false;
        int len = strlen(str);

        for (int i = 0; i < len; i++) {
            char cstr = str[i];
            if (cstr == escaper && !escaped) {
              escaped = true;
            } else if (cstr == separator && !escaped) {
              delimiterCount++;
            } else {
              escaped = false;
            }
        }

        return delimiterCount;
    }

    void getValueSplit(const char data[], char separator, std::shared_ptr<char[]> ret[])
    {
        uint8_t currentStr = 0;
        int lastIndex = 0;
        bool escaped = false;
        unsigned int len = strlen(data);

        for (unsigned int i = 0; i < len; i++) {
            const char c = data[i];

            if (c == '|' && !escaped) {
                escaped = true;
            } else if (c == separator && !escaped) {
                int newLen = i - lastIndex + 1;
                ret[currentStr] = newer_std::make_unique<char[]>(newLen);

                strncpy(ret[currentStr].get(), data + lastIndex, newLen-1);
                ret[currentStr].get()[newLen-1] = '\0';

                lastIndex = i+1;
                currentStr++;
            } else {
                escaped = false;
            }
        }
    }

    void handleRequest(WEBSERVER_T &server, String username, String password, REQUEST_TYPE reqType);
    bool handleLogin(WEBSERVER_T &server, String username, String password);

    /**
     * @param server - web server instance
     * @param username - username for the configuration page
     * @param password - password for the configuration page
     * @param nodeName - DOT NOT USE THE CHARACTERS ":", ";" or "+"
     */
    void setup(WEBSERVER_T &server, String username, String password, String nodeName) {
        LOGN("Entered config page setup.");

#ifdef ESP32
        if (!LittleFS.begin(false /* false: Do not format if mount failed */)) {
            Serial.println("Failed to mount LittleFS");
            if (!LittleFS.begin(true /* true: format */)) {
                Serial.println("Failed to format LittleFS");
            } else {
                Serial.println("LittleFS formatted successfully");
            }
        }
#elif ESP8266
        LittleFS.begin();
#endif

        customActionsCount = 0;
        maxCustomActions = 0;
        name = nodeName;

        server.on(F("/config"), HTTP_GET, [&server, username, password]() {
            handleRequest(server, username, password, CONFIG_PAGE);
        });

        server.on(F("/config/info"), HTTP_GET, [&server, username, password]() {
            handleRequest(server, username, password, INFO);
        });

        server.on(F("/config/save"), HTTP_POST, [&server, username, password]() {
            handleRequest(server, username, password, SAVE);
        });

        server.on(F("/config/customa"), HTTP_POST, [&server, username, password]() {
            handleRequest(server, username, password, CUSTOM_ACTIONS);
        });

        server.on(F("/config/files"), HTTP_POST, [&server, username, password]() {
            handleRequest(server, username, password, FILES);
        });

        server.on(F("/config/files/download"), HTTP_POST, [&server, username, password]() {
            handleRequest(server, username, password, DOWNLOAD_FILE);
        });

        server.on(F("/config/files/delete"), HTTP_POST, [&server, username, password]() {
            handleRequest(server, username, password, DELETE_FILE);
        });

        server.on(F("/config/update/firmware"), HTTP_POST, [&server, username, password]() {
            handleRequest(server, username, password, OTA_END);
        }, [&server, username, password]() {
            handleRequest(server, username, password, OTA_WRITE_FIRMWARE);
        });

        server.on(F("/config/update/filesystem"), HTTP_POST, [&server, username, password]() {
            handleRequest(server, username, password, OTA_END);
        }, [&server, username, password]() {
            handleRequest(server, username, password, OTA_WRITE_FILESYSTEM);
        });

        server.on(F("/config/wifi"), HTTP_GET, [&server, username, password]() {
            handleRequest(server, username, password, WIFI_LIST);
        });

        server.on(F("/config/wifi"), HTTP_POST, [&server, username, password]() {
            handleRequest(server, username, password, WIFI_SET);
        });

        server.onNotFound([&server]() {
            server.send(404, "text/html", F("<html><head><title>Page not found</title></head><body><p>Page not found.</p> <a href=\"/config\">Go to root.</a></body></html>"));
        });

        LOGN("Config page setup complete.");
    }

    /**
     * Set the type persistent environment variables storage for the library, as well as use the instance to recover any saved variables (if there are any).
     *
     * @param storage - storage instance for environment variables. Default for this libraty is LittleFSEnvVarStorage, but can be any subclass of EnvVarStorage.
     */
    void setAndUpdateEnvVarStorage(EnvVarStorage *storage) {
        envVarStorage = storage;

        if (envVarStorage != NULL) {
            LOGN("Env var storage is set, configuring env vars.");
#ifdef ESP32
            if (!LittleFS.begin(false /* false: Do not format if mount failed */)) {
                LOGN("Failed to mount LittleFS");
                if (!LittleFS.begin(true /* true: format */)) {
                    LOGN("Failed to format LittleFS");
                } else {
                    LOGN("LittleFS formatted successfully");
                }
            }
#elif ESP8266
            LittleFS.begin();
#endif

            uint8_t count = envVarStorage->countVars();
            LOGF("Found %d env vars\n", count);
            if (count > 0) {
                LOGN("Recoving env vars from storage.");
                std::shared_ptr<EnvVar> recovered[count];
                envVarStorage->recoverVars(recovered);

                for (uint8_t i = 0; i < count; i++) {
                    std::shared_ptr<EnvVar> ev = recovered[i];

                    LOGF("Recovered env var with key %s, searching in defined vars.\n", ev->key.c_str());
                    for (uint8_t j = 0; j < envVarCount; j++) {
                        EnvVar *masterEv = envVars[j];
                        if (masterEv == NULL) {
                            break;
                        }

                        if (masterEv->key == ev->key) {
                            LOGF("Setting env var %s to value %s.\n", ev->key.c_str(), ev->value.c_str());
                            masterEv->value = ev->value;
                        }
                    }
                }
            }
        }
    }

    /**
     * Adds an environment variable to the webpage.
     *
     * @param ev - environment variable to be added. EnvVar key value has to be unique between all added variables.
     */
    void addEnvVar(EnvVar *ev) {
        LOGF("Adding env var %s.\n", ev->key.c_str());

        if (envVarCount + 1 > maxEnvVars) {
            maxEnvVars = maxEnvVars == 0 ? 1 : ceil(maxEnvVars * 1.5);
            LOGF("Env var array overflow, increasing size to %d.\n", maxEnvVars);
            envVars = (EnvVar**) realloc(envVars, sizeof(EnvVar*) * maxEnvVars);
        }

        envVars[envVarCount] = ev;
        envVarCount++;
    }

    /**
     * Adds a custom action to the webpage.
     *
     * @param key - name of the action, has to be unique for all added actions.
     * @param handler - handler function for the action.
     */
    void addCustomAction(String key, std::function<void(WEBSERVER_T &server)> handler) {
        LOGF("Adding action %s.\n", key.c_str());
        if (customActionsCount + 1 > maxCustomActions) {
            maxCustomActions = maxCustomActions == 0 ? 1 : ceil(maxCustomActions * 1.5);
            LOGF("Actions array overflow, increasing size to %d.\n", maxCustomActions);
            customActions = (CustomAction**) realloc(customActions, sizeof(CustomAction*) * maxCustomActions);
        }

        customActions[customActionsCount] = new CustomAction{key, handler};
        customActionsCount++;
    }

    const char* getUpdateErrorStr() {
#ifdef ESP32
        return Update.errorString();
#elif ESP8266
        return Update.getErrorString().c_str();
#endif
    }

    void ota(WEBSERVER_T &server, String username, String password, REQUEST_TYPE reqType) {
        LOGN("OTA upload receiving, starting update process.");

        HTTPUpload& upload = server.upload();
        int command = U_FLASH;
        if (reqType == OTA_WRITE_FILESYSTEM) {
            LOGN("OTA update set to FILESYSTEM mode.");
#ifdef ESP32
            command = U_SPIFFS;
#elif ESP8266
            command = U_FS;
#endif
        }

        if (upload.status == UPLOAD_FILE_START) {
            LOGN("Starting OTA update.");
#ifdef ESP8266
            WiFiUDP::stopAll();
#endif

#ifdef ESP32
            uint32_t maxSpace = UPDATE_SIZE_UNKNOWN;
#elif ESP8266
            uint32_t maxSpace = 0;
            if (command == U_FLASH) {
                maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            } else {
                maxSpace = FS_end - FS_start;
            }

            Update.runAsync(true);
#endif

            LOGF("Calculate max space is %d.\n", maxSpace);
            if (!Update.begin(maxSpace, command)) {  // start with max available size
                LOGN("Error when starting update.");
                server.send(400, "text/plain", getUpdateErrorStr());
            }

        } else if (upload.status == UPLOAD_FILE_WRITE) {
            LOGN("Update write.");
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                LOGN("Error when writing update.");
                server.send(400, "text/plain", getUpdateErrorStr());
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            LOGN("Update ended.");
            if (Update.end(true)) {
                server.send(200);
            } else {
                LOGN("Error finishing update.");
                server.send(400, "text/plain", getUpdateErrorStr());
            }
        }
        yield();
    }

    /**
     * Returns true if the wifi connection is ready or false if otherwise.
     * @return boolean
     */
    bool isWiFiReady() {
        return WiFi.status() == WL_CONNECTED;
    }

    /**
     * Tries to reconnect automatically to the saved wireless connection, if there are any.
     *
     * @param force - pass true if the connection is to be forced, that is, if you want to ignore any saved wireless
     * configurations. Should be false on normal usage.
     * @param timeoutMs - timeout in milliseconds for waiting for a connection.
     */
    void tryConnectWifi(bool force, unsigned long timeoutMs) {
        LOGF("Trying to connect to wifi, force reconnect: %s\n", force ? "yes" : "no");
        if (!force && WiFi.status() == WL_CONNECTED) {
            LOGN("Already connected, cancelling reconnection.");
            return;
        }

#ifdef ESP32
        WiFi.mode(WIFI_MODE_APSTA);
#elif ESP8266
        WiFi.mode(WIFI_AP_STA);
#endif

        WiFi.setAutoReconnect(true);
        WiFi.persistent(true);

        if (force) {
            WiFi.disconnect(false, true);
            WiFi.begin(wifiSsid, wifiPass);
        } else {
            WiFi.begin();
        }

        LOGN("Connecting now...");
        int result = WiFi.waitForConnectResult(timeoutMs);
        LOGF("Connection result: %d\n", result);

        if (result != WL_CONNECTED) {
            LOGN("Connection error.");
            lastConnectionError = result;
        }
    }

    /**
     * Tries to reconnect automatically to the saved wireless connection, if there are any.
     *
     * @param force - pass true if the connection is to be forced, that is, if you want to ignore any saved wireless
     * configurations. Should be false on normal usage.
     */
    void tryConnectWifi(bool force)
    {
        tryConnectWifi(force, 30000);
    }

    void setWiFiCredentials(String &ssid, String &pass) {
        wifiSsid = ssid;
        wifiPass = pass;
    }

    void setAPConfig(const char ssid[], const char pass[]) {
        apSsid = ssid;
        apPass = pass;
        WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
    }

    inline void handleRequest(WEBSERVER_T &server, String username, String password, REQUEST_TYPE reqType) {
        LOGF("Received request of type %d.\n", reqType);

        if (!server.authenticate(username.c_str(), password.c_str())) {
            LOGN("Authentication failure.");
            delay(1500);
            server.requestAuthentication();
            return;
        }

        switch (reqType) {
            case CONFIG_PAGE:
                server.sendHeader("Content-Encoding", "gzip");
                server.send_P(200, "text/html", (const char*) ESP_CONFIG_HTML, ESP_CONFIG_HTML_LEN);
                break;
            case FILES: {
                String path = server.arg("plain");
                if (path.isEmpty()) {
                    path = "/";
                }

                String ret;

#ifdef ESP32
                File file = LittleFS.open(path);
                File nextFile;
                while (file.isDirectory() && (nextFile = file.openNextFile())) {
                    ret += String(nextFile.name()) + ":" + (file.isDirectory() ? "true" : "false") + ":" + file.size() + ";";

                    if (nextFile) {
                        nextFile.close();
                    }
                }
#elif ESP8266
                Dir dir = LittleFS.openDir(path);
                while (dir.next()) {
                    ret += dir.fileName() + ":" + (dir.isDirectory() ? "true" : "false") + ":" + dir.fileSize() + ";";
                }
#endif

                server.send(200, "text/plain", ret);
                break;
            }
            case DOWNLOAD_FILE: {
                String path = server.arg("plain");
                if (path.isEmpty()) {
                    server.send(404);
                    return;
                }

                if (!LittleFS.exists(path)) {
                    server.send(404);
                    return;
                }

                File file = LittleFS.open(path, "r");
                server.sendHeader("Content-Disposition", file.name());
                server.streamFile(file, "text");
                file.close();
                break;
            }
            case DELETE_FILE: {
                String path = server.arg("plain");
                if (path.isEmpty()) {
                    server.send(404);
                    return;
                }

                if (!LittleFS.exists(path)) {
                    server.send(404);
                    return;
                }

                LittleFS.remove(path);
                server.send(200);
                break;
            }
            case CUSTOM_ACTIONS: {
                if (customActionsCount == 0) {
                    return;
                }

                CustomAction *ca = NULL;
                String body = server.arg(F("plain"));

                for (uint8_t i = 0; i < customActionsCount; i++) {
                    if (String(customActions[i]->key) == body) {
                        ca = customActions[i];
                    }
                }

                if (ca != NULL) {
                    ca->handler(server);
                } else {
                    server.send(200);
                }
                break;
            }
            case SAVE: {
                if (envVarCount == 0) {
                    server.send(200);
                    return;
                }

                String body = server.arg("plain");

                uint8_t size = countChar(body.c_str(), ';');
                std::shared_ptr<char[]> split[size];
                getValueSplit(body.c_str(), ';', split);

                for (uint8_t i = 0; i < size; i++) {
                    uint8_t keyAndValueSize = countChar(split[i].get(), ':');

                    if (keyAndValueSize < 2) {
                        continue;
                    }

                    std::shared_ptr<char[]> keyAndValue[keyAndValueSize];
                    getValueSplit(split[i].get(), ':', keyAndValue);

                    char key[strlen(keyAndValue[0].get())];
                    char newValue[strlen(keyAndValue[1].get())];

                    unescape(key, keyAndValue[0].get());
                    unescape(newValue, keyAndValue[1].get());

                    for (uint8_t j = 0; j < envVarCount; j++) {
                        EnvVar *ev = envVars[j];
                        if (ev == NULL) {
                            break;
                        }

                        if (ev->key == key) {
                            ev->value = newValue;
                            break;
                        }
                    }
                }

                if (saveEnvVarsCallback != NULL) {
                    saveEnvVarsCallback(envVars, envVarCount);
                }

                if (envVarStorage != NULL) {
                    envVarStorage->saveVars(envVars, envVarCount);
                }

                server.send(200);
                delay(100);

#ifdef ESP32
                ESP.restart();
#elif ESP8266
                ESP.reset();
#endif
                break;
            }
            case OTA_END: {
                server.sendHeader("Connection", "close");
                server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
                delay(100);
                ESP.restart();
                break;
            }
            case OTA_WRITE_FIRMWARE: {
                ota(server, username, password, OTA_WRITE_FIRMWARE);
                break;
            }
            case OTA_WRITE_FILESYSTEM: {
                ota(server, username, password, OTA_WRITE_FILESYSTEM);
                break;
            }
            case INFO: {
#ifdef ESP32
                String usedBytes = String(LittleFS.usedBytes());
                String totalBytes = String(LittleFS.totalBytes());
#elif ESP8266
                FSInfo fsInfo;
                LittleFS.info(fsInfo);
                String usedBytes = String(fsInfo.usedBytes);
                String totalBytes = String(fsInfo.totalBytes);
#endif
                String freeHeap = String(ESP.getFreeHeap());

                int nameLen = name.length();
                int infoSize = nameLen + WiFi.macAddress().length() + usedBytes.length() + totalBytes.length() + freeHeap.length() + 64;
                infoSize += envSize() + caSize();

                char buf[infoSize];

                strcpy(buf, name.c_str());
                strcat(buf, "+");
                strcat(buf, WiFi.macAddress().c_str());
                strcat(buf, "+");
                strcat(buf, usedBytes.c_str());
                strcat(buf, "+");
                strcat(buf, totalBytes.c_str());
                strcat(buf, "+");
                strcat(buf, freeHeap.c_str());
                strcat(buf, "+");

                for (uint8_t i = 0; i < envVarCount; i++) {
                    EnvVar *ev = envVars[i];

                    char bufKey[sizeWithEscaping(ev->key.c_str())];
                    char bufVal[sizeWithEscaping(ev->value.c_str())];

                    escape(bufKey, ev->key.c_str());
                    escape(bufVal, ev->value.c_str());

                    strcat(buf, bufKey);
                    strcat(buf, ":");
                    strcat(buf, bufVal);
                    strcat(buf, ":;");
                }
                strcat(buf, "+");

                for (uint8_t i = 0; i < customActionsCount; i++) {
                    CustomAction *ca = customActions[i];

                    char bufKey[sizeWithEscaping(ca->key.c_str())];
                    escape(bufKey, ca->key.c_str());

                    strcat(buf, bufKey);
                    strcat(buf, ";");
                }

                strcat(buf, "+");
                strcat(buf, WiFi.status() == WL_DISCONNECTED || WiFi.getMode() == WIFI_AP ? "0" : "1");
                strcat(buf, "+");

                server.send(200, "text/plain", buf);
                break;
            }
            case WIFI_LIST: {
                int count = WiFi.scanNetworks();
                String ssid = WiFi.SSID();
                int bufSize = ssid.length() + 8;
                int wifiStatus = WiFi.status();

                char ssids[count][33];
                if (wifiStatus != WL_IDLE_STATUS || lastConnectionError != -1) {
                    for (int i = 0; i < count; i++) {
#ifdef ESP32
                        const wifi_ap_record_t *it = reinterpret_cast<wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
#elif ESP8266
                        const bss_info *it = WiFi.getScanInfoByIndex(i);
#endif

                        if(!it) {
                            ssids[i][0] = '\0';
                            continue;
                        }

                        memcpy(ssids[i], it->ssid, sizeof(it->ssid));
                        ssids[count][32] = '\0';
                        bufSize += strlen(ssids[count]) + 10;
                    }
                }

                char buf[bufSize];
                buf[0] = '\0';

                if (wifiStatus != WL_IDLE_STATUS || lastConnectionError != -1) {
                    for (int i = 0; i < count; i++) {
                        char escapebuf[strlen(ssids[i])];
                        escape(escapebuf, ssids[i]);

                        uint32_t rssi = WiFi.RSSI(i);
                        int rssiLength = snprintf(NULL, 0, "%d", rssi);
                        char rssibuf[rssiLength+1];
                        sprintf(rssibuf, "%d", rssi);

                        strcat(buf, escapebuf);
                        strcat(buf, ":");
                        strcat(buf, rssibuf);
                        strcat(buf, ":;");
                    }
                }

                strcat(buf, "+");
                strcat(buf, ssid.c_str());
                strcat(buf, "+");
                strcat(buf, String(lastConnectionError != -1 ? lastConnectionError : wifiStatus).c_str());
                strcat(buf, "+");

                server.send(200, "text/plain", buf);
                break;
            }
            case WIFI_SET: {
                String body = server.arg("plain");

                uint8_t size = countChar(body.c_str(), ':');
                std::shared_ptr<char[]> ssidAndPass[size];
                getValueSplit(body.c_str(), ':', ssidAndPass);

                if (size < 2) {
                    server.send(400, "text/plain", "Invalid request.");
                    return;
                }

                char ssidUnescaped[strlen(ssidAndPass[0].get())];
                char passUnescaped[strlen(ssidAndPass[1].get())];

                unescape(ssidUnescaped, ssidAndPass[0].get());
                unescape(passUnescaped, ssidAndPass[1].get());

                server.send(200);
                wifiSsid = String(ssidUnescaped);
                wifiPass = String(passUnescaped);
                tryConnectWifi(true);
                break;
            }
        }
    }

    /**
     * Updated the state of the library, should be called every main loop of the board.
     */
    void loop() {
        int status = WiFi.status();

         if (!apStarted && lastConnectionError != -1) {
             LOGF("Connection error %d, starting AP.\n", lastConnectionError);

#ifdef ESP32
             WiFi.mode(WIFI_MODE_APSTA);
#elif ESP8266
             WiFi.mode(WIFI_AP_STA);
#endif

             WiFi.softAP(apSsid, apPass);
             LOGF("Server IP is %s.\n", apIp.toString().c_str());
             apStarted = true;
             connected = false;
        }

         if (!connected && status == WL_CONNECTED) {
             LOGF("Connected successfully to wireless network, IP: %s.\n", WiFi.localIP().toString().c_str());
             lastConnectionError = -1;
             apStarted = false;
             connected = true;

             LOGN("Disabling AP.");
#ifdef ESP32
             WiFi.mode(WIFI_MODE_STA);
#elif ESP8266
             WiFi.mode(WIFI_AP_STA);
#endif
         }
    }

    /**
     * Default EnvVarStorage subclass for the library, will store all environment variables in a LittleFS text file.
     * File path is defined by the class's constructor argument.
     */
    class LittleFSEnvVarStorage : public EnvVarStorage {
    public:
        /**
         * @param filePath - path to the file where the environment variables will be saved.
         */
        LittleFSEnvVarStorage(const String filePath) : filePath(filePath) {};

        void saveVars(ESP_CONFIG_PAGE::EnvVar **toStore, uint8_t count) override {
            int size = envSize() + 32;
            char buf[size];
            strcpy(buf, "");

            for (uint8_t i = 0; i < count; i++) {
                EnvVar *ev = toStore[i];
                if (ev == NULL) {
                    break;
                }

                char bufKey[sizeWithEscaping(ev->key.c_str())];
                char bufVal[sizeWithEscaping(ev->value.c_str())];

                escape(bufKey, ev->key.c_str());
                escape(bufVal, ev->value.c_str());

                strcat(buf, bufKey);
                strcat(buf, ":");
                strcat(buf, bufVal);
                strcat(buf, ":;");
            }

            File file = LittleFS.open(filePath, "w");
#ifdef ESP32
            file.print(buf);
#elif ESP8266
            file.write(buf);
#endif
            file.close();
        }

        uint8_t countVars() override {
            if (!LittleFS.exists(filePath)) {
                return 0;
            }

            File file = LittleFS.open(filePath, "r");
            String content = file.readString();
            file.close();

            return ESP_CONFIG_PAGE::countChar(content.c_str(), ';');
        }

        void recoverVars(std::shared_ptr<ESP_CONFIG_PAGE::EnvVar> recovered[]) override {
            if (!LittleFS.exists(filePath)) {
                return;
            }

            File file = LittleFS.open(filePath, "r");
            String content = file.readString();

            uint8_t count = countChar(content.c_str(), ';');
            std::shared_ptr<char[]> split[count];
            getValueSplit(content.c_str(), ';', split);

            for (uint8_t i = 0; i < count; i++) {
                uint8_t varStrCount = countChar(split[i].get(), ':');

                if (varStrCount < 2) {
                    continue;
                }

                std::shared_ptr<char[]> varStr[varStrCount];
                getValueSplit(split[i].get(), ':', varStr);

                char key[strlen(varStr[0].get())];
                char val[strlen(varStr[1].get())];

                unescape(key, varStr[0].get());
                unescape(val, varStr[1].get());

                recovered[i] = std::make_shared<ESP_CONFIG_PAGE::EnvVar>(String(key), String(val));
            }

            file.close();
        }

    private:
        const String filePath;
    };

}

#endif //DX_ESP_CONFIG_PAGE_H
