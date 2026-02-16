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
        EnvVar(const char *key, char *value) : key(key), value(value){}

        /**
         * @param key - environment variable name and search key, should be unique between all environment variables.
         */
        EnvVar(const char *key): key(key), value(nullptr){}

        const char *key;
        char *value;
    };

    EnvVar** envVars;
    uint8_t envVarCount = 0;
    uint8_t maxEnvVars = 0;
    KeyValueStorage* envVarStorage = nullptr;

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
            LOGN("Recovering env vars from storage.");
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
            envVars = (EnvVar**) realloc(envVars, sizeof(EnvVar*) * maxEnvVars);
        }

        envVars[envVarCount] = ev;
        envVarCount++;
    }

    inline void saveEnv(REQUEST_T request) // TODO
    {
        if (envVarCount == 0)
        {
            request->send(200);
            return;
        }

        String body = request->arg("plain");

        unsigned int maxLineLength = getMaxLineLength(body.c_str()) + 1;
        char buf[maxLineLength];
        unsigned int currentChar = 0;
        unsigned int bodyLen = body.length();

        char currentKey[maxLineLength];
        bool isKey = true;

        for (unsigned int i = 0; i < bodyLen; i++)
        {
            char c = body[i];

            if (c == '\n')
            {
                buf[currentChar] = 0;

                if (isKey)
                {
                    strcpy(currentKey, buf);
                }
                else
                {
                    for (uint8_t j = 0; j < envVarCount; j++)
                    {
                        EnvVar *var = envVars[j];
                        if (var != nullptr && strcmp(var->key, currentKey))
                        {
                            envVarStorage->save(currentKey, buf);
                            break;
                        }
                    }
                }

                isKey = !isKey;
                currentChar = 0;
            }
            else
            {
                buf[currentChar] = c;
                currentChar++;
            }
        }

        request->send(200);
        delay(200);

#ifdef ESP32
        ESP.restart();
#elif ESP8266
        ESP.reset();
#endif
    }

    inline void getEnv(REQUEST_T request)
    {
        int infoSize = 0;
        for (uint8_t i = 0; i < envVarCount; i++)
        {
            EnvVar* ev = envVars[i];
            infoSize += 2 + strlen(ev->key) + (ev->value != nullptr ? strlen(ev->value) : 0);
        }

        ResponseContext c{};
        initResponseContext(200, "text/plain", infoSize, c);
        startResponse(request, c);

        for (uint8_t i = 0; i < envVarCount; i++)
        {
            EnvVar* ev = envVars[i];

            if (ev == nullptr)
            {
                continue;
            }

            writeResponse(ev->key, c);
            writeResponse("\n", c);
            writeResponse(ev->value != nullptr ? ev->value : "", c);
            writeResponse("\n", c);
        }

        endResponse(request, c);
    }

    inline void enableEnvModule()
    {
        addServerHandler((char*) F("/config/save"), HTTP_POST, saveEnv);
        addServerHandler((char*) F("/config/env"), HTTP_GET, getEnv);
    }
}

#endif //ESP_CONFIG_PAGE_ENV_H
