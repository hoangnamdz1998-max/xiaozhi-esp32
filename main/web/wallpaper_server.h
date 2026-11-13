#ifndef WALLPAPER_SERVER_H
#define WALLPAPER_SERVER_H

#include <esp_http_server.h>

class WallpaperServer {
public:
    static WallpaperServer& GetInstance();

    bool Start();
    void Stop();
    bool IsRunning() const { return server_ != nullptr; }

private:
    WallpaperServer();
    ~WallpaperServer();

    httpd_handle_t server_ = nullptr;
};

#endif // WALLPAPER_SERVER_H
