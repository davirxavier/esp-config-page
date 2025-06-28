#ifndef ESP_CONFIG_PAGE_WIRELESS_H
#define ESP_CONFIG_PAGE_WIRELESS_H

#include "esp-config-defines.h"

namespace ESP_CONFIG_PAGE
{
    String wifiSsid = "";
    String wifiPass = "";
    String apSsid = "ESP";
    String apPass = "12345678";
    IPAddress apIp = IPAddress(192, 168, 1, 1);
    String dnsName = "esp-config.local";
    int lastConnectionError = -1;
    bool apStarted = false;
    bool connected = false;

    /**
     * Returns true if the wifi connection is ready or false if otherwise.
     * @return boolean
     */
    inline bool isWiFiReady()
    {
        return WiFi.status() == WL_CONNECTED && !apStarted;
    }

    /**
     * Tries to reconnect automatically to the saved wireless connection, if there are any.
     *
     * @param force - pass true if the connection is to be forced, that is, if you want to ignore any saved wireless
     * configurations. Should be false on normal usage.
     * @param timeoutMs - timeout in milliseconds for waiting for a connection.
     */
    inline void tryConnectWifi(bool force, unsigned long timeoutMs)
    {
        LOGF("Trying to connect to wifi, force reconnect: %s\n", force ? "yes" : "no");
        if (!force && WiFi.status() == WL_CONNECTED)
        {
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

        if (force)
        {
            WiFi.disconnect(false, true);
            WiFi.begin(wifiSsid, wifiPass);
        }
        else
        {
            WiFi.begin();
        }

        LOGN("Connecting now...");
        int result = WiFi.waitForConnectResult(timeoutMs);
        LOGF("Connection result: %d\n", result);

        if (result != WL_CONNECTED)
        {
            LOGN("Connection error.");
            lastConnectionError = result;
            WiFi.disconnect();
        }
    }

    /**
     * Tries to reconnect automatically to the saved wireless connection, if there are any.
     *
     * @param force - pass true if the connection is to be forced, that is, if you want to ignore any saved wireless
     * configurations. Should be false on normal usage.
     */
    inline void tryConnectWifi(bool force)
    {
        tryConnectWifi(force, 10000);
    }

    inline void setWiFiCredentials(String& ssid, String& pass)
    {
        wifiSsid = ssid;
        wifiPass = pass;
    }

    inline void setAPConfig(const char ssid[], const char pass[])
    {
        apSsid = ssid;
        apPass = pass;
        WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
    }

    inline void wifiGet()
    {
        int count = WiFi.scanNetworks();
        if (count < 0)
        {
            server->send(500, "text/plain", "Error while searching networks.");
            return;
        }

        String ssid = WiFi.SSID();
        int bufSize = ssid.length() + 8;
        int wifiStatus = WiFi.status();

        char ssids[count][33];
        if (wifiStatus != WL_IDLE_STATUS || lastConnectionError != -1)
        {
            for (int i = 0; i < count; i++)
            {
#ifdef ESP32
                const wifi_ap_record_t* it = reinterpret_cast<wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
#elif ESP8266
                const bss_info *it = WiFi.getScanInfoByIndex(i);
#endif

                 if (!it)
                 {
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

        strcat(buf, ssid.c_str());
        strcat(buf, "\n");
        strcat(buf, String(lastConnectionError != -1 ? lastConnectionError : wifiStatus).c_str());
        strcat(buf, "\n");

        if (wifiStatus != WL_IDLE_STATUS || lastConnectionError != -1)
        {
            for (int i = 0; i < count; i++)
            {
                uint32_t rssi = WiFi.RSSI(i);
                int rssiLength = snprintf(nullptr, 0, "%d", rssi);
                char rssibuf[rssiLength + 1];
                sprintf(rssibuf, "%d", rssi);

                strcat(buf, ssids[i]);
                strcat(buf, "\n");
                strcat(buf, rssibuf);
                strcat(buf, "\n");
            }
        }

        server->send(200, "text/plain", buf);
    }

    inline void wifiSet()
    {
        String body = server->arg("plain");

        if (countChar(body.c_str(), '\n') < 2)
        {
            server->send(400);
            return;
        }

        char buf[33];
        char ssid[33];
        unsigned int currentChar = 0;
        bool isSsid = true;

        for (unsigned int i = 0; i < body.length(); i++)
        {
            char c = body[i];

            if (c == '\n')
            {
                buf[currentChar] = 0;

                if (isSsid)
                {
                    strcpy(ssid, buf);
                    isSsid = false;
                }
                else
                {
                    server->send(200);
                    wifiSsid = String(ssid);
                    wifiPass = String(buf);
                    tryConnectWifi(true);
                    break;
                }

                currentChar = 0;
            }
            else
            {
                buf[currentChar] = c;
                currentChar++;
            }
        }
    }

    inline void enableWirelessModule()
    {
        if (server == nullptr)
        {
            return;
        }

        addServerHandler((char*) F("/config/wifi"), HTTP_GET, wifiGet);
        addServerHandler((char*) F("/config/wifi"), HTTP_POST, wifiSet);
    }

    inline void wirelessLoop()
    {
        int status = WiFi.status();

        if (!apStarted && lastConnectionError != -1)
        {
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

        if (!connected && status == WL_CONNECTED)
        {
            LOGF("Connected successfully to wireless network, IP: %s.\n", WiFi.localIP().toString().c_str());
            lastConnectionError = -1;
            apStarted = false;
            connected = true;

            LOGN("Disabling AP.");
#ifdef ESP32
            WiFi.mode(WIFI_MODE_STA);
#elif ESP8266
            WiFi.mode(WIFI_STA);
#endif
        }
    }
}
#endif //ESP_CONFIG_PAGE_WIRELESS_H
