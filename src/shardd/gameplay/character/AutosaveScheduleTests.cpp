// ETAPE2.3 — tests des sauvegardes automatiques étalées.
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/gameplay/character/AutosaveSchedule.h"

#include <cstdio>
#include <vector>

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
}

int main()
{
	using engine::server::IsCharacterAutosaveDue;
	constexpr uint32_t kInterval = 600; // 30 s à 20 Hz

	// 1. Exactement une sauvegarde par période pour chaque personnage.
	for (uint64_t id : { 1ull, 599ull, 600ull, 601ull, 123456789ull })
	{
		int count = 0;
		for (uint64_t tick = 1000; tick < 1000 + kInterval; ++tick)
		{
			if (IsCharacterAutosaveDue(id, tick, kInterval))
				++count;
		}
		Check(count == 1, "une sauvegarde par période");
	}

	// 2. Répartition : 6000 personnages consécutifs -> 10 sauvegardes par tick, pas de pic.
	std::vector<int> perTick(kInterval, 0);
	for (uint64_t id = 1; id <= 6000; ++id)
	{
		for (uint64_t tick = 0; tick < kInterval; ++tick)
		{
			if (IsCharacterAutosaveDue(id, tick, kInterval))
				++perTick[static_cast<size_t>(tick)];
		}
	}
	bool flat = true;
	for (int n : perTick)
		if (n != 10)
			flat = false;
	Check(flat, "charge répartie : 10 sauvegardes par tick");

	// 3. Cas limites.
	Check(!IsCharacterAutosaveDue(5, 5, 0), "période 0 : jamais");
	Check(!IsCharacterAutosaveDue(0, 0, kInterval), "clé 0 : jamais");
	Check(IsCharacterAutosaveDue(7, 7, 1), "période 1 : à chaque tick");
	Check(IsCharacterAutosaveDue(7, 8, 1), "période 1 : à chaque tick (bis)");

	if (s_failCount == 0)
		std::puts("[OK] autosave_schedule_tests");
	return s_failCount == 0 ? 0 : 1;
}
