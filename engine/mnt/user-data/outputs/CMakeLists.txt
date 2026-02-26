cmake_minimum_required(VERSION 3.24)
project(mmorpg_engine VERSION 0.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Memory module (M00.2) ────────────────────────────────────────────────────
add_subdirectory(engine/core/memory)

# ── Smoke test (M00.2 DoD validation) ───────────────────────────────────────
add_subdirectory(engine/core/memory/tests)
