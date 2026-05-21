#pragma once

/// @file src/client/character_creation/CharacterCustomizationSystem.h
/// @brief Chargement, validation et résolution de la customisation de
///        personnage (CHAR-MODEL.25).
///
/// Le système est *data-driven* : il charge les fichiers
/// `game/data/configuration/races/*.json` (générés par
/// `tools/asset_pipeline/gen_race_configs.py` à partir de `races.json`) et
/// expose :
///   - la validation d'un `CharacterCustomization` contre les limites de race ;
///   - la génération de personnages aléatoires valides ;
///   - la *résolution* d'une customisation en assets concrets
///     (`ResolvedCharacterAssets` : chemins de mesh, scaling d'os, collision,
///     textures) prête à être consommée par un futur étage de rendu.
///
/// IMPORTANT — limite d'intégration actuelle : le moteur n'expose pas encore de
/// scène à base de `GameObject` / `Skeleton` / composants. `ApplyCustomization`
/// se limite donc à résoudre les assets et tracer le résultat ; le câblage vers
/// le rendu skinné réel (attachement des mesh aux sockets, scaling GPU des os,
/// upload des textures) est délibérément laissé en stub documenté.

#include "src/client/character_creation/CharacterCustomization.h"

#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::core
{
	class Config;
}

namespace engine::client
{
	/// Intervalle borné [min, max] avec valeur par défaut.
	struct ValueRange
	{
		double min          = 0.0;
		double max          = 0.0;
		double defaultValue = 0.0;
	};

	/// Limites physiques d'une race (issues de physicalLimits du JSON).
	struct RacePhysicalLimits
	{
		struct Height
		{
			double baseMeters   = 1.75;
			double scaleMin     = 0.9;
			double scaleMax     = 1.1;
			double scaleDefault = 1.0;
		} height;

		ValueRange bodyMass;

		struct Proportions
		{
			ValueRange legLength;
			ValueRange shoulderWidth;
			ValueRange torsoWidth;
		} proportions;
	};

	/// Dimensions de la capsule de collision par défaut.
	struct RaceCollisionDefaults
	{
		double radius = 0.45;
		double height = 1.75;
	};

	/// Un module attachable / sélectionnable (corps, tête, cheveux, trait racial).
	struct CustomizationModule
	{
		std::string id;
		std::string model; ///< Chemin relatif à paths.content.
		std::string displayName;
	};

	/// Une option de couleur (peau, cheveux, yeux).
	struct ColorOption
	{
		std::string id;
		std::string displayName;
		std::string hex;
		// Peau uniquement :
		std::string diffuse;
		std::string normal;
		std::string orm;
		// Cheveux / yeux :
		std::string texture;
		bool        emissive = false;
	};

	/// Définition d'un morph target (slider) avec ses bornes.
	struct MorphTargetDef
	{
		std::string name;
		std::string displayName;
		double      min          = -1.0;
		double      max          = 1.0;
		double      defaultValue = 0.0;
	};

	/// Configuration complète d'une race, chargée depuis un fichier JSON.
	class RaceConfiguration
	{
	public:
		std::string raceId;
		std::string displayName;
		std::string description;
		std::string baseSkeleton  = "humanoid_base";
		std::string animationSet  = "humanoid_base";

		RacePhysicalLimits    physicalLimits;
		RaceCollisionDefaults collisionDefaults;

		std::vector<std::string> genders;

		// Modules par genre ("male"/"female").
		std::unordered_map<std::string, std::vector<CustomizationModule>> bodyTypes;
		std::unordered_map<std::string, std::vector<CustomizationModule>> heads;
		std::unordered_map<std::string, std::vector<CustomizationModule>> hairStyles;
		std::unordered_map<std::string, std::vector<CustomizationModule>> facialHair;

		// Traits raciaux par nom de feature ("tusks", "horns", "tails", "ears"…).
		std::unordered_map<std::string, std::vector<CustomizationModule>> racialFeatures;

		std::vector<ColorOption> skinTones;
		std::vector<ColorOption> hairColors;
		std::vector<ColorOption> eyeColors;

		std::vector<MorphTargetDef> faceMorphs;
		std::vector<MorphTargetDef> bodyMorphs;

		std::vector<std::string> additionalAnimations;

		/// Charge une config de race depuis un fichier JSON (parser
		/// `engine::core::Config`). Renvoie false si le fichier est illisible
		/// ou n'expose pas de `raceId`. `out` est laissé partiellement rempli
		/// en cas d'échec (ne pas l'utiliser).
		static bool LoadFromFile(const std::string& jsonPath, RaceConfiguration& out);

		/// Liste de modules pour un genre, ou nullptr si absent.
		const std::vector<CustomizationModule>* ModulesFor(
		    const std::unordered_map<std::string, std::vector<CustomizationModule>>& map,
		    const std::string& gender) const;

		bool HasGender(const std::string& gender) const;
	};

	// ------------------------------------------------------------------------
	// Résolution : customisation -> assets concrets
	// ------------------------------------------------------------------------

	/// Scaling local à appliquer sur un os nommé du squelette.
	struct ResolvedBoneScale
	{
		std::string boneName;
		float       x = 1.0f;
		float       y = 1.0f;
		float       z = 1.0f;
	};

	/// Un mesh à attacher à un socket nommé du squelette.
	struct ResolvedAttachment
	{
		std::string kind;      ///< "head", "hair", "facial_hair", "<feature>".
		std::string socket;    ///< Nom du socket / bone.
		std::string modelPath; ///< Chemin relatif à paths.content.
	};

	/// Résultat de la résolution d'une customisation : tout ce qu'un étage de
	/// rendu skinné aurait besoin pour instancier le personnage.
	struct ResolvedCharacterAssets
	{
		bool valid = false;

		std::string bodyMeshPath;
		std::vector<ResolvedAttachment> attachments;

		// Peau.
		std::string skinHex;
		std::string skinDiffuse;
		std::string skinNormal;
		std::string skinOrm;

		// Cheveux / yeux.
		std::string hairColorHex;
		std::string hairColorTexture;
		std::string eyeColorHex;
		std::string eyeColorTexture;

		// Métriques résolues.
		float rootScale = 1.0f;
		std::vector<ResolvedBoneScale> boneScales;
		float collisionRadius = 0.0f;
		float collisionHeight = 0.0f;
	};

	// ------------------------------------------------------------------------
	// Système
	// ------------------------------------------------------------------------

	class CharacterCustomizationSystem
	{
	public:
		CharacterCustomizationSystem();
		~CharacterCustomizationSystem();

		CharacterCustomizationSystem(const CharacterCustomizationSystem&)            = delete;
		CharacterCustomizationSystem& operator=(const CharacterCustomizationSystem&) = delete;

		/// Charge toutes les configs de race présentes dans
		/// `<configBasePath>/races/*.json`. Renvoie true si au moins une race
		/// a été chargée.
		bool Initialize(const std::string& configBasePath = "game/data/configuration");

		// ---- Validation ----
		bool ValidateCustomization(const CharacterCustomization& custom) const;
		std::vector<std::string> GetValidationErrors(const CharacterCustomization& custom) const;

		// ---- Résolution / application ----

		/// Résout une customisation en assets concrets. `valid == false` si la
		/// customisation est invalide (voir GetValidationErrors).
		ResolvedCharacterAssets ResolveCustomization(const CharacterCustomization& custom) const;

		/// Stub documenté : résout les assets et trace le plan d'application.
		/// L'application réelle au rendu skinné est différée (cf. en-tête).
		void ApplyCustomization(const CharacterCustomization& custom) const;

		// ---- Accès ----
		const RaceConfiguration* GetRaceConfig(const std::string& raceId) const;
		std::vector<std::string> GetAvailableRaces() const;
		size_t RaceCount() const { return m_raceConfigs.size(); }

		// ---- Presets / génération ----

		/// Customisation par défaut valide (premiers modules, métriques par
		/// défaut de la race). raceId/gender doivent exister.
		CharacterCustomization MakeDefaultCustomization(const std::string& raceId,
		                                                const std::string& gender) const;

		/// Customisation aléatoire valide pour la race/genre donnés.
		CharacterCustomization GenerateRandomCustomization(const std::string& raceId,
		                                                   const std::string& gender);

	private:
		static bool InRange(double v, const ValueRange& r);
		static const ColorOption* FindColor(const std::vector<ColorOption>& palette,
		                                     const std::string& id);

		std::unordered_map<std::string, std::shared_ptr<RaceConfiguration>> m_raceConfigs;
		std::mt19937 m_rng;
	};

} // namespace engine::client
