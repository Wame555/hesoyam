#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <vector>
#include <ixwebsocket/IXWebSocket.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

// --- Execution report (fill/részfill)
struct ExecUpdate {
    uint64_t orderId{0};
    std::string symbol;
    std::string side;   // BUY/SELL
    std::string status; // NEW, PARTIALLY_FILLED, FILLED, CANCELED, REJECTED, EXPIRED, ...
    double lastQty{0.0};    // l
    double lastPrice{0.0};  // L
    double cumQty{0.0};     // z
};

// --- Teljes számla snapshot mező
struct Balance {
    std::string asset;
    double free{0.0};
    double locked{0.0};
};

// --- OCO lista státusz (hasznos naplózni)
struct ListStatus {
    std::string symbol;
    std::string listClientOrderId;
    std::string contingencyType; // OCO
    std::string listStatusType;
    std::string listOrderStatus;
};

class BinanceUserStream {
public:
    using ExecCB         = std::function<void(const ExecUpdate&)>;
    using BalancesCB     = std::function<void(const std::vector<Balance>&)>;
    using BalanceDeltaCB = std::function<void(const std::string& asset, double delta, uint64_t eventTime)>;
    using ListStatusCB   = std::function<void(const ListStatus&)>;

    BinanceUserStream(std::string api_key, bool testnet);
    ~BinanceUserStream();

    bool start();   // POST listenKey + WS connect + keepalive
    void stop();    // WS stop + DELETE listenKey
    bool running() const { return running_.load(); }

    // Callbacks
    void set_on_exec(ExecCB cb)                 { on_exec_ = std::move(cb); }
    void set_on_balances(BalancesCB cb)         { on_balances_ = std::move(cb); }
    void set_on_balance_delta(BalanceDeltaCB cb){ on_balance_delta_ = std::move(cb); }
    void set_on_list_status(ListStatusCB cb)    { on_list_status_ = std::move(cb); }

private:
    std::string rest_base() const { return testnet_? "https://testnet.binance.vision" : "https://api.binance.com"; }
    std::string ws_base()   const { return testnet_? "wss://testnet.binance.vision/ws" : "wss://stream.binance.com:9443/ws"; }

    bool create_listen_key();
    void keepalive_loop();   // 30 percenként PUT
    void connect_ws();       // WS beállítás + start
    void handle_msg(const std::string& s); // JSON parse -> callbackek

    std::string api_key_;
    bool testnet_{};
    std::string listen_key_;

    ix::WebSocket ws_;
    std::atomic<bool> running_{false};
    std::thread keepalive_thr_;

    ExecCB         on_exec_;
    BalancesCB     on_balances_;
    BalanceDeltaCB on_balance_delta_;
    ListStatusCB   on_list_status_;
};
