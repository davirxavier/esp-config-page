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
         */
        EnvVar(const char *key)
        {
            EnvVar(key, nullptr);
        };

        /**
         * @param key - environment variable name and search key, should be unique between all environment variables.
         * @param value - initial value for the environment variable.
         */
        EnvVar(const char *key, char *value) : key(key)
        {
            if (value == nullptr)
            {
                value = (char*) malloc(1);
                value[0] = 0;
            }
        };
        const char *key;
        char *value;
    };

    EnvVar** envVars;
    uint8_t envVarCount = 0;
    uint8_t maxEnvVars = 0;
    void (*saveEnvVarsCallback)(EnvVar** envVars, uint8_t envVarCount) = nullptr;
    KeyValueStorage* envVarStorage = nullptr;

    inline int envSize()
    {
        int infoSize = 0;

        for (uint8_t i = 0; i < envVarCount; i++)
        {
            EnvVar* ev = envVars[i];
            infoSize += sizeWithEscaping(ev->key) + sizeWithEscaping(ev->value) + 4;
        }

        return infoSize;
    }

    /**
     * Set the type persistent environment variables storage for the library, as well as use the instance to recover any saved variables (if there are any).
     *
     * @param storage - storage instance for environment variables. Default for this libraty is LittleFSEnvVarStorage, but can be any subclass of EnvVarStorage.
     */
    inline void setAndUpdateEnvVarStorage(KeyValueStorage* storage)
    {
        envVarStorage = storage;

        if (envVarStorage != nullptr)
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

            LOGN("Recoving env vars from storage.");
            for (uint8_t i = 0; i < envVarCount; i++)
            {
                EnvVar *var = envVars[i];
                if (var == nullptr)
                {
                    continue;
                }

                char *value = envVarStorage->recover(var->key);
                if (value == nullptr)
                {
                    LOGF("Variable %s not found in storage, skipping.\n", var->key);
                    continue;
                }

                if (var->value != nullptr)
                {
                    free(var->value);
                }
                var->value = value;
            }
        }
    }

    /**
     * Adds an environment variable to the webpage.
     *
     * @param ev environment variable to be added. EnvVar key value has to be unique between all added variables.
     */
    inline void addEnvVar(EnvVar* ev)
    {
        LOGF("Adding env var %s.\n", ev->key);

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
                if (ev == nullptr)
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

        if (saveEnvVarsCallback != nullptr)
        {
            saveEnvVarsCallback(envVars, envVarCount);
        }

        if (envVarStorage != nullptr)
        {
            for (uint8_t i = 0; i < envVarCount; i++)
            {
                EnvVar *ev = envVars[i];
                if (ev == nullptr)
                {
                    continue;
                }

                envVarStorage->save(ev->key, ev->value);
            }
        }

        server->send(200);
        delay(100);

#ifdef ESP32
        ESP.restart();
#elif ESP8266
        ESP.reset();
#endif
    }

    inline void getEnv()
    {
        char buf[envSize()+1];
        for (uint8_t i = 0; i < envVarCount; i++)
        {
            EnvVar* ev = envVars[i];

            char bufKey[sizeWithEscaping(ev->key)];
            char bufVal[sizeWithEscaping(ev->value)];

            escape(bufKey, ev->key);
            escape(bufVal, ev->value);

            strcat(buf, bufKey);
            strcat(buf, ":");
            strcat(buf, bufVal);
            strcat(buf, ":;");
        }

        server->send(200, "text/plain", buf);
    }

    inline void enableEnvModule()
    {
        REGISTER_SERVER_METHOD(F("/config/save"), HTTP_POST, saveEnv);
        REGISTER_SERVER_METHOD(F("/config/env"), HTTP_GET, getEnv);
    }
}

#endif //ESP_CONFIG_PAGE_ENV_H
