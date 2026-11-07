#include "lcd_display.h"
#include "custom_wallpaper.h"
#include "assets/lang_config.h"
#include "gif/lvgl_gif.h"
#include "lvgl_theme.h"
#include "settings.h"

#include <esp_err.h>
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
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);        // Overlap with top_bar_

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
    CreateMenuUI();
    CreateQuickSettingsUI();
    CreateCalendarUI();
    CreateWeatherDetailUI();
    CreateClockUI();
    CreateStopwatchUI();
    CreateTimerUI();
    CreateExchangeRatesUI();


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
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);        // Overlap with top_bar_

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
    CreateMenuUI();
    CreateQuickSettingsUI();
    CreateCalendarUI();
    CreateWeatherDetailUI();
    CreateClockUI();
    CreateStopwatchUI();
    CreateTimerUI();
    CreateExchangeRatesUI();


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
                if (standby_box_ != nullptr) {
                    lv_obj_remove_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);
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
            if (menu_box_ != nullptr) lv_obj_add_flag(menu_box_, LV_OBJ_FLAG_HIDDEN);
            if (quick_settings_box_ != nullptr) lv_obj_add_flag(quick_settings_box_, LV_OBJ_FLAG_HIDDEN);
            if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
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
        lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);
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
    LayoutMenuWidgets(is_portrait);
    LayoutQuickSettingsWidgets(is_portrait);
    LayoutCalendarWidgets(is_portrait);
    LayoutWeatherDetailWidgets(is_portrait);
    LayoutClockWidgets(is_portrait);
    LayoutStopwatchWidgets(is_portrait);
    LayoutTimerWidgets(is_portrait);
    LayoutExchangeRatesWidgets(is_portrait);

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

    if (screen_mode_ == kScreenClock) {
        UpdateClockUI();
    } else if (screen_mode_ == kScreenStopwatch) {
        UpdateStopwatchUI();
    } else if (screen_mode_ == kScreenTimer) {
        UpdateTimerUI();
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

    if (!is_portrait) {
        // Landscape (320x240) - UNCHANGED as user requested
        if (standby_clock_) lv_obj_set_pos(standby_clock_, 12, 8);
        if (standby_sec_) lv_obj_set_pos(standby_sec_, 128, 22);
        if (standby_weekday_) lv_obj_set_pos(standby_weekday_, 12, 52);
        if (standby_date_) lv_obj_set_pos(standby_date_, 12, 74);
        if (standby_lunar_) lv_obj_set_pos(standby_lunar_, 12, 96);
        if (standby_year_) lv_obj_add_flag(standby_year_, LV_OBJ_FLAG_HIDDEN);

        // Top-Right: Weather
        if (standby_weather_icon_) lv_obj_set_pos(standby_weather_icon_, 184, 8);
        if (standby_temp_) {
            lv_obj_set_size(standby_temp_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(standby_temp_, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_pos(standby_temp_, 236, 14);
        }

        if (standby_city_) lv_obj_set_pos(standby_city_, 184, 56);
        if (standby_condition_) lv_obj_set_pos(standby_condition_, 184, 76);
        if (standby_humidity_) lv_obj_set_pos(standby_humidity_, 184, 96);
        if (standby_wind_) lv_obj_set_pos(standby_wind_, 184, 116);

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
        if (standby_weekday_) lv_obj_set_pos(standby_weekday_, 12, 16);
        if (standby_date_) lv_obj_set_pos(standby_date_, 12, 46);
        if (standby_lunar_) lv_obj_set_pos(standby_lunar_, 12, 76);
        if (standby_year_) lv_obj_add_flag(standby_year_, LV_OBJ_FLAG_HIDDEN);

        // Top-Right: Giờ bên phải, số giây đếm nằm dưới phút (căn phải thẳng hàng với phút)
        if (standby_clock_) lv_obj_set_pos(standby_clock_, 126, 20);
        if (standby_sec_) lv_obj_set_pos(standby_sec_, 198, 62); // Directly under minutes (aligned right)

        // Mid-Left: Nhiệt độ xuống DƯỚI đám mây và căn giữa lại
        if (standby_weather_icon_) lv_obj_set_pos(standby_weather_icon_, 36, 126);
        if (standby_temp_) {
            lv_obj_set_size(standby_temp_, 116, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(standby_temp_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(standby_temp_, 0, 174);
        }

        // Mid-Right: Nâng các thông tin chi tiết lên và rải đều
        if (standby_city_) lv_obj_set_pos(standby_city_, 120, 126);
        if (standby_condition_) lv_obj_set_pos(standby_condition_, 120, 150);
        if (standby_humidity_) lv_obj_set_pos(standby_humidity_, 120, 174);
        if (standby_wind_) lv_obj_set_pos(standby_wind_, 120, 198);

        // Bottom: 4 Forecast Cards
        int card_w = 50;
        int card_h = 78;
        int gap = 8;
        int start_x = 8;
        int start_y = 236;

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
    if (screen_mode_ == kScreenMenu) {
        if (menu_page_ < 2) {
            SetMenuPage(menu_page_ + 1, true, true); // Swipe left: trượt trang sang trái (0 -> 1 -> 2)
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
    if (screen_mode_ == kScreenMenu) {
        if (menu_page_ > 0) {
            SetMenuPage(menu_page_ - 1, true, false); // Swipe right: trượt trang sang phải (2 -> 1 -> 0)
        }
    } else if (screen_mode_ == kScreenCalendar) {
        if (curr_cal_month_ == 1) {
            curr_cal_month_ = 12;
            curr_cal_year_--;
        } else {
            curr_cal_month_--;
        }
        UpdateCalendarUI();
    } else if (screen_mode_ == kScreenWeatherDetail || screen_mode_ == kScreenClock || screen_mode_ == kScreenExchangeRates) {
        ShowMenu();
    } else if (screen_mode_ == kScreenStopwatch || screen_mode_ == kScreenTimer) {
        ShowClock();
    }
}

void LcdDisplay::OnSwipeUp() {
    DisplayLockGuard lock(this);
    if (!lock) return;
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
        if (standby_box_ != nullptr) lv_obj_remove_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);
        screen_mode_ = kScreenStandby;
        return;
    }

    if (screen_mode_ == kScreenStandby) {
        ShowMenu(); // Swipe up from standby opens icon menu
    } else if (screen_mode_ == kScreenMenu) {
        ShowStandby(); // Swipe up from icon menu returns to standby
    } else if (screen_mode_ == kScreenQuickSettings) {
        ShowStandby(); // Swipe up from quick settings closes it
    } else if (screen_mode_ == kScreenCalendar || screen_mode_ == kScreenWeatherDetail ||
               screen_mode_ == kScreenClock || screen_mode_ == kScreenExchangeRates) {
        ShowMenu();
    } else if (screen_mode_ == kScreenStopwatch || screen_mode_ == kScreenTimer) {
        ShowClock(); // Thoát từ trang con về trang Đồng hồ chính!
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
    if (screen_mode_ == kScreenStandby || screen_mode_ == kScreenMenu) {
        ShowQuickSettings(); // Swipe down from standby or icon menu opens Quick Settings
    } else if (screen_mode_ == kScreenCalendar || screen_mode_ == kScreenWeatherDetail ||
               screen_mode_ == kScreenClock || screen_mode_ == kScreenExchangeRates) {
        ShowMenu();
    } else if (screen_mode_ == kScreenStopwatch || screen_mode_ == kScreenTimer) {
        ShowClock();
    }
}

void LcdDisplay::ShowStandby() {
    DisplayLockGuard lock(this);
    if (!lock) return;
    screen_mode_ = kScreenStandby;
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
    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);
    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);
    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);
    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    auto& app = Application::GetInstance();
    if (app.GetDeviceState() == kDeviceStateIdle) {
        if (content_ != nullptr) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);
        if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        if (wallpaper_image_ != nullptr) lv_obj_add_flag(wallpaper_image_, LV_OBJ_FLAG_HIDDEN);
        if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        if (standby_box_ != nullptr) lv_obj_remove_flag(standby_box_, LV_OBJ_FLAG_HIDDEN);
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
    if (calendar_box_ != nullptr) lv_obj_add_flag(calendar_box_, LV_OBJ_FLAG_HIDDEN);
    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);
    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);
    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);
    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

    // Nền màn hình chuẩn đen tuyền, không thấy chatbot
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    SetMenuPage(0); // Bắt đầu ở trang 1
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

void LcdDisplay::ShowQuickSettings() {
    DisplayLockGuard lock(this);
    if (!lock) return;
    screen_mode_ = kScreenQuickSettings;
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
    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);
    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);
    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);
    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

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

    if (quick_settings_box_ != nullptr) {
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
    if (clock_box_ != nullptr) lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);
    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);
    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

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
    if (weather_detail_box_ != nullptr) lv_obj_add_flag(weather_detail_box_, LV_OBJ_FLAG_HIDDEN);
    if (stopwatch_box_ != nullptr) lv_obj_add_flag(stopwatch_box_, LV_OBJ_FLAG_HIDDEN);
    if (timer_box_ != nullptr) lv_obj_add_flag(timer_box_, LV_OBJ_FLAG_HIDDEN);
    if (rates_box_ != nullptr) lv_obj_add_flag(rates_box_, LV_OBJ_FLAG_HIDDEN);

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
    for (int i = 0; i < 4; ++i) {
        if (forecast_cards_[i].day) lv_obj_set_style_text_color(forecast_cards_[i].day, col, 0);
        if (forecast_cards_[i].temp) lv_obj_set_style_text_color(forecast_cards_[i].temp, col, 0);
    }
}

void LcdDisplay::UpdateColorDotsSelection() {
    for (int i = 0; i < 10; ++i) {
        if (qs_color_dots_[i] == nullptr) continue;
        uint32_t col = (uint32_t)(intptr_t)lv_obj_get_user_data(qs_color_dots_[i]);
        if (col == standby_text_color_) {
            // Selected: viền dày màu trắng + vòng tròn bao quanh nổi bật (Apple-style selection ring)
            lv_obj_set_style_outline_width(qs_color_dots_[i], 2, 0);
            lv_obj_set_style_outline_color(qs_color_dots_[i], lv_color_white(), 0);
            lv_obj_set_style_outline_pad(qs_color_dots_[i], 2, 0);
            lv_obj_set_style_outline_opa(qs_color_dots_[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(qs_color_dots_[i], 2, 0);
            lv_obj_set_style_border_color(qs_color_dots_[i], lv_color_white(), 0);
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
    if (page > 2) page = 2;
    int old_page = menu_page_;
    menu_page_ = page;

    // Cập nhật dấu chấm trang
    for (int i = 0; i < 3; ++i) {
        if (menu_dots_[i] == nullptr) continue;
        if (i == page) {
            lv_obj_set_style_bg_color(menu_dots_[i], lv_color_white(), 0);
            lv_obj_set_style_bg_opa(menu_dots_[i], LV_OPA_COVER, 0);
            lv_obj_set_size(menu_dots_[i], 14, 6);
            lv_obj_set_style_radius(menu_dots_[i], 3, 0);
        } else {
            lv_obj_set_style_bg_color(menu_dots_[i], lv_color_hex(0x64748B), 0);
            lv_obj_set_style_bg_opa(menu_dots_[i], LV_OPA_70, 0);
            lv_obj_set_size(menu_dots_[i], 6, 6);
            lv_obj_set_style_radius(menu_dots_[i], 3, 0);
        }
    }

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

    const char* titles[22] = {
        // Trang 1 (9 icons)
        "Lịch", "Đồng hồ", "Thời tiết",
        "Tỷ giá", "Trợ lý AI", "Cài đặt",
        "Âm nhạc", "Hình nền", "Thư viện",
        // Trang 2 (9 icons)
        "Ghi chú", "Báo thức", "Sức khỏe",
        "Tin tức", "Thiết bị", "Đài FM",
        "Mã QR", "La bàn", "Máy tính",
        // Trang 3 (4 icons)
        "Đèn pin", "Trò chơi", "Dịch thuật", "Hẹn giờ"
    };

    time_t now_cal = time(nullptr);
    struct tm* tm_now = localtime(&now_cal);
    int cur_day = (tm_now != nullptr && tm_now->tm_mday >= 1) ? tm_now->tm_mday : 7;
    int cur_wday = (tm_now != nullptr) ? tm_now->tm_wday : 6;
    const char* wday_names[7] = {"CN", "T.2", "T.3", "T.4", "T.5", "T.6", "T.7"};

    for (int i = 0; i < 22; ++i) {
        int p = (i < 9) ? 0 : ((i < 18) ? 1 : 2);
        lv_obj_t* btn = lv_obj_create(menu_page_box_[p]);
        menu_items_[i] = btn;
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* tile = lv_obj_create(btn);
        lv_obj_set_size(tile, 44, 44);
        lv_obj_align(tile, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(tile, 11, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE);

        if (i == 0) {
            // Lịch (iOS Calendar)
            lv_obj_set_style_bg_color(tile, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

            lv_obj_t* red_bar = lv_obj_create(tile);
            lv_obj_set_size(red_bar, 44, 14);
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
            lv_obj_set_style_text_font(day_lbl, &font_noto_sans_basic_16_4, 0);
            lv_obj_set_style_text_color(day_lbl, lv_color_hex(0x1C1C1E), 0);
            lv_obj_align(day_lbl, LV_ALIGN_CENTER, 0, 6);
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
            // Tỷ giá (Sleek Finance / Markets badge)
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x0F172A), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

            lv_obj_t* circle = lv_obj_create(tile);
            lv_obj_set_size(circle, 32, 32);
            lv_obj_set_style_radius(circle, 16, 0);
            lv_obj_set_style_bg_color(circle, lv_color_hex(0x10B981), 0);
            lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(circle, 0, 0);
            lv_obj_set_style_pad_all(circle, 0, 0);
            lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
            lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t* icon = lv_label_create(circle);
            lv_label_set_text(icon, "$");
            lv_obj_set_style_text_font(icon, &font_noto_sans_basic_20_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 4) {
            // Trợ lý AI
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x34C759), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_CHAT_BUBBLE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 5) {
            // Cài đặt
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x8E8E93), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_SETTINGS);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 6) {
            // Âm nhạc
            lv_obj_set_style_bg_color(tile, lv_color_hex(0xFA2D48), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_MUSIC_NOTE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 7) {
            // Hình nền
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x5856D6), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_IMAGE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 8) {
            // Thư viện
            lv_obj_set_style_bg_color(tile, lv_color_hex(0xF39C12), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_PHOTO_CAMERA);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 9) {
            // Ghi chú
            lv_obj_set_style_bg_color(tile, lv_color_hex(0xF59E0B), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_EDIT_SQUARE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 10) {
            // Báo thức
            lv_obj_set_style_bg_color(tile, lv_color_hex(0xF97316), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_ALARM);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 11) {
            // Sức khỏe
            lv_obj_set_style_bg_color(tile, lv_color_hex(0xF43F5E), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_FAVORITE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 12) {
            // Tin tức
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x0284C7), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_EXPLORE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 13) {
            // Thiết bị
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x6366F1), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_INFO);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 14) {
            // Đài FM
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x8B5CF6), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, "FM");
            lv_obj_set_style_text_font(icon, &font_noto_sans_basic_20_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 15) {
            // Mã QR
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x0284C7), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, "[QR]");
            lv_obj_set_style_text_font(icon, &font_noto_sans_basic_16_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 16) {
            // La bàn
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_NAVIGATION);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_hex(0xEF4444), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 17) {
            // Máy tính
            lv_obj_set_style_bg_color(tile, lv_color_hex(0xF59E0B), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_CALCULATE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 18) {
            // Đèn pin
            lv_obj_set_style_bg_color(tile, lv_color_hex(0xEAB308), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_BRIGHTNESS_6);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 19) {
            // Trò chơi
            lv_obj_set_style_bg_color(tile, lv_color_hex(0xEC4899), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_SPORTS_ESPORTS);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 20) {
            // Dịch thuật
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x06B6D4), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_LANGUAGE);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        } else if (i == 21) {
            // Hẹn giờ
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x10B981), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_t* icon = lv_label_create(tile);
            lv_label_set_text(icon, MATERIAL_SYMBOLS_WATCH);
            lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        }

        // Title label below tile
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, titles[i]);
        lv_obj_set_style_text_font(lbl, &font_noto_sans_custom_14_1, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
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
                self->ShowNotification("Âm nhạc & Âm thanh", 1500);
            } else if (index == 7) {
                Board::GetInstance().NextWallpaper();
            } else if (index == 8) {
                self->ShowNotification("Thư viện ảnh", 1500);
            } else if (index == 9) {
                self->ShowNotification("Ghi chú & Lời nhắc", 1500);
            } else if (index == 10) {
                self->ShowTimer();
            } else if (index == 11) {
                self->ShowNotification("Sức khỏe & Hoạt động", 1500);
            } else if (index == 12) {
                self->ShowNotification("Tin tức tổng hợp", 1500);
            } else if (index == 13) {
                char dev_buf[64];
                snprintf(dev_buf, sizeof(dev_buf), "ESP32-S3 | RAM: %lu KB", (unsigned long)(esp_get_free_heap_size() / 1024));
                self->ShowNotification(dev_buf, 2000);
            } else if (index == 14) {
                self->ShowNotification("Đài FM Trực Tuyến", 1500);
            } else if (index == 15) {
                self->ShowNotification("Mã QR: Quét kết nối", 1800);
            } else if (index == 16) {
                self->ShowNotification("La bàn: Hướng Bắc 0°", 1500);
            } else if (index == 17) {
                self->ShowNotification("Máy tính cá nhân", 1500);
            } else if (index == 18) {
                if (Board::GetInstance().GetBacklight()) {
                    Board::GetInstance().GetBacklight()->SetBrightness(100, false);
                }
                self->ShowNotification("Đèn pin: Bật sáng 100%", 1500);
            } else if (index == 19) {
                int dice = (esp_random() % 6) + 1;
                char gbuf[32];
                snprintf(gbuf, sizeof(gbuf), "Xúc xắc: %d điểm!", dice);
                self->ShowNotification(gbuf, 1800);
            } else if (index == 20) {
                self->ShowNotification("Dịch thuật AI", 1500);
            } else if (index == 21) {
                self->ShowTimer();
            }
        }, LV_EVENT_CLICKED, this);
    }

    // Indicator dots (3 pages)
    for (int i = 0; i < 3; ++i) {
        menu_dots_[i] = lv_obj_create(menu_box_);
        lv_obj_set_size(menu_dots_[i], 6, 6);
        lv_obj_set_style_radius(menu_dots_[i], 3, 0);
        lv_obj_set_style_border_width(menu_dots_[i], 0, 0);
        lv_obj_set_style_pad_all(menu_dots_[i], 0, 0);
        lv_obj_clear_flag(menu_dots_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(menu_dots_[i], LV_OBJ_FLAG_CLICKABLE);
    }

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

    if (is_portrait) {
        // 240 x 320: 3 columns x 3 rows (Không còn chữ "ỨNG DỤNG", lề trên thoáng đãng)
        int col_x[3] = {4, 83, 162};
        int row_y[3] = {20, 104, 188};
        for (int i = 0; i < 22; ++i) {
            if (menu_items_[i] == nullptr) continue;
            int page_idx = (i < 9) ? i : ((i < 18) ? (i - 9) : (i - 18));
            int r = page_idx / 3;
            int c = page_idx % 3;
            lv_obj_set_size(menu_items_[i], 74, 72);
            lv_obj_set_pos(menu_items_[i], col_x[c], row_y[r]);

            lv_obj_t* tile = lv_obj_get_child(menu_items_[i], 0);
            if (tile) {
                lv_obj_set_size(tile, 46, 46);
                lv_obj_set_style_radius(tile, 12, 0);
            }
            lv_obj_t* lbl = lv_obj_get_child(menu_items_[i], 1);
            if (lbl) {
                lv_obj_set_width(lbl, 74);
                lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 0);
            }
        }
        if (menu_dots_[0] && menu_dots_[1] && menu_dots_[2]) {
            lv_obj_set_pos(menu_dots_[0], 120 - 18, 282);
            lv_obj_set_pos(menu_dots_[1], 120 - 4, 282);
            lv_obj_set_pos(menu_dots_[2], 120 + 10, 282);
        }
    } else {
        // 320 x 240: 3 columns x 3 rows
        int col_x[3] = {16, 116, 216};
        int row_y[3] = {16, 80, 144};
        for (int i = 0; i < 22; ++i) {
            if (menu_items_[i] == nullptr) continue;
            int page_idx = (i < 9) ? i : ((i < 18) ? (i - 9) : (i - 18));
            int r = page_idx / 3;
            int c = page_idx % 3;
            lv_obj_set_size(menu_items_[i], 88, 58);
            lv_obj_set_pos(menu_items_[i], col_x[c], row_y[r]);

            lv_obj_t* tile = lv_obj_get_child(menu_items_[i], 0);
            if (tile) {
                lv_obj_set_size(tile, 38, 38);
                lv_obj_set_style_radius(tile, 10, 0);
            }
            lv_obj_t* lbl = lv_obj_get_child(menu_items_[i], 1);
            if (lbl) {
                lv_obj_set_width(lbl, 88);
                lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 0);
            }
        }
        if (menu_dots_[0] && menu_dots_[1] && menu_dots_[2]) {
            lv_obj_set_pos(menu_dots_[0], 160 - 18, 216);
            lv_obj_set_pos(menu_dots_[1], 160 - 4, 216);
            lv_obj_set_pos(menu_dots_[2], 160 + 10, 216);
        }
    }

    for (int i = 0; i < 3; ++i) {
        if (menu_dots_[i]) lv_obj_move_foreground(menu_dots_[i]);
    }
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

    // Title
    qs_title_ = lv_label_create(quick_settings_box_);
    lv_label_set_text(qs_title_, "Cài đặt nhanh");
    lv_obj_set_style_text_font(qs_title_, &font_noto_sans_custom_16_4, 0);
    lv_obj_set_style_text_color(qs_title_, lv_color_white(), 0);

    // Close button
    qs_close_btn_ = lv_button_create(quick_settings_box_);
    lv_obj_set_size(qs_close_btn_, 32, 28);
    lv_obj_set_style_bg_opa(qs_close_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(qs_close_btn_, 0, 0);
    lv_obj_set_style_pad_all(qs_close_btn_, 0, 0);
    lv_obj_t* close_lbl = lv_label_create(qs_close_btn_);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_font(close_lbl, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_align(close_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(qs_close_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
        self->ShowStandby();
    }, LV_EVENT_CLICKED, this);

    // Sliders Card
    qs_sliders_card_ = lv_obj_create(quick_settings_box_);
    lv_obj_set_style_bg_color(qs_sliders_card_, lv_color_hex(0x151e2e), 0);
    lv_obj_set_style_bg_opa(qs_sliders_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qs_sliders_card_, 10, 0);
    lv_obj_set_style_border_width(qs_sliders_card_, 0, 0);
    lv_obj_set_style_pad_all(qs_sliders_card_, 0, 0);
    lv_obj_clear_flag(qs_sliders_card_, LV_OBJ_FLAG_SCROLLABLE);

    // Brightness row
    lv_obj_t* b_icon = lv_label_create(qs_sliders_card_);
    lv_label_set_text(b_icon, MATERIAL_SYMBOLS_BRIGHTNESS_6);
    lv_obj_set_style_text_font(b_icon, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(b_icon, lv_color_hex(0xFBBF24), 0);
    lv_obj_set_user_data(b_icon, (void*)(intptr_t)101);

    qs_brightness_slider_ = lv_slider_create(qs_sliders_card_);
    lv_slider_set_range(qs_brightness_slider_, 10, 100);
    int b_val = Board::GetInstance().GetBacklight() ? Board::GetInstance().GetBacklight()->brightness() : 100;
    lv_slider_set_value(qs_brightness_slider_, b_val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(qs_brightness_slider_, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_bg_color(qs_brightness_slider_, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(qs_brightness_slider_, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_radius(qs_brightness_slider_, 10, LV_PART_INDICATOR);
    lv_obj_set_style_radius(qs_brightness_slider_, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(qs_brightness_slider_, 4, LV_PART_KNOB);
    lv_obj_set_ext_click_area(qs_brightness_slider_, 6);

    qs_brightness_label_ = lv_label_create(qs_sliders_card_);
    char b_buf[16];
    snprintf(b_buf, sizeof(b_buf), "%d%%", b_val);
    lv_label_set_text(qs_brightness_label_, b_buf);
    lv_obj_set_style_text_font(qs_brightness_label_, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(qs_brightness_label_, lv_color_white(), 0);

    lv_obj_add_event_cb(qs_brightness_slider_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        int val = lv_slider_get_value(self->qs_brightness_slider_);
        if (Board::GetInstance().GetBacklight() != nullptr) {
            Board::GetInstance().GetBacklight()->SetBrightness(val, false); // Mượt mà tức thì, không ghi NVS
        }
        if (self->qs_brightness_label_ != nullptr) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%", val);
            lv_label_set_text(self->qs_brightness_label_, buf);
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_add_event_cb(qs_brightness_slider_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        int val = lv_slider_get_value(self->qs_brightness_slider_);
        if (Board::GetInstance().GetBacklight() != nullptr) {
            Board::GetInstance().GetBacklight()->SetBrightness(val, true); // Lưu NVS 1 lần khi buông tay
        }
    }, LV_EVENT_RELEASED, this);

    // Volume row
    lv_obj_t* v_icon = lv_label_create(qs_sliders_card_);
    lv_label_set_text(v_icon, MATERIAL_SYMBOLS_VOLUME_UP);
    lv_obj_set_style_text_font(v_icon, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(v_icon, lv_color_hex(0x4ADE80), 0);
    lv_obj_set_user_data(v_icon, (void*)(intptr_t)102);

    qs_volume_slider_ = lv_slider_create(qs_sliders_card_);
    lv_slider_set_range(qs_volume_slider_, 0, 100);
    int v_val = Board::GetInstance().GetAudioCodec() ? Board::GetInstance().GetAudioCodec()->output_volume() : 80;
    lv_slider_set_value(qs_volume_slider_, v_val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(qs_volume_slider_, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_bg_color(qs_volume_slider_, lv_color_hex(0x4ADE80), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(qs_volume_slider_, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_radius(qs_volume_slider_, 10, LV_PART_INDICATOR);
    lv_obj_set_style_radius(qs_volume_slider_, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(qs_volume_slider_, 4, LV_PART_KNOB);
    lv_obj_set_ext_click_area(qs_volume_slider_, 6);

    qs_volume_label_ = lv_label_create(qs_sliders_card_);
    char v_buf[16];
    snprintf(v_buf, sizeof(v_buf), "%d%%", v_val);
    lv_label_set_text(qs_volume_label_, v_buf);
    lv_obj_set_style_text_font(qs_volume_label_, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(qs_volume_label_, lv_color_white(), 0);

    lv_obj_add_event_cb(qs_volume_slider_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        int val = lv_slider_get_value(self->qs_volume_slider_);
        if (self->qs_volume_label_ != nullptr) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%", val);
            lv_label_set_text(self->qs_volume_label_, buf);
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_add_event_cb(qs_volume_slider_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        int val = lv_slider_get_value(self->qs_volume_slider_);
        if (Board::GetInstance().GetAudioCodec() != nullptr) {
            Board::GetInstance().GetAudioCodec()->SetOutputVolume(val); // Lưu âm lượng NVS khi buông tay
        }
    }, LV_EVENT_RELEASED, this);

    // Text Color Card
    qs_color_card_ = lv_obj_create(quick_settings_box_);
    lv_obj_set_style_bg_color(qs_color_card_, lv_color_hex(0x151e2e), 0);
    lv_obj_set_style_bg_opa(qs_color_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qs_color_card_, 10, 0);
    lv_obj_set_style_border_width(qs_color_card_, 0, 0);
    lv_obj_set_style_pad_all(qs_color_card_, 0, 0);
    lv_obj_clear_flag(qs_color_card_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* col_title = lv_label_create(qs_color_card_);
    lv_label_set_text(col_title, "Màu chữ màn hình");
    lv_obj_set_style_text_font(col_title, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(col_title, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_user_data(col_title, (void*)(intptr_t)103);

    const uint32_t colors[10] = {
        0xFFFFFF, // Trắng sáng
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
    for (int i = 0; i < 10; ++i) {
        lv_obj_t* dot = lv_obj_create(qs_color_card_);
        qs_color_dots_[i] = dot;
        lv_obj_set_style_bg_color(dot, lv_color_hex(colors[i]), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(0x475569), 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(dot, 8);
        lv_obj_set_user_data(dot, (void*)(intptr_t)colors[i]);

        lv_obj_add_event_cb(dot, [](lv_event_t* e) {
            auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
            auto* target = lv_event_get_current_target_obj(e);
            uint32_t col = (uint32_t)(intptr_t)lv_obj_get_user_data(target);
            self->SetStandbyTextColor(col);
            self->ShowNotification("Đã đổi màu chữ", 1200);
        }, LV_EVENT_CLICKED, this);
    }

    // Action Buttons:
    // AI Assistant Button
    qs_ai_btn_ = lv_button_create(quick_settings_box_);
    lv_obj_set_style_bg_color(qs_ai_btn_, lv_color_hex(0x16A34A), 0);
    lv_obj_set_style_bg_opa(qs_ai_btn_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qs_ai_btn_, 8, 0);
    lv_obj_set_style_border_width(qs_ai_btn_, 0, 0);
    lv_obj_set_style_pad_all(qs_ai_btn_, 0, 0);
    lv_obj_t* ai_lbl = lv_label_create(qs_ai_btn_);
    lv_label_set_text(ai_lbl, MATERIAL_SYMBOLS_MIC "  Trợ lý ảo");
    lv_obj_set_style_text_font(ai_lbl, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(ai_lbl, lv_color_white(), 0);
    lv_obj_align(ai_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(qs_ai_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
        self->ShowStandby();
        Application::GetInstance().ToggleChatState();
    }, LV_EVENT_CLICKED, this);

    // Rotate Screen Button
    qs_rotate_btn_ = lv_button_create(quick_settings_box_);
    lv_obj_set_style_bg_color(qs_rotate_btn_, lv_color_hex(0x334155), 0);
    lv_obj_set_style_bg_opa(qs_rotate_btn_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qs_rotate_btn_, 8, 0);
    lv_obj_set_style_border_width(qs_rotate_btn_, 0, 0);
    lv_obj_set_style_pad_all(qs_rotate_btn_, 0, 0);
    lv_obj_t* rot_lbl = lv_label_create(qs_rotate_btn_);
    lv_label_set_text(rot_lbl, MATERIAL_SYMBOLS_REPEAT "  Xoay màn hình");
    lv_obj_set_style_text_font(rot_lbl, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(rot_lbl, lv_color_white(), 0);
    lv_obj_align(rot_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(qs_rotate_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
        Board::GetInstance().ToggleRotation("toggle");
    }, LV_EVENT_CLICKED, this);

    // Restart Button
    qs_restart_btn_ = lv_button_create(quick_settings_box_);
    lv_obj_set_style_bg_color(qs_restart_btn_, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_bg_opa(qs_restart_btn_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qs_restart_btn_, 8, 0);
    lv_obj_set_style_border_width(qs_restart_btn_, 0, 0);
    lv_obj_set_style_pad_all(qs_restart_btn_, 0, 0);
    lv_obj_t* rst_lbl = lv_label_create(qs_restart_btn_);
    lv_label_set_text(rst_lbl, MATERIAL_SYMBOLS_POWER_SETTINGS_NEW "  Khởi động lại");
    lv_obj_set_style_text_font(rst_lbl, &font_noto_sans_custom_14_1, 0);
    lv_obj_set_style_text_color(rst_lbl, lv_color_white(), 0);
    lv_obj_align(rst_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(qs_restart_btn_, [](lv_event_t* e) {
        auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (esp_timer_get_time() / 1000 - self->last_swipe_time_ < 500) return;
        self->ShowNotification("Đang khởi động lại...", 2000);
        esp_timer_handle_t timer;
        esp_timer_create_args_t args = {
            .callback = [](void* arg) {
                esp_restart();
            },
            .name = "qs_restart"
        };
        esp_timer_create(&args, &timer);
        esp_timer_start_once(timer, 600000);
    }, LV_EVENT_CLICKED, this);

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
        // 240 x 320
        if (qs_title_) lv_obj_align(qs_title_, LV_ALIGN_TOP_MID, 0, 10);
        if (qs_close_btn_) lv_obj_set_pos(qs_close_btn_, 202, 6);

        if (qs_sliders_card_) {
            lv_obj_set_size(qs_sliders_card_, 220, 104);
            lv_obj_set_pos(qs_sliders_card_, 10, 40);
        }
        if (b_icon) lv_obj_set_pos(b_icon, 8, 8);
        if (qs_brightness_slider_) {
            lv_obj_set_size(qs_brightness_slider_, 112, 20);
            lv_obj_set_pos(qs_brightness_slider_, 38, 12);
        }
        if (qs_brightness_label_) lv_obj_set_pos(qs_brightness_label_, 172, 14);

        if (v_icon) lv_obj_set_pos(v_icon, 8, 62);
        if (qs_volume_slider_) {
            lv_obj_set_size(qs_volume_slider_, 112, 20);
            lv_obj_set_pos(qs_volume_slider_, 38, 66);
        }
        if (qs_volume_label_) lv_obj_set_pos(qs_volume_label_, 172, 68);

        if (qs_color_card_) {
            lv_obj_set_size(qs_color_card_, 220, 80);
            lv_obj_set_pos(qs_color_card_, 10, 148);
        }
        if (col_title) lv_obj_set_pos(col_title, 10, 4);
        if (qs_color_card_) {
            int dot_idx = 0;
            uint32_t cnt = lv_obj_get_child_count(qs_color_card_);
            for (uint32_t i = 0; i < cnt; ++i) {
                lv_obj_t* c = lv_obj_get_child(qs_color_card_, i);
                if (c != col_title) {
                    int row = dot_idx / 5;
                    int col = dot_idx % 5;
                    lv_obj_set_size(c, 24, 24);
                    lv_obj_set_style_radius(c, 12, 0);
                    lv_obj_set_pos(c, 12 + col * 40, 22 + row * 27);
                    dot_idx++;
                }
            }
        }

        if (qs_ai_btn_) {
            lv_obj_set_size(qs_ai_btn_, 220, 36);
            lv_obj_set_pos(qs_ai_btn_, 10, 234);
        }
        if (qs_rotate_btn_) {
            lv_obj_set_size(qs_rotate_btn_, 105, 36);
            lv_obj_set_pos(qs_rotate_btn_, 10, 274);
        }
        if (qs_restart_btn_) {
            lv_obj_set_size(qs_restart_btn_, 105, 36);
            lv_obj_set_pos(qs_restart_btn_, 125, 274);
        }
    } else {
        // 320 x 240 (Màn hình ngang theo bố cục mới: thanh trượt full màn -> chọn màu -> 3 nút chức năng)
        if (qs_title_) lv_obj_set_pos(qs_title_, 14, 6);
        if (qs_close_btn_) lv_obj_set_pos(qs_close_btn_, 282, 4);

        // 1. Thanh kéo âm thanh & độ sáng kéo dài hết màn hình (width = 304, height = 82)
        // Xích xuống pos y = 32 để không che chữ 'Cài đặt nhanh' ở trên
        if (qs_sliders_card_) {
            lv_obj_set_size(qs_sliders_card_, 304, 82);
            lv_obj_set_pos(qs_sliders_card_, 8, 32);
        }
        if (b_icon) lv_obj_set_pos(b_icon, 8, 5);
        if (qs_brightness_slider_) {
            lv_obj_set_size(qs_brightness_slider_, 196, 20);
            lv_obj_set_pos(qs_brightness_slider_, 36, 9);
        }
        if (qs_brightness_label_) lv_obj_set_pos(qs_brightness_label_, 256, 11);

        if (v_icon) lv_obj_set_pos(v_icon, 8, 49);
        if (qs_volume_slider_) {
            lv_obj_set_size(qs_volume_slider_, 196, 20);
            lv_obj_set_pos(qs_volume_slider_, 36, 53);
        }
        if (qs_volume_label_) lv_obj_set_pos(qs_volume_label_, 256, 55);

        // 2. Phía dưới là bảng chọn màu chữ (width = 304, pos y = 120)
        if (qs_color_card_) {
            lv_obj_set_size(qs_color_card_, 304, 48);
            lv_obj_set_pos(qs_color_card_, 8, 120);
        }
        if (col_title) lv_obj_set_pos(col_title, 10, 2);
        if (qs_color_card_) {
            int dot_idx = 0;
            uint32_t cnt = lv_obj_get_child_count(qs_color_card_);
            for (uint32_t i = 0; i < cnt; ++i) {
                lv_obj_t* c = lv_obj_get_child(qs_color_card_, i);
                if (c != col_title) {
                    lv_obj_set_size(c, 20, 20);
                    lv_obj_set_style_radius(c, 10, 0);
                    lv_obj_set_pos(c, 16 + dot_idx * 28, 21);
                    dot_idx++;
                }
            }
        }

        // 3. Cuối cùng là các chức năng trợ lý, xoay màn hình, khởi động lại (pos y = 174)
        if (qs_ai_btn_) {
            lv_obj_set_size(qs_ai_btn_, 96, 34);
            lv_obj_set_pos(qs_ai_btn_, 8, 174);
        }
        if (qs_rotate_btn_) {
            lv_obj_set_size(qs_rotate_btn_, 96, 34);
            lv_obj_set_pos(qs_rotate_btn_, 112, 174);
        }
        if (qs_restart_btn_) {
            lv_obj_set_size(qs_restart_btn_, 96, 34);
            lv_obj_set_pos(qs_restart_btn_, 216, 174);
        }
    }

    UpdateColorDotsSelection();
}

static const char* GetHolidayOrEvent(int s_day, int s_month, int l_day, int l_month, int wday) {
    // Dương lịch
    if (s_day == 1 && s_month == 1) return "Tết Dương Lịch";
    if (s_day == 14 && s_month == 2) return "Lễ Tình Nhân (Valentine)";
    if (s_day == 8 && s_month == 3) return "Quốc tế Phụ nữ (8/3)";
    if (s_day == 26 && s_month == 3) return "Ngày Thành lập Đoàn TNCS";
    if (s_day == 30 && s_month == 4) return "Giải phóng miền Nam (30/4)";
    if (s_day == 1 && s_month == 5) return "Quốc tế Lao động (1/5)";
    if (s_day == 1 && s_month == 6) return "Quốc tế Thiếu nhi (1/6)";
    if (s_day == 27 && s_month == 7) return "Ngày Thương binh Liệt sĩ";
    if (s_day == 19 && s_month == 8) return "Cách mạng Tháng Tám";
    if (s_day == 2 && s_month == 9) return "Quốc khánh Việt Nam (2/9)";
    if (s_day == 10 && s_month == 10) return "Giải phóng Thủ đô (10/10)";
    if (s_day == 20 && s_month == 10) return "Ngày Phụ nữ Việt Nam (20/10)";
    if (s_day == 20 && s_month == 11) return "Ngày Nhà giáo Việt Nam (20/11)";
    if (s_day == 22 && s_month == 12) return "Ngày Quân đội Nhân dân VN";
    if (s_day == 24 && s_month == 12) return "Đêm Giáng sinh (Noel)";
    if (s_day == 25 && s_month == 12) return "Lễ Giáng sinh (Noel)";
    if (s_day == 31 && s_month == 12) return "Đêm Giao thừa Dương lịch";

    // Âm lịch
    if (l_day == 23 && l_month == 12) return "Tiễn Ông Táo về trời";
    if (l_day >= 29 && l_month == 12) return "Đêm Giao thừa Âm lịch";
    if (l_day == 1 && l_month == 1) return "Mùng 1 Tết Nguyên Đán";
    if (l_day == 2 && l_month == 1) return "Mùng 2 Tết Nguyên Đán";
    if (l_day == 3 && l_month == 1) return "Mùng 3 Tết Nguyên Đán";
    if (l_day == 15 && l_month == 1) return "Tết Nguyên Tiêu (Rằm T.1)";
    if (l_day == 10 && l_month == 3) return "Giỗ Tổ Hùng Vương (10/3 ÂL)";
    if (l_day == 15 && l_month == 4) return "Lễ Phật Đản (Rằm T.4)";
    if (l_day == 5 && l_month == 5) return "Tết Đoan Ngọ (5/5 ÂL)";
    if (l_day == 15 && l_month == 7) return "Vu Lan Báo Hiếu (Rằm T.7)";
    if (l_day == 15 && l_month == 8) return "Tết Trung Thu (Rằm T.8)";

    if (wday == 0) return "Chủ nhật: Thư giãn & nạp năng lượng!";
    if (wday == 6) return "Thứ bảy: Cuối tuần vui vẻ bên gia đình!";
    return "Chúc bạn ngày mới tràn đầy niềm vui!";
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
    lv_obj_set_style_text_font(calendar_title_label_, &font_noto_sans_basic_20_4, 0);
    lv_obj_set_style_text_color(calendar_title_label_, lv_color_white(), 0);

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
        lv_obj_set_style_radius(cell, 6, 0);
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
        lv_obj_set_style_text_font(solar, &font_noto_sans_basic_16_4, 0); // Lịch dương to rõ
        lv_obj_set_style_text_color(solar, lv_color_white(), 0);
        lv_obj_align(solar, LV_ALIGN_TOP_MID, 0, 1);
        lv_obj_clear_flag(solar, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lunar = lv_label_create(cell);
        calendar_cells_[i].lunar_label = lunar;
        lv_obj_set_style_text_font(lunar, &font_noto_sans_basic_14_1, 0);
        lv_obj_set_style_text_color(lunar, lv_color_hex(0x94A3B8), 0);
        lv_obj_align(lunar, LV_ALIGN_BOTTOM_MID, 0, -1); // Lịch âm lùi xuống đáy ô, không đè lên lịch dương
        lv_obj_clear_flag(lunar, LV_OBJ_FLAG_CLICKABLE);
    }

    LayoutCalendarWidgets(height_ > width_);
}

void LcdDisplay::LayoutCalendarWidgets(bool is_portrait) {
    if (calendar_box_ == nullptr) return;
    lv_obj_set_size(calendar_box_, width_, height_);

    if (is_portrait) {
        // 240 x 320: Dịch các nút xuống y = 14..18 để không bị che bởi khung viền
        if (calendar_back_btn_) lv_obj_set_pos(calendar_back_btn_, 6, 12);
        if (calendar_prev_btn_) lv_obj_set_pos(calendar_prev_btn_, 46, 12);
        if (calendar_title_label_) lv_obj_align(calendar_title_label_, LV_ALIGN_TOP_MID, 0, 16);
        if (calendar_next_btn_) lv_obj_set_pos(calendar_next_btn_, 196, 12);

        uint32_t cnt = lv_obj_get_child_count(calendar_box_);
        for (uint32_t i = 0; i < cnt; ++i) {
            lv_obj_t* child = lv_obj_get_child(calendar_box_, i);
            int id = (int)(intptr_t)lv_obj_get_user_data(child);
            if (id >= 100 && id < 107) {
                int col = id - 100;
                lv_obj_set_size(child, 33, 16);
                lv_obj_set_style_text_align(child, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_set_pos(child, 4 + col * 33, 44);
            }
        }

        // 6 hàng ngày tháng
        for (int i = 0; i < 42; ++i) {
            if (calendar_cells_[i].container == nullptr) continue;
            int r = i / 7;
            int c = i % 7;
            lv_obj_set_size(calendar_cells_[i].container, 32, 28);
            lv_obj_set_pos(calendar_cells_[i].container, 4 + c * 33, 62 + r * 28);
        }

        // Today Card phía dưới ở màn hình dọc
        if (cal_today_card_) {
            lv_obj_set_size(cal_today_card_, 228, 80);
            lv_obj_set_pos(cal_today_card_, 6, 234);
            if (cal_today_title_) lv_obj_set_pos(cal_today_title_, 8, 4);
            if (cal_today_wday_) lv_obj_set_pos(cal_today_wday_, 80, 4);
            if (cal_today_lunar_) lv_obj_set_pos(cal_today_lunar_, 156, 4);
            if (cal_today_month_year_) lv_obj_set_pos(cal_today_month_year_, 8, 22);
            if (cal_today_day_) lv_obj_add_flag(cal_today_day_, LV_OBJ_FLAG_HIDDEN);
            if (cal_today_event_) {
                lv_obj_set_width(cal_today_event_, 212);
                lv_obj_set_pos(cal_today_event_, 8, 42);
            }
        }
    } else {
        // 320 x 240: Có 1 bên là ngày hôm nay/đã chọn + sự kiện, bên phải là lịch tháng
        if (calendar_back_btn_) lv_obj_set_pos(calendar_back_btn_, 6, 6);

        if (cal_today_card_) {
            lv_obj_set_size(cal_today_card_, 102, 192);
            lv_obj_set_pos(cal_today_card_, 6, 40);
            if (cal_today_title_) lv_obj_align(cal_today_title_, LV_ALIGN_TOP_MID, 0, 4);
            if (cal_today_day_) {
                lv_obj_remove_flag(cal_today_day_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_align(cal_today_day_, LV_ALIGN_TOP_MID, 0, 18);
            }
            if (cal_today_month_year_) lv_obj_align(cal_today_month_year_, LV_ALIGN_TOP_MID, 0, 52);
            if (cal_today_wday_) lv_obj_align(cal_today_wday_, LV_ALIGN_TOP_MID, 0, 70);
            if (cal_today_lunar_) lv_obj_align(cal_today_lunar_, LV_ALIGN_TOP_MID, 0, 88);
            if (cal_today_event_) {
                lv_obj_set_width(cal_today_event_, 94);
                lv_obj_set_style_text_align(cal_today_event_, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_align(cal_today_event_, LV_ALIGN_TOP_MID, 0, 108);
            }
        }

        // Bên phải (x = 114..316): Các nút điều hướng to rõ và bảng ngày tháng rộng rãi, không bị khuất
        if (calendar_prev_btn_) {
            lv_obj_set_size(calendar_prev_btn_, 32, 28);
            lv_obj_set_pos(calendar_prev_btn_, 116, 4);
        }
        if (calendar_title_label_) {
            lv_obj_set_size(calendar_title_label_, 132, 26);
            lv_obj_set_style_text_align(calendar_title_label_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(calendar_title_label_, 148, 6);
        }
        if (calendar_next_btn_) {
            lv_obj_set_size(calendar_next_btn_, 32, 28);
            lv_obj_set_pos(calendar_next_btn_, 280, 4);
        }

        uint32_t cnt = lv_obj_get_child_count(calendar_box_);
        for (uint32_t i = 0; i < cnt; ++i) {
            lv_obj_t* child = lv_obj_get_child(calendar_box_, i);
            int id = (int)(intptr_t)lv_obj_get_user_data(child);
            if (id >= 100 && id < 107) {
                int col = id - 100;
                lv_obj_set_size(child, 28, 14);
                lv_obj_set_style_text_align(child, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_set_pos(child, 116 + col * 28, 34);
            }
        }

        for (int i = 0; i < 42; ++i) {
            if (calendar_cells_[i].container == nullptr) continue;
            int r = i / 7;
            int c = i % 7;
            lv_obj_set_size(calendar_cells_[i].container, 28, 30);
            lv_obj_set_pos(calendar_cells_[i].container, 116 + c * 28, 50 + r * 31);
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
    int today_wday = (tm_now != nullptr) ? tm_now->tm_wday : 6;

    int show_day = (selected_cal_day_ > 0) ? selected_cal_day_ : today_day;

    struct tm st = {};
    st.tm_year = curr_cal_year_ - 1900;
    st.tm_mon = curr_cal_month_ - 1;
    st.tm_mday = show_day;
    mktime(&st);
    int show_wday = st.tm_wday;

    const char* full_wdays[] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
    LunarDate lunar_show = VietnameseLunarCalendar::SolarToLunar(show_day, curr_cal_month_, curr_cal_year_, 7.0);
    const char* event_str = GetHolidayOrEvent(show_day, curr_cal_month_, lunar_show.day, lunar_show.month, show_wday);

    bool is_current_today = (show_day == today_day && curr_cal_month_ == today_month && curr_cal_year_ == today_year);

    if (cal_today_title_) {
        lv_label_set_text(cal_today_title_, is_current_today ? "HÔM NAY" : "ĐÃ CHỌN");
    }
    if (cal_today_day_) {
        char sday[8];
        snprintf(sday, sizeof(sday), "%02d", show_day);
        lv_label_set_text(cal_today_day_, sday);
    }
    if (cal_today_month_year_) {
        char mybuf[32];
        snprintf(mybuf, sizeof(mybuf), "Tháng %d, %d", curr_cal_month_, curr_cal_year_);
        lv_label_set_text(cal_today_month_year_, mybuf);
    }
    if (cal_today_wday_) {
        lv_label_set_text(cal_today_wday_, full_wdays[show_wday]);
    }
    if (cal_today_lunar_) {
        char lstr[32];
        snprintf(lstr, sizeof(lstr), "Âm: %d/%d", lunar_show.day, lunar_show.month);
        lv_label_set_text(cal_today_lunar_, lstr);
    }
    if (cal_today_event_) {
        lv_label_set_text(cal_today_event_, event_str);
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
            char lbuf[12];
            if (lunar.day == 1) {
                snprintf(lbuf, sizeof(lbuf), "1/%d", lunar.month);
            } else {
                snprintf(lbuf, sizeof(lbuf), "%d", lunar.day);
            }
            lv_label_set_text(c.lunar_label, lbuf);

            bool is_today = (day_num == today_day && curr_cal_month_ == today_month && curr_cal_year_ == today_year);
            bool is_selected = (day_num == show_day);
            bool is_sunday = ((cell % 7) == 0);

            if (is_today) {
                lv_obj_set_style_bg_color(c.container, lv_color_hex(0x2563EB), 0);
                lv_obj_set_style_bg_opa(c.container, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(c.container, is_selected ? 2 : 0, 0);
                lv_obj_set_style_border_color(c.container, lv_color_hex(0xFFD700), 0);
                lv_obj_set_style_text_color(c.solar_label, lv_color_white(), 0);
                lv_obj_set_style_text_color(c.lunar_label, lv_color_hex(0xFFE082), 0);
            } else if (is_selected) {
                lv_obj_set_style_bg_color(c.container, lv_color_hex(0x1E293B), 0);
                lv_obj_set_style_bg_opa(c.container, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(c.container, 2, 0);
                lv_obj_set_style_border_color(c.container, lv_color_hex(0x00E5FF), 0);
                lv_obj_set_style_text_color(c.solar_label, lv_color_hex(0x00E5FF), 0);
                lv_obj_set_style_text_color(c.lunar_label, lv_color_white(), 0);
            } else {
                lv_obj_set_style_bg_opa(c.container, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(c.container, 0, 0);
                if (is_sunday) {
                    lv_obj_set_style_text_color(c.solar_label, lv_color_hex(0xFF5252), 0);
                } else {
                    lv_obj_set_style_text_color(c.solar_label, lv_color_white(), 0);
                }
                if (lunar.day == 1) {
                    lv_obj_set_style_text_color(c.lunar_label, lv_color_hex(0xFFD54F), 0);
                } else {
                    lv_obj_set_style_text_color(c.lunar_label, lv_color_hex(0x90A4AE), 0);
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
    lv_obj_t* back_btn = lv_button_create(weather_detail_box_);
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
        // 240 x 320: Thẻ đồng hồ chính cao rộng rãi, không bị che
        if (clock_back_btn_) lv_obj_set_pos(clock_back_btn_, 6, 12);
        if (clock_card_) {
            lv_obj_set_size(clock_card_, 224, 114);
            lv_obj_set_pos(clock_card_, 8, 44);
            if (clock_time_label_) lv_obj_align(clock_time_label_, LV_ALIGN_TOP_MID, 0, 10);
            if (clock_date_label_) lv_obj_align(clock_date_label_, LV_ALIGN_TOP_MID, 0, 60);
            if (clock_sub_label_) lv_obj_align(clock_sub_label_, LV_ALIGN_TOP_MID, 0, 84);
        }
        // Các khu Bấm giờ & Hẹn giờ nhỏ gọn lại
        if (clock_sw_btn_) {
            lv_obj_set_size(clock_sw_btn_, 224, 46);
            lv_obj_set_pos(clock_sw_btn_, 8, 172);
            lv_obj_t* ico = lv_obj_get_child(clock_sw_btn_, 0);
            lv_obj_t* txt = lv_obj_get_child(clock_sw_btn_, 1);
            if (ico) lv_obj_align(ico, LV_ALIGN_LEFT_MID, 16, 0);
            if (txt) lv_obj_align(txt, LV_ALIGN_LEFT_MID, 56, 0);
        }
        if (clock_timer_btn_) {
            lv_obj_set_size(clock_timer_btn_, 224, 46);
            lv_obj_set_pos(clock_timer_btn_, 8, 228);
            lv_obj_t* ico = lv_obj_get_child(clock_timer_btn_, 0);
            lv_obj_t* txt = lv_obj_get_child(clock_timer_btn_, 1);
            if (ico) lv_obj_align(ico, LV_ALIGN_LEFT_MID, 16, 0);
            if (txt) lv_obj_align(txt, LV_ALIGN_LEFT_MID, 56, 0);
        }
    } else {
        // 320 x 240: Thẻ đồng hồ chính ở trên, 2 nút nhỏ gọn ở dưới không bị khuất chữ
        if (clock_back_btn_) lv_obj_set_pos(clock_back_btn_, 6, 6);
        if (clock_card_) {
            lv_obj_set_size(clock_card_, 304, 96);
            lv_obj_set_pos(clock_card_, 8, 34);
            if (clock_time_label_) lv_obj_align(clock_time_label_, LV_ALIGN_TOP_MID, 0, 6);
            if (clock_date_label_) lv_obj_align(clock_date_label_, LV_ALIGN_TOP_MID, 0, 48);
            if (clock_sub_label_) lv_obj_align(clock_sub_label_, LV_ALIGN_TOP_MID, 0, 70);
        }
        if (clock_sw_btn_) {
            lv_obj_set_size(clock_sw_btn_, 148, 54);
            lv_obj_set_pos(clock_sw_btn_, 8, 138);
            lv_obj_t* ico = lv_obj_get_child(clock_sw_btn_, 0);
            lv_obj_t* txt = lv_obj_get_child(clock_sw_btn_, 1);
            if (ico) lv_obj_align(ico, LV_ALIGN_LEFT_MID, 10, 0);
            if (txt) lv_obj_align(txt, LV_ALIGN_LEFT_MID, 46, 0);
        }
        if (clock_timer_btn_) {
            lv_obj_set_size(clock_timer_btn_, 148, 54);
            lv_obj_set_pos(clock_timer_btn_, 164, 138);
            lv_obj_t* ico = lv_obj_get_child(clock_timer_btn_, 0);
            lv_obj_t* txt = lv_obj_get_child(clock_timer_btn_, 1);
            if (ico) lv_obj_align(ico, LV_ALIGN_LEFT_MID, 10, 0);
            if (txt) lv_obj_align(txt, LV_ALIGN_LEFT_MID, 46, 0);
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
            // Landscape
            lv_obj_set_size(row, 150, 44);
            lv_obj_set_pos(row, 0, i * 48);
            lv_obj_align(tgl_btn, LV_ALIGN_RIGHT_MID, -34, 0);
            lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, -2, 0);
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
        // 320 x 240 (Landscape): Trái là 2 cột số xoay + thứ + thêm; Phải là danh sách báo thức lùi xuống không che chữ
        if (timer_back_btn_) lv_obj_set_pos(timer_back_btn_, 6, 6);
        if (title_lbl) lv_obj_set_pos(title_lbl, 46, 8);

        // 2 Roller to rõ, màu tương phản sắc nét
        if (alarm_hour_roller_) {
            lv_obj_set_size(alarm_hour_roller_, 58, 92);
            lv_obj_set_pos(alarm_hour_roller_, 10, 36);
        }
        if (colon_lbl) lv_obj_set_pos(colon_lbl, 71, 70);
        if (alarm_min_roller_) {
            lv_obj_set_size(alarm_min_roller_, 58, 92);
            lv_obj_set_pos(alarm_min_roller_, 80, 36);
        }

        // 7 nút thứ chia làm 2 hàng to dễ bấm:
        // Hàng 1 (4 nút): CN, T2, T3, T4
        for (int i = 0; i < 4; ++i) {
            if (alarm_day_btns_[i]) {
                lv_obj_set_size(alarm_day_btns_[i], 31, 26);
                lv_obj_set_pos(alarm_day_btns_[i], 8 + i * 35, 134);
            }
        }
        // Hàng 2 (3 nút): T5, T6, T7
        for (int i = 4; i < 7; ++i) {
            if (alarm_day_btns_[i]) {
                lv_obj_set_size(alarm_day_btns_[i], 42, 26);
                lv_obj_set_pos(alarm_day_btns_[i], 8 + (i - 4) * 47, 164);
            }
        }

        // Add button
        if (alarm_add_btn_) {
            lv_obj_set_size(alarm_add_btn_, 138, 30);
            lv_obj_set_pos(alarm_add_btn_, 8, 196);
        }

        // Alarm list lùi xuống y=38 để không che tiêu đề "Báo Thức"
        if (alarm_list_container_) {
            lv_obj_set_size(alarm_list_container_, 160, 196);
            lv_obj_set_pos(alarm_list_container_, 152, 38);
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
    if (rate_cards_[0].val1_lbl) lv_label_set_text(rate_cards_[0].val1_lbl, "Mua: 88.5 tr");
    if (rate_cards_[0].val2_lbl) lv_label_set_text(rate_cards_[0].val2_lbl, "Bán: 90.5 tr");
    if (rate_cards_[0].trend_lbl) lv_label_set_text(rate_cards_[0].trend_lbl, "+0.8% (Tăng)");

    // 2. USD (VND / USD)
    if (rate_cards_[1].val1_lbl) lv_label_set_text(rate_cards_[1].val1_lbl, "Mua: 25.160");
    if (rate_cards_[1].val2_lbl) lv_label_set_text(rate_cards_[1].val2_lbl, "Bán: 25.485");
    if (rate_cards_[1].trend_lbl) lv_label_set_text(rate_cards_[1].trend_lbl, "+10 (Ổn định)");

    // 3. Crypto (USD)
    if (rate_cards_[2].val1_lbl) lv_label_set_text(rate_cards_[2].val1_lbl, "BTC: $89,500");
    if (rate_cards_[2].val2_lbl) lv_label_set_text(rate_cards_[2].val2_lbl, "ETH: $3,180");
    if (rate_cards_[2].trend_lbl) lv_label_set_text(rate_cards_[2].trend_lbl, "+3.5% (Tăng)");

    // 4. Xăng dầu (VND / lít)
    if (rate_cards_[3].val1_lbl) lv_label_set_text(rate_cards_[3].val1_lbl, "RON95: 20.85k");
    if (rate_cards_[3].val2_lbl) lv_label_set_text(rate_cards_[3].val2_lbl, "E5: 19.75k");
    if (rate_cards_[3].trend_lbl) lv_label_set_text(rate_cards_[3].trend_lbl, "DO: 18.63k");
}

