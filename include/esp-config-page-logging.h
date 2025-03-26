#ifndef DX_ESP_CONFIG_PAGE_LOGGING_H
#define DX_ESP_CONFIG_PAGE_LOGGING_H

#define ENABLE_LOGGING_MODULE

#include "Arduino.h"
#include "WebSocketsServer.h"
#include "LittleFS.h"

#define MAX_CLIENTS 8

namespace ESP_CONFIG_PAGE_LOGGING
{
#ifdef ESP32
    using SERIAL_T = HWCDC;
#elif ESP8266
    using SERIAL_T = HardwareSerial;
#endif

    WebSocketsServer server(4000);
    unsigned long lastClean;
    uint8_t pingMessage[] = {0xDE};
    bool isAuthEnabled = false;

    Stream* logSerial = &Serial;
    String username;
    String password;

    bool retentionEnabled = false;
    String logFilePath;
    File logFile;
    unsigned long maximumSizeBytes;
    bool isLoggingEnabled = false;

    struct ConnectedClient
    {
        ConnectedClient(uint8_t id, unsigned long connectedTime, bool authed) : id(id), connectedTime(connectedTime),
            authed(authed)
        {
        };
        uint8_t id;
        unsigned long connectedTime;
        bool authed;
    };

    ConnectedClient* connectedClients[MAX_CLIENTS];

    inline void sendDataToClients(const uint8_t* buffer, const size_t size, bool isText)
    {
        if (isAuthEnabled)
        {
            for (uint8_t i = 0; i < MAX_CLIENTS; i++)
            {
                if (connectedClients[i] != nullptr && connectedClients[i]->authed)
                {
                    if (isText)
                    {
                        server.sendTXT(connectedClients[i]->id, buffer, size);
                    }
                    else
                    {
                        server.sendBIN(connectedClients[i]->id, buffer, size);
                    }
                }
            }
        }
        else
        {
            server.broadcastTXT(buffer, size);
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

            sendDataToClients(buffer, size, true);

            if (retentionEnabled && logFile && logFile.size() < maximumSizeBytes)
            {
                logFile.write(buffer, size);
                logFile.print('\n');
                logFile.flush();
            }
            else if (retentionEnabled && logFile && logFile.size() >= maximumSizeBytes)
            {
                logFile.close();
                LittleFS.remove(logFilePath);
                logFile = LittleFS.open(logFilePath, "w");
            }

            return SERIAL_T::write(buffer, size);
        }
    };

    inline void enableLogging(String u, String p, Stream& serial)
    {
        logSerial = &serial;

        for (uint8_t i = 0; i < MAX_CLIENTS; i++)
        {
            connectedClients[i] = nullptr;
        }

        if (!u.isEmpty() && !p.isEmpty())
        {
            username = u;
            password = p;

            isAuthEnabled = true;

            server.onEvent([](uint8_t client, WStype_t type, uint8_t* payload, size_t length)
            {
                if (type == WStype_CONNECTED)
                {
                    for (uint8_t i = 0; i < MAX_CLIENTS; i++)
                    {
                        if (connectedClients[i] == nullptr)
                        {
                            connectedClients[i] = new ConnectedClient(client, millis(), false);

                            if (retentionEnabled)
                            {
                                retentionEnabled = false;
                                logFile.close();

                                File file = LittleFS.open(logFilePath, "r");
                                uint8_t buf[file.size()];
                                file.read(buf, file.size());
                                file.close();

                                server.sendTXT(client, buf, sizeof(buf));
                                logFile = LittleFS.open(logFilePath, "a");
                                retentionEnabled = true;
                            }
                            return;
                        }
                    }

                    server.sendTXT(client, "Capacity exceeded");
                    server.disconnect(client);
                }
                else if (type == WStype_TEXT)
                {
                    unsigned int usernameLength = username.length();
                    unsigned int passLength = password.length();

                    if (length - 6 - passLength < usernameLength)
                    {
                        return;
                    }

                    if (length - 6 - usernameLength < passLength)
                    {
                        return;
                    }

                    char* text = reinterpret_cast<char*>(payload);
                    char header[5];
                    strncpy(header, text, 4);
                    header[4] = 0;

                    if (strcmp(header, "auth") != 0)
                    {
                        return;
                    }

                    char user[usernameLength + 1];
                    strncpy(user, text + 5, usernameLength);
                    user[usernameLength] = 0;

                    if (strcmp(user, username.c_str()) != 0)
                    {
                        return;
                    }

                    char pass[passLength + 1];
                    strncpy(pass, text + 6 + usernameLength, passLength);
                    pass[passLength] = 0;

                    if (strcmp(pass, password.c_str()) != 0)
                    {
                        return;
                    }

                    for (uint8_t i = 0; i < MAX_CLIENTS; i++)
                    {
                        if (connectedClients[i] != nullptr && connectedClients[i]->id == client)
                        {
                            connectedClients[i]->authed = true;
                            break;
                        }
                    }
                }
                else if (type == WStype_DISCONNECTED)
                {
                    for (uint8_t i = 0; i < MAX_CLIENTS; i++)
                    {
                        if (connectedClients[i] != nullptr && client == connectedClients[i]->id)
                        {
                            delete connectedClients[i];
                            connectedClients[i] = nullptr;
                        }
                    }
                }
            });
        }

        server.begin();
        isLoggingEnabled = true;
    }

    inline void enableLogging(Stream& serial)
    {
        enableLogging(String(), String(), serial);
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
            sendDataToClients(pingMessage, 1, false);

            if (retentionEnabled && logFile)
            {
                logFile.flush();
            }

            if (isAuthEnabled)
            {
                for (uint8_t i = 0; i < MAX_CLIENTS; i++)
                {
                    if (connectedClients[i] != nullptr && !connectedClients[i]->authed && millis() - connectedClients[i]
                        ->connectedTime > 5000)
                    {
                        auto client = connectedClients[i]->id;
                        if (server.clientIsConnected(client))
                        {
                            server.sendTXT(client, "Auth timeout");
                            server.disconnect(client);
                        }
                    }
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
