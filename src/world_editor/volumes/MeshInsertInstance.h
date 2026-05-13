#pragma once

#include "src/shared/math/Math.h"

#include <cstdint>
#include <string>

namespace engine::editor::world::volumes
{
	/// Une instance de mesh insert (M100.40, fondation Phase 11 « Volumes 3D »).
	/// Représente un mesh glTF posé sur la heightmap (sans la percer) : grottes,
	/// surplombs, arches, donjons. M100.41/.42/.43 réutilisent cette même struct
	/// avec `insertCategory` différent.
	///
	/// Le `guid` est un identifiant unique 64-bit ; le générateur est un
	/// compteur monotone seedé via `MeshInsertDocument::NextGuid()` (jamais
	/// 0, qui sert de sentinelle "absent").
	struct MeshInsertInstance
	{
		uint64_t           guid             = 0u;
		std::string        gltfRelativePath; // ex: "meshes/caves/cave_small_01.gltf"
		engine::math::Vec3 worldPosition;    // point d'ancrage (typ. centre porte)
		engine::math::Vec3 eulerRotationDeg; // rotation XYZ degrés (Y dominant)
		float              uniformScale = 1.0f;
		std::string        insertCategory;   // "cave", "overhang", "arch", "dungeon"
		std::string        displayName;      // libre, pour Outliner
		bool               hasInteriorVolume   = true;
		bool               castsShadow         = true;
		bool               receivesAudioReverb = true;
		bool               allowsWaterIngress  = false; // si vrai, l'eau M100.13 peut entrer
		float              lightProbeIntensity = 1.0f;
	};

	/// Sentinelle valeur "absent" pour un Guid.
	constexpr uint64_t kInvalidMeshInsertGuid = 0u;
}
