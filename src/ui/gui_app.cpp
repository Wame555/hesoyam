// gui_app.cpp from canvas (full)
#include "ui/gui_app.hpp"
#include <SFML/Graphics.hpp>
#include <imgui.h>
#include <imgui-SFML.h>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <atomic>
#include <algorithm>
#include "core/module.hpp"   // core::IModule teljes definíciója

#include "indicators/rsi.hpp"
#include "indicators/sma_ema.hpp"
#include "indicators/bollinger.hpp"
#include "indicators/mtfa.hpp"
#include "data/binance_ws.hpp"
#include "data/binance_userstream.hpp"
#include "util/concurrent_queue.hpp"
#include "telemetry/telegram_notifier.hpp"
#include "exec/binance_rest.hpp"
#include "exec/risk.hpp"
#include "exec/position_tracker.hpp"
#include "sim/demo_account.hpp"

// --- ensure IModule complete type is visible here
#if __has_include("core/module.hpp")
  #include "core/module.hpp"
#elif __has_include("core/imodule.hpp")
  #include "core/imodule.hpp"
#elif __has_include("module.hpp")
  #include "module.hpp"
#else
  #error "Nem találom az IModule definíció headerét. Add meg a helyes include-ot."
#endif

using core::IModule;
using Clock = std::chrono::steady_clock;

namespace ui {

static const char* tf_to_str(Timeframe tf){
    switch(tf){
        case Timeframe::M1: return "1m"; case Timeframe::M3: return "3m"; case Timeframe::M5: return "5m";
        case Timeframe::M15: return "15m"; case Timeframe::M30: return "30m"; case Timeframe::H1: return "1h";
        case Timeframe::H4: return "4h"; case Timeframe::D1: return "1d"; default: return "1m";
    }
}
static Timeframe idx_to_tf(int idx){
    Timeframe map[8] = {Timeframe::M1,Timeframe::M3,Timeframe::M5,Timeframe::M15,Timeframe::M30,Timeframe::H1,Timeframe::H4,Timeframe::D1};
    return map[std::clamp(idx,0,7)];
}

struct GuiApp::Impl {
    sf::RenderWindow window{sf::VideoMode(1280, 900), "Crypto Modular Bot (GUI)"};

    // Config
    GuiConfig cfg{};
    Symbol sym{"BTC","USDT"};
    char symbol_buf[32] = "BTCUSDT";
    int tf_idx = 2; // M5
    const char* tf_labels[8] = {"M1","M3","M5","M15","M30","H1","H4","D1"};

    // Modules & decision
    std::vector<std::unique_ptr<IModule>> modules;
    Weights weights; Scores scores;
    double combined{50.0};
    Signal last_action{Signal::Neutral};

    // Market data (kline + last price)
    std::unique_ptr<BinanceWsClient> ws;
    util::ConcurrentQueue<Bar> bar_q;
    std::atomic<double> last_price{0.0};

    // Paper/demo
    sim::DemoAccount account{10000.0};
    double order_qty{100.0};
    double default_sl_pct{1.5};
    double default_tp_pct{2.0};
    bool allow_short_demo{false};

    // Telegram
    char tg_token[128] = "";
    char tg_chat[64] = "";

    // Live trading (REST)
    bool live_enabled{false};
    bool testnet{true};
    char api_key[96] = "";
    char api_secret[96] = "";
    std::unique_ptr<exec::BinanceRest> spot;
    exec::RiskManager risk{2.0};
    std::string last_exec_msg;

    // user-data stream
    std::unique_ptr<data::BinanceUserStream> uds;  // <<< data::
    bool uds_connected{false};

    // OCO bracket
    bool attach_bracket{true};
    double live_sl_pct{1.5};
    double live_tp_pct{2.0};

    // Order tracking & log
    struct TrackedOrder { uint64_t id{0}; std::string side; double last_exec{0.0}; };
    std::vector<TrackedOrder> tracked;
    std::vector<exec::OrderLogEntry> log;

    // Polling
    double poll_sec{2.0};
    Clock::time_point last_poll{Clock::now()};
    std::vector<exec::OrderInfo> open_orders_cache;
    exec::PositionTracker pos_tracker;

    // Balances panel
    std::unordered_map<std::string, std::pair<double,double>> balances; // asset -> {free, locked}
    std::vector<std::string> bal_log; // delta sorok
    bool hide_dust{true};
    double dust_threshold{1e-6};

    // Backtest
    char bt_path[256] = "";
    bool bt_use_mtf{false}; int bt_mtf_factor{12};
    std::vector<float> bt_equity; float bt_min=0, bt_max=0; int bt_trades=0; float bt_maxdd=0; bool bt_has=false;
    struct WeightRow { float w1,w2,w3,final_eq,pf,winrate; };
    std::vector<WeightRow> grid_top;

    Impl(){ window.setFramerateLimit(60); }
};

GuiApp::GuiApp(): self(std::make_unique<Impl>()){
    ImGui::SFML::Init(self->window);
}
GuiApp::~GuiApp(){
    if (self->ws) self->ws->stop();
    if (self->uds && self->uds_connected) self->uds->stop();
    ImGui::SFML::Shutdown();
}

void GuiApp::add_module(std::unique_ptr<IModule> m, double weight){
    self->weights.w[m->id()] = weight;
    self->modules.emplace_back(std::move(m));
}

void GuiApp::run(){
    sf::Clock delta;

    auto start_ws = [this]{
        std::string sym = self->symbol_buf; std::string sym_l = sym;
        std::transform(sym_l.begin(), sym_l.end(), sym_l.begin(), ::tolower);
        std::string interval = tf_to_str(self->cfg.tf);
        self->ws = std::make_unique<BinanceWsClient>(sym_l, interval);
        self->ws->set_on_kline([this](const Bar& b, bool is_final){ if (is_final) self->bar_q.push(b); });
        self->ws->set_on_price([this](double p){ self->last_price.store(p); });
        self->ws->start();
    };
    self->cfg.tf = idx_to_tf(self->tf_idx);
    start_ws();

    bool running=true;
    while (running){
        sf::Event ev{}; 
        while (self->window.pollEvent(ev)){
            ImGui::SFML::ProcessEvent(self->window, ev);
            if (ev.type==sf::Event::Closed) running=false;
        }
        ImGui::SFML::Update(self->window, delta.restart());

        // --- Bar feldolgozás
        Bar bar;
        while (self->bar_q.try_pop(bar)){
            for (auto& m : self->modules){
                auto r = m->on_bar(self->sym, self->cfg.tf, bar);
                self->scores.s[m->id()] = r.score;
            }
            auto d = decide(self->scores, self->weights, self->cfg.thr_long, self->cfg.thr_short);
            self->combined = d.combined_score; self->last_action = d.action;

            // demo auto trade (opcionális)
            if (self->cfg.auto_trade){
                if (d.action==Signal::Long){
                    self->account.open_long(self->order_qty, bar.close, self->default_sl_pct, self->default_tp_pct);
                } else if (d.action==Signal::Short && self->allow_short_demo){
                    self->account.open_short(self->order_qty, bar.close, self->default_sl_pct, self->default_tp_pct);
                }
            }
            self->account.on_price(bar.close);
        }

        // --- LIVE: open order polling (részfill követéshez)
        if (self->live_enabled && self->spot){
            auto now = Clock::now();
            if (std::chrono::duration<double>(now - self->last_poll).count() > self->poll_sec){
                self->last_poll = now;
                self->open_orders_cache = self->spot->open_orders(self->symbol_buf);
                for (size_t i=0;i<self->tracked.size();){
                    auto& to = self->tracked[i];
                    auto oi = self->spot->get_order(self->symbol_buf, to.id);
                    if (oi){
                        double newExec = oi->executedQty;
                        double delta_qty = std::max(0.0, newExec - to.last_exec);
                        if (delta_qty>0){
                            if (to.side=="BUY") self->pos_tracker.on_fill_buy(self->symbol_buf, delta_qty, self->last_price.load());
                            else                 self->pos_tracker.on_fill_sell(self->symbol_buf, delta_qty, self->last_price.load());
                            to.last_exec = newExec;
                        }
                        using S=exec::OrderStatus;
                        if (oi->status==S::Filled || oi->status==S::Canceled || oi->status==S::Rejected || oi->status==S::Expired){
                            self->tracked.erase(self->tracked.begin()+i);
                            continue;
                        }
                    }
                    ++i;
                }
            }
        }

        // --- UI: Overview
        if (ImGui::Begin("Overview")){
            ImGui::InputText("Symbol", self->symbol_buf, IM_ARRAYSIZE(self->symbol_buf));
            ImGui::Combo("Timeframe", &self->tf_idx, self->tf_labels, 8);
            ImGui::Checkbox("Auto trade (demo)", &self->cfg.auto_trade);
            ImGui::SameLine(); ImGui::Checkbox("Allow SHORT (paper)", &self->allow_short_demo);
            ImGui::SliderFloat("LONG threshold", &self->cfg.thr_long, 50.f, 90.f, "%.0f");
            ImGui::SliderFloat("SHORT threshold", &self->cfg.thr_short, 10.f, 50.f, "%.0f");
            ImGui::Separator();
            ImGui::Text("Last price: %.2f", self->last_price.load());
            ImGui::Text("Combined score: %.1f", self->combined);
            ImGui::Text("Decision: %s", to_string(self->last_action));
            if (ImGui::Button("Apply symbol/TF & Reconnect WS")){
                if (self->ws) self->ws->stop();
                self->cfg.tf = idx_to_tf(self->tf_idx);
                // base/quote frissítés (durva): utolsó 4 char USDT feltételezés
                std::string s = self->symbol_buf;
                if (s.size()>=6){ self->sym.base = s.substr(0, s.size()-4); self->sym.quote = s.substr(s.size()-4); }
                start_ws();
            }
        }
        ImGui::End();

        // --- UI: Modules
        if (ImGui::Begin("Modules")){
            if (ImGui::BeginTable("modtbl", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)){
                ImGui::TableSetupColumn("Module"); ImGui::TableSetupColumn("Weight"); ImGui::TableSetupColumn("Score");
                ImGui::TableHeadersRow();
                for (auto& [id, w] : self->weights.w){
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(id.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", w);
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", self->scores.s[id]);
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();

        // --- UI: Demo Account
        if (ImGui::Begin("Demo Account")){
            ImGui::Text("Balance: %.2f USDT", self->account.balance());
            ImGui::InputDouble("Order size (USDT)", &self->order_qty, 50.0, 500.0, "%.0f");
            ImGui::InputDouble("Default SL %", &self->default_sl_pct, 0.1, 1.0, "%.2f");
            ImGui::InputDouble("Default TP %", &self->default_tp_pct, 0.1, 1.0, "%.2f");
            if (ImGui::Button("Open LONG")) self->account.open_long(self->order_qty, self->last_price.load(), self->default_sl_pct, self->default_tp_pct);
            ImGui::SameLine();
            if (ImGui::Button("Open SHORT")) self->account.open_short(self->order_qty, self->last_price.load(), self->default_sl_pct, self->default_tp_pct);
            ImGui::SameLine();
            if (ImGui::Button("Close ALL")) self->account.close_all(self->last_price.load());

            if (ImGui::BeginTable("postbl", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)){
                ImGui::TableSetupColumn("ID"); ImGui::TableSetupColumn("Side"); ImGui::TableSetupColumn("Qty USDT"); ImGui::TableSetupColumn("Entry"); ImGui::TableSetupColumn("SL"); ImGui::TableSetupColumn("TP"); ImGui::TableSetupColumn("PnL"); ImGui::TableSetupColumn("Action");
                ImGui::TableHeadersRow();
                for (auto& p : self->account.positions()){
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%llu", (unsigned long long)p.id);
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%s", p.short_side? "SHORT" : "LONG");
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%.0f", p.qty_usdt);
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", p.entry);
                    ImGui::TableSetColumnIndex(4); ImGui::Text("%.2f", p.sl);
                    ImGui::TableSetColumnIndex(5); ImGui::Text("%.2f", p.tp);
                    ImGui::TableSetColumnIndex(6); ImGui::Text("%.2f", self->account.unrealized_pnl(p, self->last_price.load()));
                    ImGui::TableSetColumnIndex(7);
                    if (ImGui::SmallButton((std::string("Close##")+std::to_string(p.id)).c_str())) self->account.close(p.id, self->last_price.load());
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();

        // --- UI: Telegram
        if (ImGui::Begin("Telegram")){
            ImGui::InputText("Bot token", self->tg_token, IM_ARRAYSIZE(self->tg_token));
            ImGui::InputText("Chat ID", self->tg_chat, IM_ARRAYSIZE(self->tg_chat));
            if (ImGui::Button("Init")) telegram::init(self->tg_token, self->tg_chat);
            ImGui::SameLine(); if (ImGui::Button("Test message")) telegram::send("Crypto Modular Bot – hello from GUI");
        }
        ImGui::End();

        // --- UI: Live Trading (Spot)
        if (ImGui::Begin("Live Trading (Spot)")){
            ImGui::Checkbox("Enable LIVE (spot)", &self->live_enabled);
            ImGui::SameLine(); ImGui::Checkbox("Testnet", &self->testnet);
            ImGui::InputText("API Key", self->api_key, IM_ARRAYSIZE(self->api_key));
            ImGui::InputText("API Secret", self->api_secret, IM_ARRAYSIZE(self->api_secret));
            if (ImGui::Button("Connect/Init")){
                exec::ApiConfig cfg{self->api_key, self->api_secret, self->testnet, 5000};
                self->spot = std::make_unique<exec::BinanceRest>(cfg);
                self->last_exec_msg = self->spot->ping();
                // user-data stream
                self->uds = std::make_unique<data::BinanceUserStream>(self->api_key, self->testnet);  // <<< data::
                self->uds->set_on_exec([this](const data::ExecUpdate& u){  // <<< data::
                if (u.lastQty > 0) {
                if (u.side == "BUY")
                self->pos_tracker.on_fill_buy(u.symbol, u.lastQty, u.lastPrice);
                else
                self->pos_tracker.on_fill_sell(u.symbol, u.lastQty, u.lastPrice);
                }
                });
                self->uds->set_on_balances([this](const std::vector<data::Balance>& v){
                for (auto& b : v){ self->balances[b.asset] = {b.free, b.locked}; }
                });
                self->uds->set_on_balance_delta([this](const std::string& a, double d, uint64_t E){
                    char buf[160]; std::snprintf(buf, sizeof(buf), "%s delta=%.8f @%llu", a.c_str(), d, (unsigned long long)E);
                    self->bal_log.emplace_back(buf);
                });
                self->uds->set_on_list_status([this](const data::ListStatus& ls){
    self->log.push_back({
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(),
        std::string("OCO listStatus: ")+ls.listOrderStatus+" "+ls.listStatusType+" sym="+ls.symbol
    });
});
            }
            ImGui::TextWrapped("%s", self->last_exec_msg.c_str());
            ImGui::Separator();
            if (!self->uds_connected) {
                if (ImGui::Button("Start user-data stream") && self->uds)
                self->uds_connected = self->uds->start();  // <<< most már oké, teljes típus ismert
                } else {
                ImGui::SameLine();
                if (ImGui::Button("Stop user-data stream")) {
                    if (self->uds) { self->uds->stop(); self->uds_connected = false; }
                }
            }
            ImGui::Checkbox("Attach OCO bracket after BUY", &self->attach_bracket);
            ImGui::InputDouble("LIVE SL %", &self->live_sl_pct, 0.1, 1.0, "%.2f");
            ImGui::SameLine(); ImGui::InputDouble("LIVE TP %", &self->live_tp_pct, 0.1, 1.0, "%.2f");
            ImGui::Text("Risk: daily loss limit = %.2f %%", self->risk.max_daily_loss_pct());
            if (ImGui::Button("Reset risk day")) self->risk.force_reset_day();
            ImGui::Separator();
            if (self->live_enabled && self->spot){
                if (!self->risk.allow_trade()){
                    ImGui::TextColored(ImVec4(1,0.6f,0,1), "Trading paused: daily loss limit reached");
                }
                if (ImGui::Button("Market BUY (qty USDT)")){
                    if (self->risk.allow_trade()){
                        auto r = self->spot->market_buy(self->symbol_buf, self->order_qty);
                        self->last_exec_msg = r.msg;
                        self->log.push_back({(uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(), "BUY: "+r.msg});
                        if (r.filled_base>0) self->pos_tracker.on_fill_buy(self->symbol_buf, r.filled_base, self->last_price.load());
                        if (r.info.orderId) self->tracked.push_back({r.info.orderId, "BUY", 0.0});
                        if (self->attach_bracket && r.filled_base>0){
                            double entry = self->last_price.load();
                            double tp = entry * (1.0 + self->live_tp_pct/100.0);
                            double sl = entry * (1.0 - self->live_sl_pct/100.0);
                            double sl_limit = sl * 0.999;
                            auto oco = self->spot->oco_sell_bracket(self->symbol_buf, r.filled_base, tp, sl, sl_limit);
                            self->last_exec_msg = oco.msg;
                            self->log.push_back({(uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(), "OCO SELL: "+oco.msg});
                            for (auto id : oco.extra_order_ids) self->tracked.push_back({id, "SELL", 0.0});
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Market SELL (qty USDT)")){
                    if (self->risk.allow_trade()){
                        auto r = self->spot->market_sell(self->symbol_buf, self->order_qty);
                        self->last_exec_msg = r.msg;
                        self->log.push_back({(uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(), "SELL: "+r.msg});
                        if (r.filled_base>0) self->pos_tracker.on_fill_sell(self->symbol_buf, r.filled_base, self->last_price.load());
                        if (r.info.orderId) self->tracked.push_back({r.info.orderId, "SELL", 0.0});
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Close net position (MARKET)")){
                    auto np = self->pos_tracker.get(self->symbol_buf);
                    if (np.base_qty>0 && self->risk.allow_trade()){
                        double quote_amt = np.base_qty * self->last_price.load();
                        auto r = self->spot->market_sell(self->symbol_buf, quote_amt);
                        self->last_exec_msg = r.msg;
                        self->log.push_back({(uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(), "CLOSE: "+r.msg});
                        if (r.filled_base>0) self->pos_tracker.on_fill_sell(self->symbol_buf, r.filled_base, self->last_price.load());
                        if (r.info.orderId) self->tracked.push_back({r.info.orderId, "SELL", 0.0});
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Smart CLOSE (cancel OCO + market)")){
                    if (self->spot){
                        std::string msg; self->spot->cancel_all_open_orders(self->symbol_buf, &msg);
                        self->log.push_back({(uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(), std::string("CANCEL ALL: ")+msg});
                    }
                    auto np = self->pos_tracker.get(self->symbol_buf);
                    if (np.base_qty>0 && self->risk.allow_trade()){
                        double quote_amt = np.base_qty * self->last_price.load();
                        auto r = self->spot->market_sell(self->symbol_buf, quote_amt);
                        self->last_exec_msg = r.msg;
                        self->log.push_back({(uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(), "SMART CLOSE: "+r.msg});
                        if (r.filled_base>0) self->pos_tracker.on_fill_sell(self->symbol_buf, r.filled_base, self->last_price.load());
                    }
                }
            }
            if (ImGui::BeginTable("orderlog", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)){
                ImGui::TableSetupColumn("Time"); ImGui::TableSetupColumn("Message"); ImGui::TableHeadersRow();
                for (int i=(int)self->log.size()-1;i>=0 && i>(int)self->log.size()-200; --i){
                    auto& e = self->log[i]; ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%llu", (unsigned long long)e.ts_ms);
                    ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", e.msg.c_str());
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();

        // --- UI: Orders & Positions
        if (ImGui::Begin("Orders & Positions")){
            auto np = self->pos_tracker.get(self->symbol_buf);
            ImGui::Text("Net position (%s): qty=%.6f avg=%.2f | Unrealized≈ %.2f  (last=%.2f)",
                        self->symbol_buf, np.base_qty, np.avg_entry,
                        (self->last_price.load()-np.avg_entry)*np.base_qty, self->last_price.load());
            if (ImGui::BeginTable("orders", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)){
                ImGui::TableSetupColumn("ID"); ImGui::TableSetupColumn("Side"); ImGui::TableSetupColumn("Type"); ImGui::TableSetupColumn("Price"); ImGui::TableSetupColumn("OrigQty"); ImGui::TableSetupColumn("ExecQty"); ImGui::TableSetupColumn("Action");
                ImGui::TableHeadersRow();
                for (auto& o : self->open_orders_cache){
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%llu", (unsigned long long)o.orderId);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(o.side.c_str());
                    ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(o.type.c_str());
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%.6f", o.price);
                    ImGui::TableSetColumnIndex(4); ImGui::Text("%.8f", o.origQty);
                    ImGui::TableSetColumnIndex(5); ImGui::Text("%.8f", o.executedQty);
                    ImGui::TableSetColumnIndex(6);
                    std::string btn = std::string("Cancel##") + std::to_string(o.orderId);
                    if (ImGui::SmallButton(btn.c_str()) && self->spot){
                        std::string msg; bool ok = self->spot->cancel_order(self->symbol_buf, o.orderId, &msg);
                        self->last_exec_msg = msg;
                        self->log.push_back({(uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(),
                                             std::string("CANCEL ")+(ok?"OK":"ERR")+": "+msg});
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();

        // --- UI: Account (balances + delta log + érték)
        if (ImGui::Begin("Account")){
            ImGui::Checkbox("Hide dust", &self->hide_dust);
            ImGui::SameLine();
            ImGui::InputDouble("Dust threshold", &self->dust_threshold, 0.0, 0.0, "%.8f");

            // hozzávetőleges portfólió (USDT + base*price)
            double total_usdt = 0.0;
            auto it_usdt = self->balances.find("USDT");
            if (it_usdt!=self->balances.end()) total_usdt += it_usdt->second.first + it_usdt->second.second;
            std::string s = self->symbol_buf;
            std::string base = (s.size()>=4? s.substr(0, s.size()-4) : "BTC");
            auto it_base = self->balances.find(base);
            if (it_base!=self->balances.end()){
                double qty = it_base->second.first + it_base->second.second;
                total_usdt += qty * self->last_price.load();
            }
            ImGui::Text("Portfolio (approx, USDT): %.2f", total_usdt);

            if (ImGui::BeginTable("bal", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)){
                ImGui::TableSetupColumn("Asset"); ImGui::TableSetupColumn("Free"); ImGui::TableSetupColumn("Locked"); ImGui::TableSetupColumn("Value (USDT, approx)");
                ImGui::TableHeadersRow();
                for (auto& kv : self->balances){
                    double free = kv.second.first, locked = kv.second.second;
                    double amt = free + locked;
                    if (self->hide_dust && amt < self->dust_threshold) continue;
                    double val = 0.0;
                    if (kv.first=="USDT") val = amt;
                    else if (kv.first==base) val = amt * self->last_price.load();
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(kv.first.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%.8f", free);
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%.8f", locked);
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%.4f", val);
                }
                ImGui::EndTable();
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Balance updates:");
            for (int i=(int)self->bal_log.size()-1;i>=0 && i>(int)self->bal_log.size()-100; --i){
                ImGui::TextWrapped("%s", self->bal_log[i].c_str());
            }
        }
        ImGui::End();

        // --- UI: Backtest (egyszerűsített, equity plot + grid top)
        if (ImGui::Begin("Backtest")){
            ImGui::InputText("CSV path", self->bt_path, IM_ARRAYSIZE(self->bt_path));
            ImGui::Checkbox("Use MTF module", &self->bt_use_mtf); ImGui::SameLine(); ImGui::InputInt("MTF factor", &self->bt_mtf_factor);
            if (ImGui::Button("Run backtest")){
                struct Row { long long t; double o,h,l,c,v; };
                std::ifstream f(self->bt_path); std::string line; std::vector<Row> rows; rows.reserve(200000);
                if (f.good()){
                    std::getline(f,line);
                    while (std::getline(f,line)){
                        if (line.empty()) continue; std::stringstream ss(line); std::string x; Row r{};
                        std::getline(ss,x,','); r.t = std::stoll(x);
                        std::getline(ss,x,','); r.o = std::stod(x);
                        std::getline(ss,x,','); r.h = std::stod(x);
                        std::getline(ss,x,','); r.l = std::stod(x);
                        std::getline(ss,x,','); r.c = std::stod(x);
                        std::getline(ss,x,','); r.v = std::stod(x);
                        rows.push_back(r);
                    }
                    ind::SmaEmaModule m1(20,50); ind::RsiModule m2(14); ind::BollModule m3(20,2.0); ind::MtfSmaModule m4(self->bt_mtf_factor, 10, 30);
                    std::vector<IModule*> mods{ &m1, &m2, &m3 }; if (self->bt_use_mtf) mods.push_back(&m4);
                    Weights w; w.w["SMA_EMA"]=0.4; w.w["RSI"]=0.3; w.w["BOLL"]=0.3; if (self->bt_use_mtf) w.w["MTF_SMA"]=0.2; Scores s;

                    double cash=10000.0, pos=0.0, entry=0.0, fee=0.0004; double eq_peak=cash, maxdd=0.0; int trades=0;
                    self->bt_equity.clear(); self->bt_equity.reserve(rows.size());
                    Symbol sym{"BTC","USDT"};

                    for (auto& r : rows){
                        Bar b{r.t,r.o,r.h,r.l,r.c,r.v};
                        for (auto* m : mods){ auto R=m->on_bar(sym, Timeframe::M5, b); s.s[m->id()] = R.score; }
                        auto d = decide(s,w,70,30);
                        if (d.action==Signal::Long && pos<=0){
                            if (pos<0){ cash += (-pos)*(entry - b.close) - std::abs(pos)*entry*fee - std::abs(pos)*b.close*fee; pos=0; ++trades; if (cash>eq_peak) eq_peak=cash; }
                            double qty = cash * 0.2 / b.close; if (qty>0){ pos += qty; cash -= qty*b.close; cash -= qty*b.close*fee; entry=b.close; }
                        } else if (d.action==Signal::Short && pos>0){
                            cash += pos*(b.close - entry) - std::abs(pos)*entry*fee - std::abs(pos)*b.close*fee; pos=0; ++trades; if (cash>eq_peak) eq_peak=cash;
                        }
                        double eq = cash + pos*b.close;
                        self->bt_equity.push_back((float)eq);
                        if (eq>eq_peak) eq_peak=eq;
                        maxdd = std::max(maxdd, (float)((eq_peak-eq)/std::max(1.0,eq_peak)));
                    }
                    if (pos>0){ double last=rows.back().c; cash += pos*last - std::abs(pos)*entry*fee - std::abs(pos)*last*fee; pos=0; ++trades; }
                    self->bt_trades = trades; self->bt_maxdd = (float)maxdd; self->bt_has=true;
                    auto [mn,mx] = std::minmax_element(self->bt_equity.begin(), self->bt_equity.end());
                    self->bt_min = (mn!=self->bt_equity.end()? *mn : 0); self->bt_max = (mx!=self->bt_equity.end()? *mx : 0);

                    // Gyors grid a súlyokra (0.1 lépés, 3 modul)
                    self->grid_top.clear();
                    for (int i=1;i<=8;++i){
                        for (int j=1;j<=8-i;++j){
                            int k=10-i-j; if (k<=0) continue;
                            float w1=i/10.f, w2=j/10.f, w3=k/10.f;
                            Weights wg; wg.w["SMA_EMA"]=w1; wg.w["RSI"]=w2; wg.w["BOLL"]=w3; if (self->bt_use_mtf) wg.w["MTF_SMA"]=0.2f;
                            double c2=10000.0, p2=0.0, e2=0.0; int tr=0; double eqp=c2;
                            for (auto& r2 : rows){
                                Bar b2{r2.t,r2.o,r2.h,r2.l,r2.c,r2.v}; Scores ss;
                                ind::SmaEmaModule mm1(20,50); ind::RsiModule mm2(14); ind::BollModule mm3(20,2.0); ind::MtfSmaModule mm4(self->bt_mtf_factor,10,30);
                                std::vector<IModule*> mlist{&mm1,&mm2,&mm3}; if(self->bt_use_mtf) mlist.push_back(&mm4);
                                for (auto* m : mlist){ auto R=m->on_bar(sym, Timeframe::M5, b2); ss.s[m->id()] = R.score; }
                                auto d2=decide(ss,wg,70,30);
                                if (d2.action==Signal::Long && p2<=0){
                                    if (p2<0){ c2 += (-p2)*(e2 - b2.close); p2=0; ++tr; if (c2>eqp) eqp=c2; }
                                    double qty=c2*0.2/b2.close; if (qty>0){ p2+=qty; c2-=qty*b2.close; e2=b2.close; }
                                } else if (d2.action==Signal::Short && p2>0){
                                    c2 += p2*(b2.close-e2); p2=0; ++tr; if (c2>eqp) eqp=c2;
                                }
                                double eq=c2+p2*b2.close; if (eq>eqp) eqp=eq;
                            }
                            if (p2>0){ c2 += p2*rows.back().c; p2=0; ++tr; }
                            self->grid_top.push_back({w1,w2,w3,(float)c2,0.0f,0.0f});
                        }
                    }
                    std::sort(self->grid_top.begin(), self->grid_top.end(), [](auto&a,auto&b){ return a.final_eq>b.final_eq; });
                    if (self->grid_top.size()>10) self->grid_top.resize(10);
                }
            }
            if (self->bt_has){
                ImGui::Text("Trades: %d  MaxDD: %.2f%%", self->bt_trades, self->bt_maxdd*100.0f);
                ImGui::PlotLines("Equity", self->bt_equity.data(), (int)self->bt_equity.size(), 0, nullptr, self->bt_min, self->bt_max, ImVec2(-1, 160));
            }
            if (!self->grid_top.empty()){
                if (ImGui::BeginTable("grid", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)){
                    ImGui::TableSetupColumn("w(SMA)"); ImGui::TableSetupColumn("w(RSI)"); ImGui::TableSetupColumn("w(BOLL)");
                    ImGui::TableSetupColumn("Final Eq"); ImGui::TableSetupColumn("PF"); ImGui::TableSetupColumn("Winrate");
                    ImGui::TableHeadersRow();
                    for (auto& r : self->grid_top){
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::Text("%.1f", r.w1);
                        ImGui::TableSetColumnIndex(1); ImGui::Text("%.1f", r.w2);
                        ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", r.w3);
                        ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", r.final_eq);
                        ImGui::TableSetColumnIndex(4); ImGui::Text("%.2f", r.pf);
                        ImGui::TableSetColumnIndex(5); ImGui::Text("%.2f", r.winrate);
                    }
                    ImGui::EndTable();
                }
            }
        }
        ImGui::End();

        // --- Render
        self->window.clear();
        ImGui::SFML::Render(self->window);
        self->window.display();
    }
    self->window.close();
}

} // namespace ui
