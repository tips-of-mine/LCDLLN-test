// src/client/world/water/WaterSurfaces.h
#pragma once

#include "src/shared/math/Math.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::world::water
{
	/// Magic du fichier `instances/water.bin` ("WATR" little-endian).
	constexpr uint32_t kWaterMagic   = 0x52544157u;
	/// Version courante du payload `water.bin`.
	/// v1 (M100.13) : lakes + rivers uniquement.
	/// v2 (M100.36) : ajoute une section finale "ocean" (4 octets, seaLevelMeters).
	/// v3 (M100.37) : étend la section ocean (~32 octets : seaLevelMeters +
	///                bottomColor + turbidity + windInfluence + enabled),
	///                ajoute un flag `isOcean` (1 octet) par `LakeInstance`.
	///                Le reader v3 reste rétrocompat v2 et v1 (nouveaux champs
	///                reçoivent leurs valeurs par défaut). Le writer écrit
	///                toujours en v3.
	constexpr uint32_t kWaterVersion = 3u;

	/// Lac : polygone fermé CCW dans XZ, surface plate à `waterLevelY`.
	struct LakeInstance
	{
		std::string name;
		std::vector<engine::math::Vec3> polygon;          // CCW dans XZ
		engine::math::Vec3 bottomColor{ 0.05f, 0.20f, 0.30f };
		float turbidity   = 0.4f;
		float waterLevelY = 0.0f;
		/// M100.37 — Flag informatif marquant un lac comme étant LA surface
		/// océan globale de la zone (un seul par zone, généré par
		/// `CoastlineEditorTool`). Pour M100.37 le rendu reste identique aux
		/// lacs normaux ; le flag permet aux passes futures (M100.26 weather,
		/// M100.15 nage côtière) de différencier.
		bool  isOcean     = false;
	};

	/// Node d'une rivière : position 3D (Y typiquement = terrain height),
	/// largeur et profondeur locale.
	struct RiverNode
	{
		engine::math::Vec3 position;
		float widthMeters = 4.0f;
		float depthMeters = 1.0f;
	};

	struct RiverInstance
	{
		std::string name;
		std::vector<RiverNode> nodes;                     // au moins 2 pour produire un mesh
	};

	struct WaterScene
	{
		std::vector<LakeInstance>  lakes;
		std::vector<RiverInstance> rivers;
	};

	/// Section "ocean" sérialisée dans `water.bin` v2+ (M100.36, étendue M100.37).
	/// Mirror de `engine::editor::world::OceanSettings` côté format. La
	/// duplication est volontaire : le layer client `engine::world::water` ne
	/// dépend pas du layer éditeur. La conversion vit dans WaterDocument.
	///
	/// Évolution :
	///   - v2 : seul `seaLevelMeters` est écrit (4 octets).
	///   - v3 : tous les champs sont écrits (~32 octets).
	struct OceanSectionData
	{
		float seaLevelMeters = 50.0f;
		float bottomColor[3] = { 0.10f, 0.23f, 0.33f };
		float turbidity      = 0.4f;
		float windInfluence  = 0.2f;
		bool  enabled        = true;
	};

	/// Sérialise au format `water.bin` v3 (M100.37 — étendu de M100.13/.36).
	/// Header OutputVersionHeader (magic=kWaterMagic, version=3,
	/// contentHash=xxhash64 du payload). Le payload est composé de :
	///   [ lakes (avec isOcean) ][ rivers ][ ocean section ~32 octets ]
	///
	/// \param scene    Scène eau (lacs + rivières), incl. flag `isOcean`.
	/// \param ocean    Paramètres globaux de l'océan (M100.37 v3).
	/// \param outBytes Buffer rempli avec le fichier complet.
	/// \param outError Renseigné en cas d'échec (jamais en v3 actuelle).
	bool SaveWaterBin(const WaterScene& scene, const OceanSectionData& ocean,
		std::vector<uint8_t>& outBytes, std::string& outError);

	/// Désérialise un `water.bin` (v1, v2 OU v3). Valide magic + contentHash.
	/// Reset `outScene`. Champs absents du fichier (selon la version) prennent
	/// la valeur d'entrée de `outOcean` (typiquement le défaut
	/// `OceanSectionData{}`). Les versions sont gérées comme suit :
	///   - v1 : aucune section ocean → `outOcean` inchangé. `isOcean` par défaut
	///     à `false` sur tous les lacs.
	///   - v2 : seul `seaLevelMeters` lu, autres champs ocean inchangés.
	///     `isOcean` par défaut à `false` sur tous les lacs.
	///   - v3 : section ocean complète lue, `isOcean` lu par lac (1 octet).
	bool LoadWaterBin(std::span<const uint8_t> bytes,
		WaterScene& outScene, OceanSectionData& outOcean, std::string& outError);
}
