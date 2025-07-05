//
// Created by xav on 2/22/25.
//

#ifndef ESP_CONFIG_PAGE_OTA_H
#define ESP_CONFIG_PAGE_OTA_H

namespace ESP_CONFIG_PAGE
{

#ifdef ESP32
#define GET_UPDATE_ERROR_STR Update.errorString()
#elif ESP8266
#define GET_UPDATE_ERROR_STR Update.getErrorString().c_str()
#endif

    inline void ota(bool isFilesystem)
    {
        ESP_CONFIG_PAGE_LOGGING::disableLogging();

        HTTPUpload& upload = server->upload();

#ifdef ESP32
        LOGF("Upload info: current size is %d, total sent size is %d, status is %d, name is %s\n", upload.currentSize, upload.totalSize, upload.status, upload.name.c_str());
#elif ESP8266
        LOGF("Upload info: current size is %d, total sent size is %d, total file size is %d, status is %d, name is %s\n", upload.currentSize, upload.totalSize, upload.contentLength, upload.status, upload.name.c_str());
#endif

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

        LOGF("Upload status: %d\n", upload.status);
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
            close_all_fs();
#endif

            LOGF("Calculate max space is %d.\n", maxSpace);
            if (!Update.begin(maxSpace, command))
            {
                // start with max available size
                LOGN("Error when starting update.");
                server->send(400, "text/plain", GET_UPDATE_ERROR_STR);
            }
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
            LOGN("Update write.");
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
            {
                LOGN("Error when writing update.");
                server->send(400, "text/plain", GET_UPDATE_ERROR_STR);
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
                server->send(400, "text/plain", GET_UPDATE_ERROR_STR);
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
