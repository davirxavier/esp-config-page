//
// Created by xav on 2/22/25.
//

#ifndef ESP_CONFIG_PAGE_OTA_H
#define ESP_CONFIG_PAGE_OTA_H

#ifdef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
#include <esp_ota_ops.h>
#include <esp_partition.h>
#warning  "Using ESP-IDF OTA API instead of Arduino's"
#endif

namespace ESP_CONFIG_PAGE
{
#ifdef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
    inline size_t writeOffset = 0;
    inline const esp_partition_t *otaPartition = nullptr;
    inline esp_ota_handle_t otaHandle = 0;
#endif

#ifndef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
#ifdef ESP32
#define GET_UPDATE_ERROR_STR Update.errorString()
#elif ESP8266
#define GET_UPDATE_ERROR_STR Update.getErrorString().c_str()
#endif
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

#ifdef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
            if (isFilesystem)
            {
                writeOffset = 0;

                otaPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
                if (otaPartition == NULL) {
                    LOGN("Error when starting update.");
                    server->send(500, "text/plain", "Error starting update, filesystem partition is null");
                    return;
                }

                esp_err_t err = esp_partition_erase_range(otaPartition, 0, otaPartition->size);
                if (err != ESP_OK) {
                    LOGN("Error when starting update.");
                    server->send(500, "text/plain", "Error when erasing filesystem partition");
                    return;
                }
            }
            else
            {
                otaPartition = esp_ota_get_next_update_partition(NULL);
                if (otaPartition == NULL)
                {
                    LOGN("Error when starting update.");
                    server->send(500, "text/plain", "Start update error, next update partition is null");
                    return;
                }

                esp_err_t err = esp_ota_begin(otaPartition, OTA_SIZE_UNKNOWN, &otaHandle);
                if (err != ESP_OK) {
                    LOGN("Error when starting update.");
                    server->send(500, "text/plain", "Error when setting up firmware ota update");
                    return;
                }
            }
#else
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
#endif
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
            LOGN("Update write.");

#ifdef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
            if (isFilesystem)
            {
                if (!otaPartition)
                {
                    LOGN("Error when writing to update.");
                    server->send(500, "text/plain", "Error writing to partition, is null");
                    return;
                }

                esp_err_t err = esp_partition_write(otaPartition, writeOffset, upload.buf, upload.currentSize);
                if (err != ESP_OK) {
                    LOGN("Error when writing to update.");
                    server->send(500, "text/plain", String(err));
                    return;
                }

                writeOffset += upload.currentSize;
            }
            else
            {
                if (otaHandle == 0)
                {
                    LOGN("Error when writing to update.");
                    server->send(500, "text/plain", "Ota handle is null");
                    return;
                }

                esp_ota_write(otaHandle, (const void*) upload.buf, upload.currentSize);
            }
#else
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
            {
                LOGN("Error when writing update.");
                server->send(400, "text/plain", GET_UPDATE_ERROR_STR);
            }
#endif
        }
        else if (upload.status == UPLOAD_FILE_END)
        {
            LOGN("Update ended.");

#ifdef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
            if (isFilesystem)
            {
                server->send(200, "text/plain", "");
                esp_restart();
            }
            else {
                if (otaHandle == 0)
                {
                    LOGN("Error when writing to update.");
                    server->send(500, "text/plain", "Ota handle is null");
                    esp_restart();
                    return;
                }

                esp_err_t err = esp_ota_end(otaHandle);
                if (err != ESP_OK) {
                    LOGF("Ota update unsuccessful, err: %x\n", err);
                    server->send(500, "text/plain", String("Ota update unsuccessful, err: ") + err);
                    esp_restart();
                    return;
                }

                err = esp_ota_set_boot_partition(otaPartition);
                if (err != ESP_OK) {
                    LOGF("Error setting ota partition as new boot partition: %x\n", err);
                    server->send(500, "text/plain", "Error setting ota partition as new boot partition.");
                    esp_restart();
                    return;
                }

                LOGN("Update successful");
            }
#else
            if (Update.end(true))
            {
                server->send(200);
            }
            else
            {
                LOGN("Error finishing update.");
                server->send(400, "text/plain", GET_UPDATE_ERROR_STR);
            }
#endif
        }
        delay(10);
    }

    inline void otaEnd()
    {
        server->send(200, "text/plain", "");
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
                       // otaEnd();
                   }, []()
                   {
                       VALIDATE_AUTH();
                       ota(true);
                   });
    }
}

#endif //ESP_CONFIG_PAGE_OTA_H
