#pragma once

#include "src/shared/math/Math.h"

#include <cstdint>
#include <string>

namespace engine::editor::world::volumes::dungeons
{
	/// Instance d'un portail de donjon (M100.43, Phase 11 « Volumes 3D »).
	///
	/// Distinct de `MeshInsertInstance` car contient des metadata gameplay :
	///   - `dungeonTemplateId` : identifiant string consommé par le master
	///     (handler `EnterDungeonHandler`, M100.44) pour résoudre une entry
	///     dans `dungeon_instances` (migration 0063).
	///   - `triggerRadius` : zone d'activation autour du pivot ; le client
	///     déclenche un prompt "Entrer" quand le joueur est dedans.
	///   - `requiredLevel` / `minDifficulty` / `maxDifficulty` : gating.
	///   - `decorativeMeshPath` : asset glTF optionnel pour la stylisation
	///     visuelle (arche d'entrée, brasier, runes), purement cosmétique.
	///
	/// Le `guid` est un identifiant unique 64-bit ; le générateur est un
	/// compteur monotone seedé via `DungeonPortalDocument::NextGuid()`.
	struct DungeonPortalInstance
	{
		uint64_t           guid                = 0u;
		std::string        dungeonTemplateId;          ///< ≤ 64 octets, matche kMaxDungeonTemplateIdBytes.
		std::string        displayName;
		std::string        decorativeMeshPath;          ///< optionnel
		engine::math::Vec3 worldPosition;
		engine::math::Vec3 eulerRotationDeg;
		float              triggerRadius        = 3.0f;
		uint16_t           requiredLevel        = 1u;
		uint8_t            minDifficulty        = 1u;   ///< borne mapping `kMaxDungeonDifficulty`
		uint8_t            maxDifficulty        = 1u;
		bool               isOneShot            = false; ///< true = instance unique partagée (raid)
		bool               persistsAcrossLogin  = false;
	};

	constexpr uint64_t kInvalidDungeonPortalGuid = 0u;
}
