#include "exec/binance_rest.hpp"
#include <cpr/cpr.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <chrono>
#include <iomanip>

using json = nlohmann::json;

namespace exec {

static inline uint64_t now_ms(){
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static OrderStatus parse_status(const std::string& s){
    if (s=="NEW") return OrderStatus::New;
    if (s=="PARTIALLY_FILLED") return OrderStatus::PartiallyFilled;
    if (s=="FILLED") return OrderStatus::Filled;
    if (s=="CANCELED") return OrderStatus::Canceled;
    if (s=="REJECTED") return OrderStatus::Rejected;
    if (s=="EXPIRED") return OrderStatus::Expired;
    return OrderStatus::Unknown;
}

static double to_d(const json& j, const char* k){
    if (!j.contains(k)) return 0.0;
    if (j[k].is_string()) return std::strtod(j[k].get_ref<const std::string&>().c_str(), nullptr);
    if (j[k].is_number()) return j[k].get<double>();
    return 0.0;
}

BinanceRest::BinanceRest(ApiConfig cfg) : cfg_(std::move(cfg)) {}

std::string BinanceRest::rest_base() const {
    return cfg_.testnet? "https://testnet.binance.vision" : "https://api.binance.com";
}

std::string BinanceRest::sign_query(const std::string& query) const {
    unsigned int len = 0;
    unsigned char md[EVP_MAX_MD_SIZE];
    HMAC(EVP_sha256(),
         reinterpret_cast<const unsigned char*>(cfg_.api_secret.data()),
         (int)cfg_.api_secret.size(),
         reinterpret_cast<const unsigned char*>(query.data()),
         (int)query.size(),
         md, &len);
    std::ostringstream oss;
    for (unsigned int i=0;i<len;++i) oss<< std::hex << std::setw(2) << std::setfill('0') << (int)md[i];
    return oss.str();
}

json BinanceRest::http_get(const std::string& path, const std::string& query, bool signed_req){
    std::string url = rest_base()+path;
    std::string q = query;
    if (signed_req){
        if (!q.empty()) q.push_back('&');
        q += "timestamp="+std::to_string(now_ms());
        auto sig = sign_query(q);
        q += "&signature="+sig;
    }
    cpr::Header hdr = {{"X-MBX-APIKEY", cfg_.api_key}};
    auto req = cpr::Get(cpr::Url{url}, cpr::Parameters{}, cpr::Header{signed_req?hdr: cpr::Header{}}, cpr::Parameters{{}}, cpr::Timeout{cfg_.timeout_ms}, cpr::Parameters{}, cpr::Parameters{}, cpr::Body{}, cpr::Proxies{}, cpr::VerifySsl{true});
    // cpr nem a legszebb itt; egyszerűbb:
    cpr::Response r;
if (!q.empty())
    r = cpr::Get(cpr::Url{url + "?" + q},
                 cpr::Header{signed_req ? hdr : cpr::Header{}},
                 cpr::Timeout{cfg_.timeout_ms},
                 cpr::VerifySsl{true});
    
    else
    r = cpr::Get(cpr::Url{url},
                 cpr::Header{signed_req ? hdr : cpr::Header{}},
                 cpr::Timeout{cfg_.timeout_ms},
                 cpr::VerifySsl{true});
    if (r.status_code>=400){ spdlog::warn("GET {} : {} {}", path, r.status_code, r.text); }
    try{ return json::parse(r.text.empty()?"{}":r.text); } catch(...){ return json::object(); }
}

json BinanceRest::http_post(const std::string& path, const std::string& body_or_query, bool signed_req, bool body_is_payload){
    std::string url = rest_base()+path;
    cpr::Header hdr = {{"X-MBX-APIKEY", cfg_.api_key}, {"Content-Type","application/x-www-form-urlencoded"}};

    std::string q = body_or_query;
    if (signed_req){
        if (!q.empty() && q.back()!='&') q.push_back('&');
        q += "timestamp="+std::to_string(now_ms());
        auto sig = sign_query(q);
        q += "&signature="+sig;
    }
    cpr::Response r;
    if (body_is_payload) {
    r = cpr::Post(cpr::Url{url},
                  cpr::Header{hdr},
                  cpr::Body{q},
                  cpr::Timeout{cfg_.timeout_ms},
                  cpr::VerifySsl{true});
    } else {
    r = cpr::Post(cpr::Url{url + "?" + q},
                  cpr::Header{hdr},
                  cpr::Timeout{cfg_.timeout_ms},
                  cpr::VerifySsl{true});
    }
    if (r.status_code>=400){ spdlog::warn("POST {} : {} {}", path, r.status_code, r.text); }
    try{ return json::parse(r.text.empty()?"{}":r.text); } catch(...){ return json::object(); }
}

json BinanceRest::http_delete(const std::string& path, const std::string& query, bool signed_req){
    std::string url = rest_base()+path;
    cpr::Header hdr = {{"X-MBX-APIKEY", cfg_.api_key}};
    std::string q = query;
    if (signed_req){
        if (!q.empty()) q.push_back('&');
        q += "timestamp="+std::to_string(now_ms());
        auto sig = sign_query(q);
        q += "&signature="+sig;
    }
    cpr::Response r = cpr::Delete(cpr::Url{url + "?" + q},
                              cpr::Header{hdr},
                              cpr::Timeout{cfg_.timeout_ms},
                              cpr::VerifySsl{true});
    if (r.status_code>=400){ spdlog::warn("DELETE {} : {} {}", path, r.status_code, r.text); }
    try{ return json::parse(r.text.empty()?"{}":r.text); } catch(...){ return json::object(); }
}

std::string BinanceRest::ping(){
    auto j = http_get("/api/v3/ping");
    return j.empty()? "pong (empty)" : "pong ok";
}

MarketResult BinanceRest::market_buy(const std::string& symbol, double quote_amount){
    // POST /api/v3/order (HMAC)  type=MARKET, side=BUY, quoteOrderQty=...
    std::ostringstream q;
    q << "symbol=" << symbol
      << "&side=BUY&type=MARKET"
      << "&quoteOrderQty=" << std::fixed << std::setprecision(2) << quote_amount
      << "&recvWindow=5000";
    auto j = http_post("/api/v3/order", q.str(), true, false);

    MarketResult out;
    if (j.contains("orderId")) out.info.orderId = j["orderId"].get<uint64_t>();
    out.msg = j.dump();
    // Binance azonnali töltésnél is "fills" lista/ vagy executedQty van
    out.filled_base = to_d(j, "executedQty");
    return out;
}

MarketResult BinanceRest::market_sell(const std::string& symbol, double quote_amount){
    // SELL by quote amt: először lekérjük az árat és bázis mennyiséget becsüljük
    // Egyszerűsítés: hagyjuk a quoteOrderQty-t SELL-re is (Binance engedi is)
    std::ostringstream q;
    q << "symbol=" << symbol
      << "&side=SELL&type=MARKET"
      << "&quoteOrderQty=" << std::fixed << std::setprecision(2) << quote_amount
      << "&recvWindow=5000";
    auto j = http_post("/api/v3/order", q.str(), true, false);

    MarketResult out;
    if (j.contains("orderId")) out.info.orderId = j["orderId"].get<uint64_t>();
    out.msg = j.dump();
    out.filled_base = to_d(j, "executedQty");
    return out;
}

OcoResult BinanceRest::oco_sell_bracket(const std::string& symbol, double base_qty,
                                        double tp_price, double sl_price, double sl_limit_price){
    // POST /api/v3/order/oco
    // params: symbol, side=SELL, quantity, price (TP), stopPrice, stopLimitPrice, stopLimitTimeInForce=GTC
    std::ostringstream q;
    q << "symbol="<<symbol
      << "&side=SELL"
      << "&quantity=" << std::fixed << std::setprecision(8) << base_qty
      << "&price=" << std::fixed << std::setprecision(2) << tp_price
      << "&stopPrice=" << std::fixed << std::setprecision(2) << sl_price
      << "&stopLimitPrice=" << std::fixed << std::setprecision(2) << sl_limit_price
      << "&stopLimitTimeInForce=GTC&recvWindow=5000";

    auto j = http_post("/api/v3/order/oco", q.str(), true, false);
    OcoResult out; out.msg = j.dump();
    try{
        if (j.contains("orders") && j["orders"].is_array()){
            for (auto& o : j["orders"]){
                if (o.contains("orderId")) out.extra_order_ids.push_back(o["orderId"].get<uint64_t>());
            }
        }
    }catch(...){}
    return out;
}

std::vector<OrderInfo> BinanceRest::open_orders(const std::string& symbol){
    auto j = http_get("/api/v3/openOrders", "symbol="+symbol+"&recvWindow=5000", true);
    std::vector<OrderInfo> v;
    if (!j.is_array()) return v;
    for (auto& o : j){
        OrderInfo i;
        i.orderId = o.value("orderId", 0ULL);
        i.symbol  = o.value("symbol", std::string{});
        i.side    = o.value("side", std::string{});
        i.type    = o.value("type", std::string{});
        i.status  = parse_status(o.value("status", std::string{}));
        i.price   = to_d(o, "price");
        i.origQty = to_d(o, "origQty");
        i.executedQty = to_d(o, "executedQty");
        v.push_back(i);
    }
    return v;
}

std::optional<OrderInfo> BinanceRest::get_order(const std::string& symbol, uint64_t orderId){
    std::ostringstream q; q<<"symbol="<<symbol<<"&orderId="<<orderId<<"&recvWindow=5000";
    auto j = http_get("/api/v3/order", q.str(), true);
    if (!j.contains("orderId")) return std::nullopt;
    OrderInfo i;
    i.orderId = j.value("orderId", 0ULL);
    i.symbol  = j.value("symbol", std::string{});
    i.side    = j.value("side", std::string{});
    i.type    = j.value("type", std::string{});
    i.status  = parse_status(j.value("status", std::string{}));
    i.price   = to_d(j, "price");
    i.origQty = to_d(j, "origQty");
    i.executedQty = to_d(j, "executedQty");
    return i;
}

bool BinanceRest::cancel_order(const std::string& symbol, uint64_t orderId, std::string* out_msg){
    std::ostringstream q; q<<"symbol="<<symbol<<"&orderId="<<orderId<<"&recvWindow=5000";
    auto j = http_delete("/api/v3/order", q.str(), true);
    if (out_msg) *out_msg = j.dump();
    return j.contains("symbol"); // ha visszajött a törölt order
}

bool BinanceRest::cancel_all_open_orders(const std::string& symbol, std::string* out_msg){
    std::ostringstream q; q<<"symbol="<<symbol<<"&recvWindow=5000";
    auto j = http_delete("/api/v3/openOrders", q.str(), true);
    if (out_msg) *out_msg = j.dump();
    // Ha open order nem volt, is jöhet üzenet — tekintsük sikeresnek
    return true;
}

} // namespace exec
