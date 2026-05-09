# M100.9 — gen_terrain_placeholders : mini-tool standalone qui génère 8 PNG
# 4×4 unicolore dans `game/data/terrain/placeholders/`, fournissant des textures
# de fallback visuellement distinctes pour le shader 8-layer terrain. Pas de
# dépendance zlib externe : encodage PNG canonique avec deflate stored block.
# Remplaçables transparents quand les vrais assets PBR arriveront.

cmake_minimum_required(VERSION 3.20)

add_executable(gen_terrain_placeholders main.cpp)

if(MSVC)
  target_compile_options(gen_terrain_placeholders PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(gen_terrain_placeholders PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Custom command POST_BUILD : exécute le tool pour générer les PNG dans
# `<source_root>/game/data/terrain/placeholders/`. Tourne sur Linux + Windows.
# Le tool lit le 1er argv pour le dossier de sortie.
add_custom_command(TARGET gen_terrain_placeholders POST_BUILD
  COMMAND $<TARGET_FILE:gen_terrain_placeholders>
          "${CMAKE_SOURCE_DIR}/game/data/terrain/placeholders"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Generation des placeholders terrain (8 PNG 4x4 colores)..."
)
