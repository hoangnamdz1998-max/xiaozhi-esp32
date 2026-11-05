#include "custom_wallpaper.h"

extern const uint8_t wallpaper_portrait_bin_start[] asm("_binary_wallpaper_portrait_bin_start");
extern const uint8_t wallpaper_portrait_bin_end[] asm("_binary_wallpaper_portrait_bin_end");

extern const uint8_t wallpaper_landscape_bin_start[] asm("_binary_wallpaper_landscape_bin_start");
extern const uint8_t wallpaper_landscape_bin_end[] asm("_binary_wallpaper_landscape_bin_end");

extern const uint8_t boot_portrait_bin_start[] asm("_binary_boot_portrait_bin_start");
extern const uint8_t boot_portrait_bin_end[] asm("_binary_boot_portrait_bin_end");

extern const uint8_t boot_landscape_bin_start[] asm("_binary_boot_landscape_bin_start");
extern const uint8_t boot_landscape_bin_end[] asm("_binary_boot_landscape_bin_end");

extern const lv_image_dsc_t wallpaper_portrait_dsc = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = 240,
        .h = 320,
        .stride = 240 * 2,
        .reserved_2 = 0,
    },
    .data_size = 240 * 320 * 2,
    .data = wallpaper_portrait_bin_start,
    .reserved = nullptr,
};

extern const lv_image_dsc_t wallpaper_landscape_dsc = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = 320,
        .h = 240,
        .stride = 320 * 2,
        .reserved_2 = 0,
    },
    .data_size = 320 * 240 * 2,
    .data = wallpaper_landscape_bin_start,
    .reserved = nullptr,
};

extern const lv_image_dsc_t boot_portrait_dsc = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = 240,
        .h = 320,
        .stride = 240 * 2,
        .reserved_2 = 0,
    },
    .data_size = 240 * 320 * 2,
    .data = boot_portrait_bin_start,
    .reserved = nullptr,
};

extern const lv_image_dsc_t boot_landscape_dsc = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = 320,
        .h = 240,
        .stride = 320 * 2,
        .reserved_2 = 0,
    },
    .data_size = 320 * 240 * 2,
    .data = boot_landscape_bin_start,
    .reserved = nullptr,
};
