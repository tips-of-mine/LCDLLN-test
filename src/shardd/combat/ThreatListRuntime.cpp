#include "src/shardd/combat/ThreatListRuntime.h"

#include <vector>

namespace engine::server::combat
{
	/// Seed V1 hardcode : un seul couple "creature attaquee + deux
	/// attaquants". Volontairement minimaliste : la valeur de la PR est
	/// d'avoir un Tick reellement appele en boucle main, pas d'avoir
	/// un scenario realiste. Le scenario realiste viendra avec les
	/// handlers Damage cote shardd qui appelleront AddThreat depuis
	/// les paquets gameplay.
	void ThreatListRuntime::SeedV1Aggro()
	{
		m_lists.clear();
		m_totalDecayed = 0;

		ThreatList& tl = m_lists[1];   // creature fictive #1
		tl.AddThreat(1001, 50.0f);     // player #1001
		tl.AddThreat(1002, 30.0f);     // player #1002
	}

	/// Decay uniforme par tick. Pour chaque ThreatList, on collecte les
	/// attaquants a soustraire (on ne peut pas iterer + supprimer en
	/// meme temps sur une unordered_map sans utiliser erase(it)). Puis
	/// on applique : si nouvelle valeur <= 0, DropAttacker ; sinon
	/// SetThreat.
	///
	/// Note : (void)nowMs car V1 n'utilise pas le temps wall-clock — le
	/// decay est purement "par tick". Conservation du parametre pour
	/// future iteration scoring d'inactivite.
	std::size_t ThreatListRuntime::Tick(uint64_t /*nowMs*/, float decayAmount)
	{
		if (decayAmount <= 0.0f)
			return 0;

		std::size_t decayedThisTick = 0;
		for (auto& [creatureId, list] : m_lists)
		{
			// Snapshot des attaquants courants pour eviter mutation
			// pendant iteration. TopN(SIZE_MAX) renvoie tout, trie
			// par threat decroissant — l'ordre n'a pas d'importance
			// pour le decay, on prend l'API publique qui existe deja.
			std::vector<EntityId> attackers = list.TopN(static_cast<std::size_t>(-1));
			for (EntityId atk : attackers)
			{
				const float cur = list.GetThreat(atk);
				const float next = cur - decayAmount;
				if (next <= 0.0f)
				{
					list.DropAttacker(atk);
					++decayedThisTick;
				}
				else
				{
					list.SetThreat(atk, next);
				}
			}
		}
		m_totalDecayed += decayedThisTick;
		return decayedThisTick;
	}

	/// Somme des Size() de chaque ThreatList. Lineaire en nombre de
	/// creatures suivies (V1 : 1).
	std::size_t ThreatListRuntime::TotalEntries() const
	{
		std::size_t n = 0;
		for (const auto& [creatureId, list] : m_lists)
			n += list.Size();
		return n;
	}

	/// Acces direct pour futurs handlers ; nullptr si creatureId pas
	/// suivi. V1 : seule creatureId=1 retourne un pointeur valide.
	ThreatList* ThreatListRuntime::Get(EntityId creatureId)
	{
		auto it = m_lists.find(creatureId);
		return (it == m_lists.end()) ? nullptr : &it->second;
	}
}
