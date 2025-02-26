#ifndef ESP_CONFIG_PAGE_DEFINES
#define ESP_CONFIG_PAGE_DEFINES

#ifdef ESP32
#include "WebServer.h"
#include "Update.h"
#elif ESP8266
#include "ESP8266WebServer.h"
#endif

#include "std-util.h"

#define VALIDATE_AUTH() if (!ESP_CONFIG_PAGE::validateAuth()) return

#define ENABLE_LOGGING
#ifdef ENABLE_LOGGING
#define LOGH() Serial.print("[ESP-CONFIG-PAGE] ")
#define LOG(str) LOGH(); ESP_CONFIG_PAGE::serial->print(str)
#define LOGN(str) LOGH(); ESP_CONFIG_PAGE::serial->println(str)
#define LOGF(str, p...) LOGH(); ESP_CONFIG_PAGE::serial->printf(str, p)
#else
#define LOG(str)
#define LOGN(str)
#define LOGF(str, p...)
#endif

namespace ESP_CONFIG_PAGE
{
#ifdef ESP32
    using WEBSERVER_T = WebServer;
#elif ESP8266
    using WEBSERVER_T = ESP8266WebServer;
#endif

    WEBSERVER_T *server;
    String username;
    String password;
    String nodeName;

    inline bool validateAuth()
    {
        if (!server->authenticate(username.c_str(), password.c_str()))
        {
            delay(1000);
            server->requestAuthentication();
            return false;
        }

        return true;
    }

    Stream* serial = &Serial;

    String name;
    const char escapeChars[] = {':', ';', '+', '\0'};
    const char escaper = '|';

    inline int sizeWithEscaping(const char* str)
    {
        int len = strlen(str);
        int escapedCount = 0;

        for (int i = 0; i < len; i++)
        {
            const char c = str[i];
            if (strchr(escapeChars, c))
            {
                escapedCount++;
            }
        }

        return escapedCount + len + 1;
    }

    inline void unescape(char buf[], const char* source)
    {
        unsigned int len = strlen(source);
        bool escaped = false;
        int bufIndex = 0;

        for (unsigned int i = 0; i < len; i++)
        {
            const char c = source[i];

            if (c == escaper && !escaped)
            {
                escaped = true;
            }
            else if (strchr(escapeChars, c) && !escaped)
            {
                buf[bufIndex] = c;
                bufIndex++;
            }
            else
            {
                escaped = false;
                buf[bufIndex] = c;
                bufIndex++;
            }
        }

        buf[bufIndex] = '\0';
    }

    inline void escape(char buf[], const char* source)
    {
        int len = strlen(source);
        int bufIndex = 0;

        for (int i = 0; i < len; i++)
        {
            if (strchr(escapeChars, source[i]))
            {
                buf[bufIndex] = escaper;
                bufIndex++;
            }

            buf[bufIndex] = source[i];
            bufIndex++;
        }

        buf[bufIndex] = '\0';
    }

    inline uint8_t countChar(const char str[], const char separator)
    {
        uint8_t delimiterCount = 0;
        bool escaped = false;
        int len = strlen(str);

        for (int i = 0; i < len; i++)
        {
            char cstr = str[i];
            if (cstr == escaper && !escaped)
            {
                escaped = true;
            }
            else if (cstr == separator && !escaped)
            {
                delimiterCount++;
            }
            else
            {
                escaped = false;
            }
        }

        return delimiterCount;
    }

    inline void getValueSplit(const char data[], char separator, std::shared_ptr<char[]> ret[])
    {
        uint8_t currentStr = 0;
        int lastIndex = 0;
        bool escaped = false;
        unsigned int len = strlen(data);

        for (unsigned int i = 0; i < len; i++)
        {
            const char c = data[i];

            if (c == '|' && !escaped)
            {
                escaped = true;
            }
            else if (c == separator && !escaped)
            {
                int newLen = i - lastIndex + 1;
                ret[currentStr] = newer_std::make_unique<char[]>(newLen);

                strncpy(ret[currentStr].get(), data + lastIndex, newLen - 1);
                ret[currentStr].get()[newLen - 1] = '\0';

                lastIndex = i + 1;
                currentStr++;
            }
            else
            {
                escaped = false;
            }
        }
    }

    inline void setSerial(Stream* toSet)
    {
        serial = toSet;
    }

    enum Modules
    {
        OTA,
        WIRELESS,
        FILES,
        ACTIONS,
        ENVIRONMENT,
        LOGGING,
    };
    Modules *enabledModules = nullptr;
    uint8_t enabledModulesCount = 0;
}

#endif