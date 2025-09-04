#pragma once
#include <string>
#include <cstdint>

namespace exec {
// Minimál placeholder a fordításhoz — bővíthető a későbbiekben
struct OrderState {
    uint64_t id{0};
    std::string symbol;
    std::string side;
    double price{0.0};
    double qty{0.0};
};
} // namespace exec
