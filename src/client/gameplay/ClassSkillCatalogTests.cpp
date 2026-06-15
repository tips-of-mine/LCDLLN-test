#include "src/client/gameplay/ClassSkillCatalog.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace
{
	const char* kClass = R"JSON(
	{ "classId": "mage", "sourceTree": "class_mage", "skills": [
	  { "id": "mage_single_t1", "name": "Eclair", "branch": "single", "tier": 1, "level": 1,
	    "effectKind": "Damage", "target": "SingleEnemy", "powerValue": 1.1, "rangeMeters": 30.0,
	    "areaRadiusMeters": 0.0, "castTimeMs": 1500, "cooldownMs": 3000, "resourceCostPercent": 6,
	    "description": "Decharge arcanique." } ] }
	)JSON";

	void TestLoadClass()
	{
		engine::client::ClassSkillCatalog cat;
		std::string err;
		assert(cat.LoadClassFromText(kClass, err));
		const auto* s = cat.GetClassSkills("mage");
		assert(s != nullptr && s->size() == 1u);
		assert((*s)[0].effectKind == "Damage");
		assert((*s)[0].rangeMeters == 30.0f);
		assert(cat.GetClassSkills("inconnu") == nullptr);
		std::puts("[OK] TestLoadClass");
	}
}

int main()
{
	TestLoadClass();
	std::puts("[OK] ClassSkillCatalogTests");
	return 0;
}
