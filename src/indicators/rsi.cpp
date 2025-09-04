#include "indicators/rsi.hpp"
#include <numeric>
#include <cmath>

namespace ind {
double compute_rsi(const std::deque<double>& c, size_t p){
    if (c.size() <= p) return 50.0;
    double g=0.0,l=0.0;
    for (size_t i=c.size()-p; i<c.size(); ++i){
        const double d = c[i]-c[i-1];
        if (d>=0) g+=d; else l-=d;
    }
    if (g==0 && l==0) return 50.0;
    const double rs  = (l==0? 1000.0 : g/l);
    const double rsi = 100.0 - (100.0/(1.0+rs));
    return std::clamp(rsi, 0.0, 100.0);
}
ModuleResult RsiModule::on_bar(const Symbol&, Timeframe, const Bar& b){
    closes.push_back(b.close); if (closes.size()>2000) closes.pop_front();
    if (closes.size()<warmup_bars()) return {50.0, Signal::Neutral, warmup_bars()};
    const double rsi = compute_rsi(closes, period);
    const double score = (rsi>70? rsi : rsi<30? (100-rsi) : 50.0);
    const Signal s = (rsi>70? Signal::Short : rsi<30? Signal::Long : Signal::Neutral);
    return {std::clamp(score, 0.0, 100.0), s, warmup_bars()};
}
} // namespace ind
// rsi.cpp from canvas
