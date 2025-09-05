cmake_minimum_required(VERSION 3.22)
project(hesoyam LANGUAGES CXX)

# ---- Toolchain / language ----------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
  # /permissive- jobb STL kompat (MSVC default már így is ok)
endif()

# ---- Options (lépcsőzetes bekapcsolás) --------------------------------------
option(BUILD_GUI        "Build GUI lib + bot_gui app" ON)
option(BUILD_BACKTEST   "Build backtester app"        ON)
option(BUILD_LIVE       "Enable live trading pieces"  ON) # csak később élesítjük

# ---- Dependencies via vcpkg (manifest/toolchain ajánlott) -------------------
# FONTOS: SFML 2.x kell! (2.6.1 javasolt)
find_package(SFML 2 REQUIRED COMPONENTS graphics window system CONFIG)
find_package(imgui REQUIRED CONFIG)
find_package(imgui-sfml REQUIRED CONFIG)        # vcpkg target: imgui-sfml::imgui-sfml
find_package(spdlog REQUIRED CONFIG)
find_package(fmt REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(cpr REQUIRED CONFIG)
find_package(ixwebsocket REQUIRED CONFIG)

# ---- Helpers ----------------------------------------------------------------
set(PROJ_INCLUDE "${CMAKE_SOURCE_DIR}/include")

# Gyors glob-ok (egyszerűség kedvéért). Ha szeretnéd, később cserélhetjük explicit listára.
file(GLOB_RECURSE INDICATORS_SRC  CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/indicators/*.cpp")
file(GLOB_RECURSE TELEMETRY_SRC   CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/telemetry/*.cpp")
file(GLOB_RECURSE EXEC_SRC        CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/exec/*.cpp")
file(GLOB_RECURSE DATA_SRC        CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/data/*.cpp")
file(GLOB_RECURSE UI_SRC          CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/*.cpp")

# ---- Libraries ---------------------------------------------------------------
add_library(indicators STATIC ${INDICATORS_SRC})
target_include_directories(indicators PUBLIC "${PROJ_INCLUDE}")
target_link_libraries(indicators PUBLIC fmt::fmt spdlog::spdlog)

add_library(telemetry STATIC ${TELEMETRY_SRC})
target_include_directories(telemetry PUBLIC "${PROJ_INCLUDE}")
target_link_libraries(telemetry PUBLIC fmt::fmt spdlog::spdlog)

add_library(exec STATIC ${EXEC_SRC})
target_include_directories(exec PUBLIC "${PROJ_INCLUDE}")
target_link_libraries(exec PUBLIC fmt::fmt spdlog::spdlog nlohmann_json::nlohmann_json cpr::cpr)

add_library(data STATIC ${DATA_SRC})
target_include_directories(data PUBLIC "${PROJ_INCLUDE}")
target_link_libraries(data PUBLIC fmt::fmt spdlog::spdlog nlohmann_json::nlohmann_json ixwebsocket::ixwebsocket cpr::cpr)

if(BUILD_GUI)
  add_library(ui STATIC ${UI_SRC})
  target_include_directories(ui PUBLIC "${PROJ_INCLUDE}")
  target_link_libraries(ui
    PUBLIC
      indicators telemetry exec data
      sfml-graphics sfml-window sfml-system
      imgui::imgui imgui-sfml::imgui-sfml
      fmt::fmt spdlog::spdlog
      nlohmann_json::nlohmann_json
  )
endif()

# ---- Apps --------------------------------------------------------------------
if(BUILD_BACKTEST)
  add_executable(backtester apps/backtester.cpp)
  target_include_directories(backtester PRIVATE "${PROJ_INCLUDE}")
  target_link_libraries(backtester PRIVATE indicators exec data telemetry fmt::fmt spdlog::spdlog)
endif()

if(BUILD_GUI)
  add_executable(bot_gui apps/bot_gui.cpp)
  target_include_directories(bot_gui PRIVATE "${PROJ_INCLUDE}")
  target_link_libraries(bot_gui PRIVATE ui)
endif()
