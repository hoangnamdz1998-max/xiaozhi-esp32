#include "boards/common/esp32_music.h"
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <dirent.h>
#include "display/lvgl_display/jpg/jpeg_to_image.h"
#define LODEPNG_NO_COMPILE_CPP
#include "src/libs/lodepng/lodepng.h"

#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <wifi_station.h>
#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "adc_battery_monitor.h"
#include "settings.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include "led/single_led.h"
#include "system_reset.h"
#include "esp_lcd_ili9341.h"

#define TAG "FreenoveESP32S3Display"

class TouchDriver {
public:
    TouchDriver() : dev_(nullptr) {}

    bool Init(i2c_master_bus_handle_t bus, uint8_t addr) {
        i2c_device_config_t cfg = {
            .device_address = addr,
            .scl_speed_hz = 400000,
            .scl_wait_us = 0,
        };
        return i2c_master_bus_add_device(bus, &cfg, &dev_) == ESP_OK;
    }

    bool Read(bool &touched, uint16_t &x, uint16_t &y) {
        touched = false;
        x = y = 0;
        if (!dev_) return false;

        uint8_t reg = 0x02;
        uint8_t buf[5];
        if (i2c_master_transmit_receive(dev_, &reg, 1, buf, 5, 50) != ESP_OK) return false;

        uint8_t points = buf[0] & 0x0F;
        if (points == 0) return true;

        touched = true;
        x = ((buf[1] & 0x0F) << 8) | buf[2];
        y = ((buf[3] & 0x0F) << 8) | buf[4];
        return true;
    }

private:
    i2c_master_dev_handle_t dev_;
};

class FreenoveESP32S3Display : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay *display_;
    i2c_master_bus_handle_t codec_i2c_bus_;
    TouchDriver touch_;
    lv_indev_t* indev_ = nullptr;
    int16_t raw_touch_x_ = 0;
    int16_t raw_touch_y_ = 0;
    int16_t touch_lvgl_x_ = 0;
    int16_t touch_lvgl_y_ = 0;
    bool touch_pressed_ = false;

    static void touchpad_read(lv_indev_t* indev, lv_indev_data_t* data) {
        auto* self = static_cast<FreenoveESP32S3Display*>(lv_indev_get_user_data(indev));
        if (self == nullptr) return;

        data->point.x = self->touch_lvgl_x_;
        data->point.y = self->touch_lvgl_y_;
        data->state = self->touch_pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
    AdcBatteryMonitor* adc_battery_monitor_;
    bool is_portrait_ = false;
    bool sd_card_mounted_ = false;
    std::vector<std::string> wallpaper_files_;
    size_t current_wallpaper_idx_ = 0;
    uint8_t* current_jpeg_decoded_buf_ = nullptr;
    lv_image_dsc_t current_wallpaper_dsc_ = {};
    esp_timer_handle_t wallpaper_timer_ = nullptr;
    bool first_wallpaper_loaded_ = false;
    int wallpaper_rotation_tick_ = 0;

    void InitializeSdCard() {
        // Freenove ESP32-S3 Display 2.8" SDMMC Pinout:
        // CLK=GPIO 38, CMD=GPIO 40, D0=GPIO 39, D1=GPIO 41, D2=GPIO 48, D3=GPIO 47
        gpio_set_pull_mode(GPIO_NUM_38, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(GPIO_NUM_40, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(GPIO_NUM_39, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(GPIO_NUM_41, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(GPIO_NUM_48, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(GPIO_NUM_47, GPIO_PULLUP_ONLY);

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024,
            .disk_status_check_enable = false
        };

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.clk = GPIO_NUM_38;
        slot_config.cmd = GPIO_NUM_40;
        slot_config.d0 = GPIO_NUM_39;
        slot_config.d1 = GPIO_NUM_41;
        slot_config.d2 = GPIO_NUM_48;
        slot_config.d3 = GPIO_NUM_47;
        slot_config.d4 = GPIO_NUM_NC;
        slot_config.d5 = GPIO_NUM_NC;
        slot_config.d6 = GPIO_NUM_NC;
        slot_config.d7 = GPIO_NUM_NC;
        slot_config.cd = GPIO_NUM_NC;
        slot_config.wp = GPIO_NUM_NC;
        slot_config.width = 4;
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        sdmmc_card_t* card = nullptr;
        esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "4-bit SDMMC mount failed (%s), trying 1-bit mode...", esp_err_to_name(ret));
            slot_config.width = 1;
            slot_config.d1 = GPIO_NUM_NC;
            slot_config.d2 = GPIO_NUM_NC;
            slot_config.d3 = GPIO_NUM_NC;
            host.flags = SDMMC_HOST_FLAG_1BIT;
            ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
        }

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SD card mounted successfully at /sdcard (Name: %s, Capacity: %llu MB)",
                     card->cid.name, ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024));
            sd_card_mounted_ = true;
            ScanWallpapers();
        } else {
            ESP_LOGW(TAG, "No SD card mounted: %s", esp_err_to_name(ret));
            sd_card_mounted_ = false;
        }

        // Start periodic wallpaper rotation timer (checks every 1 second)
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                auto* self = static_cast<FreenoveESP32S3Display*>(arg);
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateIdle) {
                    if (!self->first_wallpaper_loaded_) {
                        self->first_wallpaper_loaded_ = true;
                        if (!self->wallpaper_files_.empty()) {
                            self->LoadWallpaper(0);
                        }
                    } else if (++self->wallpaper_rotation_tick_ >= 60) {
                        self->wallpaper_rotation_tick_ = 0;
                        self->NextWallpaper();
                    }
                }
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "wallpaper_rot"
        };
        esp_timer_create(&timer_args, &wallpaper_timer_);
        esp_timer_start_periodic(wallpaper_timer_, 1000000ULL);
    }

    void ScanWallpapers() {
        wallpaper_files_.clear();
        std::vector<std::string> search_dirs = {"/sdcard/wallpapers", "/sdcard/wallpaper", "/sdcard"};
        for (const auto& dir_path : search_dirs) {
            DIR* dir = opendir(dir_path.c_str());
            if (dir != nullptr) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
                        std::string name = entry->d_name;
                        ESP_LOGI(TAG, "File on SD: %s/%s", dir_path.c_str(), name.c_str());
                        std::string lower = name;
                        for (auto& c : lower) c = tolower(c);
                        if (lower.ends_with(".jpg") || lower.ends_with(".jpeg") || lower.ends_with(".png")) {
                            wallpaper_files_.push_back(dir_path + "/" + name);
                            ESP_LOGI(TAG, "Registered wallpaper: %s", (dir_path + "/" + name).c_str());
                        }
                    }
                }
                closedir(dir);
                if (!wallpaper_files_.empty()) {
                    break;
                }
            }
        }
        ESP_LOGI(TAG, "Total wallpapers found on SD card: %d", (int)wallpaper_files_.size());
    }

    bool LoadWallpaper(size_t index) {
        if (wallpaper_files_.empty() || index >= wallpaper_files_.size()) {
            return false;
        }
        const std::string& path = wallpaper_files_[index];
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) {
            ESP_LOGE(TAG, "Cannot open wallpaper file: %s", path.c_str());
            return false;
        }
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        long file_sz = (long)size;
        ESP_LOGI(TAG, "Opening wallpaper file: %s, size = %ld bytes", path.c_str(), file_sz);
        if (size == 0 || size > 10 * 1024 * 1024) { // Max 10MB
            fclose(f);
            ESP_LOGW(TAG, "Wallpaper file invalid size: %ld bytes (max 10MB)", file_sz);
            return false;
        }

        uint8_t* file_buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!file_buf) {
            fclose(f);
            ESP_LOGE(TAG, "Cannot allocate %ld bytes in PSRAM for wallpaper file", file_sz);
            return false;
        }
        fread(file_buf, 1, size, f);
        fclose(f);

        uint8_t* out_rgb = nullptr;
        size_t out_len = 0;
        size_t out_w = 0, out_h = 0, out_stride = 0;

        std::string lower_path = path;
        for (auto& c : lower_path) c = tolower(c);

        if (lower_path.ends_with(".png")) {
            unsigned char* png_rgba = nullptr;
            unsigned png_w = 0, png_h = 0;
            unsigned error = lodepng_decode32(&png_rgba, &png_w, &png_h, file_buf, size);
            heap_caps_free(file_buf);
            if (error != 0 || !png_rgba || png_w == 0 || png_h == 0) {
                ESP_LOGE(TAG, "lodepng error %u: %s", error, lodepng_error_text(error));
                if (png_rgba) free(png_rgba);
                return false;
            }
            ESP_LOGI(TAG, "Decoded PNG: %ux%u, scaling to 320x240 RGB565", png_w, png_h);
            out_w = 320;
            out_h = 240;
            out_stride = 320 * 2;
            out_len = 320 * 240 * 2;
            out_rgb = (uint8_t*)heap_caps_malloc(out_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!out_rgb) {
                ESP_LOGE(TAG, "Cannot allocate memory for PNG wallpaper");
                free(png_rgba);
                return false;
            }
            uint16_t* dst = (uint16_t*)out_rgb;
            for (size_t y = 0; y < 240; y++) {
                size_t src_y = y * png_h / 240;
                for (size_t x = 0; x < 320; x++) {
                    size_t src_x = x * png_w / 320;
                    size_t idx = (src_y * png_w + src_x) * 4;
                    uint8_t r = png_rgba[idx];
                    uint8_t g = png_rgba[idx + 1];
                    uint8_t b = png_rgba[idx + 2];
                    dst[y * 320 + x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                }
            }
            free(png_rgba);
        } else {
            esp_err_t ret = jpeg_to_image_fit(file_buf, size, 320, 240,
                                              &out_rgb, &out_len, &out_w, &out_h, &out_stride);
            heap_caps_free(file_buf);
            if (ret != ESP_OK || out_rgb == nullptr) {
                ESP_LOGE(TAG, "Failed to decode JPEG: %s", path.c_str());
                return false;
            }
        }

        if (current_jpeg_decoded_buf_ != nullptr) {
            free(current_jpeg_decoded_buf_);
        }
        current_jpeg_decoded_buf_ = out_rgb;

        current_wallpaper_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        current_wallpaper_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        current_wallpaper_dsc_.header.flags = 0;
        current_wallpaper_dsc_.header.w = (uint32_t)out_w;
        current_wallpaper_dsc_.header.h = (uint32_t)out_h;
        current_wallpaper_dsc_.header.stride = (uint32_t)out_stride;
        current_wallpaper_dsc_.header.reserved_2 = 0;
        current_wallpaper_dsc_.data_size = out_len;
        current_wallpaper_dsc_.data = current_jpeg_decoded_buf_;
        current_wallpaper_dsc_.reserved = nullptr;

        current_wallpaper_idx_ = index;
        if (display_ != nullptr) {
            display_->SetCustomWallpaper(&current_wallpaper_dsc_);
            ESP_LOGI(TAG, "Applied wallpaper %s (%dx%d)", path.c_str(), (int)out_w, (int)out_h);
        }
        return true;
    }

    virtual void NextWallpaper() override {
        if (wallpaper_files_.empty()) {
            return;
        }
        size_t next_idx = (current_wallpaper_idx_ + 1) % wallpaper_files_.size();
        LoadWallpaper(next_idx);
    }

    void LoadOrientation() {
        Settings settings("display", false);
        int orient = settings.GetInt("orientation", 0);
        is_portrait_ = (orient == 1);
        ESP_LOGI(TAG, "Screen orientation: %s (%d)", is_portrait_ ? "Portrait" : "Landscape", orient);
    }

    virtual void ToggleRotation(const std::string& mode = "toggle") override {
        bool target_portrait;
        if (mode == "portrait" || mode == "doc") {
            target_portrait = true;
        } else if (mode == "landscape" || mode == "ngang") {
            target_portrait = false;
        } else {
            target_portrait = !is_portrait_;
        }

        is_portrait_ = target_portrait;

        Settings settings("display", true);
        settings.SetInt("orientation", target_portrait ? 1 : 0);
        ESP_LOGI(TAG, "Screen orientation set to %d (%s) dynamically without reboot",
                 target_portrait ? 1 : 0, target_portrait ? "Portrait" : "Landscape");

        if (display_) {
            display_->SetOrientation(target_portrait);
            display_->ShowNotification(target_portrait ? "Xoay màn hình dọc" : "Xoay màn hình ngang", 1500);
        }
    }

    void InitializeBatteryMonitor() {
        adc_battery_monitor_ = new AdcBatteryMonitor(ADC_UNIT_1, ADC_CHANNEL_8, 200000, 200000, GPIO_NUM_NC);
    }

    static void TouchTask(void *arg) {
        auto *self = static_cast<FreenoveESP32S3Display*>(arg);
        auto &app = Application::GetInstance();

        uint32_t last_tap = 0;
        uint32_t down_start = 0;
        bool down = false;
        bool swiped = false;
        int16_t start_screen_x = 0, start_screen_y = 0;
        int16_t last_screen_x = 0, last_screen_y = 0;
        int tap_count = 0;

        while (true) {
            bool t;
            uint16_t raw_x, raw_y;
            self->touch_.Read(t, raw_x, raw_y);

            uint32_t now = esp_timer_get_time() / 1000;

            if (t) {
                // Compute logical screen coordinates based on orientation
                int16_t screen_x = 0;
                int16_t screen_y = 0;

                if (self->is_portrait_) {
                    // Portrait: width 240, height 320
                    // Short physical axis (raw_x: 0..239) is screen_x
                    // Long physical axis (raw_y: 0..319) is screen_y
                    screen_x = (int16_t)raw_x;
                    screen_y = (int16_t)raw_y;
                    if (screen_x < 0) screen_x = 0;
                    if (screen_x > 239) screen_x = 239;
                    if (screen_y < 0) screen_y = 0;
                    if (screen_y > 319) screen_y = 319;

                    // Reverse LVGL 9 rotate_point(LV_DISPLAY_ROTATION_90) so point lands on (screen_x, screen_y)
                    self->touch_lvgl_x_ = screen_y;
                    self->touch_lvgl_y_ = 319 - screen_x;
                } else {
                    // Landscape: width 320, height 240
                    // Long physical axis (raw_y: 0..319) is screen_x
                    // Short physical axis (raw_x: 0..239) is inverted for screen_y (0 at top, 239 at bottom)
                    screen_x = (int16_t)raw_y;
                    screen_y = 239 - (int16_t)raw_x;
                    if (screen_x < 0) screen_x = 0;
                    if (screen_x > 319) screen_x = 319;
                    if (screen_y < 0) screen_y = 0;
                    if (screen_y > 239) screen_y = 239;

                    self->touch_lvgl_x_ = screen_x;
                    self->touch_lvgl_y_ = screen_y;
                }

                if (!down) {
                    down = true;
                    down_start = now;
                    start_screen_x = screen_x;
                    start_screen_y = screen_y;
                    last_screen_x = screen_x;
                    last_screen_y = screen_y;
                    swiped = false;
                    self->raw_touch_x_ = raw_x;
                    self->raw_touch_y_ = raw_y;
                    self->touch_pressed_ = true;
                } else {
                    last_screen_x = screen_x;
                    last_screen_y = screen_y;
                    self->raw_touch_x_ = raw_x;
                    self->raw_touch_y_ = raw_y;

                    int delta_x = (int)screen_x - (int)start_screen_x;
                    int delta_y = (int)screen_y - (int)start_screen_y;

                    if (!swiped && (now - down_start < 800)) {
                        // Check vertical swipe first (vuốt dưới lên / vuốt trên xuống)
                        if (abs(delta_y) > 35 && abs(delta_y) > abs(delta_x) * 1.2) {
                            swiped = true;
                            self->touch_pressed_ = false; // release pointer so button under finger won't trigger
                            if (delta_y < -35) {
                                ESP_LOGI(TAG, "Vuot duoi len (Swipe UP, dy=%d)", delta_y);
                                if (self->display_) self->display_->OnSwipeUp();
                            } else if (delta_y > 35) {
                                ESP_LOGI(TAG, "Vuot tren xuong (Swipe DOWN, dy=%d)", delta_y);
                                if (self->display_) self->display_->OnSwipeDown();
                            }
                        } else if (abs(delta_x) > 35 && abs(delta_x) > abs(delta_y) * 1.2) {
                            swiped = true;
                            self->touch_pressed_ = false;
                            if (delta_x < -35) {
                                ESP_LOGI(TAG, "Vuot sang trai (Swipe LEFT, dx=%d)", delta_x);
                                if (self->display_) self->display_->OnSwipeLeft();
                            } else if (delta_x > 35) {
                                ESP_LOGI(TAG, "Vuot sang phai (Swipe RIGHT, dx=%d)", delta_x);
                                if (self->display_) self->display_->OnSwipeRight();
                            }
                        }
                    }
                }
            }

            if (!t && down) {
                down = false;
                self->touch_pressed_ = false;
                uint32_t press = now - down_start;
                int delta_x = (int)last_screen_x - (int)start_screen_x;
                int delta_y = (int)last_screen_y - (int)start_screen_y;

                bool is_gesture = swiped || (press < 600 && (abs(delta_y) > 35 || abs(delta_x) > 35));
                if (is_gesture) {
                    if (!swiped) {
                        if (abs(delta_y) > 35 && abs(delta_y) > abs(delta_x) * 1.2) {
                            if (delta_y < -35) {
                                ESP_LOGI(TAG, "Vuot duoi len on release (dy=%d)", delta_y);
                                if (self->display_) self->display_->OnSwipeUp();
                            } else if (delta_y > 35) {
                                ESP_LOGI(TAG, "Vuot tren xuong on release (dy=%d)", delta_y);
                                if (self->display_) self->display_->OnSwipeDown();
                            }
                        } else if (abs(delta_x) > 35 && abs(delta_x) > abs(delta_y) * 1.2) {
                            if (delta_x < -35) {
                                ESP_LOGI(TAG, "Vuot sang trai on release (dx=%d)", delta_x);
                                if (self->display_) self->display_->OnSwipeLeft();
                            } else if (delta_x > 35) {
                                ESP_LOGI(TAG, "Vuot sang phai on release (dx=%d)", delta_x);
                                if (self->display_) self->display_->OnSwipeRight();
                            }
                        }
                    }
                    tap_count = 0;
                    last_tap = 0;
                } else if (press > 3000) {
                    self->EnterWifiConfigMode();
                    tap_count = 0;
                    last_tap = 0;
                } else {
                    if (now - last_tap < 300) {
                        tap_count++;
                    } else {
                        tap_count = 1;
                    }
                    last_tap = now;

                    if (tap_count == 3) {
                        self->ToggleRotation("toggle");
                        tap_count = 0;
                        last_tap = 0;
                    } else if (tap_count == 4) {
                        self->NextWallpaper();
                        tap_count = 0;
                        last_tap = 0;
                    }
                }
            }

            if (tap_count > 0 && (now - last_tap >= 300)) {
                tap_count = 0;
            }

            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void InitializeTouch() {
        if (!touch_.Init(codec_i2c_bus_, 0x38)) return;

        DisplayLockGuard lock(display_);
        indev_ = lv_indev_create();
        lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev_, touchpad_read);
        lv_indev_set_user_data(indev_, this);
        lv_indev_set_disp(indev_, lv_display_get_default());

        xTaskCreatePinnedToCore(TouchTask, "touch_task", 4096, this, 5, nullptr, 0);
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = AUDIO_CODEC_I2C_NUM,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MIS0_PIN;
        buscfg.sclk_io_num = DISPLAY_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto &app = Application::GetInstance();
            auto state = app.GetDeviceState();
            if (state == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            if (state == kDeviceStateWifiConfiguring || state == kDeviceStateAudioTesting) {
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
        ESP_LOGI(TAG, "Install LCD driver ILI9341");
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);

        int width = DISPLAY_WIDTH;
        int height = DISPLAY_HEIGHT;
        bool swap_xy = DISPLAY_SWAP_XY;
        bool mirror_x = DISPLAY_MIRROR_X;
        bool mirror_y = DISPLAY_MIRROR_Y;

        esp_lcd_panel_swap_xy(panel, swap_xy);
        esp_lcd_panel_mirror(panel, mirror_x, mirror_y);
        display_ = new SpiLcdDisplay(panel_io, panel,
            width, height,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            mirror_x, mirror_y, swap_xy);

        if (is_portrait_) {
            display_->SetOrientation(true);
        }
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.screen.set_rotation",
            "Xoay hướng hiển thị của màn hình giữa chế độ ngang (landscape 320x240) và dọc (portrait 240x320). "
            "Dùng công cụ này khi người dùng nói 'xoay màn hình', 'màn hình dọc', 'màn hình ngang', "
            "'xoay ngang', 'xoay dọc', hoặc 'rotate screen'.",
            PropertyList({Property("orientation", kPropertyTypeString, std::string("toggle"))}),
            [this](const PropertyList& properties) -> ToolResult {
                std::string mode = properties["orientation"].value<std::string>();
                ToggleRotation(mode);
                return true;
            });

        mcp_server.AddTool("self.wallpaper.next",
            "Chuyển sang hình nền tiếp theo trong thẻ nhớ khi người dùng yêu cầu đổi hình nền, chuyển ảnh khác, ảnh tiếp theo.",
            PropertyList(),
            [this](const PropertyList& properties) -> ToolResult {
                if (!sd_card_mounted_) {
                    return "Không nhận diện được thẻ nhớ microSD. Hãy kiểm tra thẻ đã gắn chặt và đúng định dạng FAT32.";
                }
                if (wallpaper_files_.empty()) {
                    return "Thẻ nhớ đã kết nối nhưng không tìm thấy file ảnh .jpg nào.";
                }
                NextWallpaper();
                return "Đã chuyển sang hình nền tiếp theo";
            });
    }

public:
    FreenoveESP32S3Display(): boot_button_(BOOT_BUTTON_GPIO)
    {
        LoadOrientation();
        InitializeI2c();
        InitializeBatteryMonitor();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeTouch();
        InitializeButtons();
        InitializeSdCard();
        InitializeTools();
        GetBacklight()->SetBrightness(100);
    }

    virtual Led *GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual Music* GetMusic() override {
        static Esp32Music music;
        return &music;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, AUDIO_CODEC_I2C_NUM,
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR, true, true);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override { return display_; }

    virtual Backlight *GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        charging = adc_battery_monitor_->IsCharging();
        discharging = adc_battery_monitor_->IsDischarging();
        level = adc_battery_monitor_->GetBatteryLevel();
        return true;
    }
};

DECLARE_BOARD(FreenoveESP32S3Display);
