# Patch – User-Data Stream: Balance / Position events

**Érintett fájlok**
- `include/data/binance_userstream.hpp` – új callbackok: `set_on_balances`, `set_on_balance_delta`, `set_on_list_status`
- `src/data/binance_userstream.cpp` – `outboundAccountPosition`, `balanceUpdate`, `listStatus` feldolgozás

**GUI integráció példa (src/ui/gui_app.cpp):**

```cpp
// 1) Impl-be add:
std::unordered_map<std::string, std::pair<double,double>> balances; // asset -> {free, locked}
std::vector<std::string> bal_log;

// 2) Connect/Init után, user-data stream callbackek:
self->uds->set_on_balances([this](const std::vector<Balance>& v){
    for (auto& b : v){ self->balances[b.asset] = {b.free, b.locked}; }
});
self->uds->set_on_balance_delta([this](const std::string& a, double d, uint64_t E){
    char buf[128]; std::snprintf(buf, sizeof(buf), "%s delta=%.8f @%llu", a.c_str(), d, (unsigned long long)E);
    self->bal_log.emplace_back(buf);
});

// (opcionális)
self->uds->set_on_list_status([this](const ListStatus& ls){
    self->log.push_back({(uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(),
        std::string("OCO listStatus: ")+ls.listOrderStatus+" "+ls.listStatusType+" sym="+ls.symbol});
});

// 3) Új "Account" panel kirajzolása a fő loopban:
if (ImGui::Begin("Account")){
    if (ImGui::BeginTable("bal", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("Asset"); ImGui::TableSetupColumn("Free"); ImGui::TableSetupColumn("Locked"); ImGui::TableHeadersRow();
        for (auto& kv : self->balances){
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(kv.first.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.8f", kv.second.first);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.8f", kv.second.second);
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
```

> Tipp: A fenti kódot illeszd be a meglévő `gui_app.cpp`-dbe a megfelelő helyekre. Ha kéred, küldök egy teljes, már beillesztett `gui_app.cpp`-t is patchként.
