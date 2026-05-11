#pragma once
// Wave 21 — Map : base abstraite pour les sous-classes WorldMap /
// DungeonMap / BattlegroundMap. Represente le MAP LUI-MEME (template +
// instance runtime) et expose les hooks de cycle de vie (OnPlayerEnter,
// OnPlayerLeave, OnTick).
//
// Distinction avec InstanceManager (Wave 9 existant) : InstanceManager
// gere les InstanceId genriques (lifecycle Created/Active/Idle/Despawned).
// Map represente la donnee + le comportement specifique d'une map ; un
// InstanceManager peut gerer plusieurs Map d'un meme type (ex : 50 dungeons
// "Magma Ruins" actives en parallele pour 50 groupes differents).
//
// Wave 21 livre la hierarchie abstraite + 3 specialisations (World /
// Dungeon / Battleground). Le wiring runtime (creation auto-pool, threading,
// integration avec InstanceManager) est volontairement laisse pour une
// Wave ulterieure — cette PR fournit les types + les invariants par
// sous-classe.

#include <cstdint>
#include <string>
#include <unordered_set>

namespace engine::server::maps
{
	using MapId      = uint32_t;
	using InstanceId = uint64_t;

	/// Type discriminant pour les sous-classes Map. Stable (wire format en
	/// dependra potentiellement).
	enum class MapType : uint8_t
	{
		World        = 0,  ///< map ouverte, persistante, 1 instance globale
		Dungeon      = 1,  ///< map instance, locked par player ou group
		Battleground = 2,  ///< map courte duree, scoreboard integre
	};

	inline const char* MapTypeToString(MapType t) noexcept
	{
		switch (t)
		{
			case MapType::World:        return "World";
			case MapType::Dungeon:      return "Dungeon";
			case MapType::Battleground: return "Battleground";
		}
		return "Unknown";
	}

	/// Base abstraite : tous les Map exposent un mapId immutable, un type,
	/// et un set de players actuellement presents. Les sous-classes ajoutent
	/// leur propre semantique (lock, scoreboard, persistance...).
	class Map
	{
	public:
		/// \param mapId identifiant template stable (lookup DB)
		/// \param instanceId instance runtime unique (alloue par caller, ex via
		///        InstanceManager)
		Map(MapId mapId, InstanceId instanceId)
			: m_mapId(mapId), m_instanceId(instanceId)
		{}

		virtual ~Map() = default;

		MapId      Id() const noexcept { return m_mapId; }
		InstanceId Instance() const noexcept { return m_instanceId; }

		/// Type discriminant — implemente par chaque sous-classe.
		virtual MapType Type() const noexcept = 0;

		/// Ajoute \p playerId au set des players presents. Retourne false si
		/// le map n'autorise pas l'entree (ex: lock dungeon, capacite BG
		/// atteinte). Le caller (typiquement le tick shard) decide quoi
		/// faire en cas de refus.
		virtual bool AddPlayer(uint64_t playerId) = 0;

		/// Retire \p playerId du set des players presents. No-op si absent.
		virtual void RemovePlayer(uint64_t playerId) { m_players.erase(playerId); }

		bool HasPlayer(uint64_t playerId) const noexcept
		{
			return m_players.count(playerId) > 0;
		}

		size_t PlayerCount() const noexcept { return m_players.size(); }

		const std::unordered_set<uint64_t>& Players() const noexcept { return m_players; }

	protected:
		MapId                       m_mapId;
		InstanceId                  m_instanceId;
		std::unordered_set<uint64_t> m_players;
	};
}
