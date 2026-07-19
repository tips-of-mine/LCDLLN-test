#pragma once
// SlashCommandRegistry : charge le fichier de reference des slash commands
// (game/data/config/slash_commands.json) au boot du master, expose un
// lookup par nom (canonical ou alias) + verification du role minimum.
//
// La lecture est faite une seule fois au boot ; pas de hot-reload V1.
// Le registre est read-only apres LoadFromFile (pas de mutex), ce qui
// permet aux handlers de l'interroger sans synchronisation.
//
// Format JSON attendu (cf. game/data/config/slash_commands.json) :
//   { "commands": [ { "command":"/sky moon <phase 0..15>", "aliases":[...],
//                     "minRole":"administrator", "status":"implemented" }, ... ] }
//
// Convention V1 : on extrait le prefixe canonique d'une commande sans la
// portion "<arg>" pour l'indexer (ex: "/sky moon <phase 0..15>" -> "/sky moon").
// Le client envoie toujours la forme canonique sans placeholder, donc un
// Lookup("/sky moon") doit reussir.

#include "src/masterd/account/AccountRole.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// Une entree du registre, partagee par toutes les cles indexees
	/// (le canonical et chaque alias pointent vers la meme entree par valeur).
	struct SlashCommandEntry
	{
		std::string                command;   ///< Forme canonique (sans placeholder), ex: "/sky moon".
		std::vector<std::string>   aliases;   ///< Aliases ex: ["/sky"] pour "/sky info".
		engine::server::AccountRole minRole = engine::server::AccountRole::Player;
		std::string                status;    ///< "implemented" / "client_only_legacy" / etc. (info).
		/// Console /help (2026-07-18) — description et catégorie affichables
		/// (reprises du JSON ; vides si absentes).
		std::string                description;
		std::string                category;
	};

	/// Registre des slash commands lu au boot. Read-only apres LoadFromFile.
	class SlashCommandRegistry
	{
	public:
		/// Resultat d'une verification de droits.
		struct CheckResult
		{
			bool        found       = false; ///< true si la commande existe dans le registre.
			bool        allowed     = false; ///< true si \p userRole >= entry.minRole.
			AccountRole minRequired = AccountRole::Player; ///< Role minimum exige (info pour message).
		};

		SlashCommandRegistry() = default;

		/// Charge le fichier JSON. Retourne true si parsing reussi et au
		/// moins une commande chargee. En cas d'echec partiel (entree
		/// invalide), elle est ignoree avec un warning ; le reste est charge.
		///
		/// \param jsonPath chemin relatif ou absolu au fichier slash_commands.json.
		/// \return true si le fichier a ete lu et au moins une commande indexee.
		bool LoadFromFile(const std::string& jsonPath);

		/// Lookup par nom. Accepte la forme canonique ("/sky moon") ou un alias
		/// (ex: "/sky"). Retourne nullptr si non trouve.
		const SlashCommandEntry* Lookup(const std::string& command) const;

		/// Verifie si \p userRole peut executer \p command. Retourne :
		///   {found:false, allowed:false, minRequired:Player} si commande inconnue
		///   {found:true, allowed:true, minRequired:<min>}    si role suffisant
		///   {found:true, allowed:false, minRequired:<min>}   si role insuffisant
		CheckResult Check(const std::string& command, AccountRole userRole) const;

		/// Nombre d'entrees uniques chargees (pas le total des cles indexees).
		size_t Size() const { return m_entries.size(); }

		/// Console /help (2026-07-18) — liste complete des entrees (ordre du
		/// fichier). Lecture seule ; sert a construire la liste des commandes
		/// visibles par un role donne.
		const std::vector<SlashCommandEntry>& Entries() const { return m_entries; }

	private:
		/// Stockage canonique par valeur (une entree par commande).
		std::vector<SlashCommandEntry> m_entries;
		/// Index par nom (canonique + alias) -> indice dans m_entries.
		std::unordered_map<std::string, size_t> m_byName;
	};
}
