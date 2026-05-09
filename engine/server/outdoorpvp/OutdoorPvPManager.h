#pragma once
// CMANGOS.36 (Phase 5.36a) — OutdoorPvPManager : zones contestees a objectifs
// (towers/captures) avec score par faction et reset configurable. Header-only.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server::outdoorpvp
{
	using ZoneId      = uint32_t;
	using ObjectiveId = uint32_t;
	using FactionId   = uint8_t;  ///< 0 = Alliance, 1 = Horde, 0xFF = neutre

	struct Objective
	{
		ObjectiveId id        = 0;
		FactionId   owner     = 0xFF; ///< 0xFF = neutre/contested
		uint32_t    capturePct = 0;   ///< 0..100, progression de la capture en cours
		FactionId   capturingBy = 0xFF; ///< faction qui progresse (0xFF si aucune)
	};

	struct Zone
	{
		ZoneId id = 0;
		std::vector<Objective> objectives;
		std::unordered_map<FactionId, uint32_t> score; ///< score par faction
	};

	/// Etat du gestionnaire OutdoorPvP. Header-only, zero dep externe.
	class OutdoorPvPManager
	{
	public:
		void RegisterZone(const Zone& z) { m_zones[z.id] = z; }
		bool HasZone(ZoneId zid) const { return m_zones.count(zid) > 0; }

		/// Demarre une capture par \p fac sur l'objectif \p oid de la zone \p zid.
		/// Reset capturePct si la faction change. Retourne false si zone/objectif inconnu.
		bool BeginCapture(ZoneId zid, ObjectiveId oid, FactionId fac)
		{
			auto* obj = FindObjective(zid, oid);
			if (!obj) return false;
			if (obj->capturingBy != fac)
			{
				obj->capturingBy = fac;
				obj->capturePct  = 0;
			}
			return true;
		}

		/// Avance la capture en cours de \p deltaPct (0..100). Si capturePct atteint
		/// 100, l'objectif change de proprietaire et la faction gagne 1 point de score.
		/// Retourne true si capture terminee dans cet appel (transition d'owner).
		bool TickCapture(ZoneId zid, ObjectiveId oid, uint32_t deltaPct)
		{
			auto* obj = FindObjective(zid, oid);
			if (!obj) return false;
			if (obj->capturingBy == 0xFF) return false;
			obj->capturePct += deltaPct;
			if (obj->capturePct >= 100)
			{
				obj->capturePct = 0;
				const auto fac  = obj->capturingBy;
				obj->owner       = fac;
				obj->capturingBy = 0xFF;
				m_zones[zid].score[fac] += 1;
				return true;
			}
			return false;
		}

		/// Score actuel d'une faction dans une zone (0 si zone/faction absente).
		uint32_t Score(ZoneId zid, FactionId fac) const
		{
			auto it = m_zones.find(zid);
			if (it == m_zones.end()) return 0;
			auto sit = it->second.score.find(fac);
			return (sit == it->second.score.end()) ? 0 : sit->second;
		}

		/// Reset complet d'une zone (score + objectifs neutres). Utilise au reset
		/// hebdomadaire ou pour tests.
		void ResetZone(ZoneId zid)
		{
			auto it = m_zones.find(zid);
			if (it == m_zones.end()) return;
			it->second.score.clear();
			for (auto& o : it->second.objectives)
			{
				o.owner       = 0xFF;
				o.capturePct  = 0;
				o.capturingBy = 0xFF;
			}
		}

	private:
		Objective* FindObjective(ZoneId zid, ObjectiveId oid)
		{
			auto it = m_zones.find(zid);
			if (it == m_zones.end()) return nullptr;
			for (auto& o : it->second.objectives)
				if (o.id == oid) return &o;
			return nullptr;
		}

		std::unordered_map<ZoneId, Zone> m_zones;
	};
}
