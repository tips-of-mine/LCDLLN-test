// src/client/world/hazard/HazardVolumes.h
#pragma once

#include "src/shared/math/Math.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::world::hazard
{
	/// Magic du fichier `instances/hazards.bin` ("HAZA" little-endian).
	constexpr uint32_t kHazardsMagic   = 0x5A415748u;
	constexpr uint32_t kHazardsVersion = 1u;

	/// Type de hazard. Détermine les paramètres de simulation par défaut
	/// et le comportement spécial (LavaSurface tue par contact).
	enum class HazardType : uint32_t
	{
		Quicksand   = 0,
		Bog         = 1,
		Tar         = 2,
		LavaSurface = 3
	};

	/// Forme du volume. Le choix Box/Cylinder est figé à la création par l'éditeur.
	enum class HazardShape : uint32_t
	{
		Box      = 0,
		Cylinder = 1
	};

	/// Mode d'évasion. `None` = mort scriptée si maxDepth atteinte.
	/// `MashButtonItem` requiert en plus un item dans l'inventaire local.
	enum class EscapeMode : uint32_t
	{
		None            = 0,
		MashButton      = 1,
		LateralMove     = 2,
		MashButtonItem  = 3
	};

	/// Une instance de volume hazard dans le monde. Position monde +
	/// dimensions selon `shape`. Les paramètres de simulation sont par
	/// instance pour permettre des variantes éditeur.
	struct HazardInstance
	{
		HazardType type            = HazardType::Quicksand;
		HazardShape shape          = HazardShape::Cylinder;
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		engine::math::Vec3 boxHalfExtents{ 2.0f, 1.0f, 2.0f }; // utilisé si Box
		float cylRadius            = 4.0f;                      // utilisé si Cylinder
		float cylHeight            = 2.0f;                      // utilisé si Cylinder
		float sinkRateMps          = 0.15f;
		float maxDepthMeters       = 1.8f;
		float slowdownMul          = 0.10f;
		EscapeMode escapeMode      = EscapeMode::MashButton;
		uint32_t requiredItemId    = 0;                         // 0 si escapeMode != MashButtonItem
	};

	struct HazardScene
	{
		std::vector<HazardInstance> hazards;
	};

	/// Sérialise au format `hazards.bin` (M100.16). Header OutputVersionHeader
	/// (magic=kHazardsMagic, version=kHazardsVersion, contentHash=xxhash64 du payload).
	bool SaveHazardsBin(const HazardScene& scene,
		std::vector<uint8_t>& outBytes, std::string& outError);

	/// Désérialise. Valide magic, version, contentHash. Reset outScene.
	bool LoadHazardsBin(std::span<const uint8_t> bytes,
		HazardScene& outScene, std::string& outError);

	/// Test point-in-volume 3D selon `hz.shape`.
	bool PointInHazard(const HazardInstance& hz, engine::math::Vec3 worldPos) noexcept;
}
