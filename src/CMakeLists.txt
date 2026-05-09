# src/CMakeLists.txt — agrège les apps cmangos-style.
# Chaque sous-dossier déclare ses targets via son propre CMakeLists.txt.

add_subdirectory(shared)

if(WIN32)
  add_subdirectory(shardd)        # WIN32 sandbox (target shard_sandbox_app)
  # Les apps client/world_editor sont définies dans le CMakeLists racine
  # (engine_app, world_editor_app) car elles dépendent de modules vcpkg
  # Vulkan/ImGui et de target_sources spécifiques.
elseif(UNIX)
  add_subdirectory(masterd)       # Linux master (target master_app)
  add_subdirectory(shardd)        # Linux shard (target shard_app)
endif()
