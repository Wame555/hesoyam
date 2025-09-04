#pragma once
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>

#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <ixwebsocket/IXWebSocket.h>
#include <spdlog/spdlog.h>

namespace data {

struct ExecUpdate {
    std::string symbol;
    std::string side;     // "BUY" / "SELL"
    double lastQty{0.0};
    double lastPrice{0.0};
};

class BinanceUserStream {
public:
    using ExecCB = std::function<void(const ExecUpdate&)>;

    BinanceUserStream(std::string api_key, bool testnet = true);
    ~BinanceUserStream();

    void set_on_exec(ExecCB cb);

    bool start();
    void stop();

private:
    std::string rest_base() const;
    bool create_listen_key(std::string& out_key);
    void keepalive_loop();
    void connect_ws(const std::string& listen_key);

    std::string api_key_;
    bool testnet_{true};

    std::unique_ptr<ix::WebSocket> ws_;
    std::thread keepalive_thread_;
    std::atomic<bool> running_{false};
    std::mutex mtx_;
    std::string listen_key_;
    ExecCB on_exec_;
};

} // namespace data
