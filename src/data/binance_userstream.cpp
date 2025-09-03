#include "data/binance_userstream.hpp"
#include <cstdlib>

using json = nlohmann::json;

static double to_d(const json& j, const char* k){
    if (j.contains(k) && j[k].is_string()) return std::strtod(j[k].get_ref<const std::string&>().c_str(), nullptr);
    if (j.contains(k) && j[k].is_number()) return j[k].get<double>();
    return 0.0;
}

BinanceUserStream::BinanceUserStream(std::string api_key, bool testnet)
: api_key_(std::move(api_key)), testnet_(testnet) {}

BinanceUserStream::~BinanceUserStream(){ stop(); }

bool BinanceUserStream::create_listen_key(){
    auto url = rest_base()+"/api/v3/userDataStream";
    auto r = cpr::Post(cpr::Url{url}, cpr::Header{{"X-MBX-APIKEY", api_key_}});
    if (r.status_code>=300){
        spdlog::error("userDataStream POST {}: {}", r.status_code, r.text);
        return false;
    }
    try{
        auto j = json::parse(r.text);
        listen_key_ = j.value("listenKey", std::string{});
    }catch(...){
        spdlog::error("listenKey parse error: {}", r.text);
    }
    return !listen_key_.empty();
}

bool BinanceUserStream::start(){
    if (api_key_.empty()) { spdlog::warn("No API key for user-data stream"); return false; }
    if (!create_listen_key()) return false;
    running_.store(true);
    connect_ws();
    keepalive_thr_ = std::thread([this]{ keepalive_loop(); });
    return true;
}

void BinanceUserStream::stop(){
    running_.store(false);
    try { ws_.stop(); } catch(...) {}
    if (keepalive_thr_.joinable()) keepalive_thr_.join();
    if (!listen_key_.empty()){
        auto url = rest_base()+"/api/v3/userDataStream?listenKey="+listen_key_;
        (void)cpr::Delete(cpr::Url{url}, cpr::Header{{"X-MBX-APIKEY", api_key_}});
        listen_key_.clear();
    }
}

void BinanceUserStream::keepalive_loop(){
    // Binance: 60 perc — mi 30 percenként frissítünk
    while (running_.load()){
        std::this_thread::sleep_for(std::chrono::minutes(30));
        if (!running_.load()) break;
        if (listen_key_.empty()) continue;
        auto url = rest_base()+"/api/v3/userDataStream?listenKey="+listen_key_;
        auto r = cpr::Put(cpr::Url{url}, cpr::Header{{"X-MBX-APIKEY", api_key_}});
        if (r.status_code>=300) spdlog::warn("userStream keepalive HTTP {}: {}", r.status_code, r.text);
    }
}

void BinanceUserStream::connect_ws(){
    std::string url = ws_base()+"/"+listen_key_;
    ws_.setUrl(url);
    ws_.setPingInterval(15);
    ws_.setOnMessage([this](const ix::WebSocketMessagePtr& msg){
        using ix::WebSocketMessageType;
        switch (msg->type){
            case WebSocketMessageType::Open:
                spdlog::info("User-data WS open");
                break;
            case WebSocketMessageType::Message:
                handle_msg(msg->str);
                break;
            case WebSocketMessageType::Close:
            case WebSocketMessageType::Error:
                spdlog::warn("User-data WS closed/error: code={} reason={}", msg->closeInfo.code, msg->errorInfo.reason);
                if (running_.load()){
                    // új listenKey és reconnect
                    if (create_listen_key()) connect_ws();
                }
                break;
            default: break;
        }
    });
    ws_.start();
}

void BinanceUserStream::handle_msg(const std::string& s){
    try{
        auto j = json::parse(s);
        if (!j.contains("e")) return;
        const std::string ev = j["e"].get<std::string>();

        if (ev=="executionReport"){
            ExecUpdate u;
            u.orderId   = j.value("i", 0ULL);
            u.symbol    = j.value("s", std::string{});
            u.side      = j.value("S", std::string{});
            u.status    = j.value("X", std::string{});
            u.lastQty   = to_d(j,"l");
            u.lastPrice = to_d(j,"L");
            u.cumQty    = to_d(j,"z");
            if (on_exec_) on_exec_(u);
            return;
        }

        if (ev=="outboundAccountPosition"){
            std::vector<Balance> bs;
            if (j.contains("B") && j["B"].is_array()){
                for (auto& b : j["B"]){
                    Balance bb;
                    bb.asset  = b.value("a", std::string{});
                    bb.free   = to_d(b,"f");
                    bb.locked = to_d(b,"l");
                    bs.push_back(bb);
                }
            }
            if (on_balances_) on_balances_(bs);
            return;
        }

        if (ev=="balanceUpdate"){
            std::string asset = j.value("a", std::string{});
            double delta      = to_d(j,"d");
            uint64_t et       = j.value("E", 0ULL);
            if (on_balance_delta_) on_balance_delta_(asset, delta, et);
            return;
        }

        if (ev=="listStatus"){
            ListStatus ls;
            ls.symbol             = j.value("s", std::string{});
            ls.listClientOrderId  = j.value("c", std::string{});
            ls.contingencyType    = j.value("g", std::string{});
            ls.listStatusType     = j.value("l", std::string{});
            ls.listOrderStatus    = j.value("L", std::string{});
            if (on_list_status_) on_list_status_(ls);
            return;
        }
    }catch(const std::exception& e){
        spdlog::error("user WS parse: {}", e.what());
    }
}
