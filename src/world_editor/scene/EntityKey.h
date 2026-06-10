#pragma once

#include "src/world_editor/scene/EditorSelection.h" // EntityKind

#include <cstdint>
#include <string_view>

namespace engine::editor::scene
{
	/// Clé stable `uint64` d'une entité pour `LayersDocument` (assignement par
	/// calque survivant aux `Rebuild`, contrairement à `EntityId.index`).
	/// Dérive un FNV-1a 64 bits du `guid`, mélangé au `kind` pour éviter les
	/// collisions inter-types. Concern distinct du hash 32 bits `assetIdHash`.

	/// Clé pour une entité à guid textuel (LayoutInstance). Jamais nulle.
	uint64_t MakeEntityKeyFromString(EntityKind kind, std::string_view guid);

	/// Clé pour une entité à guid numérique (MeshInsert / DungeonPortal). Jamais nulle.
	uint64_t MakeEntityKeyFromGuid(EntityKind kind, uint64_t guid);
}
