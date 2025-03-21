//
// Created by xav on 7/25/24.
//

#ifndef DX_ESP_CONFIG_PAGE_H
#define DX_ESP_CONFIG_PAGE_H

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFiUdp.h>
#include <esp-config-page-logging.h>
#include <esp-config-defines.h>
#include <esp-config-page-ca.h>
#include <esp-config-page-env.h>
#include <esp-config-page-files.h>
#include <esp-config-page-ota.h>
#include <esp-config-page-wireless.h>
#include <esp-config-page-attributes.h>
#include <esp-config-page-scheduling.h>

#include "config-html.h"

namespace ESP_CONFIG_PAGE
{
    inline void getInfo()
    {
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
        int infoSize = nameLen + WiFi.macAddress().length() + usedBytes.length() + totalBytes.length() +
            freeHeap.length() + strlen(__DATE__) + strlen(__TIME__) + 64;

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

        strcat(buf, WiFi.status() == WL_DISCONNECTED || WiFi.getMode() == WIFI_AP ? "0" : "1");
        strcat(buf, "+");

        strcat(buf, __DATE__);
        strcat(buf, " ");
        strcat(buf, __TIME__);
        strcat(buf, "+");

        ESP_CONFIG_PAGE::server->sendHeader("Authorization", ESP_CONFIG_PAGE::server->header("Authorization"));
        ESP_CONFIG_PAGE::server->send(200, "text/plain", buf);
    }

    void getConfigPage()
    {
        ESP_CONFIG_PAGE::server->sendHeader("Content-Encoding", "gzip");
        ESP_CONFIG_PAGE::server->send_P(200, "text/html", (const char*) ESP_CONFIG_HTML, ESP_CONFIG_HTML_LEN);
    }

    /**
     * Init chosen modules for the config page module.
     *
     * @param modules - Array of modules to enable
     * @param moduleCount - Number of modules in array
     * @param server - Webserver instance to use.
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

        addServerHandler((char*) F("/config"), HTTP_GET, getConfigPage);
        addServerHandler((char*) F("/config/info"), HTTP_GET, getInfo);
        server->onNotFound([]()
        {
            ESP_CONFIG_PAGE::server->send(404, "text/html", F("<html><head><title>Page not found</title></head><body><p>Page not found.</p> <a href=\"/config\">Go to root.</a></body></html>"));
        });

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
            case ATTRIBUTES:
                {
                    enableAttributesModule();
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
