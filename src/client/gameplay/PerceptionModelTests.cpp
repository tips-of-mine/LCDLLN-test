// FIX.10 — Tests du modèle de Perception : valeurs attendues relevées dans le classeur
// LCDLLN_Faction_Races_Classes.xlsx (onglet Tableau, colonne Perception), avec le
// fichier livré game/data/gameplay/perception.json.

#include "src/client/gameplay/PerceptionModel.h"
#include "src/shared/core/Log.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
	int s_failCount = 0;

	void CheckNear(float actual, float expected, const char* msg)
	{
		if (std::fabs(actual - expected) > 0.051f)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << " : " << actual << " attendu " << expected << std::endl;
		}
	}

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

int main()
{
	engine::core::LogSettings logSettings;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	// Le test tourne depuis la racine du dépôt (WORKING_DIRECTORY de lcdlln_add_simple_test).
	std::ifstream in("game/data/gameplay/perception.json");
	std::stringstream ss;
	ss << in.rdbuf();
	engine::client::PerceptionModel model;
	std::string error;
	Check(model.LoadFromText(ss.str(), error), "chargement de perception.json");
	Check(model.IsLoaded(), "modele charge");
	CheckNear(model.HalfCircleRatio(), 0.5f, "coeur du cercle = 1/2 rayon");

	// Chevaliers de la Lumière, Humain, Guerrier (melee), Homme : 9 / 31,1 / 53,6.
	CheckNear(model.RadiusM(1, "warrior", "humains", "male"), 9.0f, "guerrier humain H niv 1");
	CheckNear(model.RadiusM(50, "warrior", "humains", "male"), 31.1f, "guerrier humain H niv 50");
	CheckNear(model.RadiusM(100, "warrior", "humains", "male"), 53.6f, "guerrier humain H niv 100");
	// Chevaliers de la Justice, Humain, Paladin (sacre), Femme : 9,5 / 32,8 / 56,5.
	CheckNear(model.RadiusM(1, "paladin", "humains", "female"), 9.5f, "paladin humaine F niv 1");
	CheckNear(model.RadiusM(50, "paladin", "humains", "female"), 32.8f, "paladin humaine F niv 50");
	CheckNear(model.RadiusM(100, "paladin", "humains", "female"), 56.5f, "paladin humaine F niv 100");
	// Dzorak, Orkh, Pisteur, Femme (hunter -> pisteur, orcs) : 11,3 / 38,8 / 66,9.
	CheckNear(model.RadiusM(1, "hunter", "orcs", "female"), 11.3f, "chasseuse orc F niv 1");
	CheckNear(model.RadiusM(50, "hunter", "orcs", "female"), 38.8f, "chasseuse orc F niv 50");
	CheckNear(model.RadiusM(100, "hunter", "orcs", "female"), 66.9f, "chasseuse orc F niv 100");
	// Légion infernale, Démon, Guerrier, Homme : 9,5 / 32,6 / 56,2.
	CheckNear(model.RadiusM(1, "warrior", "demons", "male"), 9.5f, "guerrier demon H niv 1");
	CheckNear(model.RadiusM(50, "warrior", "demons", "male"), 32.6f, "guerrier demon H niv 50");
	CheckNear(model.RadiusM(100, "warrior", "demons", "male"), 56.2f, "guerrier demon H niv 100");
	// Faction Elfe, Elfe, Mage (lanceur), Homme : 14,4 / 49,6 / 85,5.
	CheckNear(model.RadiusM(1, "mage", "elfes", "male"), 14.4f, "mage elfe H niv 1");
	CheckNear(model.RadiusM(50, "mage", "elfes", "male"), 49.6f, "mage elfe H niv 50");
	CheckNear(model.RadiusM(100, "mage", "elfes", "male"), 85.5f, "mage elfe H niv 100");

	// Bornes et inconnus : niveau 0 => niveau 1 ; classe / race inconnues => multiplicateur 1.
	CheckNear(model.RadiusM(0, "warrior", "humains", "male"), 9.0f, "niveau 0 borne a 1");
	CheckNear(model.RadiusM(500, "warrior", "humains", "male"), 53.6f, "niveau borne au max");
	CheckNear(model.RadiusM(1, "inconnue", "inconnue", ""), 10.0f, "classe et race inconnues");

	// JSON invalide : modèle non chargé.
	engine::client::PerceptionModel bad;
	Check(!bad.LoadFromText("{ \"per_level_m\": 0.5 }", error), "base manquante refusee");
	Check(!bad.IsLoaded(), "modele invalide non charge");

	engine::core::Log::Shutdown();
	if (s_failCount == 0)
		std::cout << "[OK] perception_model_tests" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
