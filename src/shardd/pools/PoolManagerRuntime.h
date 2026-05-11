#pragma once
// Wave 6 — Wrapper runtime PoolManager : detient un PoolManager seede
// au boot du shardd avec 1-2 pools de demonstration (wolves, rabbits).
// V1 : pools hardcodes pour prouver que le wiring est correct ; les
// futurs PR brancheront un loader DB (table pool_template / pool_member)
// qui remplira m_mgr depuis MySQL.
//
// L'objectif principal de cette PR : instanciation au boot + accesseur
// Roll(PoolId) reutilisable par le code de spawner. Le tirage weighted
// random (sans replacement) est entierement delegue au header-only
// PoolManager.h, on n'ajoute pas de logique ici, juste du cablage.

#include "src/shardd/pools/PoolManager.h"

#include <cstdint>
#include <random>
#include <vector>

namespace engine::server::pools
{
	/// Wrapper minimaliste autour de PoolManager : detient le manager,
	/// un RNG mt19937 (seede via random_device au boot) et expose
	/// Roll(PoolId) pour le code spawner. Future iteration : SeedFromDb()
	/// qui replace SeedV1Pools().
	class PoolManagerRuntime
	{
	public:
		PoolManagerRuntime() : m_rng(std::random_device{}()) {}

		/// Enregistre 1-2 pools V1 hardcodes (wolves, rabbits) pour valider
		/// le wiring. Idempotent : peut etre rappele pour reset (jamais en
		/// prod). Voir .cpp pour les valeurs.
		void SeedV1Pools();

		/// Tire \p count spawns depuis la pool \p poolId. Wrapper direct
		/// sur PoolManager::Roll qui passe le RNG interne. Retourne vide
		/// si pool inconnue.
		///
		/// \param poolId Identifiant de pool seede via SeedV1Pools() ou un
		///   futur loader DB.
		std::vector<SpawnId> Roll(PoolId poolId);

		/// Nombre de pools enregistrees (sert pour le log boot
		/// "[PoolManager] N pools registered at boot").
		std::size_t PoolCount() const noexcept { return m_mgr.PoolCount(); }

	private:
		PoolManager   m_mgr;
		std::mt19937  m_rng;
	};
}
