#include "lcd_display.h"

#include "custom_wallpaper.h"
#include "web/wallpaper_manager.h"
#include "qrcodegen.h"
#include "wifi_station.h"
#include <ssid_manager.h>
#include "jpeg_to_image.h"

#include "assets/lang_config.h"

#include "gif/lvgl_gif.h"

#include "lvgl_theme.h"

#include "settings.h"



#include <esp_err.h>
#include <esp_pthread.h>

#include <esp_heap_caps.h>

#include <esp_log.h>

#include <esp_lvgl_port.h>

#include <esp_psram.h>

#include <material_symbols.h>

#include <noto_emoji.h>

#include <src/misc/cache/lv_cache.h>

#include <algorithm>

#include <cstring>

#include <vector>



#include "board.h"

#include "application.h"

#include "vietnamese_lunar_calendar.h"

#include "weather_icons.h"

#include "weather/weather_service.h"

#include <wifi_manager.h>

#include "boards/common/music.h"

LV_FONT_DECLARE(font_noto_sans_basic_14_1);



static uint32_t MapVietnameseChar(uint32_t unicode_letter) {

    // Toàn bộ bảng chữ cái tiếng Việt (chữ hoa và chữ thường với đầy đủ 5 dấu: sắc, huyền, hỏi, ngã, nặng)

    // đã được tích hợp 100% trực tiếp vào bộ font hệ thống.

    return unicode_letter;

}



static lv_font_t font_noto_sans_custom_16_4;

static bool CustomFontGetGlyphDsc16(const lv_font_t* font, lv_font_glyph_dsc_t* dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next) {

    unicode_letter = MapVietnameseChar(unicode_letter);

    return lv_font_get_glyph_dsc_fmt_txt(font, dsc_out, unicode_letter, unicode_letter_next);

}



static lv_font_t font_noto_sans_custom_14_1;

static bool CustomFontGetGlyphDsc14(const lv_font_t* font, lv_font_glyph_dsc_t* dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next) {

    unicode_letter = MapVietnameseChar(unicode_letter);

    return lv_font_get_glyph_dsc_fmt_txt(font, dsc_out, unicode_letter, unicode_letter_next);

}



static lv_font_t font_noto_sans_custom_20_4;

static bool CustomFontGetGlyphDsc20(const lv_font_t* font, lv_font_glyph_dsc_t* dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next) {

    unicode_letter = MapVietnameseChar(unicode_letter);

    return lv_font_get_glyph_dsc_fmt_txt(font, dsc_out, unicode_letter, unicode_letter_next);

}



LV_FONT_DECLARE(font_noto_sans_basic_16_4);

LV_FONT_DECLARE(font_noto_sans_basic_20_4);

LV_FONT_DECLARE(font_noto_sans_basic_30_4);

LV_FONT_DECLARE(font_noto_sans_basic_20_4);

#if LV_FONT_MONTSERRAT_40

LV_FONT_DECLARE(lv_font_montserrat_40);

#endif

#if LV_FONT_MONTSERRAT_12

LV_FONT_DECLARE(lv_font_montserrat_12);

#endif

#if LV_FONT_MONTSERRAT_10

LV_FONT_DECLARE(lv_font_montserrat_10);

#endif



const LcdDisplay::FmChannel LcdDisplay::kFmChannels[4] = {
    {"Hà Nội 90", "FM 90.0 MHz", "http://14.162.146.90:8000/HANOI90"},
    {"VOV1", "Thời Sự 24/7", "http://113.164.229.226:7025/;stream"},
    {"Bình Định", "FM Miền Trung", "http://14.224.130.186:8000/binhdinh"},
    {"Viet Radio", "Ca Nhạc & Tin", "http://71.4.56.20/3rdout/radio.hou.std2/icecast.audio"}
};



#define TAG "LcdDisplay"



LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

LV_FONT_DECLARE(BUILTIN_ICON_FONT);

LV_FONT_DECLARE(font_material_symbols_30_4);

LV_FONT_DECLARE(font_noto_emoji_30_4);



static bool IsChineseChar(uint32_t cp) {

    return (cp >= 0x4E00 && cp <= 0x9FFF) ||    // CJK Unified Ideographs

           (cp >= 0x3400 && cp <= 0x4DBF) ||    // CJK Extension A

           (cp >= 0x20000 && cp <= 0x2FA1F) ||  // CJK Extension B-F

           (cp >= 0x3000 && cp <= 0x303F) ||    // CJK Symbols and Punctuation

           (cp >= 0xF900 && cp <= 0xFAFF) ||    // CJK Compatibility Ideographs

           (cp >= 0xFF00 && cp <= 0xFF60);      // Fullwidth forms

}



static std::string FilterChinese(const char* content) {

    if (content == nullptr) {

        return "";

    }

    std::string text = content;

    const char* lili = "\xE8\x8E\x89\xE8\x8E\x89";

    size_t pos = 0;

    while ((pos = text.find(lili, pos)) != std::string::npos) {

        text.replace(pos, 6, "Hi Lily");

        pos += 7;

    }

    const char* li = "\xE8\x8E\x89";

    pos = 0;

    while ((pos = text.find(li, pos)) != std::string::npos) {

        text.replace(pos, 3, "Lily");

        pos += 4;

    }



    std::string result;

    result.reserve(text.size());

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text.c_str());

    size_t len = text.length();

    size_t i = 0;

    while (i < len) {

        uint32_t cp = 0;

        size_t char_bytes = 1;

        if (ptr[i] < 0x80) {

            cp = ptr[i];

            char_bytes = 1;

        } else if ((ptr[i] & 0xE0) == 0xC0) {

            if (i + 1 < len) {

                cp = ((ptr[i] & 0x1F) << 6) | (ptr[i+1] & 0x3F);

                char_bytes = 2;

            } else {

                char_bytes = 1;

            }

        } else if ((ptr[i] & 0xF0) == 0xE0) {

            if (i + 2 < len) {

                cp = ((ptr[i] & 0x0F) << 12) | ((ptr[i+1] & 0x3F) << 6) | (ptr[i+2] & 0x3F);

                char_bytes = 3;

            } else {

                char_bytes = 1;

            }

        } else if ((ptr[i] & 0xF8) == 0xF0) {

            if (i + 3 < len) {

                cp = ((ptr[i] & 0x07) << 18) | ((ptr[i+1] & 0x3F) << 12) | ((ptr[i+2] & 0x3F) << 6) | (ptr[i+3] & 0x3F);

                char_bytes = 4;

            } else {

                char_bytes = 1;

            }

        }



        if (IsChineseChar(cp)) {

            i += char_bytes;

        } else {

            for (size_t b = 0; b < char_bytes; ++b) {

                result.push_back(text[i + b]);

            }

            i += char_bytes;

        }

    }

    return result;

}





void LcdDisplay::InitializeLcdThemes() {

    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);

    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);

    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_material_symbols_30_4);

    auto emoji_font = std::make_shared<LvglBuiltInFont>(&font_noto_emoji_30_4);



    // light theme

    auto light_theme = new LvglTheme("light");

    light_theme->set_background_color(lv_color_hex(0xFFFFFF));

    light_theme->set_text_color(lv_color_hex(0x000000));

    light_theme->set_chat_background_color(lv_color_hex(0xE0E0E0));

    light_theme->set_user_bubble_color(lv_color_hex(0x00FF00));

    light_theme->set_assistant_bubble_color(lv_color_hex(0xDDDDDD));

    light_theme->set_system_bubble_color(lv_color_hex(0xFFFFFF));

    light_theme->set_system_text_color(lv_color_hex(0x000000));

    light_theme->set_border_color(lv_color_hex(0x000000));

    light_theme->set_low_battery_color(lv_color_hex(0x000000));

    light_theme->set_text_font(text_font);

    light_theme->set_icon_font(icon_font);

    light_theme->set_large_icon_font(large_icon_font);

    light_theme->set_emoji_font(emoji_font);



    // dark theme

    auto dark_theme = new LvglTheme("dark");

    dark_theme->set_background_color(lv_color_hex(0x000000));

    dark_theme->set_text_color(lv_color_hex(0xFFFFFF));

    dark_theme->set_chat_background_color(lv_color_hex(0x1F1F1F));

    dark_theme->set_user_bubble_color(lv_color_hex(0x00FF00));

    dark_theme->set_assistant_bubble_color(lv_color_hex(0x222222));

    dark_theme->set_system_bubble_color(lv_color_hex(0x000000));

    dark_theme->set_system_text_color(lv_color_hex(0xFFFFFF));

    dark_theme->set_border_color(lv_color_hex(0xFFFFFF));

    dark_theme->set_low_battery_color(lv_color_hex(0xFF0000));

    dark_theme->set_text_font(text_font);

    dark_theme->set_icon_font(icon_font);

    dark_theme->set_large_icon_font(large_icon_font);

    dark_theme->set_emoji_font(emoji_font);



    auto& theme_manager = LvglThemeManager::GetInstance();

    theme_manager.RegisterTheme("light", light_theme);

    theme_manager.RegisterTheme("dark", dark_theme);

}



LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,

                       int height)

    : panel_io_(panel_io), panel_(panel) {

    width_ = width;

    height_ = height;



    // Initialize LCD themes

    InitializeLcdThemes();



    // Load theme from settings

    Settings settings("display", false);

    std::string theme_name = settings.GetString("theme", "light");

    current_theme_ = LvglThemeManager::GetInstance().GetTheme(theme_name);



    // Create a timer to hide the preview image

    esp_timer_create_args_t preview_timer_args = {

        .callback =

            [](void* arg) {

                LcdDisplay* display = static_cast<LcdDisplay*>(arg);

                display->SetPreviewImage(nullptr);

            },

        .arg = this,

        .dispatch_method = ESP_TIMER_TASK,

        .name = "preview_timer",

        .skip_unhandled_events = false,

    };

    esp_timer_create(&preview_timer_args, &preview_timer_);

}



SpiLcdDisplay::SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,

                             int width, int height, int offset_x, int offset_y, bool mirror_x,

                             bool mirror_y, bool swap_xy)

    : LcdDisplay(panel_io, panel, width, height) {

    // draw white

    std::vector<uint16_t> buffer(width_, 0xFFFF);

    for (int y = 0; y < height_; y++) {

        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());

    }



    // Set the display to on

    ESP_LOGI(TAG, "Turning display on");

    {

        esp_err_t __err = esp_lcd_panel_disp_on_off(panel_, true);

        if (__err == ESP_ERR_NOT_SUPPORTED) {

            ESP_LOGW(TAG, "Panel does not support disp_on_off; assuming ON");

        } else {

            ESP_ERROR_CHECK(__err);

        }

    }



    ESP_LOGI(TAG, "Initialize LVGL library");

    lv_init();



#if CONFIG_SPIRAM

    // lv image cache, currently only PNG is supported

    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;

    if (psram_size_mb >= 8) {

#if CONFIG_LV_USE_BUILTIN_MALLOC

        // Base TLSF pool is CONFIG_LV_MEM_SIZE_KILOBYTES in PSRAM; grow it so

        // the 2MB decoded-image cache does not exhaust a 2MB-PSRAM-safe pool.

        // TLSF rejects any single pool larger than LV_MEM_SIZE when

        // LV_MEM_POOL_EXPAND_SIZE is 0 (block_size_max = 1 << ceil(log2(LV_MEM_SIZE))).

        const size_t extra_total = 2560 * 1024;

        const size_t chunk_size = LV_MEM_SIZE;

        size_t added = 0;

        while (added < extra_total) {

            size_t this_chunk = extra_total - added;

            if (this_chunk > chunk_size) {

                this_chunk = chunk_size;

            }

            void* pool = heap_caps_malloc(this_chunk, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

            if (pool == nullptr || lv_mem_add_pool(pool, this_chunk) == nullptr) {

                if (pool != nullptr) {

                    heap_caps_free(pool);

                }

                ESP_LOGE(TAG, "Failed to add %uKB LVGL PSRAM pool (added %uKB)",

                         (unsigned)(this_chunk / 1024), (unsigned)(added / 1024));

                break;

            }

            added += this_chunk;

        }

        if (added == extra_total) {

            ESP_LOGI(TAG, "Added %uKB LVGL pool in PSRAM", (unsigned)(added / 1024));

        }

#endif

        lv_image_cache_resize(2 * 1024 * 1024, true);

        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");

    } else if (psram_size_mb >= 2) {

        lv_image_cache_resize(512 * 1024, true);

        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");

    }

#endif



    ESP_LOGI(TAG, "Initialize LVGL port");

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();

    port_cfg.task_priority = 1;

#if CONFIG_SOC_CPU_CORES_NUM > 1

    port_cfg.task_affinity = 1;

#endif

    lvgl_port_init(&port_cfg);



    ESP_LOGI(TAG, "Adding LCD display");

    // SPI LCD still needs an internal DMA bounce buffer for PSRAM sources.

    // A full-frame transfer (~150KB) cannot allocate that bounce buffer, so

    // keep a small DMA strip in internal SRAM and partial refresh.

    const lvgl_port_display_cfg_t display_cfg = {

        .io_handle = panel_io_,

        .panel_handle = panel_,

        .control_handle = nullptr,

        .buffer_size = static_cast<uint32_t>(width_ * 20),

        .double_buffer = false,

        .trans_size = 0,

        .hres = static_cast<uint32_t>(width_),

        .vres = static_cast<uint32_t>(height_),

        .monochrome = false,

        .rotation =

            {

                .swap_xy = swap_xy,

                .mirror_x = mirror_x,

                .mirror_y = mirror_y,

            },

        .color_format = LV_COLOR_FORMAT_RGB565,

        .flags =

            {

                .buff_dma = 1,

                .buff_spiram = 0,

                .sw_rotate = 0,

                .swap_bytes = 1,

                .full_refresh = 0,

                .direct_mode = 0,

            },

    };



    display_ = lvgl_port_add_disp(&display_cfg);

    if (display_ == nullptr) {

        ESP_LOGE(TAG, "Failed to add display");

        return;

    }



    if (offset_x != 0 || offset_y != 0) {

        lv_display_set_offset(display_, offset_x, offset_y);

    }

}



// RGB LCD implementation

RgbLcdDisplay::RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,

                             int width, int height, int offset_x, int offset_y, bool mirror_x,

                             bool mirror_y, bool swap_xy)

    : LcdDisplay(panel_io, panel, width, height) {

    // draw white

    std::vector<uint16_t> buffer(width_, 0xFFFF);

    for (int y = 0; y < height_; y++) {

        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());

    }



    ESP_LOGI(TAG, "Initialize LVGL library");

    lv_init();



    ESP_LOGI(TAG, "Initialize LVGL port");

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();

    port_cfg.task_priority = 1;

    port_cfg.timer_period_ms = 50;

    lvgl_port_init(&port_cfg);



    ESP_LOGI(TAG, "Adding LCD display");

    const lvgl_port_display_cfg_t display_cfg = {

        .io_handle = panel_io_,

        .panel_handle = panel_,

        .buffer_size = static_cast<uint32_t>(width_ * 20),

        .double_buffer = true,

        .hres = static_cast<uint32_t>(width_),

        .vres = static_cast<uint32_t>(height_),

        .rotation =

            {

                .swap_xy = swap_xy,

                .mirror_x = mirror_x,

                .mirror_y = mirror_y,

            },

        .flags =

            {

                .buff_dma = 1,

                .swap_bytes = 0,

                .full_refresh = 1,

                .direct_mode = 1,

            },

    };



    const lvgl_port_display_rgb_cfg_t rgb_cfg = {.flags = {

                                                     .bb_mode = true,

                                                     .avoid_tearing = true,

                                                 }};



    display_ = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);

    if (display_ == nullptr) {

        ESP_LOGE(TAG, "Failed to add RGB display");

        return;

    }



    if (offset_x != 0 || offset_y != 0) {

        lv_display_set_offset(display_, offset_x, offset_y);

    }

}



MipiLcdDisplay::MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,

                               int width, int height, int offset_x, int offset_y, bool mirror_x,

                               bool mirror_y, bool swap_xy)

    : LcdDisplay(panel_io, panel, width, height) {

    ESP_LOGI(TAG, "Initialize LVGL library");

    lv_init();



    ESP_LOGI(TAG, "Initialize LVGL port");

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();

    lvgl_port_init(&port_cfg);



    ESP_LOGI(TAG, "Adding LCD display");

    const lvgl_port_display_cfg_t disp_cfg = {

        .io_handle = panel_io,

        .panel_handle = panel,

        .control_handle = nullptr,

        .buffer_size = static_cast<uint32_t>(width_ * 50),

        .double_buffer = false,

        .hres = static_cast<uint32_t>(width_),

        .vres = static_cast<uint32_t>(height_),

        .monochrome = false,

        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */

        .rotation =

            {

                .swap_xy = swap_xy,

                .mirror_x = mirror_x,

                .mirror_y = mirror_y,

            },

        .flags =

            {

                .buff_dma = true,

                .buff_spiram = false,

                .sw_rotate = true,

            },

    };



    const lvgl_port_display_dsi_cfg_t dpi_cfg = {.flags = {

                                                     .avoid_tearing = false,

                                                 }};

    display_ = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);

    if (display_ == nullptr) {

        ESP_LOGE(TAG, "Failed to add display");

        return;

    }



    if (offset_x != 0 || offset_y != 0) {

        lv_display_set_offset(display_, offset_x, offset_y);

    }

}



LcdDisplay::~LcdDisplay() {

    SetPreviewImage(nullptr);



    // Clean up GIF controller

    if (gif_controller_) {

        gif_controller_->Stop();

        gif_controller_.reset();

    }



    if (preview_timer_ != nullptr) {

        esp_timer_stop(preview_timer_);

        esp_timer_delete(preview_timer_);

    }



    if (preview_image_ != nullptr) {

        lv_obj_del(preview_image_);

    }

    if (chat_message_label_ != nullptr) {

        lv_obj_del(chat_message_label_);

    }

    if (emoji_label_ != nullptr) {

        lv_obj_del(emoji_label_);

    }

    if (emoji_image_ != nullptr) {

        lv_obj_del(emoji_image_);

    }

    if (emoji_box_ != nullptr) {

        lv_obj_del(emoji_box_);

    }

    if (content_ != nullptr) {

        lv_obj_del(content_);

    }

    if (bottom_bar_ != nullptr) {

        lv_obj_del(bottom_bar_);

    }

    if (status_bar_ != nullptr) {

        lv_obj_del(status_bar_);

    }

    if (top_bar_ != nullptr) {

        lv_obj_del(top_bar_);

    }

    if (side_bar_ != nullptr) {

        lv_obj_del(side_bar_);

    }

    if (container_ != nullptr) {

        lv_obj_del(container_);

    }

    if (display_ != nullptr) {

        lv_display_delete(display_);

    }



    if (panel_ != nullptr) {

        esp_lcd_panel_del(panel_);

    }

    if (panel_io_ != nullptr) {

        esp_lcd_panel_io_del(panel_io_);

    }

}



bool LcdDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }



void LcdDisplay::Unlock() { lvgl_port_unlock(); }



#if CONFIG_USE_WECHAT_MESSAGE_STYLE

void LcdDisplay::SetupUI() {

    // Prevent duplicate calls - if already called, return early

    if (setup_ui_called_) {

        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");

        return;

    }



    Display::SetupUI();  // Mark SetupUI as called

    DisplayLockGuard lock(this);



    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);

    auto text_font = lvgl_theme->text_font()->font();

    auto icon_font = lvgl_theme->icon_font()->font();

    auto large_icon_font = lvgl_theme->large_icon_font()->font();



    auto screen = lv_screen_active();

    lv_obj_set_style_text_font(screen, text_font, 0);

    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);

    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);



    /* Container */

    container_ = lv_obj_create(screen);

    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);

    lv_obj_set_style_radius(container_, 0, 0);

    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);

    lv_obj_set_style_pad_all(container_, 0, 0);

    lv_obj_set_style_border_width(container_, 0, 0);

    lv_obj_set_style_pad_row(container_, 0, 0);

    lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);

    lv_obj_set_style_border_color(container_, lvgl_theme->border_color(), 0);



    /* Wallpaper image (displayed when not connected to WiFi) */

    wallpaper_image_ = lv_image_create(screen);

    lv_obj_set_size(wallpaper_image_, LV_HOR_RES, LV_VER_RES);

    lv_obj_align(wallpaper_image_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_style_pad_all(wallpaper_image_, 0, 0);

    lv_obj_set_style_border_width(wallpaper_image_, 0, 0);

    const lv_image_dsc_t* def_dsc = (width_ > height_) ? &boot_landscape_dsc : &boot_portrait_dsc;

    lv_image_set_src(wallpaper_image_, def_dsc);

    if (def_dsc->header.w > 0 && def_dsc->header.h > 0) {

        uint32_t zoom_w = (256 * width_) / def_dsc->header.w;

        uint32_t zoom_h = (256 * height_) / def_dsc->header.h;

        uint32_t zoom = (zoom_w > zoom_h) ? zoom_w : zoom_h;

        lv_image_set_scale(wallpaper_image_, zoom);

    }

    lv_image_set_inner_align(wallpaper_image_, LV_IMAGE_ALIGN_CENTER);



    /* Layer 1: Top bar - for status icons */

    top_bar_ = lv_obj_create(container_);

    lv_obj_set_size(top_bar_, LV_HOR_RES, LV_SIZE_CONTENT);

    lv_obj_set_style_radius(top_bar_, 0, 0);

    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_50, 0);  // 50% opacity background

    lv_obj_set_style_bg_color(top_bar_, lvgl_theme->background_color(), 0);

    lv_obj_set_style_border_width(top_bar_, 0, 0);

    lv_obj_set_style_pad_all(top_bar_, 0, 0);

    lv_obj_set_style_pad_top(top_bar_, lvgl_theme->spacing(2), 0);

    lv_obj_set_style_pad_bottom(top_bar_, lvgl_theme->spacing(2), 0);

    lv_obj_set_style_pad_left(top_bar_, lvgl_theme->spacing(4), 0);

    lv_obj_set_style_pad_right(top_bar_, lvgl_theme->spacing(4), 0);

    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);

    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,

                          LV_FLEX_ALIGN_CENTER);

    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);



    // Left container: Network icon & Date / Lunar calendar

    lv_obj_t* left_container = lv_obj_create(top_bar_);

    lv_obj_set_size(left_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_set_style_bg_opa(left_container, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(left_container, 0, 0);

    lv_obj_set_style_pad_all(left_container, 0, 0);

    lv_obj_set_flex_flow(left_container, LV_FLEX_FLOW_ROW);

    lv_obj_set_flex_align(left_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,

                          LV_FLEX_ALIGN_CENTER);



    calendar_label_ = lv_label_create(left_container);

    lv_label_set_text(calendar_label_, "");

    lv_obj_set_style_text_font(calendar_label_, &font_noto_sans_basic_20_4, 0);

    lv_obj_set_style_text_color(calendar_label_, lv_color_white(), 0);



    // Right icons container

    lv_obj_t* right_icons = lv_obj_create(top_bar_);

    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(right_icons, 0, 0);

    lv_obj_set_style_pad_all(right_icons, 0, 0);

    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);

    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,

                          LV_FLEX_ALIGN_CENTER);



    network_label_ = lv_label_create(right_icons);

    lv_label_set_text(network_label_, "");

    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);



    mute_label_ = lv_label_create(right_icons);

    lv_label_set_text(mute_label_, "");

    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);

    lv_obj_set_style_margin_left(mute_label_, lvgl_theme->spacing(2), 0);



    battery_label_ = lv_label_create(right_icons);

    lv_label_set_text(battery_label_, "");

    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);

    lv_obj_set_style_margin_left(battery_label_, lvgl_theme->spacing(2), 0);



    /* Layer 2: Status bar - for center text labels */

    status_bar_ = lv_obj_create(screen);

    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);

    lv_obj_set_style_radius(status_bar_, 0, 0);

    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background

    lv_obj_set_style_border_width(status_bar_, 0, 0);

    lv_obj_set_style_pad_all(status_bar_, 0, 0);

    lv_obj_set_style_pad_top(status_bar_, lvgl_theme->spacing(2), 0);

    lv_obj_set_style_pad_bottom(status_bar_, lvgl_theme->spacing(2), 0);

    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning

    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 26);       // Positioned below top bar to avoid wifi icon



    notification_label_ = lv_label_create(status_bar_);

    lv_obj_set_width(notification_label_, LV_HOR_RES * 0.8);

    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);

    lv_label_set_text(notification_label_, "");

    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);



    status_label_ = lv_label_create(status_bar_);

    lv_obj_set_width(status_label_, LV_HOR_RES * 0.8);

    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);

    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);



    idle_clock_label_ = lv_label_create(screen);

    lv_label_set_text(idle_clock_label_, "");

#if LV_FONT_MONTSERRAT_40

    lv_obj_set_style_text_font(idle_clock_label_, &lv_font_montserrat_40, 0);

#else

    lv_obj_set_style_text_font(idle_clock_label_, &font_noto_sans_basic_20_4, 0);

#endif

    lv_obj_set_style_text_color(idle_clock_label_, lv_color_white(), 0);

    lv_obj_set_style_bg_color(idle_clock_label_, lv_color_black(), 0);

    lv_obj_set_style_bg_opa(idle_clock_label_, LV_OPA_30, 0);

    lv_obj_set_style_radius(idle_clock_label_, 8, 0);

    lv_obj_set_style_pad_hor(idle_clock_label_, 12, 0);

    lv_obj_set_style_pad_ver(idle_clock_label_, 4, 0);

    lv_obj_align(idle_clock_label_, LV_ALIGN_CENTER, 0, 10);

    lv_obj_add_flag(idle_clock_label_, LV_OBJ_FLAG_HIDDEN);



    weather_box_ = lv_obj_create(screen);

    lv_obj_set_size(weather_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_set_style_bg_color(weather_box_, lv_color_black(), 0);

    lv_obj_set_style_bg_opa(weather_box_, LV_OPA_30, 0);

    lv_obj_set_style_radius(weather_box_, 8, 0);

    lv_obj_set_style_border_width(weather_box_, 0, 0);

    lv_obj_set_style_pad_hor(weather_box_, 10, 0);

    lv_obj_set_style_pad_ver(weather_box_, 4, 0);

    lv_obj_set_flex_flow(weather_box_, LV_FLEX_FLOW_ROW);

    lv_obj_set_flex_align(weather_box_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_set_style_pad_column(weather_box_, 6, 0);

    lv_obj_align(weather_box_, LV_ALIGN_CENTER, 0, (height_ > width_) ? 70 : 58);

    lv_obj_add_flag(weather_box_, LV_OBJ_FLAG_HIDDEN);



    weather_icon_ = lv_image_create(weather_box_);

    lv_image_set_src(weather_icon_, &weather_icon_cloud_dsc);



    weather_label_ = lv_label_create(weather_box_);

    lv_obj_set_style_text_font(weather_label_, &font_noto_sans_basic_16_4, 0);

    lv_obj_set_style_text_color(weather_label_, lv_color_white(), 0);

    lv_label_set_text(weather_label_, "");



    // --- STANDBY FULL SCREEN DASHBOARD (Matching User Reference) ---

    standby_box_ = lv_obj_create(screen);

    lv_obj_set_size(standby_box_, LV_HOR_RES, LV_VER_RES);

    lv_obj_set_style_bg_color(standby_box_, lv_color_hex(0x0f172a), 0);

    lv_obj_set_style_bg_opa(standby_box_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(standby_box_, 0, 0);

    lv_obj_set_style_border_width(standby_box_, 0, 0);

    lv_obj_set_style_pad_all(standby_box_, 0, 0);

    lv_obj_set_scrollbar_mode(standby_box_, LV_SCROLLBAR_MODE_OFF);

    lv_obj_clear_flag(standby_box_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);



    // Initialize custom fonts supporting Vietnamese glyphs

    font_noto_sans_custom_16_4 = font_noto_sans_basic_16_4;

    font_noto_sans_custom_16_4.get_glyph_dsc = CustomFontGetGlyphDsc16;

    font_noto_sans_custom_14_1 = font_noto_sans_basic_14_1;

    font_noto_sans_custom_14_1.get_glyph_dsc = CustomFontGetGlyphDsc14;

    font_noto_sans_custom_20_4 = font_noto_sans_basic_20_4;

    font_noto_sans_custom_20_4.get_glyph_dsc = CustomFontGetGlyphDsc20;



    Settings settings("display", false);

    standby_text_color_ = (uint32_t)settings.GetInt("text_color", 0xFFFFFF);

    lv_color_t text_col = lv_color_hex(standby_text_color_);

    // Standby Top-Right Status Bar (WiFi, Battery %, Battery Icon)
    standby_status_bar_ = lv_obj_create(standby_box_);
    lv_obj_set_size(standby_status_bar_, width_, 30);
    lv_obj_set_pos(standby_status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(standby_status_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(standby_status_bar_, 0, 0);
    lv_obj_set_style_pad_all(standby_status_bar_, 0, 0);
    lv_obj_clear_flag(standby_status_bar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(standby_status_bar_, LV_OBJ_FLAG_CLICKABLE);

    standby_right_icons_ = lv_obj_create(standby_status_bar_);
    lv_obj_set_size(standby_right_icons_, LV_SIZE_CONTENT, 30);
    lv_obj_align(standby_right_icons_, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_opa(standby_right_icons_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(standby_right_icons_, 0, 0);
    lv_obj_set_style_pad_all(standby_right_icons_, 0, 0);
    lv_obj_set_flex_flow(standby_right_icons_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(standby_right_icons_, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(standby_right_icons_, 6, 0);
    lv_obj_clear_flag(standby_right_icons_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(standby_right_icons_, LV_OBJ_FLAG_CLICKABLE);

    standby_wifi_label_ = lv_label_create(standby_right_icons_);
    lv_label_set_text(standby_wifi_label_, "");
    lv_obj_set_style_text_font(standby_wifi_label_, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(standby_wifi_label_, text_col, 0);
    lv_obj_clear_flag(standby_wifi_label_, LV_OBJ_FLAG_CLICKABLE);

    standby_battery_pct_label_ = lv_label_create(standby_right_icons_);
    lv_label_set_text(standby_battery_pct_label_, "");
    lv_obj_set_style_text_font(standby_battery_pct_label_, &font_noto_sans_basic_14_1, 0);
    lv_obj_set_style_text_color(standby_battery_pct_label_, text_col, 0);
    lv_obj_clear_flag(standby_battery_pct_label_, LV_OBJ_FLAG_CLICKABLE);

    standby_battery_label_ = lv_label_create(standby_right_icons_);
    lv_label_set_text(standby_battery_label_, "");
    lv_obj_set_style_text_font(standby_battery_label_, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(standby_battery_label_, text_col, 0);
    lv_obj_clear_flag(standby_battery_label_, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_move_foreground(standby_status_bar_);




    // Clock (HH:MM) - All white text

    standby_clock_ = lv_label_create(standby_box_);

#if LV_FONT_MONTSERRAT_40

    lv_obj_set_style_text_font(standby_clock_, &lv_font_montserrat_40, 0);

#else

    lv_obj_set_style_text_font(standby_clock_, &font_noto_sans_basic_30_4, 0);

#endif

    lv_obj_set_style_text_color(standby_clock_, text_col, 0);

    lv_label_set_text(standby_clock_, "00:00");



    // Seconds (:SS smaller font)

    standby_sec_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_sec_, &font_noto_sans_basic_20_4, 0);

    lv_obj_set_style_text_color(standby_sec_, text_col, 0);

    lv_label_set_text(standby_sec_, ":00");



    // Weekday (Thứ Tư, etc.)

    standby_weekday_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_weekday_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_weekday_, text_col, 0);

    lv_label_set_text(standby_weekday_, "");



    // Date (dd/mm/yyyy)

    standby_date_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_date_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_date_, text_col, 0);

    lv_label_set_text(standby_date_, "");



    // Lunar

    standby_lunar_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_lunar_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_lunar_, text_col, 0);

    lv_label_set_text(standby_lunar_, "");



    // Year (hidden)

    standby_year_ = lv_label_create(standby_box_);

    lv_obj_add_flag(standby_year_, LV_OBJ_FLAG_HIDDEN);



    // Weather Main Icon

    standby_weather_icon_ = lv_image_create(standby_box_);

    lv_image_set_src(standby_weather_icon_, &weather_icon_sun_cloud_48_dsc);



    // Main Temp

    standby_temp_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_temp_, &font_noto_sans_basic_30_4, 0);

    lv_obj_set_style_text_color(standby_temp_, text_col, 0);

    lv_label_set_text(standby_temp_, "--°C");



    // City

    standby_city_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_city_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_city_, text_col, 0);

    lv_label_set_text(standby_city_, "Đang cập nhật...");



    // Condition

    standby_condition_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_condition_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_condition_, text_col, 0);

    lv_label_set_text(standby_condition_, "Trời có mây");



    // Humidity

    standby_humidity_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_humidity_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_humidity_, text_col, 0);

    lv_label_set_text(standby_humidity_, "Độ ẩm: --%");



    // Wind

    standby_wind_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_wind_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_wind_, text_col, 0);

    lv_label_set_text(standby_wind_, "Gió: -- km/h");



    // 4 Forecast Cards

    for (int i = 0; i < 4; ++i) {

        forecast_cards_[i].card = lv_obj_create(standby_box_);

        lv_obj_set_style_bg_color(forecast_cards_[i].card, lv_color_hex(0x1e293b), 0);

        lv_obj_set_style_bg_opa(forecast_cards_[i].card, LV_OPA_COVER, 0);

        lv_obj_set_style_radius(forecast_cards_[i].card, 10, 0);

        lv_obj_set_style_border_width(forecast_cards_[i].card, 0, 0);

        lv_obj_set_style_pad_all(forecast_cards_[i].card, 4, 0);

        lv_obj_clear_flag(forecast_cards_[i].card, LV_OBJ_FLAG_SCROLLABLE);



        forecast_cards_[i].day = lv_label_create(forecast_cards_[i].card);

        lv_obj_set_style_text_font(forecast_cards_[i].day, &font_noto_sans_custom_14_1, 0);

        lv_obj_set_style_text_color(forecast_cards_[i].day, text_col, 0);

        lv_label_set_text(forecast_cards_[i].day, "--");



        forecast_cards_[i].icon = lv_image_create(forecast_cards_[i].card);

        lv_image_set_src(forecast_cards_[i].icon, &weather_icon_cloud_24_dsc);



        forecast_cards_[i].temp = lv_label_create(forecast_cards_[i].card);

        lv_obj_set_style_text_font(forecast_cards_[i].temp, &font_noto_sans_basic_16_4, 0);

        lv_obj_set_style_text_color(forecast_cards_[i].temp, text_col, 0);

        lv_label_set_text(forecast_cards_[i].temp, "--°C");

    }



    LayoutStandbyWidgets(height_ > width_);
    UpdateStandbyStatusBar();

    CreateStandby2UI();

    CreateStandby2UI();

    CreateMenuUI();

    CreateQuickSettingsUI();

    CreateCalendarUI();

    CreateWeatherDetailUI();

    CreateClockUI();

    CreateStopwatchUI();

    CreateTimerUI();

    CreateExchangeRatesUI();

    CreateCalculatorUI();

    CreateFlashlightUI();

    CreateFmRadioUI();





    /* Content - Chat area */

    content_ = lv_obj_create(container_);

    lv_obj_set_style_radius(content_, 0, 0);

    lv_obj_set_width(content_, LV_HOR_RES);

    lv_obj_set_flex_grow(content_, 1);

    lv_obj_set_style_pad_all(content_, lvgl_theme->spacing(4), 0);

    lv_obj_set_style_border_width(content_, 0, 0);

    lv_obj_set_style_bg_color(content_, lvgl_theme->chat_background_color(),

                              0);  // Background for chat area



    // Enable scrolling for chat content

    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_scroll_dir(content_, LV_DIR_VER);



    // Create a flex container for chat messages

    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);

    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_set_style_pad_row(content_, lvgl_theme->spacing(4), 0);  // Space between messages



    // We'll create chat messages dynamically in SetChatMessage

    chat_message_label_ = nullptr;



    low_battery_popup_ = lv_obj_create(screen);

    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);

    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));

    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);

    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);

    low_battery_label_ = lv_label_create(low_battery_popup_);

    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);

    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);

    lv_obj_center(low_battery_label_);

    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);



    emoji_image_ = lv_img_create(screen);

    lv_obj_align(emoji_image_, LV_ALIGN_TOP_MID, 0,

                 text_font->line_height + lvgl_theme->spacing(8));



    // Display AI logo while booting

    emoji_label_ = lv_label_create(screen);

    lv_obj_center(emoji_label_);

    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);

    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);

    lv_label_set_text(emoji_label_, MATERIAL_SYMBOLS_ROBOT_2);
    StartBootAnimation();

}

#if CONFIG_IDF_TARGET_ESP32P4

#define MAX_MESSAGES 40

#else

#define MAX_MESSAGES 20

#endif

void LcdDisplay::SetChatMessage(const char* role, const char* content) {

    if (!setup_ui_called_) {

        ESP_LOGW(TAG, "SetChatMessage('%s', '%s') called before SetupUI() - message will be lost!",

                 role, content);

    }

    DisplayLockGuard lock(this);

    std::string filtered_msg = FilterChinese(content);

    content = filtered_msg.c_str();

    if (content_ == nullptr) {

        if (setup_ui_called_) {

            ESP_LOGW(TAG,

                     "SetChatMessage('%s', '%s') failed: content_ is nullptr (SetupUI() was called "

                     "but container not created)",

                     role, content);

        }

        return;

    }



    // Check if message count exceeds limit

    uint32_t child_count = lv_obj_get_child_cnt(content_);

    if (child_count >= MAX_MESSAGES) {

        // Delete the oldest message (first child object)

        lv_obj_t* first_child = lv_obj_get_child(content_, 0);

        if (first_child != nullptr) {

            lv_obj_del(first_child);

            // Refresh child count after deletion

            child_count = lv_obj_get_child_cnt(content_);

        }

        // Scroll to the last message immediately (get last_child after deletion)

        if (child_count > 0) {

            lv_obj_t* last_child = lv_obj_get_child(content_, child_count - 1);

            if (last_child != nullptr && lv_obj_is_valid(last_child)) {

                lv_obj_scroll_to_view_recursive(last_child, LV_ANIM_OFF);

            }

        }

    }



    // Collapse system messages (if it's a system message, check if the last message is also a

    // system message)

    if (strcmp(role, "system") == 0) {

        // Refresh child count to get accurate count after potential deletion above

        child_count = lv_obj_get_child_cnt(content_);

        if (child_count > 0) {

            // Get the last message container

            lv_obj_t* last_container = lv_obj_get_child(content_, child_count - 1);

            if (last_container != nullptr && lv_obj_is_valid(last_container) &&

                lv_obj_get_child_cnt(last_container) > 0) {

                // Get the bubble inside the container

                lv_obj_t* last_bubble = lv_obj_get_child(last_container, 0);

                if (last_bubble != nullptr && lv_obj_is_valid(last_bubble)) {

                    // Check if bubble type is system message

                    void* bubble_type_ptr = lv_obj_get_user_data(last_bubble);

                    if (bubble_type_ptr != nullptr &&

                        strcmp((const char*)bubble_type_ptr, "system") == 0) {

                        // If the last message is also a system message, delete it

                        lv_obj_del(last_container);

                    }

                }

            }

        }

    } else {

        // Hide the centered AI logo

        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

    }



    // Avoid empty message boxes

    if (strlen(content) == 0) {

        return;

    }



    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);



    // Create a message bubble

    lv_obj_t* msg_bubble = lv_obj_create(content_);

    lv_obj_set_style_radius(msg_bubble, 8, 0);

    lv_obj_set_scrollbar_mode(msg_bubble, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_border_width(msg_bubble, 0, 0);

    lv_obj_set_style_pad_all(msg_bubble, lvgl_theme->spacing(4), 0);



    // Create the message text

    lv_obj_t* msg_text = lv_label_create(msg_bubble);

    lv_label_set_text(msg_text, content);



    // Calculate bubble width constraints

    lv_coord_t max_width = LV_HOR_RES * 85 / 100 - 16;  // 85% of screen width

    lv_coord_t min_width = 20;



    // Let LVGL calculate the natural text width first

    lv_obj_set_width(msg_text, LV_SIZE_CONTENT);

    lv_obj_update_layout(msg_text);

    lv_coord_t text_width = lv_obj_get_width(msg_text);



    // Ensure text width is not less than minimum width

    if (text_width < min_width) {

        text_width = min_width;

    }



    // Constrain to max width

    lv_coord_t bubble_width = (text_width < max_width) ? text_width : max_width;



    // Set message text width

    lv_obj_set_width(msg_text, bubble_width);

    lv_label_set_long_mode(msg_text, LV_LABEL_LONG_WRAP);



    // Set bubble width

    lv_obj_set_width(msg_bubble, bubble_width);

    lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);



    // Set alignment and style based on message role

    if (strcmp(role, "user") == 0) {

        // User messages are right-aligned with green background

        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->user_bubble_color(), 0);

        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);

        // Set text color for contrast

        lv_obj_set_style_text_color(msg_text, lvgl_theme->text_color(), 0);



        // Set custom attribute to mark bubble type

        lv_obj_set_user_data(msg_bubble, (void*)"user");



        // Set appropriate width for content

        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);

        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);



        // Don't grow

        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);

    } else if (strcmp(role, "assistant") == 0) {

        // Assistant messages are left-aligned with white background

        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->assistant_bubble_color(), 0);

        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);

        // Set text color for contrast

        lv_obj_set_style_text_color(msg_text, lvgl_theme->text_color(), 0);



        // Set custom attribute to mark bubble type

        lv_obj_set_user_data(msg_bubble, (void*)"assistant");



        // Set appropriate width for content

        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);

        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);



        // Don't grow

        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);

    } else if (strcmp(role, "system") == 0) {

        // System messages are center-aligned with light gray background

        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->system_bubble_color(), 0);

        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);

        // Set text color for contrast

        lv_obj_set_style_text_color(msg_text, lvgl_theme->system_text_color(), 0);



        // Set custom attribute to mark bubble type

        lv_obj_set_user_data(msg_bubble, (void*)"system");



        // Set appropriate width for content

        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);

        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);



        // Don't grow

        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);

    }



    // Create a full-width container for user messages to ensure right alignment

    if (strcmp(role, "user") == 0) {

        // Create a full-width container

        lv_obj_t* container = lv_obj_create(content_);

        lv_obj_set_width(container, LV_HOR_RES);

        lv_obj_set_height(container, LV_SIZE_CONTENT);



        // Make container transparent and borderless

        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);

        lv_obj_set_style_border_width(container, 0, 0);

        lv_obj_set_style_pad_all(container, 0, 0);



        // Move the message bubble into this container

        lv_obj_set_parent(msg_bubble, container);



        // Right align the bubble in the container

        lv_obj_align(msg_bubble, LV_ALIGN_RIGHT_MID, -25, 0);



        // Auto-scroll to this container

        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);

    } else if (strcmp(role, "system") == 0) {

        // Create full-width container for system messages to ensure center alignment

        lv_obj_t* container = lv_obj_create(content_);

        lv_obj_set_width(container, LV_HOR_RES);

        lv_obj_set_height(container, LV_SIZE_CONTENT);



        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);

        lv_obj_set_style_border_width(container, 0, 0);

        lv_obj_set_style_pad_all(container, 0, 0);



        lv_obj_set_parent(msg_bubble, container);

        lv_obj_align(msg_bubble, LV_ALIGN_CENTER, 0, 0);

        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);

    } else {

        // For assistant messages

        // Left align assistant messages

        lv_obj_align(msg_bubble, LV_ALIGN_LEFT_MID, 0, 0);



        // Auto-scroll to the message bubble

        lv_obj_scroll_to_view_recursive(msg_bubble, LV_ANIM_ON);

    }



    // Store reference to the latest message label

    chat_message_label_ = msg_text;

}



void LcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {

    DisplayLockGuard lock(this);

    if (content_ == nullptr) {

        return;

    }



    if (image == nullptr) {

        return;

    }



    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);

    // Create a message bubble for image preview

    lv_obj_t* img_bubble = lv_obj_create(content_);

    lv_obj_set_style_radius(img_bubble, 8, 0);

    lv_obj_set_scrollbar_mode(img_bubble, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_border_width(img_bubble, 0, 0);

    lv_obj_set_style_pad_all(img_bubble, lvgl_theme->spacing(4), 0);



    // Set image bubble background color (similar to system message)

    lv_obj_set_style_bg_color(img_bubble, lvgl_theme->assistant_bubble_color(), 0);

    lv_obj_set_style_bg_opa(img_bubble, LV_OPA_70, 0);



    // Set custom attribute to mark bubble type

    lv_obj_set_user_data(img_bubble, (void*)"image");



    // Create the image object inside the bubble

    lv_obj_t* preview_image = lv_image_create(img_bubble);



    // Calculate appropriate size for the image

    lv_coord_t max_width = LV_HOR_RES * 70 / 100;   // 70% of screen width

    lv_coord_t max_height = LV_VER_RES * 50 / 100;  // 50% of screen height



    // Calculate zoom factor to fit within maximum dimensions

    auto img_dsc = image->image_dsc();

    lv_coord_t img_width = img_dsc->header.w;

    lv_coord_t img_height = img_dsc->header.h;

    if (img_width == 0 || img_height == 0) {

        img_width = max_width;

        img_height = max_height;

        ESP_LOGW(TAG, "Invalid image dimensions: %ld x %ld, using default dimensions: %ld x %ld",

                 img_width, img_height, max_width, max_height);

    }



    lv_coord_t zoom_w = (max_width * 256) / img_width;

    lv_coord_t zoom_h = (max_height * 256) / img_height;

    lv_coord_t zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;



    // Ensure zoom doesn't exceed 256 (100%)

    if (zoom > 256)

        zoom = 256;



    // Set image properties

    lv_image_set_src(preview_image, img_dsc);

    lv_image_set_scale(preview_image, zoom);



    // Add event handler to clean up LvglImage when image is deleted

    // We need to transfer ownership of the unique_ptr to the event callback

    LvglImage* raw_image = image.release();  // Release ownership of smart pointer

    lv_obj_add_event_cb(

        preview_image,

        [](lv_event_t* e) {

            LvglImage* img = (LvglImage*)lv_event_get_user_data(e);

            if (img != nullptr) {

                delete img;  // Properly release memory by deleting LvglImage object

            }

        },

        LV_EVENT_DELETE, (void*)raw_image);



    // Calculate actual scaled image dimensions

    lv_coord_t scaled_width = (img_width * zoom) / 256;

    lv_coord_t scaled_height = (img_height * zoom) / 256;



    // Set bubble size to be 16 pixels larger than the image (8 pixels on each side)

    lv_obj_set_width(img_bubble, scaled_width + 16);

    lv_obj_set_height(img_bubble, scaled_height + 16);



    // Don't grow in flex layout

    lv_obj_set_style_flex_grow(img_bubble, 0, 0);



    // Center the image within the bubble

    lv_obj_center(preview_image);



    // Left align the image bubble like assistant messages

    lv_obj_align(img_bubble, LV_ALIGN_LEFT_MID, 0, 0);



    // Auto-scroll to the image bubble

    lv_obj_scroll_to_view_recursive(img_bubble, LV_ANIM_ON);

}



void LcdDisplay::RemoveChatMessage(const char* role, const char* content) {

    DisplayLockGuard lock(this);

    if (content_ == nullptr || role == nullptr || content == nullptr) {

        return;

    }

    for (uint32_t i = 0; i < lv_obj_get_child_cnt(content_); ++i) {

        auto container = lv_obj_get_child(content_, i);

        auto bubble = container;

        auto bubble_role = static_cast<const char*>(lv_obj_get_user_data(bubble));

        if (bubble_role == nullptr && lv_obj_get_child_cnt(bubble) > 0) {

            bubble = lv_obj_get_child(bubble, 0);

            bubble_role = static_cast<const char*>(lv_obj_get_user_data(bubble));

        }

        if (bubble_role == nullptr || strcmp(bubble_role, role) != 0 ||

            lv_obj_get_child_cnt(bubble) == 0) {

            continue;

        }

        auto label = lv_obj_get_child(bubble, 0);

        if (strcmp(lv_label_get_text(label), content) != 0) {

            continue;

        }

        if (chat_message_label_ == label) {

            chat_message_label_ = nullptr;

        }

        lv_obj_del(container);

        if (lv_obj_get_child_cnt(content_) == 0 && emoji_label_ != nullptr) {

            lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

        }

        return;

    }

}



void LcdDisplay::ClearChatMessages() {

    DisplayLockGuard lock(this);

    if (content_ == nullptr) {

        return;

    }



    // Use lv_obj_clean to delete all children of content_ (chat message bubbles)

    lv_obj_clean(content_);



    // Reset chat_message_label_ as it has been deleted

    chat_message_label_ = nullptr;



    // Show the centered AI logo (emoji_label_) again

    if (emoji_label_ != nullptr) {

        lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

    }



    ESP_LOGI(TAG, "Chat messages cleared");

}

#else

void LcdDisplay::SetupUI() {

    // Prevent duplicate calls - if already called, return early

    if (setup_ui_called_) {

        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");

        return;

    }



    Display::SetupUI();  // Mark SetupUI as called

    DisplayLockGuard lock(this);

    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);

    auto text_font = lvgl_theme->text_font()->font();

    auto icon_font = lvgl_theme->icon_font()->font();

    auto large_icon_font = lvgl_theme->large_icon_font()->font();



    auto screen = lv_screen_active();

    lv_obj_set_style_text_font(screen, text_font, 0);

    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);

    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);



    /* Container - used as background */

    container_ = lv_obj_create(screen);

    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);

    lv_obj_set_style_radius(container_, 0, 0);

    lv_obj_set_style_pad_all(container_, 0, 0);

    lv_obj_set_style_border_width(container_, 0, 0);

    lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);

    lv_obj_set_style_border_color(container_, lvgl_theme->border_color(), 0);



    /* Wallpaper image (displayed when not connected to WiFi) */

    wallpaper_image_ = lv_image_create(screen);

    lv_obj_set_size(wallpaper_image_, LV_HOR_RES, LV_VER_RES);

    lv_obj_align(wallpaper_image_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_style_pad_all(wallpaper_image_, 0, 0);

    lv_obj_set_style_border_width(wallpaper_image_, 0, 0);

    const lv_image_dsc_t* def_dsc = (width_ > height_) ? &boot_landscape_dsc : &boot_portrait_dsc;

    lv_image_set_src(wallpaper_image_, def_dsc);

    if (def_dsc->header.w > 0 && def_dsc->header.h > 0) {

        uint32_t zoom_w = (256 * width_) / def_dsc->header.w;

        uint32_t zoom_h = (256 * height_) / def_dsc->header.h;

        uint32_t zoom = (zoom_w > zoom_h) ? zoom_w : zoom_h;

        lv_image_set_scale(wallpaper_image_, zoom);

    }

    lv_image_set_inner_align(wallpaper_image_, LV_IMAGE_ALIGN_CENTER);



    /* Bottom layer: emoji_box_ - centered display */

    emoji_box_ = lv_obj_create(screen);

    lv_obj_set_size(emoji_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_set_style_bg_opa(emoji_box_, LV_OPA_TRANSP, 0);

    lv_obj_set_style_pad_all(emoji_box_, 0, 0);

    lv_obj_set_style_border_width(emoji_box_, 0, 0);

    lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);



    emoji_label_ = lv_label_create(emoji_box_);

    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);

    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);

    lv_label_set_text(emoji_label_, MATERIAL_SYMBOLS_ROBOT_2);



    emoji_image_ = lv_img_create(emoji_box_);

    lv_obj_center(emoji_image_);

    lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);



    /* Middle layer: preview_image_ - centered display */

    preview_image_ = lv_image_create(screen);

    lv_obj_set_size(preview_image_, width_ / 2, height_ / 2);

    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);



    /* Layer 1: Top bar - for status icons */

    top_bar_ = lv_obj_create(screen);

    lv_obj_set_size(top_bar_, LV_HOR_RES, LV_SIZE_CONTENT);

    lv_obj_set_style_radius(top_bar_, 0, 0);

    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_50, 0);  // 50% opacity background

    lv_obj_set_style_bg_color(top_bar_, lvgl_theme->background_color(), 0);

    lv_obj_set_style_border_width(top_bar_, 0, 0);

    lv_obj_set_style_pad_all(top_bar_, 0, 0);

    lv_obj_set_style_pad_top(top_bar_, lvgl_theme->spacing(2), 0);

    lv_obj_set_style_pad_bottom(top_bar_, lvgl_theme->spacing(2), 0);

    lv_obj_set_style_pad_left(top_bar_, lvgl_theme->spacing(4), 0);

    lv_obj_set_style_pad_right(top_bar_, lvgl_theme->spacing(4), 0);

    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);

    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,

                          LV_FLEX_ALIGN_CENTER);

    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);

    lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);



    // Left container: Network icon & Date / Lunar calendar

    lv_obj_t* left_container = lv_obj_create(top_bar_);

    lv_obj_set_size(left_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_set_style_bg_opa(left_container, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(left_container, 0, 0);

    lv_obj_set_style_pad_all(left_container, 0, 0);

    lv_obj_set_flex_flow(left_container, LV_FLEX_FLOW_ROW);

    lv_obj_set_flex_align(left_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,

                          LV_FLEX_ALIGN_CENTER);



    calendar_label_ = lv_label_create(left_container);

    lv_label_set_text(calendar_label_, "");

    lv_obj_set_style_text_font(calendar_label_, &font_noto_sans_basic_20_4, 0);

    lv_obj_set_style_text_color(calendar_label_, lv_color_white(), 0);



    // Right icons container

    lv_obj_t* right_icons = lv_obj_create(top_bar_);

    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(right_icons, 0, 0);

    lv_obj_set_style_pad_all(right_icons, 0, 0);

    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);

    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,

                          LV_FLEX_ALIGN_CENTER);



    network_label_ = lv_label_create(right_icons);

    lv_label_set_text(network_label_, "");

    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);



    mute_label_ = lv_label_create(right_icons);

    lv_label_set_text(mute_label_, "");

    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);

    lv_obj_set_style_margin_left(mute_label_, lvgl_theme->spacing(2), 0);



    battery_label_ = lv_label_create(right_icons);

    lv_label_set_text(battery_label_, "");

    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);

    lv_obj_set_style_margin_left(battery_label_, lvgl_theme->spacing(2), 0);



    /* Layer 2: Status bar - for center text labels */

    status_bar_ = lv_obj_create(screen);

    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);

    lv_obj_set_style_radius(status_bar_, 0, 0);

    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background

    lv_obj_set_style_border_width(status_bar_, 0, 0);

    lv_obj_set_style_pad_all(status_bar_, 0, 0);

    lv_obj_set_style_pad_top(status_bar_, lvgl_theme->spacing(2), 0);

    lv_obj_set_style_pad_bottom(status_bar_, lvgl_theme->spacing(2), 0);

    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning

    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 26);       // Positioned below top bar to avoid wifi icon



    notification_label_ = lv_label_create(status_bar_);

    lv_obj_set_width(notification_label_, LV_HOR_RES * 0.85);

    lv_label_set_long_mode(notification_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);

    lv_label_set_text(notification_label_, "");

    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);



    status_label_ = lv_label_create(status_bar_);

    lv_obj_set_width(status_label_, LV_HOR_RES * 0.75);

    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);

    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);



    idle_clock_label_ = lv_label_create(screen);

    lv_label_set_text(idle_clock_label_, "");

#if LV_FONT_MONTSERRAT_40

    lv_obj_set_style_text_font(idle_clock_label_, &lv_font_montserrat_40, 0);

#else

    lv_obj_set_style_text_font(idle_clock_label_, &font_noto_sans_basic_20_4, 0);

#endif

    lv_obj_set_style_text_color(idle_clock_label_, lv_color_white(), 0);

    lv_obj_set_style_bg_color(idle_clock_label_, lv_color_black(), 0);

    lv_obj_set_style_bg_opa(idle_clock_label_, LV_OPA_30, 0);

    lv_obj_set_style_radius(idle_clock_label_, 8, 0);

    lv_obj_set_style_pad_hor(idle_clock_label_, 12, 0);

    lv_obj_set_style_pad_ver(idle_clock_label_, 4, 0);

    lv_obj_align(idle_clock_label_, LV_ALIGN_CENTER, 0, 10);

    lv_obj_add_flag(idle_clock_label_, LV_OBJ_FLAG_HIDDEN);



    weather_box_ = lv_obj_create(screen);

    lv_obj_set_size(weather_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_set_style_bg_color(weather_box_, lv_color_black(), 0);

    lv_obj_set_style_bg_opa(weather_box_, LV_OPA_30, 0);

    lv_obj_set_style_radius(weather_box_, 8, 0);

    lv_obj_set_style_border_width(weather_box_, 0, 0);

    lv_obj_set_style_pad_hor(weather_box_, 10, 0);

    lv_obj_set_style_pad_ver(weather_box_, 4, 0);

    lv_obj_set_flex_flow(weather_box_, LV_FLEX_FLOW_ROW);

    lv_obj_set_flex_align(weather_box_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_set_style_pad_column(weather_box_, 6, 0);

    lv_obj_align(weather_box_, LV_ALIGN_CENTER, 0, (height_ > width_) ? 70 : 58);

    lv_obj_add_flag(weather_box_, LV_OBJ_FLAG_HIDDEN);



    weather_icon_ = lv_image_create(weather_box_);

    lv_image_set_src(weather_icon_, &weather_icon_cloud_dsc);



    weather_label_ = lv_label_create(weather_box_);

    lv_obj_set_style_text_font(weather_label_, &font_noto_sans_basic_16_4, 0);

    lv_obj_set_style_text_color(weather_label_, lv_color_white(), 0);

    lv_label_set_text(weather_label_, "");



    // --- STANDBY FULL SCREEN DASHBOARD (Matching User Reference) ---

    standby_box_ = lv_obj_create(screen);

    lv_obj_set_size(standby_box_, LV_HOR_RES, LV_VER_RES);

    lv_obj_set_style_bg_color(standby_box_, lv_color_hex(0x0f172a), 0);

    lv_obj_set_style_bg_opa(standby_box_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(standby_box_, 0, 0);

    lv_obj_set_style_border_width(standby_box_, 0, 0);

    lv_obj_set_style_pad_all(standby_box_, 0, 0);

    lv_obj_set_scrollbar_mode(standby_box_, LV_SCROLLBAR_MODE_OFF);

    lv_obj_clear_flag(standby_box_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);



    // Initialize custom fonts supporting Vietnamese glyphs

    font_noto_sans_custom_16_4 = font_noto_sans_basic_16_4;

    font_noto_sans_custom_16_4.get_glyph_dsc = CustomFontGetGlyphDsc16;

    font_noto_sans_custom_14_1 = font_noto_sans_basic_14_1;

    font_noto_sans_custom_14_1.get_glyph_dsc = CustomFontGetGlyphDsc14;

    font_noto_sans_custom_20_4 = font_noto_sans_basic_20_4;

    font_noto_sans_custom_20_4.get_glyph_dsc = CustomFontGetGlyphDsc20;



    Settings settings("display", false);

    standby_text_color_ = (uint32_t)settings.GetInt("text_color", 0xFFFFFF);

    lv_color_t text_col = lv_color_hex(standby_text_color_);

    // Standby Top-Right Status Bar (WiFi, Battery %, Battery Icon)
    standby_status_bar_ = lv_obj_create(standby_box_);
    lv_obj_set_size(standby_status_bar_, width_, 30);
    lv_obj_set_pos(standby_status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(standby_status_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(standby_status_bar_, 0, 0);
    lv_obj_set_style_pad_all(standby_status_bar_, 0, 0);
    lv_obj_clear_flag(standby_status_bar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(standby_status_bar_, LV_OBJ_FLAG_CLICKABLE);

    standby_right_icons_ = lv_obj_create(standby_status_bar_);
    lv_obj_set_size(standby_right_icons_, LV_SIZE_CONTENT, 30);
    lv_obj_align(standby_right_icons_, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_opa(standby_right_icons_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(standby_right_icons_, 0, 0);
    lv_obj_set_style_pad_all(standby_right_icons_, 0, 0);
    lv_obj_set_flex_flow(standby_right_icons_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(standby_right_icons_, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(standby_right_icons_, 6, 0);
    lv_obj_clear_flag(standby_right_icons_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(standby_right_icons_, LV_OBJ_FLAG_CLICKABLE);

    standby_wifi_label_ = lv_label_create(standby_right_icons_);
    lv_label_set_text(standby_wifi_label_, "");
    lv_obj_set_style_text_font(standby_wifi_label_, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(standby_wifi_label_, text_col, 0);
    lv_obj_clear_flag(standby_wifi_label_, LV_OBJ_FLAG_CLICKABLE);

    standby_battery_pct_label_ = lv_label_create(standby_right_icons_);
    lv_label_set_text(standby_battery_pct_label_, "");
    lv_obj_set_style_text_font(standby_battery_pct_label_, &font_noto_sans_basic_14_1, 0);
    lv_obj_set_style_text_color(standby_battery_pct_label_, text_col, 0);
    lv_obj_clear_flag(standby_battery_pct_label_, LV_OBJ_FLAG_CLICKABLE);

    standby_battery_label_ = lv_label_create(standby_right_icons_);
    lv_label_set_text(standby_battery_label_, "");
    lv_obj_set_style_text_font(standby_battery_label_, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(standby_battery_label_, text_col, 0);
    lv_obj_clear_flag(standby_battery_label_, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_move_foreground(standby_status_bar_);




    // Clock (HH:MM) - All white text

    standby_clock_ = lv_label_create(standby_box_);

#if LV_FONT_MONTSERRAT_40

    lv_obj_set_style_text_font(standby_clock_, &lv_font_montserrat_40, 0);

#else

    lv_obj_set_style_text_font(standby_clock_, &font_noto_sans_basic_30_4, 0);

#endif

    lv_obj_set_style_text_color(standby_clock_, text_col, 0);

    lv_label_set_text(standby_clock_, "00:00");



    // Seconds (:SS smaller font)

    standby_sec_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_sec_, &font_noto_sans_basic_20_4, 0);

    lv_obj_set_style_text_color(standby_sec_, text_col, 0);

    lv_label_set_text(standby_sec_, ":00");



    // Weekday (Thứ Tư, etc.)

    standby_weekday_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_weekday_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_weekday_, text_col, 0);

    lv_label_set_text(standby_weekday_, "");



    // Date (dd/mm/yyyy)

    standby_date_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_date_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_date_, text_col, 0);

    lv_label_set_text(standby_date_, "");



    // Lunar

    standby_lunar_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_lunar_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_lunar_, text_col, 0);

    lv_label_set_text(standby_lunar_, "");



    // Year (hidden)

    standby_year_ = lv_label_create(standby_box_);

    lv_obj_add_flag(standby_year_, LV_OBJ_FLAG_HIDDEN);



    // Weather Main Icon

    standby_weather_icon_ = lv_image_create(standby_box_);

    lv_image_set_src(standby_weather_icon_, &weather_icon_sun_cloud_48_dsc);



    // Main Temp

    standby_temp_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_temp_, &font_noto_sans_basic_30_4, 0);

    lv_obj_set_style_text_color(standby_temp_, text_col, 0);

    lv_label_set_text(standby_temp_, "--°C");



    // City

    standby_city_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_city_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_city_, text_col, 0);

    lv_label_set_text(standby_city_, "Đang cập nhật...");



    // Condition

    standby_condition_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_condition_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_condition_, text_col, 0);

    lv_label_set_text(standby_condition_, "Trời có mây");



    // Humidity

    standby_humidity_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_humidity_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_humidity_, text_col, 0);

    lv_label_set_text(standby_humidity_, "Độ ẩm: --%");



    // Wind

    standby_wind_ = lv_label_create(standby_box_);

    lv_obj_set_style_text_font(standby_wind_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(standby_wind_, text_col, 0);

    lv_label_set_text(standby_wind_, "Gió: -- km/h");



    // 4 Forecast Cards

    for (int i = 0; i < 4; ++i) {

        forecast_cards_[i].card = lv_obj_create(standby_box_);

        lv_obj_set_style_bg_color(forecast_cards_[i].card, lv_color_hex(0x1e293b), 0);

        lv_obj_set_style_bg_opa(forecast_cards_[i].card, LV_OPA_COVER, 0);

        lv_obj_set_style_radius(forecast_cards_[i].card, 10, 0);

        lv_obj_set_style_border_width(forecast_cards_[i].card, 0, 0);

        lv_obj_set_style_pad_all(forecast_cards_[i].card, 4, 0);

        lv_obj_clear_flag(forecast_cards_[i].card, LV_OBJ_FLAG_SCROLLABLE);



        forecast_cards_[i].day = lv_label_create(forecast_cards_[i].card);

        lv_obj_set_style_text_font(forecast_cards_[i].day, &font_noto_sans_custom_14_1, 0);

        lv_obj_set_style_text_color(forecast_cards_[i].day, text_col, 0);

        lv_label_set_text(forecast_cards_[i].day, "--");



        forecast_cards_[i].icon = lv_image_create(forecast_cards_[i].card);

        lv_image_set_src(forecast_cards_[i].icon, &weather_icon_cloud_24_dsc);



        forecast_cards_[i].temp = lv_label_create(forecast_cards_[i].card);

        lv_obj_set_style_text_font(forecast_cards_[i].temp, &font_noto_sans_basic_16_4, 0);

        lv_obj_set_style_text_color(forecast_cards_[i].temp, text_col, 0);

        lv_label_set_text(forecast_cards_[i].temp, "--°C");

    }



    LayoutStandbyWidgets(height_ > width_);
    UpdateStandbyStatusBar();

    CreateStandby2UI();

    CreateMenuUI();

    CreateQuickSettingsUI();

    CreateCalendarUI();

    CreateWeatherDetailUI();

    CreateClockUI();

    CreateStopwatchUI();

    CreateTimerUI();

    CreateExchangeRatesUI();

    CreateCalculatorUI();

    CreateFlashlightUI();

    CreateFmRadioUI();





#if CONFIG_USE_MULTILINE_CHAT_MESSAGE

    /* Bottom bar - auto height, grows upward with wrapped text */

    bottom_bar_ = lv_obj_create(screen);

    lv_obj_set_width(bottom_bar_, LV_HOR_RES);

    lv_obj_set_height(bottom_bar_, LV_SIZE_CONTENT);

    lv_obj_set_style_radius(bottom_bar_, 0, 0);

    lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);

    lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_50, 0);

    lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);

    lv_obj_set_style_pad_all(bottom_bar_, lvgl_theme->spacing(4), 0);

    lv_obj_set_style_border_width(bottom_bar_, 0, 0);

    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);

    lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);



    /* chat_message_label_ placed in bottom_bar_, multiline wrapped display */

    chat_message_label_ = lv_label_create(bottom_bar_);

    lv_label_set_text(chat_message_label_, "");

    lv_obj_set_width(chat_message_label_, LV_HOR_RES - lvgl_theme->spacing(8));

    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);

    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);

    lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);  // Hide until there is content

#else

    /* Top layer: Bottom bar - fixed height at bottom */

    bottom_bar_ = lv_obj_create(screen);

    lv_obj_set_size(bottom_bar_, LV_HOR_RES, text_font->line_height + lvgl_theme->spacing(8));

    lv_obj_set_style_radius(bottom_bar_, 0, 0);

    lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);

    lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);

    lv_obj_set_style_pad_all(bottom_bar_, 0, 0);

    lv_obj_set_style_pad_left(bottom_bar_, lvgl_theme->spacing(4), 0);

    lv_obj_set_style_pad_right(bottom_bar_, lvgl_theme->spacing(4), 0);

    lv_obj_set_style_border_width(bottom_bar_, 0, 0);

    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);

    lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);



    /* chat_message_label_ placed in bottom_bar_, single-line horizontal scroll */

    chat_message_label_ = lv_label_create(bottom_bar_);

    lv_label_set_text(chat_message_label_, "");

    lv_obj_set_width(chat_message_label_, LV_HOR_RES - lvgl_theme->spacing(8));

    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);

    lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);



    // Start scrolling after a delay (short text won't scroll)

    static lv_anim_t a;

    lv_anim_init(&a);

    lv_anim_set_delay(&a, 1000);

    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);

    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);

    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000),

                                   LV_PART_MAIN);

    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);  // Hide until there is content

#endif



    low_battery_popup_ = lv_obj_create(screen);

    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);

    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));

    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);

    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);



    low_battery_label_ = lv_label_create(low_battery_popup_);

    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);

    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);

    lv_obj_center(low_battery_label_);

    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);



    // Default to wallpaper active on startup before WiFi connects

    is_wallpaper_active_ = true;

    if (wallpaper_image_ != nullptr) {

        lv_obj_remove_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    }

    if (emoji_box_ != nullptr) {

        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (container_ != nullptr) {

        lv_obj_set_style_bg_color(container_, lv_color_black(), 0);

    }

    if (top_bar_ != nullptr) {

        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);

    }

    if (bottom_bar_ != nullptr) {

        lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_TRANSP, 0);

    }

    if (network_label_ != nullptr) lv_obj_set_style_text_color(network_label_, lv_color_white(), 0);

    if (mute_label_ != nullptr) lv_obj_set_style_text_color(mute_label_, lv_color_white(), 0);

    if (battery_label_ != nullptr) lv_obj_set_style_text_color(battery_label_, lv_color_white(), 0);

    if (status_label_ != nullptr) lv_obj_set_style_text_color(status_label_, lv_color_white(), 0);

    if (notification_label_ != nullptr) lv_obj_set_style_text_color(notification_label_, lv_color_white(), 0);

    if (chat_message_label_ != nullptr) lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);

    if (calendar_label_ != nullptr) lv_obj_set_style_text_color(calendar_label_, lv_color_white(), 0);

    if (idle_clock_label_ != nullptr) lv_obj_set_style_text_color(idle_clock_label_, lv_color_white(), 0);
    StartBootAnimation();

}



void LcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {

    DisplayLockGuard lock(this);

    if (preview_image_ == nullptr) {

        ESP_LOGE(TAG, "Preview image is not initialized");

        return;

    }



    if (image == nullptr) {

        esp_timer_stop(preview_timer_);

        if (!is_wallpaper_active_) {

            lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

        }

        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

        preview_image_cached_.reset();

        if (gif_controller_) {

            gif_controller_->Start();

        }

        return;

    }



    preview_image_cached_ = std::move(image);

    auto img_dsc = preview_image_cached_->image_dsc();

    lv_image_set_src(preview_image_, img_dsc);

    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {

        // zoom factor 0.5

        lv_image_set_scale(preview_image_, 128 * width_ / img_dsc->header.w);

    }



    // Hide emoji_box_

    if (gif_controller_) {

        gif_controller_->Stop();

    }

    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    esp_timer_stop(preview_timer_);

    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000));

}



void LcdDisplay::SetChatMessage(const char* role, const char* content) {

    if (!setup_ui_called_) {

        ESP_LOGW(TAG, "SetChatMessage('%s', '%s') called before SetupUI() - message will be lost!",

                 role, content);

    }

    DisplayLockGuard lock(this);

    std::string filtered_msg = FilterChinese(content);

    content = filtered_msg.c_str();

    if (chat_message_label_ == nullptr) {

        if (setup_ui_called_) {

            ESP_LOGW(TAG,

                     "SetChatMessage('%s', '%s') failed: chat_message_label_ is nullptr (SetupUI() "

                     "was called but label not created)",

                     role, content);

        }

        return;

    }

    lv_anim_delete(chat_message_label_, nullptr);

    lv_label_set_text(chat_message_label_, content);

    // Show bottom_bar_ only when there is content (and subtitle is not globally hidden)

    if (bottom_bar_ != nullptr) {

        if (content == nullptr || content[0] == '\0') {

            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);

        } else if (!hide_subtitle_) {

            lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);

            if (is_wallpaper_active_) {

                lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_TRANSP, 0);

                lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);

            }

        }

    }

#if CONFIG_USE_MULTILINE_CHAT_MESSAGE

    // Re-align bottom_bar_ after text change so it stays anchored to the bottom

    // as its height adapts to the wrapped content.

    if (bottom_bar_ != nullptr) {

        lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);

    }

#endif

}



void LcdDisplay::RemoveChatMessage(const char* role, const char* content) {

    DisplayLockGuard lock(this);

    if (chat_message_label_ == nullptr || content == nullptr) {

        return;

    }

    std::string filtered_msg = FilterChinese(content);

    if (strcmp(lv_label_get_text(chat_message_label_), content) != 0 &&

        strcmp(lv_label_get_text(chat_message_label_), filtered_msg.c_str()) != 0) {

        return;

    }

    // The classic layout has one text slot and does not distinguish message roles.

    lv_anim_delete(chat_message_label_, nullptr);

    lv_label_set_text(chat_message_label_, "");

    if (bottom_bar_ != nullptr) {

        lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::ClearChatMessages() {

    DisplayLockGuard lock(this);

    // In non-wechat mode, just clear the chat message label and hide the bar

    if (chat_message_label_ != nullptr) {

        lv_label_set_text(chat_message_label_, "");

    }

    if (bottom_bar_ != nullptr) {

        lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);

    }

}

#endif



bool LcdDisplay::SetEmotion(const char* emotion) {

    if (!setup_ui_called_) {

        ESP_LOGW(TAG, "SetEmotion('%s') called before SetupUI() - emotion will not be displayed!",

                 emotion);

    }

    if (emoji_image_ == nullptr) {

        if (setup_ui_called_) {

            ESP_LOGW(TAG,

                     "SetEmotion('%s') failed: emoji_image_ is nullptr (SetupUI() was called but "

                     "emoji image not created)",

                     emotion);

        }

        return false;

    }



    auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();

    auto image = emoji_collection != nullptr ? emoji_collection->GetEmojiImage(emotion) : nullptr;

    if (image == nullptr) {

        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);

        const char* utf8 = noto_emoji_get_utf8(emotion);

        const lv_font_t* emotion_font = lvgl_theme->emoji_font()->font();

        if (utf8 == nullptr) {

            utf8 = material_symbols_get_utf8(emotion);

            emotion_font = lvgl_theme->large_icon_font()->font();

        }

        if (utf8 == nullptr || emoji_label_ == nullptr) {

            // No image and no glyph for this name: nothing was shown for the request.

            return false;

        }

        DisplayLockGuard lock(this);

        if (gif_controller_) {

            gif_controller_->Stop();

            gif_controller_.reset();

        }

        lv_obj_set_style_text_font(emoji_label_, emotion_font, 0);

        lv_label_set_text(emoji_label_, utf8);

        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

        lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

        // The glyph is this emotion's own, from noto_emoji or material_symbols, not a

        // stand-in - so the request was honoured.

        return true;

    }



    DisplayLockGuard lock(this);

    // Stop any running GIF animation in the same lock scope as setting new image

    // to prevent LVGL from accessing freed image data between operations

    if (gif_controller_) {

        gif_controller_->Stop();

        gif_controller_.reset();

    }

    bool shown = false;

    if (image->IsGif()) {

        // Create new GIF controller

        gif_controller_ = std::make_unique<LvglGif>(image->image_dsc());



        if (gif_controller_->IsLoaded()) {

            // Set up frame update callback

            gif_controller_->SetFrameCallback(

                [this]() { lv_image_set_src(emoji_image_, gif_controller_->image_dsc()); });



            // Set initial frame and start animation

            lv_image_set_src(emoji_image_, gif_controller_->image_dsc());

            gif_controller_->Start();



            // Show GIF, hide others

            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

            lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

            shown = true;

        } else {

            // The collection had an image for this emotion but it would not decode, so

            // nothing is on screen for the request.

            ESP_LOGE(TAG, "Failed to load GIF for emotion: %s", emotion);

            gif_controller_.reset();

        }

    } else {

        lv_image_set_src(emoji_image_, image->image_dsc());

        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

        shown = true;

    }



#if CONFIG_USE_WECHAT_MESSAGE_STYLE

    // In WeChat message style, if emotion is neutral, don't display it

    uint32_t child_count = lv_obj_get_child_cnt(content_);

    if (strcmp(emotion, "neutral") == 0 && child_count > 0) {

        // Stop GIF animation if running

        if (gif_controller_) {

            gif_controller_->Stop();

            gif_controller_.reset();

        }



        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

    }

#endif

    // The WeChat style deliberately hides a neutral emotion. That is a choice about

    // presentation, not a failure to honour the request, so `shown` stands as it was.

    return shown;

}



void LcdDisplay::SetTheme(Theme* theme) {

    DisplayLockGuard lock(this);



    auto lvgl_theme = static_cast<LvglTheme*>(theme);



    // Get the active screen

    lv_obj_t* screen = lv_screen_active();



    // Set font

    auto text_font = lvgl_theme->text_font()->font();

    auto icon_font = lvgl_theme->icon_font()->font();

    auto large_icon_font = lvgl_theme->large_icon_font()->font();



    if (text_font->line_height >= 40) {

        lv_obj_set_style_text_font(mute_label_, large_icon_font, 0);

        lv_obj_set_style_text_font(battery_label_, large_icon_font, 0);

        lv_obj_set_style_text_font(network_label_, large_icon_font, 0);

    } else {

        lv_obj_set_style_text_font(mute_label_, icon_font, 0);

        lv_obj_set_style_text_font(battery_label_, icon_font, 0);

        lv_obj_set_style_text_font(network_label_, icon_font, 0);

    }



    // Set parent text color

    lv_obj_set_style_text_font(screen, text_font, 0);

    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);



    // Set background image

    if (lvgl_theme->background_image() != nullptr) {

        lv_obj_set_style_bg_image_src(container_, lvgl_theme->background_image()->image_dsc(), 0);

    } else {

        lv_obj_set_style_bg_image_src(container_, nullptr, 0);

        lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);

    }



    // Update top bar background color with 50% opacity

    if (top_bar_ != nullptr) {

        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_50, 0);

        lv_obj_set_style_bg_color(top_bar_, lvgl_theme->background_color(), 0);

    }



    // Update status bar elements

    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);

    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);

    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);

    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);

    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);

    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);



    // If we have the chat message style, update all message bubbles

#if CONFIG_USE_WECHAT_MESSAGE_STYLE

    // Set content background opacity

    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);



    // Iterate through all children of content (message containers or bubbles)

    uint32_t child_count = lv_obj_get_child_cnt(content_);

    for (uint32_t i = 0; i < child_count; i++) {

        lv_obj_t* obj = lv_obj_get_child(content_, i);

        if (obj == nullptr)

            continue;



        lv_obj_t* bubble = nullptr;



        // Check if this object is a container or bubble

        // If it's a container (user or system message), get its child as bubble

        // If it's a bubble (assistant message), use it directly

        if (lv_obj_get_child_cnt(obj) > 0) {

            // Might be a container, check if it's a user or system message container

            // User and system message containers are transparent

            lv_opa_t bg_opa = lv_obj_get_style_bg_opa(obj, LV_PART_MAIN);

            if (bg_opa == LV_OPA_TRANSP) {

                // This is a user or system message container

                bubble = lv_obj_get_child(obj, 0);

            } else {

                // This might be an assistant message bubble itself

                bubble = obj;

            }

        } else {

            // No child elements, might be other UI elements, skip

            continue;

        }



        if (bubble == nullptr)

            continue;



        // Use saved user data to identify bubble type

        void* bubble_type_ptr = lv_obj_get_user_data(bubble);

        if (bubble_type_ptr != nullptr) {

            const char* bubble_type = static_cast<const char*>(bubble_type_ptr);



            // Apply correct color based on bubble type

            if (strcmp(bubble_type, "user") == 0) {

                lv_obj_set_style_bg_color(bubble, lvgl_theme->user_bubble_color(), 0);

            } else if (strcmp(bubble_type, "assistant") == 0) {

                lv_obj_set_style_bg_color(bubble, lvgl_theme->assistant_bubble_color(), 0);

            } else if (strcmp(bubble_type, "system") == 0) {

                lv_obj_set_style_bg_color(bubble, lvgl_theme->system_bubble_color(), 0);

            } else if (strcmp(bubble_type, "image") == 0) {

                lv_obj_set_style_bg_color(bubble, lvgl_theme->system_bubble_color(), 0);

            }



            // Update border color

            lv_obj_set_style_border_color(bubble, lvgl_theme->border_color(), 0);



            // Update text color for the message

            if (lv_obj_get_child_cnt(bubble) > 0) {

                lv_obj_t* text = lv_obj_get_child(bubble, 0);

                if (text != nullptr) {

                    // Set text color based on bubble type

                    if (strcmp(bubble_type, "system") == 0) {

                        lv_obj_set_style_text_color(text, lvgl_theme->system_text_color(), 0);

                    } else {

                        lv_obj_set_style_text_color(text, lvgl_theme->text_color(), 0);

                    }

                }

            }

        } else {

            ESP_LOGW(TAG, "child[%lu] Bubble type is not found", i);

        }

    }

#else

    // Simple UI mode - just update the main chat message

    if (chat_message_label_ != nullptr) {

        lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);

    }



    if (emoji_label_ != nullptr) {

        lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);

    }



    // Update bottom bar background color with 50% opacity

    if (bottom_bar_ != nullptr) {

        lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_50, 0);

        lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);

    }

#endif



    // Update low battery popup

    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);



    // No errors occurred. Save theme to settings

    Display::SetTheme(lvgl_theme);

}



void LcdDisplay::SetHideSubtitle(bool hide) {

    DisplayLockGuard lock(this);

    hide_subtitle_ = hide;



    // Immediately update UI visibility based on the setting

    if (bottom_bar_ != nullptr) {

        if (hide) {

            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);

        } else {

            // Only show if there is actual content to display

            const char* text =

                (chat_message_label_ != nullptr) ? lv_label_get_text(chat_message_label_) : nullptr;

            if (text != nullptr && text[0] != '\0') {

                lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);

            }

        }

    }

}



void LcdDisplay::SetCustomWallpaper(const lv_image_dsc_t* img_dsc) {

    if (wallpaper_image_ == nullptr || img_dsc == nullptr) {

        return;

    }

    has_custom_sd_wallpaper_ = true;

    DisplayLockGuard lock(this);

    lv_image_set_src(wallpaper_image_, img_dsc);

    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {

        uint32_t zoom_w = (256 * width_) / img_dsc->header.w;

        uint32_t zoom_h = (256 * height_) / img_dsc->header.h;

        uint32_t zoom = (zoom_w > zoom_h) ? zoom_w : zoom_h;

        lv_image_set_scale(wallpaper_image_, zoom);

    }

    lv_image_set_inner_align(wallpaper_image_, LV_IMAGE_ALIGN_CENTER);

    lv_obj_align(wallpaper_image_, LV_ALIGN_CENTER, 0, 0);

}



void LcdDisplay::OnStateChanged(DeviceState state) {

    DisplayLockGuard lock(this);

    is_wallpaper_active_ = true;

    if (wallpaper_image_ == nullptr) {

        return;

    }



    if (screen_mode_ == kScreenMenu || screen_mode_ == kScreenQuickSettings || 

        screen_mode_ == kScreenCalendar || screen_mode_ == kScreenWeatherDetail ||

        (screen_mode_ == kScreenStandby && state == kDeviceStateIdle)) {

        lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    } else {

        lv_obj_remove_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    }

    if (emoji_box_ != nullptr) {

        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (container_ != nullptr) {

        lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);

    }

    if (top_bar_ != nullptr) {

        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_30, 0);

        lv_obj_set_style_bg_color(top_bar_, lv_color_black(), 0);

    }

    if (bottom_bar_ != nullptr) {

        lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_30, 0);

        lv_obj_set_style_bg_color(bottom_bar_, lv_color_black(), 0);

    }

    if (network_label_ != nullptr) lv_obj_set_style_text_color(network_label_, lv_color_white(), 0);

    if (mute_label_ != nullptr) lv_obj_set_style_text_color(mute_label_, lv_color_white(), 0);

    if (battery_label_ != nullptr) lv_obj_set_style_text_color(battery_label_, lv_color_white(), 0);

    if (status_label_ != nullptr) lv_obj_set_style_text_color(status_label_, lv_color_white(), 0);

    if (notification_label_ != nullptr) lv_obj_set_style_text_color(notification_label_, lv_color_white(), 0);

    if (chat_message_label_ != nullptr) lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);

    if (state == kDeviceStateIdle) {

        wifi_connected_ = true;

        if (!has_custom_sd_wallpaper_ && wallpaper_image_ != nullptr) {

            const lv_image_dsc_t* standby_dsc = (width_ > height_) ? &wallpaper_landscape_dsc : &wallpaper_portrait_dsc;

            lv_image_set_src(wallpaper_image_, standby_dsc);

        }

        if (calendar_label_ != nullptr) {

            lv_obj_set_style_text_color(calendar_label_, lv_color_white(), 0);

            lv_obj_remove_flag(calendar_label_, LV_OBJ_FLAG_HIDDEN);

        }

        if (idle_clock_label_ != nullptr) {

            lv_obj_set_style_text_color(idle_clock_label_, lv_color_white(), 0);

            lv_obj_remove_flag(idle_clock_label_, LV_OBJ_FLAG_HIDDEN);

        }

        if (status_label_ != nullptr) {

            lv_label_set_text(status_label_, "");

            lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

        }

            if (weather_box_ != nullptr && weather_ready_) {

                lv_obj_remove_flag(weather_box_, LV_OBJ_FLAG_HIDDEN);

            }

            if (screen_mode_ == kScreenMenu && menu_box_ != nullptr) {

                lv_obj_remove_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

            } else if (screen_mode_ == kScreenCalendar && calendar_box_ != nullptr) {

                lv_obj_remove_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);

            } else if (screen_mode_ == kScreenWeatherDetail && weather_detail_box_ != nullptr) {

                lv_obj_remove_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);

            } else {

                if (screen_mode_ == kScreenStandby2) {
                    if (standby2_box_ != nullptr) {
                        lv_obj_remove_flag(standby2_box_, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_move_foreground(standby2_box_);
                        UpdateStandbyStatusBar();
                    }
                } else {
                    if (standby_box_ != nullptr) {
                        lv_obj_remove_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);
                        UpdateStandbyStatusBar();
                    }
                }

            }

    } else if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {

        wifi_connected_ = false;

        if (!has_custom_sd_wallpaper_ && wallpaper_image_ != nullptr) {

            const lv_image_dsc_t* boot_dsc = (width_ > height_) ? &boot_landscape_dsc : &boot_portrait_dsc;

            lv_image_set_src(wallpaper_image_, boot_dsc);

        }

        if (calendar_label_ != nullptr) {

            lv_obj_add_flag(calendar_label_, LV_OBJ_FLAG_HIDDEN);

        }

        if (idle_clock_label_ != nullptr) {

            lv_obj_add_flag(idle_clock_label_, LV_OBJ_FLAG_HIDDEN);

        }

        if (status_label_ != nullptr) {

            lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

        }

            if (weather_box_ != nullptr) {

                lv_obj_add_flag(weather_box_, LV_OBJ_FLAG_HIDDEN);

            }

            if (standby_box_ != nullptr) {
                lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);
            }
            if (standby2_box_ != nullptr) {
                lv_obj_add_flag(standby2_box_, LV_OBJ_FLAG_HIDDEN);
            }

    } else {

        if (calendar_label_ != nullptr) {

            lv_obj_add_flag(calendar_label_, LV_OBJ_FLAG_HIDDEN);

        }

        if (idle_clock_label_ != nullptr) {

            lv_obj_add_flag(idle_clock_label_, LV_OBJ_FLAG_HIDDEN);

        }

        if (status_label_ != nullptr) {

            lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

        }

            if (weather_box_ != nullptr) {

                lv_obj_add_flag(weather_box_, LV_OBJ_FLAG_HIDDEN);

            }

            if (standby_box_ != nullptr) {
                lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);
            }
            if (standby2_box_ != nullptr) {
                lv_obj_add_flag(standby2_box_, LV_OBJ_FLAG_HIDDEN);
            }

            if (menu_box_ != nullptr) lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

            if (quick_settings_box_ != nullptr) lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

            if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (wallpaper_box_ != nullptr) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);

            if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::SetOrientation(bool is_portrait) {

    DisplayLockGuard lock(this);

    if (display_ == nullptr) {

        return;

    }



    lv_display_rotation_t target_rot = is_portrait ? LV_DISPLAY_ROTATION_90 : LV_DISPLAY_ROTATION_0;

    lv_display_set_rotation(display_, target_rot);

    width_ = lv_display_get_horizontal_resolution(display_);

    height_ = lv_display_get_vertical_resolution(display_);



    if (!setup_ui_called_) {

        return;

    }



    auto screen = lv_screen_active();

    if (screen != nullptr) {

        lv_obj_set_size(screen, width_, height_);

    }



    if (container_ != nullptr) {

        lv_obj_set_size(container_, width_, height_);

    }



    if (wallpaper_image_ != nullptr) {

        lv_obj_set_size(wallpaper_image_, width_, height_);

        if (!has_custom_sd_wallpaper_) {

            if (wifi_connected_) {

                lv_image_set_src(wallpaper_image_, (width_ > height_) ? &wallpaper_landscape_dsc : &wallpaper_portrait_dsc);

            } else {

                lv_image_set_src(wallpaper_image_, (width_ > height_) ? &boot_landscape_dsc : &boot_portrait_dsc);

            }

        }

        lv_obj_align(wallpaper_image_, LV_ALIGN_CENTER, 0, 0);

    }



    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);



    if (top_bar_ != nullptr) {

        lv_obj_set_size(top_bar_, width_, LV_SIZE_CONTENT);

        lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);

    }



    if (status_bar_ != nullptr) {

        lv_obj_set_size(status_bar_, width_, LV_SIZE_CONTENT);

        lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 26);

    }



    if (notification_label_ != nullptr) {

        lv_obj_set_width(notification_label_, width_ * 0.85);

        lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);

    }



    if (status_label_ != nullptr) {

        lv_obj_set_width(status_label_, width_ * 0.75);

        lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    }



    if (content_ != nullptr) {

        lv_obj_set_width(content_, width_);

    }



    if (bottom_bar_ != nullptr) {

        lv_obj_set_width(bottom_bar_, width_);

        lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);

    }



    if (chat_message_label_ != nullptr && lvgl_theme != nullptr) {

        lv_obj_set_width(chat_message_label_, width_ - lvgl_theme->spacing(8));

        lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);

    }



    if (emoji_box_ != nullptr) {

        lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);

    }



    if (idle_clock_label_ != nullptr) {

        lv_obj_align(idle_clock_label_, LV_ALIGN_CENTER, 0, 10);

    }

    if (weather_box_ != nullptr) {

        lv_obj_align(weather_box_, LV_ALIGN_CENTER, 0, (height_ > width_) ? 70 : 58);

    }

    LayoutStandbyWidgets(is_portrait);
    LayoutStandby2Widgets(is_portrait);
    LayoutWallpaperSettingsWidgets(is_portrait);
    UpdateStandbyStatusBar();

    LayoutMenuWidgets(is_portrait);

    LayoutQuickSettingsWidgets(is_portrait);

    LayoutCalendarWidgets(is_portrait);

    LayoutWeatherDetailWidgets(is_portrait);

    LayoutClockWidgets(is_portrait);

    LayoutStopwatchWidgets(is_portrait);

    LayoutTimerWidgets(is_portrait);

    LayoutExchangeRatesWidgets(is_portrait);

    LayoutCalculatorWidgets(is_portrait);

    LayoutFmRadioWidgets(is_portrait);

    LayoutMusicWidgets(is_portrait);



    if (flashlight_box_ != nullptr) {

        lv_obj_set_size(flashlight_box_, width_, height_);

        lv_obj_set_pos(flashlight_box_, 0, 0);

    }



    if (preview_image_ != nullptr) {

        lv_obj_set_size(preview_image_, width_ / 2, height_ / 2);

        lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);

    }



    if (low_battery_popup_ != nullptr && lvgl_theme != nullptr) {

        auto text_font = lvgl_theme->text_font()->font();

        lv_obj_set_size(low_battery_popup_, width_ * 0.9, text_font->line_height * 2);

        lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));

    }



    if (screen != nullptr) {

        lv_obj_invalidate(screen);

    }

}



void LcdDisplay::UpdateStatusBar(bool update_all) {

    LvglDisplay::UpdateStatusBar(update_all);



    auto& app = Application::GetInstance();

    DisplayLockGuard lock(this);



    if (app.GetDeviceState() == kDeviceStateIdle) {

        time_t now = time(NULL);

        struct tm* tm_now = localtime(&now);

        if (tm_now->tm_year >= 2025 - 1900) {

            // Keep status_label_ clear in idle mode so only the large clock shows

            if (status_label_ != nullptr) {

                lv_label_set_text(status_label_, "");

                lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

            }



            // Update large center clock every second (HH:MM:SS)

            if (idle_clock_label_ != nullptr) {

                int cur_sec = tm_now->tm_hour * 3600 + tm_now->tm_min * 60 + tm_now->tm_sec;

                if (update_all || cur_sec != last_displayed_clock_sec_) {

                    last_displayed_clock_sec_ = cur_sec;

                    char time_str[16];

                    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_now);

                    lv_label_set_text(idle_clock_label_, time_str);

                    if (standby_clock_ != nullptr) {

                        char hm[8];

                        strftime(hm, sizeof(hm), "%H:%M", tm_now);

                        lv_label_set_text(standby_clock_, hm);

                    }

                    if (standby_sec_ != nullptr) {

                        char sec[8];

                        strftime(sec, sizeof(sec), ":%S", tm_now);

                        lv_label_set_text(standby_sec_, sec);

                        bool is_p = (height_ > width_);

                        if (!is_p) {

                            lv_obj_update_layout(standby_clock_);

                            int cw = lv_obj_get_width(standby_clock_);

                            if (cw > 0) {

                                int clk_x = lv_obj_get_x(standby_clock_);

                                int clk_y = lv_obj_get_y(standby_clock_);

                                lv_obj_set_pos(standby_sec_, clk_x + cw + 3, clk_y + 14);

                            }

                        } else {

                            if (standby_clock_ != nullptr) {

                                lv_obj_update_layout(standby_clock_);

                                lv_obj_update_layout(standby_sec_);

                                int cw = lv_obj_get_width(standby_clock_);

                                int sw = lv_obj_get_width(standby_sec_);

                                int clk_x = lv_obj_get_x(standby_clock_);

                                int clk_y = lv_obj_get_y(standby_clock_);

                                int sec_x = (cw > 0 && sw > 0) ? (clk_x + cw - sw) : (clk_x + 72);

                                lv_obj_set_pos(standby_sec_, sec_x, clk_y + 42);

                            }

                        }

                    }

                }

                lv_obj_remove_flag(idle_clock_label_, LV_OBJ_FLAG_HIDDEN);

            }



            // Update top-left calendar daily

            if (calendar_label_ != nullptr) {

                int cur_day = tm_now->tm_year * 1000 + tm_now->tm_yday;

                if (update_all || cur_day != last_displayed_calendar_day_) {

                    last_displayed_calendar_day_ = cur_day;

                    int dd = tm_now->tm_mday;

                    int mm = tm_now->tm_mon + 1;

                    int yy = tm_now->tm_year + 1900;

                    LunarDate lunar = VietnameseLunarCalendar::SolarToLunar(dd, mm, yy, 7.0);



                    char buf[64];

                    if (lunar.is_leap) {

                        snprintf(buf, sizeof(buf), "%02d/%02d/%04d\nÂm lịch: %02d/%02d (N)", dd, mm, yy, lunar.day, lunar.month);

                    } else {

                        snprintf(buf, sizeof(buf), "%02d/%02d/%04d\nÂm lịch: %02d/%02d", dd, mm, yy, lunar.day, lunar.month);

                    }

                    lv_label_set_text(calendar_label_, buf);

                    if (standby_weekday_ != nullptr) {

                        const char* full_wdays[] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};

                        lv_label_set_text(standby_weekday_, full_wdays[tm_now->tm_wday]);

                    }

                    if (standby_date_ != nullptr) {

                        char dbuf[64];

                        snprintf(dbuf, sizeof(dbuf), "%02d/%02d/%04d", dd, mm, yy);

                        lv_label_set_text(standby_date_, dbuf);

                    }

                    if (standby_lunar_ != nullptr) {

                        char lbuf[64];

                        if (lunar.day <= 10) {

                            snprintf(lbuf, sizeof(lbuf), "Âm lịch: Mùng %d / %d", lunar.day, lunar.month);

                        } else {

                            snprintf(lbuf, sizeof(lbuf), "Âm lịch: %d / %d", lunar.day, lunar.month);

                        }

                        lv_label_set_text(standby_lunar_, lbuf);

                    }

                    if (standby_year_ != nullptr) {

                        lv_obj_add_flag(standby_year_, LV_OBJ_FLAG_HIDDEN);

                    }

                }

                lv_obj_remove_flag(calendar_label_, LV_OBJ_FLAG_HIDDEN);

            }

        } else {

            if (calendar_label_ != nullptr) lv_label_set_text(calendar_label_, "");

            if (idle_clock_label_ != nullptr) lv_label_set_text(idle_clock_label_, "");

        }

    } else {

        last_displayed_clock_sec_ = -1;

    }

    if (standby_box_ != nullptr && !lv_obj_has_flag(standby_box_, LV_OBJ_FLAG_HIDDEN)) {
        UpdateStandbyStatusBar();
    }
    if (screen_mode_ == kScreenStandby2 || (standby2_box_ != nullptr && !lv_obj_has_flag(standby2_box_, LV_OBJ_FLAG_HIDDEN))) {
        UpdateStandbyStatusBar();
        SyncStandby2Weather();
        time_t now2 = time(nullptr);
        struct tm* tm2 = localtime(&now2);
        if (tm2 != nullptr) {
            UpdateStandby2Clock(tm2, true);
        }
    }

    if (screen_mode_ == kScreenClock) {

        UpdateClockUI();

    } else if (screen_mode_ == kScreenStopwatch) {

        UpdateStopwatchUI();

    } else if (screen_mode_ == kScreenTimer) {

        UpdateTimerUI();

    } else if (screen_mode_ == kScreenMenu) {

        UpdateMenuStatusBar();

    }

}



void LcdDisplay::UpdateWeather(float temp, int humidity, const char* condition, int icon_type) {

    DisplayLockGuard lock(this);

    if (weather_label_ == nullptr || weather_icon_ == nullptr) {

        return;

    }

    const lv_image_dsc_t* dsc = &weather_icon_cloud_dsc;

    switch (icon_type) {

        case 0: dsc = &weather_icon_sun_dsc; break;

        case 1: dsc = &weather_icon_sun_cloud_dsc; break;

        case 2: dsc = &weather_icon_cloud_dsc; break;

        case 3: dsc = &weather_icon_rain_dsc; break;

        case 4: dsc = &weather_icon_thunder_dsc; break;

        default: dsc = &weather_icon_cloud_dsc; break;

    }

    lv_image_set_src(weather_icon_, dsc);



    char buf[64];

    snprintf(buf, sizeof(buf), "%.0f°C · %s · Độ ẩm: %d%%", temp, condition, humidity);

    lv_label_set_text(weather_label_, buf);

    if (standby2_weather_temp_ != nullptr) {
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%.0f°C", temp);
        lv_label_set_text(standby2_weather_temp_, tbuf);
    }
    if (standby2_weather_icon_ != nullptr) {
        lv_image_set_src(standby2_weather_icon_, dsc);
    }

    weather_ready_ = true;



    if (wifi_connected_ && weather_box_ != nullptr) {

        lv_obj_remove_flag(weather_box_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::LayoutStandbyWidgets(bool is_portrait) {
    if (standby_box_ == nullptr) return;

    int w = is_portrait ? 240 : 320;
    int h = is_portrait ? 320 : 240;
    lv_obj_set_size(standby_box_, w, h);
    lv_obj_align(standby_box_, LV_ALIGN_TOP_LEFT, 0, 0);

    if (standby_status_bar_) {
        lv_obj_set_size(standby_status_bar_, w, 26);
        lv_obj_set_pos(standby_status_bar_, 0, 0);
        if (standby_right_icons_) {
            lv_obj_align(standby_right_icons_, LV_ALIGN_RIGHT_MID, is_portrait ? -8 : -10, 0);
        }
        lv_obj_move_foreground(standby_status_bar_);
    }

    if (!is_portrait) {
        // Landscape (320x240) - UNCHANGED as user requested
        if (standby_clock_) lv_obj_set_pos(standby_clock_, 12, 8);
        if (standby_sec_) lv_obj_set_pos(standby_sec_, 128, 22);
        if (standby_weekday_) lv_obj_set_pos(standby_weekday_, 12, 52);
        if (standby_date_) lv_obj_set_pos(standby_date_, 12, 74);
        if (standby_lunar_) lv_obj_set_pos(standby_lunar_, 12, 96);
        if (standby_year_) lv_obj_add_flag(standby_year_, LV_OBJ_FLAG_HIDDEN);

        // Top-Right: Weather
        if (standby_weather_icon_) lv_obj_set_pos(standby_weather_icon_, 175, 24);
        if (standby_temp_) {
            lv_obj_set_size(standby_temp_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(standby_temp_, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_pos(standby_temp_, 228, 28);
        }

        if (standby_city_) lv_obj_set_pos(standby_city_, 175, 60);
        if (standby_condition_) lv_obj_set_pos(standby_condition_, 175, 78);
        if (standby_humidity_) lv_obj_set_pos(standby_humidity_, 175, 96);
        if (standby_wind_) lv_obj_set_pos(standby_wind_, 175, 114);

        // Bottom: 4 Forecast Cards
        int card_w = 68;
        int card_h = 86;
        int gap = 8;
        int start_x = 12;
        int start_y = 144;



        for (int i = 0; i < 4; ++i) {

            if (forecast_cards_[i].card) {

                lv_obj_set_pos(forecast_cards_[i].card, start_x + i * (card_w + gap), start_y);

                lv_obj_set_size(forecast_cards_[i].card, card_w, card_h);

                if (forecast_cards_[i].day) lv_obj_align(forecast_cards_[i].day, LV_ALIGN_TOP_MID, 0, 4);

                if (forecast_cards_[i].icon) lv_obj_align(forecast_cards_[i].icon, LV_ALIGN_CENTER, 0, -2);

                if (forecast_cards_[i].temp) lv_obj_align(forecast_cards_[i].temp, LV_ALIGN_BOTTOM_MID, 0, -4);

            }

        }

    } else {

        // Portrait (240x320)

        // Top-Left: Thứ & Ngày tháng xích lên và rải đều
        if (standby_weekday_) lv_obj_set_pos(standby_weekday_, 12, 36);

        if (standby_date_) lv_obj_set_pos(standby_date_, 12, 64);

        if (standby_lunar_) lv_obj_set_pos(standby_lunar_, 12, 92);

        if (standby_year_) lv_obj_add_flag(standby_year_, LV_OBJ_FLAG_HIDDEN);



        // Top-Right: Giờ bên phải, số giây đếm nằm dưới phút (căn phải thẳng hàng với phút)

        if (standby_clock_) lv_obj_set_pos(standby_clock_, 126, 36);

        if (standby_sec_) lv_obj_set_pos(standby_sec_, 198, 78); // Directly under minutes



        // Mid-Left: Nhiệt độ xuống DƯỚI đám mây và căn giữa lại

        if (standby_weather_icon_) lv_obj_set_pos(standby_weather_icon_, 36, 126);

        if (standby_temp_) {

            lv_obj_set_size(standby_temp_, 116, LV_SIZE_CONTENT);

            lv_obj_set_style_text_align(standby_temp_, LV_TEXT_ALIGN_CENTER, 0);

            lv_obj_set_pos(standby_temp_, 0, 174);

        }



        // Mid-Right: Nâng các thông tin chi tiết lên và rải đều (rộng hơn không bị thiếu chữ)
        if (standby_city_) {
            lv_obj_set_pos(standby_city_, 108, 122);
            lv_obj_set_size(standby_city_, 128, LV_SIZE_CONTENT);
        }
        if (standby_condition_) {
            lv_obj_set_pos(standby_condition_, 108, 146);
            lv_obj_set_size(standby_condition_, 128, LV_SIZE_CONTENT);
        }
        if (standby_humidity_) {
            lv_obj_set_pos(standby_humidity_, 108, 170);
            lv_obj_set_size(standby_humidity_, 128, LV_SIZE_CONTENT);
        }
        if (standby_wind_) {
            lv_obj_set_pos(standby_wind_, 108, 194);
            lv_obj_set_size(standby_wind_, 128, LV_SIZE_CONTENT);
        }

        // Bottom: 4 Forecast Cards rải đều toàn màn hình dọc 240px
        int card_w = 52;
        int card_h = 82;
        int gap = 6;
        int start_x = 6;
        int start_y = 226;



        for (int i = 0; i < 4; ++i) {

            if (forecast_cards_[i].card) {

                lv_obj_set_pos(forecast_cards_[i].card, start_x + i * (card_w + gap), start_y);

                lv_obj_set_size(forecast_cards_[i].card, card_w, card_h);

                if (forecast_cards_[i].day) lv_obj_align(forecast_cards_[i].day, LV_ALIGN_TOP_MID, 0, 4);

                if (forecast_cards_[i].icon) lv_obj_align(forecast_cards_[i].icon, LV_ALIGN_CENTER, 0, -2);

                if (forecast_cards_[i].temp) lv_obj_align(forecast_cards_[i].temp, LV_ALIGN_BOTTOM_MID, 0, -4);

            }

        }

    }

}



void LcdDisplay::UpdateWeatherFull(const WeatherInfo& info) {

    DisplayLockGuard lock(this);

    if (standby_box_ == nullptr) return;



    char temp_str[16];

    snprintf(temp_str, sizeof(temp_str), "%.0f°C", info.temp);

    if (standby_temp_ != nullptr) lv_label_set_text(standby_temp_, temp_str);



    char hum_str[32];

    snprintf(hum_str, sizeof(hum_str), "Độ ẩm: %d%%", info.humidity);

    if (standby_humidity_ != nullptr) lv_label_set_text(standby_humidity_, hum_str);



    char wind_str[32];

    snprintf(wind_str, sizeof(wind_str), "Gió: %.0f km/h", info.wind_speed);

    if (standby_wind_ != nullptr) lv_label_set_text(standby_wind_, wind_str);



    if (standby_city_ != nullptr) lv_label_set_text(standby_city_, info.city.c_str());

    if (standby_condition_ != nullptr) lv_label_set_text(standby_condition_, info.condition.c_str());



    if (standby_weather_icon_ != nullptr) {

        const lv_image_dsc_t* dsc48 = &weather_icon_sun_cloud_48_dsc;

        switch (info.icon_type) {

            case 0: dsc48 = &weather_icon_sun_48_dsc; break;

            case 1: dsc48 = &weather_icon_sun_cloud_48_dsc; break;

            case 2: dsc48 = &weather_icon_cloud_48_dsc; break;

            case 3: dsc48 = &weather_icon_rain_48_dsc; break;

            case 4: dsc48 = &weather_icon_thunder_48_dsc; break;

            case 5: dsc48 = &weather_icon_moon_48_dsc; break;

            case 6: dsc48 = &weather_icon_moon_cloud_48_dsc; break;

        }

        lv_image_set_src(standby_weather_icon_, dsc48);

    }



    for (size_t i = 0; i < 4 && i < info.daily.size(); ++i) {

        if (forecast_cards_[i].day != nullptr) {

            lv_label_set_text(forecast_cards_[i].day, info.daily[i].day_str.c_str());

        }

        if (forecast_cards_[i].icon != nullptr) {

            const lv_image_dsc_t* dsc24 = &weather_icon_cloud_24_dsc;

            switch (info.daily[i].icon_type) {

                case 0: dsc24 = &weather_icon_sun_24_dsc; break;

                case 1: dsc24 = &weather_icon_sun_cloud_24_dsc; break;

                case 2: dsc24 = &weather_icon_cloud_24_dsc; break;

                case 3: dsc24 = &weather_icon_rain_24_dsc; break;

                case 4: dsc24 = &weather_icon_thunder_24_dsc; break;

                case 5: dsc24 = &weather_icon_moon_24_dsc; break;

                case 6: dsc24 = &weather_icon_moon_cloud_24_dsc; break;

            }

            lv_image_set_src(forecast_cards_[i].icon, dsc24);

        }

        if (forecast_cards_[i].temp != nullptr) {

            char tbuf[16];

            snprintf(tbuf, sizeof(tbuf), "%.0f°C", info.daily[i].temp_max);

            lv_label_set_text(forecast_cards_[i].temp, tbuf);

        }

    }



    weather_ready_ = true;

    if (wifi_connected_ && standby_box_ != nullptr) {

        if (screen_mode_ == kScreenStandby) {

            lv_obj_remove_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);
            UpdateStandbyStatusBar();

        }

    }

    if (screen_mode_ == kScreenWeatherDetail) {

        UpdateWeatherDetailUI();

    }

}





void LcdDisplay::OnSwipeLeft() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    last_swipe_time_ = esp_timer_get_time() / 1000;

    lv_indev_t* indev = lv_indev_get_next(NULL);

    while (indev) {

        lv_indev_reset(indev, NULL);

        indev = lv_indev_get_next(indev);

    }

    if (screen_mode_ == kScreenStandby) {
        ShowStandby2();
        return;
    }

    if (screen_mode_ == kScreenStandby2) {
        return;
    }

    if (screen_mode_ == kScreenMenu) {

        if (menu_page_ < 1) {

            SetMenuPage(menu_page_ + 1, true, true); // Swipe left: trượt sang trang 2 (0 -> 1)

        }

    } else if (screen_mode_ == kScreenCalendar) {

        if (curr_cal_month_ == 12) {

            curr_cal_month_ = 1;

            curr_cal_year_++;

        } else {

            curr_cal_month_++;

        }

        UpdateCalendarUI();

    }

}



void LcdDisplay::OnSwipeRight() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    last_swipe_time_ = esp_timer_get_time() / 1000;

    lv_indev_t* indev = lv_indev_get_next(NULL);

    while (indev) {

        lv_indev_reset(indev, NULL);

        indev = lv_indev_get_next(indev);

    }

    if (screen_mode_ == kScreenStandby2) {
        ShowStandby();
        return;
    }

    if (screen_mode_ == kScreenMenu) {

        if (menu_page_ > 0) {

            SetMenuPage(menu_page_ - 1, true, false); // Swipe right: trượt về trang 1 (1 -> 0)

        }

    } else if (screen_mode_ == kScreenCalendar) {

        if (curr_cal_month_ == 1) {

            curr_cal_month_ = 12;

            curr_cal_year_--;

        } else {

            curr_cal_month_--;

        }

        UpdateCalendarUI();

    } else if (screen_mode_ == kScreenWeatherDetail || screen_mode_ == kScreenClock ||

               screen_mode_ == kScreenExchangeRates) {

        ShowMenu();

    } else if (screen_mode_ == kScreenQuickSettings) {

        // Không thoát bằng vuốt - chỉ thoát qua nút 'X'

    } else if (screen_mode_ == kScreenStopwatch) {

        ShowClock();

    } else if (screen_mode_ == kScreenWallpaperSettings) {

        WallpaperManager::GetInstance().SetActiveWallpaperIndex(wp_preview_idx_);
        WallpaperManager::GetInstance().SetActiveLayoutIndex(wp_preview_layout_);
        if (wallpaper_box_) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);
        if (wp_preview_buf_ != nullptr) {
            heap_caps_free(wp_preview_buf_);
            wp_preview_buf_ = nullptr;
        }
        ReloadWallpaper();
        ShowStandby2();

    }

}



void LcdDisplay::OnSwipeUp() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    if (qs_qr_modal_ != nullptr && !lv_obj_has_flag(qs_qr_modal_, LV_OBJ_FLAG_HIDDEN)) {
        HideQrModal();
        return;
    }

    last_swipe_time_ = esp_timer_get_time() / 1000;

    lv_indev_t* indev = lv_indev_get_next(NULL);

    while (indev) {

        lv_indev_reset(indev, NULL);

        indev = lv_indev_get_next(indev);

    }



    auto& app = Application::GetInstance();

    auto state = app.GetDeviceState();



    // Nếu đang ở màn hình chatbot (Listening, Speaking, Connecting):

    // Vuốt lên sẽ thoát ngay chế độ chatbot và trở về màn hình chờ ban đầu!

    if (state == kDeviceStateListening || state == kDeviceStateSpeaking || state == kDeviceStateConnecting) {

        app.ToggleChatState();

        if (content_ != nullptr) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);

        if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

        if (wallpaper_image_ != nullptr) lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

        if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

        if (standby_box_ != nullptr) {
            lv_obj_remove_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);
            UpdateStandbyStatusBar();
        }

        screen_mode_ = kScreenStandby;

        return;

    }



    if (screen_mode_ == kScreenStandby || screen_mode_ == kScreenStandby2) {

        ShowMenu(); // Swipe up from standby opens icon menu

    } else if (screen_mode_ == kScreenMenu) {

        ShowStandby(); // Swipe up from icon menu returns to standby

    } else if (screen_mode_ == kScreenQuickSettings) {

        // Không thoát khi vuốt lên trong Quick Settings - chỉ thoát khi bấm nút 'X'!

    } else if (screen_mode_ == kScreenCalendar || screen_mode_ == kScreenWeatherDetail ||

               screen_mode_ == kScreenClock || screen_mode_ == kScreenExchangeRates ||

               screen_mode_ == kScreenCalculator) {

        ShowMenu();

    } else if (screen_mode_ == kScreenFlashlight) {

        if (Board::GetInstance().GetBacklight()) {

            Board::GetInstance().GetBacklight()->SetBrightness(saved_flashlight_brightness_, false);

        }

        lv_obj_set_style_bg_color(lv_screen_active(), is_light_theme_ ? lv_color_hex(0xF8FAFC) : lv_color_black(), 0);

        ShowMenu();

    } else if (screen_mode_ == kScreenFmRadio) {

        auto music = Board::GetInstance().GetMusic();

        if (music && fm_is_playing_) {

            fm_is_playing_ = false;

            std::thread([music]() {

                music->StopStreaming();

            }).detach();

        }

        ShowMenu();

    } else if (screen_mode_ == kScreenStopwatch || screen_mode_ == kScreenTimer) {

        ShowClock(); // Thoát từ trang con về trang Đồng hồ chính (Báo thức vuốt từ mép dưới lên như iPhone)

    } else if (screen_mode_ == kScreenMusic) {
        ShowMenu();
    } else if (screen_mode_ == kScreenWallpaperSettings) {
        WallpaperManager::GetInstance().SetActiveWallpaperIndex(wp_preview_idx_);
        WallpaperManager::GetInstance().SetActiveLayoutIndex(wp_preview_layout_);
        if (wallpaper_box_) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);
        if (wp_preview_buf_ != nullptr) {
            heap_caps_free(wp_preview_buf_);
            wp_preview_buf_ = nullptr;
        }
        ReloadWallpaper();
        ShowStandby2();
        return;
    }

}



void LcdDisplay::OnSwipeDown() {
    DisplayLockGuard lock(this);
    if (!lock) return;
    last_swipe_time_ = esp_timer_get_time() / 1000;
    lv_indev_t* indev = lv_indev_get_next(NULL);
    while (indev) {
        lv_indev_reset(indev, NULL);
        indev = lv_indev_get_next(indev);
    }

    if (screen_mode_ == kScreenQuickSettings) {
        // KHONG thoat khi vuot xuong trong QuickSettings de cuon xem thong tin
        return;
    }

    // O bat ki trang nao khi vuot xuong se la cai dat nhanh
    ShowQuickSettings();
}



void LcdDisplay::ShowStandby() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    screen_mode_ = kScreenStandby;

    previous_screen_mode_ = kScreenStandby;

    if (standby2_box_ != nullptr) lv_obj_add_flag(standby2_box_, LV_OBJ_FLAG_HIDDEN);

    if (menu_box_ != nullptr) {

        lv_anim_delete(menu_box_, nullptr);

        lv_obj_set_y(menu_box_, 0);

        lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (quick_settings_box_ != nullptr) {

        lv_anim_delete(quick_settings_box_, nullptr);

        lv_obj_set_y(quick_settings_box_, 0);

        lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (wallpaper_box_ != nullptr) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);

    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);

    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);

    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);

    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);

    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    if (calc_box_ != nullptr) lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);

    if (flashlight_box_ != nullptr) lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);

    if (fm_box_ != nullptr) lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);
    if (music_box_ != nullptr) lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);



    auto& app = Application::GetInstance();

    if (app.GetDeviceState() == kDeviceStateIdle) {

        if (content_ != nullptr) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);

        if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

        if (wallpaper_image_ != nullptr) lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

        if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

        if (standby_box_ != nullptr) {
            lv_obj_remove_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);
            UpdateStandbyStatusBar();
        }

    } else {

        if (container_ != nullptr) lv_obj_remove_flag(container_, LV_OBJ_FLAG_HIDDEN);

        if (content_ != nullptr) lv_obj_remove_flag(content_, LV_OBJ_FLAG_HIDDEN);

        if (wallpaper_image_ != nullptr) lv_obj_remove_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::ShowMenu() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    screen_mode_ = kScreenMenu;



    // Đảm bảo ẩn hoàn toàn chatbot container, content, wallpaper và emoji

    if (content_ != nullptr) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);

    if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (wallpaper_image_ != nullptr) lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);



    if (quick_settings_box_ != nullptr) {

        lv_anim_delete(quick_settings_box_, nullptr);

        lv_obj_set_y(quick_settings_box_, 0);

        lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (standby2_box_ != nullptr) lv_obj_add_flag(standby2_box_, LV_OBJ_FLAG_HIDDEN);
    if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (wallpaper_box_ != nullptr) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);

    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);

    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);

    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);

    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);

    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    if (calc_box_ != nullptr) lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);

    if (flashlight_box_ != nullptr) lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);

    if (fm_box_ != nullptr) lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);
    if (music_box_ != nullptr) lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);



    // Nền màn hình chuẩn đen tuyền, không thấy chatbot

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);



    SetMenuPage(menu_page_); // Bắt đầu ở trang 1

    UpdateMenuStatusBar();

    if (menu_box_ != nullptr) {

        lv_obj_move_foreground(menu_box_);

        lv_anim_delete(menu_box_, nullptr);

        lv_obj_remove_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_y(menu_box_, height_);



        lv_anim_t a;

        lv_anim_init(&a);

        lv_anim_set_var(&a, menu_box_);

        lv_anim_set_values(&a, height_, 0);

        lv_anim_set_duration(&a, 220);

        lv_anim_set_exec_cb(&a, [](void* var, int32_t val) {

            lv_obj_set_y((lv_obj_t*)var, val);

        });

        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

        lv_anim_start(&a);

    }



    if (standby_box_ != nullptr) lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);

}



void LcdDisplay::ExitQuickSettings() {
    qs_at_bottom_.store(false);
    if (quick_settings_box_ != nullptr) {
        lv_anim_delete(quick_settings_box_, nullptr);
        lv_obj_set_y(quick_settings_box_, 0);
        lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);
    }

    switch (previous_screen_mode_) {
        case kScreenStandby:
            ShowStandby();
            break;
        case kScreenStandby2:
            ShowStandby2();
            break;
        case kScreenCalendar:
            ShowCalendar();
            break;
        case kScreenWeatherDetail:
            ShowWeatherDetail();
            break;
        case kScreenClock:
            ShowClock();
            break;
        case kScreenStopwatch:
            ShowStopwatch();
            break;
        case kScreenTimer:
            ShowTimer();
            break;
        case kScreenExchangeRates:
            ShowExchangeRates();
            break;
        case kScreenCalculator:
            ShowCalculator();
            break;
        case kScreenFlashlight:
            ShowFlashlight();
            break;
        case kScreenFmRadio:
            ShowFmRadio();
            break;
        case kScreenWallpaperSettings:
            ShowWallpaperSettings();
            break;
        case kScreenMusic:
            ShowMusicScreen();
            break;
        case kScreenMenu:
        default:
            ShowMenu();
            break;
    }
}



void LcdDisplay::ShowQuickSettings() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    if (screen_mode_ != kScreenQuickSettings) {

        previous_screen_mode_ = screen_mode_;

    }

    screen_mode_ = kScreenQuickSettings;

    if (standby2_box_ != nullptr) lv_obj_add_flag(standby2_box_, LV_OBJ_FLAG_HIDDEN);
    if (standby_box_ != nullptr) lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);

    if (wallpaper_image_ != nullptr) lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

    if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (content_ != nullptr) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);

    if (menu_box_ != nullptr) {

        lv_anim_delete(menu_box_, nullptr);

        lv_obj_set_y(menu_box_, 0);

        lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (wallpaper_box_ != nullptr) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);

    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);

    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);

    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);

    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);

    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    if (calc_box_ != nullptr) lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);

    if (flashlight_box_ != nullptr) lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);

    if (fm_box_ != nullptr) lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);
    if (music_box_ != nullptr) lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);



    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);



    if (qs_brightness_slider_ != nullptr && Board::GetInstance().GetBacklight() != nullptr) {

        int b = Board::GetInstance().GetBacklight()->brightness();

        lv_slider_set_value(qs_brightness_slider_, b, LV_ANIM_OFF);

        if (qs_brightness_label_ != nullptr) {

            char buf[16];

            snprintf(buf, sizeof(buf), "%d%%", b);

            lv_label_set_text(qs_brightness_label_, buf);

        }

    }

    if (qs_volume_slider_ != nullptr && Board::GetInstance().GetAudioCodec() != nullptr) {

        int v = Board::GetInstance().GetAudioCodec()->output_volume();

        lv_slider_set_value(qs_volume_slider_, v, LV_ANIM_OFF);

        if (qs_volume_label_ != nullptr) {

            char buf[16];

            snprintf(buf, sizeof(buf), "%d%%", v);

            lv_label_set_text(qs_volume_label_, buf);

        }

    }



    UpdateColorDotsSelection();

    UpdateQuickSettingsInfo();



    if (quick_settings_box_ != nullptr) {

        if (qs_content_box_ != nullptr) {
            lv_obj_scroll_to_y(qs_content_box_, 0, LV_ANIM_OFF);
        }

        qs_at_bottom_.store(false);

        lv_anim_delete(quick_settings_box_, nullptr);

        lv_obj_remove_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_y(quick_settings_box_, -height_);



        lv_anim_t a;

        lv_anim_init(&a);

        lv_anim_set_var(&a, quick_settings_box_);

        lv_anim_set_values(&a, -height_, 0);

        lv_anim_set_duration(&a, 220);

        lv_anim_set_exec_cb(&a, [](void* var, int32_t val) {

            lv_obj_set_y((lv_obj_t*)var, val);

        });

        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

        lv_anim_start(&a);

    }

}



void LcdDisplay::ShowCalendar() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    screen_mode_ = kScreenCalendar;

    if (standby_box_ != nullptr) lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);

    if (wallpaper_image_ != nullptr) lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

    if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (content_ != nullptr) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);

    if (menu_box_ != nullptr) {

        lv_anim_delete(menu_box_, nullptr);

        lv_obj_set_y(menu_box_, 0);

        lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (quick_settings_box_ != nullptr) {

        lv_anim_delete(quick_settings_box_, nullptr);

        lv_obj_set_y(quick_settings_box_, 0);

        lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);

    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);

    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);

    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);

    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    if (calc_box_ != nullptr) lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);

    if (flashlight_box_ != nullptr) lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);

    if (fm_box_ != nullptr) lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);
    if (music_box_ != nullptr) lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);



    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);



    time_t now = time(nullptr);

    struct tm* tm_info = localtime(&now);

    if (tm_info != nullptr && tm_info->tm_year > 100) {

        curr_cal_year_ = tm_info->tm_year + 1900;

        curr_cal_month_ = tm_info->tm_mon + 1;

    }

    UpdateCalendarUI();



    if (calendar_box_ != nullptr) {

        lv_obj_remove_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::ShowWeatherDetail() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    screen_mode_ = kScreenWeatherDetail;

    if (standby_box_ != nullptr) lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);

    if (wallpaper_image_ != nullptr) lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

    if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (content_ != nullptr) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);

    if (menu_box_ != nullptr) {

        lv_anim_delete(menu_box_, nullptr);

        lv_obj_set_y(menu_box_, 0);

        lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (quick_settings_box_ != nullptr) {

        lv_anim_delete(quick_settings_box_, nullptr);

        lv_obj_set_y(quick_settings_box_, 0);

        lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (wallpaper_box_ != nullptr) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);

    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);

    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);

    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);

    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    if (calc_box_ != nullptr) lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);

    if (flashlight_box_ != nullptr) lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);

    if (fm_box_ != nullptr) lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);
    if (music_box_ != nullptr) lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);



    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);



    UpdateWeatherDetailUI();



    if (weather_detail_box_ != nullptr) {

        lv_obj_remove_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::ShowClock() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    screen_mode_ = kScreenClock;

    if (standby_box_ != nullptr) lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);

    if (wallpaper_image_ != nullptr) lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

    if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (content_ != nullptr) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);

    if (menu_box_ != nullptr) {

        lv_anim_delete(menu_box_, nullptr);

        lv_obj_set_y(menu_box_, 0);

        lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (quick_settings_box_ != nullptr) {

        lv_anim_delete(quick_settings_box_, nullptr);

        lv_obj_set_y(quick_settings_box_, 0);

        lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (wallpaper_box_ != nullptr) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);

    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);

    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);

    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);

    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    if (calc_box_ != nullptr) lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);

    if (flashlight_box_ != nullptr) lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);

    if (fm_box_ != nullptr) lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);
    if (music_box_ != nullptr) lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);



    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);



    UpdateClockUI();

    if (clock_box_ != nullptr) {

        lv_obj_remove_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::ShowStopwatch() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    screen_mode_ = kScreenStopwatch;

    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);

    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);

    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    if (calc_box_ != nullptr) lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);

    if (flashlight_box_ != nullptr) lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);

    if (fm_box_ != nullptr) lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);
    if (music_box_ != nullptr) lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);

    if (menu_box_ != nullptr) lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);



    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);



    UpdateStopwatchUI();

    if (stopwatch_box_ != nullptr) {

        lv_obj_remove_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::ShowTimer() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    screen_mode_ = kScreenTimer;

    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);

    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);

    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    if (calc_box_ != nullptr) lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);

    if (flashlight_box_ != nullptr) lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);

    if (fm_box_ != nullptr) lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);
    if (music_box_ != nullptr) lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);

    if (menu_box_ != nullptr) lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);



    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);



    UpdateTimerUI();

    if (timer_box_ != nullptr) {

        lv_obj_remove_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::ShowExchangeRates() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    screen_mode_ = kScreenExchangeRates;

    if (standby_box_ != nullptr) lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);

    if (wallpaper_image_ != nullptr) lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

    if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (content_ != nullptr) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);

    if (menu_box_ != nullptr) {

        lv_anim_delete(menu_box_, nullptr);

        lv_obj_set_y(menu_box_, 0);

        lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (quick_settings_box_ != nullptr) {

        lv_anim_delete(quick_settings_box_, nullptr);

        lv_obj_set_y(quick_settings_box_, 0);

        lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (wallpaper_box_ != nullptr) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);

    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);

    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);

    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);

    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);



    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);



    UpdateExchangeRatesUI();

    if (rates_box_ != nullptr) {

        lv_obj_remove_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::ShowCalculator() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    screen_mode_ = kScreenCalculator;

    if (standby_box_ != nullptr) lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);

    if (wallpaper_image_ != nullptr) lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

    if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (content_ != nullptr) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);

    if (menu_box_ != nullptr) {

        lv_anim_delete(menu_box_, nullptr);

        lv_obj_set_y(menu_box_, 0);

        lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (quick_settings_box_ != nullptr) {

        lv_anim_delete(quick_settings_box_, nullptr);

        lv_obj_set_y(quick_settings_box_, 0);

        lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (wallpaper_box_ != nullptr) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);

    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);

    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);

    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);

    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);

    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    if (flashlight_box_ != nullptr) lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);

    if (fm_box_ != nullptr) lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);
    if (music_box_ != nullptr) lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);



    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);



    if (calc_box_ != nullptr) {

        lv_obj_remove_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::ShowFlashlight() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    screen_mode_ = kScreenFlashlight;

    if (standby_box_ != nullptr) lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);

    if (wallpaper_image_ != nullptr) lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

    if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (content_ != nullptr) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);

    if (menu_box_ != nullptr) {

        lv_anim_delete(menu_box_, nullptr);

        lv_obj_set_y(menu_box_, 0);

        lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (quick_settings_box_ != nullptr) {

        lv_anim_delete(quick_settings_box_, nullptr);

        lv_obj_set_y(quick_settings_box_, 0);

        lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (wallpaper_box_ != nullptr) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);

    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);

    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);

    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);

    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);

    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    if (calc_box_ != nullptr) lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);

    if (fm_box_ != nullptr) lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);
    if (music_box_ != nullptr) lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);



    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), 0);

    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);



    if (Board::GetInstance().GetBacklight()) {

        saved_flashlight_brightness_ = Board::GetInstance().GetBacklight()->brightness();

        Board::GetInstance().GetBacklight()->SetBrightness(100, false);

    }



    if (flashlight_box_ != nullptr) {

        lv_obj_set_size(flashlight_box_, width_, height_);

        lv_obj_set_pos(flashlight_box_, 0, 0);

        lv_obj_remove_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::ShowFmRadio() {

    DisplayLockGuard lock(this);

    if (!lock) return;

    screen_mode_ = kScreenFmRadio;

    if (standby_box_ != nullptr) lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);

    if (wallpaper_image_ != nullptr) lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);

    if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);

    if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (content_ != nullptr) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);

    if (menu_box_ != nullptr) {

        lv_anim_delete(menu_box_, nullptr);

        lv_obj_set_y(menu_box_, 0);

        lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (quick_settings_box_ != nullptr) {

        lv_anim_delete(quick_settings_box_, nullptr);

        lv_obj_set_y(quick_settings_box_, 0);

        lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

    }

    if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (wallpaper_box_ != nullptr) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);

    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);

    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);

    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);

    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);

    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    if (calc_box_ != nullptr) lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);

    if (flashlight_box_ != nullptr) lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);



    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);



    UpdateFmRadioUI();

    if (fm_box_ != nullptr) {

        lv_obj_remove_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);

    }

}



void LcdDisplay::SetStandbyTextColor(uint32_t color_hex) {

    DisplayLockGuard lock(this);

    if (!lock) return;

    standby_text_color_ = color_hex;

    Settings settings("display", true);

    settings.SetInt("text_color", (int)color_hex);

    UpdateColorDotsSelection();



    lv_color_t col = lv_color_hex(color_hex);

    if (standby_clock_) lv_obj_set_style_text_color(standby_clock_, col, 0);

    if (standby_sec_) lv_obj_set_style_text_color(standby_sec_, col, 0);

    if (standby_weekday_) lv_obj_set_style_text_color(standby_weekday_, col, 0);

    if (standby_date_) lv_obj_set_style_text_color(standby_date_, col, 0);

    if (standby_lunar_) lv_obj_set_style_text_color(standby_lunar_, col, 0);

    if (standby_temp_) lv_obj_set_style_text_color(standby_temp_, col, 0);

    if (standby_city_) lv_obj_set_style_text_color(standby_city_, col, 0);

    if (standby_condition_) lv_obj_set_style_text_color(standby_condition_, col, 0);

    if (standby_humidity_) lv_obj_set_style_text_color(standby_humidity_, col, 0);

    if (standby_wind_) lv_obj_set_style_text_color(standby_wind_, col, 0);
    if (standby_wifi_label_) lv_obj_set_style_text_color(standby_wifi_label_, col, 0);
    if (standby_battery_pct_label_) lv_obj_set_style_text_color(standby_battery_pct_label_, col, 0);
    if (standby_battery_label_) lv_obj_set_style_text_color(standby_battery_label_, col, 0);

    for (int i = 0; i < 4; ++i) {

        if (forecast_cards_[i].day) lv_obj_set_style_text_color(forecast_cards_[i].day, col, 0);

        if (forecast_cards_[i].temp) lv_obj_set_style_text_color(forecast_cards_[i].temp, col, 0);

    }



    // Đồng bộ màu chữ sang Màn hình chờ 2 (Flip Clock, Thứ ngày tháng, Thời tiết, Pin & Wi-Fi)
    for (int i = 0; i < 4; ++i) {
        if (flip_labels_[i]) lv_obj_set_style_text_color(flip_labels_[i], col, 0);
        if (flip_flap_labels_[i]) lv_obj_set_style_text_color(flip_flap_labels_[i], col, 0);
    }
    if (standby2_date_lbl_) lv_obj_set_style_text_color(standby2_date_lbl_, col, 0);
    if (standby2_city_lbl_) lv_obj_set_style_text_color(standby2_city_lbl_, col, 0);
    if (standby2_weather_temp_) lv_obj_set_style_text_color(standby2_weather_temp_, col, 0);
    if (standby2_wifi_icon_) lv_obj_set_style_text_color(standby2_wifi_icon_, col, 0);
    if (standby2_battery_pct_) lv_obj_set_style_text_color(standby2_battery_pct_, col, 0);
    if (standby2_battery_icon_) lv_obj_set_style_text_color(standby2_battery_icon_, col, 0);
}



void LcdDisplay::UpdateColorDotsPalette() {

    const uint32_t dark_palette[10] = {

        0xFFFFFF, // Trắng sáng mặc định

        0xFDE047, // Vàng chanh

        0x38BDF8, // Xanh da trời

        0x4ADE80, // Xanh lá

        0xFB923C, // Cam tươi

        0xF472B6, // Hồng phấn

        0xC084FC, // Tím Lavender

        0xF87171, // Đỏ san hô

        0x2DD4BF, // Xanh Teal

        0xFEF08A  // Vàng nhạt kem

    };

    const uint32_t light_palette[10] = {

        0x0F172A, // Đen đậm mặc định (tương phản cực cao trên nền trắng)

        0x1E3A8A, // Xanh Navy đậm

        0x991B1B, // Đỏ sẫm Ruby

        0x14532D, // Xanh lá rừng đậm

        0x581C87, // Tím than

        0x7C2D12, // Nâu Chocolate đậm

        0x0E7490, // Xanh cổ vịt đậm

        0x831843, // Hồng mận đậm

        0x334155, // Xám than chì

        0x431407  // Cà phê đậm

    };

    const uint32_t* current = is_light_theme_ ? light_palette : dark_palette;

    for (int i = 0; i < 10; ++i) {

        if (qs_color_dots_[i]) {

            lv_obj_set_style_bg_color(qs_color_dots_[i], lv_color_hex(current[i]), 0);

            lv_obj_set_user_data(qs_color_dots_[i], (void*)(intptr_t)current[i]);

        }

    }

    SetStandbyTextColor(is_light_theme_ ? 0x0F172A : 0xFFFFFF);

    UpdateColorDotsSelection();

}



void LcdDisplay::UpdateColorDotsSelection() {

    lv_color_t ring_color = is_light_theme_ ? lv_color_hex(0x0F172A) : lv_color_white();

    for (int i = 0; i < 10; ++i) {

        if (qs_color_dots_[i] == nullptr) continue;

        uint32_t col = (uint32_t)(intptr_t)lv_obj_get_user_data(qs_color_dots_[i]);

        if (col == standby_text_color_) {

            // Selected: viền dày tương phản + vòng tròn bao quanh nổi bật (Apple-style selection ring)

            lv_obj_set_style_outline_width(qs_color_dots_[i], 2, 0);

            lv_obj_set_style_outline_color(qs_color_dots_[i], ring_color, 0);

            lv_obj_set_style_outline_pad(qs_color_dots_[i], 2, 0);

            lv_obj_set_style_outline_opa(qs_color_dots_[i], LV_OPA_COVER, 0);

            lv_obj_set_style_border_width(qs_color_dots_[i], 2, 0);

            lv_obj_set_style_border_color(qs_color_dots_[i], ring_color, 0);

        } else {

            // Unselected: viền mảnh xám tinh tế, không có outline

            lv_obj_set_style_outline_width(qs_color_dots_[i], 0, 0);

            lv_obj_set_style_outline_opa(qs_color_dots_[i], LV_OPA_TRANSP, 0);

            lv_obj_set_style_border_width(qs_color_dots_[i], 1, 0);

            lv_obj_set_style_border_color(qs_color_dots_[i], lv_color_hex(0x475569), 0);

        }

    }

}



void LcdDisplay::SetMenuPage(int page, bool animate, bool swipe_left) {

    if (page < 0) page = 0;

    if (page > 1) page = 1; // Chỉ có 2 trang menu

    int old_page = menu_page_;

    menu_page_ = page;



    // Cập nhật 2 dấu chấm trang

    for (int i = 0; i < 2; ++i) {

        if (menu_dots_[i] == nullptr) continue;

        if (i == page) {

            lv_obj_set_style_bg_color(menu_dots_[i], is_light_theme_ ? lv_color_hex(0x0284C7) : lv_color_white(), 0);

            lv_obj_set_style_bg_opa(menu_dots_[i], LV_OPA_COVER, 0);

            lv_obj_set_size(menu_dots_[i], 14, 6);

            lv_obj_set_style_radius(menu_dots_[i], 3, 0);

        } else {

            lv_obj_set_style_bg_color(menu_dots_[i], is_light_theme_ ? lv_color_hex(0x94A3B8) : lv_color_hex(0x64748B), 0);

            lv_obj_set_style_bg_opa(menu_dots_[i], LV_OPA_70, 0);

            lv_obj_set_size(menu_dots_[i], 6, 6);

            lv_obj_set_style_radius(menu_dots_[i], 3, 0);

        }

    }

    if (menu_dots_[2]) lv_obj_add_flag(menu_dots_[2], LV_OBJ_FLAG_HIDDEN);



    if (!animate || old_page == page) {

        for (int p = 0; p < 3; ++p) {

            if (menu_page_box_[p] == nullptr) continue;

            lv_anim_delete(menu_page_box_[p], nullptr);

            lv_obj_set_x(menu_page_box_[p], 0);

            if (p == page) {

                lv_obj_remove_flag(menu_page_box_[p], LV_OBJ_FLAG_HIDDEN);

            } else {

                lv_obj_add_flag(menu_page_box_[p], LV_OBJ_FLAG_HIDDEN);

            }

        }

        return;

    }



    // Huỷ các animation đang chạy trên các trang và ẩn trang không liên quan

    for (int p = 0; p < 3; ++p) {

        if (menu_page_box_[p] == nullptr) continue;

        lv_anim_delete(menu_page_box_[p], nullptr);

        if (p != old_page && p != page) {

            lv_obj_add_flag(menu_page_box_[p], LV_OBJ_FLAG_HIDDEN);

            lv_obj_set_x(menu_page_box_[p], 0);

        }

    }



    // swipe_left: vuốt ngón tay sang trái -> trang mới vào từ bên phải (+width_ -> 0), trang cũ trượt ra bên trái (0 -> -width_)

    // !swipe_left: vuốt ngón tay sang phải -> trang mới vào từ bên trái (-width_ -> 0), trang cũ trượt ra bên phải (0 -> +width_)

    int new_start_x = swipe_left ? width_ : -width_;

    int old_target_x = swipe_left ? -width_ : width_;



    if (menu_page_box_[old_page]) {

        lv_obj_remove_flag(menu_page_box_[old_page], LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_x(menu_page_box_[old_page], 0);

        lv_anim_t a_old;

        lv_anim_init(&a_old);

        lv_anim_set_var(&a_old, menu_page_box_[old_page]);

        lv_anim_set_values(&a_old, 0, old_target_x);

        lv_anim_set_duration(&a_old, 220);

        lv_anim_set_exec_cb(&a_old, [](void* var, int32_t val) {

            lv_obj_set_x((lv_obj_t*)var, val);

        });

        lv_anim_set_path_cb(&a_old, lv_anim_path_ease_out);

        lv_anim_set_completed_cb(&a_old, [](lv_anim_t* a) {

            auto* obj = static_cast<lv_obj_t*>(a->var);

            if (obj) {

                lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);

                lv_obj_set_x(obj, 0);

            }

        });

        lv_anim_start(&a_old);

    }



    if (menu_page_box_[page]) {

        lv_obj_remove_flag(menu_page_box_[page], LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_x(menu_page_box_[page], new_start_x);

        lv_anim_t a_new;

        lv_anim_init(&a_new);

        lv_anim_set_var(&a_new, menu_page_box_[page]);

        lv_anim_set_values(&a_new, new_start_x, 0);

        lv_anim_set_duration(&a_new, 220);

        lv_anim_set_exec_cb(&a_new, [](void* var, int32_t val) {

            lv_obj_set_x((lv_obj_t*)var, val);

        });

        lv_anim_set_path_cb(&a_new, lv_anim_path_ease_out);

        lv_anim_start(&a_new);

    }

}




void LcdDisplay::UpdateStandbyStatusBar() {
    if ((standby_box_ == nullptr || lv_obj_has_flag(standby_box_, LV_OBJ_FLAG_HIDDEN)) &&
        (standby2_box_ == nullptr || lv_obj_has_flag(standby2_box_, LV_OBJ_FLAG_HIDDEN))) {
        return;
    }
    auto& board = Board::GetInstance();
    const char* wifi_icon = board.GetNetworkStateIcon();
    if (wifi_icon != nullptr) {
        if (standby_wifi_label_ != nullptr) {
            lv_label_set_text(standby_wifi_label_, wifi_icon);
        }
        if (standby2_wifi_icon_ != nullptr) {
            lv_label_set_text(standby2_wifi_icon_, wifi_icon);
        }
    }
    if (standby_battery_label_ != nullptr || standby_battery_pct_label_ != nullptr ||
        standby2_battery_icon_ != nullptr || standby2_battery_pct_ != nullptr) {
        int battery_level = 0;
        bool charging = false, discharging = false;
        if (board.GetBatteryLevel(battery_level, charging, discharging)) {
            if (battery_level < 0) battery_level = 0;
            if (battery_level > 100) battery_level = 100;
            char pct_str[16];
            snprintf(pct_str, sizeof(pct_str), "%d%%", battery_level);
            if (standby_battery_pct_label_ != nullptr) {
                lv_label_set_text(standby_battery_pct_label_, pct_str);
            }
            if (standby2_battery_pct_ != nullptr) {
                lv_label_set_text(standby2_battery_pct_, pct_str);
            }
            const char* b_icon = MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL;
            if (charging) {
                if (battery_level >= 100) {
                    b_icon = MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL;
                } else {
                    b_icon = MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_BOLT;
                }
            } else {
                const char* levels[] = {
                    MATERIAL_SYMBOLS_BATTERY_ANDROID_0,
                    MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_1,
                    MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_2,
                    MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_3,
                    MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_4,
                    MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_5,
                    MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_6,
                    MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL,
                };
                int level_index = battery_level <= 0
                                      ? 0
                                      : (battery_level >= 100 ? 7 : 1 + ((battery_level - 1) * 6 / 99));
                b_icon = levels[level_index];
            }
            if (standby_battery_label_ != nullptr) {
                lv_label_set_text(standby_battery_label_, b_icon);
            }
            if (standby2_battery_icon_ != nullptr) {
                lv_label_set_text(standby2_battery_icon_, b_icon);
            }
        } else {
            if (standby_battery_label_ != nullptr) lv_label_set_text(standby_battery_label_, "");
            if (standby_battery_pct_label_ != nullptr) lv_label_set_text(standby_battery_pct_label_, "");
            if (standby2_battery_icon_ != nullptr) lv_label_set_text(standby2_battery_icon_, "");
            if (standby2_battery_pct_ != nullptr) lv_label_set_text(standby2_battery_pct_, "");
        }
    }
    if (standby_right_icons_ != nullptr) {
        lv_obj_align(standby_right_icons_, LV_ALIGN_RIGHT_MID, (height_ > width_) ? -8 : -10, 0);
    }
    if (standby_status_bar_ != nullptr) {
        lv_obj_move_foreground(standby_status_bar_);
    }
}

void LcdDisplay::UpdateMenuStatusBar() {
    if (menu_box_ == nullptr || lv_obj_has_flag(menu_box_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    time_t now = time(nullptr);
    struct tm* tm_now = localtime(&now);
    if (menu_time_label_ != nullptr) {
        if (tm_now != nullptr && tm_now->tm_year >= 2025 - 1900) {
            char time_str[8];
            strftime(time_str, sizeof(time_str), "%H:%M", tm_now);
            lv_label_set_text(menu_time_label_, time_str);
        } else {
            lv_label_set_text(menu_time_label_, "--:--");
        }
    }
    if (menu_wifi_label_ != nullptr) {
        auto& board = Board::GetInstance();
        const char* icon = board.GetNetworkStateIcon();
        if (icon != nullptr) {
            lv_label_set_text(menu_wifi_label_, icon);
        }
    }
    if (menu_battery_label_ != nullptr || menu_battery_pct_label_ != nullptr) {
        auto& board = Board::GetInstance();
        int battery_level = 0;
        bool charging = false, discharging = false;
        if (board.GetBatteryLevel(battery_level, charging, discharging)) {
            if (battery_level < 0) battery_level = 0;
            if (battery_level > 100) battery_level = 100;
            if (menu_battery_pct_label_ != nullptr) {
                char pct_str[16];
                snprintf(pct_str, sizeof(pct_str), "%d%%", battery_level);
                lv_label_set_text(menu_battery_pct_label_, pct_str);
            }
            if (menu_battery_label_ != nullptr) {
                if (charging) {
                    if (battery_level >= 100) {
                        lv_label_set_text(menu_battery_label_, MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL);
                    } else {
                        lv_label_set_text(menu_battery_label_, MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_BOLT);
                    }
                } else {
                    const char* levels[] = {
                        MATERIAL_SYMBOLS_BATTERY_ANDROID_0,
                        MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_1,
                        MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_2,
                        MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_3,
                        MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_4,
                        MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_5,
                        MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_6,
                        MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL,
                    };
                    int level_index = battery_level <= 0 ? 0 : (battery_level >= 100 ? 7 : 1 + ((battery_level - 1) * 6 / 99));
                    lv_label_set_text(menu_battery_label_, levels[level_index]);
                }
            }
        } else {
            if (menu_battery_label_ != nullptr) lv_label_set_text(menu_battery_label_, "");
            if (menu_battery_pct_label_ != nullptr) lv_label_set_text(menu_battery_pct_label_, "");
        }
    }
}

void LcdDisplay::CreateMenuUI() {
    auto screen = lv_screen_active();
    menu_box_ = lv_obj_create(screen);
    lv_obj_set_size(menu_box_, width_, height_);
    lv_obj_set_style_bg_color(menu_box_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(menu_box_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(menu_box_, 0, 0);
    lv_obj_set_style_border_width(menu_box_, 0, 0);
    lv_obj_set_style_pad_all(menu_box_, 0, 0);
    lv_obj_clear_flag(menu_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);

    // Tạo 3 page containers để trượt animation mượt mà khi chuyển trang
    for (int p = 0; p < 3; ++p) {
        menu_page_box_[p] = lv_obj_create(menu_box_);
        lv_obj_set_size(menu_page_box_[p], width_, height_);
        lv_obj_set_pos(menu_page_box_[p], 0, 0);
        lv_obj_set_style_bg_opa(menu_page_box_[p], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(menu_page_box_[p], 0, 0);
        lv_obj_set_style_pad_all(menu_page_box_[p], 0, 0);
        lv_obj_clear_flag(menu_page_box_[p], LV_OBJ_FLAG_SCROLLABLE);
        if (p > 0) lv_obj_add_flag(menu_page_box_[p], LV_OBJ_FLAG_HIDDEN);
    }

    const char* titles[12] = {
        // Trang 1 (6 icons to rõ)
        "Lịch", "Đồng hồ", "Thời tiết",
        "Tỷ giá", "Trợ lý AI", "Cài đặt",
        // Trang 2 (6 icons to rõ)
        "Âm nhạc", "Hình nền", "Máy tính",
        "Đài FM", "Mã QR", "Đèn pin"
    };

    time_t now_cal = time(nullptr);
    struct tm* tm_now = localtime(&now_cal);
    int cur_day = (tm_now != nullptr && tm_now->tm_mday >= 1) ? tm_now->tm_mday : 7;
    int cur_wday = (tm_now != nullptr) ? tm_now->tm_wday : 6;
    const char* wday_names[7] = {"CN", "T.2", "T.3", "T.4", "T.5", "T.6", "T.7"};

    for (int i = 0; i < 22; ++i) {
        menu_items_[i] = nullptr;
    }

    for (int i = 0; i < 12; ++i) {
        int p = (i < 6) ? 0 : 1; // 6 icon mỗi trang
        lv_obj_t* btn = lv_obj_create(menu_page_box_[p]);
        menu_items_[i] = btn;
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* tile = lv_obj_create(btn);
        lv_obj_set_size(tile, 66, 66);
        lv_obj_align(tile, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(tile, 16, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE);

        if (i == 0) {
            // Lịch (iOS Calendar badge phong cách lớn)
            lv_obj_set_style_bg_color(tile, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

            lv_obj_t* red_bar = lv_obj_create(tile);
            lv_obj_set_size(red_bar, 66, 20);
            lv_obj_align(red_bar, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_set_style_bg_color(red_bar, lv_color_hex(0xFF3B30), 0);
            lv_obj_set_style_bg_opa(red_bar, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(red_bar, 0, 0);
            lv_obj_set_style_border_width(red_bar, 0, 0);
            lv_obj_set_style_pad_all(red_bar, 0, 0);
            lv_obj_clear_flag(red_bar, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(red_bar, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t* wday_lbl = lv_label_create(red_bar);
            lv_label_set_text(wday_lbl, wday_names[cur_wday]);
            lv_obj_set_style_text_font(wday_lbl, &font_noto_sans_custom_14_1, 0);
            lv_obj_set_style_text_color(wday_lbl, lv_color_white(), 0);
            lv_obj_align(wday_lbl, LV_ALIGN_CENTER, 0, 0);

            char day_str[16];
            snprintf(day_str, sizeof(day_str), "%d", cur_day);
            lv_obj_t* day_lbl = lv_label_create(tile);
            lv_label_set_text(day_lbl, day_str);
            lv_obj_set_style_text_font(day_lbl, &font_noto_sans_basic_30_4, 0);
            lv_obj_set_style_text_color(day_lbl, lv_color_hex(0x1C1C1E), 0);
            lv_obj_align(day_lbl, LV_ALIGN_CENTER, 0, 7);
        } else if (i == 1) {
            // Đồng hồ
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x1C1C1E), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_SCHEDULE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 2) {
            // Thời tiết
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x29B6F6), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_image_create(tile);
            lv_image_set_src(icon, &weather_icon_sun_cloud_24_dsc);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 3) {
            // Tỷ giá
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x0F172A), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

            lv_obj_t* circle = lv_obj_create(tile);
            lv_obj_set_size(circle, 46, 46);
            lv_obj_set_style_radius(circle, 23, 0);
            lv_obj_set_style_bg_color(circle, lv_color_hex(0x10B981), 0);
            lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(circle, 0, 0);
            lv_obj_set_style_pad_all(circle, 0, 0);
            lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
            lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t* icon = lv_label_create(circle);
            lv_label_set_text(icon, "$");
            lv_obj_set_style_text_font(icon, &font_noto_sans_basic_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 4) {
            // Trợ lý AI
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_CHAT_BUBBLE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 5) {
            // Cài đặt
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x475569), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_SETTINGS);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 6) {
            // Âm nhạc
            lv_obj_set_style_bg_color(tile, lv_color_hex(0xEC4899), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_MUSIC_NOTE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 7) {
            // Hình nền
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x0D9488), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_IMAGE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 8) {
            // Máy tính
            lv_obj_set_style_bg_color(tile, lv_color_hex(0xF97316), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_CALCULATE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 9) {
            // Đài FM
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x8B5CF6), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, "FM");
            lv_obj_set_style_text_font(icon, &font_noto_sans_basic_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 10) {
            // Mã QR
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x0284C7), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, "[QR]");
            lv_obj_set_style_text_font(icon, &font_noto_sans_basic_20_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 11) {
            // Đèn pin
            lv_obj_set_style_bg_color(tile, lv_color_hex(0xEAB308), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_BRIGHTNESS_6);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        }

        // Title label below tile
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, titles[i]);
        lv_obj_set_style_text_font(lbl, &font_noto_sans_basic_14_1, 0);
        lv_obj_set_style_text_color(lbl, is_light_theme_ ? lv_color_hex(0x0F172A) : lv_color_white(), 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            uint32_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms - self->last_swipe_time_ < 500) {
                return;
            }
            auto* current_btn = lv_event_get_current_target_obj(e);
            int index = (int)(intptr_t)lv_obj_get_user_data(current_btn);
            if (index == 0) {
                self->ShowCalendar();
            } else if (index == 1) {
                self->ShowClock();
            } else if (index == 2) {
                self->ShowWeatherDetail();
            } else if (index == 3) {
                self->ShowExchangeRates();
            } else if (index == 4) {
                self->ShowStandby();
                Application::GetInstance().ToggleChatState();
            } else if (index == 5) {
                self->ShowQuickSettings();
            } else if (index == 6) {
                self->ShowMusicScreen();
            } else if (index == 7) {
                self->ShowWallpaperSettings();
            } else if (index == 8) {
                self->ShowCalculator();
            } else if (index == 9) {
                self->ShowFmRadio();
            } else if (index == 10) {
                self->ShowNotification("Mã QR: Quét kết nối", 1800);
            } else if (index == 11) {
                self->ShowFlashlight();
            }
        }, LV_EVENT_CLICKED, this);
    }

    // Indicator dots (2 trang)
    for (int i = 0; i < 3; ++i) {
        menu_dots_[i] = lv_obj_create(menu_box_);
        lv_obj_set_size(menu_dots_[i], 6, 6);
        lv_obj_set_style_radius(menu_dots_[i], 3, 0);
        lv_obj_set_style_border_width(menu_dots_[i], 0, 0);
        lv_obj_set_style_pad_all(menu_dots_[i], 0, 0);
        lv_obj_clear_flag(menu_dots_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(menu_dots_[i], LV_OBJ_FLAG_CLICKABLE);
    }

    // Menu Status Bar (Fixed on top of menu_box_)
    menu_status_bar_ = lv_obj_create(menu_box_);
    lv_obj_set_size(menu_status_bar_, width_, 30);
    lv_obj_set_pos(menu_status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(menu_status_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menu_status_bar_, 0, 0);
    lv_obj_set_style_pad_all(menu_status_bar_, 0, 0);
    lv_obj_clear_flag(menu_status_bar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(menu_status_bar_, LV_OBJ_FLAG_CLICKABLE);

    // GÓC TRÁI: Giờ phút (HH:MM) - to rõ ràng font 20pt
    menu_time_label_ = lv_label_create(menu_status_bar_);
    lv_label_set_text(menu_time_label_, "");
    lv_obj_set_style_text_font(menu_time_label_, &font_noto_sans_basic_20_4, 0);
    lv_obj_set_style_text_color(menu_time_label_, lv_color_white(), 0);
    lv_obj_align(menu_time_label_, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_clear_flag(menu_time_label_, LV_OBJ_FLAG_CLICKABLE);

    // GÓC PHẢI: Container chứa Sóng WiFi và Pin - to rõ nét font 30pt
    lv_obj_t* right_icons = lv_obj_create(menu_status_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, 30);
    lv_obj_align(right_icons, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_icons, 6, 0);
    lv_obj_clear_flag(right_icons, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(right_icons, LV_OBJ_FLAG_CLICKABLE);

    menu_wifi_label_ = lv_label_create(right_icons);
    lv_label_set_text(menu_wifi_label_, "");
    lv_obj_set_style_text_font(menu_wifi_label_, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(menu_wifi_label_, lv_color_white(), 0);
    lv_obj_clear_flag(menu_wifi_label_, LV_OBJ_FLAG_CLICKABLE);

    menu_battery_pct_label_ = lv_label_create(right_icons);
    lv_label_set_text(menu_battery_pct_label_, "");
    lv_obj_set_style_text_font(menu_battery_pct_label_, &font_noto_sans_basic_14_1, 0);
    lv_obj_set_style_text_color(menu_battery_pct_label_, lv_color_white(), 0);
    lv_obj_clear_flag(menu_battery_pct_label_, LV_OBJ_FLAG_CLICKABLE);

    menu_battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(menu_battery_label_, "");
    lv_obj_set_style_text_font(menu_battery_label_, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(menu_battery_label_, lv_color_white(), 0);
    lv_obj_clear_flag(menu_battery_label_, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_move_foreground(menu_status_bar_);

    LayoutMenuWidgets(height_ > width_);
}

void LcdDisplay::LayoutMenuWidgets(bool is_portrait) {
    if (menu_box_ == nullptr) return;
    lv_obj_set_size(menu_box_, width_, height_);

    for (int p = 0; p < 3; ++p) {
        if (menu_page_box_[p]) {
            lv_obj_set_size(menu_page_box_[p], width_, height_);
            lv_obj_set_pos(menu_page_box_[p], 0, 0);
        }
    }

    if (menu_status_bar_) {
        lv_obj_set_size(menu_status_bar_, width_, is_portrait ? 30 : 26);
        lv_obj_set_pos(menu_status_bar_, 0, 0);
        if (menu_time_label_) {
            lv_obj_align(menu_time_label_, LV_ALIGN_LEFT_MID, 12, 0);
        }
    }

    if (is_portrait) {
        // 240 x 320: 2 columns x 3 rows (6 icons / trang, cỡ lớn 66x66 px)
        int col_x[2] = {10, 124};
        int row_y[3] = {34, 124, 214};
        for (int i = 0; i < 12; ++i) {
            if (menu_items_[i] == nullptr) continue;
            int page_idx = (i < 6) ? i : (i - 6);
            int r = page_idx / 2;
            int c = page_idx % 2;
            lv_obj_set_size(menu_items_[i], 106, 86);
            lv_obj_set_pos(menu_items_[i], col_x[c], row_y[r]);

            lv_obj_t* tile = lv_obj_get_child(menu_items_[i], 0);
            if (tile) {
                lv_obj_set_size(tile, 66, 66);
                lv_obj_set_style_radius(tile, 16, 0);
                if (i == 0) {
                    lv_obj_t* red_bar = lv_obj_get_child(tile, 0);
                    if (red_bar) lv_obj_set_size(red_bar, 66, 20);
                } else if (i == 3) {
                    lv_obj_t* circle = lv_obj_get_child(tile, 0);
                    if (circle) {
                        lv_obj_set_size(circle, 46, 46);
                        lv_obj_set_style_radius(circle, 23, 0);
                    }
                }
            }
            lv_obj_t* lbl = lv_obj_get_child(menu_items_[i], 1);
            if (lbl) {
                lv_obj_set_width(lbl, 106);
                lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 0);
            }
        }
        if (menu_dots_[0] && menu_dots_[1]) {
            lv_obj_set_pos(menu_dots_[0], 120 - 10, 306);
            lv_obj_set_pos(menu_dots_[1], 120 + 2, 306);
        }
        if (menu_dots_[2]) lv_obj_add_flag(menu_dots_[2], LV_OBJ_FLAG_HIDDEN);
    } else {
        // 320 x 240: 3 columns x 2 rows (6 icons / trang)
        int col_x[3] = {16, 116, 216};
        int row_y[2] = {30, 120};
        for (int i = 0; i < 12; ++i) {
            if (menu_items_[i] == nullptr) continue;
            int page_idx = (i < 6) ? i : (i - 6);
            int r = page_idx / 3;
            int c = page_idx % 3;
            lv_obj_set_size(menu_items_[i], 88, 80);
            lv_obj_set_pos(menu_items_[i], col_x[c], row_y[r]);

            lv_obj_t* tile = lv_obj_get_child(menu_items_[i], 0);
            if (tile) {
                lv_obj_set_size(tile, 56, 56);
                lv_obj_set_style_radius(tile, 14, 0);
                if (i == 0) {
                    lv_obj_t* red_bar = lv_obj_get_child(tile, 0);
                    if (red_bar) lv_obj_set_size(red_bar, 56, 16);
                } else if (i == 3) {
                    lv_obj_t* circle = lv_obj_get_child(tile, 0);
                    if (circle) {
                        lv_obj_set_size(circle, 38, 38);
                        lv_obj_set_style_radius(circle, 19, 0);
                    }
                }
            }
            lv_obj_t* lbl = lv_obj_get_child(menu_items_[i], 1);
            if (lbl) {
                lv_obj_set_width(lbl, 88);
                lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 0);
            }
        }
        if (menu_dots_[0] && menu_dots_[1]) {
            lv_obj_set_pos(menu_dots_[0], 160 - 10, 218);
            lv_obj_set_pos(menu_dots_[1], 160 + 2, 218);
        }
        if (menu_dots_[2]) lv_obj_add_flag(menu_dots_[2], LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < 3; ++i) {
        if (menu_dots_[i]) lv_obj_move_foreground(menu_dots_[i]);
    }
    if (menu_status_bar_) lv_obj_move_foreground(menu_status_bar_);
    SetMenuPage(menu_page_, false);
}

void LcdDisplay::CreateQuickSettingsUI() {
    auto screen = lv_screen_active();
    quick_settings_box_ = lv_obj_create(screen);
    lv_obj_set_size(quick_settings_box_, width_, height_);
    lv_obj_set_style_bg_color(quick_settings_box_, lv_color_hex(0x0a0f1d), 0);
    lv_obj_set_style_bg_opa(quick_settings_box_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(quick_settings_box_, 0, 0);
    lv_obj_set_style_border_width(quick_settings_box_, 0, 0);
    lv_obj_set_style_pad_all(quick_settings_box_, 0, 0);
    lv_obj_clear_flag(quick_settings_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

    // Title - Header cố định không bị cuộn mất, chữ to hơn (20pt)
    qs_title_ = lv_label_create(quick_settings_box_);
    lv_label_set_text(qs_title_, "Cài đặt nhanh");
    lv_obj_set_style_text_font(qs_title_, &font_noto_sans_custom_20_4, 0);
    lv_obj_set_style_text_color(qs_title_, lv_color_white(), 0);

    // Close button - Nút 'X' to hơn, xích vào trong cách mép, cố định ở trên cùng
    qs_close_btn_ = lv_button_create(quick_settings_box_);
    lv_obj_set_size(qs_close_btn_, 42, 36);
    lv_obj_set_style_bg_opa(qs_close_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(qs_close_btn_, 0, 0);
    lv_obj_set_style_pad_all(qs_close_btn_, 0, 0);
    lv_obj_set_ext_click_area(qs_close_btn_, 8);
    lv_obj_t* close_lbl = lv_label_create(qs_close_btn_);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_font(close_lbl, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_align(close_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(qs_close_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
        self->ExitQuickSettings();
    }, LV_EVENT_CLICKED, this);

    // Scrollable Content Box chứa toàn bộ nội dung cuộn bên dưới
    qs_content_box_ = lv_obj_create(quick_settings_box_);
    lv_obj_set_style_bg_opa(qs_content_box_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(qs_content_box_, 0, 0);
    lv_obj_set_style_border_width(qs_content_box_, 0, 0);
    lv_obj_set_style_pad_all(qs_content_box_, 0, 0);
    lv_obj_add_flag(qs_content_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(qs_content_box_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(qs_content_box_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(qs_content_box_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (self && self->qs_content_box_) {
            self->qs_at_bottom_.store(lv_obj_get_scroll_bottom(self->qs_content_box_) <= 15);
        }
    }, LV_EVENT_SCROLL, this);

    // Sliders Card (trong qs_content_box_)
    qs_sliders_card_ = lv_obj_create(qs_content_box_);
    lv_obj_set_style_bg_color(qs_sliders_card_, lv_color_hex(0x151e2e), 0);
    lv_obj_set_style_bg_opa(qs_sliders_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qs_sliders_card_, 10, 0);
    lv_obj_set_style_border_width(qs_sliders_card_, 0, 0);
    lv_obj_set_style_pad_all(qs_sliders_card_, 0, 0);
    lv_obj_clear_flag(qs_sliders_card_, LV_OBJ_FLAG_SCROLLABLE);

    // Brightness row - icon & slider màu trắng
    lv_obj_t* b_icon = lv_label_create(qs_sliders_card_);
    lv_label_set_text(b_icon, MATERIAL_SYMBOLS_BRIGHTNESS_6);
    lv_obj_set_style_text_font(b_icon, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(b_icon, lv_color_white(), 0);
    lv_obj_set_user_data(b_icon, (void*)(intptr_t)101);

    qs_brightness_slider_ = lv_slider_create(qs_sliders_card_);
    lv_slider_set_range(qs_brightness_slider_, 10, 100);
    int b_val = Board::GetInstance().GetBacklight() ? Board::GetInstance().GetBacklight()->brightness() : 100;
    lv_slider_set_value(qs_brightness_slider_, b_val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(qs_brightness_slider_, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_bg_color(qs_brightness_slider_, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(qs_brightness_slider_, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_radius(qs_brightness_slider_, 4, LV_PART_INDICATOR);
    lv_obj_set_style_radius(qs_brightness_slider_, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(qs_brightness_slider_, 4, LV_PART_KNOB);
    lv_obj_set_ext_click_area(qs_brightness_slider_, 6);

    qs_brightness_label_ = lv_label_create(qs_sliders_card_);
    char b_buf[16];
    snprintf(b_buf, sizeof(b_buf), "%d%%", b_val);
    lv_label_set_text(qs_brightness_label_, b_buf);
    lv_obj_set_style_text_font(qs_brightness_label_, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(qs_brightness_label_, lv_color_white(), 0);

    lv_obj_add_event_cb(qs_brightness_slider_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        int val = lv_slider_get_value(self->qs_brightness_slider_);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(self->qs_brightness_label_, buf);
        if (Board::GetInstance().GetBacklight()) {
            Board::GetInstance().GetBacklight()->SetBrightness(val, true);
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    // Volume row - icon & slider màu trắng
    lv_obj_t* v_icon = lv_label_create(qs_sliders_card_);
    lv_label_set_text(v_icon, MATERIAL_SYMBOLS_VOLUME_UP);
    lv_obj_set_style_text_font(v_icon, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(v_icon, lv_color_white(), 0);
    lv_obj_set_user_data(v_icon, (void*)(intptr_t)102);

    qs_volume_slider_ = lv_slider_create(qs_sliders_card_);
    lv_slider_set_range(qs_volume_slider_, 0, 100);
    int v_val = Board::GetInstance().GetAudioCodec() ? Board::GetInstance().GetAudioCodec()->output_volume() : 70;
    lv_slider_set_value(qs_volume_slider_, v_val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(qs_volume_slider_, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_bg_color(qs_volume_slider_, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(qs_volume_slider_, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_radius(qs_volume_slider_, 4, LV_PART_INDICATOR);
    lv_obj_set_style_radius(qs_volume_slider_, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(qs_volume_slider_, 4, LV_PART_KNOB);
    lv_obj_set_ext_click_area(qs_volume_slider_, 6);

    qs_volume_label_ = lv_label_create(qs_sliders_card_);
    char v_buf[16];
    snprintf(v_buf, sizeof(v_buf), "%d%%", v_val);
    lv_label_set_text(qs_volume_label_, v_buf);
    lv_obj_set_style_text_font(qs_volume_label_, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(qs_volume_label_, lv_color_white(), 0);

    lv_obj_add_event_cb(qs_volume_slider_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        int val = lv_slider_get_value(self->qs_volume_slider_);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(self->qs_volume_label_, buf);
        if (Board::GetInstance().GetAudioCodec()) {
            Board::GetInstance().GetAudioCodec()->SetOutputVolume(val);
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    // Color Palette Card (trong qs_content_box_)
    qs_color_card_ = lv_obj_create(qs_content_box_);
    lv_obj_set_style_bg_color(qs_color_card_, lv_color_hex(0x151e2e), 0);
    lv_obj_set_style_bg_opa(qs_color_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qs_color_card_, 10, 0);
    lv_obj_set_style_border_width(qs_color_card_, 0, 0);
    lv_obj_set_style_pad_all(qs_color_card_, 0, 0);
    lv_obj_clear_flag(qs_color_card_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* col_title = lv_label_create(qs_color_card_);
    lv_label_set_text(col_title, "Màu chữ màn hình");
    lv_obj_set_style_text_font(col_title, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(col_title, lv_color_white(), 0);
    lv_obj_set_user_data(col_title, (void*)(intptr_t)103);

    for (int i = 0; i < 10; ++i) {
        lv_obj_t* dot = lv_obj_create(qs_color_card_);
        qs_color_dots_[i] = dot;
        lv_obj_set_size(dot, 20, 20);
        lv_obj_set_style_radius(dot, 10, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(0x475569), 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(dot, 8);

        lv_obj_add_event_cb(dot, [](lv_event_t* e) {
            auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
            auto* target = lv_event_get_current_target_obj(e);
            uint32_t col = (uint32_t)(intptr_t)lv_obj_get_user_data(target);
            self->SetStandbyTextColor(col);
            self->ShowNotification("Đã đổi màu chữ", 1200);
        }, LV_EVENT_CLICKED, this);
    }

    // Helper lambda for border buttons: nền tối #151e2e, viền trắng mỏng 1px
    auto style_action_btn = [](lv_obj_t* btn) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x151e2e), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_white(), 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
    };

    // 1. Trợ lý AI (trong qs_content_box_)
    qs_ai_btn_ = lv_button_create(qs_content_box_);
    style_action_btn(qs_ai_btn_);
    lv_obj_t* ai_lbl = lv_label_create(qs_ai_btn_);
    lv_label_set_text(ai_lbl, "Trợ lý ảo");
    lv_obj_set_style_text_font(ai_lbl, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(ai_lbl, lv_color_white(), 0);
    lv_obj_align(ai_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(qs_ai_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
        self->ExitQuickSettings();
        Application::GetInstance().ToggleChatState();
    }, LV_EVENT_CLICKED, this);

    // 2. Xoay màn hình (trong qs_content_box_)
    qs_rotate_btn_ = lv_button_create(qs_content_box_);
    style_action_btn(qs_rotate_btn_);
    lv_obj_t* rot_lbl = lv_label_create(qs_rotate_btn_);
    lv_label_set_text(rot_lbl, "Xoay màn hình");
    lv_obj_set_style_text_font(rot_lbl, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(rot_lbl, lv_color_white(), 0);
    lv_obj_align(rot_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(qs_rotate_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
        Board::GetInstance().ToggleRotation("toggle");
    }, LV_EVENT_CLICKED, this);

    // 3. Thêm ảnh (trong qs_content_box_) - Bấm vào hiện mã QR upload ảnh
    qs_wallpaper_btn_ = lv_button_create(qs_content_box_);
    style_action_btn(qs_wallpaper_btn_);
    lv_obj_t* wp_btn_lbl = lv_label_create(qs_wallpaper_btn_);
    lv_label_set_text(wp_btn_lbl, "Thêm ảnh");
    lv_obj_set_style_text_font(wp_btn_lbl, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(wp_btn_lbl, lv_color_white(), 0);
    lv_obj_align(wp_btn_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(qs_wallpaper_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
        self->ShowQrModal();
    }, LV_EVENT_CLICKED, this);

    // 4. Cấu hình WiFi (trong qs_content_box_)
    qs_wifi_btn_ = lv_button_create(qs_content_box_);
    style_action_btn(qs_wifi_btn_);
    lv_obj_t* wifi_lbl = lv_label_create(qs_wifi_btn_);
    lv_label_set_text(wifi_lbl, "Cấu hình WiFi");
    lv_obj_set_style_text_font(wifi_lbl, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(wifi_lbl, lv_color_white(), 0);
    lv_obj_align(wifi_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(qs_wifi_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
        self->ExitQuickSettings();
        self->ShowNotification("Vào cấu hình WiFi...", 2000);
        Board::GetInstance().EnterWifiConfigMode();
    }, LV_EVENT_CLICKED, this);

    // 5. Restart Button - Giữ nguyên màu đỏ nổi bật (trong qs_content_box_)
    qs_restart_btn_ = lv_button_create(qs_content_box_);
    lv_obj_set_style_bg_color(qs_restart_btn_, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_bg_opa(qs_restart_btn_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qs_restart_btn_, 8, 0);
    lv_obj_set_style_pad_all(qs_restart_btn_, 0, 0);
    lv_obj_t* rst_lbl = lv_label_create(qs_restart_btn_);
    lv_label_set_text(rst_lbl, "Khởi động lại");
    lv_obj_set_style_text_font(rst_lbl, &font_noto_sans_custom_20_4, 0);
    lv_obj_set_style_text_color(rst_lbl, lv_color_white(), 0);
    lv_obj_align(rst_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(qs_restart_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
        self->ShowNotification("Đang khởi động lại...", 1500);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }, LV_EVENT_CLICKED, this);

    // QR Modal Overlay
    CreateQrModal();

    // System Info Card (trong qs_content_box_)
    qs_info_card_ = lv_obj_create(qs_content_box_);
    lv_obj_set_style_bg_color(qs_info_card_, lv_color_hex(0x151e2e), 0);
    lv_obj_set_style_bg_opa(qs_info_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qs_info_card_, 10, 0);
    lv_obj_set_style_border_width(qs_info_card_, 0, 0);
    lv_obj_set_style_pad_all(qs_info_card_, 0, 0);
    lv_obj_clear_flag(qs_info_card_, LV_OBJ_FLAG_SCROLLABLE);

    auto make_info_label = [this](const char* placeholder) -> lv_obj_t* {
        lv_obj_t* lbl = lv_label_create(qs_info_card_);
        lv_label_set_text(lbl, placeholder);
        lv_obj_set_style_text_font(lbl, &font_noto_sans_custom_16_4, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        return lbl;
    };
    qs_info_wifi_ = make_info_label("WiFi: --");
    qs_info_ip_   = make_info_label("IP: --");
    qs_info_temp_ = make_info_label("Nhiệt độ: --");
    qs_info_sd_   = make_info_label("Thẻ nhớ SD: --");

    UpdateColorDotsPalette();
    LayoutQuickSettingsWidgets(height_ > width_);
}

void LcdDisplay::LayoutQuickSettingsWidgets(bool is_portrait) {
    if (quick_settings_box_ == nullptr) return;
    lv_obj_set_size(quick_settings_box_, width_, height_);

    lv_obj_t* b_icon = nullptr;
    lv_obj_t* v_icon = nullptr;
    if (qs_sliders_card_ != nullptr) {
        uint32_t cnt = lv_obj_get_child_count(qs_sliders_card_);
        for (uint32_t i = 0; i < cnt; ++i) {
            lv_obj_t* c = lv_obj_get_child(qs_sliders_card_, i);
            int tag = (int)(intptr_t)lv_obj_get_user_data(c);
            if (tag == 101) b_icon = c;
            else if (tag == 102) v_icon = c;
        }
    }

    lv_obj_t* col_title = nullptr;
    if (qs_color_card_ != nullptr) {
        uint32_t cnt = lv_obj_get_child_count(qs_color_card_);
        for (uint32_t i = 0; i < cnt; ++i) {
            lv_obj_t* c = lv_obj_get_child(qs_color_card_, i);
            int tag = (int)(intptr_t)lv_obj_get_user_data(c);
            if (tag == 103) col_title = c;
        }
    }

    if (is_portrait) {
        // 240 x 320 (Màn hình dọc)
        // Fixed Top Header (tiêu đề to 20pt, nút X to và xích sang trái)
        if (qs_title_) lv_obj_align(qs_title_, LV_ALIGN_TOP_MID, -10, 6);
        if (qs_close_btn_) {
            lv_obj_set_size(qs_close_btn_, 42, 34);
            lv_obj_set_pos(qs_close_btn_, 188, 3);
        }

        // Scrollable content area
        if (qs_content_box_) {
            lv_obj_set_pos(qs_content_box_, 0, 38);
            lv_obj_set_size(qs_content_box_, 240, 282);
        }

        // 1. Thanh kéo âm thanh & độ sáng (thanh mảnh cao 8px, card rộng 230px)
        if (qs_sliders_card_) {
            lv_obj_set_size(qs_sliders_card_, 230, 74);
            lv_obj_set_pos(qs_sliders_card_, 5, 4);
        }
        if (b_icon) lv_obj_set_pos(b_icon, 8, 4);
        if (qs_brightness_slider_) {
            lv_obj_set_size(qs_brightness_slider_, 120, 8);
            lv_obj_set_pos(qs_brightness_slider_, 38, 12);
        }
        if (qs_brightness_label_) lv_obj_set_pos(qs_brightness_label_, 176, 7);

        if (v_icon) lv_obj_set_pos(v_icon, 8, 40);
        if (qs_volume_slider_) {
            lv_obj_set_size(qs_volume_slider_, 120, 8);
            lv_obj_set_pos(qs_volume_slider_, 38, 48);
        }
        if (qs_volume_label_) lv_obj_set_pos(qs_volume_label_, 176, 43);

        // 2. Bảng chọn màu chữ: rộng 230px, cao 74px
        if (qs_color_card_) {
            lv_obj_set_size(qs_color_card_, 230, 74);
            lv_obj_set_pos(qs_color_card_, 5, 84);
        }
        if (col_title) lv_obj_set_pos(col_title, 10, 3);
        if (qs_color_card_) {
            int dot_idx = 0;
            uint32_t cnt = lv_obj_get_child_count(qs_color_card_);
            for (uint32_t i = 0; i < cnt; ++i) {
                lv_obj_t* c = lv_obj_get_child(qs_color_card_, i);
                if (c != col_title) {
                    int row = dot_idx / 5;
                    int col = dot_idx % 5;
                    lv_obj_set_size(c, 20, 20);
                    lv_obj_set_style_radius(c, 10, 0);
                    lv_obj_set_pos(c, 14 + col * 42, 24 + row * 24);
                    dot_idx++;
                }
            }
        }

        // 3. Các nút chức năng (mở rộng ô to ra: rộng 112px, cao 36px để chữ không bị sát viền)
        // Hàng 1 (y = 164): Trợ lý AI và Ảnh Bìa
        if (qs_ai_btn_) {
            lv_obj_set_size(qs_ai_btn_, 112, 36);
            lv_obj_set_pos(qs_ai_btn_, 5, 164);
        }
        if (qs_wallpaper_btn_) {
            lv_obj_clear_flag(qs_wallpaper_btn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(qs_wallpaper_btn_, 112, 36);
            lv_obj_set_pos(qs_wallpaper_btn_, 123, 164);
        }

        // Hàng 2 (y = 206): Xoay màn hình và Cấu hình WiFi (ô rộng 112px thoải mái chữ)
        if (qs_rotate_btn_) {
            lv_obj_set_size(qs_rotate_btn_, 112, 36);
            lv_obj_set_pos(qs_rotate_btn_, 5, 206);
        }
        if (qs_wifi_btn_) {
            lv_obj_set_size(qs_wifi_btn_, 112, 36);
            lv_obj_set_pos(qs_wifi_btn_, 123, 206);
        }

        // Hàng 3 (y = 248): Khởi động lại (to rõ 20pt, nút to 46px)
        if (qs_restart_btn_) {
            lv_obj_set_size(qs_restart_btn_, 230, 46);
            lv_obj_set_pos(qs_restart_btn_, 5, 248);
        }

        // Hàng 4 (y = 302): Thẻ thông tin hệ thống (chữ to 16_4 đồng bộ)
        if (qs_info_card_) {
            lv_obj_set_size(qs_info_card_, 230, 134);
            lv_obj_set_pos(qs_info_card_, 5, 302);
        }
        if (qs_info_wifi_) lv_obj_set_pos(qs_info_wifi_, 10, 8);
        if (qs_info_ip_) lv_obj_set_pos(qs_info_ip_, 10, 38);
        if (qs_info_temp_) lv_obj_set_pos(qs_info_temp_, 10, 68);
        if (qs_info_sd_) lv_obj_set_pos(qs_info_sd_, 10, 98);
    } else {
        // 320 x 240 (Màn hình ngang)
        // Fixed Top Header (tiêu đề to 20pt, nút X to và xích sang trái)
        if (qs_title_) lv_obj_set_pos(qs_title_, 14, 6);
        if (qs_close_btn_) {
            lv_obj_set_size(qs_close_btn_, 42, 32);
            lv_obj_set_pos(qs_close_btn_, 264, 2);
        }

        // Scrollable content area
        if (qs_content_box_) {
            lv_obj_set_pos(qs_content_box_, 0, 36);
            lv_obj_set_size(qs_content_box_, 320, 204);
        }

        // 1. Thanh kéo âm thanh & độ sáng
        if (qs_sliders_card_) {
            lv_obj_set_size(qs_sliders_card_, 304, 66);
            lv_obj_set_pos(qs_sliders_card_, 8, 4);
        }
        if (b_icon) lv_obj_set_pos(b_icon, 8, 4);
        if (qs_brightness_slider_) {
            lv_obj_set_size(qs_brightness_slider_, 200, 8);
            lv_obj_set_pos(qs_brightness_slider_, 36, 10);
        }
        if (qs_brightness_label_) lv_obj_set_pos(qs_brightness_label_, 254, 6);

        if (v_icon) lv_obj_set_pos(v_icon, 8, 36);
        if (qs_volume_slider_) {
            lv_obj_set_size(qs_volume_slider_, 200, 8);
            lv_obj_set_pos(qs_volume_slider_, 36, 42);
        }
        if (qs_volume_label_) lv_obj_set_pos(qs_volume_label_, 254, 38);

        // 2. Bảng chọn màu chữ - Tách riêng chữ ở trên (y=4) và 10 chấm màu ở dưới (y=28), KHÔNG BỊ CHE!
        if (qs_color_card_) {
            lv_obj_set_size(qs_color_card_, 304, 54);
            lv_obj_set_pos(qs_color_card_, 8, 76);
        }
        if (col_title) lv_obj_set_pos(col_title, 10, 4);
        if (qs_color_card_) {
            int dot_idx = 0;
            uint32_t cnt = lv_obj_get_child_count(qs_color_card_);
            for (uint32_t i = 0; i < cnt; ++i) {
                lv_obj_t* c = lv_obj_get_child(qs_color_card_, i);
                if (c != col_title) {
                    lv_obj_set_size(c, 18, 18);
                    lv_obj_set_style_radius(c, 9, 0);
                    lv_obj_set_pos(c, 14 + dot_idx * 28, 28);
                    dot_idx++;
                }
            }
        }

        // 3. MÀN HÌNH NGANG: 1 HÀNG 2 Ô (148 x 34)
        // Hàng 1 (y = 136): Trợ lý AI và Ảnh Bìa
        if (qs_ai_btn_) {
            lv_obj_set_size(qs_ai_btn_, 148, 34);
            lv_obj_set_pos(qs_ai_btn_, 8, 136);
        }
        if (qs_wallpaper_btn_) {
            lv_obj_clear_flag(qs_wallpaper_btn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(qs_wallpaper_btn_, 148, 34);
            lv_obj_set_pos(qs_wallpaper_btn_, 164, 136);
        }

        // Hàng 2 (y = 176): Xoay màn hình và Cấu hình WiFi
        if (qs_rotate_btn_) {
            lv_obj_set_size(qs_rotate_btn_, 148, 34);
            lv_obj_set_pos(qs_rotate_btn_, 8, 176);
        }
        if (qs_wifi_btn_) {
            lv_obj_set_size(qs_wifi_btn_, 148, 34);
            lv_obj_set_pos(qs_wifi_btn_, 164, 176);
        }

        // Hàng 3 (y = 216): Khởi động lại (to rõ 20pt, nút to 44px)
        if (qs_restart_btn_) {
            lv_obj_set_size(qs_restart_btn_, 304, 44);
            lv_obj_set_pos(qs_restart_btn_, 8, 216);
        }

        // Hàng 4 (y = 268): Thẻ thông tin hệ thống (chữ to 16_4 đồng bộ)
        if (qs_info_card_) {
            lv_obj_set_size(qs_info_card_, 304, 134);
            lv_obj_set_pos(qs_info_card_, 8, 268);
        }
        if (qs_info_wifi_) lv_obj_set_pos(qs_info_wifi_, 10, 8);
        if (qs_info_ip_) lv_obj_set_pos(qs_info_ip_, 10, 38);
        if (qs_info_temp_) lv_obj_set_pos(qs_info_temp_, 10, 68);
        if (qs_info_sd_) lv_obj_set_pos(qs_info_sd_, 10, 98);
    }
}


void LcdDisplay::UpdateQuickSettingsInfo() {

    if (quick_settings_box_ == nullptr) return;



    // 1. WiFi SSID

    std::string ssid = WifiManager::GetInstance().GetSsid();

    if (ssid.empty()) {

        ssid = "Chưa kết nối";

    }

    char wifi_buf[64];

    snprintf(wifi_buf, sizeof(wifi_buf), "WiFi: %s", ssid.c_str());

    if (qs_info_wifi_) lv_label_set_text(qs_info_wifi_, wifi_buf);



    // 2. IP Address

    std::string ip = WifiManager::GetInstance().GetIpAddress();

    if (ip.empty()) {

        ip = "--";

    }

    char ip_buf[64];

    snprintf(ip_buf, sizeof(ip_buf), "Địa chỉ IP: %s", ip.c_str());

    if (qs_info_ip_) lv_label_set_text(qs_info_ip_, ip_buf);



    // 3. Nhiệt độ chip ESP32

    float temp = 0.0f;

    char temp_buf[48];

    if (Board::GetInstance().GetTemperature(temp)) {

        snprintf(temp_buf, sizeof(temp_buf), "Nhiệt độ chip: %.1f °C", temp);

    } else {

        snprintf(temp_buf, sizeof(temp_buf), "Nhiệt độ chip: --");

    }

    if (qs_info_temp_) lv_label_set_text(qs_info_temp_, temp_buf);



    // 4. Thẻ nhớ MicroSD

    char sd_buf[64];

    if (Board::GetInstance().IsSdCardMounted()) {

        uint64_t cap_mb = Board::GetInstance().GetSdCardCapacityMB();

        if (cap_mb >= 1024) {

            snprintf(sd_buf, sizeof(sd_buf), "Thẻ nhớ: Đã gắn (%.1f GB)", (float)cap_mb / 1024.0f);

        } else {

            snprintf(sd_buf, sizeof(sd_buf), "Thẻ nhớ: Đã gắn (%llu MB)", (unsigned long long)cap_mb);

        }

    } else {

        snprintf(sd_buf, sizeof(sd_buf), "Thẻ nhớ: Chưa gắn");

    }

    if (qs_info_sd_) lv_label_set_text(qs_info_sd_, sd_buf);

}



void LcdDisplay::ApplyTheme() {

    lv_color_t bg_col = is_light_theme_ ? lv_color_hex(0xF8FAFC) : lv_color_black();

    lv_color_t card_bg = is_light_theme_ ? lv_color_hex(0xE2E8F0) : lv_color_hex(0x161D27);

    lv_color_t card_border = is_light_theme_ ? lv_color_hex(0xCBD5E1) : lv_color_hex(0x283548);

    lv_color_t txt_main = is_light_theme_ ? lv_color_hex(0x0F172A) : lv_color_white();



    if (standby_box_) lv_obj_set_style_bg_color(standby_box_, bg_col, 0);

    if (standby_clock_) lv_obj_set_style_text_color(standby_clock_, txt_main, 0);

    if (standby_sec_) lv_obj_set_style_text_color(standby_sec_, txt_main, 0);

    if (standby_weekday_) lv_obj_set_style_text_color(standby_weekday_, txt_main, 0);

    if (standby_date_) lv_obj_set_style_text_color(standby_date_, txt_main, 0);



    if (menu_box_) lv_obj_set_style_bg_color(menu_box_, bg_col, 0);



    if (quick_settings_box_) lv_obj_set_style_bg_color(quick_settings_box_, bg_col, 0);

    if (qs_sliders_card_) {

        lv_obj_set_style_bg_color(qs_sliders_card_, card_bg, 0);

        lv_obj_set_style_border_color(qs_sliders_card_, card_border, 0);

    }

    if (qs_color_card_) {

        lv_obj_set_style_bg_color(qs_color_card_, card_bg, 0);

        lv_obj_set_style_border_color(qs_color_card_, card_border, 0);

    }

    if (qs_info_card_) {

        lv_obj_set_style_bg_color(qs_info_card_, card_bg, 0);

        lv_obj_set_style_border_color(qs_info_card_, card_border, 0);

    }

    if (qs_title_) lv_obj_set_style_text_color(qs_title_, txt_main, 0);



    if (calendar_box_) lv_obj_set_style_bg_color(calendar_box_, bg_col, 0);

    if (cal_today_card_) {

        lv_obj_set_style_bg_color(cal_today_card_, card_bg, 0);

        lv_obj_set_style_border_color(cal_today_card_, card_border, 0);

    }

    if (calendar_title_label_) lv_obj_set_style_text_color(calendar_title_label_, txt_main, 0);

    UpdateCalendarUI();



    if (clock_box_) lv_obj_set_style_bg_color(clock_box_, bg_col, 0);

    if (clock_card_) {

        lv_obj_set_style_bg_color(clock_card_, card_bg, 0);

        lv_obj_set_style_border_color(clock_card_, card_border, 0);

    }

    if (clock_time_label_) lv_obj_set_style_text_color(clock_time_label_, txt_main, 0);



    if (weather_detail_box_) lv_obj_set_style_bg_color(weather_detail_box_, bg_col, 0);

    if (rates_box_) lv_obj_set_style_bg_color(rates_box_, bg_col, 0);

    if (stopwatch_box_) lv_obj_set_style_bg_color(stopwatch_box_, bg_col, 0);

    if (timer_box_) lv_obj_set_style_bg_color(timer_box_, bg_col, 0);

    if (calc_box_) lv_obj_set_style_bg_color(calc_box_, bg_col, 0);

    if (fm_box_) lv_obj_set_style_bg_color(fm_box_, bg_col, 0);

    if (alarm_list_container_) {

        lv_obj_set_style_bg_color(alarm_list_container_, card_bg, 0);

        lv_obj_set_style_border_color(alarm_list_container_, card_border, 0);

    }



    if (calendar_box_) {

        uint32_t cnt = lv_obj_get_child_count(calendar_box_);

        for (uint32_t i = 0; i < cnt; ++i) {

            lv_obj_t* child = lv_obj_get_child(calendar_box_, i);

            int id = (int)(intptr_t)lv_obj_get_user_data(child);

            if (id > 100 && id < 107) {

                lv_obj_set_style_text_color(child, is_light_theme_ ? lv_color_hex(0x475569) : lv_color_hex(0xB0BEC5), 0);

            }

        }

    }



    // Đổi màu các chữ dưới icon trong Menu sang đen ở nền sáng:

    for (int i = 0; i < 12; ++i) {

        if (menu_items_[i]) {

            uint32_t cnt = lv_obj_get_child_count(menu_items_[i]);

            if (cnt >= 2) {

                lv_obj_t* lbl = lv_obj_get_child(menu_items_[i], 1);

                if (lbl) lv_obj_set_style_text_color(lbl, txt_main, 0);

            }

        }

    }

    SetMenuPage(menu_page_, false);



    // Đổi màu tất cả dấu quay về / đóng sang đen ở nền sáng:

    if (calendar_back_btn_ && lv_obj_get_child_count(calendar_back_btn_) > 0) {

        lv_obj_set_style_text_color(lv_obj_get_child(calendar_back_btn_, 0), txt_main, 0);

    }

    if (calendar_prev_btn_ && lv_obj_get_child_count(calendar_prev_btn_) > 0) {

        lv_obj_set_style_bg_color(calendar_prev_btn_, card_bg, 0);

        lv_obj_set_style_text_color(lv_obj_get_child(calendar_prev_btn_, 0), txt_main, 0);

    }

    if (calendar_next_btn_ && lv_obj_get_child_count(calendar_next_btn_) > 0) {

        lv_obj_set_style_bg_color(calendar_next_btn_, card_bg, 0);

        lv_obj_set_style_text_color(lv_obj_get_child(calendar_next_btn_, 0), txt_main, 0);

    }

    if (clock_back_btn_ && lv_obj_get_child_count(clock_back_btn_) > 0) {

        lv_obj_set_style_text_color(lv_obj_get_child(clock_back_btn_, 0), txt_main, 0);

    }

    if (clock_box_ && lv_obj_get_child_count(clock_box_) > 1) {

        lv_obj_set_style_text_color(lv_obj_get_child(clock_box_, 1), txt_main, 0);

    }

    if (sw_back_btn_ && lv_obj_get_child_count(sw_back_btn_) > 0) {

        lv_obj_set_style_text_color(lv_obj_get_child(sw_back_btn_, 0), txt_main, 0);

    }

    if (timer_back_btn_ && lv_obj_get_child_count(timer_back_btn_) > 0) {

        lv_obj_set_style_text_color(lv_obj_get_child(timer_back_btn_, 0), txt_main, 0);

    }

    if (timer_box_ && lv_obj_get_child_count(timer_box_) > 1) {

        lv_obj_set_style_text_color(lv_obj_get_child(timer_box_, 1), txt_main, 0);

    }

    if (rates_back_btn_ && lv_obj_get_child_count(rates_back_btn_) > 0) {

        lv_obj_set_style_text_color(lv_obj_get_child(rates_back_btn_, 0), txt_main, 0);

    }

    if (rates_refresh_btn_ && lv_obj_get_child_count(rates_refresh_btn_) > 0) {

        lv_obj_set_style_text_color(lv_obj_get_child(rates_refresh_btn_, 0), txt_main, 0);

    }

    if (rates_box_ && lv_obj_get_child_count(rates_box_) > 2) {

        lv_obj_set_style_text_color(lv_obj_get_child(rates_box_, 2), txt_main, 0);

    }

    if (weather_detail_back_btn_ && lv_obj_get_child_count(weather_detail_back_btn_) > 0) {

        lv_obj_set_style_text_color(lv_obj_get_child(weather_detail_back_btn_, 0), txt_main, 0);

    }

    if (weather_detail_city_) {

        lv_obj_set_style_text_color(weather_detail_city_, txt_main, 0);

    }

    if (qs_close_btn_ && lv_obj_get_child_count(qs_close_btn_) > 0) {

        lv_obj_set_style_text_color(lv_obj_get_child(qs_close_btn_, 0), txt_main, 0);

    }



    UpdateColorDotsPalette();



    ShowNotification(is_light_theme_ ? "Đã chuyển nền Sáng!" : "Đã chuyển nền Tối!", 1200);

}



static void GetHolidayOrEvent(int s_day, int s_month, int s_year, int l_day, int l_month, int wday, char* out_buf, size_t out_size, bool& is_good_day) {

    const char* holiday = nullptr;



    // Ngày Lễ Dương Lịch

    if (s_day == 1 && s_month == 1) holiday = "Tết Dương Lịch";

    else if (s_day == 14 && s_month == 2) holiday = "Lễ Tình Nhân (Valentine)";

    else if (s_day == 8 && s_month == 3) holiday = "Quốc tế Phụ nữ (8/3)";

    else if (s_day == 26 && s_month == 3) holiday = "Ngày Thành lập Đoàn TNCS";

    else if (s_day == 30 && s_month == 4) holiday = "Giải phóng miền Nam (30/4)";

    else if (s_day == 1 && s_month == 5) holiday = "Quốc tế Lao động (1/5)";

    else if (s_day == 1 && s_month == 6) holiday = "Quốc tế Thiếu nhi (1/6)";

    else if (s_day == 27 && s_month == 7) holiday = "Ngày Thương binh Liệt sĩ";

    else if (s_day == 19 && s_month == 8) holiday = "Cách mạng Tháng Tám";

    else if (s_day == 2 && s_month == 9) holiday = "Quốc khánh Việt Nam (2/9)";

    else if (s_day == 10 && s_month == 10) holiday = "Giải phóng Thủ đô (10/10)";

    else if (s_day == 20 && s_month == 10) holiday = "Ngày Phụ nữ VN (20/10)";

    else if (s_day == 20 && s_month == 11) holiday = "Ngày Nhà giáo VN (20/11)";

    else if (s_day == 22 && s_month == 12) holiday = "Ngày QĐND Việt Nam (22/12)";

    else if (s_day == 24 && s_month == 12) holiday = "Đêm Giáng sinh (Noel)";

    else if (s_day == 25 && s_month == 12) holiday = "Lễ Giáng sinh (Noel)";

    else if (s_day == 31 && s_month == 12) holiday = "Đêm Giao thừa Dương lịch";



    // Ngày Lễ Âm Lịch

    else if (l_day == 23 && l_month == 12) holiday = "Tiễn Ông Táo về trời";

    else if (l_day >= 29 && l_month == 12) holiday = "Đêm Giao thừa Âm lịch";

    else if (l_day == 1 && l_month == 1) holiday = "Mùng 1 Tết Nguyên Đán";

    else if (l_day == 2 && l_month == 1) holiday = "Mùng 2 Tết Nguyên Đán";

    else if (l_day == 3 && l_month == 1) holiday = "Mùng 3 Tết Nguyên Đán";

    else if (l_day == 4 && l_month == 1) holiday = "Mùng 4 Tết Nguyên Đán";

    else if (l_day == 15 && l_month == 1) holiday = "Tết Nguyên Tiêu (Rằm T.1)";

    else if (l_day == 10 && l_month == 3) holiday = "Giỗ Tổ Hùng Vương (10/3 ÂL)";

    else if (l_day == 15 && l_month == 4) holiday = "Lễ Phật Đản (Rằm T.4)";

    else if (l_day == 5 && l_month == 5) holiday = "Tết Đoan Ngọ (5/5 ÂL)";

    else if (l_day == 15 && l_month == 7) holiday = "Vu Lan Báo Hiếu (Rằm T.7)";

    else if (l_day == 15 && l_month == 8) holiday = "Tết Trung Thu (Rằm T.8)";

    else if (l_day == 1 && l_month == 10) holiday = "Tết Trùng Cửu (1/10 ÂL)";

    else if (l_day == 15 && l_month == 10) holiday = "Tết Hạ Nguyên (Rằm T.10)";



    if (holiday != nullptr) {

        snprintf(out_buf, out_size, "Lễ: %s", holiday);

        is_good_day = true;

        return;

    }



    // Không phải ngày lễ -> Tính Ngày Hoàng Đạo / Hắc Đạo và Can Chi ngày

    int jd = VietnameseLunarCalendar::JdFromDate(s_day, s_month, s_year);

    int can_idx = (jd + 9) % 10;

    int chi_idx = (jd + 1) % 12;



    const char* CAN[10] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};

    const char* CHI[12] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};



    struct StarInfo {

        const char* name;

        bool is_hoang_dao;

    };

    static const StarInfo STARS[12] = {

        {"Thanh Long", true},

        {"Minh Đường", true},

        {"Thiên Hình", false},

        {"Chu Tước",   false},

        {"Kim Quỹ",    true},

        {"Kim Đường",  true},

        {"Bạch Hổ",    false},

        {"Ngọc Đường", true},

        {"Thiên Lao",  false},

        {"Huyền Vũ",   false},

        {"Tư Mệnh",    true},

        {"Câu Trận",   false}

    };



    // Khởi sao Thanh Long theo tháng âm lịch

    int start_chi = ((l_month - 1) % 6) * 2;

    int star_idx = (chi_idx - start_chi + 12) % 12;

    const StarInfo& star = STARS[star_idx];

    is_good_day = star.is_hoang_dao;



    if (star.is_hoang_dao) {

        snprintf(out_buf, out_size, "Hoàng Đạo: %s (%s %s)", star.name, CAN[can_idx], CHI[chi_idx]);

    } else {

        snprintf(out_buf, out_size, "Hắc Đạo: %s (%s %s)", star.name, CAN[can_idx], CHI[chi_idx]);

    }

}



void LcdDisplay::CreateCalendarUI() {

    auto screen = lv_screen_active();

    calendar_box_ = lv_obj_create(screen);

    lv_obj_set_size(calendar_box_, width_, height_);

    lv_obj_set_style_bg_color(calendar_box_, lv_color_black(), 0);

    lv_obj_set_style_bg_opa(calendar_box_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(calendar_box_, 0, 0);

    lv_obj_set_style_border_width(calendar_box_, 0, 0);

    lv_obj_set_style_pad_all(calendar_box_, 0, 0);

    lv_obj_clear_flag(calendar_box_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);



    // Back button

    calendar_back_btn_ = lv_button_create(calendar_box_);

    lv_obj_set_size(calendar_back_btn_, 36, 32);

    lv_obj_set_style_bg_opa(calendar_back_btn_, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(calendar_back_btn_, 0, 0);

    lv_obj_set_style_pad_all(calendar_back_btn_, 0, 0);

    lv_obj_set_ext_click_area(calendar_back_btn_, 8);

    lv_obj_t* back_lbl = lv_label_create(calendar_back_btn_);

    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);

    lv_obj_set_style_text_font(back_lbl, &font_material_symbols_30_4, 0);

    lv_obj_set_style_text_color(back_lbl, lv_color_white(), 0);

    lv_obj_align(back_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(calendar_back_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;

        self->ShowMenu();

    }, LV_EVENT_CLICKED, this);



    // Prev month button

    calendar_prev_btn_ = lv_button_create(calendar_box_);

    lv_obj_set_size(calendar_prev_btn_, 34, 30);

    lv_obj_set_style_bg_color(calendar_prev_btn_, lv_color_hex(0x223344), 0);

    lv_obj_set_style_bg_opa(calendar_prev_btn_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(calendar_prev_btn_, 8, 0);

    lv_obj_set_style_border_width(calendar_prev_btn_, 0, 0);

    lv_obj_set_style_pad_all(calendar_prev_btn_, 0, 0);

    lv_obj_set_ext_click_area(calendar_prev_btn_, 10);

    lv_obj_t* prev_lbl = lv_label_create(calendar_prev_btn_);

    lv_label_set_text(prev_lbl, "<");

    lv_obj_set_style_text_font(prev_lbl, &font_noto_sans_basic_20_4, 0);

    lv_obj_set_style_text_color(prev_lbl, lv_color_white(), 0);

    lv_obj_align(prev_lbl, LV_ALIGN_CENTER, 0, -1);

    lv_obj_add_event_cb(calendar_prev_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;

        self->selected_cal_day_ = -1;

        if (self->curr_cal_month_ == 1) {

            self->curr_cal_month_ = 12;

            self->curr_cal_year_--;

        } else {

            self->curr_cal_month_--;

        }

        self->UpdateCalendarUI();

    }, LV_EVENT_CLICKED, this);



    // Next month button

    calendar_next_btn_ = lv_button_create(calendar_box_);

    lv_obj_set_size(calendar_next_btn_, 34, 30);

    lv_obj_set_style_bg_color(calendar_next_btn_, lv_color_hex(0x223344), 0);

    lv_obj_set_style_bg_opa(calendar_next_btn_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(calendar_next_btn_, 8, 0);

    lv_obj_set_style_border_width(calendar_next_btn_, 0, 0);

    lv_obj_set_style_pad_all(calendar_next_btn_, 0, 0);

    lv_obj_set_ext_click_area(calendar_next_btn_, 10);

    lv_obj_t* next_lbl = lv_label_create(calendar_next_btn_);

    lv_label_set_text(next_lbl, ">");

    lv_obj_set_style_text_font(next_lbl, &font_noto_sans_basic_20_4, 0);

    lv_obj_set_style_text_color(next_lbl, lv_color_white(), 0);

    lv_obj_align(next_lbl, LV_ALIGN_CENTER, 0, -1);

    lv_obj_add_event_cb(calendar_next_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;

        self->selected_cal_day_ = -1;

        if (self->curr_cal_month_ == 12) {

            self->curr_cal_month_ = 1;

            self->curr_cal_year_++;

        } else {

            self->curr_cal_month_++;

        }

        self->UpdateCalendarUI();

    }, LV_EVENT_CLICKED, this);



    // Title label

    calendar_title_label_ = lv_label_create(calendar_box_);

    lv_label_set_text(calendar_title_label_, "Tháng 11 / 2026");

    lv_obj_set_style_text_font(calendar_title_label_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(calendar_title_label_, lv_color_white(), 0);

    lv_obj_set_style_pad_all(calendar_title_label_, 0, 0);



    // Today Card

    cal_today_card_ = lv_obj_create(calendar_box_);

    lv_obj_set_style_bg_color(cal_today_card_, lv_color_hex(0x161D27), 0);

    lv_obj_set_style_bg_opa(cal_today_card_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(cal_today_card_, 8, 0);

    lv_obj_set_style_border_width(cal_today_card_, 1, 0);

    lv_obj_set_style_border_color(cal_today_card_, lv_color_hex(0x283548), 0);

    lv_obj_set_style_pad_all(cal_today_card_, 4, 0);

    lv_obj_clear_flag(cal_today_card_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_clear_flag(cal_today_card_, LV_OBJ_FLAG_CLICKABLE);



    cal_today_title_ = lv_label_create(cal_today_card_);

    lv_obj_set_style_text_font(cal_today_title_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(cal_today_title_, lv_color_hex(0x64B5F6), 0);



    cal_today_day_ = lv_label_create(cal_today_card_);

    lv_obj_set_style_text_font(cal_today_day_, &font_noto_sans_basic_30_4, 0);

    lv_obj_set_style_text_color(cal_today_day_, lv_color_white(), 0);



    cal_today_month_year_ = lv_label_create(cal_today_card_);

    lv_obj_set_style_text_font(cal_today_month_year_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(cal_today_month_year_, lv_color_hex(0x60A5FA), 0);



    cal_today_wday_ = lv_label_create(cal_today_card_);

    lv_obj_set_style_text_font(cal_today_wday_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(cal_today_wday_, lv_color_hex(0xFFD54F), 0);



    cal_today_lunar_ = lv_label_create(cal_today_card_);

    lv_obj_set_style_text_font(cal_today_lunar_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(cal_today_lunar_, lv_color_hex(0x81C784), 0);



    cal_today_event_ = lv_label_create(cal_today_card_);

    lv_obj_set_style_text_font(cal_today_event_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(cal_today_event_, lv_color_hex(0xFFE082), 0);

    lv_label_set_long_mode(cal_today_event_, LV_LABEL_LONG_WRAP);



    // DOW headers (CN, T2..T7)

    const char* dow_names[7] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};

    for (int col = 0; col < 7; ++col) {

        lv_obj_t* lbl = lv_label_create(calendar_box_);

        lv_label_set_text(lbl, dow_names[col]);

        lv_obj_set_style_text_font(lbl, &font_noto_sans_basic_14_1, 0);

        if (col == 0) {

            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF5252), 0);

        } else {

            lv_obj_set_style_text_color(lbl, lv_color_hex(0xB0BEC5), 0);

        }

        lv_obj_set_user_data(lbl, (void*)(intptr_t)(100 + col));

    }



    // 42 cells (6 rows x 7 cols)

    for (int i = 0; i < 42; ++i) {

        lv_obj_t* cell = lv_obj_create(calendar_box_);

        calendar_cells_[i].container = cell;

        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);

        lv_obj_set_style_border_width(cell, 0, 0);

        lv_obj_set_style_radius(cell, 4, 0);

        lv_obj_set_style_pad_all(cell, 0, 0);

        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_set_user_data(cell, (void*)(intptr_t)i);

        lv_obj_add_event_cb(cell, [](lv_event_t* e) {

            auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

            int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));

            int day = self->calendar_cells_[idx].day_num;

            if (day <= 0) return;

            self->selected_cal_day_ = day;

            self->UpdateCalendarUI();

        }, LV_EVENT_CLICKED, this);



        lv_obj_t* solar = lv_label_create(cell);

        calendar_cells_[i].solar_label = solar;

        lv_obj_set_style_text_font(solar, &font_noto_sans_basic_14_1, 0); // Lịch dương vừa vặn, không lấn lịch âm

        lv_obj_set_style_text_color(solar, lv_color_white(), 0);

        lv_obj_set_style_pad_all(solar, 0, 0);

        lv_obj_clear_flag(solar, LV_OBJ_FLAG_CLICKABLE);



        lv_obj_t* lunar = lv_label_create(cell);

        calendar_cells_[i].lunar_label = lunar;

#if LV_FONT_MONTSERRAT_10

        lv_obj_set_style_text_font(lunar, &lv_font_montserrat_10, 0);

#elif LV_FONT_MONTSERRAT_12

        lv_obj_set_style_text_font(lunar, &lv_font_montserrat_12, 0);

#else

        lv_obj_set_style_text_font(lunar, &font_noto_sans_basic_14_1, 0);

#endif

        lv_obj_set_style_text_color(lunar, lv_color_hex(0x94A3B8), 0);

        lv_obj_set_style_pad_all(lunar, 0, 0);

        lv_obj_clear_flag(lunar, LV_OBJ_FLAG_CLICKABLE);

    }



    LayoutCalendarWidgets(height_ > width_);

}



void LcdDisplay::LayoutCalendarWidgets(bool is_portrait) {

    if (calendar_box_ == nullptr) return;

    lv_obj_set_size(calendar_box_, width_, height_);



    if (is_portrait) {

        // 240 x 320: Cân đối khoảng cách, ô ngày cao 28px để khung xanh/vàng không che số âm

        if (calendar_back_btn_) lv_obj_set_pos(calendar_back_btn_, 6, 4);

        if (calendar_prev_btn_) {

            lv_obj_set_size(calendar_prev_btn_, 34, 24);

            lv_obj_set_pos(calendar_prev_btn_, 46, 4);

        }

        if (calendar_next_btn_) {

            lv_obj_set_size(calendar_next_btn_, 34, 24);

            lv_obj_set_pos(calendar_next_btn_, 200, 4);

        }



        // Chữ Tháng Năm nằm ở hàng riêng biệt, cao 20px

        if (calendar_title_label_) {

            lv_obj_set_size(calendar_title_label_, 220, 20);

            lv_obj_set_style_text_align(calendar_title_label_, LV_TEXT_ALIGN_CENTER, 0);

            lv_obj_set_pos(calendar_title_label_, 10, 29);

        }



        // Hàng Thứ (CN..T7): cao 14px

        uint32_t cnt = lv_obj_get_child_count(calendar_box_);

        for (uint32_t i = 0; i < cnt; ++i) {

            lv_obj_t* child = lv_obj_get_child(calendar_box_, i);

            int id = (int)(intptr_t)lv_obj_get_user_data(child);

            if (id >= 100 && id < 107) {

                int col = id - 100;

                lv_obj_set_size(child, 33, 14);

                lv_obj_set_style_text_align(child, LV_TEXT_ALIGN_CENTER, 0);

                lv_obj_set_pos(child, 4 + col * 33, 49);

            }

        }



        // 6 hàng ngày tháng (hàng y=65, ô cao 28px: solar y=0, lunar y=14)

        for (int i = 0; i < 42; ++i) {

            if (calendar_cells_[i].container == nullptr) continue;

            int r = i / 7;

            int c = i % 7;

            lv_obj_set_size(calendar_cells_[i].container, 32, 28);

            lv_obj_set_pos(calendar_cells_[i].container, 4 + c * 33, 65 + r * 29);

            if (calendar_cells_[i].solar_label) {

                lv_obj_set_width(calendar_cells_[i].solar_label, 32);

                lv_obj_set_style_text_align(calendar_cells_[i].solar_label, LV_TEXT_ALIGN_CENTER, 0);

                lv_obj_set_pos(calendar_cells_[i].solar_label, 0, 0);

            }

            if (calendar_cells_[i].lunar_label) {

                lv_obj_set_width(calendar_cells_[i].lunar_label, 32);

                lv_obj_set_style_text_align(calendar_cells_[i].lunar_label, LV_TEXT_ALIGN_CENTER, 0);

                lv_obj_set_pos(calendar_cells_[i].lunar_label, 0, 14);

            }

        }



        // Today Card phía dưới ở màn hình dọc: đặt tại y = 242, cao 72px cách đáy màn hình 6px, không bị che mất sự kiện

        if (cal_today_card_) {

            lv_obj_set_size(cal_today_card_, 228, 72);

            lv_obj_set_pos(cal_today_card_, 6, 242);

            if (cal_today_title_) {

                lv_obj_set_style_text_align(cal_today_title_, LV_TEXT_ALIGN_CENTER, 0);

                lv_obj_align(cal_today_title_, LV_ALIGN_TOP_MID, -42, 2);

            }

            if (cal_today_wday_) {

                lv_obj_set_style_text_align(cal_today_wday_, LV_TEXT_ALIGN_CENTER, 0);

                lv_obj_align(cal_today_wday_, LV_ALIGN_TOP_MID, 42, 2);

            }

            if (cal_today_month_year_) {

                lv_obj_set_style_text_align(cal_today_month_year_, LV_TEXT_ALIGN_CENTER, 0);

                lv_obj_align(cal_today_month_year_, LV_ALIGN_TOP_MID, -42, 19);

            }

            if (cal_today_lunar_) {

                lv_obj_set_style_text_align(cal_today_lunar_, LV_TEXT_ALIGN_CENTER, 0);

                lv_obj_align(cal_today_lunar_, LV_ALIGN_TOP_MID, 42, 19);

            }

            if (cal_today_day_) lv_obj_add_flag(cal_today_day_, LV_OBJ_FLAG_HIDDEN);

            if (cal_today_event_) {

                lv_obj_set_width(cal_today_event_, 216);

                lv_obj_set_style_text_align(cal_today_event_, LV_TEXT_ALIGN_CENTER, 0);

                lv_obj_align(cal_today_event_, LV_ALIGN_TOP_MID, 0, 37);

            }

        }

    } else {

        // 320 x 240 (Ngang): Nút chuyển tháng dễ bấm, hàng thứ đủ chân, lịch âm không đè dương

        if (calendar_back_btn_) lv_obj_set_pos(calendar_back_btn_, 6, 4);



        if (cal_today_card_) {

            lv_obj_set_size(cal_today_card_, 102, 196);

            lv_obj_set_pos(cal_today_card_, 6, 38);

            if (cal_today_title_) lv_obj_align(cal_today_title_, LV_ALIGN_TOP_MID, 0, 4);

            if (cal_today_day_) {

                lv_obj_remove_flag(cal_today_day_, LV_OBJ_FLAG_HIDDEN);

                lv_obj_align(cal_today_day_, LV_ALIGN_TOP_MID, 0, 18);

            }

            if (cal_today_month_year_) lv_obj_align(cal_today_month_year_, LV_ALIGN_TOP_MID, 0, 56);

            if (cal_today_wday_) lv_obj_align(cal_today_wday_, LV_ALIGN_TOP_MID, 0, 74);

            if (cal_today_lunar_) lv_obj_align(cal_today_lunar_, LV_ALIGN_TOP_MID, 0, 92);

            if (cal_today_event_) {

                lv_obj_set_width(cal_today_event_, 94);

                lv_obj_set_style_text_align(cal_today_event_, LV_TEXT_ALIGN_CENTER, 0);

                lv_obj_align(cal_today_event_, LV_ALIGN_TOP_MID, 0, 114);

            }

        }



        // Hàng 1 (y = 4): Nút < và > chuyển tháng

        if (calendar_prev_btn_) {

            lv_obj_set_size(calendar_prev_btn_, 34, 22);

            lv_obj_set_pos(calendar_prev_btn_, 114, 4);

        }

        if (calendar_next_btn_) {

            lv_obj_set_size(calendar_next_btn_, 34, 22);

            lv_obj_set_pos(calendar_next_btn_, 276, 4);

        }



        // Hàng 2 (y = 26): Tháng Năm ở hàng riêng biệt rộng rãi 196px

        if (calendar_title_label_) {

            lv_obj_set_size(calendar_title_label_, 196, 18);

            lv_obj_set_style_text_align(calendar_title_label_, LV_TEXT_ALIGN_CENTER, 0);

            lv_obj_set_pos(calendar_title_label_, 114, 26);

        }



        // Hàng 3 (y = 44): Hàng chữ Thứ cao 14px

        uint32_t cnt = lv_obj_get_child_count(calendar_box_);

        for (uint32_t i = 0; i < cnt; ++i) {

            lv_obj_t* child = lv_obj_get_child(calendar_box_, i);

            int id = (int)(intptr_t)lv_obj_get_user_data(child);

            if (id >= 100 && id < 107) {

                int col = id - 100;

                lv_obj_set_size(child, 28, 14);

                lv_obj_set_style_text_align(child, LV_TEXT_ALIGN_CENTER, 0);

                lv_obj_set_pos(child, 114 + col * 28, 44);

            }

        }



        // Hàng 4..9 (y = 58..231): 6 hàng ngày tháng cao 28px, solar y=0, lunar y=14

        for (int i = 0; i < 42; ++i) {

            if (calendar_cells_[i].container == nullptr) continue;

            int r = i / 7;

            int c = i % 7;

            lv_obj_set_size(calendar_cells_[i].container, 28, 28);

            lv_obj_set_pos(calendar_cells_[i].container, 114 + c * 28, 58 + r * 29);

            if (calendar_cells_[i].solar_label) {

                lv_obj_set_width(calendar_cells_[i].solar_label, 28);

                lv_obj_set_style_text_align(calendar_cells_[i].solar_label, LV_TEXT_ALIGN_CENTER, 0);

                lv_obj_set_pos(calendar_cells_[i].solar_label, 0, 0);

            }

            if (calendar_cells_[i].lunar_label) {

                lv_obj_set_width(calendar_cells_[i].lunar_label, 28);

                lv_obj_set_style_text_align(calendar_cells_[i].lunar_label, LV_TEXT_ALIGN_CENTER, 0);

                lv_obj_set_pos(calendar_cells_[i].lunar_label, 0, 14);

            }

        }

    }

}



void LcdDisplay::UpdateCalendarUI() {

    if (calendar_box_ == nullptr) return;



    char title_buf[32];

    snprintf(title_buf, sizeof(title_buf), "Tháng %d / %d", curr_cal_month_, curr_cal_year_);

    if (calendar_title_label_ != nullptr) {

        lv_label_set_text(calendar_title_label_, title_buf);

    }



    time_t raw_time = time(nullptr);

    struct tm* tm_now = localtime(&raw_time);

    int today_day = (tm_now != nullptr) ? tm_now->tm_mday : 7;

    int today_month = (tm_now != nullptr) ? (tm_now->tm_mon + 1) : 11;

    int today_year = (tm_now != nullptr) ? (tm_now->tm_year + 1900) : 2026;



    int show_day = (selected_cal_day_ > 0) ? selected_cal_day_ : today_day;



    struct tm st = {};

    st.tm_year = curr_cal_year_ - 1900;

    st.tm_mon = curr_cal_month_ - 1;

    st.tm_mday = show_day;

    mktime(&st);

    int show_wday = st.tm_wday;



    const char* full_wdays[] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};

    LunarDate lunar_show = VietnameseLunarCalendar::SolarToLunar(show_day, curr_cal_month_, curr_cal_year_, 7.0);

    char event_buf[64];

    bool is_good_day = true;

    GetHolidayOrEvent(show_day, curr_cal_month_, curr_cal_year_, lunar_show.day, lunar_show.month, show_wday, event_buf, sizeof(event_buf), is_good_day);



    bool is_current_today = (show_day == today_day && curr_cal_month_ == today_month && curr_cal_year_ == today_year);



    if (cal_today_title_) {

        lv_label_set_text(cal_today_title_, is_current_today ? "HÔM NAY" : "ĐÃ CHỌN");

        lv_obj_set_style_text_color(cal_today_title_, is_light_theme_ ? lv_color_hex(0x1D4ED8) : lv_color_hex(0x64B5F6), 0);

    }

    if (cal_today_day_) {

        char sday[8];

        snprintf(sday, sizeof(sday), "%02d", show_day);

        lv_label_set_text(cal_today_day_, sday);

        lv_obj_set_style_text_color(cal_today_day_, is_light_theme_ ? lv_color_hex(0x0F172A) : lv_color_white(), 0);

    }

    if (cal_today_month_year_) {

        char mybuf[32];

        snprintf(mybuf, sizeof(mybuf), "Tháng %d, %d", curr_cal_month_, curr_cal_year_);

        lv_label_set_text(cal_today_month_year_, mybuf);

        lv_obj_set_style_text_color(cal_today_month_year_, is_light_theme_ ? lv_color_hex(0x2563EB) : lv_color_hex(0x60A5FA), 0);

    }

    if (cal_today_wday_) {

        lv_label_set_text(cal_today_wday_, full_wdays[show_wday]);

        lv_obj_set_style_text_color(cal_today_wday_, is_light_theme_ ? lv_color_hex(0xB45309) : lv_color_hex(0xFFD54F), 0);

    }

    if (cal_today_lunar_) {

        char lstr[32];

        snprintf(lstr, sizeof(lstr), "Âm: %d/%d", lunar_show.day, lunar_show.month);

        lv_label_set_text(cal_today_lunar_, lstr);

        lv_obj_set_style_text_color(cal_today_lunar_, is_light_theme_ ? lv_color_hex(0x15803D) : lv_color_hex(0x81C784), 0);

    }

    if (cal_today_event_) {

        lv_label_set_text(cal_today_event_, event_buf);

        if (is_good_day) {

            lv_obj_set_style_text_color(cal_today_event_, is_light_theme_ ? lv_color_hex(0xB45309) : lv_color_hex(0xFFE082), 0);

        } else {

            lv_obj_set_style_text_color(cal_today_event_, is_light_theme_ ? lv_color_hex(0x64748B) : lv_color_hex(0x94A3B8), 0);

        }

    }



    struct tm t = {};

    t.tm_year = curr_cal_year_ - 1900;

    t.tm_mon = curr_cal_month_ - 1;

    t.tm_mday = 1;

    mktime(&t);

    int first_wday = t.tm_wday;



    int days_in_month = 31;

    if (curr_cal_month_ == 4 || curr_cal_month_ == 6 || curr_cal_month_ == 9 || curr_cal_month_ == 11) {

        days_in_month = 30;

    } else if (curr_cal_month_ == 2) {

        bool is_leap = (curr_cal_year_ % 4 == 0 && curr_cal_year_ % 100 != 0) || (curr_cal_year_ % 400 == 0);

        days_in_month = is_leap ? 29 : 28;

    }



    for (int cell = 0; cell < 42; ++cell) {

        auto& c = calendar_cells_[cell];

        if (c.container == nullptr) continue;



        int day_num = cell - first_wday + 1;

        c.day_num = day_num;

        if (day_num >= 1 && day_num <= days_in_month) {

            lv_obj_remove_flag(c.container, LV_OBJ_FLAG_HIDDEN);



            char sbuf[8];

            snprintf(sbuf, sizeof(sbuf), "%d", day_num);

            lv_label_set_text(c.solar_label, sbuf);



            LunarDate lunar = VietnameseLunarCalendar::SolarToLunar(day_num, curr_cal_month_, curr_cal_year_);

            char lbuf[8];

            snprintf(lbuf, sizeof(lbuf), "%d", lunar.day);

            lv_label_set_text(c.lunar_label, lbuf);



            bool is_today = (day_num == today_day && curr_cal_month_ == today_month && curr_cal_year_ == today_year);

            bool is_selected = (day_num == show_day);

            bool is_sunday = ((cell % 7) == 0);



            if (is_today) {

                lv_obj_set_style_bg_color(c.container, lv_color_hex(0x2563EB), 0);

                lv_obj_set_style_bg_opa(c.container, LV_OPA_COVER, 0);

                lv_obj_set_style_border_width(c.container, 1, 0);

                lv_obj_set_style_border_color(c.container, lv_color_hex(0xFFD700), 0);

                lv_obj_set_style_text_color(c.solar_label, lv_color_white(), 0);

                lv_obj_set_style_text_color(c.lunar_label, lv_color_hex(0xFFE082), 0);

            } else if (is_selected) {

                lv_obj_set_style_bg_color(c.container, is_light_theme_ ? lv_color_hex(0xCBD5E1) : lv_color_hex(0x1E293B), 0);

                lv_obj_set_style_bg_opa(c.container, LV_OPA_COVER, 0);

                lv_obj_set_style_border_width(c.container, 1, 0);

                lv_obj_set_style_border_color(c.container, lv_color_hex(0x0284C7), 0);

                lv_obj_set_style_text_color(c.solar_label, is_light_theme_ ? lv_color_hex(0x0284C7) : lv_color_hex(0x00E5FF), 0);

                lv_obj_set_style_text_color(c.lunar_label, is_light_theme_ ? lv_color_hex(0x0F172A) : lv_color_white(), 0);

            } else {

                lv_obj_set_style_bg_opa(c.container, LV_OPA_TRANSP, 0);

                lv_obj_set_style_border_width(c.container, 0, 0);

                if (is_sunday) {

                    lv_obj_set_style_text_color(c.solar_label, lv_color_hex(0xDC2626), 0);

                } else {

                    lv_obj_set_style_text_color(c.solar_label, is_light_theme_ ? lv_color_hex(0x0F172A) : lv_color_white(), 0);

                }

                if (lunar.day == 1) {

                    lv_obj_set_style_text_color(c.lunar_label, is_light_theme_ ? lv_color_hex(0xD97706) : lv_color_hex(0xFFD54F), 0);

                } else {

                    lv_obj_set_style_text_color(c.lunar_label, is_light_theme_ ? lv_color_hex(0x64748B) : lv_color_hex(0x90A4AE), 0);

                }

            }

        } else {

            c.day_num = 0;

            lv_obj_add_flag(c.container, LV_OBJ_FLAG_HIDDEN);

        }

    }

}



void LcdDisplay::CreateWeatherDetailUI() {

    auto screen = lv_screen_active();

    weather_detail_box_ = lv_obj_create(screen);

    lv_obj_set_size(weather_detail_box_, width_, height_);

    lv_obj_set_style_bg_color(weather_detail_box_, lv_color_hex(0x0c1017), 0);

    lv_obj_set_style_bg_opa(weather_detail_box_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(weather_detail_box_, 0, 0);

    lv_obj_set_style_border_width(weather_detail_box_, 0, 0);

    lv_obj_set_style_pad_all(weather_detail_box_, 0, 0);

    lv_obj_clear_flag(weather_detail_box_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);



    // Back button

    weather_detail_back_btn_ = lv_button_create(weather_detail_box_);

    lv_obj_t* back_btn = weather_detail_back_btn_;

    lv_obj_set_size(back_btn, 36, 32);

    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(back_btn, 0, 0);

    lv_obj_set_style_pad_all(back_btn, 0, 0);

    lv_obj_set_ext_click_area(back_btn, 8);

    lv_obj_t* back_lbl = lv_label_create(back_btn);

    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);

    lv_obj_set_style_text_font(back_lbl, &font_material_symbols_30_4, 0);

    lv_obj_set_style_text_color(back_lbl, lv_color_white(), 0);

    lv_obj_align(back_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;

        self->ShowMenu();

    }, LV_EVENT_CLICKED, this);



    // City label

    weather_detail_city_ = lv_label_create(weather_detail_box_);

    lv_label_set_text(weather_detail_city_, "Đang tải...");

    lv_obj_set_style_text_font(weather_detail_city_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(weather_detail_city_, lv_color_white(), 0);



    // Hero card

    lv_obj_t* hero = lv_obj_create(weather_detail_box_);

    lv_obj_set_style_bg_color(hero, lv_color_hex(0x151e2e), 0);

    lv_obj_set_style_bg_opa(hero, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(hero, 10, 0);

    lv_obj_set_style_border_width(hero, 0, 0);

    lv_obj_set_style_pad_all(hero, 0, 0);

    lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_user_data(hero, (void*)(intptr_t)200);



    weather_detail_icon_ = lv_image_create(hero);

    lv_image_set_src(weather_detail_icon_, &weather_icon_sun_cloud_48_dsc);



    weather_detail_temp_ = lv_label_create(hero);

    lv_label_set_text(weather_detail_temp_, "--°C");

#if LV_FONT_MONTSERRAT_40

    lv_obj_set_style_text_font(weather_detail_temp_, &lv_font_montserrat_40, 0);

#else

    lv_obj_set_style_text_font(weather_detail_temp_, &font_noto_sans_basic_30_4, 0);

#endif

    lv_obj_set_style_text_color(weather_detail_temp_, lv_color_white(), 0);



    weather_detail_condition_ = lv_label_create(hero);

    lv_label_set_text(weather_detail_condition_, "--");

    lv_obj_set_style_text_font(weather_detail_condition_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(weather_detail_condition_, lv_color_hex(0x64B5F6), 0);



    // 2 hàng riêng biệt: Độ ẩm và Gió để không bao giờ bị tràn mất chữ

    weather_detail_humidity_ = lv_label_create(hero);

    lv_label_set_text(weather_detail_humidity_, "Độ ẩm: --%");

    lv_obj_set_style_text_font(weather_detail_humidity_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(weather_detail_humidity_, lv_color_hex(0xB0BEC5), 0);



    weather_detail_wind_ = lv_label_create(hero);

    lv_label_set_text(weather_detail_wind_, "Gió: -- km/h");

    lv_obj_set_style_text_font(weather_detail_wind_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(weather_detail_wind_, lv_color_hex(0xB0BEC5), 0);



    // 4 daily forecast rows

    for (int i = 0; i < 4; ++i) {

        lv_obj_t* row = lv_obj_create(weather_detail_box_);

        weather_detail_daily_[i].row = row;

        lv_obj_set_style_bg_color(row, lv_color_hex(0x111823), 0);

        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);

        lv_obj_set_style_radius(row, 6, 0);

        lv_obj_set_style_border_width(row, 0, 0);

        lv_obj_set_style_pad_all(row, 0, 0);

        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);



        lv_obj_t* day = lv_label_create(row);

        weather_detail_daily_[i].day_label = day;

        lv_label_set_text(day, "--");

        lv_obj_set_style_text_font(day, &font_noto_sans_basic_14_1, 0);

        lv_obj_set_style_text_color(day, lv_color_white(), 0);



        lv_obj_t* icon = lv_image_create(row);

        weather_detail_daily_[i].icon = icon;

        lv_image_set_src(icon, &weather_icon_cloud_24_dsc);



        lv_obj_t* temp = lv_label_create(row);

        weather_detail_daily_[i].temp_label = temp;

        lv_label_set_text(temp, "--°C");

        lv_obj_set_style_text_font(temp, &font_noto_sans_basic_14_1, 0);

        lv_obj_set_style_text_color(temp, lv_color_hex(0xFFA726), 0);

    }



    LayoutWeatherDetailWidgets(height_ > width_);

}



void LcdDisplay::LayoutWeatherDetailWidgets(bool is_portrait) {

    if (weather_detail_box_ == nullptr) return;

    lv_obj_set_size(weather_detail_box_, width_, height_);



    if (weather_detail_city_ != nullptr) {

        lv_obj_align(weather_detail_city_, LV_ALIGN_TOP_MID, 0, is_portrait ? 16 : 10);

    }



    lv_obj_t* hero = nullptr;

    uint32_t cnt = lv_obj_get_child_count(weather_detail_box_);

    for (uint32_t i = 0; i < cnt; ++i) {

        lv_obj_t* child = lv_obj_get_child(weather_detail_box_, i);

        int tag = (int)(intptr_t)lv_obj_get_user_data(child);

        if (tag == 200) hero = child;

    }



    if (is_portrait) {

        // 240 x 320: Dịch hero card xuống y = 46, bỏ chữ tiêu đề dự báo

        if (hero != nullptr) {

            lv_obj_set_size(hero, 224, 114);

            lv_obj_set_pos(hero, 8, 46);

        }

        if (weather_detail_icon_ != nullptr) lv_obj_set_pos(weather_detail_icon_, 12, 10);

        if (weather_detail_temp_ != nullptr) lv_obj_set_pos(weather_detail_temp_, 74, 6);

        if (weather_detail_condition_ != nullptr) lv_obj_set_pos(weather_detail_condition_, 74, 46);

        if (weather_detail_humidity_ != nullptr) lv_obj_set_pos(weather_detail_humidity_, 12, 68);

        if (weather_detail_wind_ != nullptr) lv_obj_set_pos(weather_detail_wind_, 12, 88);



        for (int i = 0; i < 4; ++i) {

            auto& d = weather_detail_daily_[i];

            if (d.row == nullptr) continue;

            lv_obj_set_size(d.row, 224, 32);

            lv_obj_set_pos(d.row, 8, 168 + i * 36);

            if (d.day_label != nullptr) lv_obj_set_pos(d.day_label, 10, 6);

            if (d.icon != nullptr) lv_obj_set_pos(d.icon, 108, 4);

            if (d.temp_label != nullptr) lv_obj_set_pos(d.temp_label, 162, 6);

        }

    } else {

        // 320 x 240

        if (hero != nullptr) {

            lv_obj_set_size(hero, 144, 194);

            lv_obj_set_pos(hero, 8, 36);

        }

        if (weather_detail_icon_ != nullptr) lv_obj_align(weather_detail_icon_, LV_ALIGN_TOP_MID, 0, 6);

        if (weather_detail_temp_ != nullptr) lv_obj_align(weather_detail_temp_, LV_ALIGN_TOP_MID, 0, 54);

        if (weather_detail_condition_ != nullptr) lv_obj_align(weather_detail_condition_, LV_ALIGN_TOP_MID, 0, 96);

        if (weather_detail_humidity_ != nullptr) lv_obj_align(weather_detail_humidity_, LV_ALIGN_TOP_MID, 0, 132);

        if (weather_detail_wind_ != nullptr) lv_obj_align(weather_detail_wind_, LV_ALIGN_TOP_MID, 0, 156);



        for (int i = 0; i < 4; ++i) {

            auto& d = weather_detail_daily_[i];

            if (d.row == nullptr) continue;

            lv_obj_set_size(d.row, 152, 40);

            lv_obj_set_pos(d.row, 160, 36 + i * 46);

            if (d.day_label != nullptr) lv_obj_set_pos(d.day_label, 8, 10);

            if (d.icon != nullptr) lv_obj_set_pos(d.icon, 68, 8);

            if (d.temp_label != nullptr) lv_obj_set_pos(d.temp_label, 105, 10);

        }

    }

}



void LcdDisplay::UpdateWeatherDetailUI() {

    if (weather_detail_box_ == nullptr) return;

    const auto& info = WeatherService::GetInstance().GetWeatherInfo();



    if (weather_detail_city_ != nullptr) {

        lv_label_set_text(weather_detail_city_, info.city.c_str());

    }

    if (weather_detail_temp_ != nullptr) {

        char tbuf[16];

        snprintf(tbuf, sizeof(tbuf), "%.0f°C", info.temp);

        lv_label_set_text(weather_detail_temp_, tbuf);

    }

    if (weather_detail_condition_ != nullptr) {

        lv_label_set_text(weather_detail_condition_, info.condition.c_str());

    }

    if (weather_detail_humidity_ != nullptr) {

        char hbuf[32];

        snprintf(hbuf, sizeof(hbuf), "Độ ẩm: %d%%", info.humidity);

        lv_label_set_text(weather_detail_humidity_, hbuf);

    }

    if (weather_detail_wind_ != nullptr) {

        char wbuf[32];

        snprintf(wbuf, sizeof(wbuf), "Gió: %.0f km/h", info.wind_speed);

        lv_label_set_text(weather_detail_wind_, wbuf);

    }

    if (weather_detail_icon_ != nullptr) {

        const lv_image_dsc_t* dsc48 = &weather_icon_sun_cloud_48_dsc;

        switch (info.icon_type) {

            case 0: dsc48 = &weather_icon_sun_48_dsc; break;

            case 1: dsc48 = &weather_icon_sun_cloud_48_dsc; break;

            case 2: dsc48 = &weather_icon_cloud_48_dsc; break;

            case 3: dsc48 = &weather_icon_rain_48_dsc; break;

            case 4: dsc48 = &weather_icon_thunder_48_dsc; break;

            case 5: dsc48 = &weather_icon_moon_48_dsc; break;

            case 6: dsc48 = &weather_icon_moon_cloud_48_dsc; break;

        }

        lv_image_set_src(weather_detail_icon_, dsc48);

    }



    for (size_t i = 0; i < 4; ++i) {

        if (i < info.daily.size()) {

            if (weather_detail_daily_[i].row != nullptr) {

                lv_obj_remove_flag(weather_detail_daily_[i].row, LV_OBJ_FLAG_HIDDEN);

            }

            if (weather_detail_daily_[i].day_label != nullptr) {

                lv_label_set_text(weather_detail_daily_[i].day_label, info.daily[i].day_str.c_str());

            }

            if (weather_detail_daily_[i].icon != nullptr) {

                const lv_image_dsc_t* dsc24 = &weather_icon_cloud_24_dsc;

                switch (info.daily[i].icon_type) {

                    case 0: dsc24 = &weather_icon_sun_24_dsc; break;

                    case 1: dsc24 = &weather_icon_sun_cloud_24_dsc; break;

                    case 2: dsc24 = &weather_icon_cloud_24_dsc; break;

                    case 3: dsc24 = &weather_icon_rain_24_dsc; break;

                    case 4: dsc24 = &weather_icon_thunder_24_dsc; break;

                    case 5: dsc24 = &weather_icon_moon_24_dsc; break;

                    case 6: dsc24 = &weather_icon_moon_cloud_24_dsc; break;

                }

                lv_image_set_src(weather_detail_daily_[i].icon, dsc24);

            }

            if (weather_detail_daily_[i].temp_label != nullptr) {

                char tbuf[16];

                snprintf(tbuf, sizeof(tbuf), "%.0f°C", info.daily[i].temp_max);

                lv_label_set_text(weather_detail_daily_[i].temp_label, tbuf);

            }

        } else {

            if (weather_detail_daily_[i].row != nullptr) {

                lv_obj_add_flag(weather_detail_daily_[i].row, LV_OBJ_FLAG_HIDDEN);

            }

        }

    }

}



// ======================== CLOCK SCREEN ========================

void LcdDisplay::CreateClockUI() {

    auto screen = lv_screen_active();

    clock_box_ = lv_obj_create(screen);

    lv_obj_set_size(clock_box_, width_, height_);

    lv_obj_set_style_bg_color(clock_box_, lv_color_black(), 0);

    lv_obj_set_style_bg_opa(clock_box_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(clock_box_, 0, 0);

    lv_obj_set_style_border_width(clock_box_, 0, 0);

    lv_obj_set_style_pad_all(clock_box_, 0, 0);

    lv_obj_clear_flag(clock_box_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);



    // Back button

    clock_back_btn_ = lv_button_create(clock_box_);

    lv_obj_set_size(clock_back_btn_, 36, 32);

    lv_obj_set_style_bg_opa(clock_back_btn_, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(clock_back_btn_, 0, 0);

    lv_obj_set_style_pad_all(clock_back_btn_, 0, 0);

    lv_obj_set_ext_click_area(clock_back_btn_, 8);

    lv_obj_t* back_lbl = lv_label_create(clock_back_btn_);

    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);

    lv_obj_set_style_text_font(back_lbl, &font_material_symbols_30_4, 0);

    lv_obj_set_style_text_color(back_lbl, lv_color_white(), 0);

    lv_obj_align(back_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(clock_back_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;

        self->ShowMenu();

    }, LV_EVENT_CLICKED, this);



    // Title

    lv_obj_t* title_lbl = lv_label_create(clock_box_);

    lv_label_set_text(title_lbl, "ĐỒNG HỒ");

    lv_obj_set_style_text_font(title_lbl, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);

    lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 14);



    // Clock Card

    clock_card_ = lv_obj_create(clock_box_);

    lv_obj_set_style_bg_color(clock_card_, lv_color_hex(0x161D27), 0);

    lv_obj_set_style_bg_opa(clock_card_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(clock_card_, 10, 0);

    lv_obj_set_style_border_width(clock_card_, 1, 0);

    lv_obj_set_style_border_color(clock_card_, lv_color_hex(0x283548), 0);

    lv_obj_set_style_pad_all(clock_card_, 0, 0);

    lv_obj_clear_flag(clock_card_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_clear_flag(clock_card_, LV_OBJ_FLAG_CLICKABLE);



    clock_time_label_ = lv_label_create(clock_card_);

    lv_label_set_text(clock_time_label_, "--:--:--");

    lv_obj_set_style_text_font(clock_time_label_, &font_noto_sans_basic_30_4, 0);

    lv_obj_set_style_text_color(clock_time_label_, lv_color_white(), 0);



    clock_date_label_ = lv_label_create(clock_card_);

    lv_label_set_text(clock_date_label_, "--/--/----");

    lv_obj_set_style_text_font(clock_date_label_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(clock_date_label_, lv_color_hex(0x64B5F6), 0);



    clock_sub_label_ = lv_label_create(clock_card_);

    lv_label_set_text(clock_sub_label_, "Việt Nam (GMT+7)");

    lv_obj_set_style_text_font(clock_sub_label_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(clock_sub_label_, lv_color_hex(0x90A4AE), 0);



    // Button Bấm giờ (Stopwatch & Countdown)

    clock_sw_btn_ = lv_button_create(clock_box_);

    lv_obj_set_style_bg_color(clock_sw_btn_, lv_color_hex(0x161D27), 0);

    lv_obj_set_style_bg_opa(clock_sw_btn_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(clock_sw_btn_, 8, 0);

    lv_obj_set_style_border_width(clock_sw_btn_, 1, 0);

    lv_obj_set_style_border_color(clock_sw_btn_, lv_color_hex(0x00E5FF), 0);

    lv_obj_set_style_pad_all(clock_sw_btn_, 0, 0);

    lv_obj_t* sw_ico = lv_label_create(clock_sw_btn_);

    lv_label_set_text(sw_ico, MATERIAL_SYMBOLS_WATCH);

    lv_obj_set_style_text_font(sw_ico, &font_material_symbols_30_4, 0);

    lv_obj_set_style_text_color(sw_ico, lv_color_hex(0x00E5FF), 0);

    lv_obj_t* sw_txt = lv_label_create(clock_sw_btn_);

    lv_label_set_text(sw_txt, "Bấm giờ");

    lv_obj_set_style_text_font(sw_txt, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(sw_txt, lv_color_white(), 0);

    lv_obj_add_event_cb(clock_sw_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;

        self->ShowStopwatch();

    }, LV_EVENT_CLICKED, this);



    // Button Hẹn giờ (Alarm Clock)

    clock_timer_btn_ = lv_button_create(clock_box_);

    lv_obj_set_style_bg_color(clock_timer_btn_, lv_color_hex(0x161D27), 0);

    lv_obj_set_style_bg_opa(clock_timer_btn_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(clock_timer_btn_, 8, 0);

    lv_obj_set_style_border_width(clock_timer_btn_, 1, 0);

    lv_obj_set_style_border_color(clock_timer_btn_, lv_color_hex(0xFF9100), 0);

    lv_obj_set_style_pad_all(clock_timer_btn_, 0, 0);

    lv_obj_t* tm_ico = lv_label_create(clock_timer_btn_);

    lv_label_set_text(tm_ico, MATERIAL_SYMBOLS_ALARM);

    lv_obj_set_style_text_font(tm_ico, &font_material_symbols_30_4, 0);

    lv_obj_set_style_text_color(tm_ico, lv_color_hex(0xFF9100), 0);

    lv_obj_t* tm_txt = lv_label_create(clock_timer_btn_);

    lv_label_set_text(tm_txt, "Báo thức");

    lv_obj_set_style_text_font(tm_txt, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(tm_txt, lv_color_white(), 0);

    lv_obj_add_event_cb(clock_timer_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;

        self->ShowTimer();

    }, LV_EVENT_CLICKED, this);



    LayoutClockWidgets(height_ > width_);

}



void LcdDisplay::LayoutClockWidgets(bool is_portrait) {

    if (clock_box_ == nullptr) return;

    lv_obj_set_size(clock_box_, width_, height_);



    if (is_portrait) {

        // 240 x 320: Thẻ đồng hồ chính xích xuống, không che nút quay về

        if (clock_back_btn_) lv_obj_set_pos(clock_back_btn_, 6, 10);

        if (clock_card_) {

            lv_obj_set_size(clock_card_, 224, 118);

            lv_obj_set_pos(clock_card_, 8, 50);

            if (clock_time_label_) lv_obj_align(clock_time_label_, LV_ALIGN_TOP_MID, 0, 10);

            if (clock_date_label_) lv_obj_align(clock_date_label_, LV_ALIGN_TOP_MID, 0, 62);

            if (clock_sub_label_) lv_obj_align(clock_sub_label_, LV_ALIGN_TOP_MID, 0, 86);

        }

        // Các nút Bấm giờ & Hẹn giờ phía dưới

        if (clock_sw_btn_) {

            lv_obj_set_size(clock_sw_btn_, 224, 52);

            lv_obj_set_pos(clock_sw_btn_, 8, 180);

            lv_obj_t* ico = lv_obj_get_child(clock_sw_btn_, 0);

            lv_obj_t* txt = lv_obj_get_child(clock_sw_btn_, 1);

            if (ico) lv_obj_align(ico, LV_ALIGN_LEFT_MID, 16, 0);

            if (txt) lv_obj_align(txt, LV_ALIGN_LEFT_MID, 56, 0);

        }

        if (clock_timer_btn_) {

            lv_obj_set_size(clock_timer_btn_, 224, 52);

            lv_obj_set_pos(clock_timer_btn_, 8, 244);

            lv_obj_t* ico = lv_obj_get_child(clock_timer_btn_, 0);

            lv_obj_t* txt = lv_obj_get_child(clock_timer_btn_, 1);

            if (ico) lv_obj_align(ico, LV_ALIGN_LEFT_MID, 16, 0);

            if (txt) lv_obj_align(txt, LV_ALIGN_LEFT_MID, 56, 0);

        }

    } else {

        // 320 x 240: Thẻ đồng hồ hạ thấp xuống y = 46, hoàn toàn không che nút quay về ở y = 8, 2 nút dưới to đẹp tận dụng đáy màn hình

        if (clock_back_btn_) lv_obj_set_pos(clock_back_btn_, 6, 8);

        if (clock_card_) {

            lv_obj_set_size(clock_card_, 304, 98);

            lv_obj_set_pos(clock_card_, 8, 46);

            if (clock_time_label_) lv_obj_align(clock_time_label_, LV_ALIGN_TOP_MID, 0, 8);

            if (clock_date_label_) lv_obj_align(clock_date_label_, LV_ALIGN_TOP_MID, 0, 50);

            if (clock_sub_label_) lv_obj_align(clock_sub_label_, LV_ALIGN_TOP_MID, 0, 72);

        }

        if (clock_sw_btn_) {

            lv_obj_set_size(clock_sw_btn_, 148, 66);

            lv_obj_set_pos(clock_sw_btn_, 8, 154);

            lv_obj_t* ico = lv_obj_get_child(clock_sw_btn_, 0);

            lv_obj_t* txt = lv_obj_get_child(clock_sw_btn_, 1);

            if (ico) lv_obj_align(ico, LV_ALIGN_LEFT_MID, 12, 0);

            if (txt) lv_obj_align(txt, LV_ALIGN_LEFT_MID, 48, 0);

        }

        if (clock_timer_btn_) {

            lv_obj_set_size(clock_timer_btn_, 148, 66);

            lv_obj_set_pos(clock_timer_btn_, 164, 154);

            lv_obj_t* ico = lv_obj_get_child(clock_timer_btn_, 0);

            lv_obj_t* txt = lv_obj_get_child(clock_timer_btn_, 1);

            if (ico) lv_obj_align(ico, LV_ALIGN_LEFT_MID, 12, 0);

            if (txt) lv_obj_align(txt, LV_ALIGN_LEFT_MID, 48, 0);

        }

    }

}



void LcdDisplay::UpdateClockUI() {

    if (clock_box_ == nullptr) return;

    time_t raw_time = time(nullptr);

    struct tm* tm_now = localtime(&raw_time);

    if (tm_now == nullptr) return;



    char time_str[16];

    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);

    if (clock_time_label_) lv_label_set_text(clock_time_label_, time_str);



    const char* wdays[] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};

    char date_str[64];

    snprintf(date_str, sizeof(date_str), "%s, %02d/%02d/%04d", wdays[tm_now->tm_wday], tm_now->tm_mday, tm_now->tm_mon + 1, tm_now->tm_year + 1900);

    if (clock_date_label_) lv_label_set_text(clock_date_label_, date_str);



    // Kiểm tra tất cả báo thức đã cài đặt trong danh sách

    if (tm_now != nullptr) {

        int wday = tm_now->tm_wday; // 0=CN, 1=T2..6=T7

        for (auto& alm : alarms_) {

            if (alm.enabled) {

                bool day_match = (alm.days_mask & (1 << wday)) != 0;

                if (day_match && tm_now->tm_hour == alm.hour && tm_now->tm_min == alm.min) {

                    if (!alm.triggered_today) {

                        alm.triggered_today = true;

                        char msg[64];

                        snprintf(msg, sizeof(msg), "BÁO THỨC %02d:%02d: ĐÃ ĐẾN GIỜ!", alm.hour, alm.min);

                        ShowNotification(msg, 6000);

                        Application::GetInstance().GetAudioService().PlaySound(Lang::Sounds::OGG_EXCLAMATION);

                    }

                } else if (tm_now->tm_min != alm.min) {

                    alm.triggered_today = false;

                }

            }

        }

    }

}



// ======================== STOPWATCH & COUNTDOWN SCREEN ========================

void LcdDisplay::CreateStopwatchUI() {

    auto screen = lv_screen_active();

    stopwatch_box_ = lv_obj_create(screen);

    lv_obj_set_size(stopwatch_box_, width_, height_);

    lv_obj_set_style_bg_color(stopwatch_box_, lv_color_hex(0x0f172a), 0);

    lv_obj_set_style_bg_opa(stopwatch_box_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(stopwatch_box_, 0, 0);

    lv_obj_set_style_border_width(stopwatch_box_, 0, 0);

    lv_obj_set_style_pad_all(stopwatch_box_, 0, 0);

    lv_obj_clear_flag(stopwatch_box_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);



    // Back button -> Trở về màn hình Đồng hồ chính!

    sw_back_btn_ = lv_button_create(stopwatch_box_);

    lv_obj_set_size(sw_back_btn_, 36, 32);

    lv_obj_set_style_bg_opa(sw_back_btn_, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(sw_back_btn_, 0, 0);

    lv_obj_set_style_pad_all(sw_back_btn_, 0, 0);

    lv_obj_set_ext_click_area(sw_back_btn_, 8);

    lv_obj_t* back_lbl = lv_label_create(sw_back_btn_);

    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);

    lv_obj_set_style_text_font(back_lbl, &font_material_symbols_30_4, 0);

    lv_obj_set_style_text_color(back_lbl, lv_color_white(), 0);

    lv_obj_align(back_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(sw_back_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;

        self->ShowClock();

    }, LV_EVENT_CLICKED, this);



    // Tab switcher: [ BẤM GIỜ ]  [ ĐẾM NGƯỢC ]

    sw_tab_sw_btn_ = lv_button_create(stopwatch_box_);

    lv_obj_set_size(sw_tab_sw_btn_, 94, 30);

    lv_obj_set_style_bg_color(sw_tab_sw_btn_, lv_color_hex(0x0284C7), 0);

    lv_obj_set_style_bg_opa(sw_tab_sw_btn_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(sw_tab_sw_btn_, 6, 0);

    lv_obj_set_style_pad_all(sw_tab_sw_btn_, 0, 0);

    lv_obj_set_ext_click_area(sw_tab_sw_btn_, 8);

    lv_obj_t* tab_sw_txt = lv_label_create(sw_tab_sw_btn_);

    lv_label_set_text(tab_sw_txt, "Bấm giờ");

    lv_obj_set_style_text_font(tab_sw_txt, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(tab_sw_txt, lv_color_white(), 0);

    lv_obj_align(tab_sw_txt, LV_ALIGN_CENTER, 0, 0);

    lv_obj_clear_flag(tab_sw_txt, LV_OBJ_FLAG_CLICKABLE);



    sw_tab_cd_btn_ = lv_button_create(stopwatch_box_);

    lv_obj_set_size(sw_tab_cd_btn_, 94, 30);

    lv_obj_set_style_bg_color(sw_tab_cd_btn_, lv_color_hex(0x1E293B), 0);

    lv_obj_set_style_bg_opa(sw_tab_cd_btn_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(sw_tab_cd_btn_, 6, 0);

    lv_obj_set_style_pad_all(sw_tab_cd_btn_, 0, 0);

    lv_obj_set_ext_click_area(sw_tab_cd_btn_, 8);

    lv_obj_t* tab_cd_txt = lv_label_create(sw_tab_cd_btn_);

    lv_label_set_text(tab_cd_txt, "Đếm ngược");

    lv_obj_set_style_text_font(tab_cd_txt, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(tab_cd_txt, lv_color_hex(0x94A3B8), 0);

    lv_obj_align(tab_cd_txt, LV_ALIGN_CENTER, 0, 0);

    lv_obj_clear_flag(tab_cd_txt, LV_OBJ_FLAG_CLICKABLE);



    lv_obj_add_event_cb(sw_tab_sw_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        self->sw_mode_countdown_ = false;

        lv_obj_set_style_bg_color(self->sw_tab_sw_btn_, lv_color_hex(0x0284C7), 0);

        lv_obj_set_style_bg_color(self->sw_tab_cd_btn_, lv_color_hex(0x1E293B), 0);

        lv_obj_t* t1 = lv_obj_get_child(self->sw_tab_sw_btn_, 0);

        lv_obj_t* t2 = lv_obj_get_child(self->sw_tab_cd_btn_, 0);

        if (t1) lv_obj_set_style_text_color(t1, lv_color_white(), 0);

        if (t2) lv_obj_set_style_text_color(t2, lv_color_hex(0x94A3B8), 0);

        if (self->sw_container_) lv_obj_remove_flag(self->sw_container_, LV_OBJ_FLAG_HIDDEN);

        if (self->cd_container_) lv_obj_add_flag(self->cd_container_, LV_OBJ_FLAG_HIDDEN);

    }, LV_EVENT_CLICKED, this);



    lv_obj_add_event_cb(sw_tab_cd_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        self->sw_mode_countdown_ = true;

        lv_obj_set_style_bg_color(self->sw_tab_sw_btn_, lv_color_hex(0x1E293B), 0);

        lv_obj_set_style_bg_color(self->sw_tab_cd_btn_, lv_color_hex(0x0284C7), 0);

        lv_obj_t* t1 = lv_obj_get_child(self->sw_tab_sw_btn_, 0);

        lv_obj_t* t2 = lv_obj_get_child(self->sw_tab_cd_btn_, 0);

        if (t1) lv_obj_set_style_text_color(t1, lv_color_hex(0x94A3B8), 0);

        if (t2) lv_obj_set_style_text_color(t2, lv_color_white(), 0);

        if (self->sw_container_) lv_obj_add_flag(self->sw_container_, LV_OBJ_FLAG_HIDDEN);

        if (self->cd_container_) lv_obj_remove_flag(self->cd_container_, LV_OBJ_FLAG_HIDDEN);

    }, LV_EVENT_CLICKED, this);



    // ================= 1. Container BẤM GIỜ =================

    sw_container_ = lv_obj_create(stopwatch_box_);

    lv_obj_set_style_bg_opa(sw_container_, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(sw_container_, 0, 0);

    lv_obj_set_style_pad_all(sw_container_, 0, 0);

    lv_obj_clear_flag(sw_container_, LV_OBJ_FLAG_SCROLLABLE);



    lv_obj_t* sw_card = lv_obj_create(sw_container_);

    lv_obj_set_style_bg_color(sw_card, lv_color_hex(0x161D27), 0);

    lv_obj_set_style_bg_opa(sw_card, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(sw_card, 10, 0);

    lv_obj_set_style_border_width(sw_card, 1, 0);

    lv_obj_set_style_border_color(sw_card, lv_color_hex(0x283548), 0);

    lv_obj_set_user_data(sw_card, (void*)(intptr_t)310);



    sw_time_label_ = lv_label_create(sw_card);

    lv_label_set_text(sw_time_label_, "00:00.0");

    lv_obj_set_style_text_font(sw_time_label_, &font_noto_sans_basic_30_4, 0);

    lv_obj_set_style_text_color(sw_time_label_, lv_color_white(), 0);



    sw_status_label_ = lv_label_create(sw_card);

    lv_label_set_text(sw_status_label_, "Sẵn sàng bấm giờ");

    lv_obj_set_style_text_font(sw_status_label_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(sw_status_label_, lv_color_hex(0x94A3B8), 0);



    sw_start_btn_ = lv_button_create(sw_container_);

    lv_obj_set_style_bg_color(sw_start_btn_, lv_color_hex(0x1B5E20), 0);

    lv_obj_set_style_bg_opa(sw_start_btn_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(sw_start_btn_, 8, 0);

    sw_start_lbl_ = lv_label_create(sw_start_btn_);

    lv_label_set_text(sw_start_lbl_, "Bắt đầu");

    lv_obj_set_style_text_font(sw_start_lbl_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(sw_start_lbl_, lv_color_white(), 0);

    lv_obj_align(sw_start_lbl_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(sw_start_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 300) return;

        self->sw_running_ = !self->sw_running_;

        if (self->sw_running_) {

            self->sw_last_tick_ms_ = esp_timer_get_time() / 1000;

            lv_obj_set_style_bg_color(self->sw_start_btn_, lv_color_hex(0xE65100), 0);

            lv_label_set_text(self->sw_start_lbl_, "Tạm dừng");

            lv_label_set_text(self->sw_status_label_, "Đang bấm giờ...");

        } else {

            lv_obj_set_style_bg_color(self->sw_start_btn_, lv_color_hex(0x1B5E20), 0);

            lv_label_set_text(self->sw_start_lbl_, "Tiếp tục");

            lv_label_set_text(self->sw_status_label_, "Đã tạm dừng");

        }

    }, LV_EVENT_CLICKED, this);



    sw_reset_btn_ = lv_button_create(sw_container_);

    lv_obj_set_style_bg_color(sw_reset_btn_, lv_color_hex(0x374151), 0);

    lv_obj_set_style_bg_opa(sw_reset_btn_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(sw_reset_btn_, 8, 0);

    sw_reset_lbl_ = lv_label_create(sw_reset_btn_);

    lv_label_set_text(sw_reset_lbl_, "Đặt lại");

    lv_obj_set_style_text_font(sw_reset_lbl_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(sw_reset_lbl_, lv_color_white(), 0);

    lv_obj_align(sw_reset_lbl_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(sw_reset_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 300) return;

        self->sw_running_ = false;

        self->sw_elapsed_ms_ = 0;

        lv_obj_set_style_bg_color(self->sw_start_btn_, lv_color_hex(0x1B5E20), 0);

        lv_label_set_text(self->sw_start_lbl_, "Bắt đầu");

        lv_label_set_text(self->sw_status_label_, "Sẵn sàng bấm giờ");

        if (self->sw_time_label_) lv_label_set_text(self->sw_time_label_, "00:00.0");

    }, LV_EVENT_CLICKED, this);



    // ================= 2. Container ĐẾM NGƯỢC =================

    cd_container_ = lv_obj_create(stopwatch_box_);

    lv_obj_set_style_bg_opa(cd_container_, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(cd_container_, 0, 0);

    lv_obj_set_style_pad_all(cd_container_, 0, 0);

    lv_obj_clear_flag(cd_container_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(cd_container_, LV_OBJ_FLAG_HIDDEN);



    lv_obj_t* cd_card = lv_obj_create(cd_container_);

    lv_obj_set_style_bg_color(cd_card, lv_color_hex(0x161D27), 0);

    lv_obj_set_style_bg_opa(cd_card, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(cd_card, 10, 0);

    lv_obj_set_style_border_width(cd_card, 1, 0);

    lv_obj_set_style_border_color(cd_card, lv_color_hex(0x283548), 0);

    lv_obj_set_user_data(cd_card, (void*)(intptr_t)320);



    cd_time_label_ = lv_label_create(cd_card);

    lv_label_set_text(cd_time_label_, "05:00.0");

    lv_obj_set_style_text_font(cd_time_label_, &font_noto_sans_basic_30_4, 0);

    lv_obj_set_style_text_color(cd_time_label_, lv_color_white(), 0);



    cd_status_label_ = lv_label_create(cd_card);

    lv_label_set_text(cd_status_label_, "Cài đặt: 5 phút");

    lv_obj_set_style_text_font(cd_status_label_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(cd_status_label_, lv_color_hex(0x94A3B8), 0);



    // Nút chỉnh phút: [- 1p] và [+ 1p]

    cd_minus_btn_ = lv_button_create(cd_container_);

    lv_obj_set_style_bg_color(cd_minus_btn_, lv_color_hex(0x223344), 0);

    lv_obj_set_style_radius(cd_minus_btn_, 6, 0);

    lv_obj_t* m_lbl = lv_label_create(cd_minus_btn_);

    lv_label_set_text(m_lbl, "- 1p");

    lv_obj_set_style_text_font(m_lbl, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(m_lbl, lv_color_white(), 0);

    lv_obj_align(m_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(cd_minus_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (self->cd_running_) return;

        if (self->cd_minutes_ > 1) self->cd_minutes_--;

        self->cd_remain_ms_ = (int64_t)self->cd_minutes_ * 60 * 1000;

        char buf[16];

        snprintf(buf, sizeof(buf), "%02d:00.0", self->cd_minutes_);

        if (self->cd_time_label_) lv_label_set_text(self->cd_time_label_, buf);

        char sbuf[32];

        snprintf(sbuf, sizeof(sbuf), "Đã chọn: %d phút", self->cd_minutes_);

        if (self->cd_status_label_) lv_label_set_text(self->cd_status_label_, sbuf);

    }, LV_EVENT_CLICKED, this);



    cd_plus_btn_ = lv_button_create(cd_container_);

    lv_obj_set_style_bg_color(cd_plus_btn_, lv_color_hex(0x223344), 0);

    lv_obj_set_style_radius(cd_plus_btn_, 6, 0);

    lv_obj_t* p_lbl = lv_label_create(cd_plus_btn_);

    lv_label_set_text(p_lbl, "+ 1p");

    lv_obj_set_style_text_font(p_lbl, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(p_lbl, lv_color_white(), 0);

    lv_obj_align(p_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(cd_plus_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (self->cd_running_) return;

        if (self->cd_minutes_ < 99) self->cd_minutes_++;

        self->cd_remain_ms_ = (int64_t)self->cd_minutes_ * 60 * 1000;

        char buf[16];

        snprintf(buf, sizeof(buf), "%02d:00.0", self->cd_minutes_);

        if (self->cd_time_label_) lv_label_set_text(self->cd_time_label_, buf);

        char sbuf[32];

        snprintf(sbuf, sizeof(sbuf), "Đã chọn: %d phút", self->cd_minutes_);

        if (self->cd_status_label_) lv_label_set_text(self->cd_status_label_, sbuf);

    }, LV_EVENT_CLICKED, this);



    // 4 nút preset nhanh: 1p, 5p, 15p, 25p

    int presets[4] = {1, 5, 15, 25};

    const char* preset_names[4] = {"1p", "5p", "15p", "25p"};

    for (int i = 0; i < 4; ++i) {

        lv_obj_t* btn = lv_button_create(cd_container_);

        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E293B), 0);

        lv_obj_set_style_radius(btn, 6, 0);

        lv_obj_set_user_data(btn, (void*)(intptr_t)(presets[i]));

        lv_obj_t* l = lv_label_create(btn);

        lv_label_set_text(l, preset_names[i]);

        lv_obj_set_style_text_font(l, &font_noto_sans_custom_14_1, 0);

        lv_obj_set_style_text_color(l, lv_color_hex(0x38BDF8), 0);

        lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {

            auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

            if (self->cd_running_) return;

            int m = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));

            self->cd_minutes_ = m;

            self->cd_remain_ms_ = (int64_t)m * 60 * 1000;

            char buf[16];

            snprintf(buf, sizeof(buf), "%02d:00.0", m);

            if (self->cd_time_label_) lv_label_set_text(self->cd_time_label_, buf);

            char sbuf[32];

            snprintf(sbuf, sizeof(sbuf), "Đã chọn: %d phút", m);

            if (self->cd_status_label_) lv_label_set_text(self->cd_status_label_, sbuf);

        }, LV_EVENT_CLICKED, this);

    }



    // Action buttons: Bắt đầu / Đặt lại

    cd_start_btn_ = lv_button_create(cd_container_);

    lv_obj_set_style_bg_color(cd_start_btn_, lv_color_hex(0x1B5E20), 0);

    lv_obj_set_style_radius(cd_start_btn_, 8, 0);

    cd_start_lbl_ = lv_label_create(cd_start_btn_);

    lv_label_set_text(cd_start_lbl_, "Bắt đầu");

    lv_obj_set_style_text_font(cd_start_lbl_, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(cd_start_lbl_, lv_color_white(), 0);

    lv_obj_align(cd_start_lbl_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(cd_start_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 300) return;

        self->cd_running_ = !self->cd_running_;

        if (self->cd_running_) {

            if (self->cd_remain_ms_ <= 0) self->cd_remain_ms_ = (int64_t)self->cd_minutes_ * 60 * 1000;

            self->cd_last_tick_ms_ = esp_timer_get_time() / 1000;

            lv_obj_set_style_bg_color(self->cd_start_btn_, lv_color_hex(0xE65100), 0);

            lv_label_set_text(self->cd_start_lbl_, "Tạm dừng");

            lv_label_set_text(self->cd_status_label_, "Đang đếm ngược...");

            lv_obj_set_style_text_color(self->cd_status_label_, lv_color_hex(0x38BDF8), 0);

        } else {

            lv_obj_set_style_bg_color(self->cd_start_btn_, lv_color_hex(0x1B5E20), 0);

            lv_label_set_text(self->cd_start_lbl_, "Tiếp tục");

            lv_label_set_text(self->cd_status_label_, "Đã tạm dừng");

        }

    }, LV_EVENT_CLICKED, this);



    cd_reset_btn_ = lv_button_create(cd_container_);

    lv_obj_set_style_bg_color(cd_reset_btn_, lv_color_hex(0x374151), 0);

    lv_obj_set_style_radius(cd_reset_btn_, 8, 0);

    lv_obj_t* cd_r_lbl = lv_label_create(cd_reset_btn_);

    lv_label_set_text(cd_r_lbl, "Đặt lại");

    lv_obj_set_style_text_font(cd_r_lbl, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(cd_r_lbl, lv_color_white(), 0);

    lv_obj_align(cd_r_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(cd_reset_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 300) return;

        self->cd_running_ = false;

        self->cd_remain_ms_ = (int64_t)self->cd_minutes_ * 60 * 1000;

        lv_obj_set_style_bg_color(self->cd_start_btn_, lv_color_hex(0x1B5E20), 0);

        lv_label_set_text(self->cd_start_lbl_, "Bắt đầu");

        char sbuf[32];

        snprintf(sbuf, sizeof(sbuf), "Cài đặt: %d phút", self->cd_minutes_);

        lv_label_set_text(self->cd_status_label_, sbuf);

        lv_obj_set_style_text_color(self->cd_status_label_, lv_color_hex(0x94A3B8), 0);

        char buf[16];

        snprintf(buf, sizeof(buf), "%02d:00.0", self->cd_minutes_);

        if (self->cd_time_label_) lv_label_set_text(self->cd_time_label_, buf);

    }, LV_EVENT_CLICKED, this);



    // Fast tick timer (50ms): cho số chạy mượt mà theo từng giây/mili-giây và đếm ngược loa tiếng!

    if (fast_tick_timer_ == nullptr) {

        fast_tick_timer_ = lv_timer_create([](lv_timer_t* t) {

            auto* self = static_cast<LcdDisplay*>(lv_timer_get_user_data(t));

            if (self == nullptr) return;



            // 1. Chạy số Bấm giờ mili-giây

            if (self->sw_running_) {

                int64_t now_ms = esp_timer_get_time() / 1000;

                self->sw_elapsed_ms_ += (now_ms - self->sw_last_tick_ms_);

                self->sw_last_tick_ms_ = now_ms;

                int total_tenth = (int)(self->sw_elapsed_ms_ / 100);

                int tenths = total_tenth % 10;

                int total_secs = total_tenth / 10;

                int secs = total_secs % 60;

                int mins = total_secs / 60;

                char buf[32];

                snprintf(buf, sizeof(buf), "%02d:%02d.%d", mins, secs, tenths);

                if (self->sw_time_label_) lv_label_set_text(self->sw_time_label_, buf);

            }



            // 2. Chạy Đếm ngược & Báo loa khi về 0

            if (self->cd_running_) {

                int64_t now_ms = esp_timer_get_time() / 1000;

                int64_t diff = now_ms - self->cd_last_tick_ms_;

                self->cd_last_tick_ms_ = now_ms;

                self->cd_remain_ms_ -= diff;

                if (self->cd_remain_ms_ <= 0) {

                    self->cd_remain_ms_ = 0;

                    self->cd_running_ = false;

                    if (self->cd_time_label_) lv_label_set_text(self->cd_time_label_, "00:00.0");

                    if (self->cd_status_label_) {

                        lv_label_set_text(self->cd_status_label_, "HẾT GIỜ!");

                        lv_obj_set_style_text_color(self->cd_status_label_, lv_color_hex(0xFF1744), 0);

                    }

                    if (self->cd_start_lbl_) lv_label_set_text(self->cd_start_lbl_, "Bắt đầu");

                    if (self->cd_start_btn_) lv_obj_set_style_bg_color(self->cd_start_btn_, lv_color_hex(0x1B5E20), 0);

                    self->ShowNotification("Đếm ngược: ĐÃ HẾT GIỜ!", 4000);

                    Application::GetInstance().GetAudioService().PlaySound(Lang::Sounds::OGG_EXCLAMATION);

                } else {

                    int total_tenth = (int)(self->cd_remain_ms_ / 100);

                    int tenths = total_tenth % 10;

                    int total_secs = total_tenth / 10;

                    int secs = total_secs % 60;

                    int mins = total_secs / 60;

                    char buf[32];

                    snprintf(buf, sizeof(buf), "%02d:%02d.%d", mins, secs, tenths);

                    if (self->cd_time_label_) lv_label_set_text(self->cd_time_label_, buf);

                }

            }

        }, 50, this);

    }



    LayoutStopwatchWidgets(height_ > width_);

}



void LcdDisplay::LayoutStopwatchWidgets(bool is_portrait) {

    if (stopwatch_box_ == nullptr) return;

    lv_obj_set_size(stopwatch_box_, width_, height_);



    if (is_portrait) {

        // 240 x 320

        if (sw_back_btn_) lv_obj_set_pos(sw_back_btn_, 6, 12);

        if (sw_tab_sw_btn_) lv_obj_set_pos(sw_tab_sw_btn_, 46, 12);

        if (sw_tab_cd_btn_) lv_obj_set_pos(sw_tab_cd_btn_, 142, 12);



        // Bấm giờ panel

        if (sw_container_) {

            lv_obj_set_size(sw_container_, 240, 266);

            lv_obj_set_pos(sw_container_, 0, 48);

            lv_obj_t* sw_card = nullptr;

            uint32_t cnt = lv_obj_get_child_count(sw_container_);

            for (uint32_t i = 0; i < cnt; ++i) {

                lv_obj_t* child = lv_obj_get_child(sw_container_, i);

                if ((int)(intptr_t)lv_obj_get_user_data(child) == 310) sw_card = child;

            }

            if (sw_card) {

                lv_obj_set_size(sw_card, 224, 114);

                lv_obj_set_pos(sw_card, 8, 6);

                if (sw_time_label_) lv_obj_align(sw_time_label_, LV_ALIGN_CENTER, 0, -12);

                if (sw_status_label_) lv_obj_align(sw_status_label_, LV_ALIGN_CENTER, 0, 28);

            }

            if (sw_start_btn_) {

                lv_obj_set_size(sw_start_btn_, 224, 48);

                lv_obj_set_pos(sw_start_btn_, 8, 134);

            }

            if (sw_reset_btn_) {

                lv_obj_set_size(sw_reset_btn_, 224, 44);

                lv_obj_set_pos(sw_reset_btn_, 8, 192);

            }

        }



        // Đếm ngược panel

        if (cd_container_) {

            lv_obj_set_size(cd_container_, 240, 266);

            lv_obj_set_pos(cd_container_, 0, 48);

            lv_obj_t* cd_card = nullptr;

            uint32_t cnt = lv_obj_get_child_count(cd_container_);

            for (uint32_t i = 0; i < cnt; ++i) {

                lv_obj_t* child = lv_obj_get_child(cd_container_, i);

                if ((int)(intptr_t)lv_obj_get_user_data(child) == 320) cd_card = child;

            }

            if (cd_card) {

                lv_obj_set_size(cd_card, 224, 88);

                lv_obj_set_pos(cd_card, 8, 4);

                if (cd_time_label_) lv_obj_align(cd_time_label_, LV_ALIGN_CENTER, 0, -10);

                if (cd_status_label_) lv_obj_align(cd_status_label_, LV_ALIGN_CENTER, 0, 24);

            }

            if (cd_minus_btn_) {

                lv_obj_set_size(cd_minus_btn_, 50, 32);

                lv_obj_set_pos(cd_minus_btn_, 8, 98);

            }

            if (cd_plus_btn_) {

                lv_obj_set_size(cd_plus_btn_, 50, 32);

                lv_obj_set_pos(cd_plus_btn_, 182, 98);

            }

            int p_idx = 0;

            uint32_t ccnt = lv_obj_get_child_count(cd_container_);

            for (uint32_t i = 0; i < ccnt; ++i) {

                lv_obj_t* child = lv_obj_get_child(cd_container_, i);

                int ud = (int)(intptr_t)lv_obj_get_user_data(child);

                if (ud == 1 || ud == 5 || ud == 15 || ud == 25) {

                    lv_obj_set_size(child, 26, 32);

                    lv_obj_set_pos(child, 62 + p_idx * 29, 98);

                    p_idx++;

                }

            }

            if (cd_start_btn_) {

                lv_obj_set_size(cd_start_btn_, 224, 46);

                lv_obj_set_pos(cd_start_btn_, 8, 140);

            }

            if (cd_reset_btn_) {

                lv_obj_set_size(cd_reset_btn_, 224, 40);

                lv_obj_set_pos(cd_reset_btn_, 8, 194);

            }

        }

    } else {

        // 320 x 240: Dời các nút xuống y=10..12 để dễ chạm, không bị sát mép trên

        if (sw_back_btn_) lv_obj_set_pos(sw_back_btn_, 6, 10);

        if (sw_tab_sw_btn_) {

            lv_obj_set_size(sw_tab_sw_btn_, 98, 32);

            lv_obj_set_pos(sw_tab_sw_btn_, 58, 10);

        }

        if (sw_tab_cd_btn_) {

            lv_obj_set_size(sw_tab_cd_btn_, 98, 32);

            lv_obj_set_pos(sw_tab_cd_btn_, 164, 10);

        }



        if (sw_container_) {

            lv_obj_set_size(sw_container_, 320, 188);

            lv_obj_set_pos(sw_container_, 0, 48);

            lv_obj_t* sw_card = nullptr;

            uint32_t cnt = lv_obj_get_child_count(sw_container_);

            for (uint32_t i = 0; i < cnt; ++i) {

                lv_obj_t* child = lv_obj_get_child(sw_container_, i);

                if ((int)(intptr_t)lv_obj_get_user_data(child) == 310) sw_card = child;

            }

            if (sw_card) {

                lv_obj_set_size(sw_card, 304, 88);

                lv_obj_set_pos(sw_card, 8, 4);

                if (sw_time_label_) lv_obj_align(sw_time_label_, LV_ALIGN_CENTER, 0, -10);

                if (sw_status_label_) lv_obj_align(sw_status_label_, LV_ALIGN_CENTER, 0, 24);

            }

            if (sw_start_btn_) {

                lv_obj_set_size(sw_start_btn_, 146, 54);

                lv_obj_set_pos(sw_start_btn_, 8, 102);

            }

            if (sw_reset_btn_) {

                lv_obj_set_size(sw_reset_btn_, 146, 54);

                lv_obj_set_pos(sw_reset_btn_, 166, 102);

            }

        }



        if (cd_container_) {

            lv_obj_set_size(cd_container_, 320, 188);

            lv_obj_set_pos(cd_container_, 0, 48);

            lv_obj_t* cd_card = nullptr;

            uint32_t cnt = lv_obj_get_child_count(cd_container_);

            for (uint32_t i = 0; i < cnt; ++i) {

                lv_obj_t* child = lv_obj_get_child(cd_container_, i);

                if ((int)(intptr_t)lv_obj_get_user_data(child) == 320) cd_card = child;

            }

            if (cd_card) {

                lv_obj_set_size(cd_card, 304, 74);

                lv_obj_set_pos(cd_card, 8, 4);

                if (cd_time_label_) lv_obj_align(cd_time_label_, LV_ALIGN_CENTER, 0, -10);

                if (cd_status_label_) lv_obj_align(cd_status_label_, LV_ALIGN_CENTER, 0, 22);

            }

            if (cd_minus_btn_) {

                lv_obj_set_size(cd_minus_btn_, 44, 30);

                lv_obj_set_pos(cd_minus_btn_, 8, 86);

            }

            if (cd_plus_btn_) {

                lv_obj_set_size(cd_plus_btn_, 44, 30);

                lv_obj_set_pos(cd_plus_btn_, 268, 86);

            }

            int p_idx = 0;

            uint32_t ccnt = lv_obj_get_child_count(cd_container_);

            for (uint32_t i = 0; i < ccnt; ++i) {

                lv_obj_t* child = lv_obj_get_child(cd_container_, i);

                int ud = (int)(intptr_t)lv_obj_get_user_data(child);

                if (ud == 1 || ud == 5 || ud == 15 || ud == 25) {

                    lv_obj_set_size(child, 48, 30);

                    lv_obj_set_pos(child, 58 + p_idx * 52, 86);

                    p_idx++;

                }

            }

            if (cd_start_btn_) {

                lv_obj_set_size(cd_start_btn_, 146, 50);

                lv_obj_set_pos(cd_start_btn_, 8, 126);

            }

            if (cd_reset_btn_) {

                lv_obj_set_size(cd_reset_btn_, 146, 50);

                lv_obj_set_pos(cd_reset_btn_, 166, 126);

            }

        }

    }

}



void LcdDisplay::UpdateStopwatchUI() {

    // Fast timer đã cập nhật nhãn trực tiếp

}



// ======================== TIMER SCREEN ========================

void LcdDisplay::RenderAlarmList() {

    if (alarm_list_container_ == nullptr) return;

    lv_obj_clean(alarm_list_container_);



    if (alarms_.empty()) {

        lv_obj_t* empty_lbl = lv_label_create(alarm_list_container_);

        lv_label_set_text(empty_lbl, "Chưa có báo thức");

        lv_obj_set_style_text_font(empty_lbl, &font_noto_sans_custom_14_1, 0);

        lv_obj_set_style_text_color(empty_lbl, lv_color_hex(0x64748B), 0);

        lv_obj_align(empty_lbl, LV_ALIGN_CENTER, 0, 0);

        return;

    }



    for (size_t i = 0; i < alarms_.size(); ++i) {

        auto& alm = alarms_[i];

        int alm_id = alm.id;



        lv_obj_t* row = lv_obj_create(alarm_list_container_);

        lv_obj_set_style_bg_color(row, lv_color_hex(0x1E293B), 0);

        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);

        lv_obj_set_style_radius(row, 6, 0);

        lv_obj_set_style_border_width(row, 1, 0);

        lv_obj_set_style_border_color(row, alm.enabled ? lv_color_hex(0x3B82F6) : lv_color_hex(0x334155), 0);

        lv_obj_set_style_pad_all(row, 0, 0);

        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);



        // Giờ: %02d:%02d

        lv_obj_t* time_lbl = lv_label_create(row);

        char tbuf[16];

        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", alm.hour, alm.min);

        lv_label_set_text(time_lbl, tbuf);

        lv_obj_set_style_text_font(time_lbl, &font_noto_sans_custom_16_4, 0);

        lv_obj_set_style_text_color(time_lbl, alm.enabled ? lv_color_white() : lv_color_hex(0x64748B), 0);

        lv_obj_align(time_lbl, LV_ALIGN_LEFT_MID, 6, -8);



        // Thứ lặp lại:

        const char* ddesc = "Hàng ngày";

        if (alm.days_mask == 0x7F) ddesc = "Hàng ngày";

        else if (alm.days_mask == 0x3E) ddesc = "T2 - T6";

        else if (alm.days_mask == 0x41) ddesc = "Cuối tuần";

        else ddesc = "Tuỳ chỉnh";



        lv_obj_t* day_lbl = lv_label_create(row);

        lv_label_set_text(day_lbl, ddesc);

        lv_obj_set_style_text_font(day_lbl, &font_noto_sans_custom_14_1, 0);

        lv_obj_set_style_text_color(day_lbl, alm.enabled ? lv_color_hex(0x38BDF8) : lv_color_hex(0x64748B), 0);

        lv_obj_align(day_lbl, LV_ALIGN_LEFT_MID, 6, 10);



        // Nút Bật/Tắt to dễ bấm

        lv_obj_t* tgl_btn = lv_button_create(row);

        lv_obj_set_size(tgl_btn, 42, 30);

        lv_obj_set_style_bg_color(tgl_btn, alm.enabled ? lv_color_hex(0x10B981) : lv_color_hex(0x475569), 0);

        lv_obj_set_style_radius(tgl_btn, 4, 0);

        lv_obj_set_style_pad_all(tgl_btn, 0, 0);

        lv_obj_set_ext_click_area(tgl_btn, 8);

        lv_obj_set_user_data(tgl_btn, (void*)(intptr_t)alm_id);

        lv_obj_t* tgl_lbl = lv_label_create(tgl_btn);

        lv_label_set_text(tgl_lbl, alm.enabled ? "BẬT" : "TẮT");

        lv_obj_set_style_text_font(tgl_lbl, &font_noto_sans_basic_14_1, 0);

        lv_obj_set_style_text_color(tgl_lbl, lv_color_white(), 0);

        lv_obj_align(tgl_lbl, LV_ALIGN_CENTER, 0, 0);

        lv_obj_clear_flag(tgl_lbl, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_add_event_cb(tgl_btn, [](lv_event_t* e) {

            auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

            int id = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));

            bool st = false;

            for (auto& a : self->alarms_) {

                if (a.id == id) {

                    a.enabled = !a.enabled;

                    st = a.enabled;

                    break;

                }

            }

            self->RenderAlarmList();

            self->ShowNotification(st ? "Đã bật báo thức!" : "Đã tắt báo thức!", 1200);

        }, LV_EVENT_CLICKED, this);



        // Nút Xóa [X] to rõ

        lv_obj_t* del_btn = lv_button_create(row);

        lv_obj_set_size(del_btn, 30, 30);

        lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xDC2626), 0);

        lv_obj_set_style_radius(del_btn, 4, 0);

        lv_obj_set_style_pad_all(del_btn, 0, 0);

        lv_obj_set_ext_click_area(del_btn, 8);

        lv_obj_set_user_data(del_btn, (void*)(intptr_t)alm_id);

        lv_obj_t* del_lbl = lv_label_create(del_btn);

        lv_label_set_text(del_lbl, "X");

        lv_obj_set_style_text_font(del_lbl, &font_noto_sans_basic_16_4, 0);

        lv_obj_set_style_text_color(del_lbl, lv_color_white(), 0);

        lv_obj_align(del_lbl, LV_ALIGN_CENTER, 0, 0);

        lv_obj_clear_flag(del_lbl, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_add_event_cb(del_btn, [](lv_event_t* e) {

            auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

            int id = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));

            for (auto it = self->alarms_.begin(); it != self->alarms_.end(); ++it) {

                if (it->id == id) {

                    self->alarms_.erase(it);

                    break;

                }

            }

            self->RenderAlarmList();

            self->ShowNotification("Đã xoá báo thức!", 1200);

        }, LV_EVENT_CLICKED, this);



        // Kích thước row

        if (width_ > height_) {

            // Landscape: Nới rộng row lên 160px để nhãn "Hàng ngày" không bị nút BẬT/TẮT che mất chữ y

            lv_obj_set_size(row, 160, 44);

            lv_obj_set_pos(row, 0, i * 48);

            lv_obj_set_size(del_btn, 26, 28);

            lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, -2, 0);

            lv_obj_set_size(tgl_btn, 38, 28);

            lv_obj_align(tgl_btn, LV_ALIGN_RIGHT_MID, -32, 0);

        } else {

            // Portrait

            lv_obj_set_size(row, 216, 44);

            lv_obj_set_pos(row, 0, i * 48);

            lv_obj_align(tgl_btn, LV_ALIGN_RIGHT_MID, -36, 0);

            lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, -2, 0);

        }

    }

}



void LcdDisplay::CreateTimerUI() {

    auto screen = lv_screen_active();

    timer_box_ = lv_obj_create(screen);

    lv_obj_set_size(timer_box_, width_, height_);

    lv_obj_set_style_bg_color(timer_box_, lv_color_hex(0x0f172a), 0);

    lv_obj_set_style_bg_opa(timer_box_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(timer_box_, 0, 0);

    lv_obj_set_style_border_width(timer_box_, 0, 0);

    lv_obj_set_style_pad_all(timer_box_, 0, 0);

    lv_obj_clear_flag(timer_box_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);



    // Back button -> Trở về màn hình Đồng hồ chính!

    timer_back_btn_ = lv_button_create(timer_box_);

    lv_obj_set_size(timer_back_btn_, 36, 32);

    lv_obj_set_style_bg_opa(timer_back_btn_, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(timer_back_btn_, 0, 0);

    lv_obj_set_style_pad_all(timer_back_btn_, 0, 0);

    lv_obj_set_ext_click_area(timer_back_btn_, 8);

    lv_obj_t* back_lbl = lv_label_create(timer_back_btn_);

    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);

    lv_obj_set_style_text_font(back_lbl, &font_material_symbols_30_4, 0);

    lv_obj_set_style_text_color(back_lbl, lv_color_white(), 0);

    lv_obj_align(back_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(timer_back_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;

        self->ShowClock();

    }, LV_EVENT_CLICKED, this);



    // Title

    lv_obj_t* title_lbl = lv_label_create(timer_box_);

    lv_label_set_text(title_lbl, "Báo Thức");

    lv_obj_set_style_text_font(title_lbl, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);

    lv_obj_set_user_data(title_lbl, (void*)(intptr_t)410);



    // 1. Roller Giờ (00..23)

    const char* hour_opts = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";

    alarm_hour_roller_ = lv_roller_create(timer_box_);

    lv_roller_set_options(alarm_hour_roller_, hour_opts, LV_ROLLER_MODE_INFINITE);

    lv_roller_set_visible_row_count(alarm_hour_roller_, 3);

    lv_obj_set_style_text_font(alarm_hour_roller_, &font_noto_sans_basic_16_4, 0);

    lv_obj_set_style_text_color(alarm_hour_roller_, lv_color_hex(0x94A3B8), 0);

    lv_obj_set_style_text_font(alarm_hour_roller_, &font_noto_sans_basic_20_4, LV_PART_SELECTED);

    lv_obj_set_style_text_color(alarm_hour_roller_, lv_color_white(), LV_PART_SELECTED);

    lv_obj_set_style_bg_color(alarm_hour_roller_, lv_color_hex(0x1D4ED8), LV_PART_SELECTED);

    lv_obj_set_style_bg_opa(alarm_hour_roller_, LV_OPA_COVER, LV_PART_SELECTED);

    lv_obj_set_style_border_color(alarm_hour_roller_, lv_color_hex(0x3B82F6), LV_PART_SELECTED);

    lv_obj_set_style_border_width(alarm_hour_roller_, 1, LV_PART_SELECTED);

    lv_obj_set_style_bg_color(alarm_hour_roller_, lv_color_hex(0x0F172A), 0);

    lv_obj_set_style_bg_opa(alarm_hour_roller_, LV_OPA_COVER, 0);

    lv_obj_set_style_border_color(alarm_hour_roller_, lv_color_hex(0x334155), 0);

    lv_obj_set_style_border_width(alarm_hour_roller_, 1, 0);

    lv_obj_set_style_radius(alarm_hour_roller_, 6, 0);

    lv_roller_set_selected(alarm_hour_roller_, 7, LV_ANIM_OFF); // default 07:00



    // Colon separator ":"

    lv_obj_t* colon_lbl = lv_label_create(timer_box_);

    lv_label_set_text(colon_lbl, ":");

    lv_obj_set_style_text_font(colon_lbl, &font_noto_sans_basic_20_4, 0);

    lv_obj_set_style_text_color(colon_lbl, lv_color_white(), 0);

    lv_obj_set_user_data(colon_lbl, (void*)(intptr_t)411);



    // 2. Roller Phút (00..59)

    const char* min_opts =

        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"

        "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"

        "20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n"

        "30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n"

        "40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n"

        "50\n51\n52\n53\n54\n55\n56\n57\n58\n59";

    alarm_min_roller_ = lv_roller_create(timer_box_);

    lv_roller_set_options(alarm_min_roller_, min_opts, LV_ROLLER_MODE_INFINITE);

    lv_roller_set_visible_row_count(alarm_min_roller_, 3);

    lv_obj_set_style_text_font(alarm_min_roller_, &font_noto_sans_basic_16_4, 0);

    lv_obj_set_style_text_color(alarm_min_roller_, lv_color_hex(0x94A3B8), 0);

    lv_obj_set_style_text_font(alarm_min_roller_, &font_noto_sans_basic_20_4, LV_PART_SELECTED);

    lv_obj_set_style_text_color(alarm_min_roller_, lv_color_white(), LV_PART_SELECTED);

    lv_obj_set_style_bg_color(alarm_min_roller_, lv_color_hex(0x1D4ED8), LV_PART_SELECTED);

    lv_obj_set_style_bg_opa(alarm_min_roller_, LV_OPA_COVER, LV_PART_SELECTED);

    lv_obj_set_style_border_color(alarm_min_roller_, lv_color_hex(0x3B82F6), LV_PART_SELECTED);

    lv_obj_set_style_border_width(alarm_min_roller_, 1, LV_PART_SELECTED);

    lv_obj_set_style_bg_color(alarm_min_roller_, lv_color_hex(0x0F172A), 0);

    lv_obj_set_style_bg_opa(alarm_min_roller_, LV_OPA_COVER, 0);

    lv_obj_set_style_border_color(alarm_min_roller_, lv_color_hex(0x334155), 0);

    lv_obj_set_style_border_width(alarm_min_roller_, 1, 0);

    lv_obj_set_style_radius(alarm_min_roller_, 6, 0);

    lv_roller_set_selected(alarm_min_roller_, 0, LV_ANIM_OFF);



    // 3. 7 nút chọn thứ trong tuần (CN, T2..T7)

    const char* dnames[7] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};

    alarm_new_days_mask_ = 0x7F; // default: all days

    for (int i = 0; i < 7; ++i) {

        alarm_day_btns_[i] = lv_button_create(timer_box_);

        lv_obj_set_style_radius(alarm_day_btns_[i], 4, 0);

        lv_obj_set_style_pad_all(alarm_day_btns_[i], 0, 0);

        lv_obj_set_user_data(alarm_day_btns_[i], (void*)(intptr_t)i);

        lv_obj_set_style_bg_color(alarm_day_btns_[i], lv_color_hex(0x2563EB), 0);

        lv_obj_set_style_bg_opa(alarm_day_btns_[i], LV_OPA_COVER, 0);



        lv_obj_t* dlbl = lv_label_create(alarm_day_btns_[i]);

        lv_label_set_text(dlbl, dnames[i]);

        lv_obj_set_style_text_font(dlbl, &font_noto_sans_basic_14_1, 0);

        lv_obj_set_style_text_color(dlbl, lv_color_white(), 0);

        lv_obj_align(dlbl, LV_ALIGN_CENTER, 0, 0);

        lv_obj_clear_flag(dlbl, LV_OBJ_FLAG_CLICKABLE);



        lv_obj_add_event_cb(alarm_day_btns_[i], [](lv_event_t* e) {

            auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

            int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));

            self->alarm_new_days_mask_ ^= (1 << idx);

            bool act = (self->alarm_new_days_mask_ & (1 << idx)) != 0;

            lv_obj_t* b = self->alarm_day_btns_[idx];

            lv_obj_set_style_bg_color(b, act ? lv_color_hex(0x2563EB) : lv_color_hex(0x1E293B), 0);

            lv_obj_t* l = lv_obj_get_child(b, 0);

            if (l) lv_obj_set_style_text_color(l, act ? lv_color_white() : lv_color_hex(0x64748B), 0);

        }, LV_EVENT_CLICKED, this);

    }



    // 4. Nút "+ Thêm Báo Thức"

    alarm_add_btn_ = lv_button_create(timer_box_);

    lv_obj_set_style_bg_color(alarm_add_btn_, lv_color_hex(0x0284C7), 0);

    lv_obj_set_style_bg_opa(alarm_add_btn_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(alarm_add_btn_, 6, 0);

    lv_obj_set_style_pad_all(alarm_add_btn_, 0, 0);

    lv_obj_t* add_lbl = lv_label_create(alarm_add_btn_);

    lv_label_set_text(add_lbl, "+ Thêm Báo Thức");

    lv_obj_set_style_text_font(add_lbl, &font_noto_sans_basic_14_1, 0);

    lv_obj_set_style_text_color(add_lbl, lv_color_white(), 0);

    lv_obj_align(add_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_clear_flag(add_lbl, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(alarm_add_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 300) return;

        int h = lv_roller_get_selected(self->alarm_hour_roller_);

        int m = lv_roller_get_selected(self->alarm_min_roller_);

        uint8_t dmask = self->alarm_new_days_mask_;

        if (dmask == 0) dmask = 0x7F;

        self->alarms_.push_back({self->next_alarm_id_++, h, m, dmask, true, false});

        self->RenderAlarmList();

        self->ShowNotification("Đã thêm báo thức!", 1500);

    }, LV_EVENT_CLICKED, this);



    // 5. Container danh sách báo thức bên cạnh

    alarm_list_container_ = lv_obj_create(timer_box_);

    lv_obj_set_style_bg_color(alarm_list_container_, lv_color_hex(0x131B26), 0);

    lv_obj_set_style_bg_opa(alarm_list_container_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(alarm_list_container_, 8, 0);

    lv_obj_set_style_border_width(alarm_list_container_, 1, 0);

    lv_obj_set_style_border_color(alarm_list_container_, lv_color_hex(0x283548), 0);

    lv_obj_set_style_pad_all(alarm_list_container_, 4, 0);

    lv_obj_add_flag(alarm_list_container_, LV_OBJ_FLAG_SCROLLABLE);



    // Khởi tạo 1 báo thức mặc định 07:00 nếu danh sách rỗng

    if (alarms_.empty()) {

        alarms_.push_back({next_alarm_id_++, 7, 0, 0x7F, true, false});

    }



    LayoutTimerWidgets(height_ > width_);

    RenderAlarmList();

}



void LcdDisplay::LayoutTimerWidgets(bool is_portrait) {

    if (timer_box_ == nullptr) return;

    lv_obj_set_size(timer_box_, width_, height_);



    lv_obj_t* title_lbl = nullptr;

    lv_obj_t* colon_lbl = nullptr;

    uint32_t cnt = lv_obj_get_child_count(timer_box_);

    for (uint32_t i = 0; i < cnt; ++i) {

        lv_obj_t* child = lv_obj_get_child(timer_box_, i);

        intptr_t id = (intptr_t)lv_obj_get_user_data(child);

        if (id == 410) title_lbl = child;

        else if (id == 411) colon_lbl = child;

    }



    if (is_portrait) {

        // 240 x 320

        if (timer_back_btn_) lv_obj_set_pos(timer_back_btn_, 6, 8);

        if (title_lbl) lv_obj_set_pos(title_lbl, 50, 12);



        // Rollers

        if (alarm_hour_roller_) {

            lv_obj_set_size(alarm_hour_roller_, 64, 82);

            lv_obj_set_pos(alarm_hour_roller_, 32, 40);

        }

        if (colon_lbl) lv_obj_set_pos(colon_lbl, 114, 68);

        if (alarm_min_roller_) {

            lv_obj_set_size(alarm_min_roller_, 64, 82);

            lv_obj_set_pos(alarm_min_roller_, 136, 40);

        }



        // 7 Day buttons

        for (int i = 0; i < 7; ++i) {

            if (alarm_day_btns_[i]) {

                lv_obj_set_size(alarm_day_btns_[i], 28, 26);

                lv_obj_set_pos(alarm_day_btns_[i], 12 + i * 31, 128);

            }

        }



        // Add button

        if (alarm_add_btn_) {

            lv_obj_set_size(alarm_add_btn_, 216, 32);

            lv_obj_set_pos(alarm_add_btn_, 12, 160);

        }



        // Alarm list

        if (alarm_list_container_) {

            lv_obj_set_size(alarm_list_container_, 224, 118);

            lv_obj_set_pos(alarm_list_container_, 8, 198);

        }

    } else {

        // 320 x 240 (Landscape): Trái là 2 cột số xoay hạ thấp xuống y = 44 không che nút quay về ở y = 6; Phải là danh sách báo thức rộng 168px

        if (timer_back_btn_) {

            lv_obj_set_size(timer_back_btn_, 34, 28);

            lv_obj_set_pos(timer_back_btn_, 6, 6);

        }

        if (title_lbl) lv_obj_set_pos(title_lbl, 44, 10);



        // 2 Roller hạ thấp xuống y = 44 (cách đáy nút quay về 10px), không che nút quay về

        if (alarm_hour_roller_) {

            lv_obj_set_size(alarm_hour_roller_, 58, 80);

            lv_obj_set_pos(alarm_hour_roller_, 10, 44);

        }

        if (colon_lbl) lv_obj_set_pos(colon_lbl, 71, 74);

        if (alarm_min_roller_) {

            lv_obj_set_size(alarm_min_roller_, 58, 80);

            lv_obj_set_pos(alarm_min_roller_, 80, 44);

        }



        // 7 nút thứ chia làm 2 hàng to dễ bấm:

        // Hàng 1 (4 nút): CN, T2, T3, T4

        for (int i = 0; i < 4; ++i) {

            if (alarm_day_btns_[i]) {

                lv_obj_set_size(alarm_day_btns_[i], 31, 26);

                lv_obj_set_pos(alarm_day_btns_[i], 8 + i * 35, 130);

            }

        }

        // Hàng 2 (3 nút): T5, T6, T7

        for (int i = 4; i < 7; ++i) {

            if (alarm_day_btns_[i]) {

                lv_obj_set_size(alarm_day_btns_[i], 42, 26);

                lv_obj_set_pos(alarm_day_btns_[i], 8 + (i - 4) * 47, 160);

            }

        }



        // Add button

        if (alarm_add_btn_) {

            lv_obj_set_size(alarm_add_btn_, 138, 32);

            lv_obj_set_pos(alarm_add_btn_, 8, 194);

        }



        // Alarm list nới rộng lên width = 168px để hiển thị trọn vẹn "Hàng ngày"

        if (alarm_list_container_) {

            lv_obj_set_size(alarm_list_container_, 168, 196);

            lv_obj_set_pos(alarm_list_container_, 148, 38);

        }

    }

}



void LcdDisplay::UpdateTimerUI() {

    RenderAlarmList();

}



// ======================== EXCHANGE RATES SCREEN ========================

void LcdDisplay::CreateExchangeRatesUI() {

    auto screen = lv_screen_active();

    rates_box_ = lv_obj_create(screen);

    lv_obj_set_size(rates_box_, width_, height_);

    lv_obj_set_style_bg_color(rates_box_, lv_color_hex(0x0f172a), 0);

    lv_obj_set_style_bg_opa(rates_box_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(rates_box_, 0, 0);

    lv_obj_set_style_border_width(rates_box_, 0, 0);

    lv_obj_set_style_pad_all(rates_box_, 0, 0);

    lv_obj_clear_flag(rates_box_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);



    // Back button

    rates_back_btn_ = lv_button_create(rates_box_);

    lv_obj_set_size(rates_back_btn_, 36, 32);

    lv_obj_set_style_bg_opa(rates_back_btn_, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(rates_back_btn_, 0, 0);

    lv_obj_set_style_pad_all(rates_back_btn_, 0, 0);

    lv_obj_set_ext_click_area(rates_back_btn_, 8);

    lv_obj_t* back_lbl = lv_label_create(rates_back_btn_);

    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);

    lv_obj_set_style_text_font(back_lbl, &font_material_symbols_30_4, 0);

    lv_obj_set_style_text_color(back_lbl, lv_color_white(), 0);

    lv_obj_align(back_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(rates_back_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;

        self->ShowMenu();

    }, LV_EVENT_CLICKED, this);



    // Refresh button

    rates_refresh_btn_ = lv_button_create(rates_box_);

    lv_obj_set_size(rates_refresh_btn_, 36, 32);

    lv_obj_set_style_bg_opa(rates_refresh_btn_, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(rates_refresh_btn_, 0, 0);

    lv_obj_set_style_pad_all(rates_refresh_btn_, 0, 0);

    lv_obj_set_ext_click_area(rates_refresh_btn_, 8);

    lv_obj_t* ref_lbl = lv_label_create(rates_refresh_btn_);

    lv_label_set_text(ref_lbl, MATERIAL_SYMBOLS_REFRESH);

    lv_obj_set_style_text_font(ref_lbl, &font_material_symbols_30_4, 0);

    lv_obj_set_style_text_color(ref_lbl, lv_color_white(), 0);

    lv_obj_align(ref_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(rates_refresh_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        self->UpdateExchangeRatesUI();

        self->ShowNotification("Đã làm mới dữ liệu tỉ giá!", 1200);

    }, LV_EVENT_CLICKED, this);



    // Title & Live Timestamp

    lv_obj_t* title_lbl = lv_label_create(rates_box_);

    lv_label_set_text(title_lbl, "Tỉ Giá Thị Trường");

    lv_obj_set_style_text_font(title_lbl, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);

    lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 10);



    rates_time_label_ = lv_label_create(rates_box_);

    lv_label_set_text(rates_time_label_, "Thời gian thực • Cập nhật: --:--");

    lv_obj_set_style_text_font(rates_time_label_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(rates_time_label_, lv_color_hex(0x90A4AE), 0);

    lv_obj_align(rates_time_label_, LV_ALIGN_TOP_MID, 0, 28);



    // 4 vùng dữ liệu: Vàng SJC, USD, Crypto, Xăng Dầu (đồng bộ tông màu màn hình chờ)

    const char* titles[4] = {"Vàng SJC", "USD", "Crypto", "Xăng Dầu"};



    for (int i = 0; i < 4; ++i) {

        auto& c = rate_cards_[i];

        c.card = lv_obj_create(rates_box_);

        lv_obj_set_style_bg_color(c.card, lv_color_hex(0x161D27), 0);

        lv_obj_set_style_bg_opa(c.card, LV_OPA_COVER, 0);

        lv_obj_set_style_radius(c.card, 8, 0);

        lv_obj_set_style_border_width(c.card, 1, 0);

        lv_obj_set_style_border_color(c.card, lv_color_hex(0x283548), 0);

        lv_obj_set_style_pad_all(c.card, 6, 0);

        lv_obj_clear_flag(c.card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_clear_flag(c.card, LV_OBJ_FLAG_CLICKABLE);



        c.title_lbl = lv_label_create(c.card);

        lv_label_set_text(c.title_lbl, titles[i]);

        lv_obj_set_style_text_font(c.title_lbl, &font_noto_sans_custom_14_1, 0);

        lv_obj_set_style_text_color(c.title_lbl, lv_color_hex(0x60A5FA), 0);



        c.val1_lbl = lv_label_create(c.card);

        lv_obj_set_style_text_font(c.val1_lbl, &font_noto_sans_custom_14_1, 0);

        lv_obj_set_style_text_color(c.val1_lbl, lv_color_white(), 0);



        c.val2_lbl = lv_label_create(c.card);

        lv_obj_set_style_text_font(c.val2_lbl, &font_noto_sans_custom_14_1, 0);

        lv_obj_set_style_text_color(c.val2_lbl, lv_color_hex(0xCBD5E1), 0);



        c.trend_lbl = lv_label_create(c.card);

        lv_obj_set_style_text_font(c.trend_lbl, &font_noto_sans_custom_14_1, 0);

        lv_obj_set_style_text_color(c.trend_lbl, lv_color_hex(0x34D399), 0);

    }



    LayoutExchangeRatesWidgets(height_ > width_);

}



void LcdDisplay::LayoutExchangeRatesWidgets(bool is_portrait) {

    if (rates_box_ == nullptr) return;

    lv_obj_set_size(rates_box_, width_, height_);



    if (is_portrait) {

        // 240 x 320: 4 thẻ 2 cột x 2 hàng

        if (rates_back_btn_) lv_obj_set_pos(rates_back_btn_, 6, 12);

        if (rates_refresh_btn_) lv_obj_set_pos(rates_refresh_btn_, 198, 12);

        if (rates_time_label_) lv_obj_align(rates_time_label_, LV_ALIGN_TOP_MID, 0, 30);



        int col_x[2] = {6, 122};

        int row_y[2] = {48, 180};

        for (int i = 0; i < 4; ++i) {

            auto& c = rate_cards_[i];

            if (!c.card) continue;

            lv_obj_set_size(c.card, 112, 126);

            lv_obj_set_pos(c.card, col_x[i % 2], row_y[i / 2]);

            if (c.title_lbl) lv_obj_set_pos(c.title_lbl, 4, 4);

            if (c.val1_lbl) lv_obj_set_pos(c.val1_lbl, 4, 30);

            if (c.val2_lbl) lv_obj_set_pos(c.val2_lbl, 4, 56);

            if (c.trend_lbl) lv_obj_set_pos(c.trend_lbl, 4, 86);

        }

    } else {

        // 320 x 240: 4 thẻ 2 cột x 2 hàng

        if (rates_back_btn_) lv_obj_set_pos(rates_back_btn_, 6, 6);

        if (rates_refresh_btn_) lv_obj_set_pos(rates_refresh_btn_, 278, 6);

        if (rates_time_label_) lv_obj_align(rates_time_label_, LV_ALIGN_TOP_MID, 0, 24);



        int col_x[2] = {8, 164};

        int row_y[2] = {42, 138};

        for (int i = 0; i < 4; ++i) {

            auto& c = rate_cards_[i];

            if (!c.card) continue;

            lv_obj_set_size(c.card, 148, 90);

            lv_obj_set_pos(c.card, col_x[i % 2], row_y[i / 2]);

            if (c.title_lbl) lv_obj_set_pos(c.title_lbl, 6, 4);

            if (c.val1_lbl) lv_obj_set_pos(c.val1_lbl, 6, 24);

            if (c.val2_lbl) lv_obj_set_pos(c.val2_lbl, 6, 44);

            if (c.trend_lbl) lv_obj_set_pos(c.trend_lbl, 6, 64);

        }

    }

}



void LcdDisplay::UpdateExchangeRatesUI() {

    if (rates_box_ == nullptr) return;

    time_t raw_time = time(nullptr);

    struct tm* t = localtime(&raw_time);

    if (t && rates_time_label_) {

        char buf[64];

        snprintf(buf, sizeof(buf), "Cập nhật trực tiếp: %02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);

        lv_label_set_text(rates_time_label_, buf);

    }



    // 1. Vàng SJC (triệu đồng / lượng)

    if (rate_cards_[0].val1_lbl) lv_label_set_text(rate_cards_[0].val1_lbl, "Mua: 138.0 tr");

    if (rate_cards_[0].val2_lbl) lv_label_set_text(rate_cards_[0].val2_lbl, "Bán: 141.0 tr");

    if (rate_cards_[0].trend_lbl) lv_label_set_text(rate_cards_[0].trend_lbl, "+0.5% (Tăng)");



    // 2. USD Vietcombank (VND / USD)

    if (rate_cards_[1].val1_lbl) lv_label_set_text(rate_cards_[1].val1_lbl, "Mua: 25.400");

    if (rate_cards_[1].val2_lbl) lv_label_set_text(rate_cards_[1].val2_lbl, "Bán: 25.780");

    if (rate_cards_[1].trend_lbl) lv_label_set_text(rate_cards_[1].trend_lbl, "+20 (Tăng)");



    // 3. Crypto (USD)

    if (rate_cards_[2].val1_lbl) lv_label_set_text(rate_cards_[2].val1_lbl, "BTC: $75,800");

    if (rate_cards_[2].val2_lbl) lv_label_set_text(rate_cards_[2].val2_lbl, "ETH: $2,240");

    if (rate_cards_[2].trend_lbl) lv_label_set_text(rate_cards_[2].trend_lbl, "+1.2% (Tăng)");



    // 4. Xăng dầu (VND / lít)

    if (rate_cards_[3].val1_lbl) lv_label_set_text(rate_cards_[3].val1_lbl, "RON95: 21.15k");

    if (rate_cards_[3].val2_lbl) lv_label_set_text(rate_cards_[3].val2_lbl, "E5: 20.08k");

    if (rate_cards_[3].trend_lbl) lv_label_set_text(rate_cards_[3].trend_lbl, "DO: 18.95k");

}



void LcdDisplay::OnCalculatorButton(const char* key) {

    if (key == nullptr) return;



    if (strcmp(key, "C") == 0) {

        calc_curr_input_ = "0";

        calc_first_val_ = 0.0;

        calc_pending_op_ = 0;

        calc_waiting_second_ = false;

        if (calc_expr_label_) lv_label_set_text(calc_expr_label_, "");

        if (calc_result_label_) lv_label_set_text(calc_result_label_, "0");

        return;

    }



    if (strcmp(key, "DEL") == 0) {

        if (!calc_waiting_second_ && calc_curr_input_.length() > 0) {

            calc_curr_input_.pop_back();

            if (calc_curr_input_.empty() || calc_curr_input_ == "-") {

                calc_curr_input_ = "0";

            }

            if (calc_result_label_) lv_label_set_text(calc_result_label_, calc_curr_input_.c_str());

        }

        return;

    }



    if (strcmp(key, "+/-") == 0) {

        if (calc_curr_input_ != "0") {

            if (calc_curr_input_[0] == '-') {

                calc_curr_input_.erase(0, 1);

            } else {

                calc_curr_input_ = "-" + calc_curr_input_;

            }

            if (calc_result_label_) lv_label_set_text(calc_result_label_, calc_curr_input_.c_str());

        }

        return;

    }



    if (strcmp(key, "%") == 0) {

        double v = atof(calc_curr_input_.c_str()) / 100.0;

        char buf[32];

        snprintf(buf, sizeof(buf), "%g", v);

        calc_curr_input_ = buf;

        if (calc_result_label_) lv_label_set_text(calc_result_label_, calc_curr_input_.c_str());

        return;

    }



    if (strcmp(key, "+") == 0 || strcmp(key, "-") == 0 || strcmp(key, "x") == 0 || strcmp(key, "/") == 0) {

        calc_first_val_ = atof(calc_curr_input_.c_str());

        calc_pending_op_ = key[0];

        calc_waiting_second_ = true;

        char expr[48];

        snprintf(expr, sizeof(expr), "%s %c", calc_curr_input_.c_str(), calc_pending_op_);

        if (calc_expr_label_) lv_label_set_text(calc_expr_label_, expr);

        return;

    }



    if (strcmp(key, "=") == 0) {

        if (calc_pending_op_ != 0) {

            double second_val = atof(calc_curr_input_.c_str());

            double res = 0.0;

            bool err = false;

            if (calc_pending_op_ == '+') res = calc_first_val_ + second_val;

            else if (calc_pending_op_ == '-') res = calc_first_val_ - second_val;

            else if (calc_pending_op_ == 'x') res = calc_first_val_ * second_val;

            else if (calc_pending_op_ == '/') {

                if (second_val == 0.0) err = true;

                else res = calc_first_val_ / second_val;

            }



            char expr[64];

            snprintf(expr, sizeof(expr), "%g %c %g =", calc_first_val_, calc_pending_op_, second_val);

            if (calc_expr_label_) lv_label_set_text(calc_expr_label_, expr);



            if (err) {

                calc_curr_input_ = "0";

                if (calc_result_label_) lv_label_set_text(calc_result_label_, "Lỗi chia 0");

            } else {

                char res_buf[32];

                snprintf(res_buf, sizeof(res_buf), "%g", res);

                calc_curr_input_ = res_buf;

                if (calc_result_label_) lv_label_set_text(calc_result_label_, calc_curr_input_.c_str());

            }

            calc_pending_op_ = 0;

            calc_waiting_second_ = true;

        }

        return;

    }



    if (strcmp(key, ".") == 0) {

        if (calc_waiting_second_) {

            calc_curr_input_ = "0.";

            calc_waiting_second_ = false;

        } else if (calc_curr_input_.find('.') == std::string::npos) {

            calc_curr_input_ += ".";

        }

        if (calc_result_label_) lv_label_set_text(calc_result_label_, calc_curr_input_.c_str());

        return;

    }



    if (calc_waiting_second_ || calc_curr_input_ == "0") {

        calc_curr_input_ = key;

        calc_waiting_second_ = false;

    } else {

        if (calc_curr_input_.length() < 12) {

            calc_curr_input_ += key;

        }

    }

    if (calc_result_label_) lv_label_set_text(calc_result_label_, calc_curr_input_.c_str());

}



void LcdDisplay::CreateCalculatorUI() {

    auto screen = lv_screen_active();

    calc_box_ = lv_obj_create(screen);

    lv_obj_set_size(calc_box_, width_, height_);

    lv_obj_set_style_bg_color(calc_box_, lv_color_hex(0x0a0f1d), 0);

    lv_obj_set_style_bg_opa(calc_box_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(calc_box_, 0, 0);

    lv_obj_set_style_border_width(calc_box_, 0, 0);

    lv_obj_set_style_pad_all(calc_box_, 0, 0);

    lv_obj_clear_flag(calc_box_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);



    calc_back_btn_ = lv_button_create(calc_box_);

    lv_obj_set_size(calc_back_btn_, 36, 32);

    lv_obj_set_style_bg_opa(calc_back_btn_, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(calc_back_btn_, 0, 0);

    lv_obj_set_style_pad_all(calc_back_btn_, 0, 0);

    lv_obj_set_ext_click_area(calc_back_btn_, 8);

    lv_obj_t* back_lbl = lv_label_create(calc_back_btn_);

    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);

    lv_obj_set_style_text_font(back_lbl, &font_material_symbols_30_4, 0);

    lv_obj_set_style_text_color(back_lbl, lv_color_white(), 0);

    lv_obj_align(back_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(calc_back_btn_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;

        self->ShowMenu();

    }, LV_EVENT_CLICKED, this);



    lv_obj_t* title_lbl = lv_label_create(calc_box_);

    lv_label_set_text(title_lbl, "Máy tính");

    lv_obj_set_style_text_font(title_lbl, &font_noto_sans_custom_16_4, 0);

    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);

    lv_obj_set_user_data(title_lbl, (void*)(intptr_t)501);



    lv_obj_t* disp_card = lv_obj_create(calc_box_);

    lv_obj_set_style_bg_color(disp_card, lv_color_hex(0x151e2e), 0);

    lv_obj_set_style_bg_opa(disp_card, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(disp_card, 8, 0);

    lv_obj_set_style_border_width(disp_card, 1, 0);

    lv_obj_set_style_border_color(disp_card, lv_color_hex(0x283548), 0);

    lv_obj_set_style_pad_all(disp_card, 4, 0);

    lv_obj_clear_flag(disp_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_user_data(disp_card, (void*)(intptr_t)502);



    calc_expr_label_ = lv_label_create(disp_card);

    lv_label_set_text(calc_expr_label_, "");

    lv_obj_set_style_text_font(calc_expr_label_, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(calc_expr_label_, lv_color_hex(0x94A3B8), 0);



    calc_result_label_ = lv_label_create(disp_card);

    lv_label_set_text(calc_result_label_, "0");

    lv_obj_set_style_text_font(calc_result_label_, &font_noto_sans_basic_20_4, 0);

    lv_obj_set_style_text_color(calc_result_label_, lv_color_white(), 0);



    const char* btn_labels[20] = {

        "C", "DEL", "%", "/",

        "7", "8", "9", "x",

        "4", "5", "6", "-",

        "1", "2", "3", "+",

        "+/-", "0", ".", "="

    };



    for (int i = 0; i < 20; ++i) {

        lv_obj_t* btn = lv_button_create(calc_box_);

        lv_obj_set_style_radius(btn, 6, 0);

        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);



        bool is_op = (i == 3 || i == 7 || i == 11 || i == 15);

        bool is_eq = (i == 19);

        bool is_func = (i < 3 || i == 16);



        if (is_eq) {

            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2563EB), 0);

        } else if (is_op) {

            lv_obj_set_style_bg_color(btn, lv_color_hex(0xD97706), 0);

        } else if (is_func) {

            lv_obj_set_style_bg_color(btn, lv_color_hex(0x334155), 0);

        } else {

            lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E293B), 0);

        }

        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);



        lv_obj_t* lbl = lv_label_create(btn);

        lv_label_set_text(lbl, btn_labels[i]);

        lv_obj_set_style_text_font(lbl, &font_noto_sans_basic_16_4, 0);

        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);



        lv_obj_set_user_data(btn, (void*)(intptr_t)(600 + i));

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {

            auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

            int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e)) - 600;

            const char* labels[20] = {

                "C", "DEL", "%", "/",

                "7", "8", "9", "x",

                "4", "5", "6", "-",

                "1", "2", "3", "+",

                "+/-", "0", ".", "="

            };

            if (idx >= 0 && idx < 20) {

                self->OnCalculatorButton(labels[idx]);

            }

        }, LV_EVENT_CLICKED, this);

    }



    LayoutCalculatorWidgets(height_ > width_);

}



void LcdDisplay::LayoutCalculatorWidgets(bool is_portrait) {

    if (calc_box_ == nullptr) return;

    lv_obj_set_size(calc_box_, width_, height_);



    lv_obj_t* title_lbl = nullptr;

    lv_obj_t* disp_card = nullptr;

    uint32_t cnt = lv_obj_get_child_count(calc_box_);

    for (uint32_t i = 0; i < cnt; ++i) {

        lv_obj_t* child = lv_obj_get_child(calc_box_, i);

        intptr_t id = (intptr_t)lv_obj_get_user_data(child);

        if (id == 501) title_lbl = child;

        else if (id == 502) disp_card = child;

    }



    if (is_portrait) {

        // 240 x 320: Hiển thị gọn đẹp, nút bấm to dễ chạm

        if (calc_back_btn_) lv_obj_set_pos(calc_back_btn_, 6, 6);

        if (title_lbl) lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 8);



        if (disp_card) {

            lv_obj_set_size(disp_card, 224, 46);

            lv_obj_set_pos(disp_card, 8, 36);

            if (calc_expr_label_) {

                lv_obj_set_width(calc_expr_label_, 214);

                lv_obj_set_pos(calc_expr_label_, 4, 1);

            }

            if (calc_result_label_) {

                lv_obj_set_width(calc_result_label_, 214);

                lv_obj_set_pos(calc_result_label_, 4, 18);

            }

        }



        // 20 buttons (5 rows x 4 cols)

        for (uint32_t i = 0; i < cnt; ++i) {

            lv_obj_t* child = lv_obj_get_child(calc_box_, i);

            int id = (int)(intptr_t)lv_obj_get_user_data(child);

            if (id >= 600 && id < 620) {

                int btn_idx = id - 600;

                int r = btn_idx / 4;

                int c = btn_idx % 4;

                lv_obj_set_size(child, 52, 40);

                lv_obj_set_pos(child, 8 + c * 57, 88 + r * 45);

            }

        }

    } else {

        // 320 x 240: Màn hình ngang

        if (calc_back_btn_) lv_obj_set_pos(calc_back_btn_, 6, 4);

        if (title_lbl) lv_obj_set_pos(title_lbl, 46, 6);



        if (disp_card) {

            lv_obj_set_size(disp_card, 130, 194);

            lv_obj_set_pos(disp_card, 8, 36);

            if (calc_expr_label_) {

                lv_obj_set_width(calc_expr_label_, 120);

                lv_obj_set_pos(calc_expr_label_, 4, 10);

            }

            if (calc_result_label_) {

                lv_obj_set_width(calc_result_label_, 120);

                lv_obj_set_pos(calc_result_label_, 4, 40);

            }

        }



        for (uint32_t i = 0; i < cnt; ++i) {

            lv_obj_t* child = lv_obj_get_child(calc_box_, i);

            int id = (int)(intptr_t)lv_obj_get_user_data(child);

            if (id >= 600 && id < 620) {

                int btn_idx = id - 600;

                int r = btn_idx / 4;

                int c = btn_idx % 4;

                lv_obj_set_size(child, 38, 34);

                lv_obj_set_pos(child, 146 + c * 42, 34 + r * 39);

            }

        }

    }

}



void LcdDisplay::CreateFlashlightUI() {

    auto screen = lv_screen_active();

    flashlight_box_ = lv_obj_create(screen);

    lv_obj_set_size(flashlight_box_, LV_PCT(100), LV_PCT(100));

    lv_obj_set_pos(flashlight_box_, 0, 0);

    lv_obj_set_style_bg_color(flashlight_box_, lv_color_white(), 0);

    lv_obj_set_style_bg_opa(flashlight_box_, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(flashlight_box_, 0, 0);

    lv_obj_set_style_border_width(flashlight_box_, 0, 0);

    lv_obj_set_style_pad_all(flashlight_box_, 0, 0);

    lv_obj_clear_flag(flashlight_box_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);



    lv_obj_t* hint = lv_label_create(flashlight_box_);

    lv_label_set_text(hint, "CHẠM HOẶC VUỐT ĐỂ TẮT");

    lv_obj_set_style_text_font(hint, &font_noto_sans_custom_14_1, 0);

    lv_obj_set_style_text_color(hint, lv_color_hex(0x94A3B8), 0);

    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);



    lv_obj_add_event_cb(flashlight_box_, [](lv_event_t* e) {

        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));

        if (Board::GetInstance().GetBacklight()) {

            Board::GetInstance().GetBacklight()->SetBrightness(self->saved_flashlight_brightness_, false);

        }

        lv_obj_set_style_bg_color(lv_screen_active(), self->is_light_theme_ ? lv_color_hex(0xF8FAFC) : lv_color_black(), 0);

        self->ShowMenu();

    }, LV_EVENT_CLICKED, this);

}



void LcdDisplay::StartBootAnimation() {
    if (&boot_anim_bin_start[0] >= &boot_anim_bin_end[0]) {
        boot_anim_running_ = false;
        return;
    }
    uint32_t total = *(const uint32_t*)boot_anim_bin_start;
    if (total == 0 || total > 100) {
        boot_anim_running_ = false;
        return;
    }
    boot_anim_running_ = true;

    struct BootAnimContext {
        int frame_index = 0;
        int total_frames = 0;
        const uint32_t* sizes = nullptr;
        const uint8_t* current_ptr = nullptr;
        uint8_t* decoded_buf = nullptr;
        lv_image_dsc_t frame_dsc;
        LcdDisplay* display = nullptr;
    };

    auto* ctx = new BootAnimContext();
    ctx->total_frames = (int)total;
    ctx->sizes = (const uint32_t*)(boot_anim_bin_start + 4);
    ctx->current_ptr = boot_anim_bin_start + 4 + total * 4;
    ctx->display = this;
    memset(&ctx->frame_dsc, 0, sizeof(ctx->frame_dsc));

    lv_timer_create([](lv_timer_t* t) {
        auto* ctx = static_cast<BootAnimContext*>(lv_timer_get_user_data(t));
        if (ctx == nullptr) {
            lv_timer_delete(t);
            return;
        }

        if (ctx->frame_index >= ctx->total_frames) {
            if (ctx->display && ctx->display->wallpaper_image_) {
                const lv_image_dsc_t* boot_dsc = (ctx->display->width_ > ctx->display->height_) ?
                                                  &boot_landscape_dsc : &boot_portrait_dsc;
                lv_image_set_src(ctx->display->wallpaper_image_, boot_dsc);
            }
            if (ctx->decoded_buf) {
                heap_caps_free(ctx->decoded_buf);
                ctx->decoded_buf = nullptr;
            }
            if (ctx->display) {
                ctx->display->boot_anim_running_ = false;
            }
            lv_timer_delete(t);
            delete ctx;
            return;
        }

        uint32_t frame_len = ctx->sizes[ctx->frame_index];
        const uint8_t* frame_jpeg = ctx->current_ptr;
        ctx->current_ptr += frame_len;

        uint8_t* new_buf = nullptr;
        size_t out_len = 0, w = 0, h = 0, stride = 0;
        esp_err_t err = jpeg_to_image(frame_jpeg, frame_len, &new_buf, &out_len, &w, &h, &stride);
        if (err == ESP_OK && new_buf != nullptr) {
            ctx->frame_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            ctx->frame_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
            ctx->frame_dsc.header.flags = 0;
            ctx->frame_dsc.header.w = (uint32_t)w;
            ctx->frame_dsc.header.h = (uint32_t)h;
            ctx->frame_dsc.header.stride = (uint32_t)stride;
            ctx->frame_dsc.data_size = out_len;
            ctx->frame_dsc.data = new_buf;

            if (ctx->display && ctx->display->wallpaper_image_) {
                lv_image_set_src(ctx->display->wallpaper_image_, &ctx->frame_dsc);
            }
            if (ctx->decoded_buf) {
                heap_caps_free(ctx->decoded_buf);
            }
            ctx->decoded_buf = new_buf;
        }
        ctx->frame_index++;
    }, 90, ctx);
}

void LcdDisplay::WaitForBootAnimation() {
    int max_wait_ms = 8000;
    int elapsed = 0;
    while (boot_anim_running_ && elapsed < max_wait_ms) {
        vTaskDelay(pdMS_TO_TICKS(50));
        elapsed += 50;
    }
    boot_anim_running_ = false;
}

void LcdDisplay::CreateFmRadioUI() {
    auto screen = lv_screen_active();
    fm_box_ = lv_obj_create(screen);
    lv_obj_set_size(fm_box_, width_, height_);
    lv_obj_set_style_bg_color(fm_box_, lv_color_hex(0x0a0f1d), 0);
    lv_obj_set_style_bg_opa(fm_box_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fm_box_, 0, 0);
    lv_obj_set_style_border_width(fm_box_, 0, 0);
    lv_obj_set_style_pad_all(fm_box_, 0, 0);
    lv_obj_clear_flag(fm_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);

    // Back button
    fm_back_btn_ = lv_button_create(fm_box_);
    lv_obj_set_size(fm_back_btn_, 36, 32);
    lv_obj_set_style_bg_opa(fm_back_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fm_back_btn_, 0, 0);
    lv_obj_set_style_pad_all(fm_back_btn_, 0, 0);
    lv_obj_set_ext_click_area(fm_back_btn_, 8);

    lv_obj_t* back_lbl = lv_label_create(fm_back_btn_);
    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);
    lv_obj_set_style_text_font(back_lbl, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_white(), 0);
    lv_obj_align(back_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(fm_back_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
        auto music = Board::GetInstance().GetMusic();
        if (music && self->fm_is_playing_) {
            self->fm_is_playing_ = false;
            std::thread([music]() {
                music->StopStreaming();
            }).detach();
        }
        self->ShowMenu();
    }, LV_EVENT_CLICKED, this);

    // Title label
    fm_title_lbl_ = lv_label_create(fm_box_);
    lv_label_set_text(fm_title_lbl_, "Đài FM Trực Tuyến");
    lv_obj_set_style_text_font(fm_title_lbl_, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(fm_title_lbl_, lv_color_white(), 0);

    // 4 Channel Buttons (2 columns x 2 rows, large touch targets)
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* btn = lv_button_create(fm_box_);
        fm_channel_btns_[i] = btn;
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x334155), 0);
        lv_obj_set_style_pad_all(btn, 6, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* name_lbl = lv_label_create(btn);
        lv_label_set_text(name_lbl, kFmChannels[i].name);
        lv_obj_set_style_text_font(name_lbl, &font_noto_sans_custom_16_4, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_white(), 0);
        lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 6, 4);

        lv_obj_t* freq_lbl = lv_label_create(btn);
        lv_label_set_text(freq_lbl, kFmChannels[i].freq);
        lv_obj_set_style_text_font(freq_lbl, &font_noto_sans_custom_14_1, 0);
        lv_obj_set_style_text_color(freq_lbl, lv_color_hex(0x38BDF8), 0);
        lv_obj_align(freq_lbl, LV_ALIGN_BOTTOM_LEFT, 6, -4);

        lv_obj_set_user_data(btn, (void*)(intptr_t)(800 + i));
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e)) - 800;
            if (idx >= 0 && idx < 4) {
                self->fm_current_channel_ = idx;
                self->fm_is_playing_ = true;
                auto music = Board::GetInstance().GetMusic();
                if (music) {
                    std::string url = self->kFmChannels[idx].url;
                    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
                    cfg.stack_size = 8192;
                    cfg.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
                    cfg.thread_name = "fm_start";
                    esp_pthread_set_cfg(&cfg);
                    std::thread([music, url]() {
                        music->StartStreaming(url);
                    }).detach();
                }
                self->UpdateFmRadioUI();
            }
        }, LV_EVENT_CLICKED, this);
    }

    // Bottom Control Panel: contains Channel Name, Live Status, and Large "Dừng phát" Button
    fm_bottom_card_ = lv_obj_create(fm_box_);
    lv_obj_set_style_bg_color(fm_bottom_card_, lv_color_hex(0x151e2e), 0);
    lv_obj_set_style_bg_opa(fm_bottom_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fm_bottom_card_, 10, 0);
    lv_obj_set_style_border_width(fm_bottom_card_, 1, 0);
    lv_obj_set_style_border_color(fm_bottom_card_, lv_color_hex(0x283548), 0);
    lv_obj_set_style_pad_all(fm_bottom_card_, 6, 0);
    lv_obj_clear_flag(fm_bottom_card_, LV_OBJ_FLAG_SCROLLABLE);

    fm_channel_lbl_ = lv_label_create(fm_bottom_card_);
    lv_label_set_text(fm_channel_lbl_, "Chưa chọn đài");
    lv_obj_set_style_text_font(fm_channel_lbl_, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(fm_channel_lbl_, lv_color_hex(0x60A5FA), 0);

    fm_status_lbl_ = lv_label_create(fm_bottom_card_);
    lv_label_set_text(fm_status_lbl_, "Chọn đài ở trên");
    lv_obj_set_style_text_font(fm_status_lbl_, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(fm_status_lbl_, lv_color_hex(0x94A3B8), 0);

    fm_play_btn_ = lv_button_create(fm_bottom_card_);
    lv_obj_set_size(fm_play_btn_, 100, 42);
    lv_obj_set_style_bg_color(fm_play_btn_, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_bg_opa(fm_play_btn_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fm_play_btn_, 8, 0);
    lv_obj_set_style_pad_all(fm_play_btn_, 0, 0);

    fm_play_lbl_ = lv_label_create(fm_play_btn_);
    lv_label_set_text(fm_play_lbl_, "Phát");
    lv_obj_set_style_text_font(fm_play_lbl_, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(fm_play_lbl_, lv_color_white(), 0);
    lv_obj_align(fm_play_lbl_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(fm_play_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (self->fm_current_channel_ < 0) {
            self->ShowNotification("Vui lòng chọn 1 đài", 1500);
            return;
        }
        auto music = Board::GetInstance().GetMusic();
        if (!music) return;

        if (self->fm_is_playing_) {
            self->fm_is_playing_ = false;
            std::thread([music]() {
                music->StopStreaming();
            }).detach();
        } else {
            self->fm_is_playing_ = true;
            std::string url = self->kFmChannels[self->fm_current_channel_].url;
            esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
            cfg.stack_size = 8192;
            cfg.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
            cfg.thread_name = "fm_start";
            esp_pthread_set_cfg(&cfg);
            std::thread([music, url]() {
                music->StartStreaming(url);
            }).detach();
        }
        self->UpdateFmRadioUI();
    }, LV_EVENT_CLICKED, this);

    LayoutFmRadioWidgets(height_ > width_);
}

void LcdDisplay::LayoutFmRadioWidgets(bool is_portrait) {
    if (fm_box_ == nullptr) return;
    lv_obj_set_size(fm_box_, width_, height_);

    if (is_portrait) {
        // Portrait: 240 x 320
        if (fm_back_btn_) lv_obj_set_pos(fm_back_btn_, 6, 6);
        if (fm_title_lbl_) lv_obj_align(fm_title_lbl_, LV_ALIGN_TOP_MID, 0, 10);

        // 4 Channels: 2 cols x 2 rows
        int col_x[2] = {8, 124};
        int row_y[2] = {42, 126};
        for (int i = 0; i < 4; ++i) {
            if (fm_channel_btns_[i]) {
                lv_obj_set_size(fm_channel_btns_[i], 108, 76);
                lv_obj_set_pos(fm_channel_btns_[i], col_x[i % 2], row_y[i / 2]);
            }
        }

        // Bottom card
        if (fm_bottom_card_) {
            lv_obj_set_size(fm_bottom_card_, 224, 100);
            lv_obj_set_pos(fm_bottom_card_, 8, 210);
            if (fm_channel_lbl_) {
                lv_obj_set_pos(fm_channel_lbl_, 6, 6);
                lv_obj_set_width(fm_channel_lbl_, 212);
                lv_label_set_long_mode(fm_channel_lbl_, LV_LABEL_LONG_DOT);
            }
            if (fm_status_lbl_) {
                lv_obj_set_pos(fm_status_lbl_, 6, 30);
                lv_obj_set_width(fm_status_lbl_, 212);
                lv_label_set_long_mode(fm_status_lbl_, LV_LABEL_LONG_DOT);
            }
            if (fm_play_btn_) {
                lv_obj_set_size(fm_play_btn_, 140, 38);
                lv_obj_align(fm_play_btn_, LV_ALIGN_BOTTOM_MID, 0, -6);
            }
        }
    } else {
        // Landscape: 320 x 240
        if (fm_back_btn_) lv_obj_set_pos(fm_back_btn_, 6, 4);
        if (fm_title_lbl_) lv_obj_align(fm_title_lbl_, LV_ALIGN_TOP_MID, 0, 6);

        // 4 Channels: 2 cols x 2 rows, big and wide!
        int col_x[2] = {10, 165};
        int row_y[2] = {36, 102};
        for (int i = 0; i < 4; ++i) {
            if (fm_channel_btns_[i]) {
                lv_obj_set_size(fm_channel_btns_[i], 145, 62);
                lv_obj_set_pos(fm_channel_btns_[i], col_x[i % 2], row_y[i / 2]);
            }
        }

        // Bottom control card
        if (fm_bottom_card_) {
            lv_obj_set_size(fm_bottom_card_, 300, 62);
            lv_obj_set_pos(fm_bottom_card_, 10, 170);
            if (fm_channel_lbl_) {
                lv_obj_set_pos(fm_channel_lbl_, 10, 8);
                lv_obj_set_width(fm_channel_lbl_, 170);
                lv_label_set_long_mode(fm_channel_lbl_, LV_LABEL_LONG_DOT);
            }
            if (fm_status_lbl_) {
                lv_obj_set_pos(fm_status_lbl_, 10, 32);
                lv_obj_set_width(fm_status_lbl_, 170);
                lv_label_set_long_mode(fm_status_lbl_, LV_LABEL_LONG_DOT);
            }
            if (fm_play_btn_) {
                lv_obj_set_size(fm_play_btn_, 100, 42);
                lv_obj_align(fm_play_btn_, LV_ALIGN_RIGHT_MID, -8, 0);
            }
        }
    }
}

void LcdDisplay::UpdateFmRadioUI() {
    if (fm_box_ == nullptr) return;

    if (fm_current_channel_ >= 0 && fm_current_channel_ < 4) {
        if (fm_channel_lbl_) {
            lv_label_set_text(fm_channel_lbl_, kFmChannels[fm_current_channel_].name);
        }
        if (fm_status_lbl_) {
            lv_label_set_text(fm_status_lbl_, fm_is_playing_ ? "Đang phát..." : "Đã dừng");
            lv_obj_set_style_text_color(fm_status_lbl_, fm_is_playing_ ? lv_color_hex(0x4ADE80) : lv_color_hex(0x94A3B8), 0);
        }
        if (fm_play_lbl_) {
            lv_label_set_text(fm_play_lbl_, fm_is_playing_ ? "Dừng phát" : "Phát");
        }
        if (fm_play_btn_) {
            lv_obj_set_style_bg_color(fm_play_btn_, fm_is_playing_ ? lv_color_hex(0xDC2626) : lv_color_hex(0x2563EB), 0);
        }
    } else {
        if (fm_channel_lbl_) lv_label_set_text(fm_channel_lbl_, "Chưa chọn đài");
        if (fm_status_lbl_) {
            lv_label_set_text(fm_status_lbl_, "Chọn đài ở trên");
            lv_obj_set_style_text_color(fm_status_lbl_, lv_color_hex(0x94A3B8), 0);
        }
        if (fm_play_lbl_) lv_label_set_text(fm_play_lbl_, "Phát");
        if (fm_play_btn_) {
            lv_obj_set_style_bg_color(fm_play_btn_, lv_color_hex(0x2563EB), 0);
        }
    }

    // Highlight active channel button
    for (int i = 0; i < 4; ++i) {
        if (!fm_channel_btns_[i]) continue;
        if (i == fm_current_channel_) {
            lv_obj_set_style_border_color(fm_channel_btns_[i], lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_border_width(fm_channel_btns_[i], 2, 0);
            lv_obj_set_style_bg_color(fm_channel_btns_[i], lv_color_hex(0x1E3A8A), 0);
        } else {
            lv_obj_set_style_border_color(fm_channel_btns_[i], lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(fm_channel_btns_[i], 1, 0);
            lv_obj_set_style_bg_color(fm_channel_btns_[i], lv_color_hex(0x1E293B), 0);
        }
    }
}



// ==================== MUSIC PLAYER SCREEN ====================
static const uint32_t kSpectrumColors[16] = {
    0x06B6D4, 0x0EA5E9, 0x38BDF8, 0x10B981, 
    0x34D399, 0xFBBF24, 0xF59E0B, 0xF97316, 
    0xEF4444, 0xEC4899, 0xD946EF, 0xA855F7, 
    0x8B5CF6, 0x6366F1, 0x38BDF8, 0x10B981
};

void LcdDisplay::CreateMusicScreenUI() {
    if (music_box_ != nullptr) return;

    auto screen = lv_screen_active();
    music_box_ = lv_obj_create(screen);
    lv_obj_set_size(music_box_, width_, height_);
    lv_obj_set_pos(music_box_, 0, 0);
    lv_obj_set_style_bg_color(music_box_, lv_color_hex(0x0b0f19), 0);
    lv_obj_set_style_bg_opa(music_box_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(music_box_, 0, 0);
    lv_obj_set_style_border_width(music_box_, 0, 0);
    lv_obj_clear_flag(music_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);

    // 1. Nút quay lại [✕]
    music_back_btn_ = lv_button_create(music_box_);
    lv_obj_set_size(music_back_btn_, 32, 32);
    lv_obj_set_style_bg_color(music_back_btn_, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_radius(music_back_btn_, 16, 0);
    lv_obj_set_style_pad_all(music_back_btn_, 0, 0);
    lv_obj_set_ext_click_area(music_back_btn_, 8);
    lv_obj_t* back_lbl = lv_label_create(music_back_btn_);
    lv_label_set_text(back_lbl, "X");
    lv_obj_set_style_text_font(back_lbl, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_white(), 0);
    lv_obj_align(back_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(music_back_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        self->ShowMenu();
    }, LV_EVENT_CLICKED, this);

    // 2. Tiêu đề
    music_title_badge_ = lv_label_create(music_box_);
    lv_label_set_text(music_title_badge_, "Đang phát nhạc");
    lv_obj_set_style_text_font(music_title_badge_, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(music_title_badge_, lv_color_hex(0x38bdf8), 0);

    // 3. Đĩa than Vinyl cổ điển
    music_vinyl_ = lv_obj_create(music_box_);
    lv_obj_set_style_bg_color(music_vinyl_, lv_color_hex(0x111827), 0);
    lv_obj_set_style_border_color(music_vinyl_, lv_color_hex(0x374151), 0);
    lv_obj_set_style_border_width(music_vinyl_, 3, 0);
    lv_obj_set_style_pad_all(music_vinyl_, 0, 0);
    lv_obj_clear_flag(music_vinyl_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* groove = lv_obj_create(music_vinyl_);
    lv_obj_set_size(groove, 68, 68);
    lv_obj_align(groove, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(groove, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(groove, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_border_width(groove, 1, 0);
    lv_obj_set_style_radius(groove, 34, 0);
    lv_obj_clear_flag(groove, LV_OBJ_FLAG_SCROLLABLE);

    music_vinyl_label_ = lv_obj_create(music_vinyl_);
    lv_obj_set_size(music_vinyl_label_, 42, 42);
    lv_obj_align(music_vinyl_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(music_vinyl_label_, lv_color_hex(0xec4899), 0);
    lv_obj_set_style_bg_opa(music_vinyl_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(music_vinyl_label_, lv_color_hex(0xf43f5e), 0);
    lv_obj_set_style_border_width(music_vinyl_label_, 2, 0);
    lv_obj_set_style_radius(music_vinyl_label_, 21, 0);
    lv_obj_set_style_pad_all(music_vinyl_label_, 0, 0);
    lv_obj_clear_flag(music_vinyl_label_, LV_OBJ_FLAG_SCROLLABLE);

    music_vinyl_note_ = lv_label_create(music_vinyl_label_);
    lv_label_set_text(music_vinyl_note_, MATERIAL_SYMBOLS_MUSIC_NOTE);
    lv_obj_set_style_text_font(music_vinyl_note_, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(music_vinyl_note_, lv_color_white(), 0);
    lv_obj_align(music_vinyl_note_, LV_ALIGN_CENTER, 0, 0);

    // 4. Thông tin bài hát & ca sĩ
    music_song_lbl_ = lv_label_create(music_box_);
    lv_label_set_text(music_song_lbl_, music_current_song_.c_str());
    lv_obj_set_style_text_font(music_song_lbl_, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(music_song_lbl_, lv_color_white(), 0);
    lv_label_set_long_mode(music_song_lbl_, LV_LABEL_LONG_DOT);

    music_artist_lbl_ = lv_label_create(music_box_);
    lv_label_set_text(music_artist_lbl_, music_current_artist_.c_str());
    lv_obj_set_style_text_font(music_artist_lbl_, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(music_artist_lbl_, lv_color_hex(0x94a3b8), 0);
    lv_label_set_long_mode(music_artist_lbl_, LV_LABEL_LONG_DOT);

    // 5. Nút điều khiển Phát / Dừng
    music_play_btn_ = lv_button_create(music_box_);
    lv_obj_set_style_bg_color(music_play_btn_, lv_color_hex(0xdc2626), 0);
    lv_obj_set_style_bg_opa(music_play_btn_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(music_play_btn_, 8, 0);
    lv_obj_set_style_pad_all(music_play_btn_, 0, 0);
    music_play_lbl_ = lv_label_create(music_play_btn_);
    lv_label_set_text(music_play_lbl_, "Dừng");
    lv_obj_set_style_text_font(music_play_lbl_, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(music_play_lbl_, lv_color_white(), 0);
    lv_obj_align(music_play_lbl_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(music_play_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        auto music = Board::GetInstance().GetMusic();
        if (!music) return;
        if (self->music_is_playing_) {
            self->music_is_playing_ = false;
            std::thread([music]() {
                music->StopStreaming();
            }).detach();
        }
        self->UpdateMusicUI();
    }, LV_EVENT_CLICKED, this);

    music_stop_btn_ = lv_button_create(music_box_);
    lv_obj_set_style_bg_color(music_stop_btn_, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_border_color(music_stop_btn_, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_width(music_stop_btn_, 1, 0);
    lv_obj_set_style_radius(music_stop_btn_, 8, 0);
    lv_obj_set_style_pad_all(music_stop_btn_, 0, 0);
    music_stop_lbl_ = lv_label_create(music_stop_btn_);
    lv_label_set_text(music_stop_lbl_, "Thoát");
    lv_obj_set_style_text_font(music_stop_lbl_, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(music_stop_lbl_, lv_color_hex(0xcbd5e1), 0);
    lv_obj_align(music_stop_lbl_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(music_stop_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        auto music = Board::GetInstance().GetMusic();
        if (music) {
            self->music_is_playing_ = false;
            std::thread([music]() {
                music->StopStreaming();
            }).detach();
        }
        self->ShowMenu();
    }, LV_EVENT_CLICKED, this);

    // 6. Dãy đèn nháy quang phổ Equalizer
    music_visualizer_box_ = lv_obj_create(music_box_);
    lv_obj_set_style_bg_opa(music_visualizer_box_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(music_visualizer_box_, 0, 0);
    lv_obj_set_style_pad_all(music_visualizer_box_, 0, 0);
    lv_obj_clear_flag(music_visualizer_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(music_visualizer_box_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(music_visualizer_box_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    for (int i = 0; i < kNumVisualizerBars; ++i) {
        music_vis_bars_[i] = lv_obj_create(music_visualizer_box_);
        lv_obj_set_size(music_vis_bars_[i], 12, 6);
        lv_obj_set_style_radius(music_vis_bars_[i], 2, 0);
        lv_obj_set_style_border_width(music_vis_bars_[i], 0, 0);
        lv_obj_set_style_pad_all(music_vis_bars_[i], 0, 0);
        lv_obj_set_style_bg_color(music_vis_bars_[i], lv_color_hex(kSpectrumColors[i]), 0);
        lv_obj_set_style_bg_opa(music_vis_bars_[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(music_vis_bars_[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    // 7. Timer nhấp nháy đèn quang phổ
    music_vis_timer_ = lv_timer_create([](lv_timer_t* t) {
        auto* self = static_cast<LcdDisplay*>(lv_timer_get_user_data(t));
        if (!self || !self->music_box_ || lv_obj_has_flag(self->music_box_, LV_OBJ_FLAG_HIDDEN)) return;

        static int tick = 0;
        tick++;
        int max_h = (self->height_ > self->width_) ? 44 : 56;

        for (int i = 0; i < self->kNumVisualizerBars; ++i) {
            if (!self->music_vis_bars_[i]) continue;
            int h = 4;
            if (self->music_is_playing_) {
                float wave1 = sinf((tick * 0.28f) + (i * 0.55f));
                float wave2 = cosf((tick * 0.42f) - (i * 0.35f));
                float norm = (wave1 + wave2 + 2.0f) * 0.25f;
                int target_h = (int)(norm * max_h);
                if (target_h < 4) target_h = 4;
                if (target_h > max_h) target_h = max_h;
                h = target_h;
            }
            lv_obj_set_height(self->music_vis_bars_[i], h);
        }
    }, 70, this);
}

void LcdDisplay::LayoutMusicWidgets(bool is_portrait) {
    if (!music_box_) return;
    lv_obj_set_size(music_box_, width_, height_);

    if (is_portrait) {
        if (music_back_btn_) lv_obj_set_pos(music_back_btn_, 6, 6);
        if (music_title_badge_) lv_obj_align(music_title_badge_, LV_ALIGN_TOP_MID, 0, 10);
        if (music_vinyl_) {
            lv_obj_set_size(music_vinyl_, 96, 96);
            lv_obj_set_style_radius(music_vinyl_, 48, 0);
            lv_obj_set_pos(music_vinyl_, 72, 36);
        }
        if (music_song_lbl_) {
            lv_obj_set_width(music_song_lbl_, 220);
            lv_obj_set_style_text_align(music_song_lbl_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(music_song_lbl_, LV_ALIGN_TOP_MID, 0, 140);
        }
        if (music_artist_lbl_) {
            lv_obj_set_width(music_artist_lbl_, 220);
            lv_obj_set_style_text_align(music_artist_lbl_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(music_artist_lbl_, LV_ALIGN_TOP_MID, 0, 168);
        }
        if (music_play_btn_) {
            lv_obj_set_size(music_play_btn_, 96, 36);
            lv_obj_set_pos(music_play_btn_, 24, 204);
        }
        if (music_stop_btn_) {
            lv_obj_set_size(music_stop_btn_, 84, 36);
            lv_obj_set_pos(music_stop_btn_, 132, 204);
        }
        if (music_visualizer_box_) {
            lv_obj_set_size(music_visualizer_box_, 224, 52);
            lv_obj_set_pos(music_visualizer_box_, 8, 256);
            for (int i = 0; i < kNumVisualizerBars; ++i) {
                if (music_vis_bars_[i]) lv_obj_set_width(music_vis_bars_[i], 10);
            }
        }
    } else {
        if (music_back_btn_) lv_obj_set_pos(music_back_btn_, 6, 4);
        if (music_title_badge_) lv_obj_align(music_title_badge_, LV_ALIGN_TOP_MID, 0, 6);
        if (music_vinyl_) {
            lv_obj_set_size(music_vinyl_, 112, 112);
            lv_obj_set_style_radius(music_vinyl_, 56, 0);
            lv_obj_set_pos(music_vinyl_, 12, 36);
        }
        if (music_song_lbl_) {
            lv_obj_set_width(music_song_lbl_, 176);
            lv_obj_set_style_text_align(music_song_lbl_, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_pos(music_song_lbl_, 134, 36);
        }
        if (music_artist_lbl_) {
            lv_obj_set_width(music_artist_lbl_, 176);
            lv_obj_set_style_text_align(music_artist_lbl_, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_pos(music_artist_lbl_, 134, 66);
        }
        if (music_play_btn_) {
            lv_obj_set_size(music_play_btn_, 86, 34);
            lv_obj_set_pos(music_play_btn_, 134, 102);
        }
        if (music_stop_btn_) {
            lv_obj_set_size(music_stop_btn_, 72, 34);
            lv_obj_set_pos(music_stop_btn_, 230, 102);
        }
        if (music_visualizer_box_) {
            lv_obj_set_size(music_visualizer_box_, 300, 64);
            lv_obj_set_pos(music_visualizer_box_, 10, 164);
            for (int i = 0; i < kNumVisualizerBars; ++i) {
                if (music_vis_bars_[i]) lv_obj_set_width(music_vis_bars_[i], 14);
            }
        }
    }
}

void LcdDisplay::UpdateMusicUI() {
    if (!music_box_) return;
    if (music_song_lbl_) lv_label_set_text(music_song_lbl_, music_current_song_.c_str());
    if (music_artist_lbl_) lv_label_set_text(music_artist_lbl_, music_current_artist_.c_str());

    if (music_play_lbl_) {
        lv_label_set_text(music_play_lbl_, music_is_playing_ ? "Dừng" : "Phát");
    }
    if (music_play_btn_) {
        lv_obj_set_style_bg_color(music_play_btn_, music_is_playing_ ? lv_color_hex(0xdc2626) : lv_color_hex(0x2563eb), 0);
    }
    if (music_vinyl_label_) {
        lv_obj_set_style_border_color(music_vinyl_label_, music_is_playing_ ? lv_color_hex(0x38bdf8) : lv_color_hex(0xf43f5e), 0);
    }
}

void LcdDisplay::ShowMusicScreen(const std::string& song_name, const std::string& artist_name) {
    DisplayLockGuard lock(this);
    if (!lock) return;

    screen_mode_ = kScreenMusic;
    if (menu_box_) lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);
    if (standby_box_) lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);
    if (standby2_box_) lv_obj_add_flag(standby2_box_, LV_OBJ_FLAG_HIDDEN);
    if (quick_settings_box_) lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);
    if (calendar_box_) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (weather_detail_box_) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);
    if (clock_box_) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
    if (stopwatch_box_) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);
    if (timer_box_) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);
    if (rates_box_) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);
    if (calc_box_) lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);
    if (flashlight_box_) lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);
    if (fm_box_) lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);
    if (music_box_ != nullptr) lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);
    if (wallpaper_box_) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);

    if (!song_name.empty()) music_current_song_ = song_name;
    if (!artist_name.empty()) music_current_artist_ = artist_name;

    CreateMusicScreenUI();
    LayoutMusicWidgets(height_ > width_);
    UpdateMusicUI();

    if (music_box_) {
        lv_obj_remove_flag(music_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(music_box_);
    }
}

void LcdDisplay::UpdateMusicPlaybackState(bool is_playing, const std::string& song_name, const std::string& artist_name) {
    DisplayLockGuard lock(this);
    if (!lock) return;

    music_is_playing_ = is_playing;
    if (!song_name.empty()) music_current_song_ = song_name;
    if (!artist_name.empty()) music_current_artist_ = artist_name;

    UpdateMusicUI();
}

void LcdDisplay::CreateStandby2UI() {
    auto screen = lv_screen_active();
    standby2_box_ = lv_obj_create(screen);
    lv_obj_set_size(standby2_box_, width_, height_);
    lv_obj_set_pos(standby2_box_, 0, 0);
    lv_obj_set_style_bg_color(standby2_box_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(standby2_box_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(standby2_box_, 0, 0);
    lv_obj_set_style_border_width(standby2_box_, 0, 0);
    lv_obj_set_style_pad_all(standby2_box_, 0, 0);
    lv_obj_clear_flag(standby2_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(standby2_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(standby2_box_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(standby2_box_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (self) {
            if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 300) return;
            self->CycleStandby2Wallpaper();
        }
    }, LV_EVENT_CLICKED, this);

    // 1. Wallpaper background
    standby2_bg_image_ = lv_image_create(standby2_box_);
    lv_obj_set_size(standby2_bg_image_, width_, height_);
    lv_obj_set_pos(standby2_bg_image_, 0, 0);
    lv_obj_set_style_border_width(standby2_bg_image_, 0, 0);
    lv_obj_add_flag(standby2_bg_image_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(standby2_bg_image_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (self) {
            if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 300) return;
            self->CycleStandby2Wallpaper();
        }
    }, LV_EVENT_CLICKED, this);

    if (standby2_bg_buf_ == nullptr) {
        bool is_p = (height_ > width_);
        const uint8_t* start_ptr = is_p ? standby2_bg_portrait_jpg_start : standby2_bg_landscape_jpg_start;
        const uint8_t* end_ptr = is_p ? standby2_bg_portrait_jpg_end : standby2_bg_landscape_jpg_end;
        size_t len = (size_t)(end_ptr - start_ptr);
        size_t out_len = 0, w = 0, h = 0, stride = 0;
        if (jpeg_to_image(start_ptr, len, &standby2_bg_buf_, &out_len, &w, &h, &stride) == ESP_OK && standby2_bg_buf_ != nullptr) {
            standby2_bg_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
            standby2_bg_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
            standby2_bg_dsc_.header.flags = 0;
            standby2_bg_dsc_.header.w = (uint32_t)w;
            standby2_bg_dsc_.header.h = (uint32_t)h;
            standby2_bg_dsc_.header.stride = (uint32_t)stride;
            standby2_bg_dsc_.data_size = out_len;
            standby2_bg_dsc_.data = standby2_bg_buf_;
        }
    }
    if (standby2_bg_buf_ != nullptr) {
        lv_image_set_src(standby2_bg_image_, &standby2_bg_dsc_);
    }

    // 2. Top-Right Status Bar: [ WiFi ]  [ % Pin ] [ Ký hiệu Pin ] - Bố cục như màn 1
    standby2_status_box_ = lv_obj_create(standby2_box_);
    lv_obj_set_size(standby2_status_box_, LV_SIZE_CONTENT, 30);
    lv_obj_set_style_bg_opa(standby2_status_box_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(standby2_status_box_, 0, 0);
    lv_obj_set_style_pad_all(standby2_status_box_, 0, 0);
    lv_obj_set_flex_flow(standby2_status_box_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(standby2_status_box_, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(standby2_status_box_, 6, 0);
    lv_obj_clear_flag(standby2_status_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(standby2_status_box_, LV_OBJ_FLAG_CLICKABLE);

    standby2_wifi_icon_ = lv_label_create(standby2_status_box_);
    lv_label_set_text(standby2_wifi_icon_, MATERIAL_SYMBOLS_WIFI);
    lv_obj_set_style_text_font(standby2_wifi_icon_, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(standby2_wifi_icon_, lv_color_white(), 0);
    lv_obj_clear_flag(standby2_wifi_icon_, LV_OBJ_FLAG_CLICKABLE);

    standby2_battery_pct_ = lv_label_create(standby2_status_box_);
    lv_label_set_text(standby2_battery_pct_, "--%");
    lv_obj_set_style_text_font(standby2_battery_pct_, &font_noto_sans_basic_14_1, 0);
    lv_obj_set_style_text_color(standby2_battery_pct_, lv_color_white(), 0);
    lv_obj_clear_flag(standby2_battery_pct_, LV_OBJ_FLAG_CLICKABLE);

    standby2_battery_icon_ = lv_label_create(standby2_status_box_);
    lv_label_set_text(standby2_battery_icon_, MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL);
    lv_obj_set_style_text_font(standby2_battery_icon_, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(standby2_battery_icon_, lv_color_white(), 0);
    lv_obj_clear_flag(standby2_battery_icon_, LV_OBJ_FLAG_CLICKABLE);

    // 3. Top-Left Clock and Date: Giờ to ở góc trên bên trái kèm Thứ, Ngày Tháng Năm
    standby2_time_lbl_ = lv_label_create(standby2_box_);
    lv_label_set_text(standby2_time_lbl_, "--:--");
    lv_obj_set_style_text_font(standby2_time_lbl_, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(standby2_time_lbl_, lv_color_white(), 0);
    lv_obj_clear_flag(standby2_time_lbl_, LV_OBJ_FLAG_CLICKABLE);

    standby2_date_lbl_ = lv_label_create(standby2_box_);
    lv_label_set_text(standby2_date_lbl_, "Thứ Ba, 10/11/2026");
    lv_obj_set_style_text_font(standby2_date_lbl_, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(standby2_date_lbl_, lv_color_white(), 0);
    lv_obj_clear_flag(standby2_date_lbl_, LV_OBJ_FLAG_CLICKABLE);

    // Weather và flip clock dưới đã bỏ
    standby2_weather_box_ = nullptr;
    standby2_city_lbl_ = nullptr;
    standby2_weather_icon_ = nullptr;
    standby2_weather_temp_ = nullptr;
    standby2_clock_box_ = nullptr;
    for (int i = 0; i < 4; ++i) {
        flip_cards_[i] = nullptr;
        flip_labels_[i] = nullptr;
        flip_slits_[i] = nullptr;
        flip_flaps_[i] = nullptr;
        flip_flap_labels_[i] = nullptr;
    }

    LayoutStandby2Widgets(height_ > width_);
    UpdateStandbyStatusBar();
}

void LcdDisplay::LayoutStandby2Widgets(bool is_portrait) {
    if (standby2_box_ == nullptr) return;
    int w = is_portrait ? 240 : 320;
    int h = is_portrait ? 320 : 240;
    lv_obj_set_size(standby2_box_, w, h);
    if (standby2_bg_image_) lv_obj_set_size(standby2_bg_image_, w, h);

    // Tự động load đúng hình nền dọc/ngang full màn hình
    int cur_orient = is_portrait ? 1 : 0;
    int cur_wp_idx = WallpaperManager::GetInstance().GetActiveWallpaperIndex();
    if (standby2_last_orient_ != cur_orient || standby2_last_wp_idx_ != cur_wp_idx) {
        standby2_last_orient_ = cur_orient;
        standby2_last_wp_idx_ = cur_wp_idx;
        if (standby2_bg_buf_ != nullptr) {
            heap_caps_free(standby2_bg_buf_);
            standby2_bg_buf_ = nullptr;
        }
        const uint8_t* img_data = nullptr;
        size_t img_len = 0;
        bool is_alloc = false;
        if (WallpaperManager::GetInstance().GetWallpaperJpeg(cur_wp_idx, is_portrait, &img_data, &img_len, &is_alloc) && img_data != nullptr) {
            size_t out_len = 0, img_w = 0, img_h = 0, stride = 0;
            if (jpeg_to_image(img_data, img_len, &standby2_bg_buf_, &out_len, &img_w, &img_h, &stride) == ESP_OK && standby2_bg_buf_ != nullptr) {
                standby2_bg_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
                standby2_bg_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
                standby2_bg_dsc_.header.flags = 0;
                standby2_bg_dsc_.header.w = (uint32_t)img_w;
                standby2_bg_dsc_.header.h = (uint32_t)img_h;
                standby2_bg_dsc_.header.stride = (uint32_t)stride;
                standby2_bg_dsc_.data_size = out_len;
                standby2_bg_dsc_.data = standby2_bg_buf_;
                if (standby2_bg_image_) {
                    lv_image_set_src(standby2_bg_image_, &standby2_bg_dsc_);
                }
            }
            if (is_alloc) {
                heap_caps_free((void*)img_data);
            }
        }
    }

    if (standby2_status_box_) {
        lv_obj_align(standby2_status_box_, LV_ALIGN_TOP_RIGHT, is_portrait ? -8 : -10, 6);
        lv_obj_move_foreground(standby2_status_box_);
    }
    if (standby2_time_lbl_) {
        lv_obj_set_pos(standby2_time_lbl_, 12, 6);
        lv_obj_move_foreground(standby2_time_lbl_);
    }
    if (standby2_date_lbl_) {
        lv_obj_set_pos(standby2_date_lbl_, 14, 50);
        lv_obj_move_foreground(standby2_date_lbl_);
    }
}

void LcdDisplay::AnimateFlipCard(int idx, char new_char) {
    if (idx < 0 || idx >= 4) return;
    if (flip_flaps_[idx] == nullptr || flip_labels_[idx] == nullptr || flip_flap_labels_[idx] == nullptr) return;

    lv_obj_t* flap = flip_flaps_[idx];
    lv_obj_t* flap_lbl = flip_flap_labels_[idx];
    lv_obj_t* main_lbl = flip_labels_[idx];
    lv_anim_delete(flap, nullptr);

    // Lấy ký tự cũ hiện tại
    const char* old_txt = lv_label_get_text(main_lbl);
    char old_char = (old_txt && old_txt[0]) ? old_txt[0] : '0';

    // Cập nhật flap hiển thị ký tự cũ ở nửa trên
    char old_str[2] = {old_char, '\0'};
    lv_label_set_text(flap_lbl, old_str);
    lv_obj_set_style_text_color(flap_lbl, lv_obj_get_style_text_color(main_lbl, LV_PART_MAIN), 0);

    // Cập nhật main_lbl thành ký tự mới (nằm dưới flap, sẽ lộ ra khi flap gập xuống)
    char new_str[2] = {new_char, '\0'};
    lv_label_set_text(main_lbl, new_str);

    // Chuẩn bị flap ở nửa trên (y = 0, h = 26)
    lv_obj_remove_flag(flap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(flap);
    if (flip_slits_[idx]) lv_obj_move_foreground(flip_slits_[idx]);
    lv_obj_set_pos(flap, 0, 0);
    lv_obj_set_size(flap, 36, 26);
    lv_obj_align(flap_lbl, LV_ALIGN_CENTER, 0, 0);

    struct FlipAnimData {
        LcdDisplay* display;
        int idx;
        char new_char;
    };
    auto* data = new FlipAnimData{this, idx, new_char};

    // Giai đoạn 1: Nửa trên của thẻ cũ gập xuống giữa (26 -> 0px)
    lv_anim_t a1;
    lv_anim_init(&a1);
    lv_anim_set_var(&a1, flap);
    lv_anim_set_values(&a1, 26, 0);
    lv_anim_set_duration(&a1, 140);
    lv_anim_set_exec_cb(&a1, [](void* var, int32_t val) {
        lv_obj_set_height((lv_obj_t*)var, val);
    });
    lv_anim_set_user_data(&a1, data);
    lv_anim_set_completed_cb(&a1, [](lv_anim_t* anim) {
        auto* d = static_cast<FlipAnimData*>(lv_anim_get_user_data(anim));
        if (d && d->display) {
            lv_obj_t* f = d->display->flip_flaps_[d->idx];
            lv_obj_t* f_lbl = d->display->flip_flap_labels_[d->idx];
            if (f && f_lbl) {
                // Đổi flap sang ký tự mới cho nửa dưới
                char s[2] = {d->new_char, '\0'};
                lv_label_set_text(f_lbl, s);

                // Giai đoạn 2: Nửa dưới của thẻ mới bung mở từ giữa xuống đáy (0 -> 26px)
                lv_obj_set_pos(f, 0, 26);
                lv_obj_set_size(f, 36, 0);
                lv_obj_align(f_lbl, LV_ALIGN_CENTER, 0, -26);

                lv_anim_t a2;
                lv_anim_init(&a2);
                lv_anim_set_var(&a2, f);
                lv_anim_set_values(&a2, 0, 26);
                lv_anim_set_duration(&a2, 140);
                lv_anim_set_exec_cb(&a2, [](void* var, int32_t val) {
                    lv_obj_set_height((lv_obj_t*)var, val);
                });
                lv_anim_set_user_data(&a2, d);
                lv_anim_set_completed_cb(&a2, [](lv_anim_t* a) {
                    auto* dt = static_cast<FlipAnimData*>(lv_anim_get_user_data(a));
                    if (dt && dt->display && dt->display->flip_flaps_[dt->idx]) {
                        lv_obj_add_flag(dt->display->flip_flaps_[dt->idx], LV_OBJ_FLAG_HIDDEN);
                    }
                    delete dt;
                });
                lv_anim_start(&a2);
                return;
            }
        }
        delete d;
    });
    lv_anim_start(&a1);
}

void LcdDisplay::UpdateStandby2Clock(const struct tm* tm_now, bool animate) {
    if (standby2_box_ == nullptr) return;

    if (standby2_time_lbl_ != nullptr) {
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tm_now->tm_hour, tm_now->tm_min);
        lv_label_set_text(standby2_time_lbl_, tbuf);
    }

    if (standby2_date_lbl_ != nullptr) {
        const char* full_wdays[] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
        char dbuf[64];
        snprintf(dbuf, sizeof(dbuf), "%s, %02d/%02d/%04d", 
                 full_wdays[tm_now->tm_wday % 7], 
                 tm_now->tm_mday, tm_now->tm_mon + 1, tm_now->tm_year + 1900);
        lv_label_set_text(standby2_date_lbl_, dbuf);
    }
}

void LcdDisplay::SyncStandby2Weather() {
    if (standby2_box_ == nullptr) return;

    const auto& info = WeatherService::GetInstance().GetWeatherInfo();

    // 1. City / Location (e.g. from WeatherService or Standby 1)
    if (standby2_city_lbl_ != nullptr) {
        if (!info.city.empty() && info.city != "Đang định vị...") {
            lv_label_set_text(standby2_city_lbl_, info.city.c_str());
        } else if (standby_city_ != nullptr) {
            const char* c = lv_label_get_text(standby_city_);
            if (c && strlen(c) > 0 && strcmp(c, "Đang định vị...") != 0) {
                lv_label_set_text(standby2_city_lbl_, c);
            }
        }
    }

    // 2. Temperature
    if (standby2_weather_temp_ != nullptr) {
        if (info.temp > -60.0f && info.temp < 80.0f && info.temp != 0.0f) {
            char tbuf[16];
            snprintf(tbuf, sizeof(tbuf), "%.0f°C", info.temp);
            lv_label_set_text(standby2_weather_temp_, tbuf);
        } else if (standby_temp_ != nullptr) {
            const char* t = lv_label_get_text(standby_temp_);
            if (t && strlen(t) > 0) {
                lv_label_set_text(standby2_weather_temp_, t);
            }
        }
    }

    // 3. Weather Icon (24x24 px)
    if (standby2_weather_icon_ != nullptr) {
        const lv_image_dsc_t* dsc24 = &weather_icon_cloud_24_dsc;
        switch (info.icon_type) {
            case 0: dsc24 = &weather_icon_sun_24_dsc; break;
            case 1: dsc24 = &weather_icon_sun_cloud_24_dsc; break;
            case 2: dsc24 = &weather_icon_cloud_24_dsc; break;
            case 3: dsc24 = &weather_icon_rain_24_dsc; break;
            case 4: dsc24 = &weather_icon_thunder_24_dsc; break;
            case 5: dsc24 = &weather_icon_moon_24_dsc; break;
            case 6: dsc24 = &weather_icon_moon_cloud_24_dsc; break;
            default: dsc24 = &weather_icon_cloud_24_dsc; break;
        }
        lv_image_set_src(standby2_weather_icon_, dsc24);
    }
}

void LcdDisplay::CycleStandby2Wallpaper() {
    auto list = WallpaperManager::GetInstance().GetWallpaperList();
    if (list.empty()) return;

    int cur_idx = WallpaperManager::GetInstance().GetActiveWallpaperIndex();
    int next_idx = 1;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].index == cur_idx) {
            next_idx = list[(i + 1) % list.size()].index;
            break;
        }
    }
    if (next_idx == 0 && list.size() > 1) {
        next_idx = 1;
    }
    WallpaperManager::GetInstance().SetActiveWallpaperIndex(next_idx);
    bool is_p = (height_ > width_);
    LayoutStandby2Widgets(is_p);
}

void LcdDisplay::ShowStandby2() {
    DisplayLockGuard lock(this);
    if (!lock) return;

    screen_mode_ = kScreenStandby2;
    previous_screen_mode_ = kScreenStandby2;

    if (standby_box_ != nullptr) lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);
    if (menu_box_ != nullptr) {
        lv_anim_delete(menu_box_, nullptr);
        lv_obj_set_y(menu_box_, 0);
        lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (quick_settings_box_ != nullptr) {
        lv_anim_delete(quick_settings_box_, nullptr);
        lv_obj_set_y(quick_settings_box_, 0);
        lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (wallpaper_box_ != nullptr) lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);
    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);
    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);
    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);
    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);
    if (calc_box_ != nullptr) lv_obj_add_flag(calc_box_, LV_OBJ_FLAG_HIDDEN);
    if (flashlight_box_ != nullptr) lv_obj_add_flag(flashlight_box_, LV_OBJ_FLAG_HIDDEN);
    if (fm_box_ != nullptr) lv_obj_add_flag(fm_box_, LV_OBJ_FLAG_HIDDEN);
    if (music_box_ != nullptr) lv_obj_add_flag(music_box_, LV_OBJ_FLAG_HIDDEN);

    if (standby2_box_ != nullptr) {
        lv_obj_remove_flag(standby2_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(standby2_box_);
        UpdateStandbyStatusBar();
        SyncStandby2Weather();
        SetStandbyTextColor(standby_text_color_);
        time_t now = time(nullptr);
        struct tm* tm_now = localtime(&now);
        if (tm_now) {
            UpdateStandby2Clock(tm_now, false);
        }
    }
}


// Buffer & Descriptor cho ma QR (110x110 RGB565)
static uint16_t s_qr_pixels[110 * 110];
static const lv_image_dsc_t s_qr_dsc = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = 110,
        .h = 110,
        .stride = 110 * 2,
        .reserved_2 = 0,
    },
    .data_size = 110 * 110 * 2,
    .data = (const uint8_t*)s_qr_pixels,
    .reserved = nullptr,
};

void LcdDisplay::CreateQrModal() {
    if (qs_qr_modal_ != nullptr) return;

    qs_qr_modal_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(qs_qr_modal_, width_, height_);
    lv_obj_set_pos(qs_qr_modal_, 0, 0);
    lv_obj_set_style_bg_color(qs_qr_modal_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(qs_qr_modal_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(qs_qr_modal_, 0, 0);
    lv_obj_set_style_pad_all(qs_qr_modal_, 0, 0);
    lv_obj_add_flag(qs_qr_modal_, LV_OBJ_FLAG_HIDDEN);

    // Card popup ở giữa
    lv_obj_t* card = lv_obj_create(qs_qr_modal_);
    lv_obj_set_size(card, (width_ < height_) ? 224 : 256, (width_ < height_) ? 266 : 224);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x0d9488), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Title tiếng Việt có dấu đậm
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, "Thêm ảnh nền");
    lv_obj_set_style_text_font(title, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 2, 4);

    // Nút đóng to hơn (32x32) để chạm dễ dàng
    lv_obj_t* close_btn = lv_button_create(card);
    lv_obj_set_size(close_btn, 32, 32);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xef4444), 0);
    lv_obj_set_style_radius(close_btn, 16, 0);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "X");
    lv_obj_set_style_text_font(close_lbl, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_align(close_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        self->HideQrModal();
    }, LV_EVENT_CLICKED, this);

    // QR Image
    qs_qr_img_ = lv_image_create(card);
    lv_image_set_src(qs_qr_img_, &s_qr_dsc);
    lv_obj_align(qs_qr_img_, LV_ALIGN_TOP_MID, 0, 32);

    lv_obj_add_event_cb(card, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
            if (dir == LV_DIR_TOP) {
                auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
                self->HideQrModal();
            }
        }
    }, LV_EVENT_GESTURE, this);

    // Label chỉ dẫn tiếng Việt có dấu với font custom 16
    qs_qr_ip_lbl_ = lv_label_create(card);
    lv_label_set_text(qs_qr_ip_lbl_, "Quét mã QR hoặc truy cập:\nhttp://...");
    lv_obj_set_style_text_font(qs_qr_ip_lbl_, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(qs_qr_ip_lbl_, lv_color_hex(0x38bdf8), 0);
    lv_obj_set_style_text_align(qs_qr_ip_lbl_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(qs_qr_ip_lbl_, LV_ALIGN_BOTTOM_MID, 0, -4);
}

void LcdDisplay::ShowQrModal() {
    CreateQrModal();
    if (qs_qr_modal_ == nullptr) return;

    std::string ip = WifiManager::GetInstance().GetIpAddress();
    if (ip.empty() || ip == "--" || ip == "0.0.0.0") ip = "192.168.2.56";
    std::string url = "http://" + ip + "/";

    // Allocate qrcodegen buffers on heap to avoid FreeRTOS task stack overflow
    uint8_t* qrcode = (uint8_t*)malloc(qrcodegen_BUFFER_LEN_MAX);
    uint8_t* tempBuffer = (uint8_t*)malloc(qrcodegen_BUFFER_LEN_MAX);
    if (qrcode != nullptr && tempBuffer != nullptr) {
        bool ok = qrcodegen_encodeText(url.c_str(), tempBuffer, qrcode, qrcodegen_Ecc_LOW,
                                       qrcodegen_VERSION_MIN, 4, qrcodegen_Mask_AUTO, true);
        if (ok && qs_qr_img_ != nullptr) {
            // Fill background white
            for (int i = 0; i < 110 * 110; i++) {
                s_qr_pixels[i] = 0xFFFF;
            }
            int qr_size = qrcodegen_getSize(qrcode);
            int canvas_size = 110;
            int scale = canvas_size / (qr_size + 2);
            if (scale < 1) scale = 1;
            int margin = (canvas_size - qr_size * scale) / 2;

            for (int y = 0; y < qr_size; y++) {
                for (int x = 0; x < qr_size; x++) {
                    if (qrcodegen_getModule(qrcode, x, y)) {
                        int px0 = margin + x * scale;
                        int py0 = margin + y * scale;
                        for (int dy = 0; dy < scale; dy++) {
                            int py = py0 + dy;
                            if (py >= 0 && py < 110) {
                                for (int dx = 0; dx < scale; dx++) {
                                    int px = px0 + dx;
                                    if (px >= 0 && px < 110) {
                                        s_qr_pixels[py * 110 + px] = 0x0000;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            lv_image_set_src(qs_qr_img_, &s_qr_dsc);
        }
    }
    if (qrcode) free(qrcode);
    if (tempBuffer) free(tempBuffer);

    std::string text = "Quét mã QR hoặc truy cập:\n" + url;
    lv_label_set_text(qs_qr_ip_lbl_, text.c_str());

    lv_obj_clear_flag(qs_qr_modal_, LV_OBJ_FLAG_HIDDEN);
}

void LcdDisplay::HideQrModal() {
    if (qs_qr_modal_ != nullptr) {
        lv_obj_add_flag(qs_qr_modal_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::CreateWallpaperSettingsUI() {
    if (wallpaper_box_ != nullptr) return;

    wallpaper_box_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(wallpaper_box_, width_, height_);
    lv_obj_set_pos(wallpaper_box_, 0, 0);
    lv_obj_set_style_bg_color(wallpaper_box_, lv_color_hex(0x0a0f1d), 0);
    lv_obj_set_style_bg_opa(wallpaper_box_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wallpaper_box_, 0, 0);
    lv_obj_set_style_pad_all(wallpaper_box_, 6, 0);
    lv_obj_clear_flag(wallpaper_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);

    // Header: Nút đóng [✕] + Tiêu đề
    lv_obj_t* back_btn = lv_button_create(wallpaper_box_);
    lv_obj_set_size(back_btn, 32, 32);
    lv_obj_set_pos(back_btn, 4, 4);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x334155), 0);
    lv_obj_set_style_radius(back_btn, 16, 0);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "X");
    lv_obj_set_style_text_font(back_lbl, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_white(), 0);
    lv_obj_align(back_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* title_lbl = lv_label_create(wallpaper_box_);
    lv_label_set_text(title_lbl, "Cài đặt hình nền");
    lv_obj_set_style_text_font(title_lbl, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 8);

    // 1. Nhóm Ảnh hệ thống (Ảnh 1, Ảnh 2, Ảnh 3, Ảnh 4)
    wp_sec1_lbl_ = lv_label_create(wallpaper_box_);
    lv_label_set_text(wp_sec1_lbl_, "Ảnh hệ thống:");
    lv_obj_set_style_text_font(wp_sec1_lbl_, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(wp_sec1_lbl_, lv_color_hex(0x38bdf8), 0);

    wp_img_scroll_ = lv_obj_create(wallpaper_box_);
    lv_obj_set_style_bg_color(wp_img_scroll_, lv_color_hex(0x131d2e), 0);
    lv_obj_set_style_border_color(wp_img_scroll_, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(wp_img_scroll_, 1, 0);
    lv_obj_set_style_radius(wp_img_scroll_, 10, 0);
    lv_obj_set_style_pad_all(wp_img_scroll_, 4, 0);
    lv_obj_set_flex_flow(wp_img_scroll_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wp_img_scroll_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(wp_img_scroll_, LV_DIR_HOR);

    // 2. Nhóm Ảnh của tôi (Ảnh tải lên, nếu chưa thì hiển thị Trống)
    wp_sec2_lbl_ = lv_label_create(wallpaper_box_);
    lv_label_set_text(wp_sec2_lbl_, "Ảnh của tôi:");
    lv_obj_set_style_text_font(wp_sec2_lbl_, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(wp_sec2_lbl_, lv_color_hex(0x38bdf8), 0);

    wp_user_scroll_ = lv_obj_create(wallpaper_box_);
    lv_obj_set_style_bg_color(wp_user_scroll_, lv_color_hex(0x131d2e), 0);
    lv_obj_set_style_border_color(wp_user_scroll_, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(wp_user_scroll_, 1, 0);
    lv_obj_set_style_radius(wp_user_scroll_, 10, 0);
    lv_obj_set_style_pad_all(wp_user_scroll_, 4, 0);
    lv_obj_set_flex_flow(wp_user_scroll_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wp_user_scroll_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(wp_user_scroll_, LV_DIR_HOR);

    // 3. Nút Lưu & Áp dụng
    wp_save_btn_ = lv_button_create(wallpaper_box_);
    lv_obj_set_style_bg_color(wp_save_btn_, lv_color_hex(0x0d9488), 0);
    lv_obj_set_style_radius(wp_save_btn_, 10, 0);
    lv_obj_t* save_lbl = lv_label_create(wp_save_btn_);
    lv_label_set_text(save_lbl, "Lưu & áp dụng");
    lv_obj_set_style_text_font(save_lbl, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(save_lbl, lv_color_white(), 0);
    lv_obj_align(save_lbl, LV_ALIGN_CENTER, 0, 0);

    auto save_exit_action = [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        WallpaperManager::GetInstance().SetActiveWallpaperIndex(self->wp_preview_idx_);
        if (self->wallpaper_box_) lv_obj_add_flag(self->wallpaper_box_, LV_OBJ_FLAG_HIDDEN);
        if (self->wp_preview_buf_ != nullptr) {
            heap_caps_free(self->wp_preview_buf_);
            self->wp_preview_buf_ = nullptr;
        }
        self->ReloadWallpaper();
        self->ShowStandby2();
    };

    lv_obj_add_event_cb(wp_save_btn_, save_exit_action, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(back_btn, save_exit_action, LV_EVENT_CLICKED, this);

    // Hiệu ứng vuốt lên hoặc sang phải để thoát và tự lưu
    lv_obj_add_event_cb(wallpaper_box_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
            if (dir == LV_DIR_TOP || dir == LV_DIR_RIGHT) {
                auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
                WallpaperManager::GetInstance().SetActiveWallpaperIndex(self->wp_preview_idx_);
                if (self->wallpaper_box_) lv_obj_add_flag(self->wallpaper_box_, LV_OBJ_FLAG_HIDDEN);
                if (self->wp_preview_buf_ != nullptr) {
                    heap_caps_free(self->wp_preview_buf_);
                    self->wp_preview_buf_ = nullptr;
                }
                self->ReloadWallpaper();
                self->ShowStandby2();
            }
        }
    }, LV_EVENT_GESTURE, this);
}

void LcdDisplay::UpdateWallpaperPreview() {
}

void LcdDisplay::UpdateWallpaperSettingsUI() {
    if (wallpaper_box_ == nullptr) return;

    // 1. Nhóm Ảnh hệ thống (Ảnh 1, Ảnh 2, Ảnh 3, Ảnh 4)
    if (wp_img_scroll_ != nullptr) {
        lv_obj_clean(wp_img_scroll_);
        for (int i = 1; i <= 4; ++i) {
            lv_obj_t* btn = lv_button_create(wp_img_scroll_);
            lv_obj_set_size(btn, 88, 40);
            lv_obj_set_style_radius(btn, 8, 0);
            bool is_sel = (i == wp_preview_idx_);
            lv_obj_set_style_bg_color(btn, is_sel ? lv_color_hex(0x0d9488) : lv_color_hex(0x1e293b), 0);
            lv_obj_set_style_border_color(btn, is_sel ? lv_color_hex(0x2dd4bf) : lv_color_hex(0x334155), 0);
            lv_obj_set_style_border_width(btn, is_sel ? 2 : 1, 0);
            lv_obj_set_user_data(btn, (void*)(intptr_t)i);

            lv_obj_t* lbl = lv_label_create(btn);
            char btn_txt[16];
            snprintf(btn_txt, sizeof(btn_txt), "Ảnh %d", i);
            lv_label_set_text(lbl, btn_txt);
            lv_obj_set_style_text_font(lbl, &font_noto_sans_custom_16_4, 0);
            lv_obj_set_style_text_color(lbl, is_sel ? lv_color_white() : lv_color_hex(0x94a3b8), 0);
            lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
                int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
                self->wp_preview_idx_ = idx;
                self->UpdateWallpaperSettingsUI();
            }, LV_EVENT_CLICKED, this);
        }
    }

    // 2. Nhóm Ảnh của tôi (tải lên >= 5, đánh số Ảnh 1, Ảnh 2... nếu không có thì Trống)
    if (wp_user_scroll_ != nullptr) {
        lv_obj_clean(wp_user_scroll_);
        auto list = WallpaperManager::GetInstance().GetWallpaperList();
        std::vector<WallpaperItem> user_wallpapers;
        for (const auto& item : list) {
            if (item.index >= 5) {
                user_wallpapers.push_back(item);
            }
        }

        if (user_wallpapers.empty()) {
            lv_obj_t* empty_lbl = lv_label_create(wp_user_scroll_);
            lv_label_set_text(empty_lbl, "Trống (Chưa có ảnh tải lên)");
            lv_obj_set_style_text_font(empty_lbl, &font_noto_sans_custom_14_1, 0);
            lv_obj_set_style_text_color(empty_lbl, lv_color_hex(0x94a3b8), 0);
            lv_obj_set_size(empty_lbl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_align(empty_lbl, LV_ALIGN_CENTER, 0, 0);
        } else {
            for (const auto& item : user_wallpapers) {
                lv_obj_t* btn = lv_button_create(wp_user_scroll_);
                lv_obj_set_size(btn, 88, 40);
                lv_obj_set_style_radius(btn, 8, 0);
                bool is_sel = (item.index == wp_preview_idx_);
                lv_obj_set_style_bg_color(btn, is_sel ? lv_color_hex(0x0d9488) : lv_color_hex(0x1e293b), 0);
                lv_obj_set_style_border_color(btn, is_sel ? lv_color_hex(0x2dd4bf) : lv_color_hex(0x334155), 0);
                lv_obj_set_style_border_width(btn, is_sel ? 2 : 1, 0);
                lv_obj_set_user_data(btn, (void*)(intptr_t)item.index);

                lv_obj_t* lbl = lv_label_create(btn);
                lv_label_set_text(lbl, item.name.c_str());
                lv_obj_set_style_text_font(lbl, &font_noto_sans_custom_16_4, 0);
                lv_obj_set_style_text_color(lbl, is_sel ? lv_color_white() : lv_color_hex(0x94a3b8), 0);
                lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

                lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                    auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
                    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
                    self->wp_preview_idx_ = idx;
                    self->UpdateWallpaperSettingsUI();
                }, LV_EVENT_CLICKED, this);
            }
        }
    }
}

void LcdDisplay::ShowWallpaperSettings() {
    screen_mode_ = kScreenWallpaperSettings;
    if (menu_box_) lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);
    if (standby_box_) lv_obj_add_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);
    if (standby2_box_) lv_obj_add_flag(standby2_box_, LV_OBJ_FLAG_HIDDEN);
    if (quick_settings_box_) lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);

    wp_preview_idx_ = WallpaperManager::GetInstance().GetActiveWallpaperIndex();
    if (wp_preview_idx_ <= 0) wp_preview_idx_ = 1;

    CreateWallpaperSettingsUI();
    LayoutWallpaperSettingsWidgets(height_ > width_);
    UpdateWallpaperSettingsUI();
    if (wallpaper_box_) lv_obj_clear_flag(wallpaper_box_, LV_OBJ_FLAG_HIDDEN);
}

void LcdDisplay::ReloadWallpaper() {
    standby2_last_wp_idx_ = -1;
    LayoutStandby2Widgets(height_ > width_);
}

void LcdDisplay::LayoutWallpaperSettingsWidgets(bool is_portrait) {
    if (wallpaper_box_ == nullptr) return;
    lv_obj_set_size(wallpaper_box_, width_, height_);

    if (is_portrait) {
        // Dọc 240x320: Bố trí thoáng đãng
        if (wp_sec1_lbl_) lv_obj_set_pos(wp_sec1_lbl_, 10, 48);
        if (wp_img_scroll_) {
            lv_obj_set_size(wp_img_scroll_, 224, 52);
            lv_obj_set_pos(wp_img_scroll_, 8, 72);
        }
        if (wp_sec2_lbl_) lv_obj_set_pos(wp_sec2_lbl_, 10, 134);
        if (wp_user_scroll_) {
            lv_obj_set_size(wp_user_scroll_, 224, 52);
            lv_obj_set_pos(wp_user_scroll_, 8, 158);
        }
        if (wp_save_btn_) {
            lv_obj_set_size(wp_save_btn_, 224, 46);
            lv_obj_set_pos(wp_save_btn_, 8, 240);
        }
    } else {
        // Ngang 320x240
        if (wp_sec1_lbl_) lv_obj_set_pos(wp_sec1_lbl_, 12, 36);
        if (wp_img_scroll_) {
            lv_obj_set_size(wp_img_scroll_, 296, 46);
            lv_obj_set_pos(wp_img_scroll_, 12, 56);
        }
        if (wp_sec2_lbl_) lv_obj_set_pos(wp_sec2_lbl_, 12, 108);
        if (wp_user_scroll_) {
            lv_obj_set_size(wp_user_scroll_, 296, 46);
            lv_obj_set_pos(wp_user_scroll_, 12, 128);
        }
        if (wp_save_btn_) {
            lv_obj_set_size(wp_save_btn_, 240, 40);
            lv_obj_set_pos(wp_save_btn_, 40, 186);
        }
    }
}
