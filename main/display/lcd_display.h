#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "gif/lvgl_gif.h"
#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <atomic>
#include <memory>

#define PREVIEW_IMAGE_DURATION_MS 5000

class LcdDisplay : public LvglDisplay {
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_draw_buf_t draw_buf_;
    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* bottom_bar_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* wallpaper_image_ = nullptr;
    bool is_wallpaper_active_ = false;
    lv_obj_t* emoji_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;
    std::unique_ptr<LvglGif> gif_controller_ = nullptr;
    lv_obj_t* emoji_box_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    esp_timer_handle_t preview_timer_ = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    bool hide_subtitle_ = false;  // Control whether to hide chat messages/subtitles
    lv_obj_t* calendar_label_ = nullptr;
    int last_displayed_calendar_day_ = -1;
    lv_obj_t* idle_clock_label_ = nullptr;
    int last_displayed_clock_sec_ = -1;
    bool has_custom_sd_wallpaper_ = false;
    bool wifi_connected_ = false;
    lv_obj_t* weather_box_ = nullptr;
    lv_obj_t* weather_icon_ = nullptr;
    lv_obj_t* weather_label_ = nullptr;
    bool weather_ready_ = false;

    // Full standby screen widgets matching reference layout
    lv_obj_t* standby_box_ = nullptr;
    lv_obj_t* standby_clock_ = nullptr;
    lv_obj_t* standby_sec_ = nullptr;
    lv_obj_t* standby_weekday_ = nullptr;
    lv_obj_t* standby_date_ = nullptr;
    lv_obj_t* standby_lunar_ = nullptr;
    lv_obj_t* standby_year_ = nullptr;
    lv_obj_t* standby_weather_icon_ = nullptr;
    lv_obj_t* standby_temp_ = nullptr;
    lv_obj_t* standby_humidity_ = nullptr;
    lv_obj_t* standby_wind_ = nullptr;
    lv_obj_t* standby_city_ = nullptr;
    lv_obj_t* standby_condition_ = nullptr;

    struct ForecastCardWidget {
        lv_obj_t* card = nullptr;
        lv_obj_t* day = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* temp = nullptr;
    };
    ForecastCardWidget forecast_cards_[4];

    void LayoutStandbyWidgets(bool is_portrait);

public:
    enum ScreenMode {
        kScreenStandby,
        kScreenMenu,
        kScreenQuickSettings,
        kScreenCalendar,
        kScreenWeatherDetail
    };

    virtual void OnSwipeLeft() override;
    virtual void OnSwipeRight() override;
    virtual void OnSwipeUp() override;
    virtual void OnSwipeDown() override;
    virtual void ShowMenu() override;
    virtual void ShowStandby() override;
    virtual void ShowQuickSettings() override;
    virtual void ShowCalendar() override;
    virtual void ShowWeatherDetail() override;
    void SetMenuPage(int page);
    void SetStandbyTextColor(uint32_t color_hex);
    ScreenMode GetScreenMode() const { return screen_mode_; }

protected:
    ScreenMode screen_mode_ = kScreenStandby;

    // App Menu (22 items across 3 pages)
    lv_obj_t* menu_box_ = nullptr;
    lv_obj_t* menu_title_ = nullptr;
    lv_obj_t* menu_items_[22] = {nullptr};
    lv_obj_t* menu_dots_[3] = {nullptr};
    int menu_page_ = 0;
    uint32_t last_swipe_time_ = 0;

    // Quick Settings Screen
    lv_obj_t* quick_settings_box_ = nullptr;
    lv_obj_t* qs_brightness_slider_ = nullptr;
    lv_obj_t* qs_brightness_label_ = nullptr;
    lv_obj_t* qs_volume_slider_ = nullptr;
    lv_obj_t* qs_volume_label_ = nullptr;
    lv_obj_t* qs_color_card_ = nullptr;
    lv_obj_t* qs_sliders_card_ = nullptr;
    lv_obj_t* qs_actions_card_ = nullptr;
    lv_obj_t* qs_ai_btn_ = nullptr;
    lv_obj_t* qs_rotate_btn_ = nullptr;
    lv_obj_t* qs_restart_btn_ = nullptr;
    lv_obj_t* qs_close_btn_ = nullptr;
    lv_obj_t* qs_title_ = nullptr;

    uint32_t standby_text_color_ = 0xFFFFFF;

    void CreateQuickSettingsUI();
    void LayoutQuickSettingsWidgets(bool is_portrait);

    // Calendar Screen
    lv_obj_t* calendar_box_ = nullptr;
    lv_obj_t* calendar_title_label_ = nullptr;
    lv_obj_t* calendar_back_btn_ = nullptr;
    lv_obj_t* calendar_prev_btn_ = nullptr;
    lv_obj_t* calendar_next_btn_ = nullptr;
    int curr_cal_year_ = 2026;
    int curr_cal_month_ = 11;
    struct CalendarCellWidget {
        lv_obj_t* container = nullptr;
        lv_obj_t* solar_label = nullptr;
        lv_obj_t* lunar_label = nullptr;
    };
    CalendarCellWidget calendar_cells_[42];

    // Weather Detail Screen
    lv_obj_t* weather_detail_box_ = nullptr;
    lv_obj_t* weather_detail_city_ = nullptr;
    lv_obj_t* weather_detail_temp_ = nullptr;
    lv_obj_t* weather_detail_condition_ = nullptr;
    lv_obj_t* weather_detail_stats_ = nullptr;
    lv_obj_t* weather_detail_icon_ = nullptr;
    struct WeatherDetailDailyWidget {
        lv_obj_t* row = nullptr;
        lv_obj_t* day_label = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* temp_label = nullptr;
    };
    WeatherDetailDailyWidget weather_detail_daily_[4];

    void CreateMenuUI();
    void CreateCalendarUI();
    void UpdateCalendarUI();
    void CreateWeatherDetailUI();
    void UpdateWeatherDetailUI();
    void LayoutMenuWidgets(bool is_portrait);
    void LayoutCalendarWidgets(bool is_portrait);
    void LayoutWeatherDetailWidgets(bool is_portrait);

    void InitializeLcdThemes();
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

protected:
    // Add protected constructor
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
               int height);

public:
    ~LcdDisplay();
    virtual bool SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void RemoveChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetupUI() override;
    virtual void OnStateChanged(DeviceState state) override;
    // Add theme switching function
    virtual void SetTheme(Theme* theme) override;

    // Set whether to hide chat messages/subtitles
    virtual void SetHideSubtitle(bool hide) override;
    virtual void SetOrientation(bool is_portrait) override;
    virtual void SetCustomWallpaper(const lv_image_dsc_t* img_dsc) override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    virtual void UpdateWeather(float temp, int humidity, const char* condition, int icon_type) override;
    virtual void UpdateWeatherFull(const WeatherInfo& info) override;
};

// SPI LCD display
class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                  int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                  bool swap_xy);
};

// RGB LCD display
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                  int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                  bool swap_xy);
};

// MIPI LCD display
class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                   bool swap_xy);
};

#endif  // LCD_DISPLAY_H
