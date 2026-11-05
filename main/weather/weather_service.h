#pragma once

#include <string>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

struct DayForecast {
    std::string day_str; // e.g. "T4 04"
    int weather_code = 0;
    int icon_type = 1;   // 0=sun, 1=sun_cloud, 2=cloud, 3=rain, 4=thunder, 5=moon, 6=moon_cloud
    float temp_max = 0.0f;
};

struct WeatherInfo {
    float temp = 27.5f;
    float apparent_temp = 32.0f;
    int humidity = 80;
    float wind_speed = 5.0f;
    int weather_code = 0;
    int is_day = 1;
    int icon_type = 0;
    std::string condition = "Trời nắng";
    std::string city = "Đang định vị...";
    std::vector<DayForecast> daily;
};

class WeatherService {
public:
    static WeatherService& GetInstance();
    void Start();
    void FetchOnceAsync();
    void RefreshNow();
    void SetLocation(const std::string& city, float lat, float lon);
    const WeatherInfo& GetWeatherInfo() const { return info_; }

private:
    WeatherService() = default;
    ~WeatherService() = default;
    WeatherService(const WeatherService&) = delete;
    WeatherService& operator=(const WeatherService&) = delete;

    void TaskLoop();
    bool FetchLocation(float& lat, float& lon, std::string& city);
    bool FetchWeather(float lat, float lon);

    TaskHandle_t task_handle_ = nullptr;
    bool has_location_ = false;
    bool need_refetch_location_ = false;
    float lat_ = 10.8231f;
    float lon_ = 106.6297f;
    std::string city_ = "Đang định vị...";
    WeatherInfo info_;
};
