#include "wallpaper_manager.h"
#include "custom_wallpaper.h"
#include "settings.h"
#include <esp_log.h>
#include <esp_spiffs.h>
#include <esp_heap_caps.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cstdio>
#include <algorithm>
#include <set>

#define TAG "WallpaperMgr"

WallpaperManager& WallpaperManager::GetInstance() {
    static WallpaperManager instance;
    return instance;
}

WallpaperManager::WallpaperManager() {
}

void WallpaperManager::Initialize() {
    if (storage_mounted_) return;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 10,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to mount SPIFFS (storage): %s, trying default partition...", esp_err_to_name(ret));
        conf.partition_label = nullptr;
        ret = esp_vfs_spiffs_register(&conf);
    }

    if (ret == ESP_OK) {
        storage_mounted_ = true;
        size_t total = 0, used = 0;
        esp_spiffs_info(conf.partition_label, &total, &used);
        ESP_LOGI(TAG, "SPIFFS mounted! Total: %u KB, Used: %u KB", (unsigned)(total / 1024), (unsigned)(used / 1024));
    } else {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s. Checking /sdcard...", esp_err_to_name(ret));
        struct stat st;
        if (stat("/sdcard", &st) == 0) {
            storage_mounted_ = false;
        }
    }

    RefreshIndexCache();
}

void WallpaperManager::RefreshIndexCache() {
    custom_indices_.clear();
    std::set<int> found;

    const char* path = storage_mounted_ ? "/spiffs" : "/sdcard";
    DIR* dir = opendir(path);
    if (!dir && !storage_mounted_) dir = opendir("/sdcard/wallpapers");
    if (dir) {
        struct dirent* de;
        while ((de = readdir(dir)) != NULL) {
            int idx = 0;
            if (sscanf(de->d_name, "wp_%d_p.jpg", &idx) == 1 && idx > 0) {
                found.insert(idx);
            } else if (sscanf(de->d_name, "/wp_%d_p.jpg", &idx) == 1 && idx > 0) {
                found.insert(idx);
            }
        }
        closedir(dir);
    }

    for (int idx : found) {
        custom_indices_.push_back(idx);
    }
    std::sort(custom_indices_.begin(), custom_indices_.end());
    ESP_LOGI(TAG, "Refreshed wallpaper cache: %u custom wallpapers found", (unsigned)custom_indices_.size());
}

int WallpaperManager::GetActiveWallpaperIndex() {
    Settings s("wallpaper", true);
    return s.GetInt("wp_idx", 1);
}

void WallpaperManager::SetActiveWallpaperIndex(int idx) {
    Settings s("wallpaper", true);
    s.SetInt("wp_idx", idx);
}

int WallpaperManager::GetActiveLayoutIndex() {
    Settings s("wallpaper", true);
    return s.GetInt("wp_layout", 0);
}

void WallpaperManager::SetActiveLayoutIndex(int layout) {
    Settings s("wallpaper", true);
    s.SetInt("wp_layout", layout);
}

std::string WallpaperManager::GetFilePath(int idx, bool is_portrait) {
    const char* base = storage_mounted_ ? "/spiffs/wp_" : "/sdcard/wp_";
    return std::string(base) + std::to_string(idx) + (is_portrait ? "_p.jpg" : "_l.jpg");
}

std::vector<WallpaperItem> WallpaperManager::GetWallpaperList() {
    std::vector<WallpaperItem> list;
    // 4 ảnh hệ thống
    list.push_back({1, "Ảnh 1", true});
    list.push_back({2, "Ảnh 2", true});
    list.push_back({3, "Ảnh 3", true});
    list.push_back({4, "Ảnh 4", true});

    int user_counter = 1;
    for (int idx : custom_indices_) {
        if (idx >= 5) {
            std::string name = "Ảnh " + std::to_string(user_counter++);
            list.push_back({idx, name, false});
        }
    }
    return list;
}

int WallpaperManager::GetNextAvailableIndex() {
    int max_idx = 4;
    for (int idx : custom_indices_) {
        if (idx > max_idx) max_idx = idx;
    }
    return max_idx + 1;
}

bool WallpaperManager::SaveWallpaperData(int idx, bool is_portrait, const uint8_t* data, size_t len) {
    if (idx <= 0 || data == nullptr || len == 0) return false;

    if (!storage_mounted_) {
        Initialize();
    }

    std::string path = GetFilePath(idx, is_portrait);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", path.c_str());
        return false;
    }

    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    ESP_LOGI(TAG, "Saved wallpaper idx %d (%s), %u bytes to %s", idx, is_portrait ? "portrait" : "landscape", (unsigned)written, path.c_str());
    
    RefreshIndexCache();
    return written == len;
}

bool WallpaperManager::DeleteWallpaper(int idx) {
    if (idx <= 0) return false; // Không được xóa ảnh hệ thống
    std::string path_p = GetFilePath(idx, true);
    std::string path_l = GetFilePath(idx, false);
    unlink(path_p.c_str());
    unlink(path_l.c_str());

    RefreshIndexCache();

    if (GetActiveWallpaperIndex() == idx) {
        SetActiveWallpaperIndex(0); // Trở về ảnh hệ thống
    }
    return true;
}

bool WallpaperManager::GetWallpaperJpeg(int idx, bool is_portrait, const uint8_t** out_data, size_t* out_len, bool* is_allocated) {
    if (out_data == nullptr || out_len == nullptr || is_allocated == nullptr) return false;

    if (idx <= 4) {
        // 4 ảnh hệ thống chất lượng cao tích hợp sẵn
        const uint8_t* start = nullptr;
        const uint8_t* end = nullptr;
        if (idx == 2) {
            start = is_portrait ? standby2_bg2_portrait_jpg_start : standby2_bg2_landscape_jpg_start;
            end = is_portrait ? standby2_bg2_portrait_jpg_end : standby2_bg2_landscape_jpg_end;
        } else if (idx == 3) {
            start = is_portrait ? standby2_bg3_portrait_jpg_start : standby2_bg3_landscape_jpg_start;
            end = is_portrait ? standby2_bg3_portrait_jpg_end : standby2_bg3_landscape_jpg_end;
        } else if (idx == 4) {
            start = is_portrait ? standby2_bg4_portrait_jpg_start : standby2_bg4_landscape_jpg_start;
            end = is_portrait ? standby2_bg4_portrait_jpg_end : standby2_bg4_landscape_jpg_end;
        } else {
            // idx == 1 hoặc 0 (mặc định)
            start = is_portrait ? standby2_bg1_portrait_jpg_start : standby2_bg1_landscape_jpg_start;
            end = is_portrait ? standby2_bg1_portrait_jpg_end : standby2_bg1_landscape_jpg_end;
        }
        *out_data = start;
        *out_len = (size_t)(end - start);
        *is_allocated = false;
        return true;
    }

    std::string path = GetFilePath(idx, is_portrait);
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        ESP_LOGW(TAG, "File %s not found, falling back to system wallpaper", path.c_str());
        return GetWallpaperJpeg(1, is_portrait, out_data, out_len, is_allocated);
    }

    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz == 0 || sz > 1024 * 512) { // Giới hạn 512KB
        fclose(f);
        return GetWallpaperJpeg(1, is_portrait, out_data, out_len, is_allocated);
    }

    uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (uint8_t*)malloc(sz);
    }
    if (!buf) {
        fclose(f);
        return GetWallpaperJpeg(1, is_portrait, out_data, out_len, is_allocated);
    }

    size_t read_bytes = fread(buf, 1, sz, f);
    fclose(f);

    if (read_bytes != sz) {
        free(buf);
        return GetWallpaperJpeg(1, is_portrait, out_data, out_len, is_allocated);
    }

    *out_data = buf;
    *out_len = sz;
    *is_allocated = true;
    return true;
}
