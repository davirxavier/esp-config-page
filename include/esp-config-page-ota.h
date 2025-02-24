//
// Created by xav on 2/22/25.
//

#ifndef ESP_CONFIG_PAGE_OTA_H
#define ESP_CONFIG_PAGE_OTA_H

namespace ESP_CONFIG_PAGE
{
    inline const char* getUpdateErrorStr()
    {
#ifdef ESP32
        return Update.errorString();
#elif ESP8266
        return Update.getErrorString().c_str();
#endif
    }

    inline void ota(bool isFilesystem)
    {
        LOGN("OTA upload receiving, starting update process.");

#ifdef ENABLE_LOGGING
        ESP_CONFIG_PAGE_LOGGING::disableLogging();
#endif

        HTTPUpload& upload = server->upload();
        int command = U_FLASH;
        if (isFilesystem)
        {
            LOGN("OTA update set to FILESYSTEM mode.");
#ifdef ESP32
            command = U_SPIFFS;
#elif ESP8266
            command = U_FS;
#endif
        }

        if (upload.status == UPLOAD_FILE_START)
        {
            LOGN("Starting OTA update.");
#ifdef ESP8266
            WiFiUDP::stopAll();
#endif

#ifdef ESP32
            uint32_t maxSpace = UPDATE_SIZE_UNKNOWN;
#elif ESP8266
            uint32_t maxSpace = 0;
            if (command == U_FLASH) {
                maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            } else {
                maxSpace = FS_end - FS_start;
            }

            Update.runAsync(true);
#endif

            LOGF("Calculate max space is %d.\n", maxSpace);
            if (!Update.begin(maxSpace, command))
            {
                // start with max available size
                LOGN("Error when starting update.");
                server->send(400, "text/plain", getUpdateErrorStr());
            }
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
            LOGN("Update write.");
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
            {
                LOGN("Error when writing update.");
                server->send(400, "text/plain", getUpdateErrorStr());
            }
        }
        else if (upload.status == UPLOAD_FILE_END)
        {
            LOGN("Update ended.");
            if (Update.end(true))
            {
                server->send(200);
            }
            else
            {
                LOGN("Error finishing update.");
                server->send(400, "text/plain", getUpdateErrorStr());
            }
        }
        yield();
    }

    inline void otaEnd()
    {
        server->sendHeader("Connection", "close");
        server->send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        LOGN("OTA update finished, restarting.");
        delay(100);
        ESP.restart();
    }

    inline void enableOtaModule()
    {
        server->on(F("/config/update/firmware"), HTTP_POST, []()
                   {
                       VALIDATE_AUTH();
                       otaEnd();
                   }, []()
                   {
                       VALIDATE_AUTH();
                       ota(false);
                   });

        server->on(F("/config/update/filesystem"), HTTP_POST, []()
                   {
                       VALIDATE_AUTH();
                       otaEnd();
                   }, []()
                   {
                       VALIDATE_AUTH();
                       ota(true);
                   });
    }
}

#endif //ESP_CONFIG_PAGE_OTA_H
