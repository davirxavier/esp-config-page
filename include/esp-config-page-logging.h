#ifndef DX_ESP_CONFIG_PAGE_LOGGING_H
#define DX_ESP_CONFIG_PAGE_LOGGING_H

#include "esp-config-defines.h"

#include "Arduino.h"
#include "ESPAsyncWebServer.h"

#define MAX_CLIENTS 8

namespace ESP_CONFIG_PAGE_LOGGING
{
#ifdef ESP32
    using SERIAL_T = HWCDC;
#elif ESP8266
    using SERIAL_T = HardwareSerial;
#endif

    AsyncWebServer server(4000);
    AsyncWebSocket socket("/ws");
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
        ConnectedClient(uint32_t id, unsigned long connectedTime, bool authed) : id(id), connectedTime(connectedTime),
            authed(authed)
        {
        };
        uint32_t id;
        unsigned long connectedTime;
        bool authed;
    };

    ConnectedClient* connectedClients[MAX_CLIENTS];

    inline void sendDataToClients(const uint8_t *buffer, const size_t size, bool isText)
    {
        if (isAuthEnabled)
        {
            for (uint8_t i = 0; i < MAX_CLIENTS; i++)
            {
                if (connectedClients[i] != nullptr && connectedClients[i]->authed)
                {
                    if (isText)
                    {
                        socket.text(connectedClients[i]->id, buffer, size);
                    }
                    else
                    {
                        socket.binary(connectedClients[i]->id, buffer, size);
                    }
                }
            }
        } else
        {
            socket.textAll(buffer, size);
        }
    }

    class ConfigPageSerial : public SERIAL_T
    {
    public:
#ifdef ESP8266
        ConfigPageSerial(HardwareSerial &serial): HardwareSerial(serial) {}
#endif

        size_t write(const uint8_t* buffer, size_t size) override
        {
            if (!isLoggingEnabled || (size == 2 && buffer[0] == '\r' && buffer[1] == '\n') || (size == 1 && buffer[0] == '\n'))
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

    inline void enableLogging(String u, String p, Stream &serial)
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
            socket.onEvent([](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len)
            {
                if (type == WS_EVT_CONNECT)
                {
                    for (uint8_t i = 0; i < MAX_CLIENTS; i++)
                    {
                        if (connectedClients[i] == nullptr)
                        {
                            connectedClients[i] = new ConnectedClient(client->id(), millis(), false);

                            if (retentionEnabled)
                            {
                                retentionEnabled = false;
                                logFile.close();

                                File file = LittleFS.open(logFilePath, "r");
                                uint8_t buf[file.size()];
                                file.read(buf, file.size());
                                file.close();

                                client->text(buf, sizeof(buf));
                                logFile = LittleFS.open(logFilePath, "a");
                                retentionEnabled = true;
                            }
                            return;
                        }
                    }

                    client->close(1, "Capacity exceeded");
                }
                else if (type == WS_EVT_DISCONNECT)
                {
                    for (uint8_t i = 0; i < MAX_CLIENTS; i++)
                    {
                        if (connectedClients[i] != nullptr && client->id() == connectedClients[i]->id)
                        {
                            delete connectedClients[i];
                            connectedClients[i] = nullptr;
                        }
                    }
                }
                else if (type == WS_EVT_DATA)
                {
                    data[len] = 0;

                    int usernameLength = username.length();
                    int passLength = password.length();

                    if (len-6-passLength < usernameLength)
                    {
                        return;
                    }

                    if (len-6-usernameLength < passLength)
                    {
                        return;
                    }

                    char* text = reinterpret_cast<char*>(data);
                    char header[5];
                    strncpy(header, text, 4);
                    header[4] = 0;

                    if (strcmp(header, "auth") != 0)
                    {
                        return;
                    }

                    char user[usernameLength+1];
                    strncpy(user, text+5, usernameLength);
                    user[usernameLength] = 0;

                    if (strcmp(user, username.c_str()) != 0)
                    {
                        return;
                    }

                    char pass[passLength+1];
                    strncpy(pass, text+6+usernameLength, passLength);
                    pass[passLength] = 0;

                    if (strcmp(pass, password.c_str()) != 0)
                    {
                        return;
                    }

                    for (uint8_t i = 0; i < MAX_CLIENTS; i++)
                    {
                        if (connectedClients[i] != nullptr && connectedClients[i]->id == client->id())
                        {
                            connectedClients[i]->authed = true;
                            break;
                        }
                    }
                }
            });
        }

        server.addHandler(&socket);
        server.begin();
        isLoggingEnabled = true;
    }

    inline void enableLogging(Stream &serial)
    {
        enableLogging(String(), String(), serial);
    }

    inline void disableLogging()
    {
        server.end();
        isLoggingEnabled = false;
    }

    inline void loop()
    {
        if (millis() - lastClean > 2000)
        {
            socket.cleanupClients();
            sendDataToClients(pingMessage, 1, false);

            if (retentionEnabled && logFile)
            {
                logFile.flush();
            }

            if (isAuthEnabled)
            {
                for (uint8_t i = 0; i < MAX_CLIENTS; i++)
                {
                    if (connectedClients[i] != nullptr && !connectedClients[i]->authed && millis() - connectedClients[i]->connectedTime > 5000)
                    {
                        auto client = socket.client(connectedClients[i]->id);
                        if (client != nullptr)
                        {
                            socket.client(connectedClients[i]->id)->close(2, "Authentication timeout.");
                        }
                    }
                }
            }

            lastClean = millis();
        }
    }

    inline void setLogRetention(String filePath, unsigned int logFileMaxSizeBytes)
    {
        logFilePath = filePath;
        logFile = LittleFS.open(filePath, "a");
        maximumSizeBytes = logFileMaxSizeBytes;
        retentionEnabled = true;
    }
}

#endif //DX_ESP_CONFIG_PAGE_LOGGING_H
