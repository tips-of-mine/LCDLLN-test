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
	///                Le reader reste rétrocompatible avec v1 : si la section
	///                ocean est absente, `outSeaLevelMeters` est laissé à sa
	///                valeur d'entrée (typiquement défaut 50 m). Le writer écrit
	///                toujours en v2.
	constexpr uint32_t kWaterVersion = 2u;

	/// Lac : polygone fermé CCW dans XZ, surface plate à `waterLevelY`.
	struct LakeInstance
	{
		std::string name;
		std::vector<engine::math::Vec3> polygon;          // CCW dans XZ
		engine::math::Vec3 bottomColor{ 0.05f, 0.20f, 0.30f };
		float turbidity   = 0.4f;
		float waterLevelY = 0.0f;
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

	/// Sérialise au format `water.bin` v2 (M100.36 — étendu de M100.13).
	/// Header OutputVersionHeader (magic=kWaterMagic, version=2,
	/// contentHash=xxhash64 du payload). Le payload est composé de :
	///   [ lakes ][ rivers ][ ocean (4 octets, seaLevelMeters) ]
	///
	/// \param scene             Scène eau (lacs + rivières).
	/// \param seaLevelMeters    Sea level global de la zone (M100.36 ; en
	///                          pratique `WaterDocument::GetOcean().seaLevelMeters`).
	/// \param outBytes          Buffer rempli avec le fichier complet.
	/// \param outError          Renseigné en cas d'échec (jamais en v2 actuelle).
	bool SaveWaterBin(const WaterScene& scene, float seaLevelMeters,
		std::vector<uint8_t>& outBytes, std::string& outError);

	/// Désérialise un `water.bin` (v1 OU v2). Valide magic + contentHash.
	/// Reset `outScene`. Si le fichier est en v1 (sans section ocean),
	/// `outSeaLevelMeters` reste à la valeur initiale fournie par l'appelant
	/// (typiquement le défaut `OceanSettings{}.seaLevelMeters = 50`). Si le
	/// fichier est en v2, `outSeaLevelMeters` est écrasé avec la valeur lue.
	///
	/// \param bytes             Fichier complet (header + payload).
	/// \param outScene          Scène eau parsée.
	/// \param outSeaLevelMeters In/out : valeur du sea level lue (ou inchangée
	///                          si fichier v1).
	/// \param outError          Renseigné en cas d'échec.
	bool LoadWaterBin(std::span<const uint8_t> bytes,
		WaterScene& outScene, float& outSeaLevelMeters, std::string& outError);
}
