#include "src/client/gameplay/ActionBarLayout.h"
#include "src/client/gameplay/SpellKitCatalog.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <string>

namespace
{
	using engine::client::SpellDisplay;

	std::vector<SpellDisplay> MakeKit()
	{
		std::vector<SpellDisplay> kit;
		SpellDisplay a{}; a.spellId = "s1"; a.slot = 1; kit.push_back(a);
		SpellDisplay b{}; b.spellId = "s2"; b.slot = 2; kit.push_back(b);
		SpellDisplay c{}; c.spellId = "s3"; c.slot = 3; kit.push_back(c);
		return kit;
	}

	// Layout vide → défaut = sorts du kit dans l'ordre, le reste vide.
	void TestEmptyLayoutUsesKitOrder()
	{
		const std::vector<SpellDisplay> kit = MakeKit();
		std::array<std::string, 10> layout{}; // tout vide
		const std::array<std::string, 10> resolved =
			engine::client::ResolveActionBarLayout(layout, kit);
		assert(resolved[0] == "s1");
		assert(resolved[1] == "s2");
		assert(resolved[2] == "s3");
		assert(resolved[3].empty());
		std::puts("[OK] TestEmptyLayoutUsesKitOrder");
	}

	// Layout custom conservé.
	void TestCustomLayoutPreserved()
	{
		const std::vector<SpellDisplay> kit = MakeKit();
		std::array<std::string, 10> layout{};
		layout[0] = "s3";
		layout[5] = "s1";
		const std::array<std::string, 10> resolved =
			engine::client::ResolveActionBarLayout(layout, kit);
		assert(resolved[0] == "s3");
		assert(resolved[5] == "s1");
		assert(resolved[1].empty());
		std::puts("[OK] TestCustomLayoutPreserved");
	}

	// spellId absent du kit (contenu modifié) → slot vidé.
	void TestObsoleteSpellDropped()
	{
		const std::vector<SpellDisplay> kit = MakeKit();
		std::array<std::string, 10> layout{};
		layout[0] = "obsolete";
		layout[1] = "s2";
		const std::array<std::string, 10> resolved =
			engine::client::ResolveActionBarLayout(layout, kit);
		assert(resolved[0].empty());
		assert(resolved[1] == "s2");
		std::puts("[OK] TestObsoleteSpellDropped");
	}

	// Doublon défensif → seconde occurrence vidée.
	void TestDuplicateDropped()
	{
		const std::vector<SpellDisplay> kit = MakeKit();
		std::array<std::string, 10> layout{};
		layout[0] = "s1";
		layout[1] = "s1";
		const std::array<std::string, 10> resolved =
			engine::client::ResolveActionBarLayout(layout, kit);
		assert(resolved[0] == "s1");
		assert(resolved[1].empty());
		std::puts("[OK] TestDuplicateDropped");
	}
}

int main()
{
	TestEmptyLayoutUsesKitOrder();
	TestCustomLayoutPreserved();
	TestObsoleteSpellDropped();
	TestDuplicateDropped();
	std::puts("[OK] ActionBarLayoutTests");
	return 0;
}
