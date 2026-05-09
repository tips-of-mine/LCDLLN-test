#pragma once
// CMANGOS.02 (Phase 2.02a) — ObjectGuid : identifiant 64-bit pour les
// entités du shard. Encode { type, entry, counter } dans un uint64_t.
//
// Layout :
//   bits [63..56] = type      (HighGuid : 8 bits → 256 types max)
//   bits [55..32] = entry     (template id, 24 bits → 16M entries — pour
//                              les types qui partagent un template comme
//                              Creature, GameObject. Pour Player, entry=0.)
//   bits [31.. 0] = counter   (per-shard, 32 bits → 4 milliards d'instances
//                              avant rollover ; recyclage possible mais
//                              non implémenté ici.)
//
// **Cohérent avec cmangos** : structure équivalente, layout simplifié pour
// LCDLLN (pas de high-low split classique car notre architecture est plus
// simple). Si besoin d'interopérabilité avec un client cmangos plus tard,
// on adaptera (transposition triviale).
//
// Pure : pas de dépendance DB / pool / réseau. Testable en isolation.

#include <cstdint>
#include <cstring>
#include <functional>
#include <ostream>
#include <string>

namespace engine::server::shard
{
	/// Type discriminant (équivalent `HighGuid` cmangos). Choix
	/// volontairement restreint à ce dont LCDLLN a besoin court terme ;
	/// étendre via PR séparée selon besoin.
	enum class HighGuid : uint8_t
	{
		None       = 0,   ///< Sentinelle "guid invalide".
		Player     = 1,
		Creature   = 2,
		GameObject = 3,
		Item       = 4,
		Corpse     = 5,
		Pet        = 6,
		DynObject  = 7,   ///< Pour spell zones, traps, etc.
		Vehicle    = 8,
	};

	/// Convertit un `HighGuid` en label court (utile log/debug). Retourne
	/// "?" pour un type inconnu (jamais nullptr).
	const char* HighGuidLabel(HighGuid t) noexcept;

	/// Identifiant opaque 64-bit. Comparable, hashable, copiable.
	class ObjectGuid
	{
	public:
		/// Construit un guid invalide (`HighGuid::None`, entry=0, counter=0).
		constexpr ObjectGuid() noexcept = default;

		/// Construit un guid à partir de ses composants. \p type peut être
		/// None (alors le guid sera invalide même si entry/counter ≠ 0).
		/// \p entry doit tenir sur 24 bits (16M max). \p counter sur 32.
		constexpr ObjectGuid(HighGuid type, uint32_t entry, uint32_t counter) noexcept
			: m_raw(Pack(type, entry, counter)) {}

		/// Construit un guid pour un type sans entry (Player, Item).
		/// Équivalent à `ObjectGuid(type, 0, counter)`.
		constexpr ObjectGuid(HighGuid type, uint32_t counter) noexcept
			: ObjectGuid(type, 0, counter) {}

		/// Construit depuis la représentation brute. Pas de validation —
		/// le caller garantit que la valeur vient d'un Raw() précédent.
		static constexpr ObjectGuid FromRaw(uint64_t raw) noexcept
		{
			ObjectGuid g; g.m_raw = raw; return g;
		}

		/// Représentation brute, pour sérialisation réseau / DB.
		constexpr uint64_t Raw() const noexcept { return m_raw; }

		constexpr HighGuid Type() const noexcept
		{
			return static_cast<HighGuid>(static_cast<uint8_t>(m_raw >> 56));
		}

		constexpr uint32_t Entry() const noexcept
		{
			return static_cast<uint32_t>((m_raw >> 32) & 0x00FF'FFFFu);
		}

		constexpr uint32_t Counter() const noexcept
		{
			return static_cast<uint32_t>(m_raw & 0xFFFF'FFFFu);
		}

		/// True si type ≠ None ET counter ≠ 0. Un guid construit par
		/// défaut ou explicitement `None` est invalide.
		constexpr bool IsValid() const noexcept
		{
			return Type() != HighGuid::None && Counter() != 0;
		}

		constexpr bool IsPlayer() const     noexcept { return Type() == HighGuid::Player; }
		constexpr bool IsCreature() const   noexcept { return Type() == HighGuid::Creature; }
		constexpr bool IsGameObject() const noexcept { return Type() == HighGuid::GameObject; }
		constexpr bool IsItem() const       noexcept { return Type() == HighGuid::Item; }

		constexpr bool operator==(const ObjectGuid& other) const noexcept = default;
		constexpr auto operator<=>(const ObjectGuid& other) const noexcept = default;

		/// Représentation lisible pour log : "{Type entry counter}", ex.
		/// "{Creature/4242 c=17}".
		std::string ToString() const;

	private:
		static constexpr uint64_t Pack(HighGuid type, uint32_t entry, uint32_t counter) noexcept
		{
			const uint64_t t = static_cast<uint64_t>(type) << 56;
			const uint64_t e = (static_cast<uint64_t>(entry) & 0x00FF'FFFFu) << 32;
			const uint64_t c = static_cast<uint64_t>(counter);
			return t | e | c;
		}

		uint64_t m_raw = 0;
	};

	/// Stream operator — utile pour LOG_INFO via spdlog (qui sait
	/// formater via operator<<). Délègue à `ToString`.
	std::ostream& operator<<(std::ostream& os, const ObjectGuid& g);

	/// Générateur thread-safe de counter monotone par type. Pas de
	/// recyclage : le counter est strictement croissant pour la durée
	/// de vie du shard. Au reboot, les compteurs repartent à 1.
	///
	/// Les clients doivent gérer la "stale guid" (un objet avec counter=N
	/// avant reboot ne réfère plus rien après reboot ; le serveur émet
	/// OBJECT_DESTROYED au logout puis OBJECT_CREATED avec un nouveau
	/// guid au respawn — cf. ticket §Notes).
	class ObjectGuidFactory
	{
	public:
		ObjectGuidFactory() = default;

		/// Génère un nouveau guid pour \p type avec entry=0.
		ObjectGuid Allocate(HighGuid type);

		/// Génère un nouveau guid pour \p type avec un entry explicite
		/// (Creature, GameObject : entry = template_id).
		ObjectGuid Allocate(HighGuid type, uint32_t entry);

		/// Reset des compteurs (utile en tests).
		void Reset();

	private:
		/// Counter par type. uint32_t — wrap-around à 4G (ne devrait
		/// jamais arriver en pratique pour la durée d'un shard).
		uint32_t m_counters[256] = {};
	};
}

namespace std
{
	template<>
	struct hash<engine::server::shard::ObjectGuid>
	{
		size_t operator()(const engine::server::shard::ObjectGuid& g) const noexcept
		{
			return std::hash<uint64_t>{}(g.Raw());
		}
	};
}
