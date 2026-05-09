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
	constexpr uint32_t kWaterVersion = 1u;

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

	/// Sérialise au format `water.bin` (M100.13). Header OutputVersionHeader
	/// (magic=kWaterMagic, version=1, contentHash=xxhash64 du payload).
	bool SaveWaterBin(const WaterScene& scene,
		std::vector<uint8_t>& outBytes, std::string& outError);

	/// Désérialise. Valide magic, version, contentHash. Reset outScene.
	bool LoadWaterBin(std::span<const uint8_t> bytes,
		WaterScene& outScene, std::string& outError);
}
