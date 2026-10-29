#pragma once
// FIX.10 — Règles d'affichage des plaques de nom au-dessus des créatures et des
// objets interactifs (retour joueur du 29/10/2026 : trop de plaques à l'écran,
// « Lapin des pres » visible à des centaines de mètres sans pouvoir l'atteindre).
//
// Fonctions pures (aucune dépendance moteur) : testées par
// NameplateVisibilityTests.cpp, appelées par Engine.cpp à chaque frame.

#include <string>
#include <string_view>

namespace engine::client
{
	/// Catégorie d'une entité distante, du point de vue de l'affichage des plaques.
	enum class NameplateCategory : unsigned char
	{
		Player,       ///< Autre joueur : règle inchangée (toujours affiché).
		AmbientNpc,   ///< PNJ d'ambiance (villageois, voyageur…) : nom à portée de vue.
		Animal,       ///< Créature de catégorie « animal » (archetypes.json) : masquée sauf sélection.
		HostileMob,   ///< Autre créature combattable (bandit, pillard…) : masquée sauf sélection.
		Other         ///< Butin, nœud de récolte : règle inchangée.
	};

	/// Paramètres de visibilité (lus dans config.json par l'appelant).
	struct NameplateVisibilityParams
	{
		/// Champ de vision (m, distance horizontale XZ) : au-delà, aucune plaque de
		/// créature n'est affichée et la sélection est annulée automatiquement.
		/// = Perception du personnage (gameplay/perception.json, selon niveau, classe,
		/// race et sexe) ; repli client.nameplate.vision_distance_m (40 m).
		float visionDistanceM = 40.0f;
		/// Vrai si la classe du joueur « sent » les animaux (chasseur…) : les plaques
		/// d'animaux sont alors visibles sans sélection, dans le champ de vision.
		bool animalSense = false;
		/// Cœur du cercle (m) : le niveau des créatures n'est révélé qu'en deçà
		/// (classeur, onglet Code couleur : ½ rayon de perception).
		float levelRevealDistanceM = 20.0f;
	};

	/// Décide si la plaque d'une entité distante est dessinée cette frame.
	/// \param category    catégorie de l'entité.
	/// \param isSelected  l'entité est la cible courante du joueur.
	/// \param distanceM   distance horizontale (XZ, mètres) joueur → entité.
	/// \return vrai si la plaque doit être dessinée.
	bool ShouldShowNameplate(NameplateCategory category, bool isSelected, float distanceM,
		const NameplateVisibilityParams& params);

	/// Vrai si la cible courante doit être désélectionnée automatiquement : elle est
	/// sortie du champ de vision (\p distanceM > visionDistanceM). Ne concerne pas les
	/// joueurs restés dans la zone d'intérêt : la même règle s'applique à toute cible.
	bool ShouldAutoDeselect(float distanceM, const NameplateVisibilityParams& params);

	/// Vrai si le niveau d'une créature à \p distanceM est révélé (cœur du cercle).
	bool ShouldShowLevel(float distanceM, const NameplateVisibilityParams& params);

	/// Vrai si \p classId figure dans \p classListCsv (liste « hunter,ranger », sans
	/// espaces significatifs, comparaison exacte). Liste vide => faux.
	bool ClassHasAnimalSense(std::string_view classId, std::string_view classListCsv);
}
