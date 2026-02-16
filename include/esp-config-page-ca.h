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
        const char *key;
        std::function<void(REQUEST_T& server)> handler;
    };

    CustomAction** customActions;
    uint8_t customActionsCount = 0;
    uint8_t maxCustomActions = 0;

    inline void triggerCustomAction(REQUEST_T request) // TODO
    {
        if (customActionsCount == 0)
        {
            return;
        }

        CustomAction* ca = nullptr;
        String body = request->arg(F("plain"));

        for (uint8_t i = 0; i < customActionsCount; i++)
        {
            if (String(customActions[i]->key) == body)
            {
                ca = customActions[i];
            }
        }

        if (ca != nullptr)
        {
            LOGF("Triggering custom action: %s\n", ca->key);
            ca->handler(request);
            request->send(200);
        }
        else
        {
            request->send(404);
        }
    }

    /**
     * Adds a custom action to the webpage.
     *
     * @param key - name of the action, has to be unique for all added actions.
     * @param handler - handler function for the action.
     */
    inline void addCustomAction(const char *key, std::function<void(REQUEST_T& server)> handler)
    {
        LOGF("Adding action %s.\n", key);
        if (customActionsCount + 1 > maxCustomActions)
        {
            maxCustomActions = maxCustomActions == 0 ? 1 : ceil(maxCustomActions * 1.5);
            LOGF("Actions array overflow, increasing size to %d.\n", maxCustomActions);
            customActions = (CustomAction**) realloc(customActions, sizeof(CustomAction*) * maxCustomActions);
        }

        customActions[customActionsCount] = new CustomAction{key, handler};
        customActionsCount++;
    }

    inline void getCa(REQUEST_T request)
    {
        int infoSize = 0;
        for (uint8_t i = 0; i < customActionsCount; i++)
        {
            CustomAction* ca = customActions[i];
            infoSize += strlen(ca->key) + 1;
        }

        ResponseContext c{};
        initResponseContext(200, "text/plain", infoSize, c);
        startResponse(request, c);

        for (uint8_t i = 0; i < customActionsCount; i++)
        {
            CustomAction* ca = customActions[i];
            writeResponse(ca->key, c);
            writeResponse("\n", c);
        }

        endResponse(request, c);
    }

    inline void enableCustomActionsModule()
    {
        addServerHandler("/config/customa", HTTP_POST, triggerCustomAction);
        addServerHandler("/config/customa", HTTP_GET, getCa);
    }
}

#endif //ESP_CONFIG_PAGE_CA_H
