cmake_minimum_required(VERSION 3.23)
project(crypto_mod_bot LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# ImGui-SFML ne hívja meg a find_package(SFML)-t, mert mi FetchContent-tel hozzuk
set(IMGUI_SFML_FIND_SFML OFF CACHE BOOL "" FORCE)

include(FetchContent)

# fmt
FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 11.0.2)
# spdlog
FetchContent_Declare(spdlog GIT_REPOSITORY https://github.com/gabime/spdlog.git GIT_TAG v1.14.1)
# JSON
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3)
# cpr (HTTP/REST – Telegram, Binance REST)
FetchContent_Declare(cpr GIT_REPOSITORY https://github.com/libcpr/cpr.git GIT_TAG 1.10.5)
# SFML + ImGui-SFML (GUI)
FetchContent_Declare(sfml GIT_REPOSITORY https://github.com/SFML/SFML.git GIT_TAG 2.6.1)
FetchContent_MakeAvailable(sfml)
# ImGui-SFML -> most már látja a sfml-graphics, sfml-window, stb. targeteket
FetchContent_Declare(imgui_sfml GIT_REPOSITORY https://github.com/SFML/imgui-sfml.git GIT_TAG v2.6)
FetchContent_MakeAvailable(imgui_sfml)
# IXWebSocket – TLS-es WebSocket Binance-hez (wss)
FetchContent_Declare(ixwebsocket GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git GIT_TAG v11.6.3)

FetchContent_MakeAvailable(fmt spdlog json cpr sfml imgui_sfml ixwebsocket)

# OpenSSL a HMAC-SHA256 aláíráshoz (Binance REST)
find_package(OpenSSL REQUIRED)

include_directories(include)

# --- Könyvtárak
add_library(indicators
    src/indicators/rsi.cpp
    src/indicators/sma_ema.cpp
    src/indicators/bollinger.cpp
    src/indicators/mtfa.cpp)

target_link_libraries(indicators PUBLIC fmt::fmt spdlog::spdlog nlohmann_json::nlohmann_json)
target_include_directories(indicators PUBLIC include)

add_library(strategy
    include/strategy/decision.hpp)
target_link_libraries(strategy PUBLIC fmt::fmt spdlog::spdlog)
target_include_directories(strategy PUBLIC include)

add_library(data
    src/data/binance_ws.cpp
    src/data/binance_userstream.cpp)
target_link_libraries(data PUBLIC ixwebsocket nlohmann_json::nlohmann_json spdlog::spdlog fmt::fmt)
target_include_directories(data PUBLIC include)

add_library(sim
    include/sim/demo_account.hpp
    include/sim/position_sim.hpp)
target_link_libraries(sim PUBLIC spdlog::spdlog fmt::fmt)
target_include_directories(sim PUBLIC include)

add_library(telemetry
    src/telemetry/telegram_notifier.cpp)
target_link_libraries(telemetry PUBLIC cpr::cpr spdlog::spdlog fmt::fmt nlohmann_json::nlohmann_json)
target_include_directories(telemetry PUBLIC include)

add_library(exec
    src/exec/binance_rest.cpp
    src/exec/filters.cpp
    src/exec/order_state.cpp
    src/exec/position_tracker.cpp)
target_link_libraries(exec PUBLIC cpr::cpr OpenSSL::SSL OpenSSL::Crypto spdlog::spdlog fmt::fmt nlohmann_json::nlohmann_json)
target_include_directories(exec PUBLIC include)

add_library(ui
    src/ui/gui_app.cpp)
target_link_libraries(ui PUBLIC ImGui-SFML::ImGui-SFML sfml-graphics sfml-window sfml-system spdlog::spdlog fmt::fmt indicators strategy telemetry data sim exec)
target_include_directories(ui PUBLIC include)

# --- Alkalmazások
add_executable(bot_gui apps/bot_gui.cpp)
target_link_libraries(bot_gui PRIVATE ui)

add_executable(backtester apps/backtester.cpp)
target_link_libraries(backtester PRIVATE indicators strategy spdlog::spdlog fmt::fmt nlohmann_json::nlohmann_json)

# Windows: SFML DLL-ek másolása (opcionális)
if (WIN32)
  add_custom_command(TARGET bot_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      $<TARGET_FILE_DIR:sfml-graphics>/../..
      $<TARGET_FILE_DIR:bot_gui>)
endif()
