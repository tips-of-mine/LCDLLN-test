#pragma once
// Wave 8 — Wrapper runtime ThreatList : detient une (ou plusieurs)
// ThreatList header-only seedees au boot du shardd avec un cas d'aggro
// V1 minimaliste (1 creature, 2 attaquants). Le but est de prouver que
// le path "tick combat + decay" est reellement exerce a chaque tick
// du runtime (ici, 5s), plutot que rester un header-only oriente tests.
//
// Cette PR : 1 creature fictive (creatureId=1) avec 2 attaquants
// (playerId=1001 threat=50, playerId=1002 threat=30). Tick(nowMs)
// applique un decay leger sur l'ensemble (le combat se calme quand
// personne ne tape) et purge les entrees a 0 (DropAttacker). Le
// nombre d'entrees purgees est renvoye pour le log periodique.
//
// Future iteration : map<CreatureGuid, ThreatList> alimentee par les
// handlers Damage / Heal / Taunt cote shardd ; SeedFromDb() pour les
// scenarios scriptes (raid encounters).

#include "src/shardd/combat/ThreatList.h"

#include <cstdint>
#include <unordered_map>

namespace engine::server::combat
{
	/// Wrapper minimaliste autour de ThreatList. V1 : detient une seule
	/// liste de threat seedee au boot pour creatureId=1, avec deux
	/// attaquants fictifs. Future iteration : map<EntityId, ThreatList>
	/// alimentee par les handlers de combat.
	class ThreatListRuntime
	{
	public:
		ThreatListRuntime() = default;

		/// Charge un scenario V1 hardcode : creature #1 avec deux
		/// attaquants (player #1001 = 50 threat, player #1002 = 30).
		/// Idempotent : peut etre rappele pour reset (jamais en prod).
		/// Effet de bord : reset m_lists et m_totalDecayed.
		void SeedV1Aggro();

		/// Applique un decay periodique a toutes les listes detenues.
		/// Pour chaque attaquant, retire \p decayAmount au threat ; si
		/// le threat tombe a 0 (ou en-dessous), l'attaquant est purge
		/// de la liste. Retourne le nombre total d'attaquants purges
		/// ce tick (pour log "[ThreatList] N entries decayed").
		///
		/// \param nowMs Horloge wall-clock en millisecondes (system_clock).
		///   Non utilise par V1 mais conserve pour parite avec EventAI
		///   et pour les futurs scenarios "decay base sur la duree
		///   d'inactivite par attaquant".
		/// \param decayAmount Quantite de threat a retirer par tick par
		///   attaquant. Defaut 5.0f -> a 5s d'intervalle, un attaquant a
		///   50 threat met 50s a etre purge sans nouvelle action de sa
		///   part. Volontairement conservateur pour V1.
		std::size_t Tick(uint64_t nowMs, float decayAmount = 5.0f);

		/// Nombre total d'attaquants purges depuis le boot. Sert pour le
		/// log periodique cumule.
		std::uint64_t TotalDecayed() const noexcept { return m_totalDecayed; }

		/// Nombre de creatures (ThreatList) suivies. V1 : 1.
		std::size_t CreatureCount() const noexcept { return m_lists.size(); }

		/// Nombre total d'attaquants suivis sur l'ensemble des listes.
		/// Util pour log boot "N entries seeded" (vs creatures).
		std::size_t TotalEntries() const;

		/// Acces lecture/ecriture pour les futurs handlers de combat
		/// (Damage/Heal/Taunt). Retourne nullptr si la creature n'est
		/// pas suivie. V1 : seul creatureId=1 existe apres SeedV1Aggro().
		ThreatList* Get(EntityId creatureId);

	private:
		std::unordered_map<EntityId, ThreatList> m_lists;
		std::uint64_t                            m_totalDecayed = 0;
	};
}
