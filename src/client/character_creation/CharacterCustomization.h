#pragma once

/// @file src/client/character_creation/CharacterCustomization.h
/// @brief Représentation sérialisable d'un personnage personnalisé (CHAR-MODEL.25).
///
/// Ce header décrit *l'état* de customisation choisi par le joueur (race, genre,
/// modules, couleurs, métriques corporelles, morph targets). Il ne dépend
/// d'aucun sous-système moteur : c'est une structure de données pure que l'on
/// peut valider, sérialiser et transmettre.
///
/// La résolution de cet état en assets concrets (chemins de mesh, scaling d'os,
/// textures) et son application au rendu sont gérées par
/// `CharacterCustomizationSystem` (voir CharacterCustomizationSystem.h).
///
/// Aligné sur le modèle de races existant : les `raceId` sont ceux de
/// `game/data/races/races.json` (humains, elfes, orcs, nains, morts_vivants,
/// corrompus, divins, demons) — pas une taxonomie parallèle.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace engine::client
{
	/// Métriques corporelles continues, bornées par race via `physicalLimits`
	/// dans le JSON de race. Toutes les valeurs sont des multiplicateurs sans
	/// unité sauf mention contraire.
	struct CharacterBodyMetrics
	{
		float heightScale        = 1.0f; ///< Scaling global appliqué à la transform racine.
		float legLengthRatio     = 1.0f; ///< Allongement vertical des os de jambe.
		float torsoWidthRatio    = 1.0f; ///< Largeur du torse (spine).
		float shoulderWidthRatio = 1.0f; ///< Écartement des clavicules.
		float bodyMassIndex      = 0.0f; ///< Corpulence (morph / swap de mesh), signé.
	};

	/// État complet de customisation d'un personnage.
	///
	/// Les indices (`headIndex`, `hairStyleIndex`, …) référencent les tableaux
	/// du JSON de race pour le `gender` choisi. Les `*Id` (couleurs) référencent
	/// les `id` des palettes du JSON de race.
	struct CharacterCustomization
	{
		// ---- Identité ----
		std::string raceId;     ///< Ex. "humains", "orcs" (cf. races.json).
		std::string gender;     ///< "male" | "female".
		std::string bodyTypeId; ///< Ex. "base", "muscular".

		// ---- Modules (indices dans les tableaux du JSON de race) ----
		uint32_t headIndex       = 0;
		uint32_t hairStyleIndex  = 0;
		uint32_t facialHairIndex = 0;

		// ---- Couleurs (ids de palettes) ----
		std::string skinToneId;
		std::string hairColorId;
		std::string eyeColorId;

		// ---- Métriques corporelles ----
		CharacterBodyMetrics bodyMetrics;

		// ---- Morph targets (poids dans [-1, 1] sauf bornes de race) ----
		std::unordered_map<std::string, float> morphWeights{
		    {"faceWidth", 0.0f},
		    {"jawWidth", 0.0f},
		    {"noseSize", 0.0f},
		    {"cheekbones", 0.0f},
		    {"eyeSize", 0.0f},
		    {"lipThickness", 0.0f},
		    {"bodyMass", 0.0f},
		};

		// ---- Traits raciaux optionnels (index dans racialFeatures[<feature>]) ----
		std::optional<uint32_t> tuskIndex;   ///< orcs.
		std::optional<uint32_t> hornIndex;   ///< demons.
		std::optional<uint32_t> tailIndex;   ///< demons.
		std::optional<uint32_t> earIndex;    ///< elfes.
		std::optional<uint32_t> scalesIndex; ///< (chevalier-dragon — hors races MVP).
		std::optional<uint32_t> wingsIndex;  ///< (démons / dragons ailés — hors MVP).

		/// Validation *structurelle* minimale (champs obligatoires non vides).
		/// La validation complète (bornes, indices, ids existants) nécessite la
		/// config de race et vit dans `CharacterCustomizationSystem`.
		bool IsValid() const;

		/// Sérialise en JSON compact (clés stables, lisible par FromJson).
		std::string ToJson() const;

		/// Désérialise depuis une chaîne produite par ToJson(). Les champs
		/// absents conservent leur valeur par défaut.
		static CharacterCustomization FromJson(const std::string& jsonStr);
	};

} // namespace engine::client
