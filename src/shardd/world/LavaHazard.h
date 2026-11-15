#pragma once

// Seconde carte (Les Enfers) — lave mortelle, v1.0.123 (décision du propriétaire du
// 15/11/2026 : « mort en 3 s », LORE_BD §16.49).
//
// Le serveur charge le masque de lave de chaque cellule des Enfers qui en contient
// (zones/enfers_*/lava_mask.lvmk, GÉNÉRÉ par tools/world_gen, même valeur que la lave
// lumineuse rendue par le client). Règles :
//   - un joueur vivant est « dans la lave » si le texel sous lui vaut au moins 0,5 et
//     qu'il n'est pas à plus de 1,5 m au-dessus du sol (saut court : pas de répit) ;
//   - il meurt après 3 s CUMULÉES dans la lave sans en sortir ; en sortir remet le
//     compteur à zéro ;
//   - seule la carte des Enfers est concernée (les Royaumes n'ont pas de masque).
//
// Module PUR (LavaMask + WorldCells, aucune dépendance réseau ni ServerApp) : testé seul
// (lava_hazard_tests). Pas thread-safe : thread de tick du shard.

#include "src/shared/world/LavaMask.h"

#include <cstddef>

namespace engine::server
{
	/// Exposition d'un joueur à la lave (un par client connecté).
	struct LavaExposureState
	{
		float seconds = 0.0f; ///< temps cumulé dans la lave depuis l'entrée (s)
		bool inLava = false;  ///< était dans la lave au tick précédent
	};

	/// Résultat d'un tick d'exposition.
	enum class LavaEvent
	{
		None,    ///< rien de nouveau
		Entered, ///< vient d'entrer dans la lave (avertir le joueur)
		Left,    ///< vient d'en sortir (compteur remis à zéro)
		Killed,  ///< 3 s atteintes : le joueur doit mourir (compteur remis à zéro)
	};

	/// Masques de lave des cellules des Enfers et règle de mort.
	class LavaHazard
	{
	public:
		static constexpr float kDeathSeconds = 3.0f;   ///< temps dans la lave avant la mort (s)
		static constexpr float kLavaThreshold = 0.5f;  ///< valeur de masque à partir de laquelle on brûle
		static constexpr float kMaxAboveGroundM = 1.5f; ///< au-delà (saut, vol), pas de brûlure

		/// Retire tous les masques.
		void Clear() { m_masks.clear(); }
		/// Ajoute le masque d'une cellule (ignoré s'il est vide).
		void AddMask(engine::world::LavaMask mask);
		/// Nombre de masques chargés.
		size_t Count() const { return m_masks.size(); }

		/// Vrai si le point global (x, y, z) des Enfers est dans la lave (voir les règles).
		/// \param y altitude des pieds du joueur (m)
		bool InLava(double x, double y, double z) const;

		/// Avance l'exposition de \p state de \p dtSeconds.
		/// \param onEnfers le joueur est sur la carte des Enfers
		/// \param alive le joueur est vivant (un mort ne brûle pas : compteur remis à zéro)
		/// \return l'événement du tick (Killed : appliquer la mort).
		LavaEvent Update(LavaExposureState& state, bool onEnfers, bool alive, double x, double y, double z, float dtSeconds) const;

	private:
		std::vector<engine::world::LavaMask> m_masks;
	};
}
