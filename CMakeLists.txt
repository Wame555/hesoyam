cmake_minimum_required(VERSION 3.23)
project(crypto_mod_bot LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- vcpkg toolchain szükséges a configure során
# cmake -S . -B build -A x64 ^
#   -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
#   -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_BUILD_TYPE=Release

# --- 3rd party (mind vcpkg-ből)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
# SFML 3 (vagy bármi): a vcpkg-n 3.0.1 az alap
find_package(SFML CONFIG REQUIRED COMPONENTS Graphics Window System)
find_package(ImGui-SFML CONFIG REQUIRED)

# --- include path a saját headerjeinkhez
include_directories(include)

# ===================== KÖNYVTÁRAK =====================

# Indicators
add_library(indicators
    src/indicators/rsi.cpp
    src/indicators/sma_ema.cpp
    src/indicators/bollinger.cpp
    src/indicators/mtfa.cpp)
target_link_libraries(indicators PUBLIC fmt::fmt spdlog::spdlog nlohmann_json::nlohmann_json)
target_include_directories(indicators PUBLIC include)

# Strategy (header-only)
add_library(strategy INTERFACE)
target_include_directories(strategy INTERFACE include)
target_link_libraries(strategy INTERFACE fmt::fmt spdlog::spdlog)

# Sim (header-only)
add_library(sim INTERFACE)
target_include_directories(sim INTERFACE include)
target_link_libraries(sim INTERFACE spdlog::spdlog fmt::fmt)

# Data (WS + user-data stream) – KELL a cpr is a header miatt
add_library(data
    src/data/binance_ws.cpp
    src/data/binance_userstream.cpp)
target_link_libraries(data PUBLIC
    ixwebsocket::ixwebsocket
    nlohmann_json::nlohmann_json
    spdlog::spdlog fmt::fmt
    cpr::cpr)
target_include_directories(data PUBLIC include)

# Telemetry (Telegram -> cpr)
add_library(telemetry
    src/telemetry/telegram_notifier.cpp)
target_link_libraries(telemetry PUBLIC cpr::cpr spdlog::spdlog fmt::fmt nlohmann_json::nlohmann_json)
target_include_directories(telemetry PUBLIC include)

# Exec (Binance REST + risk + position tracker)
add_library(exec
    src/exec/binance_rest.cpp
    src/exec/filters.cpp
    src/exec/order_state.cpp
    src/exec/position_tracker.cpp)
target_link_libraries(exec PUBLIC
    cpr::cpr
    OpenSSL::SSL OpenSSL::Crypto
    spdlog::spdlog fmt::fmt nlohmann_json::nlohmann_json)
target_include_directories(exec PUBLIC include)

# UI (SFML + ImGui-SFML)
add_library(ui
    src/ui/gui_app.cpp)
target_link_libraries(ui PUBLIC
  ImGui-SFML::ImGui-SFML
  SFML::Graphics SFML::Window SFML::System
  spdlog::spdlog fmt::fmt
  indicators strategy telemetry data sim exec)
target_include_directories(ui PUBLIC include)

# ===================== APP-OK =====================

add_executable(bot_gui apps/bot_gui.cpp)
target_link_libraries(bot_gui PRIVATE ui)

add_executable(backtester apps/backtester.cpp)
target_link_libraries(backtester PRIVATE indicators strategy spdlog::spdlog fmt::fmt nlohmann_json::nlohmann_json)

# Windows: SFML DLL-ek másolása (opcionális)
if (WIN32)
  add_custom_command(TARGET bot_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Copying SFML/ImGui-SFML runtime dlls (vcpkg handles PATH typically)"
  )
endif()
