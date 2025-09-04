cmake_minimum_required(VERSION 3.23)
project(crypto_mod_bot LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- ImGui-SFML ne keressen külön SFML-t
set(IMGUI_SFML_FIND_SFML OFF CACHE BOOL "" FORCE)

# --- SFML a saját extlibs-eivel (ne rendszerszintűt használjon)
set(SFML_USE_SYSTEM_DEPS OFF CACHE BOOL "" FORCE)

# --- TLS beállítások az IXWebSockethez
set(USE_TLS       ON  CACHE BOOL "" FORCE)
set(USE_OPEN_SSL  ON  CACHE BOOL "" FORCE)
set(USE_MBED_TLS  OFF CACHE BOOL "" FORCE)
set(USE_ZLIB      OFF CACHE BOOL "" FORCE)

# --- FetchContent források
##FetchContent_Declare(sfml GIT_REPOSITORY https://github.com/SFML/SFML.git GIT_TAG 2.6.1)
##FetchContent_MakeAvailable(sfml)

##FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.90.9)
##FetchContent_MakeAvailable(imgui)
##set(IMGUI_DIR "${imgui_SOURCE_DIR}" CACHE PATH "" FORCE)

##FetchContent_Declare(imgui_sfml GIT_REPOSITORY https://github.com/SFML/imgui-sfml.git GIT_TAG v2.6)
##FetchContent_MakeAvailable(imgui_sfml)
find_package(SFML CONFIG REQUIRED COMPONENTS Graphics Window System)
find_package(ImGui-SFML CONFIG REQUIRED)

FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 11.0.2)
FetchContent_Declare(spdlog GIT_REPOSITORY https://github.com/gabime/spdlog.git GIT_TAG v1.14.1)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3)
FetchContent_Declare(cpr GIT_REPOSITORY https://github.com/libcpr/cpr.git GIT_TAG 1.10.5)
FetchContent_Declare(ixwebsocket GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git GIT_TAG v11.4.6)
FetchContent_MakeAvailable(fmt spdlog json cpr ixwebsocket)

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

add_library(strategy INTERFACE)
target_include_directories(strategy INTERFACE include)
target_link_libraries(strategy INTERFACE fmt::fmt spdlog::spdlog)

add_library(sim INTERFACE)
target_include_directories(sim INTERFACE include)
target_link_libraries(sim INTERFACE spdlog::spdlog fmt::fmt)

add_library(data
    src/data/binance_ws.cpp
    src/data/binance_userstream.cpp)
# <- Itt a cpr!
target_link_libraries(data PUBLIC ixwebsocket nlohmann_json::nlohmann_json spdlog::spdlog fmt::fmt cpr::cpr)
target_include_directories(data PUBLIC include)

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
target_link_libraries(ui PUBLIC
  ImGui-SFML::ImGui-SFML
  sfml-graphics sfml-window sfml-system
  spdlog::spdlog fmt::fmt
  indicators strategy telemetry data sim exec)
target_include_directories(ui PUBLIC include)

# --- Alkalmazások
add_executable(bot_gui apps/bot_gui.cpp)
target_link_libraries(bot_gui PRIVATE ui)

add_executable(backtester apps/backtester.cpp)
target_link_libraries(backtester PRIVATE indicators strategy spdlog::spdlog fmt::fmt nlohmann_json::nlohmann_json)

if (WIN32)
  add_custom_command(TARGET bot_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      $<TARGET_FILE_DIR:sfml-graphics>/../..
      $<TARGET_FILE_DIR:bot_gui>)
endif()
