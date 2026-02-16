//
// Created by xav on 2/22/25.
//

#ifndef ESP_CONFIG_PAGE_FILES_H
#define ESP_CONFIG_PAGE_FILES_H

namespace ESP_CONFIG_PAGE
{
    inline void getFiles(REQUEST_T request)
    {
        String path = request->arg("plain");
        if (path.isEmpty())
        {
            path = "/";
        }

        ResponseContext c{};
        initResponseContext(200, "text/plain", 0, c);
        startResponse(request, c);

        char numBuf[33]{};

#ifdef ESP32
        File file = LittleFS.open(path);
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
        String path = request->arg("plain");
        if (path.isEmpty())
        {
            request->send(404);
            return;
        }

        if (!LittleFS.exists(path))
        {
            request->send(404);
            return;
        }

        File file = LittleFS.open(path, "r");
        if (file.isDirectory())
        {
            file.close();
            request->send(400);
            return;
        }

        ResponseContext c{};
        initResponseContext(200, "application/octet-stream", file.size(), c);
        startResponse(request, c);
        sendHeader("Content-Disposition", file.name(), c);
        writeResponse(file, c);
        file.close();
    }

    inline void deleteFile(REQUEST_T request)
    {
        String path = request->arg("plain");
        if (path.isEmpty())
        {
            request->send(404);
            return;
        }

        if (!LittleFS.exists(path))
        {
            request->send(404);
            return;
        }

        LittleFS.remove(path);
        request->send(200);
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

        addServerHandler((char*) F("/config/files"), HTTP_POST, getFiles);
        addServerHandler((char*) F("/config/files/download"), HTTP_POST, downloadFile);
        addServerHandler((char*) F("/config/files/delete"), HTTP_POST, deleteFile);
    }
}

#endif //ESP_CONFIG_PAGE_FILES_H
