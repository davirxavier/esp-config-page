//
// Created by xav on 2/22/25.
//

#ifndef ESP_CONFIG_PAGE_ENV_H
#define ESP_CONFIG_PAGE_ENV_H

#include "LittleFS.h"
#include "esp-config-defines.h"

namespace ESP_CONFIG_PAGE
{
    struct EnvVar
    {
        /**
         * @param key - environment variable name and search key, should be unique between all environment variables.
         * @param value - initial value for the environment variable.
         */
        EnvVar(const String key, String value) : key(key), value(value)
        {
        };
        const String key;
        String value;
    };

    class EnvVarStorage
    {
    public:
        virtual ~EnvVarStorage() = default;

        EnvVarStorage()
        {
        }

        virtual void saveVars(EnvVar** envVars, uint8_t count);
        virtual uint8_t countVars();
        virtual void recoverVars(std::shared_ptr<ESP_CONFIG_PAGE::EnvVar> envVars[]);
    };

    EnvVar** envVars;
    uint8_t envVarCount = 0;
    uint8_t maxEnvVars = 0;
    void (*saveEnvVarsCallback)(EnvVar** envVars, uint8_t envVarCount) = NULL;
    EnvVarStorage* envVarStorage = NULL;

    inline int envSize()
    {
        int infoSize = 0;

        for (uint8_t i = 0; i < envVarCount; i++)
        {
            EnvVar* ev = envVars[i];
            infoSize += sizeWithEscaping(ev->key.c_str()) + sizeWithEscaping(ev->value.c_str()) + 4;
        }

        return infoSize;
    }

    /**
     * Default EnvVarStorage subclass for the library, will store all environment variables in a LittleFS text file.
     * File path is defined by the class's constructor argument.
     */
    class LittleFSEnvVarStorage : public EnvVarStorage
    {
    public:
        virtual ~LittleFSEnvVarStorage() = default;
        /**
         * @param filePath - path to the file where the environment variables will be saved.
         */
        LittleFSEnvVarStorage(const String filePath) : filePath(filePath)
        {
        };

        void saveVars(ESP_CONFIG_PAGE::EnvVar** toStore, uint8_t count) override
        {
            int size = envSize() + 32;
            char buf[size];
            strcpy(buf, "");

            for (uint8_t i = 0; i < count; i++)
            {
                EnvVar* ev = toStore[i];
                if (ev == NULL)
                {
                    break;
                }

                char bufKey[sizeWithEscaping(ev->key.c_str())];
                char bufVal[sizeWithEscaping(ev->value.c_str())];

                escape(bufKey, ev->key.c_str());
                escape(bufVal, ev->value.c_str());

                strcat(buf, bufKey);
                strcat(buf, ":");
                strcat(buf, bufVal);
                strcat(buf, ":;");
            }

            File file = LittleFS.open(filePath, "w");
#ifdef ESP32
            file.print(buf);
#elif ESP8266
            file.write(buf);
#endif
            file.close();
        }

        uint8_t countVars() override
        {
            if (!LittleFS.exists(filePath))
            {
                return 0;
            }

            File file = LittleFS.open(filePath, "r");
            String content = file.readString();
            file.close();

            return ESP_CONFIG_PAGE::countChar(content.c_str(), ';');
        }

        void recoverVars(std::shared_ptr<ESP_CONFIG_PAGE::EnvVar> recovered[]) override
        {
            if (!LittleFS.exists(filePath))
            {
                return;
            }

            File file = LittleFS.open(filePath, "r");
            String content = file.readString();

            uint8_t count = countChar(content.c_str(), ';');
            std::shared_ptr<char[]> split[count];
            getValueSplit(content.c_str(), ';', split);

            for (uint8_t i = 0; i < count; i++)
            {
                uint8_t varStrCount = countChar(split[i].get(), ':');

                if (varStrCount < 2)
                {
                    continue;
                }

                std::shared_ptr<char[]> varStr[varStrCount];
                getValueSplit(split[i].get(), ':', varStr);

                char key[strlen(varStr[0].get())];
                char val[strlen(varStr[1].get())];

                unescape(key, varStr[0].get());
                unescape(val, varStr[1].get());

                recovered[i] = std::make_shared<ESP_CONFIG_PAGE::EnvVar>(String(key), String(val));
            }

            file.close();
        }

    private:
        const String filePath;
    };

    /**
     * Set the type persistent environment variables storage for the library, as well as use the instance to recover any saved variables (if there are any).
     *
     * @param storage - storage instance for environment variables. Default for this libraty is LittleFSEnvVarStorage, but can be any subclass of EnvVarStorage.
     */
    void setAndUpdateEnvVarStorage(EnvVarStorage* storage)
    {
        envVarStorage = storage;

        if (envVarStorage != NULL)
        {
            LOGN("Env var storage is set, configuring env vars.");
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

            uint8_t count = envVarStorage->countVars();
            LOGF("Found %d env vars\n", count);
            if (count > 0)
            {
                LOGN("Recoving env vars from storage.");
                std::shared_ptr<EnvVar> recovered[count];
                envVarStorage->recoverVars(recovered);

                for (uint8_t i = 0; i < count; i++)
                {
                    std::shared_ptr<EnvVar> ev = recovered[i];

                    LOGF("Recovered env var with key %s, searching in defined vars.\n", ev->key.c_str());
                    for (uint8_t j = 0; j < envVarCount; j++)
                    {
                        EnvVar* masterEv = envVars[j];
                        if (masterEv == NULL)
                        {
                            break;
                        }

                        if (masterEv->key == ev->key)
                        {
                            LOGF("Setting env var %s to value %s.\n", ev->key.c_str(), ev->value.c_str());
                            masterEv->value = ev->value;
                        }
                    }
                }
            }
        }
    }

    /**
     * Adds an environment variable to the webpage.
     *
     * @param ev - environment variable to be added. EnvVar key value has to be unique between all added variables.
     */
    inline void addEnvVar(EnvVar* ev)
    {
        LOGF("Adding env var %s.\n", ev->key.c_str());

        if (envVarCount + 1 > maxEnvVars)
        {
            maxEnvVars = maxEnvVars == 0 ? 1 : ceil(maxEnvVars * 1.5);
            LOGF("Env var array overflow, increasing size to %d.\n", maxEnvVars);
            envVars = (EnvVar**)realloc(envVars, sizeof(EnvVar*) * maxEnvVars);
        }

        envVars[envVarCount] = ev;
        envVarCount++;
    }

    inline void saveEnv()
    {

        if (envVarCount == 0)
        {
            server->send(200);
            return;
        }

        String body = server->arg("plain");

        uint8_t size = countChar(body.c_str(), ';');
        std::shared_ptr<char[]> split[size];
        getValueSplit(body.c_str(), ';', split);

        for (uint8_t i = 0; i < size; i++)
        {
            uint8_t keyAndValueSize = countChar(split[i].get(), ':');

            if (keyAndValueSize < 2)
            {
                continue;
            }

            std::shared_ptr<char[]> keyAndValue[keyAndValueSize];
            getValueSplit(split[i].get(), ':', keyAndValue);

            char key[strlen(keyAndValue[0].get())];
            char newValue[strlen(keyAndValue[1].get())];

            unescape(key, keyAndValue[0].get());
            unescape(newValue, keyAndValue[1].get());

            for (uint8_t j = 0; j < envVarCount; j++)
            {
                EnvVar* ev = envVars[j];
                if (ev == NULL)
                {
                    break;
                }

                if (ev->key == key)
                {
                    ev->value = newValue;
                    break;
                }
            }
        }

        if (saveEnvVarsCallback != NULL)
        {
            saveEnvVarsCallback(envVars, envVarCount);
        }

        if (envVarStorage != NULL)
        {
            envVarStorage->saveVars(envVars, envVarCount);
        }

        server->send(200);
        delay(100);

#ifdef ESP32
        ESP.restart();
#elif ESP8266
        ESP.reset();
#endif
    }

    inline void enableEnvModule()
    {
        server->on(F("/config/save"), HTTP_POST, []()
        {
            VALIDATE_AUTH();
            saveEnv();
        });

        server->on(F("/config/env"), HTTP_GET, []()
        {
            VALIDATE_AUTH();

            char buf[envSize()+1];
            for (uint8_t i = 0; i < envVarCount; i++)
            {
                EnvVar* ev = envVars[i];

                char bufKey[sizeWithEscaping(ev->key.c_str())];
                char bufVal[sizeWithEscaping(ev->value.c_str())];

                escape(bufKey, ev->key.c_str());
                escape(bufVal, ev->value.c_str());

                strcat(buf, bufKey);
                strcat(buf, ":");
                strcat(buf, bufVal);
                strcat(buf, ":;");
            }

            server->send(200, "text/plain", buf);
        });
    }
}

#endif //ESP_CONFIG_PAGE_ENV_H
