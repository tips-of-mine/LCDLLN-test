#pragma once
// src/shared/world/BoatDeck.h — v1.0.125 : repère et pont des bateaux des lignes maritimes
// (dromon de 30 m, game/data/models/props/ships/dromon/dromon.glb ; LORE_BD §16.51).
//
// Partagé par le shard (BoatFleet, validation des passagers) et le client (marche sur le
// pont, rendu) : les deux doivent transformer exactement de la même façon.
//
// Repère du bateau : origine au centre de la coque à la ligne de flottaison, +Z = proue,
// +Y = haut ; cap 0 = proue vers +Z monde, convention de Mat4::RotateY :
//   monde = (x + lx·cos(cap) + lz·sin(cap), z − lx·sin(cap) + lz·cos(cap)).
// En-tête seul, sans dépendance.

#include <algorithm>
#include <cmath>

namespace engine::world::boat
{
	inline constexpr float kDeckY = 1.6f;      ///< hauteur du pont principal au-dessus de la flottaison (m)
	inline constexpr float kDeckHalfX = 2.5f;  ///< demi-largeur praticable du pont (m)
	inline constexpr float kDeckMinZ = -11.5f; ///< poupe praticable (m)
	inline constexpr float kDeckMaxZ = 10.5f;  ///< proue praticable (m)
	/// Centre de la capsule du joueur au-dessus du pont (m), comme au-dessus du sol.
	inline constexpr float kStandHeight = 0.9f;
	/// Distance max (m) entre le joueur et le centre du bateau à quai pour embarquer.
	inline constexpr double kBoardRadiusM = 45.0;

	/// Repère du bateau (cap \p yaw, centre (\p bx, \p bz)) → monde (XZ).
	inline void LocalToWorld(double bx, double bz, double yaw, double lx, double lz, double& outX, double& outZ)
	{
		const double c = std::cos(yaw), s = std::sin(yaw);
		outX = bx + lx * c + lz * s;
		outZ = bz - lx * s + lz * c;
	}

	/// Monde (XZ) → repère du bateau (cap \p yaw, centre (\p bx, \p bz)).
	inline void WorldToLocal(double bx, double bz, double yaw, double wx, double wz, double& outLx, double& outLz)
	{
		const double c = std::cos(yaw), s = std::sin(yaw);
		const double dx = wx - bx, dz = wz - bz;
		outLx = dx * c - dz * s;
		outLz = dx * s + dz * c;
	}

	/// Ramène (lx, lz) sur la surface praticable du pont (NaN → centre). \return true si le point a bougé.
	inline bool ClampToDeck(float& lx, float& lz)
	{
		const bool finite = std::isfinite(lx) && std::isfinite(lz);
		const float x = finite ? std::clamp(lx, -kDeckHalfX, kDeckHalfX) : 0.0f;
		const float z = finite ? std::clamp(lz, kDeckMinZ, kDeckMaxZ) : 0.0f;
		const bool moved = !finite || x != lx || z != lz;
		lx = x;
		lz = z;
		return moved;
	}
}
