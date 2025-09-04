#pragma once
#include <deque>
#include <algorithm>
#include "core/module.hpp"

namespace ind {

double compute_rsi(const std::deque<double>& closes, size_t period);

class RsiModule final : public IModule {
    std::deque<double> closes; size_t period;
public:
    explicit RsiModule(size_t p=14): period(p) {}
    std::string id() const override { return "RSI"; }
    size_t warmup_bars() const override { return std::max<size_t>(period+1, 20); }
    void reset() override { closes.clear(); }
    ModuleResult on_bar(const Symbol&, Timeframe, const Bar&) override;
}

; // <- fontos
} // namespace ind
// rsi.hpp from canvas
