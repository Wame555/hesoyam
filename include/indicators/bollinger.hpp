#pragma once
#include <deque>
#include <algorithm>
#include "core/module.hpp"

namespace ind {

struct BB { double mid, upper, lower; };
BB compute_bb(const std::deque<double>& v, size_t p=20, double k=2.0);

class BollModule final : public IModule {
    std::deque<double> closes; size_t period; double k_;
public:
    BollModule(size_t p=20, double k=2.0): period(p), k_(k) {}
    std::string id() const override { return "BOLL"; }
    size_t warmup_bars() const override { return period + 5; }
    void reset() override { closes.clear(); }
    ModuleResult on_bar(const Symbol&, Timeframe, const Bar&) override;
}

; // <- fontos
} // namespace ind
// bollinger.hpp from canvas
