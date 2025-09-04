#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>

#include "core/types.hpp"
#include "core/module.hpp"
#include "indicators/rsi.hpp"
#include "indicators/sma_ema.hpp"
#include "indicators/bollinger.hpp"
#include "indicators/mtfa.hpp"
#include "strategy/decision.hpp"

struct Row { long long t; double o,h,l,c,v; };

static bool load_csv(const std::string& path, std::vector<Row>& out) {
    std::ifstream f(path);
    if (!f.good()) return false;
    std::string line;
    // opcionális header sor
    std::getline(f, line);
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string x; Row r{};
        if (!std::getline(ss,x,',')) continue; r.t = std::stoll(x);
        if (!std::getline(ss,x,',')) continue; r.o = std::stod(x);
        if (!std::getline(ss,x,',')) continue; r.h = std::stod(x);
        if (!std::getline(ss,x,',')) continue; r.l = std::stod(x);
        if (!std::getline(ss,x,',')) continue; r.c = std::stod(x);
        if (!std::getline(ss,x,',')) continue; r.v = std::stod(x);
        out.push_back(r);
    }
    return !out.empty();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Hasznalat: backtester <csv_path> [mtf_factor]\n";
        return 1;
    }
    std::string path = argv[1];
    int mtf_factor = (argc >= 3 ? std::max(1, std::atoi(argv[2])) : 12);

    std::vector<Row> rows;
    if (!load_csv(path, rows)) {
        std::cerr << "CSV betoltes sikertelen: " << path << "\n";
        return 2;
    }

    // modulok
    ind::SmaEmaModule m1(20,50);
    ind::RsiModule    m2(14);
    ind::BollModule   m3(20,2.0);
    ind::MtfSmaModule m4(mtf_factor, 10, 30);

    std::vector<IModule*> mods{ &m1, &m2, &m3, &m4 };

    // súlyok
    Weights w;
    w.w["SMA_EMA"] = 0.4;
    w.w["RSI"]     = 0.3;
    w.w["BOLL"]    = 0.2;
    w.w["MTF_SMA"] = 0.1;

    Scores s;
    Symbol sym{"BTC","USDT"};

    double cash=10000.0, pos=0.0, entry=0.0, fee=0.0004;
    double eq_peak=cash, maxdd=0.0;
    int trades=0;

    for (const auto& r : rows) {
        Bar b{r.t, r.o, r.h, r.l, r.c, r.v};

        for (auto* m : mods) {
            auto R = m->on_bar(sym, Timeframe::M5, b);
            s.s[m->id()] = R.score;
        }

        auto d = decide(s, w, 70.0, 30.0);

        if (d.action == Signal::Long && pos <= 0.0) {
            if (pos < 0.0) { // zárjunk shortot
                cash += (-pos) * (entry - b.close);
                cash -= std::abs(pos) * entry * fee;
                cash -= std::abs(pos) * b.close * fee;
                pos = 0.0;
                ++trades;
            }
            const double qty = (cash * 0.2) / b.close;
            if (qty > 0.0) {
                pos   += qty;
                cash  -= qty * b.close;
                cash  -= qty * b.close * fee;
                entry  = b.close;
            }
        } else if (d.action == Signal::Short && pos > 0.0) {
            cash += pos * (b.close - entry);
            cash -= std::abs(pos) * entry * fee;
            cash -= std::abs(pos) * b.close * fee;
            pos = 0.0;
            ++trades;
        }

        const double eq = cash + pos * b.close;
        if (eq > eq_peak) eq_peak = eq;
        maxdd = std::max(maxdd, (eq_peak - eq) / std::max(1.0, eq_peak));
    }

    if (pos > 0.0) {
        const double last = rows.back().c;
        cash += pos * last;
        cash -= std::abs(pos) * entry * fee;
        cash -= std::abs(pos) * last * fee;
        pos = 0.0;
        ++trades;
    }

    std::cout << "Final equity: " << cash
              << " | Trades: " << trades
              << " | MaxDD: " << maxdd * 100.0 << "%\n";

    return 0;
}
// backtester.cpp implementation
