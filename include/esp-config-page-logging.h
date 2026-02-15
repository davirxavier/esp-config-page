#ifndef DX_ESP_CONFIG_PAGE_LOGGING_H
#define DX_ESP_CONFIG_PAGE_LOGGING_H

#define ENABLE_LOGGING_MODULE

#include <esp-config-defines.h>

#include "Arduino.h"
#include "WebSocketsServer.h"
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

    WebSocketsServer server(ESP_CONP_LOGGING_PORT);
    char messageBuffer[1024]{};
    unsigned long lastClean;
    uint8_t pingMessage[] = {0xDE};

    Stream* logSerial = &Serial;
    String username;
    String password;

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
                connectedClients[i].id = -1;

                if (server.clientIsConnected(id))
                {
                    server.disconnect(id);
                }
                return true;
            }
        }

        return false;
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

    inline void sendMessage(uint8_t clientId, const char *message, const size_t len, bool isText, EventType eventType)
    {
        messageBuffer[0] = eventType;
        memcpy(messageBuffer+1, message, len);

        if (isText)
        {
            server.sendTXT(clientId, messageBuffer, len+1);
        }
        else
        {
            server.sendBIN(clientId, (uint8_t*) messageBuffer, len+1);
        }
    }

    inline void sendMessage(uint8_t clientId, const char *message, EventType eventType)
    {
        sendMessage(clientId, message, strlen(message), true, eventType);
    }

    inline void broadcastMessage(const char *message, const size_t len, bool isText, EventType eventType)
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

            broadcastMessage((char*) buffer, size, true, LOG);

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

    inline void enableLogging(String u, String p, Stream& serial)
    {
        logSerial = &serial;

        for (uint8_t i = 0; i < MAX_CLIENTS; i++)
        {
            connectedClients[i].id = -1;
        }

        username = u;
        password = p;

        server.onEvent([](uint8_t client, WStype_t type, uint8_t* payload, size_t length)
        {
            if (type == WStype_CONNECTED)
            {
                int pos = registerClient(client);
                if (pos < 0)
                {
                    sendMessage(client, "socket full", ERROR);
                    server.disconnect(client);
                    return;
                }

                LOGF("Client connected: %d\n", client);
            }
            else if (type == WStype_DISCONNECTED)
            {
                removeClient(client);
            }
            else if (type == WStype_TEXT || type == WStype_BIN)
            {
                char eventType = payload[0];
                const char *payloadWithoutEvent = (const char*) (length == 0 ? payload : payload+1);
                const size_t lengthWithoutEvent = length == 0 ? 0 : length-1;

                switch (eventType)
                {
                case AUTH:
                    {
                        LOGF("Client auth request: %d\n", client);

                        char *separator = strchr(payloadWithoutEvent, ':');
                        if (separator == nullptr)
                        {
                            sendMessage(client, "Invalid auth", ERROR);
                            return;
                        }

                        separator[0] = 0;
                        char *sentPass = separator+1;
                        if (strcmp(payloadWithoutEvent, username.c_str()) != 0 || strcmp(sentPass, password.c_str()))
                        {
                            sendMessage(client, "Invalid auth", ERROR);
                            return;
                        }

                        for (size_t i = 0; i < MAX_CLIENTS; i++)
                        {
                            if (connectedClients[i].id == client)
                            {
                                connectedClients[i].authed = true;
                                sendMessage(client, "", AUTH);
                                return;
                            }
                        }

                        sendMessage(client, "unknown client", ERROR);
                        break;
                    }
                case ALL_LOGS:
                    {
                        // if (retentionEnabled)
                        // {
                        //     retentionEnabled = false;
                        //     logFile.close();
                        //
                        //     File file = LittleFS.open(logFilePath, "r");
                        //     char buf[file.size()+1]{};
                        //     file.readBytes(buf, sizeof(buf)-1);
                        //     file.close();
                        //
                        //     sendMessage(client, buf, ALL_LOGS);
                        //     logFile = LittleFS.open(logFilePath, "a");
                        //     retentionEnabled = true;
                        // }
                        // else
                        // {
                        sendMessage(client, "", ALL_LOGS);
                        // }
                        break;
                    }
                case PING:
                    {
                        sendMessage(client, "", PONG);
                        break;
                    }
                default:
                    {
                        sendMessage(client, "Invalid event type", ERROR);
                        break;
                    }
                }
            }
        });

        server.begin();
        isLoggingEnabled = true;
    }

    inline void disableLogging()
    {
        if (isLoggingEnabled)
        {
            server.close();
            isLoggingEnabled = false;
        }
    }

    inline void loop()
    {
        server.loop();

        if (millis() - lastClean > 2000)
        {
            // broadcastMessage("P", 1, true, PING);

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

#endif //DX_ESP_CONFIG_PAGE_LOGGING_H
