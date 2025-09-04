#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

// Időkeret
enum class Timeframe { M1, M3, M5, M15, M30, H1, H4, D1 };

// Szimbólum
struct Symbol {
    std::string base{"BTC"};
    std::string quote{"USDT"};
    std::string name() const { return base + quote; }
};

// OHLCV bar
struct Bar {
    std::int64_t open_time_ms{}; // kline open time (ms)
    double open{};
    double high{};
    double low{};
    double close{};
    double volume{};
};

// Jel típus
enum class Signal { Long, Short, Neutral };

inline const char* to_string(Signal s) {
    switch (s) {
        case Signal::Long:  return "LONG";
        case Signal::Short: return "SHORT";
        default:            return "WAIT";
    }
}
// types.hpp from canvas
