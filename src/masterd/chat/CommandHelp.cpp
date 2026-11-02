// ADM.0 — Aide des commandes de jeu : voir CommandHelp.h.

#include "src/masterd/chat/CommandHelp.h"

#include "src/shared/admin/GmCommandText.h"

#include <algorithm>

namespace engine::server::chat
{
	namespace
	{
		/// Catégories connues : clé du registre → libellé affiché (ordre d'affichage
		/// de « /help » : celui du fichier, ce tableau ne sert qu'aux libellés).
		struct CategoryLabel
		{
			const char* key;
			const char* label;
		};
		constexpr CategoryLabel kCategoryLabels[] = {
			{ "core", "Aide et outils" },
			{ "movement", "Déplacement" },
			{ "information", "Informations" },
			{ "character", "Personnage" },
			{ "items", "Objets et argent" },
			{ "appearance", "Monture et apparence" },
			{ "world", "Monde, PNJ et météo" },
			{ "quests", "Quêtes et événements" },
			{ "moderation", "Modération" },
			{ "server", "Serveur" },
			{ "ui_panel", "Fenêtres" },
			{ "social", "Social" },
			{ "gameplay", "Jeu" },
			{ "debug", "Débogage" },
			{ "admin", "Administration" },
		};

		/// Ligne courte d'une commande dans une liste : « /tp — Téléportation… ».
		std::string ListLine(const SlashCommandEntry& e)
		{
			std::string line = "  " + e.command;
			if (!e.description.empty())
				line += " — " + e.description;
			return line;
		}

		/// Entrées listables pour \p role, filtrées par catégorie (vide = toutes).
		std::vector<const SlashCommandEntry*> Listable(const std::vector<SlashCommandEntry>& entries,
			AccountRole role, std::string_view category)
		{
			std::vector<const SlashCommandEntry*> out;
			for (const SlashCommandEntry& e : entries)
			{
				if (!IsListedFor(e, role))
					continue;
				if (!category.empty() && !engine::admin::EqualsIgnoreCase(e.category, category))
					continue;
				out.push_back(&e);
			}
			return out;
		}

		/// Une page de liste (\p page commence à 1 ; ramenée dans [1, total]).
		std::vector<std::string> PageReply(const std::vector<const SlashCommandEntry*>& list, int64_t page,
			AccountRole role, std::string_view category)
		{
			std::vector<std::string> out;
			const size_t total = list.size();
			if (total == 0u)
			{
				out.push_back(category.empty()
					? std::string("Aucune commande disponible pour votre rôle.")
					: "Aucune commande disponible dans la catégorie « " + std::string(category) + " ».");
				return out;
			}
			const size_t pages = (total + kHelpPageSize - 1u) / kHelpPageSize;
			const size_t p = static_cast<size_t>(std::clamp<int64_t>(page, 1, static_cast<int64_t>(pages)));
			std::string header = category.empty()
				? std::string("Commandes disponibles")
				: "Commandes « " + CategoryLabelFr(category) + " »";
			header += " — page " + std::to_string(p) + "/" + std::to_string(pages)
				+ " (" + std::to_string(total) + " commandes, rôle " + RoleLabelFr(role) + ") :";
			out.push_back(std::move(header));
			const size_t first = (p - 1u) * kHelpPageSize;
			const size_t last = (std::min)(first + kHelpPageSize, total);
			std::string currentCategory;
			for (size_t i = first; i < last; ++i)
			{
				const SlashCommandEntry& e = *list[i];
				// Dans la liste complète, un intertitre à chaque changement de catégorie.
				if (category.empty() && (i == first || e.category != currentCategory))
				{
					currentCategory = e.category;
					out.push_back("[" + CategoryLabelFr(e.category) + "]");
				}
				out.push_back(ListLine(e));
			}
			std::string footer;
			const std::string prefix = category.empty() ? std::string("/help ") : "/help " + std::string(category) + " ";
			if (p < pages)
				footer = "Suite : " + prefix + std::to_string(p + 1u) + " — ";
			footer += "Détail : /help <commande> — Recherche : /search <mot>";
			out.push_back(std::move(footer));
			return out;
		}

		/// La catégorie \p word existe-t-elle dans le registre ?
		bool IsCategory(const std::vector<SlashCommandEntry>& entries, std::string_view word)
		{
			for (const SlashCommandEntry& e : entries)
			{
				if (!e.category.empty() && engine::admin::EqualsIgnoreCase(e.category, word))
					return true;
			}
			return false;
		}
	}

	const char* RoleLabelFr(AccountRole role)
	{
		switch (role)
		{
		case AccountRole::Moderator:     return "modérateur";
		case AccountRole::GameMaster:    return "maître de jeu";
		case AccountRole::Administrator: return "administrateur";
		case AccountRole::Console:       return "console";
		default:                         return "joueur";
		}
	}

	std::string CategoryLabelFr(std::string_view category)
	{
		if (category.empty())
			return "Autres";
		for (const CategoryLabel& c : kCategoryLabels)
		{
			if (engine::admin::EqualsIgnoreCase(category, c.key))
				return c.label;
		}
		return std::string(category);
	}

	bool IsListedFor(const SlashCommandEntry& entry, AccountRole role)
	{
		return entry.status != kSlashStatusPlanned && role >= entry.minRole;
	}

	std::vector<std::string> BuildCommandDetail(const SlashCommandEntry& e, AccountRole role)
	{
		std::vector<std::string> out;
		std::string title = e.command;
		if (!e.description.empty())
			title += " — " + e.description;
		if (e.status == kSlashStatusPlanned)
			title += " (prévue, pas encore disponible)";
		out.push_back(std::move(title));
		out.push_back("Syntaxe : " + e.usage);
		if (!e.details.empty())
			out.push_back(e.details);
		if (!e.example.empty())
			out.push_back("Exemple : " + e.example);
		std::string meta = "Rôle minimum : " + std::string(RoleLabelFr(e.minRole));
		if (role < e.minRole)
			meta += " (vous n'y avez pas accès)";
		meta += " — Catégorie : " + CategoryLabelFr(e.category);
		out.push_back(std::move(meta));
		if (!e.aliases.empty())
		{
			std::string aliases = "Alias :";
			for (const std::string& a : e.aliases)
				aliases += " " + a;
			out.push_back(std::move(aliases));
		}
		return out;
	}

	std::vector<std::string> BuildHelpReply(const std::vector<SlashCommandEntry>& entries,
		const SlashCommandRegistry& registry, AccountRole role, std::string_view args)
	{
		const std::vector<std::string> words = engine::admin::SplitArgs(args);
		// « /help » ou « /help 3 » : liste complète paginée.
		if (words.empty())
			return PageReply(Listable(entries, role, {}), 1, role, {});
		if (const auto page = engine::admin::ParseInt(words[0]); page && words.size() == 1u)
			return PageReply(Listable(entries, role, {}), *page, role, {});
		// « /help movement [2] » : une catégorie.
		if (IsCategory(entries, words[0]))
		{
			int64_t page = 1;
			if (words.size() >= 2u)
			{
				if (const auto p = engine::admin::ParseInt(words[1]))
					page = *p;
			}
			return PageReply(Listable(entries, role, words[0]), page, role, words[0]);
		}
		// « /help tp » ou « /help /tp » : une commande (canonique ou alias).
		if (const SlashCommandEntry* e = registry.Lookup(words[0]))
			return BuildCommandDetail(*e, role);
		std::vector<std::string> out;
		out.push_back("Commande ou catégorie inconnue : " + words[0] + ". Essayez /search " + words[0] + ".");
		return out;
	}

	std::vector<std::string> BuildSearchReply(const std::vector<SlashCommandEntry>& entries,
		AccountRole role, std::string_view word, size_t maxResults)
	{
		std::vector<std::string> out;
		const std::string needle(engine::admin::Trim(word));
		if (needle.empty())
		{
			out.push_back("Syntaxe : /search <mot> (ex. /search teleport)");
			return out;
		}
		std::vector<const SlashCommandEntry*> found;
		for (const SlashCommandEntry& e : entries)
		{
			if (!IsListedFor(e, role))
				continue;
			bool match = engine::admin::ContainsIgnoreCase(e.command, needle)
				|| engine::admin::ContainsIgnoreCase(e.description, needle)
				|| engine::admin::ContainsIgnoreCase(e.details, needle);
			for (const std::string& a : e.aliases)
				match = match || engine::admin::ContainsIgnoreCase(a, needle);
			if (match)
				found.push_back(&e);
		}
		if (found.empty())
		{
			out.push_back("Aucune commande ne correspond à « " + needle + " ».");
			return out;
		}
		out.push_back(std::to_string(found.size()) + " commande(s) pour « " + needle + " » :");
		for (size_t i = 0; i < found.size() && i < maxResults; ++i)
			out.push_back(ListLine(*found[i]));
		if (found.size() > maxResults)
			out.push_back("… et " + std::to_string(found.size() - maxResults) + " autre(s) : précisez le mot.");
		return out;
	}
}
