/**
 * Phase 4 chat — Unit tests for SessionCharacterMap.
 * Pure in-memory, no DB, no network. Returns 0 on success, non-zero on first failure.
 */

#include "engine/server/SessionCharacterMap.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	static int s_failCount = 0;
	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

using engine::server::SessionCharacterMap;

static void TestNormalize()
{
	Assert(SessionCharacterMap::Normalize("Alyx") == "alyx", "Normalize ASCII upper -> lower");
	Assert(SessionCharacterMap::Normalize("alyx") == "alyx", "Normalize ASCII lower idempotent");
	Assert(SessionCharacterMap::Normalize("ALYX") == "alyx", "Normalize all caps");
	Assert(SessionCharacterMap::Normalize("") == "", "Normalize empty");
	// Non-ASCII bytes are left untouched (byte-equality match for accented names).
	const std::string accented = "Al\xc3\xa9x"; // "Alèx" en UTF-8
	Assert(SessionCharacterMap::Normalize(accented) == "al\xc3\xa9x", "Normalize preserves non-ASCII bytes");
}

static void TestSetAndLookup()
{
	SessionCharacterMap m;
	m.Set(42u, 1001ull, "Alyx", "alyx");

	auto byConn = m.GetByConnId(42u);
	Assert(byConn.has_value(), "GetByConnId returns set value");
	if (byConn)
	{
		Assert(byConn->characterId == 1001ull, "characterId round-trips");
		Assert(byConn->characterName == "Alyx", "characterName round-trips");
		Assert(byConn->normalizedName == "alyx", "normalizedName round-trips");
	}

	auto byName = m.FindConnByNormalizedName("alyx");
	Assert(byName.has_value() && *byName == 42u, "FindConnByNormalizedName returns connId");

	auto absent = m.FindConnByNormalizedName("bob");
	Assert(!absent.has_value(), "FindConnByNormalizedName missing returns nullopt");
}

static void TestRemoveCleansBothMaps()
{
	SessionCharacterMap m;
	m.Set(7u, 999ull, "Alyx", "alyx");
	m.Remove(7u);

	Assert(!m.GetByConnId(7u).has_value(), "Remove drops connId binding");
	Assert(!m.FindConnByNormalizedName("alyx").has_value(),
		"Remove drops reverse normalized_name binding");
}

static void TestUpdateOldNameIsDropped()
{
	// Si un client appelle Set deux fois avec un nouveau nom (ex. changement de
	// perso post-logout/re-login sur la même connexion), l'ancien nom doit
	// disparaître du whisper directory.
	SessionCharacterMap m;
	m.Set(11u, 100ull, "Alyx", "alyx");
	m.Set(11u, 200ull, "Bob", "bob");

	auto byConn = m.GetByConnId(11u);
	Assert(byConn && byConn->characterName == "Bob", "Update overwrites characterName");
	Assert(byConn && byConn->characterId == 200ull, "Update overwrites characterId");
	Assert(!m.FindConnByNormalizedName("alyx").has_value(),
		"Old normalized name no longer resolves");
	auto byNewName = m.FindConnByNormalizedName("bob");
	Assert(byNewName && *byNewName == 11u, "New normalized name resolves to same connId");
}

static void TestTwoConnectionsDifferentNames()
{
	SessionCharacterMap m;
	m.Set(1u, 100ull, "Alyx", "alyx");
	m.Set(2u, 200ull, "Bob",  "bob");

	auto a = m.FindConnByNormalizedName("alyx");
	auto b = m.FindConnByNormalizedName("bob");
	Assert(a && *a == 1u, "First conn resolves correctly");
	Assert(b && *b == 2u, "Second conn resolves correctly");

	m.Remove(1u);
	Assert(!m.FindConnByNormalizedName("alyx").has_value(), "Remove conn 1 cleans 'alyx'");
	Assert(m.FindConnByNormalizedName("bob").has_value(), "Conn 2 unchanged after Remove(1)");
}

int main()
{
	TestNormalize();
	TestSetAndLookup();
	TestRemoveCleansBothMaps();
	TestUpdateOldNameIsDropped();
	TestTwoConnectionsDifferentNames();
	std::cerr << (s_failCount == 0 ? "[OK] all session_character_map tests passed\n" : "[FAIL] some tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
