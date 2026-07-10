#pragma once

// Catalogue d'objets chargé depuis game/data/items/items.json (Chantier 2 SP-A).
// Partagé client/serveur : le CLIENT s'en sert pour l'affichage (icônes, tooltips,
// noms) ; le SERVEUR (shardd) s'en sert comme SOURCE AUTORITAIRE (slot + bonus
// réels d'un objet équipé — jamais les valeurs envoyées par le client).
//
// Parsing : réutilise engine::core::Config (aplatit le JSON en clés pointées,
// ex. "items.0.id"). Pas de dépendance JSON supplémentaire.

#include "src/shared/items/ItemDefinition.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::items
{
	class ItemCatalog
	{
	public:
		// Charge/fusionne les définitions depuis un texte JSON (schéma cf. items.json).
		// Retourne false si le JSON est illisible ; les entrées valides déjà chargées
		// sont conservées. Un id dupliqué écrase la définition précédente (dernier gagne).
		bool LoadFromJson(const std::string& jsonText);

		// Charge depuis un fichier disque (chemin résolu par l'appelant). Retourne
		// false si le fichier est absent/illisible.
		bool LoadFromFile(const std::string& filePath);

		// Renvoie la définition d'un id, ou nullptr si absente.
		const ItemDefinition* Find(std::uint32_t id) const;

		// Nombre d'objets chargés.
		std::size_t Count() const { return m_byId.size(); }

		// Accès en lecture à toutes les définitions (ordre d'insertion non garanti).
		const std::unordered_map<std::uint32_t, ItemDefinition>& All() const { return m_byId; }

	private:
		std::unordered_map<std::uint32_t, ItemDefinition> m_byId;
	};
}
