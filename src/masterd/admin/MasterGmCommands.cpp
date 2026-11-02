// ADM.0 — Commandes de jeu côté master : voir MasterGmCommands.h.

#include "src/masterd/admin/MasterGmCommands.h"

#include "src/masterd/account/AccountStore.h"
#include "src/masterd/admin/GmAuditStore.h"
#include "src/masterd/admin/GmShardRelay.h"
#include "src/masterd/admin/SlashCommandRegistry.h"
#include "src/masterd/chat/ChatCommandRouter.h"
#include "src/masterd/chat/CommandHelp.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionCharacterMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/masterd/shards/ShardRegistry.h"
#include "src/shared/admin/GmCommandText.h"
#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/net/ChatSystem.h"
#include "src/shared/network/ChatPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ShardPayloads.h"

#include <chrono>

namespace engine::server
{
	namespace
	{
		/// Longueur maximale d'une ligne de réponse (limite du chat : 256 octets).
		constexpr size_t kReplyLineBytes = 250u;

		uint64_t NowUnixMs()
		{
			return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		}
	}

	AccountRole MasterGmCommands::RoleOf(uint64_t accountId) const
	{
		return m_s.accounts != nullptr ? m_s.accounts->GetRole(accountId) : AccountRole::Player;
	}

	void MasterGmCommands::Reply(uint32_t connId, uint64_t sessionId, std::string_view line) const
	{
		if (m_s.server == nullptr || connId == 0u)
			return;
		for (const std::string& part : engine::admin::WrapUtf8(line, kReplyLineBytes))
		{
			auto packet = engine::network::BuildChatRelayPacket(NowUnixMs(),
				static_cast<uint8_t>(engine::net::ChatChannel::Server), "system", part, sessionId);
			if (!packet.empty())
				m_s.server->Send(connId, packet);
		}
	}

	void MasterGmCommands::ReplyLines(uint32_t connId, uint64_t sessionId, const std::vector<std::string>& lines) const
	{
		for (const std::string& line : lines)
			Reply(connId, sessionId, line);
	}

	void MasterGmCommands::ReplyToAccount(uint64_t accountId, uint32_t connIdHint, std::string_view line) const
	{
		if (m_s.connMap == nullptr || m_s.sessions == nullptr)
			return;
		// Connexion d'origine si elle appartient toujours au compte, sinon la première
		// connexion active du compte.
		uint32_t fallbackConn = 0;
		uint64_t fallbackSession = 0;
		for (const auto& [connId, sessionId] : m_s.connMap->Snapshot())
		{
			const auto acc = m_s.sessions->GetAccountId(sessionId);
			if (!acc || *acc != accountId)
				continue;
			if (connId == connIdHint)
			{
				Reply(connId, sessionId, line);
				return;
			}
			if (fallbackConn == 0u)
			{
				fallbackConn = connId;
				fallbackSession = sessionId;
			}
		}
		if (fallbackConn != 0u)
			Reply(fallbackConn, fallbackSession, line);
	}

	void MasterGmCommands::RegisterAll()
	{
		if (m_s.router == nullptr || m_s.registry == nullptr)
		{
			LOG_WARN(Net, "[MasterGmCommands] ADM.0 routeur ou registre absent : commandes de jeu non enregistrées");
			return;
		}
		if (m_s.relay != nullptr)
		{
			m_s.relay->SetReply([this](uint64_t accountId, uint32_t connId, const std::string& line) {
				ReplyToAccount(accountId, connId, line);
			});
			m_s.relay->SetSend([this](uint32_t shardId, const std::vector<uint8_t>& packet) {
				if (m_s.shards == nullptr || m_s.server == nullptr)
					return false;
				const auto conn = m_s.shards->GetShardConnection(shardId);
				return conn.has_value() && m_s.server->Send(*conn, packet);
			});
		}
		RegisterCore();
		RegisterShardForwarders();
		InstallAuditSink();
		LOG_INFO(Net, "[MasterGmCommands] ADM.0 commandes de jeu enregistrées ({} entrées dans le registre)",
			m_s.registry->Size());
	}

	void MasterGmCommands::RegisterCore()
	{
		using chat::CommandCallContext;
		// /help [page | commande | catégorie [page]] — alias /commands, /aide, /?.
		auto help = [this](const CommandCallContext& ctx, std::string_view args) {
			ReplyLines(ctx.connId, ctx.sessionId,
				chat::BuildHelpReply(m_s.registry->Entries(), *m_s.registry, ctx.role, args));
		};
		for (const char* name : { "/help", "/commands", "/aide", "/?" })
			m_s.router->RegisterEx(name, AccountRole::Player, help);

		// /search <mot> — alias /apropos.
		auto search = [this](const CommandCallContext& ctx, std::string_view args) {
			ReplyLines(ctx.connId, ctx.sessionId, chat::BuildSearchReply(m_s.registry->Entries(), ctx.role, args));
		};
		m_s.router->RegisterEx("/search", AccountRole::Player, search);
		m_s.router->RegisterEx("/apropos", AccountRole::Player, search);

		// /gmaudit [n] [login|cible] — dernières commandes réservées (administrateur).
		m_s.router->RegisterEx("/gmaudit", AccountRole::Administrator,
			[this](const CommandCallContext& ctx, std::string_view args) {
				if (m_s.db == nullptr)
				{
					Reply(ctx.connId, ctx.sessionId, "Journal d'audit indisponible (base non configurée).");
					return;
				}
				const auto words = engine::admin::SplitArgs(args);
				size_t limit = 15u;
				std::string filter;
				for (const std::string& w : words)
				{
					if (const auto n = engine::admin::ParseInt(w); n && *n > 0)
						limit = static_cast<size_t>((std::min<int64_t>)(*n, 50));
					else
						filter = w;
				}
				std::vector<GmAuditRecord> rows;
				if (!ListGmAudit(*m_s.db, filter, limit, rows))
				{
					Reply(ctx.connId, ctx.sessionId, "Journal d'audit illisible (base indisponible ou migration 0085 absente).");
					return;
				}
				if (rows.empty())
				{
					Reply(ctx.connId, ctx.sessionId, "Journal d'audit vide" + (filter.empty() ? std::string(".") : " pour " + filter + "."));
					return;
				}
				Reply(ctx.connId, ctx.sessionId, "Audit — " + std::to_string(rows.size()) + " dernière(s) commande(s)"
					+ (filter.empty() ? std::string(" :") : " (" + filter + ") :"));
				for (const GmAuditRecord& r : rows)
				{
					std::string line = r.createdAtUtc + " " + (r.login.empty() ? "#" + std::to_string(r.accountId) : r.login)
						+ " [" + r.role + "] /" + r.command;
					if (!r.args.empty())
						line += " " + r.args;
					if (r.result != "ok")
						line += " → " + r.result;
					Reply(ctx.connId, ctx.sessionId, line);
				}
			});
	}

	void MasterGmCommands::RegisterShardForwarders()
	{
		size_t count = 0;
		for (const SlashCommandEntry& entry : m_s.registry->Entries())
		{
			if (entry.status != kSlashStatusShardCommand)
				continue;
			// L'entrée est copiée dans le handler (le registre est en lecture seule après
			// le chargement, mais la copie évite toute dépendance à son adresse).
			auto forward = [this, entry](const chat::CommandCallContext& ctx, std::string_view args) {
				ForwardToShard(ctx, entry, args);
			};
			m_s.router->RegisterEx(entry.command, entry.minRole, forward);
			for (const std::string& alias : entry.aliases)
				m_s.router->RegisterEx(alias, entry.minRole, forward);
			++count;
		}
		LOG_INFO(Net, "[MasterGmCommands] ADM.0 {} commande(s) relayée(s) aux shards", count);
	}

	void MasterGmCommands::ForwardToShard(const chat::CommandCallContext& ctx, const SlashCommandEntry& entry,
		std::string_view args, uint64_t actorCharacterOverride, uint32_t shardOverride) const
	{
		if (m_s.relay == nullptr || m_s.charMap == nullptr)
		{
			Reply(ctx.connId, ctx.sessionId, "Commandes de jeu indisponibles (relais vers les shards non câblé).");
			return;
		}
		const auto info = m_s.charMap->GetByConnId(ctx.connId);
		if (!info && actorCharacterOverride == 0u)
		{
			Reply(ctx.connId, ctx.sessionId, entry.command + " : entrez d'abord en jeu avec un personnage.");
			return;
		}
		// Protection par rang : une commande ne vise jamais un joueur de rôle supérieur
		// à celui de l'appelant (un MJ ne gèle pas un administrateur). Tout argument
		// qui nomme un personnage en jeu est contrôlé.
		if (m_s.accounts != nullptr)
		{
			for (const std::string& word : engine::admin::SplitArgs(args))
			{
				std::string name = word;
				if (!name.empty() && name.front() == '@')
					name.erase(name.begin());
				if (name.empty() || engine::admin::ParseDouble(name))
					continue;
				const auto conn = m_s.charMap->FindConnByNormalizedName(SessionCharacterMap::Normalize(name));
				if (!conn)
					continue;
				const auto target = m_s.charMap->GetByConnId(*conn);
				if (target && target->accountId != ctx.accountId && m_s.accounts->GetRole(target->accountId) > ctx.role)
				{
					Reply(ctx.connId, ctx.sessionId, entry.command + " : " + target->characterName
						+ " a un rôle supérieur au vôtre, commande refusée.");
					return;
				}
			}
		}
		uint32_t shardId = shardOverride;
		if (shardId == 0u && info)
			shardId = info->shardId != 0u ? info->shardId : m_s.charMap->TicketShard(ctx.connId).value_or(0u);
		engine::network::GmCommandRequestPayload request;
		request.actorAccountId = ctx.accountId;
		request.actorCharacterId = actorCharacterOverride != 0u ? actorCharacterOverride : (info ? info->characterId : 0u);
		request.actorRole = static_cast<uint8_t>(ctx.role);
		request.actorName = info ? info->characterName : std::string();
		request.command = engine::admin::NormalizeCommandName(entry.command);
		request.args = std::string(args);
		const double now = m_s.nowSeconds ? m_s.nowSeconds() : 0.0;
		switch (m_s.relay->Forward(shardId, ctx.connId, std::move(request), now))
		{
		case GmShardRelay::ForwardResult::Sent:
			return;
		case GmShardRelay::ForwardResult::NoShard:
			Reply(ctx.connId, ctx.sessionId, entry.command + " : shard de votre personnage inconnu (reconnectez-vous).");
			return;
		case GmShardRelay::ForwardResult::SendFailed:
			Reply(ctx.connId, ctx.sessionId, entry.command + " : le shard est injoignable pour le moment.");
			return;
		case GmShardRelay::ForwardResult::Busy:
			Reply(ctx.connId, ctx.sessionId, "Trop de commandes en attente : patientez quelques secondes.");
			return;
		case GmShardRelay::ForwardResult::Invalid:
			Reply(ctx.connId, ctx.sessionId, entry.command + " : commande impossible à relayer (arguments trop longs ?).");
			return;
		}
	}

	void MasterGmCommands::InstallAuditSink()
	{
		m_s.router->SetAuditSink([this](const chat::CommandCallContext& ctx, std::string_view args,
			chat::CommandDispatchResult result) {
			const bool denied = result == chat::CommandDispatchResult::InsufficientRole;
			// Commandes de joueur exécutées : pas d'audit (aide, heure…), seulement le journal.
			if (!denied && ctx.minRole == AccountRole::Player)
				return;
			LOG_INFO(Net, "[GmAudit] account={} role={} cmd=/{} args='{}' result={}", ctx.accountId,
				RoleToString(ctx.role), ctx.command, engine::admin::TruncateUtf8(args, 120), denied ? "denied" : "ok");
			if (m_s.db == nullptr)
				return;
			GmAuditRecord record;
			record.accountId = ctx.accountId;
			record.role = std::string(RoleToString(ctx.role));
			record.command = ctx.command;
			record.args = std::string(args);
			record.result = denied ? "denied" : "ok";
			// Cible nommée : premier argument qui n'est pas un nombre (convention des
			// commandes « <joueur> … »), « @nom » compris.
			for (const std::string& w : engine::admin::SplitArgs(args))
			{
				if (!engine::admin::ParseDouble(w) && !engine::admin::ParseOnOff(w))
				{
					record.targetName = (!w.empty() && w.front() == '@') ? w.substr(1) : w;
					break;
				}
			}
			(void)InsertGmAudit(*m_s.db, record);
		});
	}

	void MasterGmCommands::OnShardResultPacket(uint32_t connId, const uint8_t* payload, size_t payloadSize)
	{
		if (m_s.relay == nullptr || m_s.shards == nullptr)
			return;
		// La connexion doit être celle d'un shard enregistré : un client qui enverrait
		// 217 ne peut pas écrire dans le chat d'un autre compte.
		uint32_t fromShard = 0;
		for (const auto& shard : m_s.shards->ListShards())
		{
			const auto c = m_s.shards->GetShardConnection(shard.shard_id);
			if (c && *c == connId)
			{
				fromShard = shard.shard_id;
				break;
			}
		}
		if (fromShard == 0u)
		{
			LOG_WARN(Net, "[MasterGmCommands] ADM.0 résultat de commande hors connexion de shard ignoré (connId={})", connId);
			return;
		}
		const auto parsed = engine::network::ParseGmCommandResultPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[MasterGmCommands] ADM.0 résultat de commande illisible (shard={})", fromShard);
			return;
		}
		(void)m_s.relay->OnResult(fromShard, *parsed);
	}

	void MasterGmCommands::Tick()
	{
		if (m_s.relay != nullptr && m_s.nowSeconds)
			(void)m_s.relay->Tick(m_s.nowSeconds());
	}
}
