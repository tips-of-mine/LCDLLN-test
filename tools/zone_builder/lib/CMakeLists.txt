# M100.3 — zone_builder_lib : bibliothèque statique partagée par l'exécutable
# offline `zone_builder` (tools/zone_builder/) et le futur exporter de
# l'éditeur monde (M100.5+). Garantit que les deux écrivent EXACTEMENT le même
# format binaire (anti-duplication des writers).
#
# Dépendances :
#   - PUBLIC : aucune (les headers ne tirent que la STL et src/shared/core/Config.h
#     qui est header-only à l'usage exposé).
#   - PRIVATE : spdlog (transitif via engine::core::Log) + sources individuelles
#     de engine_core (Config.cpp, Log.cpp, FileSystem.cpp, OutputVersion.cpp)
#     pour rester sans Vulkan / GLFW / ImGui — contrainte M100.3 §"Dépendances
#     de la bibliothèque". On NE link PAS engine_core (qui tire Vulkan).
#
# Format binaire : strictement inchangé (kZoneMetaMagic, kChunkMetaMagic,
# kInstancesMagic, versions). L'objectif est l'extraction, pas l'évolution.
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

add_library(zone_builder_lib STATIC
  ChunkPackageWriter.cpp
  GltfImporter.cpp
  JsonDocument.cpp
  LayoutImporter.cpp
  ${CMAKE_SOURCE_DIR}/src/client/world/OutputVersion.cpp
  ${CMAKE_SOURCE_DIR}/src/client/world/terrain/LayerPalette.cpp
  ${CMAKE_SOURCE_DIR}/src/client/world/terrain/SplatMap.cpp
  ${CMAKE_SOURCE_DIR}/src/client/world/terrain/TerrainChunk.cpp
  ${CMAKE_SOURCE_DIR}/src/client/world/terrain/TerrainLodChain.cpp
  ${CMAKE_SOURCE_DIR}/src/client/world/water/WaterSurfaces.cpp
  ${CMAKE_SOURCE_DIR}/src/shared/core/Config.cpp
  ${CMAKE_SOURCE_DIR}/src/shared/core/ServerEndpoints.cpp
  ${CMAKE_SOURCE_DIR}/src/shared/core/Log.cpp
  ${CMAKE_SOURCE_DIR}/src/shared/platform/FileSystem.cpp
)

# Le dossier `Public/` expose `<zone_builder/XXX.h>` aux consommateurs
# (executable zone_builder + tests + futur exporter éditeur).
target_include_directories(zone_builder_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Public
)
# Les sources de la lib utilisent encore `src/shared/core/Log.h`, etc., donc on
# expose aussi la racine du dépôt pour les implémentations.
target_include_directories(zone_builder_lib PRIVATE
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(zone_builder_lib PUBLIC spdlog::spdlog routine_graph)

if(MSVC)
  target_compile_options(zone_builder_lib PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(zone_builder_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

# M100.3 — Tests round-trip bit-à-bit (cf. tests/RoundtripTests.cpp en-tête).
# Pure CPU, pas d'ImGui ni Vulkan : tournables en CI sans GPU.
add_executable(zone_builder_roundtrip_tests tests/RoundtripTests.cpp)
target_link_libraries(zone_builder_roundtrip_tests PRIVATE zone_builder_lib)
target_include_directories(zone_builder_roundtrip_tests PRIVATE ${CMAKE_SOURCE_DIR})
if(MSVC)
  target_compile_options(zone_builder_roundtrip_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(zone_builder_roundtrip_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME zone_builder_roundtrip_tests COMMAND zone_builder_roundtrip_tests)

# M101.3 / M101.10 — Round-trip fichier editeur->client de routines.bin.
add_executable(routines_segment_tests tests/RoutinesSegmentTests.cpp)
target_link_libraries(routines_segment_tests PRIVATE zone_builder_lib)
target_include_directories(routines_segment_tests PRIVATE ${CMAKE_SOURCE_DIR})
if(MSVC)
  target_compile_options(routines_segment_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(routines_segment_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME routines_segment_tests COMMAND routines_segment_tests)

# Garde anti-NDEBUG : re-active les assert() pour les cibles de test de ce
# repertoire (sinon vertes sans rien verifier en Release). A laisser apres
# tous les add_test. Cf. lcdlln_keep_asserts_all_dir_tests.
lcdlln_keep_asserts_all_dir_tests()
