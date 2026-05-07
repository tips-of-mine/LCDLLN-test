# M10.5 / M11.2 — Zone builder: writes chunk packages (chunk.meta + split pak/bin files).
# M100.3 — Les writers sont maintenant dans `zone_builder_lib` (statique, partagée
# avec le futur exporter éditeur). Cet exécutable ne compile plus que `main.cpp`
# et délègue tout le travail réel à la lib.
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

add_executable(zone_builder
  main.cpp
)

# Les headers sont consommés via `<zone_builder/...>` (chemin public exposé par
# `zone_builder_lib`) ; on garde la racine du dépôt pour `engine/core/Config.h`
# et `engine/platform/FileSystem.h` utilisés directement par main.cpp.
target_include_directories(zone_builder PRIVATE
  ${CMAKE_SOURCE_DIR}
)
target_link_libraries(zone_builder PRIVATE zone_builder_lib)

set_target_properties(zone_builder PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/pkg/zone_builder"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/pkg/zone_builder"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/pkg/zone_builder"
)

if(MSVC)
  target_compile_options(zone_builder PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(zone_builder PRIVATE -Wall -Wextra -Wpedantic)
endif()
