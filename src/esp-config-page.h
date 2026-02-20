//
// Created by xav on 7/25/24.
//

#ifndef DX_ESP_CONFIG_PAGE_H
#define DX_ESP_CONFIG_PAGE_H

/*
 * Use the macros below to configure your server.
 * Copy the desired macro to your main file and define it before including this header.
 */

// Enable the library Serial logs
// #define ESP_CONFIG_PAGE_ENABLE_LOGGING

// Enable the experimental websocket logging module
//#define ESP_CONP_ENABLE_LOGGING_MODULE

// =====================================================
// WEBSERVER SETTINGS
// =====================================================

// #define ESP_CONP_ASYNC_WEBSERVER
// #define ESP_CONP_HTTPS_SERVER

// Expedition date for the embedded generated certificate. Make this more recent, if you're using this some decades in the future still
#ifndef ESP_CONP_CERT_START_DATE
#define ESP_CONP_CERT_START_DATE "20260101000000"
#endif

// Expiration date for the embedded generated certificate. Update it to a future date, if you're using this after 30 years still
#ifndef ESP_CONP_CERT_END_DATE
#define ESP_CONP_CERT_END_DATE "20560101000000"
#endif

// Folder where the generated certificate and key files will be stored for the HTTPS server on LittleFS.
#ifndef ESP_CONP_CERT_FOLDER
#define ESP_CONP_CERT_FOLDER "/esp_conp_cert_storage"
#endif

// Max size for all environment variables, you can increase this if needed.
#ifndef ESP_CONP_MAX_ENV_LENGTH
#define ESP_CONP_MAX_ENV_LENGTH 8192
#endif

// =====================================================
// OTA SETTINGS
// =====================================================

// Use the ESP-IDF framework to perform the OTA update. Enabling this works better if using Arduino as an IDF component.
// #define ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA

// Use WebSockets to deliver the OTA update. Better for unstable networks, as it will not cancel the update if there's network instability.
// #define ESP32_CONP_OTA_USE_WEBSOCKETS

// #define ESP_CONP_WS_BUFFER_SIZE 4096

// Port for the OTA WebSockets server. Change if you need to use this port.
#ifndef ESP32_CONP_OTA_WS_PORT
#define ESP32_CONP_OTA_WS_PORT 9000
#endif

// Port for the Logging WebSockets server. Change if you need to use this port.
#ifndef ESP_CONP_LOGGING_PORT
#define ESP_CONP_LOGGING_PORT 4000
#endif

#ifndef ESP_CONP_WS_BUFFER_SIZE
#ifdef ESP32
#define ESP_CONP_WS_BUFFER_SIZE (16 * 1024)
#else
#define ESP_CONP_WS_BUFFER_SIZE 4096
#endif
#endif

#if ESP_CONP_WS_BUFFER_SIZE < 64
#error "WebSocket buffer size too small!"
#endif

#if defined(ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA) && defined(ESP8266)
#undef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
#endif

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp-config-defines.h>
#include <esp-config-page-server.h>
#include <esp-config-page-ca.h>
#include <esp-config-page-env.h>
#include <esp-config-page-files.h>
#include <esp-config-page-ota.h>
#include <esp-config-page-wireless.h>

#include "config-html.h"

namespace ESP_CONFIG_PAGE
{
    inline void getInfo(REQUEST_T request)
    {
        String mac = WiFi.macAddress();

        size_t infoSize = 0;
        infoSize += name.length() + 1;
        infoSize += mac.length() + 1;

#ifdef ESP32
        infoSize += ESP_CONP_NUM_LEN("%zu", LittleFS.usedBytes()) + 1;
        infoSize += ESP_CONP_NUM_LEN("%zu", LittleFS.totalBytes()) + 1;
#elif ESP8266
        FSInfo fsInfo;
        LittleFS.info(fsInfo);
        infoSize += ESP_CONP_NUM_LEN("%zu", fsInfo.usedBytes) + 1;
        infoSize += ESP_CONP_NUM_LEN("%zu", fsInfo.totalBytes) + 1;
#endif
        infoSize += ESP_CONP_NUM_LEN("%zu", ESP.getFreeHeap()) + 1;

        const char *wifiStatus = WiFi.status() == WL_DISCONNECTED || WiFi.getMode() == WIFI_AP_STA ? "0" : "1";
        infoSize += strlen(wifiStatus) + 1;

        infoSize += strlen(__DATE__) + 1;
        infoSize += strlen(__TIME__) + 1;

#ifdef ESP32_CONP_OTA_USE_WEBSOCKETS
        const char *otaWsStatus = "2+";
#elifdef ESP_CONP_HTTPS_SERVER
        const char *otaWsStatus = "1+";
#else
        const char *otaWsStatus = "0+";
#endif
        infoSize += strlen(otaWsStatus);

        infoSize += ESP_CONP_NUM_LEN("%zu", ESP_CONP_WS_BUFFER_SIZE-32) + 1;
        infoSize += ESP_CONP_NUM_LEN("%zu", ESP32_CONP_OTA_WS_PORT) + 1;
        infoSize += ESP_CONP_NUM_LEN("%zu", ESP_CONP_LOGGING_PORT) + 1;

        char numBuf[33]{};
        ResponseContext context{};
        initResponseContext(CONP_STATUS_CODE::OK, "text/plain", infoSize, context);

        startResponse(request, context);

        { // Inner scope so we don't take too much stack, for the code after
            char headerBuf[256]{};
            getHeader(request, "Authorization", headerBuf, sizeof(headerBuf));
            sendHeader("Authorization", headerBuf, context);
        }

        writeResponse(name.c_str(), context);
        writeResponse("+", context);

        writeResponse(WiFi.macAddress().c_str(), context);
        writeResponse("+", context);

        ESP_CONP_WRITE_NUMBUF(numBuf, "%zu", LittleFS.usedBytes());
        writeResponse(numBuf, context);
        writeResponse("+", context);

        ESP_CONP_WRITE_NUMBUF(numBuf, "%zu", LittleFS.totalBytes());
        writeResponse(numBuf, context);
        writeResponse("+", context);

        ESP_CONP_WRITE_NUMBUF(numBuf, "%zu", ESP.getFreeHeap());
        writeResponse(numBuf, context);
        writeResponse("+", context);

        writeResponse(wifiStatus, context);
        writeResponse("+", context);

        writeResponse(__DATE__, context);
        writeResponse(" ", context);
        writeResponse(__TIME__, context);
        writeResponse("+", context);

        writeResponse(otaWsStatus, context);

        ESP_CONP_WRITE_NUMBUF(numBuf, "%zu", ESP_CONP_WS_BUFFER_SIZE-32);
        writeResponse(numBuf, context);
        writeResponse("+", context);

        ESP_CONP_WRITE_NUMBUF(numBuf, "%zu", ESP32_CONP_OTA_WS_PORT);
        writeResponse(numBuf, context);
        writeResponse("+", context);

        ESP_CONP_WRITE_NUMBUF(numBuf, "%zu", ESP_CONP_LOGGING_PORT);
        writeResponse(numBuf, context);
        writeResponse("+", context);

        endResponse(request, context);
    }

    inline void getConfigPage(REQUEST_T request)
    {
        ResponseContext c {.fullContent = ESP_CONFIG_HTML, .fullContentFromProgmem = true};
        initResponseContext(CONP_STATUS_CODE::OK, "text/html", ESP_CONFIG_HTML_LEN, c);
        startResponse(request, c);
        sendHeader("Content-Encoding", "gzip", c);
        endResponse(request, c);
    }

    /**
     * Init chosen modules for the config page module.
     *
     * @param modules - Array of modules to enable
     * @param moduleCount - Number of modules in array
     * @param server - Webserver instance to use. Set to NULL if using the HTTPS server.
     * @param username - Page authentication username
     * @param password - Page authentication password
     * @param nodeName - Name of this node to show in the webpage
     */
    inline void initModules(Modules modules[], const uint8_t moduleCount, WEBSERVER_T *server, const String username, const String password, const String nodeName)
    {
        LOGN("Entered config page setup.");

        enabledModules = (Modules*) malloc(moduleCount * sizeof(Modules));
        if (enabledModules == nullptr)
        {
            LOGN("Memory allocation failed for enabled modules array.");
            ESP.restart();
            return;
        }
        memcpy(enabledModules, modules, moduleCount * sizeof(Modules));
        enabledModulesCount = moduleCount;

        ESP_CONFIG_PAGE::server = server;
        ESP_CONFIG_PAGE::username = username;
        ESP_CONFIG_PAGE::password = password;
        ESP_CONFIG_PAGE::nodeName = nodeName;

        name = nodeName;

        addServerHandler("/config", HTTP_GET, getConfigPage);
        addServerHandler("/config/info", HTTP_GET, getInfo);

#ifndef ESP_CONP_HTTPS_SERVER
#ifdef ESP_CONP_ASYNC_WEBSERVER
        server->onNotFound([](REQUEST_T request)
#else
        server->onNotFound([]()
#endif
        {
            ESP_CONP_SEND_REQ(404, "text/html", F("<html><head><title>Page not found</title></head><body><p>Page not found.</p> <a href=\"/config\">Go to root.</a></body></html>"));
        });
#endif

        for (uint8_t i = 0; i < moduleCount; i++)
        {
            const Modules m = modules[i];

            switch (m)
            {
            case OTA:
                {
                    enableOtaModule();
                    break;
                }
            case WIRELESS:
                {
                    enableWirelessModule();
                    break;
                }
            case FILES:
                {
                    enableFilesModule();
                    break;
                }
            case ACTIONS:
                {
                    enableCustomActionsModule();
                    break;
                }
            case ENVIRONMENT:
                {
                    enableEnvModule();
                    break;
                }
            default: break;
            }
        }

        LOGN("Config page setup complete.");
    }


    /**
     * Initialized all modules in the config page.
     *
     * @param server - Webserver instance to use.
     * @param username - Page authentication username
     * @param password - Page authentication password
     * @param nodeName - Name of this node to show in the webpage
     */
    inline void initModules(WEBSERVER_T *server, const String username, const String password, const String nodeName)
    {
        Modules m[] = {OTA, WIRELESS, FILES, ENVIRONMENT, ACTIONS};
        initModules(m, 5, server, username, password, nodeName);
    }

    inline void loop()
    {
        if (enabledModules == nullptr)
        {
            return;
        }

        for (uint8_t i = 0; i < enabledModulesCount; i++)
        {
            const Modules m = enabledModules[i];
            switch (m)
            {
            case OTA:
                {
                    otaLoop();
                    break;
                }
            case WIRELESS:
                {
                    wirelessLoop();
                    break;
                }
            default: break;
            }
        }
    }
}

#endif //DX_ESP_CONFIG_PAGE_H
