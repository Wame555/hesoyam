#pragma once
#include <chrono>

namespace exec {

class RiskManager {
public:
    explicit RiskManager(double max_daily_loss_pct) : max_daily_loss_pct_(max_daily_loss_pct) {
        day_ = today_key();
    }

    double max_daily_loss_pct() const { return max_daily_loss_pct_; }

    bool allow_trade() {
        auto d = today_key();
        if (d != day_) { day_ = d; daily_loss_ = 0.0; } // új nap -> reset
        // Itt csak a kapcsolót adjuk vissza; a valós veszteséget külön modultól kapnánk
        return daily_loss_ < max_daily_loss_pct_;
    }

    void add_loss_pct(double pct){
        auto d = today_key();
        if (d != day_) { day_ = d; daily_loss_ = 0.0; }
        daily_loss_ += pct;
    }

    void force_reset_day(){ daily_loss_ = 0.0; day_ = today_key(); }

private:
    static int today_key(){
        using namespace std::chrono;
        auto now = system_clock::now();
        time_t t = system_clock::to_time_t(now);
        tm lt{};
        #ifdef _WIN32
        localtime_s(&lt, &t);
        #else
        localtime_r(&t, &lt);
        #endif
        return (lt.tm_year+1900)*10000 + (lt.tm_mon+1)*100 + lt.tm_mday;
    }

    double max_daily_loss_pct_{2.0};
    double daily_loss_{0.0};
    int day_{0};
};

} // namespace exec
