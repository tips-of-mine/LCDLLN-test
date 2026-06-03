# M45.4 — impostor_builder: outil offline (pas de Vulkan, pas d'engine_core).
# Pré-rend un atlas d'impostors octaédriques depuis un mesh glTF.
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

add_executable(impostor_builder
  main.cpp
  ImpostorFormat.cpp
  Rasterizer.cpp
  GltfMeshLoader.cpp
)

# Include dirs :
#  - dossier courant (headers de l'outil)
#  - external/cgltf (cgltf.h, header-only)
#  - external/stb   (stb_image.h, header-only — décodage textures baseColor)
#  - racine du dépôt (au cas où un header partagé autonome serait inclus)
target_include_directories(impostor_builder PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/external/cgltf
  ${CMAKE_SOURCE_DIR}/external/stb
  ${CMAKE_SOURCE_DIR}
)

set_target_properties(impostor_builder PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/pkg/impostor_builder"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/pkg/impostor_builder"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/pkg/impostor_builder"
)

if(MSVC)
  target_compile_options(impostor_builder PRIVATE /W4 /permissive-)
else()
  target_compile_options(impostor_builder PRIVATE -Wall -Wextra)
endif()

# Copie build_impostors.bat à côté de l'exe (script d'utilisation)
if(EXISTS "${CMAKE_SOURCE_DIR}/build_impostors.bat")
  add_custom_command(TARGET impostor_builder POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/build_impostors.bat"
            "$<TARGET_FILE_DIR:impostor_builder>/build_impostors.bat"
    COMMENT "Copie de build_impostors.bat..."
  )
endif()
