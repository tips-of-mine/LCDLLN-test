#pragma once
// ETAPE5.5 (chantier 3.5 du rapport City of Heroes) — sauvegardes différentielles.
//
// Chaque sauvegarde réécrivait tout le personnage : position, progression (quêtes,
// barres, métiers…), bourse (4 lignes), sac (DELETE + une ligne par pile) et équipement
// (DELETE + une ligne par emplacement). La plupart des sauvegardes (autosave, ramassage)
// ne changent pourtant qu'un ou deux blocs.
//
// On garde, par personnage, l'empreinte (FNV-1a 64 bits) de chaque bloc tel qu'il a été
// VALIDÉ en base (COMMIT réussi). Une sauvegarde ne réécrit que les blocs dont l'empreinte
// a changé. Comparer des empreintes plutôt que poser des marqueurs « modifié » à chaque
// mutation attrape tous les chemins de modification (y compris les quantités modifiées
// en place) sans toucher au code de jeu.
//
// Garde-fous :
//   - empreinte inconnue (premier enregistrement depuis le chargement, échec, COMMIT
//     incertain, écriture par un autre chemin) ⇒ tout est écrit ;
//   - toutes les `fullEvery` sauvegardes, écriture complète (auto-réparation si la base
//     a été modifiée par un outil externe) ;
//   - le contrôle de propriété, le courrier réclamé, la restauration et la carte restent
//     inconditionnels (ils ne font pas partie des blocs).
//
// Module sans E/S : testé seul (character_save_diff_tests). SavedBlockCache est
// thread-safe (les sauvegardes tournent sur les threads de CharacterSaveQueue).

#include "src/shardd/gameplay/character/CharacterPersistence.h"

#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace engine::server
{
	/// Blocs d'un personnage écrits séparément par WriteCharacterStateInTx.
	enum SaveBlock : uint32_t
	{
		kSaveBlockPosition = 1u << 0,  ///< characters.spawn_* + level
		kSaveBlockProgress = 1u << 1,  ///< character_progress (zone, XP, PV, quêtes, barres…)
		kSaveBlockWallet = 1u << 2,    ///< player_wallet (4 devises)
		kSaveBlockInventory = 1u << 3, ///< character_inventory
		kSaveBlockEquipment = 1u << 4, ///< character_equipment
		kSaveBlocksAll = (1u << 5) - 1u,
	};

	/// Nombre de blocs dans \p blocks (télémétrie).
	uint32_t CountSaveBlocks(uint32_t blocks);

	/// Empreintes des blocs d'un état de personnage.
	struct SaveBlockHashes
	{
		uint64_t position = 0;
		uint64_t progress = 0;
		uint64_t wallet = 0;
		uint64_t inventory = 0;
		uint64_t equipment = 0;
	};

	/// Calcule les empreintes. \p yawRadians fait partie du bloc position ;
	/// \p progressPayload = SerializeCharacterProgressPayload(state) (déjà calculé par
	/// l'appelant, qui l'écrit aussi).
	SaveBlockHashes ComputeSaveBlockHashes(const PersistedCharacterState& state, float yawRadians,
		std::string_view progressPayload);

	/// Blocs dont l'empreinte diffère entre \p saved (dernier état validé) et \p current.
	uint32_t DiffSaveBlocks(const SaveBlockHashes& saved, const SaveBlockHashes& current);

	/// Plan d'une sauvegarde : blocs à écrire et jeton de version de la référence lue.
	struct SavePlan
	{
		uint32_t blocks = kSaveBlocksAll;
		uint64_t version = 0; ///< à rendre à OnCommitted (détecte une écriture concurrente)
	};

	/// Empreintes du dernier état validé, par personnage.
	class SavedBlockCache final
	{
	public:
		/// \param fullEvery une sauvegarde sur fullEvery est complète (borné à [1 ; 1000] ;
		///        1 = toujours complète, ce qui désactive le différentiel).
		explicit SavedBlockCache(uint32_t fullEvery = 20u);

		void SetFullEvery(uint32_t fullEvery);
		uint32_t FullEvery() const;

		/// Blocs à écrire pour \p characterKey dont l'état courant a les empreintes
		/// \p current : tous si l'empreinte validée est inconnue ou si c'est le tour d'une
		/// écriture complète, sinon les blocs modifiés (éventuellement aucun). Le jeton de
		/// version rendu doit être passé à OnCommitted.
		SavePlan Plan(uint64_t characterKey, const SaveBlockHashes& current) const;

		/// COMMIT réussi d'une sauvegarde planifiée avec \p version : \p written (blocs
		/// écrits) et \p current deviennent la référence. Si la référence a changé depuis
		/// le Plan (autre écriture validée ou invalidation entre-temps), on ne sait plus
		/// quelle écriture est la dernière en base : la référence est invalidée (prochaine
		/// sauvegarde complète).
		void OnCommitted(uint64_t characterKey, const SaveBlockHashes& current, uint32_t written, uint64_t version);

		/// La référence n'est plus sûre (chargement, échec, COMMIT incertain, écriture par
		/// un autre chemin) : la prochaine sauvegarde sera complète. Le jeton de version
		/// avance (une sauvegarde déjà planifiée ne pourra pas réinstaller l'ancienne).
		void Invalidate(uint64_t characterKey);

		/// Oublie complètement le personnage (déconnexion, sauvegarde finale écrite) :
		/// mémoire bornée. À n'appeler que sans sauvegarde en cours pour lui.
		void Forget(uint64_t characterKey);

		size_t Size() const;

	private:
		struct Entry
		{
			SaveBlockHashes hashes{};
			bool valid = false;
			uint32_t savesSinceFull = 0;
			uint64_t version = 0;
		};
		mutable std::mutex m_mutex;
		std::unordered_map<uint64_t, Entry> m_entries;
		uint32_t m_fullEvery = 20u;
	};
}
