#pragma once
#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace exec {

// --- Alap config
struct ApiConfig {
    std::string api_key;
    std::string api_secret;
    bool testnet{true};
    int timeout_ms{5000};
};

// --- Order státusz enum
enum class OrderStatus {
    New, PartiallyFilled, Filled, Canceled, Rejected, Expired, Unknown
};

// --- REST order info (rész)
struct OrderInfo {
    uint64_t orderId{0};
    std::string symbol;
    std::string side;   // "BUY"/"SELL"
    std::string type;   // "MARKET","LIMIT","STOP_LOSS_LIMIT", etc.
    OrderStatus status{OrderStatus::Unknown};
    double price{0.0};
    double origQty{0.0};
    double executedQty{0.0};
};

// --- Egyszerű log elem a GUI táblához
struct OrderLogEntry {
    uint64_t ts_ms{0};
    std::string msg;
};

// --- Válaszok
struct OrderPostInfo {
    uint64_t orderId{0};
};
struct MarketResult {
    std::string msg;
    double filled_base{0.0};
    OrderPostInfo info;
};
struct OcoResult {
    std::string msg;
    std::vector<uint64_t> extra_order_ids;
};

class BinanceRest {
public:
    explicit BinanceRest(ApiConfig cfg);

    // ping GET /api/v3/ping
    std::string ping();

    // spot MARKET BUY by quote amount (USDT)
    MarketResult market_buy(const std::string& symbol, double quote_amount);

    // spot MARKET SELL by quote amount (USDT)
    MarketResult market_sell(const std::string& symbol, double quote_amount);

    // OCO bracket SELL (TP + SL) adott base qty-re
    OcoResult oco_sell_bracket(const std::string& symbol, double base_qty,
                               double tp_price, double sl_price, double sl_limit_price);

    // GET openOrders
    std::vector<OrderInfo> open_orders(const std::string& symbol);

    // GET egy order
    std::optional<OrderInfo> get_order(const std::string& symbol, uint64_t orderId);

    // DELETE egy order
    bool cancel_order(const std::string& symbol, uint64_t orderId, std::string* out_msg);

    // DELETE openOrders (összes)
    bool cancel_all_open_orders(const std::string& symbol, std::string* out_msg);

private:
    std::string rest_base() const;
    std::string sign_query(const std::string& query) const; // HMAC-SHA256

    // helper: http GET/POST/DELETE (signed/unsigned)
    nlohmann::json http_get(const std::string& path, const std::string& query = "", bool signed_req = false);
    nlohmann::json http_post(const std::string& path, const std::string& body_or_query, bool signed_req = false, bool body_is_payload=false);
    nlohmann::json http_delete(const std::string& path, const std::string& query, bool signed_req = false);

    ApiConfig cfg_;
};

} // namespace exec
