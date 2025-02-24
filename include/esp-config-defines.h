#ifndef ESP_CONFIG_PAGE_DEFINES
#define ESP_CONFIG_PAGE_DEFINES

#ifdef ESP32
#include "WebServer.h"
#include "Update.h"
#elif ESP8266
#include "ESP8266WebServer.h"
#endif

#include "LittleFS.h"
#include "std-util.h"

#define VALIDATE_AUTH() if (!ESP_CONFIG_PAGE::validateAuth()) return
#define REGISTER_SERVER_METHOD(path, method, fn) VALIDATE_AUTH(); server->on(path, method, fn)

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
                    break;
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
        LOGGING,
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
            const char *fileStr = file.readString().c_str();
            file.close();

            return strcpy((char*) malloc(strlen(fileStr)+1), fileStr);
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