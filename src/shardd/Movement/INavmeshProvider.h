#pragma once
// Wave 24 — INavmeshProvider : interface abstraite pour le pathfinding
// server-side. SCAFFOLD UNIQUEMENT — la vraie integration recast/detour
// (vcpkg recastnavigation) viendra dans une PR ulterieure avec review
// humaine sur la chaine de build.
//
// Cette interface decouple :
// - le caller (PathFollowMotion, AI movement generators) qui utilise
//   FindPath() avec un PathRequest sans connaitre l'impl concrete
// - l'impl reelle (RecastNavmeshProvider a venir) ou stub (tests)
//
// API minimal Wave 24a : FindPath retourne un vector<Waypoint> ou vide
// si pas de chemin trouve. Pas de smoothing/optimization (delegue a
// l'impl). Pas de async (Wave 24b ulterieure si requis).

#include <cstdint>
#include <vector>

namespace engine::server::movement
{
	using MapId = uint32_t;

	/// Position 3D world-space en metres. Pas de namespace conflit avec
	/// engine::math::Vec3 car on garde le scope local Movement.
	struct PathPoint
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;

		constexpr PathPoint() = default;
		constexpr PathPoint(float xx, float yy, float zz) : x(xx), y(yy), z(zz) {}

		constexpr bool operator==(const PathPoint&) const = default;
	};

	/// Requete de pathfinding. Le caller fournit position de depart + fin
	/// + mapId. Pas de constraints additionnelles (filter zones, hauteur
	/// max, etc.) dans Wave 24a — a etendre quand le besoin emerge.
	struct PathRequest
	{
		MapId     mapId = 0;
		PathPoint from{};
		PathPoint to{};
	};

	/// Resultat de FindPath. Si waypoints est vide, pas de chemin trouve
	/// (target unreachable, mapId inconnu, etc.). Sinon, contient au moins
	/// 2 points (from + to), eventuellement avec intermediaires si l'impl
	/// les fournit.
	struct PathResult
	{
		std::vector<PathPoint> waypoints;

		bool IsValid() const noexcept { return waypoints.size() >= 2; }
	};

	/// Interface abstraite. Impl concrete (RecastNavmeshProvider) ou stub.
	class INavmeshProvider
	{
	public:
		virtual ~INavmeshProvider() = default;

		/// Calcule un chemin entre \p req.from et \p req.to sur \p req.mapId.
		/// Retourne un PathResult dont waypoints est vide si pas de chemin.
		virtual PathResult FindPath(const PathRequest& req) const = 0;

		/// True si le provider connait \p mapId (a charge ses tiles, etc.).
		/// Utile pour gating : si false, FindPath retournera toujours vide.
		virtual bool HasMap(MapId mapId) const = 0;
	};
}
