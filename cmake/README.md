# cmake/

Modules CMake reutilisables pour le projet LCDLLN.

Inspire de la structure [cmangos-tbc/cmake/](https://github.com/cmangos/mangos-tbc/tree/master/cmake) qui isole les helpers dans des modules dedies, hors du `CMakeLists.txt` racine.

## Modules

| Fichier | Role |
|---|---|
| [`LCDLLNHelpers.cmake`](./LCDLLNHelpers.cmake) | Helpers de declaration de cibles : `lcdlln_add_simple_test()`. |

## Chargement

Le `CMakeLists.txt` racine charge ces modules via :

```cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(LCDLLNHelpers)
```

## Ajout d'un nouveau module

1. Creer `cmake/<NomModule>.cmake` avec un `include_guard(GLOBAL)` en tete.
2. Documenter le role et les preconditions dans le fichier (commentaires `#`).
3. Charger le module dans `CMakeLists.txt` racine (apres `list(APPEND CMAKE_MODULE_PATH ...)`) via `include(<NomModule>)`.
4. Mettre a jour ce README + `CODEBASE_MAP.md` section "Outils et CI".
