#include "indicators/sma_ema.hpp"
#include <numeric>
#include <cmath>

namespace ind {
double compute_sma(const std::deque<double>& v, size_t p){
    if (v.size()<p) return v.empty()?0.0:v.back();
    double s=0; for (size_t i=v.size()-p;i<v.size();++i) s+=v[i];
    return s/static_cast<double>(p);
}
double compute_ema(const std::deque<double>& v, size_t p){
    if (v.empty()) return 0.0;
    const double k = 2.0/(p+1.0);
    double e = v[0];
    for (size_t i=1;i<v.size();++i) e = v[i]*k + e*(1.0-k);
    return e;
}
ModuleResult SmaEmaModule::on_bar(const Symbol&, Timeframe, const Bar& b){
    closes.push_back(b.close); if (closes.size()>2000) closes.pop_front();
    if (closes.size()<warmup_bars()) return {50.0, Signal::Neutral, warmup_bars()};
    const double ema_s = compute_ema(closes, short_p);
    const double ema_l = compute_ema(closes, long_p);
    const double diff  = ema_s - ema_l;
    const double norm  = (std::abs(ema_l)>1e-12? diff/ema_l : 0.0);
    const double score = std::clamp(50.0 + norm*5000.0, 0.0, 100.0);
    const Signal s = (diff>0? Signal::Long : diff<0? Signal::Short : Signal::Neutral);
    return {score, s, warmup_bars()};
}
} // namespace ind
// sma_ema.cpp from canvas
