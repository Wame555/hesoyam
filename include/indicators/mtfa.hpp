#pragma once

// --- standard
#include <deque>
#include <string>
#include <algorithm>
#include <cmath>

// --- projekt
// Fontos: előbb types, aztán module, hogy biztosan legyen IModule.
#include "core/types.hpp"
#include "core/module.hpp"

namespace ind {

// Egyszerű MTF "EMA-kereszt" modul.
// factor_ = hány alacsonyabb TF bar tesz ki 1 magasabb TF bart (pl. 12: M5->H1)
class MtfSmaModule final : public ::IModule {  // <- globális névtér
    std::size_t factor_{0};
    std::size_t fast_p_{0};
    std::size_t slow_p_{0};

    std::deque<double> low_closes_;
    std::deque<double> hi_closes_;

    static double ema(const std::deque<double>& v, std::size_t p) {
        if (v.empty()) return 0.0;
        const double k = 2.0 / (p + 1.0);
        double e = v.front();
        for (std::size_t i = 1; i < v.size(); ++i) e = v[i] * k + e * (1.0 - k);
        return e;
    }

public:
    MtfSmaModule(std::size_t factor, std::size_t fast_p, std::size_t slow_p)
        : factor_(factor), fast_p_(fast_p), slow_p_(slow_p) {}

    std::string id() const override { return "MTF_SMA"; }
    std::size_t warmup_bars() const override { return std::max(fast_p_, slow_p_) + 3; }
    void reset() override { low_closes_.clear(); hi_closes_.clear(); }

    ModuleResult on_bar(const Symbol&, Timeframe, const Bar& b) override {
        low_closes_.push_back(b.close);

        // memóriakorlát
        const std::size_t keep = std::max<std::size_t>(factor_ * (slow_p_ + 50), 512);
        while (low_closes_.size() > keep) low_closes_.pop_front();

        // minden factor_ bar után "zárunk" egy magasabb TF bart
        if (factor_ > 0 && (low_closes_.size() % factor_) == 0) {
            hi_closes_.push_back(b.close);
            while (hi_closes_.size() > (slow_p_ + 50)) hi_closes_.pop_front();
        }

        if (hi_closes_.size() < warmup_bars())
            return {50.0, Signal::Neutral, warmup_bars()};

        const double e_fast = ema(hi_closes_, fast_p_);
        const double e_slow = ema(hi_closes_, slow_p_);
        const double diff = e_fast - e_slow;
        const double norm = (std::abs(e_slow) > 1e-12 ? diff / e_slow : 0.0);
        const double score = std::clamp(50.0 + norm * 5000.0, 0.0, 100.0);
        const Signal s = (diff > 0 ? Signal::Long : diff < 0 ? Signal::Short : Signal::Neutral);
        return {score, s, warmup_bars()};
    }
};

} // namespace ind
