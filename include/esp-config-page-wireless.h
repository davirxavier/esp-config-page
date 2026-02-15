#ifndef ESP_CONFIG_PAGE_WIRELESS_H
#define ESP_CONFIG_PAGE_WIRELESS_H

#include "esp-config-defines.h"

#define ESP_CONP_SSID_LEN 33
#define ESP_CONP_PASS_LEN 64

namespace ESP_CONFIG_PAGE
{
    String apSsid = "ESP";
    String apPass = "12345678";
    IPAddress apIp = IPAddress(192, 168, 1, 1);
    String dnsName = "esp-config.local";
    int lastConnectionError = -1;
    unsigned long connectionRetryCounter;
    unsigned long connectionTimeoutCounter;
    unsigned long connectionTimeoutMs = 15000;
    unsigned long currentReconnectRetry = 1;
    unsigned long reconnectTimeMax = 120000;
    KeyValueStorage *wifiStorage = nullptr;

    inline void addWifiNetwork(const char *ssid, const char *pass)
    {
        if (wifiStorage == nullptr)
        {
            LOGN("Trying to add wifi network, but wifi storage is not set.");
            return;
        }

        if (ssid == nullptr || strlen(ssid) == 0 || pass == nullptr || strlen(pass) == 0)
        {
            return;
        }

        char hexBuf[ESP_CONP_SSID_LEN*2+1]{};
        encodeToHex(ssid, hexBuf);
        wifiStorage->save(hexBuf, pass);
    }

    inline void setWifiStorage(KeyValueStorage *storage)
    {
        if (wifiStorage != nullptr)
        {
            free(wifiStorage);
        }

        wifiStorage = storage;
    }

    /**
     * Sets the wifi connection timeout in milliseconds. Default is 15 seconds.
     */
    inline void setConnectionTimeout(unsigned long connectionTimeoutMs)
    {
        ESP_CONFIG_PAGE::connectionTimeoutMs = connectionTimeoutMs;
    }

    /**
     * Returns true if the wifi connection is ready or false if otherwise.
     * @return boolean
     */
    inline bool isWiFiReady()
    {
        return WiFi.status() == WL_CONNECTED;
    }

    /**
     * Tries to reconnect automatically to the saved wireless connection, if there are any.
     *
     * @param force - pass true if the connection is to be forced, that is, if you want to ignore any saved wireless
     * configurations. Should be false on normal usage.
     * @param timeoutMs - timeout in milliseconds for waiting for a connection.
     */
    inline void tryConnectWifi(bool force = false, unsigned long timeoutMs = connectionTimeoutMs)
    {
        LOGF("Trying to connect to wifi, force reconnect: %s\n", force ? "yes" : "no");
        if (!force && WiFi.status() == WL_CONNECTED)
        {
            LOGN("Already connected, cancelling reconnection.");
            return;
        }

        WiFi.setAutoReconnect(false);
        WiFi.persistent(false);

        if (wifiStorage == nullptr)
        {
            return;
        }

        size_t index = 0;
        wifiStorage->doForEachKey([&index, timeoutMs](const char *ssidHex, const char *pass)
        {
            index++;

            if (strlen(ssidHex) == 0 || strlen(pass) == 0)
            {
                LOGF("Network or password at position %zu is empty, ignoring\n", index-1);
                return true;
            }

            char ssid[(strlen(ssidHex)/2)+1]{};
            decodeFromHex(ssidHex, ssid);

            LOGF("Trying connection for network %s\n", ssid);
            WiFi.disconnect(true, true);
            delay(10);
            WiFi.begin(ssid, pass);

            LOGN("Connecting now...");
            int result = WiFi.waitForConnectResult(timeoutMs);
            LOGF("Connection result: %d\n", result);

            if (result == WL_CONNECTED)
            {
                LOGF("Connected to AP successfully, IP address: %s\n", WiFi.localIP().toString().c_str());
                lastConnectionError = -1;
                currentReconnectRetry = 1;
                return false;
            }
            else
            {
                LOGN("Connection error.");
                lastConnectionError = result;
                WiFi.disconnect(false, true);
                return true;
            }

            delay(15);
        }, ESP_CONP_PASS_LEN);
    }

    inline void setAPConfig(const char ssid[], const char pass[])
    {
        apSsid = ssid;
        apPass = pass;
        WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
    }

    inline void wifiGet()
    {
        if (WiFi.status() == WL_NO_SSID_AVAIL)
        {
            WiFi.disconnect(false, true);
        }

        LOGF("Wifi status: %d\n", WiFi.status());

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

        char buf[ESP_CONP_PASS_LEN]{};
        char ssid[ESP_CONP_SSID_LEN]{};
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
                    addWifiNetwork(ssid, buf);
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
        connectionTimeoutCounter = millis() - connectionTimeoutMs;
        setWifiStorage(new ESP_CONFIG_PAGE::LittleFSKeyValueStorage("/esp-conp-saved-networks"));
        WiFi.persistent(false);
        WiFi.setAutoReconnect(true);
        WiFi.mode(WIFI_STA);
        tryConnectWifi();
    }

    inline void wirelessLoop()
    {
        int status = WiFi.status();
        int mode = WiFi.getMode();

        if (mode == WIFI_STA)
        {
            if (status == WL_CONNECTED && millis() - connectionTimeoutCounter > 1000)
            {
                connectionTimeoutCounter = millis();
            }

            if (status != WL_CONNECTED && millis() - connectionTimeoutCounter > connectionTimeoutMs)
            {
                LOGN("Connection timeout reached, starting ap.");
                WiFi.mode(WIFI_AP_STA);
                WiFi.softAP(apSsid, apPass);
                LOGF("Ap started, server IP is %s.\n", WiFi.softAPIP().toString().c_str());
            }
        }
        else if (mode == WIFI_AP_STA && status == WL_CONNECTED)
        {
            LOGF("Connected to network successfully, ip address: %s\n", WiFi.localIP().toString().c_str());
            LOGN("Disabling AP");
            currentReconnectRetry = 1;
            WiFi.mode(WIFI_STA);
        }

        if (status != WL_CONNECTED && millis() - connectionRetryCounter > min(2000 * currentReconnectRetry, reconnectTimeMax))
        {
            LOGN("Trying to reconnect...");
            tryConnectWifi(true);
            connectionRetryCounter = millis();
            currentReconnectRetry++;
        }
    }
}
#endif //ESP_CONFIG_PAGE_WIRELESS_H
