# cmake/LCDLLNHelpers.cmake
# Helpers CMake reutilisables pour le projet LCDLLN.
# Chargement : include(LCDLLNHelpers) dans le CMakeLists racine apres
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake").
#
# Inspire de la structure cmangos (cmangos-tbc/cmake/) qui isole les
# helpers dans des modules dediés, hors du CMakeLists racine.

include_guard(GLOBAL)

# Helper pour declarer un test simple sans deps externes (engine_core +
# spdlog + pthread suffisent). Cible le pattern recurrent des tests
# CMANGOS Phase 2+ (BIH, ObjectGuid, VMapFormat, GridState, ChatSanitizer,
# Metric, Messager, PacketLog, Formulas, Util, etc.) ou chaque target
# faisait ~9 lignes de boilerplate identique. Avec ce helper, declarer
# un test devient :
#
#   lcdlln_add_simple_test(my_thing_tests
#     foo/MyThingTests.cpp
#     foo/MyThing.cpp
#     foo/MyDep.cpp)
#
# Les tests qui ont besoin de MySQL, OpenSSL, ou d'autres libs gardent
# leur add_executable manuel. Ce helper reduit la zone de conflit dans
# CMakeLists.txt entre PRs paralleles : chaque nouvelle test target =
# 1 appel sur 4-5 lignes au lieu de 9-10 lignes d'equivalent inline.
#
# Exige que la cible engine_core soit deja declaree.
function(lcdlln_add_simple_test target_name)
  add_executable(${target_name} ${ARGN})
  target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(${target_name} PRIVATE engine_core spdlog::spdlog pthread)
  # -UNDEBUG : force la reactivation des assert() dans les tests, MEME en build
  # Release. Le preset CI linux-x64-release (CMAKE_BUILD_TYPE=Release) injecte
  # -DNDEBUG via CMAKE_CXX_FLAGS_RELEASE, ce qui transforme tous les assert(...)
  # en no-op -> les tests passeraient « vacuousement » (verts sans rien verifier).
  # ATTENTION : assert se base sur #ifdef NDEBUG (presence du symbole), PAS sur
  # sa valeur. Definir NDEBUG=0 ne suffirait donc PAS (le symbole reste defini) :
  # il faut bien -UNDEBUG pour le retirer. Les target_compile_options sont
  # placees apres CMAKE_CXX_FLAGS_RELEASE sur la ligne de commande, donc ce
  # -UNDEBUG annule correctement le -DNDEBUG precedent.
  target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME ${target_name} COMMAND ${target_name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()
