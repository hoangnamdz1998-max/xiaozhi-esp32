#pragma once

#include <cmath>

struct LunarDate {
    int day;
    int month;
    int year;
    bool is_leap;
};

class VietnameseLunarCalendar {
public:
    static LunarDate SolarToLunar(int dd, int mm, int yy, double time_zone = 7.0) {
        int dayNumber = JdFromDate(dd, mm, yy);
        int k = static_cast<int>(std::floor((dayNumber - 2415021.076998695) / 29.530588853));
        int monthStart = GetNewMoonDay(k + 1, time_zone);
        if (monthStart > dayNumber) {
            monthStart = GetNewMoonDay(k, time_zone);
        }

        int a11 = GetLunarMonth11(yy, time_zone);
        int b11 = a11;
        int lunarYear = yy;
        if (a11 >= monthStart) {
            lunarYear = yy;
            a11 = GetLunarMonth11(yy - 1, time_zone);
        } else {
            lunarYear = yy + 1;
            b11 = GetLunarMonth11(yy + 1, time_zone);
        }

        int lunarDay = dayNumber - monthStart + 1;
        int diff = static_cast<int>(std::floor((monthStart - a11) / 29.5 + 0.5));
        int lunarMonth = diff + 11;
        bool is_leap = false;

        if (b11 - a11 > 365) {
            int leapMonthDiff = GetLeapMonthOffset(a11, time_zone);
            if (diff >= leapMonthDiff) {
                lunarMonth = diff + 10;
                if (diff == leapMonthDiff) {
                    is_leap = true;
                }
            }
        }

        if (lunarMonth > 12) {
            lunarMonth -= 12;
        }
        if (lunarMonth >= 11 && diff < 4) {
            lunarYear -= 1;
        }

        return {lunarDay, lunarMonth, lunarYear, is_leap};
    }

private:
    static int JdFromDate(int dd, int mm, int yy) {
        int a = (14 - mm) / 12;
        int y = yy + 4800 - a;
        int m = mm + 12 * a - 3;
        int jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
        if (jd < 2299161) {
            jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - 32045;
        }
        return jd;
    }

    static double NewMoon(int k) {
        double t = k / 1236.85;
        double t2 = t * t;
        double t3 = t2 * t;
        const double dr = M_PI / 180.0;

        double jd1 = 2415020.975933 + 29.53058868 * k + 0.0001178 * t2 - 0.000000155 * t3 +
                     0.00033 * std::sin((166.56 + 132.87 * t - 0.009173 * t2) * dr);

        double m = 359.2242 + 29.10535608 * k - 0.0000333 * t2 - 0.00000347 * t3;
        double mprime = 306.0253 + 385.81691806 * k + 0.0107306 * t2 + 0.00001236 * t3;
        double f = 21.2964 + 390.67050646 * k - 0.0016528 * t2 - 0.00000239 * t3;

        double c1 = (0.1734 - 0.000393 * t) * std::sin(m * dr) + 0.0021 * std::sin(2 * m * dr) -
                    0.4068 * std::sin(mprime * dr) + 0.0161 * std::sin(2 * mprime * dr) -
                    0.0004 * std::sin(3 * mprime * dr) + 0.0104 * std::sin(2 * f * dr) -
                    0.0051 * std::sin((m + mprime) * dr) - 0.0074 * std::sin((m - mprime) * dr) +
                    0.0004 * std::sin((2 * f + m) * dr) - 0.0004 * std::sin((2 * f - m) * dr) -
                    0.0006 * std::sin((2 * f + mprime) * dr) + 0.0010 * std::sin((2 * f - mprime) * dr) +
                    0.0005 * std::sin((m + 2 * mprime) * dr);

        double deltAt;
        if (t < -1.1) {
            deltAt = 0.001 + 0.00083 * t + 0.00075 * t2 + 0.00059 * t3;
        } else {
            deltAt = 0.0238 + 0.0252 * t + 0.0158 * t2 + 0.0183 * t3;
        }
        return jd1 + c1 - deltAt;
    }

    static double SunLongitude(double jdn) {
        double t = (jdn - 2451545.0) / 36525.0;
        double t2 = t * t;
        const double dr = M_PI / 180.0;
        double l0 = 280.46646 + 36000.76983 * t + 0.0003032 * t2;
        double m = 357.52911 + 35999.05029 * t - 0.0001537 * t2;
        double c = (1.914602 - 0.004817 * t - 0.000014 * t2) * std::sin(m * dr) +
                   (0.019993 - 0.000101 * t) * std::sin(2 * m * dr) +
                   0.000289 * std::sin(3 * m * dr);
        double l = l0 + c;
        l = l - 360.0 * std::floor(l / 360.0);
        return l * dr;
    }

    static double GetSunLongitude(int dayNumber, double time_zone) {
        return SunLongitude(dayNumber - 0.5 - time_zone / 24.0);
    }

    static int GetNewMoonDay(int k, double time_zone) {
        double jd = NewMoon(k);
        return static_cast<int>(std::floor(jd + 0.5 + time_zone / 24.0));
    }

    static int GetLunarMonth11(int yy, double time_zone) {
        double off = JdFromDate(31, 12, yy) - 2415021.076998695;
        int k = static_cast<int>(std::floor(off / 29.530588853));
        int nm = GetNewMoonDay(k, time_zone);
        int sunLong = static_cast<int>(std::floor(GetSunLongitude(nm, time_zone) / (M_PI / 6.0)));
        if (sunLong >= 9) {
            nm = GetNewMoonDay(k - 1, time_zone);
        }
        return nm;
    }

    static int GetLeapMonthOffset(int a11, double time_zone) {
        int k = static_cast<int>(std::floor(0.5 + (a11 - 2415021.076998695) / 29.530588853));
        int i = 1;
        int arc = static_cast<int>(std::floor(GetSunLongitude(GetNewMoonDay(k + i, time_zone), time_zone) / (M_PI / 6.0)));
        int last = arc;
        while (true) {
            i++;
            arc = static_cast<int>(std::floor(GetSunLongitude(GetNewMoonDay(k + i, time_zone), time_zone) / (M_PI / 6.0)));
            if (arc == last || i >= 14) {
                break;
            }
            last = arc;
        }
        return i - 1;
    }
};
