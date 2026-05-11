#include "src/shardd/internals/entities/ObjectGuid.h"

#include <array>
#include <cstdio>

namespace engine::server::shard
{
	const char* HighGuidLabel(HighGuid t) noexcept
	{
		switch (t)
		{
			case HighGuid::None:       return "None";
			case HighGuid::Player:     return "Player";
			case HighGuid::Creature:   return "Creature";
			case HighGuid::GameObject: return "GameObject";
			case HighGuid::Item:       return "Item";
			case HighGuid::Corpse:     return "Corpse";
			case HighGuid::Pet:        return "Pet";
			case HighGuid::DynObject:  return "DynObject";
			case HighGuid::Vehicle:    return "Vehicle";
		}
		return "?";
	}

	std::string ObjectGuid::ToString() const
	{
		char buf[64];
		const auto* label = HighGuidLabel(Type());
		const uint32_t e = Entry();
		const uint32_t c = Counter();
		if (e == 0)
			std::snprintf(buf, sizeof(buf), "{%s c=%u}", label, c);
		else
			std::snprintf(buf, sizeof(buf), "{%s/%u c=%u}", label, e, c);
		return std::string(buf);
	}

	std::ostream& operator<<(std::ostream& os, const ObjectGuid& g)
	{
		return os << g.ToString();
	}

	ObjectGuid ObjectGuidFactory::Allocate(HighGuid type)
	{
		return Allocate(type, 0);
	}

	ObjectGuid ObjectGuidFactory::Allocate(HighGuid type, uint32_t entry)
	{
		if (type == HighGuid::None)
			return ObjectGuid{};
		const auto idx = static_cast<size_t>(type);
		// Compteur strictement croissant ; on incrémente AVANT pour ne
		// jamais retourner counter=0 (réservé au "guid invalide").
		const uint32_t c = ++m_counters[idx];
		return ObjectGuid(type, entry, c);
	}

	void ObjectGuidFactory::Reset()
	{
		for (auto& v : m_counters)
			v = 0;
	}
}
