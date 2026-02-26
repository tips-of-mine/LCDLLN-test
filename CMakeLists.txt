cmake_minimum_required(VERSION 3.24)
project(MMORPGEngine VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ---------------------------------------------------------------------------
# Dependencies (via vcpkg toolchain or system)
# ---------------------------------------------------------------------------
find_package(nlohmann_json REQUIRED)

# ---------------------------------------------------------------------------
# engine_core static library (M00.1: Log, Time, Config)
# ---------------------------------------------------------------------------
add_library(engine_core STATIC
    engine/core/Log.cpp
    engine/core/Time.cpp
    engine/core/Config.cpp
)

target_include_directories(engine_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}   # makes "engine/core/Log.h" resolvable
)

target_link_libraries(engine_core
    PUBLIC
        nlohmann_json::nlohmann_json
)

# Warnings (platform-appropriate)
if (MSVC)
    target_compile_options(engine_core PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_core PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# engine_app — minimal executable (main.cpp)
# ---------------------------------------------------------------------------
add_executable(engine_app main.cpp)

target_link_libraries(engine_app
    PRIVATE engine_core
)

if (MSVC)
    target_compile_options(engine_app PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_app PRIVATE -Wall -Wextra -Werror)
endif()

# Copy config.json next to the executable so it is found at runtime.
add_custom_command(TARGET engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:engine_app>/config.json"
    COMMENT "Copying config.json to output directory"
)
