// Object.cpp — Wave 7 foundation : implementation vide.
//
// La classe Object est entierement inline (constructeur trivial, accesseurs
// noexcept), donc ce .cpp ne contient pour l'instant que l'include du header.
// Il est cependant present pour :
//   1. Avoir une translation unit nommee si on doit ajouter des methodes
//      out-of-line ou des constantes statiques later.
//   2. Servir de link target pour la cible CMake `engine_core` /
//      `server_app` qui veut un .cpp pour generer une .o.

#include "src/shardd/entities/Object.h"
