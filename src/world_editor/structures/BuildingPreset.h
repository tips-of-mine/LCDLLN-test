#pragma once

// Auberge éditable (T2) — Modèle de preset de bâtiment : liste d'éléments
// (mesh + transform relatif au pivot) + ancre de spawn. Header-only (data).

#include <string>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::editor::world::structures
{
	/// Un élément du bâtiment : un mesh posé à un offset relatif au pivot du
	/// groupe, avec une rotation Y propre et une échelle uniforme.
	struct BuildingPresetElement
	{
		std::string meshPath;            ///< "meshes/props/Wall_Plaster_Straight.gltf"
		engine::math::Vec3 offset{};     ///< offset (m) relatif au pivot, avant rotation de groupe
		float yawDeg = 0.0f;             ///< rotation Y propre (deg)
		float scale = 1.0f;              ///< échelle uniforme
		float collisionRadius = 0.0f;    ///< cylindre collision (m) ; 0 = non solide
		bool solid = true;               ///< bloque le joueur ?
	};

	/// Un preset de bâtiment réutilisable (ex. auberge). Le pivot n'est pas
	/// stocké ici : il est fourni au moment de l'instanciation/export.
	struct BuildingPreset
	{
		std::string id;                  ///< "auberge_demo"
		std::string displayName;         ///< "Auberge (démo)"
		engine::math::Vec3 spawnAnchor{};///< offset (m) relatif au pivot : point de respawn "inn"
		std::vector<BuildingPresetElement> elements;
	};
}
