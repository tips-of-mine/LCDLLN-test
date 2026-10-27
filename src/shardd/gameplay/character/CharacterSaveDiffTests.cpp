// ETAPE5.5 — tests des sauvegardes différentielles (sans base de données).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/gameplay/character/CharacterSaveDiff.h"

#include <cstdio>

namespace
{
	int s_failCount = 0;

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::fprintf(stderr, "[FAIL] %s\n", msg);
		}
	}

	using namespace engine::server;

	PersistedCharacterState Sample()
	{
		PersistedCharacterState s;
		s.characterKey = 42;
		s.zoneId = 3;
		s.positionMetersX = 10.0f;
		s.positionMetersY = 2.0f;
		s.positionMetersZ = -5.0f;
		s.level = 7;
		s.experiencePoints = 1200;
		s.gold = 50;
		s.inventory = { { 1001u, 3u }, { 1002u, 1u } };
		s.equipment[1] = 2001u;
		return s;
	}

	uint32_t Diff(const PersistedCharacterState& a, const PersistedCharacterState& b, float yawA = 0.f, float yawB = 0.f,
		const char* payloadA = "p", const char* payloadB = "p")
	{
		return DiffSaveBlocks(ComputeSaveBlockHashes(a, yawA, payloadA), ComputeSaveBlockHashes(b, yawB, payloadB));
	}
}

int main()
{
	// 1. Chaque mutation ne touche que son bloc.
	{
		const PersistedCharacterState base = Sample();
		Check(Diff(base, base) == 0u, "état identique : aucun bloc");
		PersistedCharacterState s = base;
		s.positionMetersX += 0.5f;
		Check(Diff(base, s) == kSaveBlockPosition, "déplacement : position seule");
		Check(Diff(base, base, 0.f, 1.f) == kSaveBlockPosition, "rotation : position seule");
		s = base;
		s.level = 8;
		Check(Diff(base, s) == kSaveBlockPosition, "niveau : bloc position (colonne characters.level)");
		s = base;
		s.experiencePoints += 10;
		Check(Diff(base, s) == kSaveBlockProgress, "XP : progression seule");
		Check(Diff(base, base, 0.f, 0.f, "p", "q") == kSaveBlockProgress, "charge utile (quêtes…) : progression seule");
		s = base;
		s.gold += 1;
		Check(Diff(base, s) == kSaveBlockWallet, "or : bourse seule");
		s = base;
		s.inventory[0].quantity = 4u; // modification en place
		Check(Diff(base, s) == kSaveBlockInventory, "quantité modifiée en place : sac seul");
		s = base;
		std::swap(s.inventory[0], s.inventory[1]);
		Check(Diff(base, s) == kSaveBlockInventory, "ordre des piles : sac (slot_index)");
		s = base;
		s.inventory.push_back({ 0u, 0u });
		Check(Diff(base, s) == kSaveBlockInventory, "pile vide ajoutée : sac");
		s = base;
		s.equipment[2] = 3001u;
		Check(Diff(base, s) == kSaveBlockEquipment, "équipement seul");
		s = base;
		s.gold += 5;
		s.inventory.pop_back();
		Check(Diff(base, s) == (kSaveBlockWallet | kSaveBlockInventory), "vente : bourse + sac");
	}

	// 2. Cache : complet au départ, puis différentiel, complet tous les N.
	{
		SavedBlockCache cache(4u);
		const PersistedCharacterState base = Sample();
		const SaveBlockHashes h0 = ComputeSaveBlockHashes(base, 0.f, "p");
		SavePlan p = cache.Plan(42u, h0);
		Check(p.blocks == kSaveBlocksAll, "inconnu : tout");
		cache.OnCommitted(42u, h0, kSaveBlocksAll, p.version);
		p = cache.Plan(42u, h0);
		Check(p.blocks == 0u, "rien n'a changé : aucun bloc");
		PersistedCharacterState s = base;
		s.gold = 99;
		const SaveBlockHashes h1 = ComputeSaveBlockHashes(s, 0.f, "p");
		p = cache.Plan(42u, h1);
		Check(p.blocks == kSaveBlockWallet, "bourse seule");
		cache.OnCommitted(42u, h1, p.blocks, p.version);   // 1re partielle
		p = cache.Plan(42u, h1);
		cache.OnCommitted(42u, h1, p.blocks, p.version);   // 2e partielle (rien)
		p = cache.Plan(42u, h1);
		Check(p.blocks == 0u, "3e sauvegarde : encore différentielle");
		cache.OnCommitted(42u, h1, p.blocks, p.version);   // 3e partielle
		p = cache.Plan(42u, h1);
		Check(p.blocks == kSaveBlocksAll, "4e sauvegarde : complète (fullEvery = 4)");
		cache.OnCommitted(42u, h1, p.blocks, p.version);
		Check(cache.Plan(42u, h1).blocks == 0u, "après la complète : de nouveau différentiel");
		cache.Invalidate(42u);
		Check(cache.Plan(42u, h1).blocks == kSaveBlocksAll, "invalidé : tout");
		cache.Forget(42u);
		Check(cache.Size() == 0u, "oublié");
	}

	// 2 bis. Deux sauvegardes concurrentes planifiées sur la même référence : la seconde
	// à valider en mémoire invalide (on ne sait plus laquelle est la dernière en base).
	{
		SavedBlockCache cache(20u);
		PersistedCharacterState a = Sample();
		const SaveBlockHashes h0 = ComputeSaveBlockHashes(a, 0.f, "p");
		SavePlan p0 = cache.Plan(1u, h0);
		cache.OnCommitted(1u, h0, kSaveBlocksAll, p0.version);
		a.gold = 1;
		const SaveBlockHashes hA = ComputeSaveBlockHashes(a, 0.f, "p");
		a.gold = 2;
		const SaveBlockHashes hB = ComputeSaveBlockHashes(a, 0.f, "p");
		const SavePlan pA = cache.Plan(1u, hA);
		const SavePlan pB = cache.Plan(1u, hB);
		cache.OnCommitted(1u, hB, pB.blocks, pB.version); // B validé en mémoire d'abord
		cache.OnCommitted(1u, hA, pA.blocks, pA.version); // A ensuite : jeton périmé
		Check(cache.Plan(1u, hA).blocks == kSaveBlocksAll && cache.Plan(1u, hB).blocks == kSaveBlocksAll,
			"écritures concurrentes : référence invalidée, prochaine sauvegarde complète");
		// Invalidation pendant une sauvegarde planifiée : son COMMIT ne réinstalle rien.
		const SavePlan pC = cache.Plan(1u, hA);
		cache.OnCommitted(1u, hA, pC.blocks, pC.version); // complète : référence = hA
		const SavePlan pD = cache.Plan(1u, hB);
		cache.Invalidate(1u);
		cache.OnCommitted(1u, hB, pD.blocks, pD.version);
		Check(cache.Plan(1u, hB).blocks == kSaveBlocksAll, "invalidation pendant la sauvegarde : reste invalidé");
	}

	// 3. fullEvery = 1 désactive le différentiel ; bornes.
	{
		SavedBlockCache always(1u);
		const SaveBlockHashes h = ComputeSaveBlockHashes(Sample(), 0.f, "p");
		always.OnCommitted(1u, h, kSaveBlocksAll, always.Plan(1u, h).version);
		Check(always.Plan(1u, h).blocks == kSaveBlocksAll, "fullEvery = 1 : toujours complet");
		SavedBlockCache bounded(0u);
		Check(bounded.FullEvery() == 1u, "fullEvery borné à 1");
		bounded.SetFullEvery(100000u);
		Check(bounded.FullEvery() == 1000u, "fullEvery borné à 1000");
		Check(CountSaveBlocks(kSaveBlocksAll) == 5u && CountSaveBlocks(kSaveBlockWallet | kSaveBlockEquipment) == 2u, "comptage");
	}

	if (s_failCount == 0)
		std::puts("[OK] character_save_diff_tests");
	return s_failCount == 0 ? 0 : 1;
}
