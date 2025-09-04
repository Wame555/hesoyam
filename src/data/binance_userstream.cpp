#include "data/binance_userstream.hpp"
#include <chrono>

using json = nlohmann::json;

namespace data {

static inline uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

BinanceUserStream::BinanceUserStream(std::string api_key, bool testnet)
    : api_key_(std::move(api_key)), testnet_(testnet) {}

BinanceUserStream::~BinanceUserStream() {
    stop();
}

void BinanceUserStream::set_on_exec(ExecCB cb) {
    std::lock_guard<std::mutex> lk(mtx_);
    on_exec_ = std::move(cb);
}

std::string BinanceUserStream::rest_base() const {
    return testnet_ ? "https://testnet.binance.vision" : "https://api.binance.com";
}

bool BinanceUserStream::create_listen_key(std::string& out_key) {
    try {
        const std::string url = rest_base() + "/api/v3/userDataStream";
        cpr::Response r = cpr::Post(
            cpr::Url{url},
            cpr::Header{{"X-MBX-APIKEY", api_key_}},
            cpr::Timeout{5000},
            cpr::VerifySsl{true}
        );
        if (r.status_code >= 300) {
            spdlog::error("userDataStream create failed: {} {}", r.status_code, r.text);
            return false;
        }
        json j = json::parse(r.text.empty() ? "{}" : r.text);
        if (j.contains("listenKey")) {
            out_key = j["listenKey"].get<std::string>();
            return true;
        }
    } catch (const std::exception& e) {
        spdlog::error("create_listen_key ex: {}", e.what());
    }
    return false;
}

void BinanceUserStream::keepalive_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::minutes(25));
        if (!running_.load()) break;

        std::string lk;
        {
            std::lock_guard<std::mutex> lk_m(mtx_);
            lk = listen_key_;
        }
        if (lk.empty()) continue;

        try {
            const std::string url = rest_base() + "/api/v3/userDataStream";
            cpr::Response r = cpr::Put(
                cpr::Url{url},
                cpr::Header{{"X-MBX-APIKEY", api_key_}},
                cpr::Body{"listenKey=" + lk},
                cpr::Timeout{5000},
                cpr::VerifySsl{true}
            );
            if (r.status_code >= 300) {
                spdlog::warn("userDataStream keepalive {} {}", r.status_code, r.text);
            }
        } catch (const std::exception& e) {
            spdlog::warn("keepalive ex: {}", e.what());
        }
    }
}

void BinanceUserStream::connect_ws(const std::string& listen_key) {
    // Binance SPOT user-data: wss://stream.binance.com:9443/ws/<listenKey>
    const std::string url = std::string("wss://stream.binance.com:9443/ws/") + listen_key;

    ws_ = std::make_unique<ix::WebSocket>();
    ws_->setUrl(url);
    ws_->disablePerMessageDeflate();

    ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        using ix::WebSocketMessageType;
        if (msg->type == WebSocketMessageType::Message) {
            try {
                json j = json::parse(msg->str);

                // Végrehajtási jelentés
                if (j.contains("e") && j["e"] == "executionReport") {
                    ExecUpdate u;
                    u.symbol = j.value("s", "");
                    u.side   = j.value("S", "");
                    try { u.lastQty   = std::stod(j.value("l", std::string("0"))); } catch (...) {}
                    try { u.lastPrice = std::stod(j.value("L", std::string("0"))); } catch (...) {}

                    ExecCB cb;
                    {
                        std::lock_guard<std::mutex> lk(mtx_);
                        cb = on_exec_;
                    }
                    if (cb) cb(u);
                }

                // További eventek: OUTBOUND_ACCOUNT_POSITION, balanceUpdate, stb.

            } catch (const std::exception& e) {
                spdlog::warn("userstream parse err: {}", e.what());
            }
        } else if (msg->type == WebSocketMessageType::Open) {
            spdlog::info("UserStream WS open");
        } else if (msg->type == WebSocketMessageType::Close) {
            spdlog::warn("UserStream WS closed code={} reason={}", msg->closeInfo.code, msg->closeInfo.reason);
            if (running_.load()) {
                // egyszerű reconnect
                try { ws_->start(); } catch (...) {}
            }
        } else if (msg->type == WebSocketMessageType::Error) {
            spdlog::error("UserStream WS error: {}", msg->errorInfo.reason);
        }
    });

    ws_->start();
}

bool BinanceUserStream::start() {
    if (running_.load()) return true;
    running_.store(true);

    std::string lk;
    if (!create_listen_key(lk)) {
        running_.store(false);
        return false;
    }

    {
        std::lock_guard<std::mutex> lk_m(mtx_);
        listen_key_ = lk;
    }

    connect_ws(lk);
    keepalive_thread_ = std::thread(&BinanceUserStream::keepalive_loop, this);
    return true;
}

void BinanceUserStream::stop() {
    if (!running_.load()) return;
    running_.store(false);

    try {
        if (ws_) {
            ws_->stop();
            ws_.reset();
        }
    } catch (...) {}

    if (keepalive_thread_.joinable()) keepalive_thread_.join();
}

} // namespace data
