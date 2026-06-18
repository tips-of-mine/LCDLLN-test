#pragma once

#include "src/client/world/instances/BuildingTemplates.h"

#include <string>
#include <vector>

namespace engine::world::instances
{
	/// Bibliothèque des types de bâtiments, chargée depuis
	/// `<contentRoot>/buildings/templates/*.json` (un fichier par type).
	/// Partagée client (résolution au rendu) et éditeur (création de variantes).
	///
	/// Format d'un fichier type (compatible parseur Config — `count` + clés
	/// indexées) :
	/// ```json
	/// {
	///   "type": "tavern",
	///   "displayName": "Taverne / Auberge",
	///   "variants": {
	///     "count": 1,
	///     "0": {
	///       "id": "auberge_terrasse",
	///       "displayName": "Auberge — terrasse",
	///       "parts": {
	///         "count": 1,
	///         "0": { "mesh": "meshes/props/Table_Large.gltf",
	///                "x": 0, "y": 0, "z": 0, "rx": 0, "ry": 0, "rz": 0,
	///                "scale": 1.0, "solid": true, "collision_radius": 0.9 }
	///       }
	///     }
	///   }
	/// }
	/// ```
	class BuildingTemplateLibrary
	{
	public:
		/// Charge tous les `<type>.json` de `<contentRoot>/buildings/templates/`.
		/// Dossier absent → bibliothèque vide + true. Un fichier invalide est
		/// ignoré (warning via `outError` cumulé) sans bloquer les autres.
		bool LoadFromContent(const std::string& contentRoot, std::string& outError);

		/// Parse un fichier type JSON (ajoute/écrase le type correspondant).
		bool LoadTemplateFromJson(const std::string& jsonText, std::string& outError);

		const std::vector<BuildingTemplate>& Templates() const { return m_templates; }
		size_t Size() const { return m_templates.size(); }

		/// Recherche un type par clé. nullptr si absent.
		const BuildingTemplate* FindType(const std::string& type) const;

		/// Résout (type, variante) → pièces. nullptr si introuvable.
		const BuildingVariant* Resolve(const std::string& type,
			const std::string& variantId) const;

		/// Crée/écrase une variante dans `<contentRoot>/buildings/templates/<type>.json`
		/// (fichier créé au besoin, dossier créé au besoin). Met aussi à jour la
		/// bibliothèque en mémoire. C'est l'opération « à chaque bâtiment créé,
		/// il se sauvegarde dans le fichier de son type ».
		/// \param typeDisplayName nom lisible du type (ignoré si le fichier existe déjà).
		bool SaveVariant(const std::string& contentRoot, const std::string& type,
			const std::string& typeDisplayName, const BuildingVariant& variant,
			std::string& outError);

	private:
		/// Insère ou remplace (par `type`) un template en mémoire.
		void UpsertTemplate(BuildingTemplate tmpl);

		std::vector<BuildingTemplate> m_templates;
	};
}
