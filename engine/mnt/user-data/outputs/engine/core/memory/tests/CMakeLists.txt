# engine/core/memory/tests/CMakeLists.txt
# M00.2 — Memory module smoke test.

add_executable(memory_smoke_test
    memory_smoke_test.cpp
)

target_link_libraries(memory_smoke_test PRIVATE engine_memory)
target_compile_features(memory_smoke_test PUBLIC cxx_std_20)
