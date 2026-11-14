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
    lv_obj_t* standby_status_bar_ = nullptr;
    lv_obj_t* standby_right_icons_ = nullptr;
    lv_obj_t* standby_wifi_label_ = nullptr;
    lv_obj_t* standby_battery_pct_label_ = nullptr;
    lv_obj_t* standby_battery_label_ = nullptr;

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
        kScreenStandby2,
        kScreenStandby3,
        kScreenMenu,
        kScreenQuickSettings,
        kScreenCalendar,
        kScreenWeatherDetail,
        kScreenClock,
        kScreenStopwatch,
        kScreenTimer,
        kScreenExchangeRates,
        kScreenCalculator,
        kScreenFlashlight,
        kScreenFmRadio,
        kScreenWallpaperSettings,
        kScreenMusic
    };

    ScreenMode previous_screen_mode_ = kScreenStandby;
    void ExitQuickSettings();

    virtual void OnSwipeLeft() override;
    virtual void OnSwipeRight() override;
    virtual void OnSwipeUp() override;
    virtual void OnSwipeDown() override;
    virtual void ShowMenu() override;
    virtual void ShowStandby() override;
    virtual void ShowStandby2();
    virtual void ShowStandby3();
    virtual void ShowQuickSettings() override;
    virtual void ShowCalendar() override;
    virtual void ShowWeatherDetail() override;
    virtual void ShowClock() override;
    virtual void ShowStopwatch() override;
    virtual void ShowTimer() override;
    virtual void ShowExchangeRates() override;
    virtual void ShowWallpaperSettings() override;
    virtual void ReloadWallpaper() override;
    virtual void ShowMusicScreen(const std::string& song_name = "", const std::string& artist_name = "") override;
    virtual void UpdateMusicPlaybackState(bool is_playing, const std::string& song_name = "", const std::string& artist_name = "") override;
    void CreateMusicScreenUI();
    void LayoutMusicWidgets(bool is_portrait);
    void UpdateMusicUI();
    virtual void ShowCalculator();
    virtual void ShowFlashlight();
    virtual void ShowFmRadio();
    void SetMenuPage(int page, bool animate = false, bool swipe_left = true);
    void SetStandbyTextColor(uint32_t color_hex);
    ScreenMode GetScreenMode() const { return screen_mode_; }
    bool IsQuickSettingsAtBottom() const { return qs_at_bottom_.load(); }

protected:
    ScreenMode screen_mode_ = kScreenStandby;

    // App Menu (12 items across 2 pages)
    lv_obj_t* menu_box_ = nullptr;
    lv_obj_t* menu_page_box_[3] = {nullptr};
    lv_obj_t* menu_title_ = nullptr;
    lv_obj_t* menu_items_[22] = {nullptr};
    lv_obj_t* menu_dots_[3] = {nullptr};
    lv_obj_t* menu_status_bar_ = nullptr;
    lv_obj_t* menu_wifi_label_ = nullptr;
    lv_obj_t* menu_battery_pct_label_ = nullptr;
    lv_obj_t* menu_battery_label_ = nullptr;
    lv_obj_t* menu_time_label_ = nullptr;
    int menu_page_ = 0;
    uint32_t last_swipe_time_ = 0;

    // Quick Settings Screen
    lv_obj_t* quick_settings_box_ = nullptr;
    lv_obj_t* qs_content_box_ = nullptr;
    lv_obj_t* qs_brightness_slider_ = nullptr;
    lv_obj_t* qs_brightness_label_ = nullptr;
    lv_obj_t* qs_volume_slider_ = nullptr;
    lv_obj_t* qs_volume_label_ = nullptr;
    lv_obj_t* qs_color_card_ = nullptr;
    lv_obj_t* qs_color_dots_[10] = {nullptr};
    lv_obj_t* qs_sliders_card_ = nullptr;
    lv_obj_t* qs_actions_card_ = nullptr;
    lv_obj_t* qs_ai_btn_ = nullptr;
    lv_obj_t* qs_rotate_btn_ = nullptr;
    lv_obj_t* qs_wifi_btn_ = nullptr;
    lv_obj_t* qs_theme_btn_ = nullptr;
    lv_obj_t* qs_wallpaper_btn_ = nullptr;
    lv_obj_t* qs_qr_modal_ = nullptr;
    lv_obj_t* qs_qr_img_ = nullptr;
    lv_obj_t* qs_qr_ip_lbl_ = nullptr;
    void CreateQrModal();
    void ShowQrModal();
    void HideQrModal();

    // Wallpaper Settings Screen (Live Preview + Dual Horizontal Scroll)
    lv_obj_t* wallpaper_box_ = nullptr;
    lv_obj_t* wp_preview_card_ = nullptr;
    lv_obj_t* wp_preview_img_ = nullptr;
    uint8_t* wp_preview_buf_ = nullptr;
    lv_image_dsc_t wp_preview_dsc_ = {};
    lv_obj_t* wp_preview_weather_ = nullptr;
    lv_obj_t* wp_preview_clock_ = nullptr;
    lv_obj_t* wp_preview_clock_lbl_ = nullptr;
    lv_obj_t* wp_img_scroll_ = nullptr;
    lv_obj_t* wp_layout_scroll_ = nullptr;
    lv_obj_t* wp_user_scroll_ = nullptr;
    lv_obj_t* wp_sec1_lbl_ = nullptr;
    lv_obj_t* wp_sec2_lbl_ = nullptr;
    lv_obj_t* wp_save_btn_ = nullptr;
    int wp_preview_idx_ = 0;
    int wp_preview_layout_ = 0;
    bool wp_opened_from_menu_ = false;
    void CreateWallpaperSettingsUI();
    void LayoutWallpaperSettingsWidgets(bool is_portrait);
    void UpdateWallpaperSettingsUI();
    void UpdateWallpaperPreview();
    lv_obj_t* qs_restart_btn_ = nullptr;
    lv_obj_t* qs_close_btn_ = nullptr;
    lv_obj_t* qs_title_ = nullptr;
    lv_obj_t* qs_info_card_ = nullptr;
    lv_obj_t* qs_info_wifi_ = nullptr;
    lv_obj_t* qs_info_ip_ = nullptr;
    lv_obj_t* qs_info_temp_ = nullptr;
    lv_obj_t* qs_info_sd_ = nullptr;
    std::atomic<bool> qs_at_bottom_{false};
    void UpdateQuickSettingsInfo();

    bool is_light_theme_ = false;
    void ApplyTheme();
    void UpdateColorDotsPalette();

    uint32_t standby_text_color_ = 0xFFFFFF;

    void CreateQuickSettingsUI();
    void LayoutQuickSettingsWidgets(bool is_portrait);
    void UpdateColorDotsSelection();

    // Calendar Screen
    lv_obj_t* calendar_box_ = nullptr;
    lv_obj_t* calendar_title_label_ = nullptr;
    lv_obj_t* calendar_back_btn_ = nullptr;
    lv_obj_t* calendar_prev_btn_ = nullptr;
    lv_obj_t* calendar_next_btn_ = nullptr;
    lv_obj_t* cal_today_card_ = nullptr;
    lv_obj_t* cal_today_title_ = nullptr;
    lv_obj_t* cal_today_day_ = nullptr;
    lv_obj_t* cal_today_month_year_ = nullptr;
    lv_obj_t* cal_today_wday_ = nullptr;
    lv_obj_t* cal_today_lunar_ = nullptr;
    lv_obj_t* cal_today_event_ = nullptr;
    int curr_cal_year_ = 2026;
    int curr_cal_month_ = 11;
    int selected_cal_day_ = -1;
    struct CalendarCellWidget {
        lv_obj_t* container = nullptr;
        lv_obj_t* solar_label = nullptr;
        lv_obj_t* lunar_label = nullptr;
        int day_num = 0;
    };
    CalendarCellWidget calendar_cells_[42];

    // Weather Detail Screen
    lv_obj_t* weather_detail_box_ = nullptr;
    lv_obj_t* weather_detail_back_btn_ = nullptr;
    lv_obj_t* weather_detail_city_ = nullptr;
    lv_obj_t* weather_detail_temp_ = nullptr;
    lv_obj_t* weather_detail_condition_ = nullptr;
    lv_obj_t* weather_detail_stats_ = nullptr;
    lv_obj_t* weather_detail_humidity_ = nullptr;
    lv_obj_t* weather_detail_wind_ = nullptr;
    lv_obj_t* weather_detail_icon_ = nullptr;
    struct WeatherDetailDailyWidget {
        lv_obj_t* row = nullptr;
        lv_obj_t* day_label = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* temp_label = nullptr;
    };
    WeatherDetailDailyWidget weather_detail_daily_[4];

    // Clock Screen (Đồng hồ & lựa chọn Bấm giờ / Hẹn giờ)
    lv_obj_t* clock_box_ = nullptr;
    lv_obj_t* clock_back_btn_ = nullptr;
    lv_obj_t* clock_card_ = nullptr;
    lv_obj_t* clock_time_label_ = nullptr;
    lv_obj_t* clock_date_label_ = nullptr;
    lv_obj_t* clock_sub_label_ = nullptr;
    lv_obj_t* clock_sw_btn_ = nullptr;
    lv_obj_t* clock_timer_btn_ = nullptr;

    // Stopwatch & Countdown Screen (Bấm giờ chạy mili-giây & Đếm ngược tùy chỉnh phút + chuông)
    lv_obj_t* stopwatch_box_ = nullptr;
    lv_obj_t* sw_back_btn_ = nullptr;
    lv_obj_t* sw_tab_sw_btn_ = nullptr;
    lv_obj_t* sw_tab_cd_btn_ = nullptr;
    lv_obj_t* sw_container_ = nullptr;
    lv_obj_t* cd_container_ = nullptr;
    bool sw_mode_countdown_ = false;

    // Stopwatch widgets
    lv_obj_t* sw_time_label_ = nullptr;
    lv_obj_t* sw_status_label_ = nullptr;
    lv_obj_t* sw_start_btn_ = nullptr;
    lv_obj_t* sw_start_lbl_ = nullptr;
    lv_obj_t* sw_reset_btn_ = nullptr;
    lv_obj_t* sw_reset_lbl_ = nullptr;
    bool sw_running_ = false;
    int64_t sw_elapsed_ms_ = 0;
    int64_t sw_last_tick_ms_ = 0;

    // Countdown widgets (Đếm ngược)
    lv_obj_t* cd_time_label_ = nullptr;
    lv_obj_t* cd_status_label_ = nullptr;
    lv_obj_t* cd_start_btn_ = nullptr;
    lv_obj_t* cd_start_lbl_ = nullptr;
    lv_obj_t* cd_reset_btn_ = nullptr;
    lv_obj_t* cd_minus_btn_ = nullptr;
    lv_obj_t* cd_plus_btn_ = nullptr;
    int cd_minutes_ = 5;
    int64_t cd_remain_ms_ = 5 * 60 * 1000;
    int64_t cd_last_tick_ms_ = 0;
    bool cd_running_ = false;
    lv_timer_t* fast_tick_timer_ = nullptr;

    // Alarm Clock Screen (Báo thức con lăn xoay giờ phút + danh sách báo thức bật/tắt/xóa)
    struct AlarmRecord {
        int id = 0;
        int hour = 7;
        int min = 0;
        uint8_t days_mask = 0x7F; // Bit 0=CN, 1=T2..6=T7
        bool enabled = true;
        bool triggered_today = false;
    };
    std::vector<AlarmRecord> alarms_;
    int next_alarm_id_ = 1;

    lv_obj_t* timer_box_ = nullptr;
    lv_obj_t* timer_back_btn_ = nullptr;
    lv_obj_t* alarm_hour_roller_ = nullptr;
    lv_obj_t* alarm_min_roller_ = nullptr;
    lv_obj_t* alarm_add_btn_ = nullptr;
    lv_obj_t* alarm_day_btns_[7] = {nullptr};
    uint8_t alarm_new_days_mask_ = 0x7F;
    lv_obj_t* alarm_list_container_ = nullptr;
    void RenderAlarmList();

    // Exchange Rates Screen (Tỉ giá thị trường: Vàng SJC, USD, Crypto BTC/ETH, Xăng dầu)
    lv_obj_t* rates_box_ = nullptr;
    lv_obj_t* rates_back_btn_ = nullptr;
    lv_obj_t* rates_refresh_btn_ = nullptr;
    lv_obj_t* rates_time_label_ = nullptr;
    struct RateCardWidget {
        lv_obj_t* card = nullptr;
        lv_obj_t* icon_lbl = nullptr;
        lv_obj_t* title_lbl = nullptr;
        lv_obj_t* val1_lbl = nullptr;
        lv_obj_t* val2_lbl = nullptr;
        lv_obj_t* trend_lbl = nullptr;
    };
    RateCardWidget rate_cards_[4];

    // Calculator Screen (Máy tính cá nhân)
    lv_obj_t* calc_box_ = nullptr;
    lv_obj_t* calc_back_btn_ = nullptr;
    lv_obj_t* calc_expr_label_ = nullptr;
    lv_obj_t* calc_result_label_ = nullptr;
    std::string calc_curr_input_ = "0";
    double calc_first_val_ = 0.0;
    char calc_pending_op_ = 0;
    bool calc_waiting_second_ = false;
    void CreateCalculatorUI();
    void LayoutCalculatorWidgets(bool is_portrait);
    void OnCalculatorButton(const char* key);

    // Flashlight Screen (Đèn pin)
    lv_obj_t* flashlight_box_ = nullptr;
    int saved_flashlight_brightness_ = 100;
    void CreateFlashlightUI();

    // FM Radio Screen (Đài phát thanh trực tuyến 4 kênh chọn lọc)
    struct FmChannel {
        const char* name;
        const char* freq;
        const char* url;
    };
    static const FmChannel kFmChannels[4];
    lv_obj_t* fm_box_ = nullptr;
    lv_obj_t* fm_back_btn_ = nullptr;
    lv_obj_t* fm_title_lbl_ = nullptr;
    lv_obj_t* fm_bottom_card_ = nullptr;
    lv_obj_t* fm_status_lbl_ = nullptr;
    lv_obj_t* fm_channel_lbl_ = nullptr;
    lv_obj_t* fm_play_btn_ = nullptr;
    lv_obj_t* fm_play_lbl_ = nullptr;
    lv_obj_t* fm_channel_btns_[4] = {nullptr};
    int fm_current_channel_ = -1;
    bool fm_is_playing_ = false;

    // Music Player Screen
    struct SongItem {
        std::string title;
        std::string artist;
    };
    static const SongItem kZingTopSongsDefault[10];
    std::vector<SongItem> music_chart_songs_;
    lv_obj_t* music_chart_item_titles_[10] = {nullptr};
    lv_obj_t* music_chart_item_artists_[10] = {nullptr};

    lv_obj_t* music_box_ = nullptr;
    lv_obj_t* music_back_btn_ = nullptr;
    lv_obj_t* music_title_badge_ = nullptr;
    lv_obj_t* music_vinyl_ = nullptr;
    lv_obj_t* music_vinyl_label_ = nullptr;
    lv_obj_t* music_vinyl_note_ = nullptr;
    lv_obj_t* music_song_lbl_ = nullptr;
    lv_obj_t* music_artist_lbl_ = nullptr;
    lv_obj_t* music_play_btn_ = nullptr;
    lv_obj_t* music_play_lbl_ = nullptr;
    lv_obj_t* music_stop_btn_ = nullptr;
    lv_obj_t* music_stop_lbl_ = nullptr;
    lv_obj_t* music_visualizer_box_ = nullptr;
    lv_obj_t* music_chart_list_ = nullptr;
    lv_obj_t* music_chart_title_ = nullptr;
    static constexpr int kNumVisualizerBars = 16;
    lv_obj_t* music_vis_bars_[16] = {nullptr};
    lv_timer_t* music_vis_timer_ = nullptr;
    std::string music_current_song_ = "Chưa phát nhạc";
    std::string music_current_artist_ = "Zing MP3 Online";
    bool music_is_playing_ = false;
    void PlayChartSong(int index);
    void FetchRealtimeChart();
    void UpdateChartSongsUI();
    void CreateFmRadioUI();
    void LayoutFmRadioWidgets(bool is_portrait);
    void UpdateFmRadioUI();
    void StartBootAnimation();
    void CycleStandby2Wallpaper();
    std::atomic<bool> boot_anim_running_{false};

    // Standby Screen 2 (Màn hình chờ 2 - Ảnh nền tùy biến, Flip Clock & Weather)
    lv_obj_t* standby2_box_ = nullptr;
    lv_obj_t* standby2_bg_image_ = nullptr;
    uint8_t* standby2_bg_buf_ = nullptr;
    int standby2_last_orient_ = -1;
    int standby2_last_wp_idx_ = -1;
    lv_image_dsc_t standby2_bg_dsc_ = {};

    lv_obj_t* standby2_status_box_ = nullptr;
    lv_obj_t* standby2_wifi_icon_ = nullptr;
    lv_obj_t* standby2_battery_icon_ = nullptr;
    lv_obj_t* standby2_battery_pct_ = nullptr;

    lv_obj_t* standby2_weather_box_ = nullptr;
    lv_obj_t* standby2_city_lbl_ = nullptr;
    lv_obj_t* standby2_weather_icon_ = nullptr;
    lv_obj_t* standby2_weather_temp_ = nullptr;

    lv_obj_t* standby2_clock_box_ = nullptr;
    lv_obj_t* standby2_time_lbl_ = nullptr;
    lv_obj_t* flip_cards_[4] = {nullptr};
    lv_obj_t* flip_labels_[4] = {nullptr};
    lv_obj_t* flip_slits_[4] = {nullptr};
    lv_obj_t* flip_flaps_[4] = {nullptr};
    lv_obj_t* flip_flap_labels_[4] = {nullptr};
    char flip_chars_[4] = {' ', ' ', ' ', ' '};
    lv_obj_t* standby2_date_lbl_ = nullptr;

    void CreateStandby2UI();
    void LayoutStandby2Widgets(bool is_portrait);
    void UpdateStandby2Clock(const struct tm* tm_now, bool animate);
    void AnimateFlipCard(int idx, char new_char);
    void SyncStandby2Weather();

    lv_obj_t* standby3_box_ = nullptr;
    lv_obj_t* standby3_bg_image_ = nullptr;
    lv_obj_t* standby3_status_box_ = nullptr;
    lv_obj_t* standby3_wifi_icon_ = nullptr;
    lv_obj_t* standby3_battery_icon_ = nullptr;
    lv_obj_t* standby3_battery_pct_ = nullptr;
    lv_obj_t* standby3_time_lbl_ = nullptr;
    lv_obj_t* standby3_date_lbl_ = nullptr;

    void CreateStandby3UI();
    void LayoutStandby3Widgets(bool is_portrait);
    void UpdateStandby3Clock(const struct tm* tm_now);

    void CreateMenuUI();
    void UpdateMenuStatusBar();
    void UpdateStandbyStatusBar();
    void CreateCalendarUI();
    void UpdateCalendarUI();
    void CreateWeatherDetailUI();
    void UpdateWeatherDetailUI();
    void CreateClockUI();
    void UpdateClockUI();
    void CreateStopwatchUI();
    void UpdateStopwatchUI();
    void CreateTimerUI();
    void UpdateTimerUI();
    void CreateExchangeRatesUI();
    void UpdateExchangeRatesUI();

    void LayoutMenuWidgets(bool is_portrait);
    void LayoutCalendarWidgets(bool is_portrait);
    void LayoutWeatherDetailWidgets(bool is_portrait);
    void LayoutClockWidgets(bool is_portrait);
    void LayoutStopwatchWidgets(bool is_portrait);
    void LayoutTimerWidgets(bool is_portrait);
    void LayoutExchangeRatesWidgets(bool is_portrait);

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
    virtual void WaitForBootAnimation() override;
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
