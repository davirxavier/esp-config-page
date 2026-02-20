//
// Created by xav on 2/18/26.
//

#ifndef ESP_CONFIG_PAGE_SERVER_H
#define ESP_CONFIG_PAGE_SERVER_H

#if defined(ESP_CONP_HTTPS_SERVER) && defined(ESP8266)
#error "HTTPS server is not supported in the ESP8266 board."
#endif

#ifdef ESP_CONP_ASYNC_WEBSERVER
#include "ESPAsyncWebServer.h"
#elifdef ESP_CONP_HTTPS_SERVER
#include "esp_https_server.h"
#include "esp-config-page-cert.h"
#include "mbedtls/base64.h"
#else
#ifdef ESP32
#include "WebServer.h"
#include "Update.h"
#elif ESP8266
#include "ESP8266WebServer.h"
#endif
#endif

#define VALIDATE_AUTH(r) if (!ESP_CONFIG_PAGE::validateAuth(r)) return

namespace ESP_CONFIG_PAGE
{
    namespace CONP_STATUS_CODE
    {
        enum HTTPStatusCode
        {
            OK = 0,
            BAD_REQUEST,
            UNAUTHORIZED,
            NOT_FOUND,
            INTERNAL_SERVER_ERROR,
            INVALID,
        };

        static const char *textsByStatus[] = {
            "200 OK",
            "400 BAD REQUEST",
            "401 UNAUTHORIZED",
            "404 NOT FOUND",
            "500 INTERNAL SERVER ERROR",
        };
    }

    struct ResponseContext
    {
#ifdef ESP_CONP_ASYNC_WEBSERVER
        AsyncWebServerResponse *response = nullptr;
#elifdef ESP_CONP_HTTPS_SERVER
        httpd_req* request;
#else
        WiFiClient client;
        bool firstResponseWritten = false;
#endif
        const uint8_t* fullContent = nullptr; // Set content here when streamResponse = false
        bool fullContentFromProgmem = false;

        CONP_STATUS_CODE::HTTPStatusCode status = CONP_STATUS_CODE::OK;
        char contentType[33]{};
        size_t length = 0;
    };

#ifdef ESP_CONP_ASYNC_WEBSERVER
    using WEBSERVER_T = AsyncWebServer;
    using METHOD_T = WebRequestMethodComposite;
    using REQUEST_T = AsyncWebServerRequest*;
#elifdef ESP_CONP_HTTPS_SERVER
    using WEBSERVER_T = httpd_handle_t;
    using METHOD_T = http_method;
    using REQUEST_T = httpd_req*;
#else
#ifdef ESP32
    using WEBSERVER_T = WebServer;
#elif ESP8266
    using WEBSERVER_T = ESP8266WebServer*;
#endif
    using METHOD_T = HTTPMethod;
    using REQUEST_T = WebServer*;
#endif

    static WEBSERVER_T *server = nullptr;
    using RequestCallback = std::function<void(REQUEST_T)>;
    int registeredHandlers = 0;

#ifdef ESP_CONP_HTTPS_SERVER
    inline void setupServerConfig(httpd_ssl_config *sslConfig)
    {
        ESP_CONFIG_PAGE::LittleFSKeyValueStorage storage(ESP_CONP_CERT_FOLDER);
        ESP_CONFIG_PAGE_CERT::initModule(&storage);

        sslConfig->prvtkey_pem = (uint8_t*) ESP_CONFIG_PAGE_CERT::keyBuffer;
        sslConfig->prvtkey_len = ESP_CONFIG_PAGE_CERT::keyLen + 1;
        sslConfig->servercert = (uint8_t*) ESP_CONFIG_PAGE_CERT::certBuffer;
        sslConfig->servercert_len = ESP_CONFIG_PAGE_CERT::certLen + 1;
        sslConfig->port_secure = 443;
        sslConfig->httpd.max_uri_handlers = 20;
        sslConfig->httpd.lru_purge_enable = true;
    }
#endif

    inline size_t getHeader(REQUEST_T request, const char* name, char* output, size_t outputSize)
    {
#ifdef ESP_CONP_HTTPS_SERVER
        size_t len = httpd_req_get_hdr_value_len(request, name);

        if (len == 0)
        {
            output[0] = 0;
            return 0;
        }

        if (httpd_req_get_hdr_value_str(request, name, output, outputSize) == ESP_ERR_HTTPD_RESULT_TRUNC)
        {
            output[outputSize - 1] = 0;
        }

        if (len >= outputSize)
        {
            len = outputSize - 1;
        }

        return len;
#else
        if (request->hasHeader(name))
        {
            String header = request->header(name);
            return snprintf(output, outputSize, "%s", header.c_str());
        }

        output[0] = 0;
        return 0;
#endif
    }

    inline void requestAuth(REQUEST_T r)
    {
#ifdef ESP_CONP_HTTPS_SERVER
        httpd_resp_set_hdr(r, "WWW-Authenticate", "Basic realm=\"ESP32-CONFIG-PAGE\"");
        httpd_resp_send_err(r, HTTPD_401_UNAUTHORIZED, "unauthorized");
#else
        r->requestAuthentication();
#endif
    }

    inline bool validateAuth(REQUEST_T r)
    {
#ifdef ESP_CONP_HTTPS_SERVER
        char fullAuthHeader[128]{};
        size_t fullLen = getHeader(r, "Authorization", fullAuthHeader, sizeof(fullAuthHeader));

        const char basicHeaderStart[] = "Basic ";
        char *auth = strcasestr(fullAuthHeader, basicHeaderStart);
        if (auth == nullptr)
        {
            requestAuth(r);
            return false;
        }

        auth = auth + strlen(basicHeaderStart);
        if (auth > fullAuthHeader + fullLen)
        {
            requestAuth(r);
            return false;
        }
        size_t authLen = strlen(auth);

        size_t decodedSize = 0;
        char decodedHeader[authLen + 1]{};
        int ret = mbedtls_base64_decode((uint8_t*)decodedHeader, authLen, &decodedSize, (uint8_t*)auth, authLen);
        if (ret != 0)
        {
            LOGF("Error decoding auth size: 0x%x\n", -ret);
            httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "error decoding auth");
            return false;
        }
        decodedHeader[decodedSize] = 0;

        char usernameAndPassword[username.length() + password.length() + 2]{};
        snprintf(usernameAndPassword, sizeof(usernameAndPassword), "%s:%s", username.c_str(), password.c_str());

        bool equal = strcmp(usernameAndPassword, decodedHeader) == 0;
        if (!equal)
        {
            requestAuth(r);
        }
        return equal;
#else
        if (!r->authenticate(username.c_str(), password.c_str()))
        {
            delay(1000);
            r->requestAuthentication();
            return false;
        }
#endif

        return true;
    }

    inline void addServerHandler(const char* uri, METHOD_T method, RequestCallback fn)
    {
#ifdef ESP_CONP_ASYNC_WEBSERVER
        server->on(AsyncURIMatcher::exact(uri), method, [uri, method, fn](REQUEST_T req)
        {
            VALIDATE_AUTH(req);
            LOGF("Received request: %s - %d.\n", uri, method);
            fn(req);
        });
#elifdef ESP_CONP_HTTPS_SERVER
        auto* heap_fn = new RequestCallback(fn); // No need to unallocate, handlers will never be unregistered

        httpd_uri_t handler{
            .uri = uri,
            .method = method,
            .handler = [](httpd_req* req)
            {
                if (!validateAuth(req))
                {
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unauthorized");
                    return ESP_OK;
                }

                (*((RequestCallback*)req->user_ctx))(req);
                return ESP_OK;
            },
            .user_ctx = heap_fn,
        };
        int res = httpd_register_uri_handler(*server, &handler);
        if (res != ESP_OK)
        {
            LOGF("Error registering handler %s: 0x%x\n", uri, res);
        }
#else
        server->on(uri, method, [uri, method, fn]()
        {
            VALIDATE_AUTH(server);
            LOGF("Received request: %s - %d.\n", uri, method);
            fn(server);
        });
#endif

        registeredHandlers++;
    }

    /**
     * @param fullContent Set to the desired content to send all at once.
     */
    inline void initResponseContext(CONP_STATUS_CODE::HTTPStatusCode status,
                                    const char* contentType,
                                    size_t length,
                                    ResponseContext& c)
    {
        c.status = status;
        c.length = length;
        snprintf(c.contentType, sizeof(c.contentType), "%s", contentType);
    }

    inline bool startResponse(REQUEST_T request, ResponseContext& c)
    {
        LOGF("Started response with length %zu\n", c.length);

#ifdef ESP_CONP_ASYNC_WEBSERVER
        if (c.fullContent == nullptr)
        {
            c.response = request->beginResponseStream(c.contentType, c.length + 64);
            c.response->setCode(c.status);
        }
        else
        {
            c.response = request->beginResponse(c.status, c.contentType, c.fullContent, c.length);
        }
#elifdef ESP_CONP_HTTPS_SERVER
        if (c.status >= 0 && c.status < CONP_STATUS_CODE::INVALID)
        {
            httpd_resp_set_status(request, CONP_STATUS_CODE::textsByStatus[c.status]);
        }

        httpd_resp_set_type(request, c.contentType);
        c.request = request;
#else
        c.client = request->client();

        c.client.print(F("HTTP/1.1 "));
        c.client.print(CONP_STATUS_CODE::textsByStatus[c.status]);
        c.client.print(F("\r\nCache-Control: no-cache\r\nConnection: close\r\nContent-Type: "));
        c.client.print(c.contentType);
        c.client.print(F("\r\n"));

        if (c.length > 0)
        {
            c.client.print(F("Content-Length: "));
            c.client.print(c.length);
            c.client.print("\r\n");
        }
#endif

        return true;
    }

    inline void sendHeader(const char* name, const char* val, ResponseContext& c)
    {
#ifdef ESP_CONP_ASYNC_WEBSERVER
        if (c.response == nullptr)
        {
            return;
        }

        c.response->addHeader(name, val);
#elifdef ESP_CONP_HTTPS_SERVER
        if (c.request == nullptr)
        {
            return;
        }

        httpd_resp_set_hdr(c.request, name, val);
#else
        if (!c.client.connected())
        {
            LOGN("Tried to send header but client is disconnected.");
            return;
        }

        c.client.print(name);
        c.client.print(F(": "));
        c.client.print(val);
        c.client.print(F("\r\n"));
#endif
    }

    inline void writeResponse(const uint8_t* content, size_t len, ResponseContext& c)
    {
        if (c.fullContent != nullptr)
        {
            return;
        }

#ifdef ESP_CONP_ASYNC_WEBSERVER
        if (c.response == nullptr)
        {
            return;
        }

        auto stream = static_cast<AsyncResponseStream*>(c.response);
        stream->write(content, len);
#elifdef ESP_CONP_HTTPS_SERVER
        if (c.request == nullptr)
        {
            return;
        }

        httpd_resp_send_chunk(c.request, (const char*) content, len);
#else
        if (!c.client.connected())
        {
            LOGN("Tried to write to response but client is disconnected.");
            return;
        }

        if (!c.firstResponseWritten)
        {
            c.client.print(F("\r\n"));
            c.firstResponseWritten = true;
        }

        c.client.write(content, len);
#endif
    }

    inline void writeResponse(const char* content, ResponseContext& c)
    {
        writeResponse((const uint8_t*)content, strlen(content), c);
    }

    inline size_t writeResponse(Stream& stream, ResponseContext& c)
    {
        if (c.fullContent != nullptr)
        {
            return 0;
        }

        size_t total = 0;
        uint8_t buf[256];
        while (stream.available())
        {
            size_t read = stream.readBytes(buf, sizeof(buf));
            writeResponse(buf, read, c);
            total += read;
        }
        return total;
    }

    inline void endResponse(REQUEST_T request, ResponseContext& c)
    {
#ifdef ESP_CONP_ASYNC_WEBSERVER
        request->send(c.response);
#elifdef ESP_CONP_HTTPS_SERVER
        if (c.fullContent == nullptr)
        {
            httpd_resp_send_chunk(request, nullptr, 0);
        }
        else if (c.length > 1024)
        {
            size_t offset = 0;
            constexpr size_t chunkSize = 1024;
            while (offset < c.length)
            {
                size_t toSend = min(chunkSize, c.length - offset);
                int ret = httpd_resp_send_chunk(request, (const char *) (c.fullContent + offset), toSend);
                if (ret != ESP_OK)
                {
                    break;
                }
                offset += toSend;
            }
            httpd_resp_send_chunk(request, nullptr, 0);
        }
        else
        {
            httpd_resp_send(request, (const char*) c.fullContent, c.length);
        }
#else
        if (!c.client.connected())
        {
            return;
        }

        if (!c.firstResponseWritten)
        {
            c.client.print("\r\n");
        }

        if (c.fullContent != nullptr && c.fullContentFromProgmem)
        {
            c.client.write_P((PGM_P) c.fullContent, c.length);
        }
        else if (c.fullContent != nullptr)
        {
            c.client.write(c.fullContent, c.length);
        }

        c.client.stop();
#endif
    }

    inline size_t getBodyLen(REQUEST_T req)
    {
#ifdef ESP_CONP_HTTPS_SERVER
        return req->content_len;
#elifdef ESP_CONP_ASYNC_WEBSERVER
        return req->contentLength();
#else
        return req->clientContentLength();
#endif
    }

    inline size_t getBody(REQUEST_T req, char* buf, size_t bufsize, bool flushRemaining = true)
    {
#ifdef ESP_CONP_HTTPS_SERVER
        if (!buf || bufsize == 0)
        {
            return 0;
        }

        memset(buf, 0, bufsize);

        size_t totalLen = req->content_len;
        size_t toRead = min(totalLen, bufsize - 1);

        size_t receivedTotal = 0;
        while (receivedTotal < toRead)
        {
            int r = httpd_req_recv(req, buf + receivedTotal, toRead - receivedTotal);
            if (r <= 0)
            {
                break;
            }
            receivedTotal += r;
        }

        buf[receivedTotal] = '\0';

        if (flushRemaining && totalLen > receivedTotal)
        {
            char scratch[128];
            while (httpd_req_recv(req, scratch, sizeof(scratch)) > 0) {}
        }

        return receivedTotal;
#else
        String body = req->arg("plain");
        return snprintf(buf, bufsize, "%s", body.c_str());
#endif
    }

    inline void sendInstantResponse(CONP_STATUS_CODE::HTTPStatusCode status, const char *message, REQUEST_T req)
    {
#ifdef ESP_CONP_HTTPS_SERVER
        if (status >= 0 && status < CONP_STATUS_CODE::INVALID)
        {
            httpd_resp_set_status(req, CONP_STATUS_CODE::textsByStatus[status]);
        }

        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, message, strlen(message));
#else
        req->send(status, "text/plain", message);
#endif
    }

    inline bool getBodyAndValidateMaxSize(REQUEST_T req, char *buf, size_t bufSize)
    {
        size_t totalLen = getBodyLen(req);
        size_t received = getBody(req, buf, bufSize);

        if (totalLen > received)
        {
            sendInstantResponse(CONP_STATUS_CODE::BAD_REQUEST, "Body too big.", req);
            return true;
        }

        return false;
    }
}

#endif //ESP_CONFIG_PAGE_SERVER_H
