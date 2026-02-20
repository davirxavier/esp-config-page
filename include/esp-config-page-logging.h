#ifndef DX_ESP_CONFIG_PAGE_LOGGING_H
#define DX_ESP_CONFIG_PAGE_LOGGING_H

#ifdef ESP_CONP_ENABLE_LOGGING_MODULE

#include <esp-config-defines.h>

#include "Arduino.h"
#include "LittleFS.h"

#define MAX_CLIENTS 8

namespace ESP_CONFIG_PAGE_LOGGING
{
#ifdef ESP32
#if ARDUINO_USB_MODE
    using SERIAL_T = HWCDC;
#else
    using SERIAL_T = HardwareSerial;
#endif
#elif ESP8266
    using SERIAL_T = HardwareSerial;
#endif

#ifndef ESP_CONP_ASYNC_WEBSERVER
    AsyncWebServer server(ESP_CONP_LOGGING_PORT);
#endif

    bool handlerAdded = false;
    AsyncWebSocket webSocket("/ws");
    // WebSocketsServer server(ESP_CONP_LOGGING_PORT);
    char messageBuffer[1024]{};
    unsigned long lastClean;
    uint8_t pingMessage[] = {0xDE};

    Stream* logSerial = &Serial;

    bool retentionEnabled = false;
    String logFilePath;
    File logFile;
    unsigned long maximumSizeBytes;
    bool isLoggingEnabled = false;

    enum EventType
    {
        AUTH = 'A',
        LOG = 'l',
        ALL_LOGS = 'L',
        ERROR = 'E',
        PING = 'P',
        PONG = 'p'
    };

    struct ConnectedClient
    {
        int id = -1;
        unsigned long connectedTime = 0;
        bool authed = false;
    };

    ConnectedClient connectedClients[MAX_CLIENTS];

    inline ConnectedClient *getClient(const int id)
    {
        for (size_t i = 0; i < MAX_CLIENTS; i++)
        {
            if (connectedClients[i].id == id)
            {
                return &connectedClients[i];
            }
        }

        return nullptr;
    }

    inline bool clientIsConnected(int i)
    {
        return connectedClients[i].id >= 0;
    }

    inline bool removeClient(uint8_t id)
    {
        for (size_t i = 0; i < MAX_CLIENTS; i++)
        {
            if (connectedClients[i].id == id)
            {
                AsyncWebSocketClient* client = webSocket.client(id);
                if (client != nullptr && client->status() == WS_CONNECTED)
                {
                    client->close();
                }

                connectedClients[i].id = -1;
                return true;
            }
        }

        return false;
    }

    inline int isFull()
    {
        for (size_t i = 0; i < MAX_CLIENTS; i++)
        {
            if (connectedClients[i].id < 0)
            {
                return false;
            }
        }

        return true;
    }

    inline int registerClient(uint8_t id)
    {
        for (size_t i = 0; i < MAX_CLIENTS; i++)
        {
            if (connectedClients[i].id < 0)
            {
                connectedClients[i].id = id;
                connectedClients[i].authed = false;
                connectedClients[i].connectedTime = millis();
                return i;
            }
        }

        return -1;
    }

    inline void sendMessage(uint8_t clientId, const char* message, size_t len, bool isText, EventType eventType)
    {
        messageBuffer[0] = eventType;

        if (len > sizeof(messageBuffer) - 1)
        {
            len = sizeof(messageBuffer) - 1;
        }
        memcpy(messageBuffer + 1, message, len);

        if (isText)
        {
            webSocket.text(clientId, messageBuffer, len + 1);
        }
        else
        {
            webSocket.binary(clientId, messageBuffer, len + 1);
        }
    }

    inline void sendMessage(uint8_t clientId, const char* message, EventType eventType)
    {
        sendMessage(clientId, message, strlen(message), true, eventType);
    }

    inline void broadcastMessage(const char* message, const size_t len, bool isText, EventType eventType)
    {
        for (uint8_t i = 0; i < MAX_CLIENTS; i++)
        {
            if (clientIsConnected(i) && connectedClients[i].authed)
            {
                sendMessage(connectedClients[i].id, message, len, isText, eventType);
            }
        }
    }

    /**
     * Custom serial class, will send all printed text to the connected logging websockets client.
     */
    class ConfigPageSerial : public SERIAL_T
    {
    public:
#ifdef ESP8266
        ConfigPageSerial(HardwareSerial &serial): HardwareSerial(serial) {}
#endif

        size_t write(const uint8_t* buffer, size_t size) override
        {
            if (!isLoggingEnabled || (size == 2 && buffer[0] == '\r' && buffer[1] == '\n') || (size == 1 && buffer[0] ==
                '\n'))
            {
                return SERIAL_T::write(buffer, size);
            }

            broadcastMessage((char*)buffer, size, true, LOG);

            if (retentionEnabled && logFile && logFile.size() >= maximumSizeBytes)
            {
                logFile.close();
                LittleFS.remove(logFilePath);
                logFile = LittleFS.open(logFilePath, "a");
            }

            if (retentionEnabled && logFile)
            {
                logFile.write(buffer, size);
                logFile.print('\n');
                logFile.flush();
            }

            return SERIAL_T::write(buffer, size);
        }
    };

    inline void enableLogging(Stream& serial)
    {
        logSerial = &serial;

        for (uint8_t i = 0; i < MAX_CLIENTS; i++)
        {
            connectedClients[i].id = -1;
        }

        webSocket.onEvent([](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len)
        {
            uint32_t clientId = client->id();

            if (type == WS_EVT_CONNECT)
            {
                int pos = registerClient(clientId);
                if (pos < 0)
                {
                    sendMessage(clientId, "socket full", ERROR);
                    server->close(clientId);
                    return;
                }

                LOGF("Client connected: %d\n", clientId);
            }
            else if (type == WS_EVT_DISCONNECT)
            {
                removeClient(clientId);
            }
            else if (type == WS_EVT_DATA)
            {
                char eventType = data[0];
                switch (eventType)
                {
                case AUTH:
                    {
                        const char* payload = (const char*)(len == 0 ? data : data + 1);
                        const size_t lengthWithoutEvent = len == 0 ? 0 : len - 1;
                        LOGF("Client auth request: %d\n", clientId);

                        char authBuf[128];
                        size_t copyLen = (lengthWithoutEvent < sizeof(authBuf) - 1) ? lengthWithoutEvent : sizeof(authBuf) - 1;
                        memcpy(authBuf, payload, copyLen);
                        authBuf[copyLen] = '\0';

                        char* separator = strchr(authBuf, ':');
                        if (separator == nullptr)
                        {
                            sendMessage(clientId, "Invalid auth", ERROR);
                            return;
                        }

                        separator[0] = 0;
                        char* sentPass = separator + 1;
                        if (strcmp(authBuf, ESP_CONFIG_PAGE::username.c_str()) != 0 || strcmp(sentPass, ESP_CONFIG_PAGE::password.c_str()))
                        {
                            sendMessage(clientId, "Invalid auth", ERROR);
                            return;
                        }

                        for (size_t i = 0; i < MAX_CLIENTS; i++)
                        {
                            if (connectedClients[i].id == clientId)
                            {
                                connectedClients[i].authed = true;
                                sendMessage(clientId, "", AUTH);
                                return;
                            }
                        }

                        sendMessage(clientId, "", AUTH);
                        break;
                    }
                case ALL_LOGS:
                    sendMessage(clientId, "", ALL_LOGS);
                    break;
                case PING:
                    {
                        sendMessage(clientId, "", PONG);
                        break;
                    }
                default:
                    {
                        sendMessage(clientId, "Invalid event type", ERROR);
                        break;
                    }
                }
            }
        });

        if (handlerAdded)
        {
            webSocket.enable(true);
        }
        else
        {
            // Native javascript client doesn't support sending headers, disabled for now
            // server.addHandler(&webSocket).addMiddleware([](AsyncWebServerRequest* r, ArMiddlewareNext next)
            // {
            //     if (!r->authenticate(ESP_CONFIG_PAGE::username.c_str(), ESP_CONFIG_PAGE::password.c_str()))
            //     {
            //         r->requestAuthentication();
            //         return;
            //     }
            //
            //     if (isFull())
            //     {
            //         r->send(503, "text/plain", "socket full");
            //     }
            //     else
            //     {
            //         next();
            //     }
            // });
            server.addHandler(&webSocket);
            handlerAdded = true;
        }

        server.begin();
        isLoggingEnabled = true;
    }

    inline void disableLogging()
    {
        if (isLoggingEnabled)
        {
#ifdef ESP_CONP_ASYNC_WEBSERVER
            webSocket.enable(false);
#else
            server.end();
#endif

            isLoggingEnabled = false;
        }
    }

    inline void loop()
    {
        if (millis() - lastClean > 1000)
        {
            if (retentionEnabled && logFile)
            {
                logFile.flush();
            }

            for (uint8_t i = 0; i < MAX_CLIENTS; i++)
            {
                if (clientIsConnected(i) && !connectedClients[i].authed && millis() - connectedClients[i].connectedTime > 5000)
                {
                    removeClient(connectedClients[i].id);
                }
            }

            lastClean = millis();
        }
    }

    /**
     * Enables log retention policy in a LittleFS file. If the maximum size is exceeded the file will be cleared and recreated.
     *
     * @param filePath File to save logs
     * @param logFileMaxSizeBytes Maximum log file size in bytes
     */
    inline void setLogRetention(String filePath, unsigned int logFileMaxSizeBytes)
    {
        logFilePath = filePath;
        logFile = LittleFS.open(filePath, "a");
        maximumSizeBytes = logFileMaxSizeBytes;
        retentionEnabled = true;
    }
}

#endif
#endif //DX_ESP_CONFIG_PAGE_LOGGING_H
