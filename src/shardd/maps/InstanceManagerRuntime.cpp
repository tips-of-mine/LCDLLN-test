#include "src/shardd/maps/InstanceManagerRuntime.h"

namespace engine::server::maps
{
	/// Enregistre trois MapIds V1 hardcodees comme instanciables :
	///   - 100 : "Donjon des Marais"   (donjon 5-man)
	///   - 101 : "Crypte du Vieux Roi" (donjon 5-man)
	///   - 200 : "Vallee des Cendres"  (battleground PvP)
	/// Ces ids ne pretendent pas matcher un DBC reel ; le loader DB
	/// fournira l'ensemble canonique (probablement aligne sur la
	/// numerotation existante des assets monde).
	void InstanceManagerRuntime::SeedV1Maps()
	{
		m_maps.clear();
		m_maps.insert(MapId{100});
		m_maps.insert(MapId{101});
		m_maps.insert(MapId{200});
	}

	/// Cree une instance pour \p mapId si elle est enregistree. La
	/// validation se fait sur le set m_maps : si le loader DB n'a pas
	/// pousse la map, on refuse silencieusement (retour 0) plutot que
	/// de spawner une instance fantome. \p partyGuid est ignore en V1
	/// mais le parametre est present pour stabiliser la signature
	/// (l'appelant final passera deja la party).
	InstanceId InstanceManagerRuntime::CreateInstance(MapId mapId,
		std::uint64_t /*partyGuid*/, std::uint64_t nowMs)
	{
		if (!IsMapRegistered(mapId))
			return InstanceId{0};
		return m_mgr.Create(mapId, nowMs);
	}

	bool InstanceManagerRuntime::IsMapRegistered(MapId mapId) const noexcept
	{
		return m_maps.find(mapId) != m_maps.end();
	}
}
