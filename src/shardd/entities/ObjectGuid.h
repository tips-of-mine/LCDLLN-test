#pragma once
// ObjectGuid : identifiant 64-bit pour tous les objets MMO (player, creature,
// gameobject, item, etc.). Encodage : [type 8 bits | id 56 bits].
// Header-only, deterministe, partage par master/shardd/client (wire format).
//
// Wave 7 foundation : type minimal independant de l'ObjectGuid existant
// dans engine::server::shard (qui a un layout 3 champs type/entry/counter
// incompatible avec l'API delta replication WoW-like ciblee ici). Les deux
// vivent en parallele : la migration progressive sera faite par PRs futures.
//
// Pas d'integration dans les payloads existants, par design : Wave 7 ne livre
// QUE les briques de base + leurs tests, l'integration viendra plus tard.

#include <cstdint>
#include <string>

namespace engine::server::entities
{
	/// Type d'objet encode dans les 8 bits de poids fort de ObjectGuid.
	/// Valeurs stables : ne JAMAIS reassigner (le wire en depend).
	enum class ObjectType : uint8_t
	{
		None        = 0,
		Player      = 1,
		Creature    = 2,
		GameObject  = 3,
		Item        = 4,
		Container   = 5,
		Corpse      = 6,
		DynamicObject = 7,
		Pet         = 8,
		Vehicle     = 9,
		// Reserve : 10..255 pour futurs types.
	};

	/// Identifiant 64-bit type-safe. Encode :
	///   - bits [56..63] : ObjectType (8 bits)
	///   - bits [0..55]  : id local au type (56 bits, soit ~7.2e16 valeurs)
	class ObjectGuid
	{
	public:
		/// Construit un guid invalide (raw=0, Type=None, Id=0).
		constexpr ObjectGuid() noexcept = default;

		/// Construit depuis la representation brute (sortie d'un Raw() precedent).
		/// Pas de validation : caller garantit la valeur.
		constexpr explicit ObjectGuid(uint64_t raw) noexcept : m_raw(raw) {}

		/// Construit depuis un type + un id 56-bit. Si \p id depasse 56 bits,
		/// les bits hauts sont silencieusement tronques (clipping kIdMask).
		constexpr ObjectGuid(ObjectType type, uint64_t id) noexcept
			: m_raw((static_cast<uint64_t>(type) << 56) | (id & kIdMask)) {}

		/// Representation brute, utilisable pour serialisation reseau / DB.
		constexpr uint64_t Raw() const noexcept { return m_raw; }

		/// Extrait le type depuis les 8 bits de poids fort.
		constexpr ObjectType Type() const noexcept { return static_cast<ObjectType>(m_raw >> 56); }

		/// Extrait l'id (56 bits) depuis les bits de poids faible.
		constexpr uint64_t Id() const noexcept { return m_raw & kIdMask; }

		/// True si raw=0 (guid construit par defaut ou explicitement invalide).
		constexpr bool IsEmpty() const noexcept { return m_raw == 0; }

		constexpr bool operator==(const ObjectGuid& o) const noexcept { return m_raw == o.m_raw; }
		constexpr bool operator!=(const ObjectGuid& o) const noexcept { return m_raw != o.m_raw; }
		constexpr bool operator<(const ObjectGuid& o)  const noexcept { return m_raw < o.m_raw; }

		/// Masque des 56 bits d'id. Expose en public pour les tests / serialisation.
		static constexpr uint64_t kIdMask = 0x00FFFFFFFFFFFFFFull;

	private:
		uint64_t m_raw = 0;
	};

	/// Helper : convertit ObjectType vers string (logging/debug). Jamais nullptr.
	/// Retourne "Unknown" pour les valeurs non listees.
	inline const char* ObjectTypeToString(ObjectType t) noexcept
	{
		switch (t)
		{
			case ObjectType::None:          return "None";
			case ObjectType::Player:        return "Player";
			case ObjectType::Creature:      return "Creature";
			case ObjectType::GameObject:    return "GameObject";
			case ObjectType::Item:          return "Item";
			case ObjectType::Container:     return "Container";
			case ObjectType::Corpse:        return "Corpse";
			case ObjectType::DynamicObject: return "DynamicObject";
			case ObjectType::Pet:           return "Pet";
			case ObjectType::Vehicle:       return "Vehicle";
		}
		return "Unknown";
	}

	/// Format lisible : "Player#1234" / "Creature#56" / "Empty" si raw=0.
	inline std::string FormatGuid(ObjectGuid g)
	{
		if (g.IsEmpty()) return "Empty";
		std::string out = ObjectTypeToString(g.Type());
		out.push_back('#');
		out += std::to_string(g.Id());
		return out;
	}
}

namespace std
{
	template<>
	struct hash<engine::server::entities::ObjectGuid>
	{
		size_t operator()(const engine::server::entities::ObjectGuid& g) const noexcept
		{
			return std::hash<uint64_t>{}(g.Raw());
		}
	};
}
