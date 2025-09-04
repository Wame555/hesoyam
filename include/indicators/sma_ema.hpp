#pragma once
#include <deque>
#include <algorithm>
#include "core/module.hpp"

namespace ind {

double compute_sma(const std::deque<double>& v, size_t p);
double compute_ema(const std::deque<double>& v, size_t p);

class SmaEmaModule final : public IModule {
    std::deque<double> closes; size_t short_p, long_p;
public:
    SmaEmaModule(size_t sp=20, size_t lp=50): short_p(sp), long_p(lp) {}
    std::string id() const override { return "SMA_EMA"; }
    size_t warmup_bars() const override { return std::max(short_p, long_p) + 5; }
    void reset() override { closes.clear(); }
    ModuleResult on_bar(const Symbol&, Timeframe, const Bar&) override;
}

; // <- fontos
} // namespace ind

// sma_ema.hpp from canvas
