// src/client/character_creation/tests/AppearanceCatalogTests.cpp
//
// Personnalisation des personnages : catalogue client (AppearanceCatalog).
//  1. Catalogue réel du dépôt : chargé, 5 races jouables × 2 genres, au moins
//     5 emplacements de personnalisation par race et genre, chaque fichier
//     référencé (pièces .glb, corps, textures) existe sous game/data.
//  2. Bornage : un indice hors liste est ramené à la première option ; une race
//     inconnue donne une résolution vide (repli de l'appelant).
//  3. Résolution : pièces du LOD demandé, couleurs, proportions.

#include "src/client/character_creation/AppearanceCatalog.h"
#include "src/shared/core/Config.h"

#include <cstdio>
#include <filesystem>
#include <string>

using engine::client::AppearanceCatalog;
using engine::network::AppearanceProp;
using engine::network::AppearanceSlot;
using engine::network::CharacterAppearance;

namespace
{
	int g_failed = 0;
#define REQUIRE(cond) do { \
		if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
	} while (0)

	AppearanceCatalog LoadRepoCatalog()
	{
		engine::core::Config cfg;
		cfg.SetValue("paths.content", std::string("game/data"));
		AppearanceCatalog cat;
		REQUIRE(cat.Init(cfg));
		return cat;
	}

	void Test_RepoCatalog_CoversPlayableRaces()
	{
		const AppearanceCatalog cat = LoadRepoCatalog();
		for (const char* race : { "humains", "elfes", "nains", "orcs", "demons" })
		{
			for (const char* gender : { "male", "female" })
			{
				const auto* rg = cat.Find(race, gender);
				REQUIRE(rg != nullptr);
				if (rg == nullptr)
					continue;
				int customizable = 0;
				for (const auto& opts : rg->slots)
					if (opts.size() >= 2u)
						++customizable;
				// Exigence du propriétaire : au moins 5 éléments de personnalisation visibles.
				REQUIRE(customizable >= 5);
				REQUIRE(!rg->Options(AppearanceSlot::Hair).empty());
				REQUIRE(!rg->Options(AppearanceSlot::HairColor).empty());
			}
		}
		// Démons : cornes, queue et ailes ; orcs : défenses ; elfes : oreilles.
		REQUIRE(cat.Find("demons", "male")->Options(AppearanceSlot::Horns).size() >= 2u);
		REQUIRE(cat.Find("demons", "female")->Options(AppearanceSlot::Tail).size() >= 2u);
		REQUIRE(cat.Find("demons", "male")->Options(AppearanceSlot::Wings).size() >= 2u);
		REQUIRE(cat.Find("orcs", "male")->Options(AppearanceSlot::Tusks).size() >= 2u);
		REQUIRE(cat.Find("elfes", "female")->Options(AppearanceSlot::Ears).size() >= 2u);
		REQUIRE(cat.Find("humains", "male")->Options(AppearanceSlot::Beard).size() >= 2u);
		REQUIRE(cat.Find("humains", "male")->Options(AppearanceSlot::Moustache).size() >= 2u);
		// Races non jouables absentes.
		REQUIRE(cat.Find("morts_vivants", "male") == nullptr);
	}

	void Test_RepoCatalog_FilesExist()
	{
		const AppearanceCatalog cat = LoadRepoCatalog();
		const auto files = cat.AllReferencedFiles();
		REQUIRE(files.size() > 50u);
		for (const auto& f : files)
		{
			const bool ok = std::filesystem::exists(std::filesystem::path("game/data") / f);
			if (!ok)
				std::fprintf(stderr, "[FAIL] fichier absent : %s\n", f.c_str());
			REQUIRE(ok);
		}
	}

	void Test_SanitizeAndResolve()
	{
		const std::string json = R"({
		  "raceOrder": ["humains"],
		  "materials": {"skin": ["MI_Regular_Male"], "hair": "MI_Hair"},
		  "races": {"humains": {"male": {
		    "body": "b.glb", "hiddenBodyMeshes": ["Head_Hood"],
		    "slots": {
		      "face": [{"id": "f1", "lods": ["f1_0.glb", "f1_1.glb", "f1_2.glb"]},
		               {"id": "f2", "lods": ["f2_0.glb", "f2_1.glb", "f2_2.glb"]}],
		      "hair": [{"id": "none", "lods": []}, {"id": "long", "lods": ["h_0.glb", "h_1.glb", "h_2.glb"]}],
		      "hairColor": [{"id": "noir", "texture": "noir.png", "hex": "#000000", "tint": "#102030"}],
		      "skinTone": [{"id": "claire", "basecolor": "peau.png"}]
		    }}}}
		})";
		AppearanceCatalog cat;
		std::string err;
		REQUIRE(cat.LoadFromText(json, err));
		CharacterAppearance a;
		a.Set(AppearanceSlot::Face, 1);
		a.Set(AppearanceSlot::Hair, 9);       // hors liste -> 0 (« none »)
		a.Set(AppearanceSlot::Horns, 3);      // emplacement absent -> 0
		cat.Sanitize("humains", "male", a);
		REQUIRE(a.Get(AppearanceSlot::Face) == 1);
		REQUIRE(a.Get(AppearanceSlot::Hair) == 0);
		REQUIRE(a.Get(AppearanceSlot::Horns) == 0);

		a.Set(AppearanceSlot::Hair, 1);
		a.SetProportion(AppearanceProp::Height, 110);
		const auto r = cat.Resolve("humains", "male", a, 1);
		REQUIRE(r.body == "b.glb");
		REQUIRE(r.parts.size() == 2u);
		REQUIRE(r.parts.size() == 2u && r.parts[0] == "f2_1.glb" && r.parts[1] == "h_1.glb");
		REQUIRE(r.hairTexture == "noir.png");
		REQUIRE(r.hairTint == 0xFF302010u);   // R octet de poids faible
		REQUIRE(r.skinTint == 0xFFFFFFFFu);   // pas de teinte -> blanc
		REQUIRE(engine::client::ParseHexColorRgba8("pas une couleur", 7u) == 7u);
		REQUIRE(r.skinTexture == "peau.png");
		REQUIRE(r.heightScale > 1.09f && r.heightScale < 1.11f);
		REQUIRE(r.hiddenBodyMeshes.size() == 1u);

		// Genre féminin absent / race inconnue -> résolution vide.
		REQUIRE(cat.Resolve("humains", "female", a, 0).body.empty());
		REQUIRE(cat.Resolve("orcs", "male", a, 0).body.empty());
	}
}

int main()
{
	Test_RepoCatalog_CoversPlayableRaces();
	Test_RepoCatalog_FilesExist();
	Test_SanitizeAndResolve();
	return g_failed == 0 ? 0 : 1;
}
