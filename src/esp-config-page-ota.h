//
// Created by xav on 2/22/25.
//

#ifndef ESP_CONFIG_PAGE_OTA_H
#define ESP_CONFIG_PAGE_OTA_H

#ifdef ESP32_CONFIG_PAGE_USE_ESP_IDF_OTA
#include <esp_ota_ops.h>
#include <esp_partition.h>
#warning  "Using ESP-IDF OTA API instead of Arduino's"
#else
#include <Update.h>
#endif

#if defined(ESP32_CONP_OTA_USE_WEBSOCKETS) && !defined(ESP_CONP_HTTPS_SERVER)
#include <ESPAsyncWebServer.h>
#endif

#include <esp-config-page-crypto.h>

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
    static constexpr size_t eventBufferSize = 256;
    ESP_CONP_INLINE uint8_t currentOtaKey[32]{};
    ESP_CONP_INLINE unsigned long lastOtaKeyGen = 0;
    static constexpr unsigned long keyExpiration = 60 * 1000;
    static constexpr size_t otaNonceSize = 12;
    static constexpr size_t otaExpirySize = sizeof(uint32_t);
    static constexpr size_t otaHashSize = ESP_CONP_CRYPTO_HASH_LEN;

    struct OtaClient
    {
        int id = -1;
        unsigned long connectedTime = 0;

        uint8_t eventBuffer[eventBufferSize]{};
        size_t dataOffset = 0;
        int currentEvent = -1;

#ifdef ESP_CONP_HTTPS_SERVER
        uint8_t *writeBuffer = nullptr;
        size_t writeBufferSize = ESP_CONP_WS_BUFFER_SIZE;
        int sockfd = -1;
#endif
    };

    inline void refreshKey()
    {
        ESP_CONFIG_PAGE_CRYPTO::genRandom(currentOtaKey, sizeof(currentOtaKey));
        lastOtaKeyGen = millis();
    }

    inline void handleTokenGen(REQUEST_T request)
    {
        refreshKey();
        uint32_t expiry = (esp_timer_get_time() / 1000000ULL) + 60;
        uint8_t data[otaNonceSize + otaExpirySize]{};
        memcpy(data, &expiry, sizeof(expiry));
        ESP_CONFIG_PAGE_CRYPTO::genRandom(data + sizeof(expiry), sizeof(data) - sizeof(expiry));

        uint8_t outputHash[otaHashSize]{};
        bool success = ESP_CONFIG_PAGE_CRYPTO::hmacHash(currentOtaKey, sizeof(currentOtaKey), data, sizeof(data), outputHash);
        if (!success)
        {
            sendInstantResponse(CONP_STATUS_CODE::INTERNAL_SERVER_ERROR, "error generating hash", request);
            return;
        }

        uint8_t fullOutput[sizeof(outputHash)+sizeof(data)]{};
        memcpy(fullOutput, data, sizeof(data));
        memcpy(fullOutput + sizeof(data), outputHash, sizeof(outputHash));

        char encoded[ESP_CONFIG_PAGE_CRYPTO::calcBase64StrLen(sizeof(fullOutput))]{};
        ESP_CONFIG_PAGE_CRYPTO::base64Encode(fullOutput, sizeof(fullOutput), encoded);
        sendInstantResponse(CONP_STATUS_CODE::OK, encoded, request);
    }

#define ESP_CONP_WS_CHECK_AUTH() if (!otaClient->authed) {sendErrorResponse("", "Not authenticated."); return; }
#if !defined(ESP_CONP_ASYNC_WEBSERVER) && !defined(ESP_CONP_HTTPS_SERVER)
    ESP_CONP_INLINE AsyncWebServer otaServer(ESP32_CONP_OTA_WS_PORT);
#endif

#ifndef ESP_CONP_HTTPS_SERVER
    ESP_CONP_INLINE AsyncWebSocket otaWebSockets("/config/update/ws");
#endif

    ESP_CONP_INLINE OtaClient *otaClient = nullptr;
    ESP_CONP_INLINE unsigned long lastWsServerUpdate = 0;
#else
    ESP_CONP_INLINE REQUEST_T currentRequest = nullptr;
#endif

    ESP_CONP_INLINE unsigned long otaTimer = 0;
    ESP_CONP_INLINE unsigned long otaTimeout = 60000;
    ESP_CONP_INLINE bool otaStarted = false;
    ESP_CONP_INLINE char otaMd5[33]{};
    ESP_CONP_INLINE bool otaMd5Started = false;
    ESP_CONP_INLINE bool isOtaFilesystem = false;
    ESP_CONP_INLINE bool otaRestart = false;

    using OtaStartCallback = std::function<void()>;
    inline OtaStartCallback otaStartCallback = nullptr;

    ESP_CONP_INLINE ESP_CONP_MD5_CTX_T otaMd5Ctx;

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
        INVALID = 'I',
    };

#ifdef ESP32_CONP_OTA_USE_WEBSOCKETS
    inline void registerOtaClient(uint8_t id)
    {
        if (otaClient != nullptr)
        {
#ifdef ESP_CONP_HTTPS_SERVER
            if (otaClient->writeBuffer != nullptr)
            {
                free(otaClient->writeBuffer);
            }
#endif

            delete otaClient;
            otaClient = nullptr;
        }

        otaClient = new OtaClient(id, millis());
        LOGF("Registered OTA client with ID %d\n", id);
    }

    inline void releaseOtaClient()
    {
        if (otaClient == nullptr)
        {
            return;
        }

        LOGN("Releasing OTA client.");

#ifdef ESP_CONP_HTTPS_SERVER
        if (otaClient->id >= 0 && otaClient->sockfd >= 0)
        {
            httpd_ws_frame frame {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = nullptr,
                .len = 0,
            };
            httpd_ws_send_frame_async(*server, otaClient->sockfd, &frame);
            httpd_sess_trigger_close(*server, otaClient->sockfd);

            if (otaClient->writeBuffer != nullptr)
            {
                free(otaClient->writeBuffer);
                otaClient->writeBuffer = nullptr;
            }
        }
#else
        AsyncWebSocketClient *client = otaWebSockets.client(otaClient->id);
        if (client != nullptr && client->status() == WS_CONNECTED)
        {
            client->close();
        }
#endif

        delete otaClient;
        otaClient = nullptr;
        refreshKey();
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

    inline void otaSendText(const char *toSend)
    {
#ifdef ESP_CONP_HTTPS_SERVER
        if (otaClient == nullptr)
        {
            return;
        }

        httpd_ws_frame_t frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *) toSend,
            .len = strlen(toSend),
        };
        int res = httpd_ws_send_frame_async(*server, otaClient->sockfd, &frame);
        if (res != ESP_OK)
        {
            LOGF("Error sending response frame: 0x%x\n", res);
        }
#else
        otaWebSockets.text(otaClient->id, toSend);
#endif
    }

    inline void sendResponse(const char *status, OtaEventType eventType = SUCCESS)
    {
#ifdef ESP32_CONP_OTA_USE_WEBSOCKETS
        char toSend[strlen(status) + 3]{};
        snprintf(toSend, sizeof(toSend), "%c%s", eventType, status);
        otaSendText(toSend);
#else
        sendInstantResponse(CONP_STATUS_CODE::OK, status, currentRequest);
#endif
    }

    inline void sendErrorResponse(const char *header, const char *err, OtaEventType eventType = ERROR)
    {
        char toSend[strlen(header) + strlen(err) + 3]{};
        snprintf(toSend, sizeof(toSend), "%c%s%s", eventType, header, err);

#ifdef ESP32_CONP_OTA_USE_WEBSOCKETS
        otaSendText(toSend);
        releaseOtaClient();
#else
        sendInstantResponse(CONP_STATUS_CODE::BAD_REQUEST, toSend, currentRequest);
#endif

        otaAbort();
    }

    inline void otaStart(const char *hash)
    {
#ifdef ESP_CONP_ENABLE_LOGGING_MODULE
        ESP_CONFIG_PAGE_LOGGING::disableLogging();
#endif

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

#if defined(ESP32_CONP_OTA_USE_WEBSOCKETS)
    inline bool otaWsValidateAuth(REQUEST_T request)
    {
        char authParam[96]{};
        getParam(request, "token", authParam, sizeof(authParam));

        uint8_t tokenBytes[96]{};
        size_t written = 0;
        ESP_CONFIG_PAGE_CRYPTO::base64Decode((uint8_t*) authParam, strlen(authParam), tokenBytes, sizeof(tokenBytes), written);

        LOGF("Decoded token size is %d\n", written);
        if (written != otaHashSize + otaNonceSize + otaExpirySize)
        {
            LOGN("OTA WS invalid token size.");
            return false;
        }

        size_t payloadLen = otaNonceSize + otaExpirySize;
        uint8_t payload[payloadLen]{};
        memcpy(payload, tokenBytes, otaExpirySize);
        memcpy(payload + otaExpirySize, tokenBytes + otaExpirySize, otaNonceSize);

        uint8_t expectedHash[otaHashSize]{};
        ESP_CONFIG_PAGE_CRYPTO::hmacHash(currentOtaKey, sizeof(currentOtaKey), payload, payloadLen, expectedHash);
        if (memcmp(expectedHash, tokenBytes+payloadLen, otaHashSize))
        {
            LOGN("OTA WS invalid token.");
            return false;
        }

        uint32_t now = esp_timer_get_time() / 1000000ULL;
        uint32_t expiry = 0;
        memcpy(&expiry, payload, otaExpirySize);
        if (expiry < now)
        {
            LOGN("OTA WS token expired.");
            return false;
        }

        return true;
    }

    inline void handleEvent(uint8_t *payload, size_t payloadLen, size_t totalLen, bool isLast)
    {
        switch (otaClient->currentEvent)
                {
                case OtaEventType::START_FIRMWARE:
                    {
                        if (!otaStarted)
                        {
                            isOtaFilesystem = false;
                            otaStart(payloadLen == 0 ? nullptr : (char*) payload);
                            sendResponse("", OtaEventType::NEXT_CHUNK);
                        }
                        break;
                    }
                case OtaEventType::START_FILESYSTEM:
                    {
                        if (!otaStarted)
                        {
                            isOtaFilesystem = true;
                            otaStart(payloadLen == 0 ? nullptr : (char*) payload);
                            sendResponse("", OtaEventType::NEXT_CHUNK);
                        }
                        break;
                    }
                case OtaEventType::END:
                    {
                        if (!otaStarted)
                        {
                            sendErrorResponse("OTA not started", "");
                            return;
                        }

                        otaFinish();
                        break;
                    }
                case OtaEventType::WRITE:
                    {
                        if (otaStarted && payloadLen > 0)
                        {
                            otaWrite(payload, payloadLen);

                            if (isLast)
                            {
                                LOGF("Written %zu bytes to OTA partition.\n", totalLen);
                                sendResponse("", NEXT_CHUNK);
                            }
                        }
                        break;
                    }
                default:
                    {
                        LOGF("Invalid OTA event: %d\n", otaClient->currentEvent);
                        sendErrorResponse("Invalid event received", "");
                        break;
                    }
                }
    }
#else
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

#ifndef ESP_CONP_HTTPS_SERVER
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
        addServerHandler("/config/update/token", HTTP_GET, handleTokenGen);
#endif

#if defined(ESP32_CONP_OTA_USE_WEBSOCKETS) && defined(ESP_CONP_HTTPS_SERVER)
        esp_event_handler_register(ESP_HTTP_SERVER_EVENT,
                                   HTTP_SERVER_EVENT_DISCONNECTED,
                                   [](void *arg, esp_event_base_t base, int32_t id, void *event_data)
                                   {
                                       int sockfd = *(int *) event_data;
                                       if (otaClient != nullptr && otaClient->sockfd >= 0 && otaClient->sockfd == sockfd)
                                       {
                                           LOGN("OTA client disconnected.");
                                           releaseOtaClient();
                                       }
                                   },
                                   nullptr);

        static const httpd_uri_t wsHandler {
            .uri = "/config/update/ws",
            .method = HTTP_GET,
            .handler = [](httpd_req_t *req)
            {
                if (req->method == HTTP_GET)
                {
                    LOGN("OTA Client connected.");
                    if (hasOtaClient())
                    {
                        sendErrorResponse("", "socket full");
                        return ESP_FAIL;
                    }

                    if (!otaWsValidateAuth(req))
                    {
                        sendInstantResponse(CONP_STATUS_CODE::UNAUTHORIZED, "invalid token", req);
                        return ESP_FAIL;
                    }

                    registerOtaClient(1);
                    otaClient->sockfd = httpd_req_to_sockfd(req);
                    sendResponse("", OtaEventType::AUTH_SUCCESS);
                    return ESP_OK;
                }

                httpd_ws_frame_t frame{};
                size_t bufferSize = 0;
                int res = httpd_ws_recv_frame(req, &frame, 0);
                if (res != ESP_OK)
                {
                    sendErrorResponse("", "error receiving data");
                    return ESP_OK;
                }

                if (frame.len < eventBufferSize)
                {
                    frame.payload = otaClient->eventBuffer;
                    bufferSize = eventBufferSize-1;
                    memset(otaClient->eventBuffer, 0, eventBufferSize);
                }
                else if (frame.len <= ESP_CONP_WS_BUFFER_SIZE)
                {
                    if (otaClient->writeBuffer == nullptr)
                    {
                        otaClient->writeBuffer = (uint8_t*) malloc(otaClient->writeBufferSize);
                        if (otaClient->writeBuffer == nullptr)
                        {
                            sendErrorResponse("", "could not allocate write buffer");
                            return ESP_OK;
                        }
                    }

                    frame.payload = otaClient->writeBuffer;
                    bufferSize = otaClient->writeBufferSize-1;
                    memset(otaClient->writeBuffer, 0, otaClient->writeBufferSize);
                }
                else
                {
                    LOGN("OTA frame too big.");
                    sendErrorResponse("", "ota frame to big");
                    return ESP_OK;
                }

                res = httpd_ws_recv_frame(req, &frame, bufferSize);
                if (res != ESP_OK)
                {
                    LOGF("Error receiving OTA frame: 0x%x\n", res);
                    sendErrorResponse("", "error receiving frame");
                    return ESP_OK;
                }

                uint8_t *buffer = frame.payload;
                buffer[frame.len] = 0;

                if (otaClient == nullptr)
                {
                    sendErrorResponse("", "invalid client state");
                    return ESP_FAIL;
                }

                if (frame.len < 1)
                {
                    sendErrorResponse("", "invalid event");
                    return ESP_FAIL;
                }

                otaClient->currentEvent = frame.payload[0];
                handleEvent(frame.payload+1, frame.len-1, frame.len, true);

                if (otaClient != nullptr)
                {
                    otaClient->currentEvent = -1;
                }

                return ESP_OK;
            },
            .is_websocket = true,
        };
        int res = httpd_register_uri_handler(*server, &wsHandler);
        if (res != ESP_OK)
        {
            LOGF("Error registering OTA WS handler: 0x%x\n", res);
        }
#elifdef ESP32_CONP_OTA_USE_WEBSOCKETS
        otaWebSockets.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
        {
            uint32_t clientId = client->id();

            if (type == WS_EVT_CONNECT)
            {
                if (hasOtaClient())
                {
                    sendErrorResponse("", "socket full");
                    return;
                }

                LOGF("OTA client connected with id %d\n", clientId);
                registerOtaClient(clientId);
                sendResponse("", AUTH_SUCCESS);
            }
            else if (type == WS_EVT_DISCONNECT && otaClient != nullptr)
            {
                LOGF("OTA client disconnected with id %d\n", clientId);
                releaseOtaClient();
                otaClient->currentEvent = INVALID;
            }
            else if (type == WS_EVT_DATA)
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

                AwsFrameInfo *info = (AwsFrameInfo*) arg;
                uint8_t *payload = data;
                size_t payloadLen = len;
                bool isLast = true;

                if (info != nullptr)
                {
                    size_t payloadIndex = 0;
                    bool isFirst = info->index == 0;
                    isLast = (info->index + len) >= info->len && info->final;
                    if (isFirst)
                    {
                        if (len < 1)
                        {
                            sendErrorResponse("", "Malformed request");
                            return;
                        }

                        otaClient->currentEvent = data[0];
                        payload = data + 1;
                        payloadLen = len - 1;
                        payloadIndex = 0;
                    }
                    else
                    {
                        if (info->index < 1)
                        {
                            sendErrorResponse("", "Invalid fragment index");
                            return;
                        }

                        payload = data;
                        payloadLen = len;
                        payloadIndex = info->index - 1;
                    }

                    if (otaClient->currentEvent != WRITE)
                    {
                        if ((payloadIndex + payloadLen) > eventBufferSize)
                        {
                            sendErrorResponse("", "Payload too large for this event type.");
                            return;
                        }

                        memcpy(otaClient->eventBuffer + payloadIndex, payload, payloadLen);
                    }

                    if (otaClient->currentEvent != WRITE && !isLast)
                    {
                        return;
                    }

                    if (otaClient->currentEvent != WRITE)
                    {
                        payload = otaClient->eventBuffer;
                        payloadLen = payloadIndex + payloadLen;
                    }
                }

                handleEvent(payload, payloadLen, info != nullptr ? info->len : 0, isLast);
            }
        });

        ArMiddlewareCallback fn = [](AsyncWebServerRequest *request, ArMiddlewareNext next)
        {
            if (!otaWsValidateAuth(request))
            {
                sendInstantResponse(CONP_STATUS_CODE::UNAUTHORIZED, "invalid token", request);
                return;
            }

            next();
            refreshKey();
        };

#ifdef ESP_CONP_ASYNC_WEBSERVER
        server->addHandler(&otaWebSockets).addMiddleware(fn);
#else
        otaServer.addHandler(&otaWebSockets).addMiddleware(fn);
        otaServer.begin();
#endif

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
        if (millis() - lastOtaKeyGen > keyExpiration)
        {
            refreshKey();
        }

        if (millis() - lastWsServerUpdate > 2000)
        {
            if (otaClient != nullptr)
            {
                char str[] = {OtaEventType::PING, 0};
                otaSendText(str);
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
