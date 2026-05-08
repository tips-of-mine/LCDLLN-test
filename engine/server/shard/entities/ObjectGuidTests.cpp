// CMANGOS.02 (Phase 2.02a) — Tests ObjectGuid.
// Pure : aucune dépendance externe.

#include "engine/server/shard/entities/ObjectGuid.h"
#include "engine/core/Log.h"

#include <unordered_set>

namespace
{
	using engine::server::shard::HighGuid;
	using engine::server::shard::ObjectGuid;
	using engine::server::shard::ObjectGuidFactory;

	bool TestDefaultIsInvalid()
	{
		ObjectGuid g;
		if (g.IsValid() || g.Type() != HighGuid::None || g.Counter() != 0)
		{
			LOG_ERROR(Core, "[ObjectGuidTests] default guid should be invalid");
			return false;
		}
		LOG_INFO(Core, "[ObjectGuidTests] default invalid OK");
		return true;
	}

	bool TestPackUnpack()
	{
		const ObjectGuid g(HighGuid::Creature, 4242, 17);
		if (g.Type() != HighGuid::Creature) return false;
		if (g.Entry() != 4242) return false;
		if (g.Counter() != 17) return false;
		if (!g.IsValid()) return false;
		if (!g.IsCreature() || g.IsPlayer()) return false;
		LOG_INFO(Core, "[ObjectGuidTests] pack/unpack OK");
		return true;
	}

	bool TestEntryBoundaries()
	{
		// Entry 24-bit max = 0x00FFFFFF (16777215). Au-delà → tronqué.
		const uint32_t maxEntry = 0x00FF'FFFFu;
		ObjectGuid g(HighGuid::GameObject, maxEntry, 1);
		if (g.Entry() != maxEntry) return false;

		// Vérification : entry sur 25 bits → tronquée à 24 bits.
		ObjectGuid g2(HighGuid::GameObject, 0x01FF'FFFFu, 1);
		if (g2.Entry() != maxEntry)
		{
			LOG_ERROR(Core, "[ObjectGuidTests] entry overflow not masked, got 0x{:x}", g2.Entry());
			return false;
		}
		LOG_INFO(Core, "[ObjectGuidTests] entry boundaries OK");
		return true;
	}

	bool TestRawRoundTrip()
	{
		const ObjectGuid g(HighGuid::Player, 0, 1234567);
		const uint64_t raw = g.Raw();
		const ObjectGuid g2 = ObjectGuid::FromRaw(raw);
		if (g != g2) return false;
		LOG_INFO(Core, "[ObjectGuidTests] Raw roundtrip OK");
		return true;
	}

	bool TestEquality()
	{
		const ObjectGuid a(HighGuid::Creature, 1, 1);
		const ObjectGuid b(HighGuid::Creature, 1, 1);
		const ObjectGuid c(HighGuid::Creature, 1, 2);
		if (!(a == b) || a != b) return false;
		if (a == c) return false;
		// Ordering (<=>) : pour qu'on puisse les ranger dans un std::set.
		if (!(a < c) && !(c < a)) return false;
		LOG_INFO(Core, "[ObjectGuidTests] equality + ordering OK");
		return true;
	}

	bool TestHashable()
	{
		std::unordered_set<ObjectGuid> set;
		set.insert(ObjectGuid(HighGuid::Player, 0, 1));
		set.insert(ObjectGuid(HighGuid::Player, 0, 1));  // dup
		set.insert(ObjectGuid(HighGuid::Creature, 42, 1));
		if (set.size() != 2)
		{
			LOG_ERROR(Core, "[ObjectGuidTests] expected 2 distinct, got {}", set.size());
			return false;
		}
		LOG_INFO(Core, "[ObjectGuidTests] hashable OK");
		return true;
	}

	bool TestFactoryMonotonic()
	{
		ObjectGuidFactory f;
		const auto g1 = f.Allocate(HighGuid::Player);
		const auto g2 = f.Allocate(HighGuid::Player);
		const auto g3 = f.Allocate(HighGuid::Player);
		if (g1.Counter() != 1 || g2.Counter() != 2 || g3.Counter() != 3)
		{
			LOG_ERROR(Core, "[ObjectGuidTests] factory non-monotonic ({},{},{})",
				g1.Counter(), g2.Counter(), g3.Counter());
			return false;
		}
		// Counter never zero (réservé invalide).
		if (g1.Counter() == 0) return false;
		LOG_INFO(Core, "[ObjectGuidTests] factory monotonic OK");
		return true;
	}

	bool TestFactoryPerType()
	{
		ObjectGuidFactory f;
		const auto p1 = f.Allocate(HighGuid::Player);
		const auto c1 = f.Allocate(HighGuid::Creature, 42);
		const auto p2 = f.Allocate(HighGuid::Player);
		const auto c2 = f.Allocate(HighGuid::Creature, 42);

		// Player : 1, 2 ; Creature : 1, 2 (counters indépendants).
		if (p1.Counter() != 1 || p2.Counter() != 2) return false;
		if (c1.Counter() != 1 || c2.Counter() != 2) return false;
		// Le entry est bien transporté.
		if (c1.Entry() != 42 || c2.Entry() != 42) return false;
		LOG_INFO(Core, "[ObjectGuidTests] factory per-type counters OK");
		return true;
	}

	bool TestFactoryNoneReturnsInvalid()
	{
		ObjectGuidFactory f;
		const auto g = f.Allocate(HighGuid::None);
		if (g.IsValid())
		{
			LOG_ERROR(Core, "[ObjectGuidTests] factory should return invalid for HighGuid::None");
			return false;
		}
		LOG_INFO(Core, "[ObjectGuidTests] factory None invalid OK");
		return true;
	}

	bool TestToString()
	{
		const ObjectGuid g(HighGuid::Creature, 4242, 17);
		const auto s = g.ToString();
		if (s.find("Creature") == std::string::npos
			|| s.find("4242") == std::string::npos
			|| s.find("17") == std::string::npos)
		{
			LOG_ERROR(Core, "[ObjectGuidTests] ToString missing fields : '{}'", s);
			return false;
		}
		LOG_INFO(Core, "[ObjectGuidTests] ToString OK ('{}')", s);
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestDefaultIsInvalid()
		&& TestPackUnpack()
		&& TestEntryBoundaries()
		&& TestRawRoundTrip()
		&& TestEquality()
		&& TestHashable()
		&& TestFactoryMonotonic()
		&& TestFactoryPerType()
		&& TestFactoryNoneReturnsInvalid()
		&& TestToString();

	if (ok)
		LOG_INFO(Core, "[ObjectGuidTests] ALL OK");
	else
		LOG_ERROR(Core, "[ObjectGuidTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
