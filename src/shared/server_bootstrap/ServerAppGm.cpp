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

#include "src/shared/server_bootstrap/ServerApp.h"

#include "src/shardd/gameplay/gm/GmCommandCatalog.h"
#include "src/shared/admin/GmCommandText.h"
#include "src/shared/core/Log.h"

#include <algorithm>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::server
{
	using engine::network::GmCommandStatus;

	/// Exécuteur d'UNE commande MJ (durée de vie : l'appel à ServerApp::ExecuteGmCommand).
	/// Ami de ServerApp : lit et modifie l'état du tick directement.
	struct GmCommandExecutor
	{
		ServerApp& app;
		const engine::network::GmCommandRequestPayload& request;
		engine::network::GmCommandResultPayload& out;
		/// Arguments découpés (guillemets pris en charge, cf. GmCommandText.h).
		std::vector<std::string> args;
		/// Rôle de l'appelant tel que transmis par le master.
		gm::GmRole role = gm::GmRole::Player;
		/// Personnage de l'appelant s'il est connecté à CE shard (nullptr sinon).
		ConnectedClient* actor = nullptr;

		using Handler = void (GmCommandExecutor::*)();

		// ---- Outils communs -------------------------------------------------

		/// Ajoute une ligne de réponse (affichée telle quelle dans le chat de l'appelant).
		void Say(std::string line) { out.lines.push_back(std::move(line)); }

		/// Fixe le statut d'échec et ajoute le message d'explication.
		void Fail(GmCommandStatus status, std::string line)
		{
			out.status = static_cast<uint8_t>(status);
			Say(std::move(line));
		}

		/// Erreur de syntaxe : rappelle la forme attendue (\p syntax, ex. « /tp <x> <z> »).
		void Usage(std::string_view syntax)
		{
			Fail(GmCommandStatus::Usage, "Syntaxe : " + std::string(syntax) + " — détail : /help /" + request.command);
		}

		/// L'appelant doit être en jeu sur ce shard (commandes qui agissent sur lui).
		/// \return false (et message) sinon.
		bool RequireActor()
		{
			if (actor != nullptr)
				return true;
			Fail(GmCommandStatus::NotFound, "Votre personnage n'est pas en jeu sur ce shard.");
			return false;
		}

		/// Rôle de l'appelant au moins \p minRole ?
		bool HasRole(gm::GmRole minRole) const
		{
			return static_cast<uint8_t>(role) >= static_cast<uint8_t>(minRole);
		}

		/// Nom affiché d'un joueur (nom du personnage, sinon « P<id> »).
		static std::string NameOf(const ConnectedClient& client)
		{
			return client.characterName.empty() ? "P" + std::to_string(client.clientId) : client.characterName;
		}

		/// Joueur connecté à ce shard par nom de personnage (sans casse) ou « P<id> ».
		/// Un « @ » de tête est ignoré (« @Hubert »). nullptr si introuvable.
		ConnectedClient* FindPlayer(std::string_view name)
		{
			name = engine::admin::Trim(name);
			if (!name.empty() && name.front() == '@')
				name.remove_prefix(1);
			if (name.empty())
				return nullptr;
			for (ConnectedClient& c : app.m_clients)
			{
				if (!c.characterName.empty() && engine::admin::EqualsIgnoreCase(c.characterName, name))
					return &c;
			}
			for (ConnectedClient& c : app.m_clients)
			{
				if (engine::admin::EqualsIgnoreCase("P" + std::to_string(c.clientId), name))
					return &c;
			}
			return nullptr;
		}

		/// Cible facultative : \p args[index] s'il existe (joueur nommé), sinon l'appelant.
		/// \return nullptr (et message) si le joueur nommé est introuvable ou si
		///         l'appelant n'est pas en jeu.
		ConnectedClient* TargetOrSelf(size_t index)
		{
			if (index < args.size())
			{
				ConnectedClient* target = FindPlayer(args[index]);
				if (target == nullptr)
					Fail(GmCommandStatus::NotFound, "Joueur introuvable sur ce shard : " + args[index]);
				return target;
			}
			return RequireActor() ? actor : nullptr;
		}

		/// Prévient \p target (message système dans son chat) s'il n'est pas l'appelant.
		void NotifyTarget(ConnectedClient& target, std::string_view text)
		{
			if (&target == actor)
				return;
			app.SendChatSystemNotice(target, text);
		}

		// ---- ADM.0 — socle ---------------------------------------------------

		/// /shardinfo — état du shard : joueurs, mobs, spawners actifs, cadence du tick,
		/// temps de fonctionnement et délestage.
		void CmdShardInfo()
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

		/// Point d'entrée : catalogue, rôle, cible par défaut, handler, journal.
		void Run();
	};

	/// Nom canonique → méthode de GmCommandExecutor. Toute entrée de kGmShardCommands
	/// doit y figurer ; sinon l'appel répond « pas encore disponible » (Unavailable).
	constexpr std::pair<std::string_view, GmCommandExecutor::Handler> kGmHandlers[] = {
		{ "shardinfo", &GmCommandExecutor::CmdShardInfo },
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
