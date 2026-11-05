#include "weather_service.h"
#include "board.h"
#include "display.h"

#include <esp_log.h>
#include <cJSON.h>
#include <cstring>
#include <cstdio>
#include <ctime>

static const char* TAG = "WeatherService";

static int CodeToIconType(int code, int is_day) {
    if (is_day == 0) {
        // Night icons: 5=moon, 6=moon_cloud, 2=cloud, 3=rain, 4=thunder
        if (code == 0) return 5; // crescent moon
        if (code == 1 || code == 2) return 6; // moon_cloud
        if (code == 3) return 2; // cloud
        if (code == 45 || code == 48) return 2; // fog / overcast
        if (code >= 51 && code <= 82) return 3; // rain
        if (code >= 85 && code <= 90) return 3; // rain
        if (code >= 95) return 4; // thunder
        return 6;
    }
    // Day icons: 0=sun, 1=sun_cloud, 2=cloud, 3=rain, 4=thunder
    if (code == 0) return 0; // sun
    if (code == 1 || code == 2 || code == 3) return 1; // sun_cloud
    if (code == 45 || code == 48) return 2; // fog / overcast
    if (code >= 51 && code <= 82) return 3; // rain
    if (code >= 85 && code <= 90) return 3; // rain
    if (code >= 95) return 4; // thunder
    return 1;
}

static const char* CodeToConditionText(int code, int is_day) {
    if (is_day == 0) {
        // Night condition descriptions
        if (code == 0) return "Trời quang";
        if (code == 1 || code == 2) return "Ít mây";
        if (code == 3) return "Trời có mây";
        if (code == 45 || code == 48) return "Sương mù";
        if (code >= 51 && code <= 57) return "Mưa phùn";
        if (code >= 61 && code <= 67) return "Trời mưa";
        if (code >= 80 && code <= 82) return "Mưa rào";
        if (code >= 95) return "Mưa dông";
        return "Trời có mây";
    }
    // Day condition descriptions
    if (code == 0) return "Trời nắng";
    if (code == 1 || code == 2) return "Nắng nhẹ";
    if (code == 3) return "Trời có mây";
    if (code == 45 || code == 48) return "Sương mù";
    if (code >= 51 && code <= 57) return "Mưa phùn";
    if (code >= 61 && code <= 67) return "Trời mưa";
    if (code >= 80 && code <= 82) return "Mưa rào";
    if (code >= 95) return "Mưa dông";
    return "Trời có mây";
}

WeatherService& WeatherService::GetInstance() {
    static WeatherService instance;
    return instance;
}

void WeatherService::Start() {
    if (task_handle_ != nullptr) {
        return;
    }

    xTaskCreate(
        [](void* arg) {
            static_cast<WeatherService*>(arg)->TaskLoop();
            vTaskDelete(NULL);
        },
        "weather_task", 8192, this, 2, &task_handle_);
}

void WeatherService::RefreshNow() {
    need_refetch_location_ = true;
    if (task_handle_ != nullptr) {
        xTaskNotifyGive(task_handle_);
    } else {
        Start();
    }
}

void WeatherService::FetchOnceAsync() {
    Start();
}

void WeatherService::SetLocation(const std::string& city, float lat, float lon) {
    city_ = city;
    lat_ = lat;
    lon_ = lon;
    has_location_ = true;
    need_refetch_location_ = false;

    ESP_LOGI(TAG, "Manual location set to %s (%.4f, %.4f)", city.c_str(), lat, lon);
    RefreshNow();
}

static std::string NormalizeCityName(const std::string& raw_city) {
    std::string lower;
    for (char c : raw_city) lower += tolower((unsigned char)c);

    if (lower.find("ho chi minh") != std::string::npos || lower.find("saigon") != std::string::npos) {
        return "TP. Hồ Chí Minh";
    }
    if (lower.find("hanoi") != std::string::npos || lower.find("ha noi") != std::string::npos) return "Hà Nội";
    if (lower.find("da nang") != std::string::npos) return "Đà Nẵng";
    if (lower.find("hai phong") != std::string::npos) return "Hải Phòng";
    if (lower.find("can tho") != std::string::npos) return "Cần Thơ";
    if (lower.find("bien hoa") != std::string::npos) return "Biên Hòa";
    if (lower.find("nha trang") != std::string::npos) return "Nha Trang";
    if (lower.find("hue") != std::string::npos) return "Huế";
    if (lower.find("vung tau") != std::string::npos) return "Vũng Tàu";
    if (lower.find("buon ma thuot") != std::string::npos) return "Buôn Ma Thuột";
    if (lower.find("quy nhon") != std::string::npos) return "Quy Nhơn";
    if (lower.find("long xuyen") != std::string::npos) return "Long Xuyên";
    if (lower.find("thai nguyen") != std::string::npos) return "Thái Nguyên";
    if (lower.find("nam dinh") != std::string::npos) return "Nam Định";
    if (lower.find("rach gia") != std::string::npos) return "Rạch Giá";
    if (lower.find("thu dau mot") != std::string::npos || lower.find("binh duong") != std::string::npos) return "Bình Dương";
    if (lower.find("phan thiet") != std::string::npos) return "Phan Thiết";
    if (lower.find("pleiku") != std::string::npos) return "Pleiku";
    if (lower.find("vinh") != std::string::npos) return "Vinh";
    if (lower.find("ha long") != std::string::npos) return "Hạ Long";
    if (lower.find("da lat") != std::string::npos) return "Đà Lạt";
    if (lower.find("ca mau") != std::string::npos) return "Cà Mau";
    if (lower.find("phu quoc") != std::string::npos) return "Phú Quốc";

    return raw_city;
}

bool WeatherService::FetchLocation(float& lat, float& lon, std::string& city) {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        return false;
    }

    // 1. First choice: ipwho.is (very accurate regional mapping for Vietnam ISPs)
    {
        auto http = network->CreateHttp(0);
        if (http) {
            http->SetTimeout(8000);
            std::string url = "http://ipwho.is/";
            if (auto opened = http->Open("GET", url); opened) {
                auto sc = http->GetStatusCode();
                if (sc && *sc == 200) {
                    std::string body = http->ReadAll();
                    http->Close();
                    if (!body.empty()) {
                        cJSON* root = cJSON_Parse(body.c_str());
                        if (root) {
                            cJSON* success = cJSON_GetObjectItem(root, "success");
                            if (success && cJSON_IsTrue(success)) {
                                cJSON* j_lat = cJSON_GetObjectItem(root, "latitude");
                                cJSON* j_lon = cJSON_GetObjectItem(root, "longitude");
                                cJSON* j_city = cJSON_GetObjectItem(root, "city");
                                cJSON* j_region = cJSON_GetObjectItem(root, "region");

                                if (j_lat && j_lon) {
                                    lat = static_cast<float>(j_lat->valuedouble);
                                    lon = static_cast<float>(j_lon->valuedouble);
                                }
                                if (j_city && j_city->valuestring && strlen(j_city->valuestring) > 0) {
                                    city = NormalizeCityName(j_city->valuestring);
                                } else if (j_region && j_region->valuestring && strlen(j_region->valuestring) > 0) {
                                    city = NormalizeCityName(j_region->valuestring);
                                }
                                ESP_LOGI(TAG, "Location acquired from WiFi (ipwho.is): %s (%.4f, %.4f)",
                                         city.c_str(), lat, lon);
                                cJSON_Delete(root);
                                return true;
                            }
                            cJSON_Delete(root);
                        }
                    }
                } else {
                    http->Close();
                }
            }
        }
    }

    // 2. Fallback: ip-api.com
    {
        auto http = network->CreateHttp(0);
        if (http) {
            http->SetTimeout(8000);
            std::string url = "http://ip-api.com/json";
            if (auto opened = http->Open("GET", url); opened) {
                auto sc = http->GetStatusCode();
                if (sc && *sc == 200) {
                    std::string body = http->ReadAll();
                    http->Close();
                    if (!body.empty()) {
                        cJSON* root = cJSON_Parse(body.c_str());
                        if (root) {
                            cJSON* status = cJSON_GetObjectItem(root, "status");
                            if (status && status->valuestring && strcmp(status->valuestring, "success") == 0) {
                                cJSON* j_lat = cJSON_GetObjectItem(root, "lat");
                                cJSON* j_lon = cJSON_GetObjectItem(root, "lon");
                                cJSON* j_city = cJSON_GetObjectItem(root, "city");
                                cJSON* j_region = cJSON_GetObjectItem(root, "regionName");

                                if (j_lat && j_lon) {
                                    lat = static_cast<float>(j_lat->valuedouble);
                                    lon = static_cast<float>(j_lon->valuedouble);
                                }
                                if (j_city && j_city->valuestring && strlen(j_city->valuestring) > 0) {
                                    city = NormalizeCityName(j_city->valuestring);
                                } else if (j_region && j_region->valuestring && strlen(j_region->valuestring) > 0) {
                                    city = NormalizeCityName(j_region->valuestring);
                                }
                                ESP_LOGI(TAG, "Location acquired from WiFi (ip-api): %s (%.4f, %.4f)",
                                         city.c_str(), lat, lon);
                                cJSON_Delete(root);
                                return true;
                            }
                            cJSON_Delete(root);
                        }
                    }
                } else {
                    http->Close();
                }
            }
        }
    }

    return false;
}

bool WeatherService::FetchWeather(float lat, float lon) {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        return false;
    }

    auto http = network->CreateHttp(0);
    if (!http) {
        return false;
    }

    http->SetTimeout(10000);
    char url[340];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,is_day&daily=weather_code,temperature_2m_max&timezone=auto&forecast_days=5",
             lat, lon);

    if (auto opened = http->Open("GET", url); !opened) {
        ESP_LOGW(TAG, "Failed to open %s: %s", url, opened.error().ToString().c_str());
        return false;
    }

    auto sc = http->GetStatusCode();
    if (!sc || *sc != 200) {
        ESP_LOGW(TAG, "HTTP GET %s failed status: %d", url, sc ? *sc : -1);
        http->Close();
        return false;
    }

    std::string body = http->ReadAll();
    http->Close();

    if (body.empty()) {
        return false;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        return false;
    }

    cJSON* cur = cJSON_GetObjectItem(root, "current");
    if (!cur) {
        cJSON_Delete(root);
        return false;
    }

    cJSON* j_temp = cJSON_GetObjectItem(cur, "temperature_2m");
    cJSON* j_app = cJSON_GetObjectItem(cur, "apparent_temperature");
    cJSON* j_hum = cJSON_GetObjectItem(cur, "relative_humidity_2m");
    cJSON* j_code = cJSON_GetObjectItem(cur, "weather_code");
    cJSON* j_wind = cJSON_GetObjectItem(cur, "wind_speed_10m");
    cJSON* j_day = cJSON_GetObjectItem(cur, "is_day");
    if (j_wind) info_.wind_speed = static_cast<float>(j_wind->valuedouble);

    if (j_temp) info_.temp = static_cast<float>(j_temp->valuedouble);
    if (j_app) info_.apparent_temp = static_cast<float>(j_app->valuedouble);
    if (j_hum) info_.humidity = j_hum->valueint;
    if (j_day) info_.is_day = j_day->valueint;
    if (j_code) {
        info_.weather_code = j_code->valueint;
        info_.icon_type = CodeToIconType(info_.weather_code, info_.is_day);
        info_.condition = CodeToConditionText(info_.weather_code, info_.is_day);
    }
    info_.city = city_;

    // Parse daily forecast
    info_.daily.clear();
    cJSON* daily = cJSON_GetObjectItem(root, "daily");
    if (daily) {
        cJSON* j_time = cJSON_GetObjectItem(daily, "time");
        cJSON* j_wcode = cJSON_GetObjectItem(daily, "weather_code");
        cJSON* j_tmax = cJSON_GetObjectItem(daily, "temperature_2m_max");

        if (j_time && j_wcode && j_tmax) {
            int time_count = cJSON_GetArraySize(j_time);
            for (int i = 1; i < time_count && info_.daily.size() < 4; ++i) {
                cJSON* it_time = cJSON_GetArrayItem(j_time, i);
                cJSON* it_code = cJSON_GetArrayItem(j_wcode, i);
                cJSON* it_max = cJSON_GetArrayItem(j_tmax, i);

                if (it_time && it_code && it_max) {
                    DayForecast df;
                    df.weather_code = it_code->valueint;
                    df.icon_type = CodeToIconType(df.weather_code, 1); // 1 = daytime for forecast cards
                    df.temp_max = static_cast<float>(it_max->valuedouble);

                    const char* tstr = it_time->valuestring;
                    int y = 0, m = 0, d = 0;
                    if (sscanf(tstr, "%d-%d-%d", &y, &m, &d) == 3) {
                        struct tm t = {};
                        t.tm_year = y - 1900;
                        t.tm_mon = m - 1;
                        t.tm_mday = d;
                        mktime(&t);
                        const char* wdays[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%s %02d", wdays[t.tm_wday], d);
                        df.day_str = buf;
                    } else {
                        df.day_str = tstr;
                    }
                    info_.daily.push_back(df);
                }
            }
        }
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Live Weather fetched: Temp=%.1f C, Hum=%d%%, Wind=%.1f km/h, Code=%d, IsDay=%d (%s), City=%s, %u daily items",
             info_.temp, info_.humidity, info_.wind_speed, info_.weather_code, info_.is_day, info_.condition.c_str(),
             info_.city.c_str(), info_.daily.size());

    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        display->UpdateWeatherFull(info_);
    }

    return true;
}

void WeatherService::TaskLoop() {
    vTaskDelay(pdMS_TO_TICKS(1500));

    while (true) {
        // Automatically determine location based on current WiFi connection
        if (!has_location_ || need_refetch_location_) {
            if (FetchLocation(lat_, lon_, city_)) {
                has_location_ = true;
                need_refetch_location_ = false;
                ESP_LOGI(TAG, "Location acquired successfully from WiFi: %s (%.4f, %.4f)",
                         city_.c_str(), lat_, lon_);
            } else {
                ESP_LOGW(TAG, "FetchLocation waiting for network, retrying in 3s...");
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
        }

        bool success = FetchWeather(lat_, lon_);
        uint32_t wait_ms = success ? (15 * 60 * 1000) : (10 * 1000);
        // Wait 15 minutes or wake up immediately if RefreshNow() is triggered
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms)) > 0) {
            // Whenever WiFi connects or reconnects, re-check location
            need_refetch_location_ = true;
        }
    }
}
