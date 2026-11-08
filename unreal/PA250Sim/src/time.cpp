#include "pa250/sim.hpp"

#include <stdexcept>

namespace pa250 {
namespace {

bool is_leap(int32_t y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }

constexpr int kDaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

int days_in_month(int32_t y, uint8_t m) {
    if (m < 1 || m > 12) return 0;
    int d = kDaysInMonth[m - 1];
    if (m == 2 && is_leap(y)) ++d;
    return d;
}

int64_t days_from_epoch(int32_t y, uint8_t m, uint8_t d) {
    // Days from 1607-01-01 to y-m-d (that date at 00:00).
    int64_t days = 0;
    for (int32_t yy = static_cast<int32_t>(kEpochYear); yy < y; ++yy) days += is_leap(yy) ? 366 : 365;
    for (uint8_t mm = 1; mm < m; ++mm) days += days_in_month(y, mm);
    days += static_cast<int64_t>(d) - 1;
    return days;
}

}  // namespace

YMDH to_ymdh(GameTime t) {
    if (t.hour_index < 0) throw std::runtime_error("GameTime before epoch");
    int64_t hours = t.hour_index;
    int64_t day = hours / 24;
    uint8_t h = static_cast<uint8_t>(hours % 24);
    int32_t y = static_cast<int32_t>(kEpochYear);
    while (true) {
        int64_t year_days = is_leap(y) ? 366 : 365;
        if (day < year_days) break;
        day -= year_days;
        ++y;
    }
    uint8_t m = 1;
    while (true) {
        int md = days_in_month(y, m);
        if (day < md) break;
        day -= md;
        ++m;
    }
    return YMDH{y, m, static_cast<uint8_t>(day + 1), h};
}

GameTime from_ymdh(YMDH v) {
    if (v.y < kEpochYear || v.m < 1 || v.m > 12 || v.d < 1 || v.d > days_in_month(v.y, v.m) || v.h > 23)
        throw std::runtime_error("invalid YMDH");
    return GameTime{days_from_epoch(v.y, v.m, v.d) * 24 + v.h};
}

}  // namespace pa250
