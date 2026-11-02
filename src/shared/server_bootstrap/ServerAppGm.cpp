// ADM.0 — Exécution des commandes de jeu (MJ) côté shard.
//
// Le master a déjà vérifié le rôle de l'appelant (slash_commands.json) avant de
// relayer la commande (opcode 216). Ici, au thread de tick :
//   1. la commande est cherchée dans le catalogue du shard (GmCommandCatalog.h) ;
//   2. le rôle est contrôlé une seconde fois (défense en profondeur) ;
//   3. le handler s'exécute sur l'état autoritaire (joueurs, mobs, monde) ;
//   4. les lignes produites repartent au master (opcode 217) qui les affiche dans
//      le chat de l'appelant.
//
// Chaque handler est une méthode de GmCommandExecutor (ami de ServerApp) : la
// table kGmHandlers associe le nom canonique à la méthode. Les noms, rôles et
// textes d'aide vivent dans game/data/config/slash_commands.json (aide /help).
//
// Ce fichier est compilé dans les deux binaires du shard (server_app WIN32 et
// shard_app Linux) : aucun appel aux fonctions de ShardPayloads.cpp ici.

#include "src/shared/server_bootstrap/GmCommandExecutor.h"

#include "src/shared/core/Log.h"

#include <exception>

namespace engine::server
{
	// ---- ADM.0 — socle ------------------------------------------------------

	/// /shardinfo — état du shard : joueurs, mobs, spawners actifs, cadence du tick,
	/// temps de fonctionnement et délestage.
	void GmCommandExecutor::CmdShardInfo()
	{
		size_t activeSpawners = 0;
		for (const SpawnerRuntimeState& s : app.m_spawners)
		{
			if (s.isActive)
				++activeSpawners;
		}
		const uint32_t hz = app.m_tickHz == 0 ? 1u : app.m_tickHz;
		const uint64_t upSeconds = app.m_currentTick / hz;
		Say("Shard : " + std::to_string(app.m_clients.size()) + " joueur(s), " + std::to_string(app.m_mobs.size())
			+ " mob(s), " + std::to_string(app.m_lootBags.size()) + " sac(s) de butin.");
		Say("Spawners actifs : " + std::to_string(activeSpawners) + "/" + std::to_string(app.m_spawners.size())
			+ " — tick " + std::to_string(hz) + " Hz (tick n° " + std::to_string(app.m_currentTick) + ").");
		Say("En service depuis " + engine::admin::FormatDurationFr(upSeconds) + " — charge lissée "
			+ engine::admin::FormatFixed(app.m_loadShedder.SmoothedLoad() * 100.0, 0) + " %"
			+ (app.m_loadShedder.Active() ? " (délestage ACTIF)." : "."));
	}

	/// Nom canonique → méthode de GmCommandExecutor. Toute entrée de kGmShardCommands
	/// doit y figurer ; sinon l'appel répond « pas encore disponible » (Unavailable).
	constexpr std::pair<std::string_view, GmCommandExecutor::Handler> kGmHandlers[] = {
		{ "shardinfo", &GmCommandExecutor::CmdShardInfo },
		// ADM.1 — déplacement (ServerAppGmMovement.cpp)
		{ "tp", &GmCommandExecutor::CmdTeleport },
		{ "goto", &GmCommandExecutor::CmdGoto },
		{ "summon", &GmCommandExecutor::CmdSummon },
		{ "recall", &GmCommandExecutor::CmdRecall },
		{ "back", &GmCommandExecutor::CmdBack },
		{ "places", &GmCommandExecutor::CmdPlaces },
		{ "mark", &GmCommandExecutor::CmdMark },
		{ "marks", &GmCommandExecutor::CmdMarks },
		{ "gomark", &GmCommandExecutor::CmdGoMark },
		{ "unmark", &GmCommandExecutor::CmdUnmark },
		{ "forward", &GmCommandExecutor::CmdForward },
		{ "up", &GmCommandExecutor::CmdUp },
		{ "down", &GmCommandExecutor::CmdDown },
		{ "ground", &GmCommandExecutor::CmdGround },
		{ "spawn", &GmCommandExecutor::CmdSpawn },
		{ "speed", &GmCommandExecutor::CmdSpeed },
		{ "fly", &GmCommandExecutor::CmdFly },
		{ "freeze", &GmCommandExecutor::CmdFreeze },
		{ "unfreeze", &GmCommandExecutor::CmdUnfreeze },
		// ADM.2 — informations (ServerAppGmInfo.cpp)
		{ "pos", &GmCommandExecutor::CmdPos },
		{ "where", &GmCommandExecutor::CmdWhere },
		{ "info", &GmCommandExecutor::CmdInfo },
		{ "target", &GmCommandExecutor::CmdTarget },
		{ "zone", &GmCommandExecutor::CmdZone },
		{ "nearby", &GmCommandExecutor::CmdNearby },
		// ADM.3 — personnage (ServerAppGmCharacter.cpp)
		{ "level", &GmCommandExecutor::CmdLevel },
		{ "xp", &GmCommandExecutor::CmdXp },
		{ "heal", &GmCommandExecutor::CmdHeal },
		{ "revive", &GmCommandExecutor::CmdRevive },
		{ "kill", &GmCommandExecutor::CmdKill },
		{ "hp", &GmCommandExecutor::CmdHp },
		{ "mana", &GmCommandExecutor::CmdMana },
		{ "invincible", &GmCommandExecutor::CmdInvincible },
		{ "invisible", &GmCommandExecutor::CmdInvisible },
		{ "nocost", &GmCommandExecutor::CmdNoCost },
		{ "cooldown", &GmCommandExecutor::CmdCooldown },
		{ "learn", &GmCommandExecutor::CmdLearn },
		{ "unlearn", &GmCommandExecutor::CmdUnlearn },
		{ "resetskills", &GmCommandExecutor::CmdResetSkills },
		{ "profession", &GmCommandExecutor::CmdProfession },
		// ADM.4 — objets et argent (ServerAppGmItems.cpp)
		{ "additem", &GmCommandExecutor::CmdAddItem },
		{ "removeitem", &GmCommandExecutor::CmdRemoveItem },
		{ "money", &GmCommandExecutor::CmdMoney },
		{ "inventory", &GmCommandExecutor::CmdInventory },
		{ "equipment", &GmCommandExecutor::CmdEquipment },
		{ "clearbag", &GmCommandExecutor::CmdClearBag },
		{ "itemsearch", &GmCommandExecutor::CmdItemSearch },
		// ADM.5 — monture (ServerAppGmAppearance.cpp)
		{ "mount", &GmCommandExecutor::CmdMount },
		{ "dismount", &GmCommandExecutor::CmdDismount },
		// ADM.6 — monde (ServerAppGmWorld.cpp)
		{ "npc", &GmCommandExecutor::CmdNpc },
		{ "npcsearch", &GmCommandExecutor::CmdNpcSearch },
		{ "cleanup", &GmCommandExecutor::CmdCleanup },
		{ "respawn", &GmCommandExecutor::CmdRespawn },
		{ "revealmap", &GmCommandExecutor::CmdRevealMap },
		// ADM.7 — quêtes et événements (ServerAppGmQuests.cpp)
		{ "gmquest", &GmCommandExecutor::CmdGmQuest },
		{ "questsearch", &GmCommandExecutor::CmdQuestSearch },
		{ "event", &GmCommandExecutor::CmdEvent },
	};

	void GmCommandExecutor::Run()
	{
		out.status = static_cast<uint8_t>(GmCommandStatus::Ok);
		role = static_cast<gm::GmRole>(request.actorRole);
		const gm::GmShardCommandInfo* info = gm::FindGmShardCommand(request.command);
		if (info == nullptr)
		{
			Fail(GmCommandStatus::Unknown, "Commande /" + request.command
				+ " inconnue de ce shard (binaire plus ancien que le master ?).");
			return;
		}
		if (!HasRole(info->minRole))
		{
			Fail(GmCommandStatus::Denied, "Rôle insuffisant pour /" + request.command + ".");
			return;
		}
		Handler handler = nullptr;
		for (const auto& [name, h] : kGmHandlers)
		{
			if (name == request.command)
			{
				handler = h;
				break;
			}
		}
		if (handler == nullptr)
		{
			Fail(GmCommandStatus::Unavailable, "La commande /" + request.command + " n'est pas encore disponible sur ce shard.");
			return;
		}
		args = engine::admin::SplitArgs(request.args);
		// Identifiant 0 = aucun personnage (sinon le premier client sans clé serait choisi).
		actor = request.actorCharacterId != 0u ? app.FindConnectedClientByCharacterKey(request.actorCharacterId) : nullptr;
		try
		{
			(this->*handler)();
		}
		catch (const std::exception& e)
		{
			Fail(GmCommandStatus::Failed, "Erreur pendant /" + request.command + ".");
			LOG_ERROR(Net, "[GmCommand] exception pendant /{} : {}", request.command, e.what());
		}
		if (out.lines.empty())
			Say("/" + request.command + " : fait.");
	}

	void ServerApp::ExecuteGmCommand(const engine::network::GmCommandRequestPayload& request,
		engine::network::GmCommandResultPayload& out)
	{
		GmCommandExecutor executor{ *this, request, out, {}, gm::GmRole::Player, nullptr };
		executor.Run();
		// Audit : toute commande MJ exécutée (ou refusée) par le shard est journalisée.
		LOG_INFO(Net, "[GmCommand] account={} character={} role={} cmd=/{} args='{}' status={} lines={}",
			request.actorAccountId, request.actorCharacterId, static_cast<unsigned>(request.actorRole),
			request.command, engine::admin::TruncateUtf8(request.args, 120), static_cast<unsigned>(out.status),
			out.lines.size());
	}
}
