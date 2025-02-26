//
// Created by xav on 2/22/25.
//

#ifndef ESP_CONFIG_PAGE_FILES_H
#define ESP_CONFIG_PAGE_FILES_H

namespace ESP_CONFIG_PAGE
{
    inline void getFiles()
    {
        String path = server->arg("plain");
        if (path.isEmpty())
        {
            path = "/";
        }

        String ret;

#ifdef ESP32
        File file = LittleFS.open(path);
        File nextFile;
        while (file.isDirectory() && (nextFile = file.openNextFile()))
        {
            ret += String(nextFile.name()) + ":" + (nextFile.isDirectory() ? "true" : "false") + ":" + nextFile.
                size() + ";";

            if (nextFile)
            {
                nextFile.close();
            }
        }

        file.close();
#elif ESP8266
        Dir dir = LittleFS.openDir(path);
        while (dir.next()) {
            ret += dir.fileName() + ":" + (dir.isDirectory() ? "true" : "false") + ":" + dir.fileSize() + ";";
        }
#endif

        server->send(200, "text/plain", ret);
    }

    inline void downloadFile()
    {
        String path = server->arg("plain");
        if (path.isEmpty())
        {
            server->send(404);
            return;
        }

        if (!LittleFS.exists(path))
        {
            server->send(404);
            return;
        }

        File file = LittleFS.open(path, "r");
        server->sendHeader("Content-Disposition", file.name());
        server->streamFile(file, "text");
        file.close();
    }

    inline void deleteFile()
    {
        String path = server->arg("plain");
        if (path.isEmpty())
        {
            server->send(404);
            return;
        }

        if (!LittleFS.exists(path))
        {
            server->send(404);
            return;
        }

        LittleFS.remove(path);
        server->send(200);
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
