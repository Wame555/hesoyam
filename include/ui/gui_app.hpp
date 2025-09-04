#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include "core/types.hpp"
#include "core/module.hpp"
#include "strategy/decision.hpp"

namespace ui {

struct GuiConfig {
    double thr_long{70.0};
    double thr_short{30.0};
    Timeframe tf{Timeframe::M5};
    bool auto_trade{false};
    bool allow_short{false};
};

class GuiApp {
public:
    GuiApp();
    ~GuiApp();

    void add_module(std::unique_ptr<IModule> m, double weight);
    void run();

private:
    struct Impl;
    std::unique_ptr<Impl> self;
};

} // namespace ui
