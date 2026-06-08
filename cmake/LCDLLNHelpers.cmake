# cmake/LCDLLNHelpers.cmake
# Helpers CMake reutilisables pour le projet LCDLLN.
# Chargement : include(LCDLLNHelpers) dans le CMakeLists racine apres
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake").
#
# Inspire de la structure cmangos (cmangos-tbc/cmake/) qui isole les
# helpers dans des modules dediés, hors du CMakeLists racine.

include_guard(GLOBAL)

# Garde anti-NDEBUG pour les tests CTest.
#
# Probleme : la CI compile en preset linux-x64-release (CMAKE_BUILD_TYPE=
# Release), ce qui ajoute -DNDEBUG aux flags. -DNDEBUG desactive TOUS les
# assert() de la stdlib. Or beaucoup de nos binaires de test reposent sur
# assert(...) pour leurs verifications. Sans cette garde ils s'executent,
# affichent leurs [OK] et retournent 0 SANS rien verifier : ils sont verts
# en CI de facon trompeuse (regressions non detectees). Cf. memoire projet
# « assert+NDEBUG = piege ».
#
# Solution : passer -UNDEBUG (/UNDEBUG sur MSVC) en option de compilation
# PRIVEE sur la cible de test. Les options de cible arrivent APRES les flags
# globaux (CMAKE_CXX_FLAGS_RELEASE) sur la ligne de commande, donc le -U
# annule le -DNDEBUG : les assert restent actifs meme en Release, et
# uniquement pour la cible de test (le code de production garde NDEBUG).
#
# Idempotent : marque la cible via une propriete pour ne pas empiler le flag
# si appele plusieurs fois (helper + balayage de repertoire, cf. plus bas).
function(lcdlln_keep_asserts target)
  get_target_property(_already ${target} LCDLLN_ASSERTS_KEPT)
  if(_already)
    return()
  endif()
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
  set_target_properties(${target} PROPERTIES LCDLLN_ASSERTS_KEPT TRUE)
endfunction()

# Applique lcdlln_keep_asserts() a toutes les cibles de test (ctest)
# enregistrees dans le repertoire courant. A appeler a la FIN d'un
# CMakeLists.txt qui declare des tests « manuellement » (add_executable +
# add_test), une fois tous les add_test faits. Convention du repo :
# add_test(NAME x COMMAND x ...) => nom de test == nom de cible, donc on
# peut retrouver la cible a partir du nom de test.
#
# Note : la propriete DIRECTORY/TESTS n'est PAS recursive (elle ne voit que
# les tests du repertoire courant, pas ceux des add_subdirectory). Chaque
# CMakeLists.txt qui declare des tests doit donc appeler cette fonction.
function(lcdlln_keep_asserts_all_dir_tests)
  get_property(_tests DIRECTORY PROPERTY TESTS)
  foreach(_test ${_tests})
    if(TARGET ${_test})
      lcdlln_keep_asserts(${_test})
    endif()
  endforeach()
endfunction()

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
  target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  # Garde anti-NDEBUG : conserve les assert() actifs meme en build Release
  # (sinon le test serait vert sans rien verifier). Cf. lcdlln_keep_asserts.
  lcdlln_keep_asserts(${target_name})
  add_test(NAME ${target_name} COMMAND ${target_name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()
