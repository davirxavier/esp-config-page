//
// Created by xav on 2/22/25.
//

#ifndef ESP_CONFIG_PAGE_OTA_H
#define ESP_CONFIG_PAGE_OTA_H

#ifdef ESP32
#include <mbedtls/md5.h>
#elif ESP8266
#include <md5.h>
#endif

#ifdef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
#include <esp_ota_ops.h>
#include <esp_partition.h>
#warning  "Using ESP-IDF OTA API instead of Arduino's"
#else
#include <Update.h>
#endif

#ifdef ESP32

#ifdef mbedtls_md5_starts_ret
#define ESP_CONP_MD5_START(ctx) mbedtls_md5_starts_ret(ctx)
#else
#define ESP_CONP_MD5_START(ctx) mbedtls_md5_starts(ctx)
#endif

#ifdef mbedtls_md5_update_ret
#define ESP_CONP_MD5_UPDATE(ctx, data, len) esp_md5_update_ret(ctx, data, len)
#else
#define ESP_CONP_MD5_UPDATE(ctx, data, len) esp_md5_update(ctx, data, len)
#endif

#ifdef mbedtls_md5_finish_ret
#define ESP_CONP_MD5_END(ctx, res) mbedtls_md5_finish_ret(ctx, res)
#else
#define ESP_CONP_MD5_END(ctx, res) mbedtls_md5_finish(ctx, res)
#endif

#define ESP_CONP_MD5_CTX_T mbedtls_md5_context

#elif ESP8266
#define ESP_CONP_MD5_START(ctx) MD5Init(ctx)
#define ESP_CONP_MD5_UPDATE(ctx, data, len) MD5Update(ctx, data, len)
#define ESP_CONP_MD5_END(ctx, res) MD5Final(res, ctx)
#define ESP_CONP_MD5_CTX_T md5_context_t
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


#ifdef ESP32_CONP_OTA_USE_WEBSOCKETS
    WebSocketsServer otaWsServer(ESP32_CONP_OTA_WS_PORT);
    ESP_CONFIG_PAGE_LOGGING::ConnectedClient *otaClient = nullptr;
    unsigned long lastWsServerUpdate = 0;
#else
    REQUEST_T currentRequest = nullptr;
#endif

    unsigned long otaTimer = 0;
    unsigned long otaTimeout = 60000;
    bool otaStarted = false;
    char otaMd5[33]{};
    bool otaMd5Started = false;
    bool isOtaFilesystem = false;
    bool otaRestart = false;

    using OtaStartCallback = std::function<void()>;
    inline OtaStartCallback otaStartCallback = nullptr;

    ESP_CONP_MD5_CTX_T otaMd5Ctx;

    inline void otaChecksumStart()
    {
        ESP_CONP_MD5_START(&otaMd5Ctx);
        otaMd5Started = true;
    }

    inline void otaChecksumWrite(const uint8_t* data, size_t len)
    {
        ESP_CONP_MD5_UPDATE(&otaMd5Ctx, data, len);
    }

    inline void otaChecksumFree()
    {
        if (!otaMd5Started)
        {
            return;
        }

#ifdef ESP32
        mbedtls_md5_free(&otaMd5Ctx);
#elif ESP8266
        otaMd5Ctx = ESP_CONP_MD5_CTX_T();
#endif

        otaMd5Started = false;
    }

    inline bool otaChecksumVerify(const char* expectedMd5)
    {
        unsigned char md5Result[16];
        ESP_CONP_MD5_END(&otaMd5Ctx, md5Result);
        otaChecksumFree();

        char md5String[33];
        for (int i = 0; i < 16; i++) {
            sprintf(&md5String[i * 2], "%02x", md5Result[i]);
        }

        LOGF("Verifying OTA checksum, expected/actual: %s/%s\n", expectedMd5, md5String);
        return strcasecmp(md5String, expectedMd5) == 0;
    }

    enum OtaEventType
    {
        AUTH = 'A',
        RECONNECTION = 'R',
        START_FILESYSTEM = 'U',
        START_FIRMWARE = 'u',
        WRITE = 'W',
        END = 'N',

        ERROR = 'E',
        SUCCESS = 'S',
        AUTH_SUCCESS = 'a',
        NEXT_CHUNK = 'C',
        PING = 'P',
    };

#ifdef ESP32_CONP_OTA_USE_WEBSOCKETS
    inline void registerOtaClient(uint8_t id)
    {
        if (otaClient != nullptr)
        {
            delete otaClient;
            otaClient = nullptr;
        }

        otaClient = new ESP_CONFIG_PAGE_LOGGING::ConnectedClient(id, millis(), false);
    }

    inline void releaseOtaClient()
    {
        if (otaClient == nullptr)
        {
            return;
        }

        if (otaWsServer.clientIsConnected(otaClient->id))
        {
            otaWsServer.disconnect(otaClient->id);
        }

        delete otaClient;
        otaClient = nullptr;
    }

    inline bool hasOtaClient()
    {
        return otaClient != nullptr;
    }
#endif

    inline void otaAbort()
    {
        if (!otaStarted)
        {
            return;
        }

        LOGN("Aborting OTA update.");

#ifdef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
        esp_ota_abort(otaHandle);
#elifdef ESP32
        Update.abort();
#endif

        otaRestart = true;
    }

    inline void sendResponse(const char *status, OtaEventType eventType = SUCCESS)
    {
#ifdef ESP32_CONP_OTA_USE_WEBSOCKETS
        char toSend[strlen(status) + 3]{};
        snprintf(toSend, sizeof(toSend), "%c%s", eventType, status);
        otaWsServer.sendTXT(otaClient->id, toSend);
#else
        sendInstantResponse(CONP_STATUS_CODE::OK, status, currentRequest);
#endif
    }

    inline void sendErrorResponse(const char *header, const char *err, OtaEventType eventType = ERROR)
    {
        char toSend[strlen(header) + strlen(err) + 3]{};
        snprintf(toSend, sizeof(toSend), "%c%s%s", eventType, header, err);

#ifdef ESP32_CONP_OTA_USE_WEBSOCKETS
        otaWsServer.sendTXT(otaClient->id, toSend);
        releaseOtaClient();
#else
        sendInstantResponse(CONP_STATUS_CODE::BAD_REQUEST, toSend, currentRequest);
#endif

        otaAbort();
    }

    inline void otaStart(const char *hash)
    {
        ESP_CONFIG_PAGE_LOGGING::disableLogging();
        LOGN("OTA upload start.");

        if (otaStartCallback)
        {
            otaStartCallback();
        }

#ifndef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
        int command = U_FLASH;
        if (isOtaFilesystem)
        {
            LOGN("OTA update set to FILESYSTEM mode.");
#ifdef ESP32
            command = U_SPIFFS;
#elif ESP8266
            command = U_FS;
#endif
        }

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
#endif

        if (hash == nullptr)
        {
            LOGN("No update MD5 sent.");
            memset(otaMd5, 0, sizeof(otaMd5));
        }
        else
        {
            LOGF("Update MD5 hash: %s\n", hash);
            snprintf(otaMd5, sizeof(otaMd5), "%s", hash);
            otaChecksumStart();
        }

#ifdef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
        if (isOtaFilesystem)
        {
            writeOffset = 0;

            otaPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
            if (otaPartition == NULL) {
                LOGN("Error when starting update.");
                sendErrorResponse("", "Error starting update, filesystem partition is null");
                return;
            }

            esp_err_t err = esp_partition_erase_range(otaPartition, 0, otaPartition->size);
            if (err != ESP_OK) {
                LOGN("Error when starting update.");
                sendErrorResponse("", "Error when erasing filesystem partition");
                return;
            }
        }
        else
        {
            otaPartition = esp_ota_get_next_update_partition(NULL);
            if (otaPartition == NULL)
            {
                LOGN("Error when starting update.");
                sendErrorResponse("", "Start update error, next update partition is null");
                return;
            }

            esp_err_t err = esp_ota_begin(otaPartition, OTA_SIZE_UNKNOWN, &otaHandle);
            if (err != ESP_OK) {
                LOGN("Error when starting update.");
                sendErrorResponse("", "Error when setting up firmware ota update");
                return;
            }
        }
#else
        if (!Update.begin(maxSpace, command))
        {
            // start with max available size
            LOGF("Error when starting update: %s\n", GET_UPDATE_ERROR_STR);
            sendErrorResponse("Error starting update: ", GET_UPDATE_ERROR_STR);
        }
#endif

#ifdef ESP32_CONP_OTA_USE_WEBSOCKETS
        otaTimer = millis();
        otaStarted = true;
#endif
    }

    inline void otaWrite(uint8_t *buf, size_t bufSize)
    {
        otaChecksumWrite(buf, bufSize);

#ifdef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
        if (isOtaFilesystem)
        {
            if (!otaPartition)
            {
                LOGN("Error when writing to update.");
                sendErrorResponse("", "Error writing to partition, is null");
                return;
            }

            esp_err_t err = esp_partition_write(otaPartition, writeOffset, buf, bufSize);
            if (err != ESP_OK) {
                LOGN("Error when writing to update.");
                sendErrorResponse("Error writing: ", String(err).c_str());
                return;
            }

            writeOffset += bufSize;
        }
        else
        {
            if (otaHandle == 0)
            {
                LOGN("Error when writing to update.");
                sendErrorResponse("Error writing: ", "Ota handle is null");
                return;
            }

            esp_ota_write(otaHandle, buf, bufSize);
        }
#else
        size_t written = Update.write(buf, bufSize);
        if (written != bufSize)
        {
            LOGF("Error when writing update, written only %zu of %zu bytes, error str: %s\n", written, bufSize, GET_UPDATE_ERROR_STR);
            sendErrorResponse("Error with ota write: ", GET_UPDATE_ERROR_STR);
            return;
        }
#endif

#ifdef ESP32_CONP_OTA_USE_WEBSOCKETS
        otaTimer = millis();
        sendResponse("", NEXT_CHUNK);
#endif
    }

    inline void otaFinish()
    {
        LOGN("Finishing ota update.");

        if (strlen(otaMd5) > 0 && !otaChecksumVerify(otaMd5))
        {
            constexpr char err[] = "Error when finishing ota update: partition checksum validation failed.";
            LOGN(err);
            sendErrorResponse("", err);
            return;
        }

#ifdef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
        if (!isOtaFilesystem)
        {
            if (otaHandle == 0)
            {
                LOGN("Error when writing to update.");
                sendErrorResponse("", "Finish update error: Ota handle is null");
                esp_restart();
                return;
            }

            esp_err_t err = esp_ota_end(otaHandle);
            if (err != ESP_OK) {
                LOGF("Ota update unsuccessful, err: %x\n", err);
                sendErrorResponse("Ota update unsuccessful, err: ", String(err).c_str());
                esp_restart();
                return;
            }

            err = esp_ota_set_boot_partition(otaPartition);
            if (err != ESP_OK) {
                LOGF("Error setting ota partition as new boot partition: %x\n", err);
                sendErrorResponse("", "Error setting ota partition as new boot partition.");
                esp_restart();
                return;
            }

            LOGN("Update successful");
        }
#else
        if (Update.end(true))
        {
            LOGN("Update successful");
        }
        else
        {
            LOGN("Error finishing update.");
            sendErrorResponse("Error finishing update: ", GET_UPDATE_ERROR_STR);
        }
#endif

        sendResponse("Update successful");
        otaRestart = true;
    }

    enum UploadEventType
    {
        UPLOAD_START,
        UPLOAD_WRITE,
        UPLOAD_END,
        UPLOAD_ABORT,
    };

#if !defined(ESP32_CONP_OTA_USE_WEBSOCKETS)
    inline void handleUpdate(bool filesystem, REQUEST_T request, uint8_t *data, size_t len, UploadEventType event, bool writeOnStart = false)
    {
        isOtaFilesystem = filesystem;
        // LOGF("OTA event: %d, len: %zu\n", event, len);

        if (event == UPLOAD_START)
        {
            char md5[33]{};
            getHeader(request, "x-md5", md5, sizeof(md5));
            currentRequest = request;
            otaStarted = true;
            otaStart(strlen(md5) != 32 ? nullptr : md5);
        }

        if ((event == UPLOAD_WRITE || (writeOnStart && event == UPLOAD_START)) && len > 0)
        {
            otaWrite(data, len);
        }

        if (event == UPLOAD_END)
        {
            otaFinish();
            LOGN("OTA finished");
        }

        if (event == UPLOAD_ABORT)
        {
            otaAbort();
        }
    }

    inline UploadEventType mapValuesToUploadEvent(
#ifdef ESP_CONP_ASYNC_WEBSERVER
        size_t index)
    {
        UploadEventType event = UPLOAD_WRITE;
        if (index == 0)
        {
            event = UPLOAD_START;
        }
        return event;
    }
#else
        HTTPUpload upload)
    {
        if (upload.status == UPLOAD_FILE_START)
        {
            return UPLOAD_START;
        }
        else if (upload.status == UPLOAD_FILE_END)
        {
            return UPLOAD_END;
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
            return UPLOAD_WRITE;
        }

        return UPLOAD_ABORT;
    }
#endif

#ifdef ESP_CONP_HTTPS_SERVER
    inline void handleRequest(REQUEST_T request, bool filesystem)
    {
        size_t bufferSize = 1024 * 16;
        int tries = 0;
        char *buffer = nullptr;
        while (buffer == nullptr && tries < 8)
        {
            buffer = (char*) malloc(bufferSize/2);
            delay(200);
            tries++;
        }

        if (buffer == nullptr)
        {
            sendInstantResponse(CONP_STATUS_CODE::INTERNAL_SERVER_ERROR, "could not acquire buffer", request);
            return;
        }

        handleUpdate(filesystem, request, {}, 0, UPLOAD_START);

        size_t received = 0;
        while ((received = httpd_req_recv(request, buffer, bufferSize)) > 0)
        {
            handleUpdate(filesystem, request, (uint8_t*) buffer, received, UPLOAD_WRITE);
        }

        free(buffer);
        handleUpdate(filesystem, request, {}, 0, UPLOAD_END);
    }
#endif

#endif

    inline void enableOtaModule()
    {
#ifdef ESP32_CONP_OTA_USE_WEBSOCKETS
        otaWsServer.setMaxDataSize(ESP_CONP_WS_BUFFER_SIZE);

        otaWsServer.onEvent([](uint8_t clientId, WStype_t type, uint8_t *payload, size_t length)
        {
            if (type == WStype_CONNECTED)
            {
                if (hasOtaClient())
                {
                    sendErrorResponse("", "socket full");
                    return;
                }

                LOGF("OTA client connected with id %d\n", clientId);
                registerOtaClient(clientId);
            }
            else if (type == WStype_DISCONNECTED && otaClient != nullptr)
            {
                LOGF("OTA client disconnected with id %d\n", clientId);
                releaseOtaClient();
            }
            else if (type == WStype_TEXT || type == WStype_BIN)
            {
                if (!hasOtaClient())
                {
                    return;
                }

                if (otaClient->id != clientId)
                {
                    sendErrorResponse("", "socket full");
                    return;
                }

                char eventTypeChar = payload[0];
                uint8_t *payloadWithoutEvent = length == 0 ? payload : payload+1;
                size_t lengthWithoutEvent = length == 0 ? 0 : length-1;

                switch (eventTypeChar)
                {
                case OtaEventType::RECONNECTION:
                case OtaEventType::AUTH:
                    {
                        if (eventTypeChar == OtaEventType::AUTH && otaStarted)
                        {
                            LOGN("Had another OTA running already, aborting.");
                            otaAbort();
                        }

                        char usernameAndPassword[lengthWithoutEvent+1]{};
                        memcpy(usernameAndPassword, payloadWithoutEvent, lengthWithoutEvent);

                        char *separator = strchr(usernameAndPassword, ':');
                        if (separator == nullptr)
                        {
                            return;
                        }

                        separator[0] = 0;
                        char *password = separator+1;

                        if (strcmp(usernameAndPassword, ESP_CONFIG_PAGE::username.c_str()) != 0 ||
                            strcmp(password, ESP_CONFIG_PAGE::password.c_str()) != 0)
                        {
                            sendErrorResponse("", "Invalid auth.");
                            return;
                        }

                        if (otaClient != nullptr)
                        {
                            otaClient->authed = true;
                            if (otaStarted)
                            {
                                sendResponse("", NEXT_CHUNK);
                            }
                            else
                            {
                                sendResponse("", AUTH_SUCCESS);
                            }
                        }
                        break;
                    }
                case OtaEventType::START_FIRMWARE:
                    {
                        if (!otaStarted)
                        {
                            isOtaFilesystem = false;
                            otaStart(lengthWithoutEvent == 0 ? nullptr : (char*) payloadWithoutEvent);
                            sendResponse("", OtaEventType::NEXT_CHUNK);
                        }
                        break;
                    }
                case OtaEventType::START_FILESYSTEM:
                    {
                        if (!otaStarted)
                        {
                            isOtaFilesystem = true;
                            otaStart(lengthWithoutEvent == 0 ? nullptr : (char*) payloadWithoutEvent);
                            sendResponse("", OtaEventType::NEXT_CHUNK);
                        }
                        break;
                    }
                case OtaEventType::WRITE:
                    {
                        otaWrite(payloadWithoutEvent, lengthWithoutEvent);
                        break;
                    }
                case OtaEventType::END:
                    {
                        otaFinish();
                        break;
                    }
                default:
                    {
                        LOGF("Invalid event: %c\n", eventTypeChar);
                        char str[] = {eventTypeChar, 0};
                        sendErrorResponse("Invalid event received: ", str);
                        break;
                    }
                }
            }
        });

        otaWsServer.begin();
#else
        const char firmwareUri[] = "/config/update/firmware";
        const char filesystemUri[] = "/config/update/filesystem";

#ifdef ESP_CONP_ASYNC_WEBSERVER
        server->on(AsyncURIMatcher::exact(firmwareUri),
            HTTP_POST,
            [](REQUEST_T request)
            {
                VALIDATE_AUTH(request);
                handleUpdate(false, request, {}, 0, UPLOAD_END);
            }, [](REQUEST_T request, String filename, size_t index, uint8_t *data, size_t len, bool final)
            {
                VALIDATE_AUTH(request);
                handleUpdate(false, request, data, len, mapValuesToUploadEvent(index), true);
            });

        server->on(AsyncURIMatcher::exact(filesystemUri),
            HTTP_POST,
            [](REQUEST_T request)
            {
                VALIDATE_AUTH(request);
                handleUpdate(true, request, {}, 0, UPLOAD_END);
            }, [](REQUEST_T request, String filename, size_t index, uint8_t *data, size_t len, bool final)
            {
                VALIDATE_AUTH(request);
                handleUpdate(true, request, data, len, mapValuesToUploadEvent(index), true);
            });
#elifdef ESP_CONP_HTTPS_SERVER
        addServerHandler(firmwareUri, HTTP_POST, [](REQUEST_T req)
        {
            handleRequest(req, false);
        });
        // TODO
#else
        server->on(firmwareUri,
            HTTP_POST,
            []()
            {
                VALIDATE_AUTH(server);
                handleUpdate(false, server, {}, 0, UPLOAD_END);
            }, []()
            {
                VALIDATE_AUTH(server);
                HTTPUpload upload = server->upload();
                handleUpdate(false, server, upload.buf, upload.currentSize, mapValuesToUploadEvent(upload));
            });

        server->on(filesystemUri,
            HTTP_POST,
            []()
            {
                VALIDATE_AUTH(server);
                handleUpdate(true, server, {}, 0, UPLOAD_END);
            }, []()
            {
                VALIDATE_AUTH(server);
                HTTPUpload upload = server->upload();
                handleUpdate(true, server, upload.buf, upload.currentSize, mapValuesToUploadEvent(upload));
            });
#endif
#endif
    }

    inline void otaLoop()
    {
        if (otaRestart)
        {
            delay(1500);
            ESP.restart();
        }

#ifdef ESP32_CONP_OTA_USE_WEBSOCKETS
        otaWsServer.loop();

        if (millis() - lastWsServerUpdate > 2000)
        {
            if (otaClient != nullptr)
            {
                char str[] = {OtaEventType::PING, 0};
                otaWsServer.sendTXT(otaClient->id, str);
            }

            if (otaClient != nullptr && !otaClient->authed && millis() - otaClient->connectedTime > 10000)
            {
                auto clientId = otaClient->id;
                if (otaWsServer.clientIsConnected(clientId))
                {
                    sendErrorResponse("OTA Error: ", "Auth timeout");
                }
            }

            if (otaStarted && millis() - otaTimer > otaTimeout)
            {
                LOGN("OTA timed out.");
                otaStarted = false;
                otaAbort();
            }

            lastWsServerUpdate = millis();
        }
#endif
    }
}

#endif //ESP_CONFIG_PAGE_OTA_H
