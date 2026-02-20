#ifndef ESP_CONFIG_PAGE_DEFINES
#define ESP_CONFIG_PAGE_DEFINES

#include "LittleFS.h"

#ifdef ESP_CONFIG_PAGE_ENABLE_LOGGING
#define LOGH() Serial.print("[ESP-CONFIG-PAGE] ")
#define LOG(str) LOGH(); ESP_CONFIG_PAGE::serial->print(str)
#define LOGN(str) LOGH(); ESP_CONFIG_PAGE::serial->println(str)
#define LOGF(str, p...) LOGH(); ESP_CONFIG_PAGE::serial->printf(str, p)
#else
#define LOG(str)
#define LOGN(str)
#define LOGF(str, p...)
#endif

#define ESP_CONP_WRITE_NUMBUF(b, f, n) snprintf(b, sizeof(b), f, n)
#define ESP_CONP_NUM_LEN(f, n) snprintf(nullptr, 0, f, n)

#ifdef ESP_CONP_ASYNC_WEBSERVER
#define ESP_CONP_SEND_REQ(p...) request->send(p)
#else
#define ESP_CONP_SEND_REQ(p...) ESP_CONFIG_PAGE::server->send(p)
#endif

namespace ESP_CONFIG_PAGE
{
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

    Stream* serial = &Serial;

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

    inline void encodeToHex(const char *input, char *output) {
        constexpr char hexDigits[] = "0123456789ABCDEF";

        while (*input) {
            *output++ = hexDigits[(*input >> 4) & 0xF];
            *output++ = hexDigits[*input & 0xF];
            input++;
        }

        *output = '\0';
    }

    inline unsigned char hexCharToByte(char hexChar) {
        if ('0' <= hexChar && hexChar <= '9') {
            return hexChar - '0';
        } else if ('A' <= hexChar && hexChar <= 'F') {
            return hexChar - 'A' + 10;
        } else if ('a' <= hexChar && hexChar <= 'f') {
            return hexChar - 'a' + 10;
        }
        return 0; // Invalid character, but should never happen if input is valid
    }

    inline bool decodeFromHex(const char *hexStr, char *output) {
        int len = strlen(hexStr);

        if (len % 2 != 0) {
            printf("Invalid hex string length.\n");
            return false;
        }

        for (int i = 0; i < len; i += 2) {
            unsigned char highNibble = hexCharToByte(hexStr[i]);
            unsigned char lowNibble = hexCharToByte(hexStr[i + 1]);

            *output++ = (highNibble << 4) | lowNibble;
        }

        *output = '\0';
        return true;
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
        virtual void save(const char *key, const char *value) = 0;

        /**
         * Recover a value by key in storage.
         * @return recovered value or nullptr if there are none. ALWAYS FREE() THE CHAR* AFTER YOU ARE DONE USING IT.
         */
        virtual char* recover(const char *key) = 0;

        /**
         * Recover a value from storage. Writes to out buffer.
         */
        virtual size_t recover(const char *key, char *out, size_t outSize) = 0;

        virtual void doForEachKey(std::function<bool(const char *key, const char *value)> fn, const size_t maxValueSize) = 0;

        virtual bool exists(const char *key) = 0;
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
            this->folderPath = (char*) malloc(strlen(folderPath)+1);
            strcpy(this->folderPath, folderPath);
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

        size_t recover(const char* key, char* out, size_t outSize) override
        {
            memset(out, 0, outSize);

            char filePath[filePathLength(key)];
            getFilePath(key, filePath);
            LOGF("Recovering value for key %s in path %s.\n", key, filePath);

            if (!LittleFS.exists(filePath))
            {
                return 0;
            }

            File file = LittleFS.open(filePath, "r");
            size_t read = file.readBytes(out, outSize-1);
            file.close();

            return read;
        }

        void doForEachKey(std::function<bool(const char* key, const char* value)> fn, const size_t maxValueSize) override
        {
#ifdef ESP32
            File file = LittleFS.open(folderPath);
            File nextFile;
            while (file.isDirectory() && ((nextFile = file.openNextFile())))
            {
                if (!nextFile || nextFile.isDirectory())
                {
                    continue;
                }

                char buf[maxValueSize+1]{};
                nextFile.readBytes(buf, maxValueSize);
                bool result = fn(nextFile.name(), buf);
                nextFile.close();

                if (!result)
                {
                    break;
                }
            }

            if (file)
            {
                file.close();
            }
#elif ESP8266
            Dir dir = LittleFS.openDir(folderPath);

            while (dir.next()) {
                if (!dir.isDirectory())
                {
                    File file = dir.openFile("r");

                    char buf[maxValueSize+1]{};
                    file.readBytes(buf, maxValueSize);
                    bool result = fn(file.name(), buf);
                    file.close();

                    if (!result)
                    {
                        break;
                    }
                }
            }
#endif
        }

        bool exists(const char* key) override
        {
            char filePath[filePathLength(key)];
            getFilePath(key, filePath);
            return LittleFS.exists(filePath);
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