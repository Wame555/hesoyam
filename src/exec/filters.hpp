#pragma once
#include <string>

namespace exec {
// Minimál szűrő helper — a valós Binance szűrők (LOT_SIZE, MIN_NOTIONAL) későbbre
inline double round_step(double v, double step){
    if (step<=0) return v;
    return std::round(v/step)*step;
}
} // namespace exec
