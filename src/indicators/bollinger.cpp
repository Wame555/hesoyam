#include "indicators/bollinger.hpp"
#include <cmath>

namespace ind {
BB compute_bb(const std::deque<double>& v, size_t p, double k){
    if (v.size()<p) {
        const double last = v.empty()? 0.0 : v.back();
        return {last, last, last};
    }
    double mid=0.0;
    for (size_t i=v.size()-p;i<v.size();++i) mid += v[i];
    mid/=p;
    double var=0.0;
    for (size_t i=v.size()-p;i<v.size();++i){ const double d=v[i]-mid; var+=d*d; }
    const double sd = std::sqrt(std::max(0.0, var/p));
    return {mid, mid + k*sd, mid - k*sd};
}
ModuleResult BollModule::on_bar(const Symbol&, Timeframe, const Bar& b){
    closes.push_back(b.close); if (closes.size()>2000) closes.pop_front();
    if (closes.size()<warmup_bars()) return {50.0, Signal::Neutral, warmup_bars()};
    const auto bb = compute_bb(closes, period, k_);
    const double c = closes.back();
    double pos = (bb.upper==bb.lower? 0.5 : (c - bb.lower) / (bb.upper - bb.lower));
    pos = std::clamp(pos, 0.0, 1.0);
    const double score = (pos>0.5? pos*100.0 : (1.0-pos)*100.0);
    const Signal s = (pos>0.7? Signal::Short : pos<0.3? Signal::Long : Signal::Neutral);
    return {std::clamp(score,0.0,100.0), s, warmup_bars()};
}
} // namespace ind
// bollinger.cpp from canvas
