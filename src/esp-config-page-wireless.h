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

    inline void wifiGet(REQUEST_T request)
    {
        if (WiFi.status() == WL_NO_SSID_AVAIL)
        {
            WiFi.disconnect(false, true);
        }

        LOGF("Wifi status: %d\n", WiFi.status());

        int count = WiFi.scanNetworks();
        if (count < 0)
        {
            sendInstantResponse(CONP_STATUS_CODE::INTERNAL_SERVER_ERROR, "Error while searching networks.", request);
            return;
        }

        int wifiStatus = WiFi.status();
        int lastConError = lastConnectionError != -1 ? lastConnectionError : wifiStatus;

        String ssid = WiFi.SSID();
        int infoSize = 0;
        infoSize += ssid.length() + 1;
        infoSize += ESP_CONP_NUM_LEN("%d", lastConError) + 1;

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

                memset(ssids[i], 0, 32);

                if (!it)
                {
                    continue;
                }

                for (int j = 0; j < 32; j++)
                {
                    if (it->ssid[j] == 0)
                    {
                        ssids[i][j] = 0;
                        break;
                    }

                    ssids[i][j] = (char) it->ssid[j];
                }

                infoSize += strlen(ssids[i]) + 1;
                infoSize += ESP_CONP_NUM_LEN("%d", WiFi.RSSI(i)) + 1;
            }
        }

        char numBuf[33]{};

        ResponseContext c{};
        initResponseContext(CONP_STATUS_CODE::OK, "text/plain", infoSize, c);
        startResponse(request, c);

        writeResponse(ssid.c_str(), c);
        writeResponse("\n", c);

        ESP_CONP_WRITE_NUMBUF(numBuf, "%d", lastConError);
        writeResponse(numBuf, c);
        writeResponse("\n", c);

        if (wifiStatus != WL_IDLE_STATUS || lastConnectionError != -1)
        {
            for (int i = 0; i < count; i++)
            {
                writeResponse(ssids[i], c);
                writeResponse("\n", c);

                ESP_CONP_WRITE_NUMBUF(numBuf, "%d", WiFi.RSSI(i));
                writeResponse(numBuf, c);
                writeResponse("\n", c);
            }
        }

        endResponse(request, c);
    }

    inline void wifiSet(REQUEST_T request)
    {
        char bodyBuf[128]{};
        if (getBodyAndValidateMaxSize(request, bodyBuf, sizeof(bodyBuf)))
        {
            return;
        }

        if (countChar(bodyBuf, '\n') < 2)
        {
            sendInstantResponse(CONP_STATUS_CODE::BAD_REQUEST, "invalid request", request);
            return;
        }

        char buf[ESP_CONP_PASS_LEN]{};
        char ssid[ESP_CONP_SSID_LEN]{};
        unsigned int currentChar = 0;
        bool isSsid = true;
        size_t bodyLen = strlen(bodyBuf);

        for (size_t i = 0; i < bodyLen; i++)
        {
            char c = bodyBuf[i];

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
                    sendInstantResponse(CONP_STATUS_CODE::OK, "", request);
                    addWifiNetwork(ssid, buf);
                    tryConnectWifi(true);
                    return;
                }

                currentChar = 0;
            }
            else
            {
                buf[currentChar] = c;
                currentChar++;
            }
        }

        sendInstantResponse(CONP_STATUS_CODE::BAD_REQUEST, "invalid request", request);
    }

    inline void enableWirelessModule()
    {
        if (server == nullptr)
        {
            return;
        }

        addServerHandler("/config/wifi", HTTP_GET, wifiGet);
        addServerHandler("/config/wifi", HTTP_POST, wifiSet);
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
