#include "adc_battery_monitor.h"

AdcBatteryMonitor::AdcBatteryMonitor(adc_unit_t adc_unit, adc_channel_t adc_channel, float upper_resistor, float lower_resistor, gpio_num_t charging_pin)
    : charging_pin_(charging_pin) {
    
    // Initialize charging pin (only if it's not NC)
    if (charging_pin_ != GPIO_NUM_NC) {
        gpio_config_t gpio_cfg = {
            .pin_bit_mask = 1ULL << charging_pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&gpio_cfg));
    }

    // Initialize ADC battery estimation
    adc_battery_estimation_t adc_cfg = {
        .internal = {
            .adc_unit = adc_unit,
            .adc_bitwidth = ADC_BITWIDTH_DEFAULT,
            .adc_atten = ADC_ATTEN_DB_12,
        },
        .adc_channel = adc_channel,
        .upper_resistor = upper_resistor,
        .lower_resistor = lower_resistor
    };

    // 在ADC配置部分进行条件设置
    adc_cfg.charging_detect_cb = [](void *user_data) -> bool {
        AdcBatteryMonitor *self = (AdcBatteryMonitor *)user_data;
        if (self->charging_detect_cb_) {
            return self->charging_detect_cb_();
        }
        if (self->charging_pin_ != GPIO_NUM_NC) {
            return gpio_get_level(self->charging_pin_) == 1;
        }
        return false;
    };
    adc_cfg.charging_detect_user_data = this;
    adc_battery_estimation_handle_ = adc_battery_estimation_create(&adc_cfg);

    // Initialize timer
    esp_timer_create_args_t timer_cfg = {
        .callback = [](void *arg) {
            AdcBatteryMonitor *self = (AdcBatteryMonitor *)arg;
            self->CheckBatteryStatus();
        },
        .arg = this,
        .name = "adc_battery_monitor",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_cfg, &timer_handle_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle_, 1000000));
}

AdcBatteryMonitor::~AdcBatteryMonitor() {
    if (adc_battery_estimation_handle_) {
        ESP_ERROR_CHECK(adc_battery_estimation_destroy(adc_battery_estimation_handle_));
    }
    
    if (timer_handle_) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
    }
}

bool AdcBatteryMonitor::IsCharging() {
    if (charging_detect_cb_) {
        return charging_detect_cb_();
    }
    // 优先使用adc_battery_estimation库的功能
    if (adc_battery_estimation_handle_ != nullptr) {
        bool is_charging = false;
        esp_err_t err = adc_battery_estimation_get_charging_state(adc_battery_estimation_handle_, &is_charging);
        if (err == ESP_OK) {
            return is_charging;
        }
    }
    
    // 回退到GPIO读取或返回默认值
    if (charging_pin_ != GPIO_NUM_NC) {
        return gpio_get_level(charging_pin_) == 1;
    }
    
    return false;
}

bool AdcBatteryMonitor::IsDischarging() {
    return !IsCharging();
}

uint8_t AdcBatteryMonitor::GetBatteryLevel() {
    if (smooth_battery_level_ < 0.0f) {
        CheckBatteryStatus();
    }
    if (smooth_battery_level_ < 0.0f) {
        return 100;
    }
    return (uint8_t)(smooth_battery_level_ + 0.5f);
}

void AdcBatteryMonitor::SetChargingDetectCallback(std::function<bool()> callback) {
    charging_detect_cb_ = callback;
}

void AdcBatteryMonitor::OnChargingStatusChanged(std::function<void(bool)> callback) {
    on_charging_status_changed_ = callback;
}

void AdcBatteryMonitor::CheckBatteryStatus() {
    bool new_charging_status = IsCharging();
    if (new_charging_status != is_charging_) {
        is_charging_ = new_charging_status;
        if (on_charging_status_changed_) {
            on_charging_status_changed_(is_charging_);
        }
    }

    if (adc_battery_estimation_handle_ != nullptr) {
        float raw_capacity = 0.0f;
        esp_err_t err = adc_battery_estimation_get_capacity(adc_battery_estimation_handle_, &raw_capacity);
        if (err == ESP_OK) {
            if (raw_capacity < 0.0f) raw_capacity = 0.0f;
            if (raw_capacity > 100.0f) raw_capacity = 100.0f;

            // Bù trừ sụt áp nội trở (IR drop) do dòng nạp từ củ sạc tạo ra
            float target = raw_capacity;
            if (is_charging_) {
                if (raw_capacity > 92.0f) {
                    target = 92.0f + (raw_capacity - 92.0f);
                } else if (raw_capacity > 12.0f) {
                    target = raw_capacity - 12.0f;
                }
            }

            int64_t now_ms = esp_timer_get_time() / 1000;
            if (smooth_battery_level_ < 0.0f) {
                // Khởi tạo mức pin lần đầu khi vừa khởi động máy
                smooth_battery_level_ = target;
                last_step_time_ms_ = now_ms;
            } else {
                int64_t elapsed_ms = now_ms - last_step_time_ms_;
                if (is_charging_) {
                    // Đang cắm sạc: KHÔNG BAO GIỜ được tụt phần trăm
                    // Tăng êm mượt tối đa 1% sau mỗi 25 giây
                    if (target > smooth_battery_level_) {
                        if (elapsed_ms >= 25000) {
                            smooth_battery_level_ += 1.0f;
                            if (smooth_battery_level_ > 100.0f) smooth_battery_level_ = 100.0f;
                            last_step_time_ms_ = now_ms;
                        }
                    } else {
                        // Giữ nguyên mức pin, không tụt số khi đang cắm sạc
                        last_step_time_ms_ = now_ms;
                    }
                } else {
                    // Đang rút sạc chạy pin: KHÔNG BAO GIỜ nhảy ngược lên
                    // Giảm êm mượt tối đa 1% sau mỗi 40 giây
                    if (target < smooth_battery_level_) {
                        if (elapsed_ms >= 40000) {
                            smooth_battery_level_ -= 1.0f;
                            if (smooth_battery_level_ < 0.0f) smooth_battery_level_ = 0.0f;
                            last_step_time_ms_ = now_ms;
                        }
                    } else {
                        // Giữ nguyên mức pin, không nhảy số ảo khi rút sạc
                        last_step_time_ms_ = now_ms;
                    }
                }
            }
        }
    }
}