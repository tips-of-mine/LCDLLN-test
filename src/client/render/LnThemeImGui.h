#pragma once

// Pont ImGui du thème : convertit les couleurs LnTheme::Rgba (0–1, sans
// dépendance ImGui) vers les types ImGui. Volontairement séparé de LnTheme.h
// pour que ce dernier reste PUR (aucun #include ImGui, inclus très largement,
// y compris par des cibles sans ImGui comme ln_theme_tests). N'inclure ce
// header QUE depuis des blocs `#if defined(_WIN32)` où imgui.h est déjà présent.
//
// Ces deux helpers remplacent les couples IV()/U32() historiquement redéfinis à
// l'identique dans chaque renderer. Sémantique inchangée (bit-à-bit).
//
// Couverture de test : PAS de test unitaire dédié. ToU32 délègue à
// ImGui::ColorConvertFloat4ToU32, indisponible sur la cible ctest (Linux, où
// engine_core est compilé sans ImGui) ; un test réencodant la formule de packing
// ne testerait qu'une copie de la logique (fausse assurance en cas de divergence
// BGRA/arrondi). La non-régression est assurée par la validation visuelle des
// écrans d'auth (le rename est mécanique : s'il compile, le rendu est identique
// par construction).

#include "imgui.h"

#include "src/client/render/LnTheme.h"

namespace LnTheme
{
	/// Convertit une couleur thème (RGBA 0–1) en ImVec4 pour les appels de style ImGui.
	inline ImVec4 ToImVec4(const Rgba& c)
	{
		return ImVec4(c.r, c.g, c.b, c.a);
	}

	/// Convertit une couleur thème en ImU32 packé pour les primitives du DrawList.
	/// Identique à l'ancien U32() : packing/arrondi délégués à ImGui (source de vérité).
	inline ImU32 ToU32(const Rgba& c)
	{
		return ImGui::ColorConvertFloat4ToU32(ToImVec4(c));
	}
} // namespace LnTheme
