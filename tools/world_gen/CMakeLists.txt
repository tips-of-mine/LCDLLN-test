# Chantier « Royaumes » — générateur de monde jouable (outil OFFLINE, sans GPU).
#
# world_gen_lib : pipeline déterministe master → terrain, eau, routes, villes,
# végétation, population, carte. Réutilise zone_builder_lib (parseur JSON,
# writer water.bin, OutputVersion) ; les formats runtime partagés avec le client
# (scatter.bin, buildings.bin) sont header-only sous src/client/world/instances/.
# Ne linke PAS engine_core (règle d'architecture : pas de Vulkan/GLFW dans les
# outils et cibles serveur).
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

add_library(world_gen_lib STATIC
  lib/WorldMaster.cpp
  lib/Terrain.cpp
  lib/Places.cpp
  lib/Roads.cpp
  lib/Settlements.cpp
  lib/Vegetation.cpp
  lib/Population.cpp
  lib/Output.cpp
  lib/PngWriter.cpp
  lib/WorldGen.cpp
  # Convention des cellules de 10 km, partagée avec le client et les serveurs
  # (compilée par chemin : pas d'engine_core dans les outils).
  ${CMAKE_SOURCE_DIR}/src/shared/world/WorldCells.cpp
)
target_include_directories(world_gen_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}
)
find_package(Threads REQUIRED)
target_link_libraries(world_gen_lib PUBLIC zone_builder_lib Threads::Threads)

add_executable(lcdlln_world_gen main.cpp)
target_link_libraries(lcdlln_world_gen PRIVATE world_gen_lib)
set_target_properties(lcdlln_world_gen PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/pkg/world_gen"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/pkg/world_gen"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/pkg/world_gen"
)

# Tests : formats (round-trip), PNG (décodé par stb_image), master réel,
# déterminisme (deux générations complètes → octets identiques) et invariants
# du monde (villes au sec, départs par race, PNJ avec routine valide…).
add_executable(world_gen_tests tests/WorldGenTests.cpp)
target_link_libraries(world_gen_tests PRIVATE world_gen_lib)
target_include_directories(world_gen_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/stb)
target_compile_definitions(world_gen_tests PRIVATE LCDLLN_CONTENT_DIR="${CMAKE_SOURCE_DIR}/game/data")
add_test(NAME world_gen_tests COMMAND world_gen_tests)

foreach(tgt world_gen_lib lcdlln_world_gen world_gen_tests)
  if(MSVC)
    target_compile_options(${tgt} PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
  else()
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()
