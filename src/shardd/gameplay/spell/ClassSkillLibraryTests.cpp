#include "src/shardd/gameplay/spell/ClassSkillLibrary.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace
{
	using engine::server::ClassSkillLibrary;
	using engine::server::ClassSkillEffectKind;
	using engine::server::ClassSkillTarget;

	const char* kValidClass = R"JSON(
	{
	  "classId": "pretre_grace",
	  "sourceTree": "Pretre",
	  "skills": [
	    { "id": "pretre_grace_single_t1", "name": "Soin", "branch": "single", "tier": 1, "level": 1,
	      "effectKind": "Heal", "target": "SingleAlly", "powerValue": 1.0, "rangeMeters": 6.0,
	      "areaRadiusMeters": 0.0, "castTimeMs": 1000, "cooldownMs": 3000, "resourceCostPercent": 6,
	      "description": "Restaure la vie d'un allie (apostrophe litterale = format reel des donnees)." },
	    { "id": "pretre_grace_def_t1", "name": "Garde", "branch": "def", "tier": 1, "level": 1,
	      "effectKind": "Defense", "target": "SingleAlly", "powerValue": 1.0, "rangeMeters": 0.0,
	      "areaRadiusMeters": 0.0, "castTimeMs": 0, "cooldownMs": 18000, "resourceCostPercent": 8,
	      "description": "Reduit les degats." }
	  ]
	}
	)JSON";

	void TestLoadValidClass()
	{
		engine::core::Config cfg;
		ClassSkillLibrary lib(cfg);
		std::string err;
		assert(lib.LoadClassFromText(kValidClass, err));
		assert(err.empty());
		const auto* skills = lib.GetClassSkills("pretre_grace");
		assert(skills != nullptr);
		assert(skills->size() == 2u);
		const auto* heal = lib.FindSkill("pretre_grace", "pretre_grace_single_t1");
		assert(heal != nullptr);
		assert(heal->effectKind == ClassSkillEffectKind::Heal);
		assert(heal->target == ClassSkillTarget::SingleAlly);
		const auto* def = lib.FindSkill("pretre_grace", "pretre_grace_def_t1");
		assert(def != nullptr);
		assert(def->effectKind == ClassSkillEffectKind::Defense);
		assert(lib.FindSkill("pretre_grace", "inconnu") == nullptr);
		assert(lib.GetClassSkills("inconnu") == nullptr);
		std::puts("[OK] TestLoadValidClass");
	}

	void TestRejectsBadEffectKind()
	{
		engine::core::Config cfg;
		ClassSkillLibrary lib(cfg);
		std::string err;
		const char* bad = R"JSON({"classId":"x","skills":[{"id":"a","name":"n","branch":"single","tier":1,"level":1,"effectKind":"Bogus","target":"SingleEnemy","powerValue":1.0,"rangeMeters":0.0,"areaRadiusMeters":0.0,"castTimeMs":0,"cooldownMs":3000,"resourceCostPercent":6,"description":""}]})JSON";
		assert(!lib.LoadClassFromText(bad, err));
		assert(!err.empty());
		std::puts("[OK] TestRejectsBadEffectKind");
	}

	void TestRejectsEmptySkills()
	{
		engine::core::Config cfg;
		ClassSkillLibrary lib(cfg);
		std::string err;
		assert(!lib.LoadClassFromText(R"JSON({"classId":"x","skills":[]})JSON", err));
		std::puts("[OK] TestRejectsEmptySkills");
	}
}

int main()
{
	TestLoadValidClass();
	TestRejectsBadEffectKind();
	TestRejectsEmptySkills();
	std::puts("[OK] ClassSkillLibraryTests");
	return 0;
}
