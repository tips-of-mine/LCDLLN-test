# src/masterd/CMakeLists.txt — Linux master server.

if(NOT UNIX)
  return()
endif()

# La cible master_app est déclarée dans le CMakeLists racine pour bénéficier
# du helper lcdlln_* et des dépendances vcpkg/MySQL résolues au top-level.
# Ce fichier est un placeholder pour le futur (ex: target spécifiques masterd).
