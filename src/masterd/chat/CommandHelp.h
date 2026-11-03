#pragma once
// ADM.0 — Aide des commandes de jeu (« /help », « /search »), côté master.
//
// Construit les lignes de réponse affichées dans le chat à partir du registre
// des commandes (game/data/config/slash_commands.json, SlashCommandRegistry) :
//   - « /help [page] »          : liste paginée, 15 commandes par page, filtrée
//                                  par le rôle de l'appelant ;
//   - « /help <commande> »       : fiche détaillée (syntaxe, explication, exemple,
//                                  rôle minimum, alias) ;
//   - « /help <catégorie> [page] » : liste paginée d'une seule catégorie ;
//   - « /search <mot> »          : commandes dont le nom, un alias ou la
//                                  description contient le mot.
// Les entrées « planned » (prévues, pas encore disponibles) ne sont jamais
// listées ; elles restent consultables par « /help <commande> » (marquées).
//
// Module pur (pas de réseau ni de base) : testé par command_help_tests.

#include "src/masterd/account/AccountRole.h"
#include "src/masterd/admin/SlashCommandRegistry.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server::chat
{
	/// Nombre de commandes par page de « /help » (demande utilisateur : lots de 15).
	inline constexpr size_t kHelpPageSize = 15u;

	/// Libellé français d'un rôle (« joueur », « modérateur », « maître de jeu »,
	/// « administrateur »).
	const char* RoleLabelFr(AccountRole role);

	/// Libellé français d'une catégorie du registre (« movement » → « Déplacement »).
	/// Catégorie inconnue : renvoyée telle quelle (vide → « Autres »).
	std::string CategoryLabelFr(std::string_view category);

	/// Vrai si l'entrée est visible dans les listes pour \p role : rôle suffisant et
	/// statut différent de « planned ».
	bool IsListedFor(const SlashCommandEntry& entry, AccountRole role);

	/// Construit la réponse complète de « /help <args> » pour \p role.
	/// \param entries  entrées du registre, dans l'ordre du fichier (Entries()).
	/// \param args     texte après « /help » (vide, numéro de page, commande ou
	///                 catégorie suivie d'un numéro de page facultatif).
	/// \return lignes à envoyer (au moins une).
	std::vector<std::string> BuildHelpReply(const std::vector<SlashCommandEntry>& entries,
		const SlashCommandRegistry& registry, AccountRole role, std::string_view args);

	/// Construit la réponse de « /search <mot> » (au plus \p maxResults lignes de
	/// résultats, plus une ligne d'en-tête).
	std::vector<std::string> BuildSearchReply(const std::vector<SlashCommandEntry>& entries,
		AccountRole role, std::string_view word, size_t maxResults = 30u);

	/// Fiche détaillée d'une commande (utilisée par « /help <commande> » et par le
	/// message d'erreur de syntaxe des commandes relayées au shard).
	std::vector<std::string> BuildCommandDetail(const SlashCommandEntry& entry, AccountRole role);
}
