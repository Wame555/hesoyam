cmake_minimum_required(VERSION 3.16)
project(trader_bot LANGUAGES CXX)

# Enable C++17. We use modern language features such as std::optional and
# structured bindings throughout the project. Feel free to bump this to
# C++20 once your toolchain supports it to unlock even more powerful
# abstractions.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define a flag to make builds reproducible. Turn off compiler and linker
# specific optimisations here. You can tweak these flags to suit your
# environment or build type (Debug vs Release).
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel"
      FORCE)
endif()

# External dependencies
# -----------------------------------------------------------------------------
# websocketpp is a header only WebSocket client/server library.  You can
# install it via your package manager or include it as a git submodule.  The
# skeleton assumes it is already available on your include path.  If you
# prefer to vendor the library, add it to this repository and point
# CMAKE_INCLUDE_PATH at its include directory.
find_path(WEBSOCKETPP_INCLUDE_DIR websocketpp/client.hpp)
if(NOT WEBSOCKETPP_INCLUDE_DIR)
  message(WARNING "websocketpp headers not found. Data feed functionality will be disabled.")
endif()

# nlohmann/json is a single header library for working with JSON in C++.
# Many systems provide a package named `nlohmann_json`.  We attempt to find
# it first; if not found, you may want to fetch it from
# https://github.com/nlohmann/json and add it to your include path.
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
if(NOT NLOHMANN_JSON_INCLUDE_DIR)
  message(WARNING "nlohmann/json.hpp not found. Configuration management will be limited.")
endif()

# Dear ImGui is used for the real‑time GUI.  Integrating Dear ImGui is
# intentionally left as an exercise for later development.  Here we
# demonstrate how to structure your build system to add third party
# libraries.  You can download ImGui from
# https://github.com/ocornut/imgui and create a CMake library target named
# `imgui` to link against.  Until that is done we simply guard GUI
# compilation behind a compile definition.
option(USE_IMGUI "Build GUI components using Dear ImGui" ON)
if(USE_IMGUI)
  # If you vendored Dear ImGui, add_subdirectory here:
  # add_subdirectory(extern/imgui)
  add_definitions(-DUSE_IMGUI)
endif()

# Define the main library target.  We separate the core logic into a
# library to facilitate unit testing and reuse in multiple frontends
# (command line, GUI, etc.).
file(GLOB_RECURSE LIB_SOURCES
     src/config_manager.cpp
     src/strategy.cpp
     src/data_feed.cpp
     src/exchange_interface.cpp
     src/telegram_notifier.cpp
     src/gui.cpp
     src/app.cpp
     src/indicator/*.cpp)

file(GLOB_RECURSE LIB_HEADERS
     src/config_manager.h
     src/strategy.h
     src/data_feed.h
     src/exchange_interface.h
     src/telegram_notifier.h
     src/gui.h
     src/app.h
     src/indicator/*.h)

add_library(trader_bot_lib ${LIB_SOURCES} ${LIB_HEADERS})
target_include_directories(trader_bot_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${WEBSOCKETPP_INCLUDE_DIR}
    ${NLOHMANN_JSON_INCLUDE_DIR}
)

# Link optional dependencies if they were found.  Replace these with
# find_package results as you integrate additional libraries.
if(NOT WEBSOCKETPP_INCLUDE_DIR)
  target_compile_definitions(trader_bot_lib PRIVATE TRADER_BOT_NO_WEBSOCKETS)
endif()
if(NOT NLOHMANN_JSON_INCLUDE_DIR)
  target_compile_definitions(trader_bot_lib PRIVATE TRADER_BOT_NO_JSON)
endif()

# Add the executable that ties everything together.  Keeping main.cpp
# separate from the library allows you to swap out the entry point with
# another implementation, for example when writing unit tests.
add_executable(trader_bot src/main.cpp)
target_link_libraries(trader_bot PRIVATE trader_bot_lib)

# Installation rules (optional).  They allow `make install` to copy the
# library and headers into standard locations so your bot can be used as a
# dependency in other projects.  Adjust the install destinations to suit
# your environment.
install(TARGETS trader_bot_lib trader_bot
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib/static)
install(DIRECTORY src/ DESTINATION include
        FILES_MATCHING PATTERN "*.h")