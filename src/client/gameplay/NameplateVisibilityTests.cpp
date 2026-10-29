// FIX.10 — Tests des règles d'affichage des plaques de nom (fonctions pures).
// Pas de framework : code de retour 0 si tout passe.

#include "src/client/gameplay/NameplateVisibility.h"

#include <iostream>

namespace
{
	int s_failCount = 0;

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
	using namespace engine::client;
	NameplateVisibilityParams p;
	p.visionDistanceM = 40.0f;

	// Animaux : jamais sans sélection (joueur standard), même tout près.
	Check(!ShouldShowNameplate(NameplateCategory::Animal, false, 5.0f, p), "animal non selectionne proche : masque");
	Check(ShouldShowNameplate(NameplateCategory::Animal, true, 39.0f, p), "animal selectionne dans le champ : visible");
	Check(!ShouldShowNameplate(NameplateCategory::Animal, true, 41.0f, p), "animal selectionne hors champ : masque");

	// Mobs hostiles : même règle que les animaux, sans exception de classe.
	Check(!ShouldShowNameplate(NameplateCategory::HostileMob, false, 5.0f, p), "hostile non selectionne : masque");
	Check(ShouldShowNameplate(NameplateCategory::HostileMob, true, 10.0f, p), "hostile selectionne : visible");

	// PNJ d'ambiance : nom à portée de vue seulement.
	Check(ShouldShowNameplate(NameplateCategory::AmbientNpc, false, 40.0f, p), "PNJ a 40 m : visible");
	Check(!ShouldShowNameplate(NameplateCategory::AmbientNpc, false, 40.5f, p), "PNJ au-dela : masque");

	// Joueurs, butin, nœuds : inchangés.
	Check(ShouldShowNameplate(NameplateCategory::Player, false, 500.0f, p), "joueur : inchange");
	Check(ShouldShowNameplate(NameplateCategory::Other, false, 500.0f, p), "butin/noeud : inchange");

	// Chasseur : animaux proches visibles sans sélection, pas au-delà ; hostiles inchangés.
	NameplateVisibilityParams hunter = p;
	hunter.animalSense = true;
	Check(ShouldShowNameplate(NameplateCategory::Animal, false, 30.0f, hunter), "chasseur : animal proche visible");
	Check(!ShouldShowNameplate(NameplateCategory::Animal, false, 60.0f, hunter), "chasseur : animal lointain masque");
	Check(!ShouldShowNameplate(NameplateCategory::HostileMob, false, 30.0f, hunter), "chasseur : hostile non selectionne masque");

	// Désélection automatique au-delà du champ de vision.
	Check(!ShouldAutoDeselect(40.0f, p), "cible a 40 m : gardee");
	Check(ShouldAutoDeselect(40.1f, p), "cible au-dela : deselectionnee");

	// Niveau révélé seulement au cœur du cercle.
	p.levelRevealDistanceM = 20.0f;
	Check(ShouldShowLevel(20.0f, p), "niveau a 20 m (coeur) : visible");
	Check(!ShouldShowLevel(25.0f, p), "niveau a 25 m : masque");

	// Liste de classes « sens des animaux ».
	Check(ClassHasAnimalSense("hunter", "hunter"), "hunter dans 'hunter'");
	Check(ClassHasAnimalSense("ranger", "hunter, ranger"), "ranger dans 'hunter, ranger'");
	Check(!ClassHasAnimalSense("warrior", "hunter,ranger"), "warrior absent");
	Check(!ClassHasAnimalSense("", "hunter"), "classe vide");
	Check(!ClassHasAnimalSense("hunter", ""), "liste vide");
	Check(!ClassHasAnimalSense("hunt", "hunter"), "pas de correspondance partielle");

	if (s_failCount == 0)
		std::cout << "[OK] nameplate_visibility_tests" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
