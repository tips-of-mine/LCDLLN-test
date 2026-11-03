// ADM.0 — Tests de l'aide des commandes (/help paginé, /search, fiches) et de la
// cohérence du registre game/data/config/slash_commands.json avec le catalogue
// des commandes exécutées par le shard (GmCommandCatalog.h).
//
// Exécuté depuis la racine du dépôt (WORKING_DIRECTORY = CMAKE_SOURCE_DIR).

#include "src/masterd/admin/SlashCommandRegistry.h"
#include "src/masterd/chat/CommandHelp.h"
#include "src/shardd/gameplay/gm/GmCommandCatalog.h"
#include "src/shared/admin/GmCommandText.h"

#include <iostream>
#include <iterator>
#include <set>
#include <string>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const std::string& msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	bool AnyLineContains(const std::vector<std::string>& lines, const std::string& needle)
	{
		for (const std::string& l : lines)
		{
			if (l.find(needle) != std::string::npos)
				return true;
		}
		return false;
	}
}

int main()
{
	using namespace engine::server;
	using namespace engine::server::chat;

	SlashCommandRegistry registry;
	Check(registry.LoadFromFile("game/data/config/slash_commands.json"), "chargement de slash_commands.json");
	const auto& entries = registry.Entries();
	Check(entries.size() >= 40u, "le registre contient les commandes historiques et ADM");

	// --- Registre : noms uniques (canonique et alias), champs d'aide présents ---
	std::set<std::string> names;
	for (const SlashCommandEntry& e : entries)
	{
		const std::string key = engine::admin::ToLowerAscii(e.command);
		Check(names.insert(key).second, "nom en double dans le registre : " + e.command);
		for (const std::string& a : e.aliases)
			Check(names.insert(engine::admin::ToLowerAscii(a)).second, "alias en double : " + a + " (" + e.command + ")");
		Check(!e.command.empty() && e.command.front() == '/', "commande sans « / » : " + e.command);
		Check(!e.usage.empty(), "usage vide : " + e.command);
		// Les entrées ADM (relayées au shard ou exécutées par le master) ont une aide complète.
		if (e.status == kSlashStatusShardCommand || e.status == kSlashStatusServerRoute)
		{
			Check(!e.description.empty(), "description manquante : " + e.command);
			Check(!e.category.empty(), "catégorie manquante : " + e.command);
			Check(e.command.find_first_of(" <[") == std::string::npos, "nom canonique avec placeholder : " + e.command);
		}
		if (e.status == kSlashStatusShardCommand)
		{
			Check(!e.details.empty(), "aide détaillée manquante : " + e.command);
			Check(!e.example.empty(), "exemple manquant : " + e.command);
		}
	}

	// --- Cohérence JSON ↔ catalogue du shard (nom ET rôle) ---
	size_t shardEntries = 0;
	for (const SlashCommandEntry& e : entries)
	{
		if (e.status != kSlashStatusShardCommand)
			continue;
		++shardEntries;
		const std::string name = engine::admin::NormalizeCommandName(e.command);
		const auto* info = gm::FindGmShardCommand(name);
		Check(info != nullptr, "commande shard_command absente de GmCommandCatalog.h : " + e.command);
		if (info != nullptr)
			Check(static_cast<uint8_t>(info->minRole) == static_cast<uint8_t>(e.minRole),
				"rôle différent JSON / catalogue shard : " + e.command);
	}
	for (const gm::GmShardCommandInfo& c : gm::kGmShardCommands)
	{
		const SlashCommandEntry* e = registry.Lookup(std::string(c.name));
		Check(e != nullptr && e->status == kSlashStatusShardCommand,
			"commande du catalogue shard sans entrée shard_command dans le JSON : /" + std::string(c.name));
	}
	Check(shardEntries == std::size(gm::kGmShardCommands), "même nombre d'entrées JSON shard_command et de commandes shard");

	// --- Lookup : sans casse, « / » facultatif, alias, placeholder historique coupé ---
	Check(registry.Lookup("help") != nullptr && registry.Lookup("/HELP") != nullptr, "Lookup sans casse et sans /");
	Check(registry.Lookup("/aide") != nullptr && registry.Lookup("/aide")->command == "/help", "Lookup alias");
	Check(registry.Lookup("/sky moon") != nullptr, "Lookup historique « /sky moon <phase> »");
	Check(registry.Lookup("/inexistante") == nullptr, "Lookup inconnue");

	// --- /help : pagination par 15, filtrage par rôle ---
	const auto page1 = BuildHelpReply(entries, registry, AccountRole::Administrator, "");
	Check(!page1.empty() && page1.front().find("page 1/") != std::string::npos, "/help : en-tête page 1");
	size_t listed = 0;
	for (const std::string& l : page1)
	{
		if (l.rfind("  /", 0) == 0)
			++listed;
	}
	Check(listed == kHelpPageSize, "/help : 15 commandes par page (" + std::to_string(listed) + ")");
	Check(AnyLineContains(page1, "Suite : /help 2"), "/help : lien vers la page suivante");
	const auto lastPage = BuildHelpReply(entries, registry, AccountRole::Administrator, "999");
	Check(!lastPage.empty() && !AnyLineContains(lastPage, "Suite :"), "/help 999 : ramené à la dernière page");

	// Un joueur ne voit pas les commandes réservées.
	bool playerSeesGm = false;
	for (int page = 1; page <= 20; ++page)
	{
		const auto lines = BuildHelpReply(entries, registry, AccountRole::Player, std::to_string(page));
		playerSeesGm = playerSeesGm || AnyLineContains(lines, "/gmaudit") || AnyLineContains(lines, "/shardinfo");
	}
	Check(!playerSeesGm, "/help : un joueur ne voit pas les commandes réservées");

	// --- /help <commande> : fiche complète ---
	const auto detail = BuildHelpReply(entries, registry, AccountRole::GameMaster, "/shardinfo");
	Check(AnyLineContains(detail, "Syntaxe : /shardinfo") && AnyLineContains(detail, "Exemple :")
		&& AnyLineContains(detail, "Rôle minimum : maître de jeu"), "/help /shardinfo : fiche");
	const auto aliasDetail = BuildHelpReply(entries, registry, AccountRole::Player, "aide");
	Check(AnyLineContains(aliasDetail, "/help"), "/help aide : fiche de la commande via son alias");
	const auto denied = BuildHelpReply(entries, registry, AccountRole::Player, "gmaudit");
	Check(AnyLineContains(denied, "vous n'y avez pas accès"), "/help d'une commande réservée : accès signalé");
	const auto unknown = BuildHelpReply(entries, registry, AccountRole::Player, "zzz");
	Check(AnyLineContains(unknown, "inconnue"), "/help zzz : inconnue");

	// --- /help <catégorie> ---
	const auto core = BuildHelpReply(entries, registry, AccountRole::Administrator, "core");
	Check(AnyLineContains(core, "Aide et outils") && AnyLineContains(core, "/search"), "/help core : catégorie");

	// --- /search ---
	const auto search = BuildSearchReply(entries, AccountRole::Player, "cherche");
	Check(AnyLineContains(search, "/search"), "/search cherche");
	Check(AnyLineContains(BuildSearchReply(entries, AccountRole::Player, ""), "Syntaxe"), "/search sans mot");
	Check(AnyLineContains(BuildSearchReply(entries, AccountRole::Player, "zzzz"), "Aucune"), "/search sans résultat");

	if (s_failCount == 0)
		std::cout << "command_help_tests : tout est vert" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
