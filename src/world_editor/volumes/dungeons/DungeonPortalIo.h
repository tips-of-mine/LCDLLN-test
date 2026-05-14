#pragma once

#include "src/world_editor/volumes/dungeons/DungeonPortalInstance.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::editor::world::volumes::dungeons
{
	/// Magic du format binaire `instances/dungeon_portals.bin` ("LCDP"
	/// little-endian — Lcdlln Dungeon Portal).
	constexpr uint32_t kDungeonPortalsBinMagic   = 0x50444C43u;  // 'C' 'L' 'D' 'P' little-endian
	/// Version courante (M100.43 v1). Distinct de LCMI (M100.40) car
	/// portail = donnée gameplay, pas un mesh décoratif pur.
	constexpr uint32_t kDungeonPortalsBinVersion = 1u;

	/// Sérialise au format `dungeon_portals.bin` (M100.43 v1).
	/// Header 16 octets : `[magic(4)][version(4)][instanceCount(4)][reserved(4)]`.
	/// Chaque instance :
	///   - `guid` (uint64)
	///   - `dungeonTemplateId` (u16 length + UTF-8)
	///   - `displayName` (u16 length + UTF-8)
	///   - `decorativeMeshPath` (u16 length + UTF-8)
	///   - `worldPosition` (3 float32)
	///   - `eulerRotationDeg` (3 float32)
	///   - `triggerRadius` (float32)
	///   - `requiredLevel` (uint16)
	///   - `minDifficulty` (uint8)
	///   - `maxDifficulty` (uint8)
	///   - `flags` (u8 : bit0=isOneShot, bit1=persistsAcrossLogin)
	bool SaveDungeonPortalsBin(const std::vector<DungeonPortalInstance>& instances,
		std::vector<uint8_t>& outBytes, std::string& outError);

	/// Désérialise un `dungeon_portals.bin`. Valide magic + version.
	/// Fichier vide / minimal → liste vide retournée.
	bool LoadDungeonPortalsBin(std::span<const uint8_t> bytes,
		std::vector<DungeonPortalInstance>& outInstances, std::string& outError);
}
