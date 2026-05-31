/**
 * Tests unitaires — ShardPresenceService (présence shard-locale, Niveau 1).
 * Pur (pas de réseau/DB). Retourne 0 si OK, non-zéro sinon.
 */

#include "src/shardd/world/ShardPresenceService.h"

#include <cstdlib>
#include <iostream>

using engine::server::ShardPresenceService;
using engine::server::PresenceStatus;

namespace
{
	int s_failCount = 0;
	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

static void TestOnlineOfflineAndGet()
{
	ShardPresenceService svc;
	Assert(!svc.IsOnline(1), "inconnu => hors ligne");
	Assert(svc.GetStatus(1) == PresenceStatus::Offline, "statut inconnu => Offline");

	svc.SetOnline(1, 100, "Alyx", 7, 42, PresenceStatus::Online);
	Assert(svc.IsOnline(1), "online apres SetOnline");
	auto e = svc.Get(1);
	Assert(e.has_value(), "Get renvoie l'entree");
	if (e)
	{
		Assert(e->characterId == 100 && e->characterName == "Alyx" && e->level == 7 && e->zoneId == 42,
			"champs round-trip");
		Assert(e->status == PresenceStatus::Online, "statut round-trip");
	}

	svc.SetOffline(1);
	Assert(!svc.IsOnline(1), "hors ligne apres SetOffline");
	Assert(!svc.Get(1).has_value(), "Get nullopt apres SetOffline");
}

static void TestUpdateZoneLevel()
{
	ShardPresenceService svc;
	svc.SetOnline(2, 200, "Bob", 1, 1, PresenceStatus::Online);
	svc.UpdateZone(2, 99);
	svc.UpdateLevel(2, 12);
	auto e = svc.Get(2);
	Assert(e && e->zoneId == 99, "UpdateZone applique");
	Assert(e && e->level == 12, "UpdateLevel applique");
	// no-op sur compte inconnu
	svc.UpdateZone(999, 5);
	Assert(!svc.IsOnline(999), "UpdateZone inconnu = no-op");
}

static void TestOnlineAmongAndSnapshot()
{
	ShardPresenceService svc;
	svc.SetOnline(1, 100, "A", 1, 1);
	svc.SetOnline(3, 300, "C", 1, 1);
	auto online = svc.OnlineAccountIdsAmong({ 1, 2, 3, 4 });
	Assert(online.size() == 2, "OnlineAccountIdsAmong filtre (2/4)");
	bool has1 = false, has3 = false;
	for (uint64_t id : online) { if (id == 1) has1 = true; if (id == 3) has3 = true; }
	Assert(has1 && has3, "OnlineAccountIdsAmong contient 1 et 3");
	Assert(svc.Snapshot().size() == 2, "Snapshot = 2 entrees");

	// SetOnline du meme compte ne double-compte pas (remplace).
	svc.SetOnline(1, 101, "A2", 2, 7);
	Assert(svc.Snapshot().size() == 2, "re-SetOnline ne double-compte pas");
	auto e = svc.Get(1);
	Assert(e && e->characterId == 101 && e->zoneId == 7, "re-SetOnline remplace l'entree");
}

static void TestCharacterIndex()
{
	ShardPresenceService svc;
	svc.SetOnline(/*account*/10, /*character*/1000, "Alyx", 5, 3);
	Assert(svc.IsCharacterOnline(1000), "IsCharacterOnline true pour perso connecté");
	Assert(!svc.IsCharacterOnline(9999), "IsCharacterOnline false pour perso inconnu");
	Assert(svc.GetStatusByCharacter(1000) == PresenceStatus::Online, "GetStatusByCharacter Online");
	Assert(svc.GetStatusByCharacter(9999) == PresenceStatus::Offline, "GetStatusByCharacter inconnu => Offline");

	auto online = svc.OnlineCharacterIdsAmong({ 1000, 2000 });
	Assert(online.size() == 1 && online[0] == 1000, "OnlineCharacterIdsAmong filtre par perso");

	// SetOffline purge aussi l'index character_id.
	svc.SetOffline(10);
	Assert(!svc.IsCharacterOnline(1000), "SetOffline purge l'index character");

	// Changement de perso pour le même compte : l'ancien character sort de l'index.
	svc.SetOnline(10, 1000, "Alyx", 5, 3);
	svc.SetOnline(10, 1001, "Alyx2", 6, 4);
	Assert(!svc.IsCharacterOnline(1000), "ancien character purgé apres changement");
	Assert(svc.IsCharacterOnline(1001), "nouveau character indexé");
}

int main()
{
	TestOnlineOfflineAndGet();
	TestUpdateZoneLevel();
	TestOnlineAmongAndSnapshot();
	TestCharacterIndex();
	std::cerr << (s_failCount == 0 ? "[OK] all shard_presence_service tests passed\n" : "[FAIL] some tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
