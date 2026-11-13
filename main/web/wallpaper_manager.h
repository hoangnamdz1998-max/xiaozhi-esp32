#ifndef WALLPAPER_MANAGER_H
#define WALLPAPER_MANAGER_H

#include <string>
#include <vector>
#include <cstdint>
#include <esp_err.h>

struct WallpaperItem {
    int index;          // 0 = Hệ thống, 1 = Ảnh 1, 2 = Ảnh 2, ...
    std::string name;   // "Ảnh hệ thống (Mặc định)", "Ảnh 1", "Ảnh 2", ...
    bool is_system;
};

class WallpaperManager {
public:
    static WallpaperManager& GetInstance();

    void Initialize();
    bool IsStorageMounted() const { return storage_mounted_; }

    int GetActiveWallpaperIndex();
    void SetActiveWallpaperIndex(int idx);

    int GetActiveLayoutIndex(); // 0: Flip đáy + Thời tiết trên, 1: Giờ số giữa, 2: Flip giữa + Thời tiết đáy
    void SetActiveLayoutIndex(int layout);

    std::vector<WallpaperItem> GetWallpaperList();
    int GetNextAvailableIndex();
    void RefreshIndexCache();

    bool SaveWallpaperData(int idx, bool is_portrait, const uint8_t* data, size_t len);
    bool DeleteWallpaper(int idx);

    // Lấy dữ liệu ảnh JPEG. Nếu is_allocated = true thì caller phải giải phóng bằng heap_caps_free()
    bool GetWallpaperJpeg(int idx, bool is_portrait, const uint8_t** out_data, size_t* out_len, bool* is_allocated);

private:
    WallpaperManager();
    ~WallpaperManager() = default;

    bool storage_mounted_ = false;
    std::vector<int> custom_indices_;
    std::string GetFilePath(int idx, bool is_portrait);
};

#endif // WALLPAPER_MANAGER_H
