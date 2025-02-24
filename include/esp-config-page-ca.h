//
// Created by xav on 2/22/25.
//

#ifndef ESP_CONFIG_PAGE_CA_H
#define ESP_CONFIG_PAGE_CA_H

#include "esp-config-defines.h"

namespace ESP_CONFIG_PAGE
{
    struct CustomAction
    {
        const String key;
        std::function<void(WEBSERVER_T& server)> handler;
    };

    CustomAction** customActions;
    uint8_t customActionsCount = 0;
    uint8_t maxCustomActions = 0;

    inline int caSize()
    {
        int infoSize = 0;

        for (uint8_t i = 0; i < customActionsCount; i++)
        {
            CustomAction* ca = customActions[i];
            infoSize += sizeWithEscaping(ca->key.c_str()) + 4;
        }

        return infoSize;
    }

    inline void tiggerCustomAction()
    {
        if (customActionsCount == 0)
        {
            return;
        }

        CustomAction* ca = NULL;
        String body = server->arg(F("plain"));

        for (uint8_t i = 0; i < customActionsCount; i++)
        {
            if (String(customActions[i]->key) == body)
            {
                ca = customActions[i];
            }
        }

        if (ca != NULL)
        {
            ca->handler(*server);
        }
        else
        {
            server->send(200);
        }
    }

    /**
     * Adds a custom action to the webpage.
     *
     * @param key - name of the action, has to be unique for all added actions.
     * @param handler - handler function for the action.
     */
    inline void addCustomAction(String key, std::function<void(WEBSERVER_T& server)> handler)
    {
        LOGF("Adding action %s.\n", key.c_str());
        if (customActionsCount + 1 > maxCustomActions)
        {
            maxCustomActions = maxCustomActions == 0 ? 1 : ceil(maxCustomActions * 1.5);
            LOGF("Actions array overflow, increasing size to %d.\n", maxCustomActions);
            customActions = (CustomAction**) realloc(customActions, sizeof(CustomAction*) * maxCustomActions);
        }

        customActions[customActionsCount] = new CustomAction{key, handler};
        customActionsCount++;
    }

    inline void getCa()
    {
        char buf[caSize()+1];
        for (uint8_t i = 0; i < customActionsCount; i++)
        {
            CustomAction* ca = customActions[i];

            char bufKey[sizeWithEscaping(ca->key.c_str())];
            escape(bufKey, ca->key.c_str());

            strcat(buf, bufKey);
            strcat(buf, ";");
        }

        server->send(200, "text/plain", buf);
    }

    inline void enableCustomActionsModule()
    {
        REGISTER_SERVER_METHOD(F("/config/customa"), HTTP_POST, tiggerCustomAction);
        REGISTER_SERVER_METHOD(F("/config/customa"), HTTP_GET, getCa);
    }
}

#endif //ESP_CONFIG_PAGE_CA_H
