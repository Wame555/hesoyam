#pragma once
#include <string>
#include <unordered_map>

namespace exec {

struct NetPos {
    double base_qty{0.0};
    double avg_entry{0.0}; // súlyozott átlagos beker ár
};

class PositionTracker {
public:
    void on_fill_buy(const std::string& symbol, double qty_base, double price);
    void on_fill_sell(const std::string& symbol, double qty_base, double price);
    NetPos get(const std::string& symbol) const;

private:
    std::unordered_map<std::string, NetPos> map_;
};

} // namespace exec
