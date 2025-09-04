#include "exec/position_tracker.hpp"
#include <algorithm>

namespace exec {

void PositionTracker::on_fill_buy(const std::string& symbol, double qty, double price){
    auto& p = map_[symbol];
    double new_qty = p.base_qty + qty;
    if (new_qty <= 0){ p.base_qty = 0; p.avg_entry = 0; return; }
    // új átlagár: (régi_qty*régi_ár + új_qty*ár) / (régi_qty+új_qty)
    p.avg_entry = (p.base_qty*p.avg_entry + qty*price) / new_qty;
    p.base_qty = new_qty;
}

void PositionTracker::on_fill_sell(const std::string& symbol, double qty, double price){
    auto it = map_.find(symbol);
    if (it==map_.end()) return;
    auto& p = it->second;
    p.base_qty -= qty;
    if (p.base_qty <= 1e-12){ p.base_qty = 0; p.avg_entry = 0; }
    // (átlagárat nem változtatjuk; realizált PnL-t külön rendszer számolhatná)
}

NetPos PositionTracker::get(const std::string& symbol) const{
    auto it = map_.find(symbol);
    if (it==map_.end()) return {};
    return it->second;
}

} // namespace exec
