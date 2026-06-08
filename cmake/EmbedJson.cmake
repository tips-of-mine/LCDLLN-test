# EmbedJson.cmake — convertit un fichier JSON en header C++ exposant le contenu
# comme const char* (raw string literal). Invoqué via `cmake -P` par un
# add_custom_command. Paramètres : -DINPUT, -DOUTPUT, -DSYMBOL.
# Aucun chemin absolu en dur ; tout vient des -D.
if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
  message(FATAL_ERROR "EmbedJson.cmake : INPUT, OUTPUT et SYMBOL requis")
endif()
file(READ "${INPUT}" CONTENT)
# Délimiteur de raw string improbable dans le JSON de jeu.
set(GEN "// AUTO-GENERATED par EmbedJson.cmake — NE PAS EDITER.\n")
string(APPEND GEN "// Source : ${INPUT}\n#pragma once\n")
string(APPEND GEN "namespace engine::server::gameplay {\n")
string(APPEND GEN "inline constexpr const char* ${SYMBOL} = R\"LCDLLN_EMBED(\n")
string(APPEND GEN "${CONTENT}")
string(APPEND GEN "\n)LCDLLN_EMBED\";\n}\n")
file(WRITE "${OUTPUT}" "${GEN}")
