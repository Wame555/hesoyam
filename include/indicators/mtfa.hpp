#pragma once
#include "core/module.hpp"
#include <deque>

namespace ind {

// Egyszerű MTF: N db alacsonyabb TF bar -> 1 "magasabb TF" záróár EMA-k keresztezésével
class MtfSmaModule final : public IModule {
    size_t factor_;      // pl. 12: M5 -> H1
    size_t fast_p_;      // gyors EMA periódus a magasabb TF-en
    size_t slow_p_;      // lassú EMA periódus a magasabb TF-en

    std::deque<double> low_closes_;   // alacsony TF zárók (folyamatosan jön)
    std::deque<double> hi_closes_;    // összevont "magasabb TF" zárók

    static double ema(const std::deque<double>& v, size_t p){
        if (v.empty()) return 0.0;
        double k = 2.0/(p+1.0);
        double e = v.front();
        for (size_t i=1;i<v.size();++i) e = v[i]*k + e*(1.0-k);
        return e;
    }

public:
    MtfSmaModule(size_t factor, size_t fast_p, size_t slow_p)
    : factor_(factor), fast_p_(fast_p), slow_p_(slow_p) {}

    std::string id() const override { return "MTF_SMA"; }
    size_t warmup_bars() const override { return std::max(fast_p_, slow_p_) + 3; }
    void reset() override { low_closes_.clear(); hi_closes_.clear(); }

    ModuleResult on_bar(const Symbol&, Timeframe, const Bar& b) override {
        // minden bejövő alacsony-TF bar zárót eltárolunk
        low_closes_.push_back(b.close);
        if (low_closes_.size() > factor_* (slow_p_ + 50)) {
            // tartsunk ésszerű méretet
            while (low_closes_.size() > factor_* (slow_p_ + 50))
                low_closes_.pop_front();
        }

        // ha elértünk factor_ darab új low-TF bart, egy "magasabb TF" zárót kreálunk
        // egyszerű megközelítés: minden factor_ darab bar után bepusholjuk az utolsó zárót
        if (low_closes_.size() % factor_ == 0) {
            hi_closes_.push_back(b.close);
            if (hi_closes_.size() > (slow_p_ + 50)) hi_closes_.pop_front();
        }

        if (hi_closes_.size() < warmup_bars())
            return {50.0, Signal::Neutral, warmup_bars()};

        double e_fast = ema(hi_closes_, fast_p_);
        double e_slow = ema(hi_closes_, slow_p_);
        double diff = e_fast - e_slow;
        double norm = (std::abs(e_slow) > 1e-12 ? diff / e_slow : 0.0);
        double score = std::clamp(50.0 + norm*5000.0, 0.0, 100.0);
        Signal s = (diff>0 ? Signal::Long : diff<0 ? Signal::Short : Signal::Neutral);
        return {score, s, warmup_bars()};
    }
};

} // namespace ind

