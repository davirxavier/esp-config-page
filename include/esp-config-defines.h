#ifndef ESP_CONFIG_PAGE_DEFINES
#define ESP_CONFIG_PAGE_DEFINES

#ifdef ESP32
#include "WebServer.h"
#include "Update.h"
#elif ESP8266
#include "ESP8266WebServer.h"
#endif

#include "LittleFS.h"

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

#define VALIDATE_AUTH() if (!ESP_CONFIG_PAGE::validateAuth()) return

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

    inline unsigned int getMaxLineLength(const char* str) {
        unsigned int maxLength = 0;
        unsigned int currentLength = 0;

        unsigned int len = strlen(str);
        for (unsigned int i = 0; i < len; i++)
        {
            const char c = str[i];
            if (c == '\n' || c == '\r')
            {
                if (currentLength > maxLength)
                {
                    maxLength = currentLength;
                }

                currentLength = 0;
                if (c == '\r' && i < len-1 && str[i+1] == '\n')
                {
                    i++;
                }
            }
            else
            {
                currentLength++;
            }
        }

        return maxLength;
    }

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

    PROGMEM char httpMethodMapping[5][8] = {"DELETE", "GET", "HEAD", "POST", "PUT"};

    inline void addServerHandler(char *uri, HTTPMethod method, std::function<void(void)> fn)
    {
        server->on(uri, method, [uri, method, fn]()
        {
            VALIDATE_AUTH();
            LOGF("Received request: %s - %s.\n", uri, (method < sizeof(httpMethodMapping) ? httpMethodMapping[method] : ""));
            fn();
        });
    }

    String name;
    const char escapeChars[] = {':', ';', '+', '\0'};
    const char escaper = '|';

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

    /**
     * Sets the esp-config-page serial for printing.
     * @param toSet serial to set.
     */
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
        ATTRIBUTES,
    };
    Modules *enabledModules = nullptr;
    uint8_t enabledModulesCount = 0;

    /**
     * Free dynamically allocated array of arrays.
     */
    inline void freeArr(void** arr, unsigned int count)
    {
        if (arr == nullptr)
        {
            return;
        }

        for (unsigned int i = 0; i < count; i++)
        {
            if (arr[i] != nullptr)
            {
                free(arr[i]);
            }
        }
        free(arr);
    }

    /**
     * Storage class for any key value pair.
     */
    class KeyValueStorage
    {
    public:
        virtual ~KeyValueStorage() = default;
        KeyValueStorage() {}

        /**
         * Save a key-value pair in storage.
         */
        virtual void save(const char *key, const char *value);

        /**
         * Recover a value by key in storage.
         * @return recovered value or nullptr if there are none. ALWAYS FREE() THE CHAR* AFTER YOU ARE DONE USING IT.
         */
        virtual char* recover(const char *key);
    };

    /**
     * Default EnvVarStorage subclass for the library, will store all environment variables in a LittleFS text file.
     * File path is defined by the class's constructor argument.
     */
    class LittleFSKeyValueStorage : public KeyValueStorage
    {
    public:
        ~LittleFSKeyValueStorage() override
        {
            free(this->folderPath);
        }

        /**
         * Instantiate storage.
         * @param folderPath Path to the folder to save the values.
         */
        LittleFSKeyValueStorage(const char *folderPath)
        {
            LOGN("Trying to mount LittleFS.");
#ifdef ESP32
            if (!LittleFS.begin(false /* false: Do not format if mount failed */))
            {
                LOGN("Failed to mount LittleFS");
                if (!LittleFS.begin(true /* true: format */))
                {
                    LOGN("Failed to format LittleFS");
                }
                else
                {
                    LOGN("LittleFS formatted successfully");
                    ESP.restart();
                }
            }
#elif ESP8266
            LittleFS.begin();
#endif

#ifdef ESP32
            bool pathStartsWithSlash = folderPath[0] == '/';
            this->folderPath = (char*) malloc(strlen(folderPath) + (pathStartsWithSlash ? 0 : 1) + 1);
            this->folderPath[0] = '/';
            strcpy(pathStartsWithSlash ? this->folderPath : this->folderPath+1, pathStartsWithSlash ? folderPath+1 : folderPath);
            LOGF("Key value storage path: %s\n", this->folderPath);
#elif ESP8266
            this->folderPath = (char*) malloc(strlen(folderPath) + 1);
            strcpy(this->folderPath, folderPath);
#endif

            if (!LittleFS.exists(this->folderPath))
            {
                LOGF("Path %s does not exist, creating: %s.\n", this->folderPath, LittleFS.mkdir(this->folderPath) ? "true" : "false");
            }
        }

        void save(const char *key, const char *value) override
        {
            char filePath[filePathLength(key)];
            getFilePath(key, filePath);
            LOGF("Saving value for key %s in path %s.\n", key, filePath);

            File file = LittleFS.open(filePath, "w");

#ifdef ESP32
            file.print(value);
#elif ESP8266
            file.write(value);
#endif

            file.close();
        }

        char* recover(const char *key) override
        {
            char filePath[filePathLength(key)];
            getFilePath(key, filePath);
            LOGF("Recovering value for key %s in path %s.\n", key, filePath);

            if (!LittleFS.exists(filePath))
            {
                return nullptr;
            }

            File file = LittleFS.open(filePath, "r");
            char fileStr[file.size()+1];
            file.readBytes(fileStr, file.size());
            fileStr[file.size()] = 0;
            file.close();

            char *ret = (char*) malloc(strlen(fileStr)+1);
            strcpy(ret, fileStr);
            return ret;
        }

    protected:
        unsigned int filePathLength(const char *key)
        {
            return strlen(key) + strlen(this->folderPath) + 2;
        }

        void getFilePath(const char *key, char *outPath)
        {
            sprintf(outPath, "%s/%s", this->folderPath, key);
        }
    private:
        char *folderPath;
    };

}

#endif