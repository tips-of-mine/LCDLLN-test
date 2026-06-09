// Wave 17 entities tests : Player (accountId, characterId, name, xp + heritage Unit).
// Pattern aligne sur ObjectGuidTests.cpp : asserts + printf, pas de framework.
// Cible CTest : player_tests (cf. src/CMakeLists.txt).

#include "src/shardd/entities/Player.h"
#include "src/shardd/gameplay/character/CharacterStatsEngine.h"
#include "src/shardd/gameplay/character/CharacterStatsTables.h"

#include "CharacterStatsData.h"  // kCharacterStatsJson (généré)
#include "FactionsData.h"        // kFactionsJson (généré)

#include <cassert>
#include <cstdio>
#include <string>

using namespace engine::server::entities;

namespace
{
	/// Construction : guid, accountId, characterId, name preserves.
	void TestPlayerConstruction()
	{
		ObjectGuid g(ObjectType::Player, 42);
		Player p(g, /*accountId*/1001, /*characterId*/42, /*name*/"TestHero");
		assert(p.Guid() == g);
		assert(p.GetAccountId() == 1001);
		assert(p.GetCharacterId() == 42);
		assert(p.GetName() == "TestHero");
		std::puts("[OK] TestPlayerConstruction");
	}

	/// Name : immuable apres construction (pas de SetName). Verification
	/// via API : seul GetName() existe.
	void TestPlayerNameImmutable()
	{
		ObjectGuid g(ObjectType::Player, 1);
		Player p(g, 1, 1, "Alpha");
		const std::string& n = p.GetName();
		assert(n == "Alpha");
		// Pas d'API SetName : compile-time invariant.
		std::puts("[OK] TestPlayerNameImmutable");
	}

	/// Account/Character ids sont MarkDirty() a la construction : la premiere
	/// replication les envoie au client.
	void TestPlayerIdsMarkedDirtyAtConstruction()
	{
		ObjectGuid g(ObjectType::Player, 9);
		Player p(g, 1234, 5678, "DirtyTest");
		assert(p.IsDirty());
		assert(p.Mask().TestBit(kPlayerFieldAccountId));
		assert(p.Mask().TestBit(kPlayerFieldCharacterId));
		// XP n'a pas ete touche.
		assert(!p.Mask().TestBit(kPlayerFieldXp));
		std::puts("[OK] TestPlayerIdsMarkedDirtyAtConstruction");
	}

	/// SetXp marque le bit XP, sans toucher aux autres.
	void TestPlayerXp()
	{
		ObjectGuid g(ObjectType::Player, 2);
		Player p(g, 1, 2, "Beta");
		p.OnReplicationSent(); // reset mask post-construction (clear account/char marks)
		p.SetXp(12345);
		assert(p.GetXp() == 12345);
		assert(p.Mask().TestBit(kPlayerFieldXp));
		// Seul XP dirty, pas account/char.
		assert(p.Mask().PopCount() == 1);
		std::puts("[OK] TestPlayerXp");
	}

	/// Player herite de Unit : HP, level, faction fonctionnent.
	void TestPlayerHeriteUnit()
	{
		ObjectGuid g(ObjectType::Player, 3);
		Player p(g, 1, 3, "Gamma");
		p.SetMaxHealth(5000);
		p.SetHealth(3000);
		p.SetLevel(60);
		p.SetFaction(1); // exemple faction joueur
		assert(p.GetHealth() == 3000);
		assert(p.GetLevel() == 60);
		assert(p.GetFaction() == 1);
		assert(p.IsAlive());
		std::puts("[OK] TestPlayerHeriteUnit");
	}

	/// Player herite de WorldObject : position fonctionne.
	void TestPlayerHeriteWorldObject()
	{
		ObjectGuid g(ObjectType::Player, 4);
		Player p(g, 1, 4, "Delta");
		p.SetPosition(100.0f, 200.0f, 30.0f, 1.0f);
		p.SetMapId(1);
		assert(p.GetPosX() == 100.0f);
		assert(p.GetMapId() == 1);
		std::puts("[OK] TestPlayerHeriteWorldObject");
	}

	/// Câblage moteur de stats -> UpdateField via Player::ApplyDerivedStats.
	/// Voleur Ténébreux Elfe Femme niv.1 -> hp attendu 81 (ancre vérifiée dans
	/// CharacterStatsEngineTests : 100 * 0.90 * 0.90 = 81). Renvoie un bool et
	/// N'UTILISE PAS assert (désactivé sous NDEBUG en CI Release) : le résultat
	/// est propagé à main() qui sort en code non nul en cas d'échec.
	bool TestApplyDerivedStats()
	{
		using namespace engine::server::gameplay;

		auto tables = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!tables) { std::fprintf(stderr, "[FAIL] TestApplyDerivedStats: FromEmbedded\n"); return false; }

		ObjectGuid g(ObjectType::Player, 7);
		Player p(g, /*accountId*/2002, /*characterId*/7, /*name*/"Ombre");
		p.SetLevel(1);
		if (!p.ApplyDerivedStats(*tables, "elfe", "voleur_tenebreux", Sex::Female))
		{
			std::fprintf(stderr, "[FAIL] TestApplyDerivedStats: ApplyDerivedStats returned false\n");
			return false;
		}
		if (p.GetMaxHealth() != 81u)
		{
			std::fprintf(stderr, "[FAIL] TestApplyDerivedStats: maxHealth=%u (attendu 81)\n", p.GetMaxHealth());
			return false;
		}
		if (p.GetHealth() != 81u)
		{
			std::fprintf(stderr, "[FAIL] TestApplyDerivedStats: health=%u (attendu 81)\n", p.GetHealth());
			return false;
		}
		if (p.GetSecondaryResource() != p.GetMaxSecondaryResource())
		{
			std::fprintf(stderr, "[FAIL] TestApplyDerivedStats: resource %u != max %u\n",
			             p.GetSecondaryResource(), p.GetMaxSecondaryResource());
			return false;
		}
		if (p.GetMaxSecondaryResource() == 0u)
		{
			std::fprintf(stderr, "[FAIL] TestApplyDerivedStats: maxSecondaryResource == 0\n");
			return false;
		}
		// Stat inconnue -> false, sans modifier l'état déjà appliqué.
		if (p.ApplyDerivedStats(*tables, "faction_inexistante", "classe_bidon", Sex::Male))
		{
			std::fprintf(stderr, "[FAIL] TestApplyDerivedStats: faction inconnue aurait dû renvoyer false\n");
			return false;
		}
		std::puts("[OK] TestApplyDerivedStats");
		return true;
	}
}

int main()
{
	TestPlayerConstruction();
	TestPlayerNameImmutable();
	TestPlayerIdsMarkedDirtyAtConstruction();
	TestPlayerXp();
	TestPlayerHeriteUnit();
	TestPlayerHeriteWorldObject();
	// Test robuste sous NDEBUG : échec signalé par le code de sortie process.
	if (!TestApplyDerivedStats())
		return 1;
	std::puts("All Player tests passed");
	return 0;
}
