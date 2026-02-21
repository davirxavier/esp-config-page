//
// Created by xav on 2/22/25.
//

#ifndef ESP_CONFIG_PAGE_FILES_H
#define ESP_CONFIG_PAGE_FILES_H

namespace ESP_CONFIG_PAGE
{
    inline void checkPathEmpty(char *pathBuf)
    {
        if (pathBuf[0] == 0)
        {
            pathBuf[0] = '/';
            pathBuf[1] = 0;
        }
    }

    inline void getFiles(REQUEST_T request)
    {
        char pathBuf[256]{};
        if (getArgParamAndValidateMaxSize(request, pathBuf, sizeof(pathBuf)))
        {
            return;
        }

        checkPathEmpty(pathBuf);
        LOGF("File search path: %s\n", pathBuf);

        ResponseContext c{};
        initResponseContext(CONP_STATUS_CODE::OK, "text/plain", 0, c);
        startResponse(request, c);

        char numBuf[33]{};

#ifdef ESP32
        File file = LittleFS.open(pathBuf);
        File nextFile;
        while (file.isDirectory() && (nextFile = file.openNextFile()))
        {
            writeResponse(nextFile.name(), c);
            writeResponse(":", c);
            writeResponse(nextFile.isDirectory() ? "true" : "false", c);
            writeResponse(":", c);

            ESP_CONP_WRITE_NUMBUF(numBuf, "%zu", nextFile.size());
            writeResponse(numBuf, c);
            writeResponse(";", c);

            if (nextFile)
            {
                nextFile.close();
            }
        }

        file.close();
#elif ESP8266
        Dir dir = LittleFS.openDir(path);
        while (dir.next()) {
            writeResponse(dir.fileName(), c);
            writeResponse(":", c);
            writeResponse(dir.isDirectory() ? "true" : "false", c);
            writeResponse(":", c);
            writeResponse(dir.fileSize(), c);
            writeResponse(";", c);
        }
#endif

        endResponse(request, c);
    }

    inline void downloadFile(REQUEST_T request)
    {
        char pathBuf[256]{};
        if (getArgParamAndValidateMaxSize(request, pathBuf, sizeof(pathBuf)))
        {
            return;
        }

        if (pathBuf[0] == 0 || !LittleFS.exists(pathBuf))
        {
            sendInstantResponse(CONP_STATUS_CODE::NOT_FOUND, "file not found", request);
            return;
        }

        File file = LittleFS.open(pathBuf, "r");
        if (file.isDirectory())
        {
            file.close();
            sendInstantResponse(CONP_STATUS_CODE::BAD_REQUEST, "can't download folder", request);
            return;
        }

        ResponseContext c{};
        initResponseContext(CONP_STATUS_CODE::OK, "application/octet-stream", file.size(), c);
        startResponse(request, c);
        sendHeader("Content-Disposition", file.name(), c);
        writeResponse(file, c);
        file.close();
    }

    inline void deleteFile(REQUEST_T request)
    {
        char pathBuf[256]{};
        if (getArgParamAndValidateMaxSize(request, pathBuf, sizeof(pathBuf)))
        {
            return;
        }

        if (pathBuf[0] == 0 || !LittleFS.exists(pathBuf))
        {
            sendInstantResponse(CONP_STATUS_CODE::NOT_FOUND, "file not found", request);
            return;
        }

        LittleFS.remove(pathBuf);
        sendInstantResponse(CONP_STATUS_CODE::OK, "", request);
    }

    inline void enableFilesModule()
    {
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
            }
        }
#elif ESP8266
        LittleFS.begin();
#endif

        addServerHandler("/config/files", HTTP_GET, getFiles);
        addServerHandler("/config/files/download", HTTP_GET, downloadFile);
        addServerHandler("/config/files/delete", HTTP_DELETE, deleteFile);
    }
}

#endif //ESP_CONFIG_PAGE_FILES_H
