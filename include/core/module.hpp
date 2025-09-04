#pragma once
#include <string>
#include <deque>
#include <algorithm>
#include "core/types.hpp"

// Egy modul visszatérési típusa
struct ModuleResult {
    double score{50.0};      // 0..100
    Signal signal{Signal::Neutral};
    std::size_t warmup_bars{0};
};

// Modul interfész – minden indikátormodul ezt valósítja meg.
class IModule {
public:
    virtual ~IModule() = default;

    // Egyedi azonosító (pl. "RSI", "SMA_EMA", "BOLL", "MTF_SMA")
    virtual std::string id() const = 0;

    // Hány bar kell, mire értelmes jelet tud adni a modul
    virtual std::size_t warmup_bars() const = 0;

    // Modul állapotának nullázása
    virtual void reset() = 0;

    // Új bar érkezésekor hívjuk; itt adja a score-t és a (Long/Short/Wait) javaslatot
    virtual ModuleResult on_bar(const Symbol&, Timeframe, const Bar&) = 0;
};

// Kényelmi clamp 0..1 közé
inline double clamp01(double v) {
    return std::max(0.0, std::min(1.0, v));
}
// module.hpp from canvas
