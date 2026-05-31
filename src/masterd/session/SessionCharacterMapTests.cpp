/**
 * Phase 4 chat — Unit tests for SessionCharacterMap.
 * Pure in-memory, no DB, no network. Returns 0 on success, non-zero on first failure.
 */

#include "src/masterd/session/SessionCharacterMap.h"
#include "src/masterd/account/AccountRole.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

	bool Contains(const std::vector<uint64_t>& v, uint64_t value)
	{
		return std::find(v.begin(), v.end(), value) != v.end();
	}
}

using engine::server::SessionCharacterMap;
using engine::server::AccountRole;

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
	m.Set(42u, 5001ull, 1001ull, "Alyx", "alyx", AccountRole::GameMaster);

	auto byConn = m.GetByConnId(42u);
	Assert(byConn.has_value(), "GetByConnId returns set value");
	if (byConn)
	{
		Assert(byConn->accountId == 5001ull, "accountId round-trips");
		Assert(byConn->characterId == 1001ull, "characterId round-trips");
		Assert(byConn->characterName == "Alyx", "characterName round-trips");
		Assert(byConn->normalizedName == "alyx", "normalizedName round-trips");
		Assert(byConn->role == AccountRole::GameMaster, "role round-trips");
	}

	auto byName = m.FindConnByNormalizedName("alyx");
	Assert(byName.has_value() && *byName == 42u, "FindConnByNormalizedName returns connId");

	auto absent = m.FindConnByNormalizedName("bob");
	Assert(!absent.has_value(), "FindConnByNormalizedName missing returns nullopt");
}

static void TestRemoveCleansBothMaps()
{
	SessionCharacterMap m;
	m.Set(7u, 5007ull, 999ull, "Alyx", "alyx", AccountRole::Player);
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
	m.Set(11u, 1100ull, 100ull, "Alyx", "alyx", AccountRole::Player);
	m.Set(11u, 1100ull, 200ull, "Bob", "bob", AccountRole::Player);

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
	m.Set(1u, 101ull, 100ull, "Alyx", "alyx", AccountRole::Player);
	m.Set(2u, 102ull, 200ull, "Bob",  "bob", AccountRole::Player);

	auto a = m.FindConnByNormalizedName("alyx");
	auto b = m.FindConnByNormalizedName("bob");
	Assert(a && *a == 1u, "First conn resolves correctly");
	Assert(b && *b == 2u, "Second conn resolves correctly");

	m.Remove(1u);
	Assert(!m.FindConnByNormalizedName("alyx").has_value(), "Remove conn 1 cleans 'alyx'");
	Assert(m.FindConnByNormalizedName("bob").has_value(), "Conn 2 unchanged after Remove(1)");
}

static void TestCountAndCountByRole()
{
	SessionCharacterMap m;
	Assert(m.Count() == 0u, "Count empty == 0");

	m.Set(1u, 201ull, 100ull, "Alyx", "alyx", AccountRole::Player);
	m.Set(2u, 202ull, 200ull, "Bob", "bob", AccountRole::Player);
	m.Set(3u, 203ull, 300ull, "Mod", "mod", AccountRole::Moderator);
	m.Set(4u, 204ull, 400ull, "Gm", "gm", AccountRole::GameMaster);
	m.Set(5u, 205ull, 500ull, "Admin", "admin", AccountRole::Administrator);

	Assert(m.Count() == 5u, "Count == 5 after 5 Set");

	auto rc = m.CountByRole();
	Assert(rc.player == 2u, "CountByRole player == 2");
	Assert(rc.moderator == 1u, "CountByRole moderator == 1");
	Assert(rc.game_master == 1u, "CountByRole game_master == 1");
	Assert(rc.administrator == 1u, "CountByRole administrator == 1");
	Assert(rc.player + rc.moderator + rc.game_master + rc.administrator == m.Count(),
		"CountByRole sums to Count");

	// Une mise à jour du même connId vers un autre rôle ne double-compte pas.
	m.Set(3u, 203ull, 300ull, "Mod", "mod", AccountRole::Administrator);
	auto rc2 = m.CountByRole();
	Assert(rc2.moderator == 0u, "CountByRole moderator == 0 after role change");
	Assert(rc2.administrator == 2u, "CountByRole administrator == 2 after role change");
	Assert(m.Count() == 5u, "Count still 5 after role change");

	m.Remove(1u);
	auto rc3 = m.CountByRole();
	Assert(rc3.player == 1u, "CountByRole player == 1 after Remove");
	Assert(m.Count() == 4u, "Count == 4 after Remove");
}

static void TestListInWorldAccountIds()
{
	SessionCharacterMap m;
	Assert(m.ListInWorldAccountIds().empty(), "ListInWorldAccountIds empty == {}");

	m.Set(1u, 201ull, 100ull, "Alyx", "alyx", AccountRole::Player);
	m.Set(2u, 202ull, 200ull, "Bob", "bob", AccountRole::GameMaster);

	auto ids = m.ListInWorldAccountIds();
	Assert(ids.size() == 2u, "ListInWorldAccountIds size == 2");
	Assert(Contains(ids, 201ull), "ListInWorldAccountIds contains 201");
	Assert(Contains(ids, 202ull), "ListInWorldAccountIds contains 202");

	// Le même compte sur deux connId (cas théorique) ne doit apparaître qu'une fois.
	m.Set(3u, 201ull, 300ull, "Alyx2", "alyx2", AccountRole::Player);
	auto idsDedup = m.ListInWorldAccountIds();
	Assert(idsDedup.size() == 2u, "ListInWorldAccountIds dedup same account");
	Assert(std::count(idsDedup.begin(), idsDedup.end(), 201ull) == 1, "account 201 appears once");

	m.Remove(2u);
	auto idsAfter = m.ListInWorldAccountIds();
	Assert(!Contains(idsAfter, 202ull), "ListInWorldAccountIds drops removed account");
	Assert(Contains(idsAfter, 201ull), "ListInWorldAccountIds keeps account 201 (still on conn 1/3)");
}

int main()
{
	TestNormalize();
	TestSetAndLookup();
	TestRemoveCleansBothMaps();
	TestUpdateOldNameIsDropped();
	TestTwoConnectionsDifferentNames();
	TestCountAndCountByRole();
	TestListInWorldAccountIds();
	std::cerr << (s_failCount == 0 ? "[OK] all session_character_map tests passed\n" : "[FAIL] some tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
