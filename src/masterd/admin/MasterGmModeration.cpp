// ADM.8 — Commandes de modération exécutées par le master.
//
// /unmute, /mutes, /unban, /bans, /warn, /history, /notify, /gmw, /csr.
// Les commandes historiques /kick, /mute, /ban, /announce, /who, /report restent
// interceptées par le client et traitées par AdminCommandHandler (opcode 195).
// Chaque appel est tracé par le rappel d'audit (gm_command_audit, cible nommée
// comprise) : /history <joueur> relit cette table.

#include "src/masterd/admin/MasterGmCommands.h"

#include "src/masterd/account/AccountRecord.h"
#include "src/masterd/account/AccountStore.h"
#include "src/masterd/admin/GmAuditStore.h"
#include "src/masterd/admin/GmModerationStore.h"
#include "src/masterd/admin/SlashCommandRegistry.h"
#include "src/masterd/chat/ChatCommandRouter.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionCharacterMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/account/AccountValidation.h"
#include "src/shared/admin/GmCommandText.h"
#include "src/shared/core/Log.h"

#include <chrono>

namespace engine::server
{
	void MasterGmCommands::SendToConnection(uint32_t connId, std::string_view line) const
	{
		if (m_s.connMap == nullptr)
			return;
		for (const auto& [conn, sessionId] : m_s.connMap->Snapshot())
		{
			if (conn == connId)
			{
				Reply(conn, sessionId, line);
				return;
			}
		}
	}

	size_t MasterGmCommands::BroadcastLine(std::string_view line) const
	{
		if (m_s.connMap == nullptr)
			return 0;
		size_t sent = 0;
		for (const auto& [conn, sessionId] : m_s.connMap->Snapshot())
		{
			Reply(conn, sessionId, line);
			++sent;
		}
		return sent;
	}

	void MasterGmCommands::RegisterModeration()
	{
		using chat::CommandCallContext;
		const auto nowMs = []() {
			return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		};
		// Nom affiché de l'appelant (personnage en jeu, sinon login).
		auto actorLabel = [this](const CommandCallContext& ctx) -> std::string {
			if (m_s.charMap != nullptr)
			{
				if (const auto info = m_s.charMap->GetByConnId(ctx.connId))
					return info->characterName;
			}
			if (m_s.accounts != nullptr)
			{
				if (const auto rec = m_s.accounts->FindByAccountId(ctx.accountId))
					return rec->login;
			}
			return "#" + std::to_string(ctx.accountId);
		};

		// /unmute <joueur|login> — lève la sanction de chat (modérateur).
		m_s.router->RegisterEx("/unmute", AccountRole::Moderator, [this](const CommandCallContext& ctx, std::string_view args) {
			const auto words = engine::admin::SplitArgs(args);
			if (words.empty())
				return Reply(ctx.connId, ctx.sessionId, "Syntaxe : /unmute <joueur|login>");
			if (m_s.db == nullptr)
				return Reply(ctx.connId, ctx.sessionId, "Sanctions indisponibles (base non configurée).");
			const uint64_t accountId = ResolveAccount(words[0]);
			if (accountId == 0u)
				return Reply(ctx.connId, ctx.sessionId, "Joueur ou compte introuvable : " + words[0]);
			const int removed = DeleteChatMute(*m_s.db, accountId);
			if (removed < 0)
				return Reply(ctx.connId, ctx.sessionId, "Base indisponible : sanction non levée.");
			if (removed == 0)
				return Reply(ctx.connId, ctx.sessionId, words[0] + " n'a pas de sanction de chat.");
			Reply(ctx.connId, ctx.sessionId, "Sanction de chat de " + words[0] + " levée.");
			if (const auto online = FindOnlineCharacter(words[0]))
				SendToConnection(online->connId, "Vous pouvez de nouveau parler dans le chat.");
		});

		// /mutes — sanctions de chat en cours (modérateur).
		m_s.router->RegisterEx("/mutes", AccountRole::Moderator, [this, nowMs](const CommandCallContext& ctx, std::string_view) {
			if (m_s.db == nullptr)
				return Reply(ctx.connId, ctx.sessionId, "Sanctions indisponibles (base non configurée).");
			std::vector<GmMuteRow> rows;
			const uint64_t now = nowMs();
			if (!ListActiveMutes(*m_s.db, now, 30u, rows))
				return Reply(ctx.connId, ctx.sessionId, "Base indisponible.");
			if (rows.empty())
				return Reply(ctx.connId, ctx.sessionId, "Aucune sanction de chat en cours.");
			Reply(ctx.connId, ctx.sessionId, std::to_string(rows.size()) + " sanction(s) de chat en cours :");
			for (const GmMuteRow& r : rows)
			{
				const std::string remaining = r.untilTsMs == 0u ? std::string("permanente")
					: "encore " + engine::admin::FormatDurationFr((r.untilTsMs - now) / 1000u);
				Reply(ctx.connId, ctx.sessionId, "  " + (r.login.empty() ? "#" + std::to_string(r.accountId) : r.login)
					+ " — " + remaining + (r.reason.empty() ? std::string() : " — " + r.reason));
			}
		});

		// /unban <login> — réactive un compte banni ou verrouillé (administrateur).
		m_s.router->RegisterEx("/unban", AccountRole::Administrator, [this](const CommandCallContext& ctx, std::string_view args) {
			const auto words = engine::admin::SplitArgs(args);
			if (words.empty())
				return Reply(ctx.connId, ctx.sessionId, "Syntaxe : /unban <login>");
			if (m_s.accounts == nullptr)
				return Reply(ctx.connId, ctx.sessionId, "Comptes indisponibles.");
			const auto rec = m_s.accounts->FindByLogin(NormaliseLoginView(words[0]));
			if (!rec)
				return Reply(ctx.connId, ctx.sessionId, "Compte introuvable : " + words[0]);
			if (rec->status == AccountStatus::Active)
				return Reply(ctx.connId, ctx.sessionId, rec->login + " n'est pas banni.");
			if (!m_s.accounts->SetAccountStatus(rec->account_id, AccountStatus::Active))
				return Reply(ctx.connId, ctx.sessionId, "Base indisponible : compte non réactivé.");
			Reply(ctx.connId, ctx.sessionId, "Compte " + rec->login + " réactivé : il peut de nouveau se connecter.");
		});

		// /bans [mot] — comptes bannis ou verrouillés (modérateur).
		m_s.router->RegisterEx("/bans", AccountRole::Moderator, [this](const CommandCallContext& ctx, std::string_view args) {
			if (m_s.db == nullptr)
				return Reply(ctx.connId, ctx.sessionId, "Liste indisponible (base non configurée).");
			std::vector<std::string> logins;
			if (!ListLockedAccounts(*m_s.db, args, 50u, logins))
				return Reply(ctx.connId, ctx.sessionId, "Base indisponible.");
			if (logins.empty())
				return Reply(ctx.connId, ctx.sessionId, "Aucun compte banni" + (args.empty() ? std::string(".") : " pour « " + std::string(args) + " »."));
			std::string line = std::to_string(logins.size()) + " compte(s) banni(s) :";
			for (const std::string& l : logins)
			{
				if (line.size() + l.size() + 2u > 240u)
				{
					Reply(ctx.connId, ctx.sessionId, line);
					line = " ";
				}
				line += " " + l;
			}
			Reply(ctx.connId, ctx.sessionId, line);
		});

		// /warn <joueur> <message> — avertissement officiel (modérateur), tracé dans l'audit.
		m_s.router->RegisterEx("/warn", AccountRole::Moderator, [this](const CommandCallContext& ctx, std::string_view args) {
			const auto words = engine::admin::SplitArgs(args);
			if (words.size() < 2u)
				return Reply(ctx.connId, ctx.sessionId, "Syntaxe : /warn <joueur> <message>");
			const auto target = FindOnlineCharacter(words[0]);
			if (!target)
				return Reply(ctx.connId, ctx.sessionId, "Joueur introuvable (pas en jeu) : " + words[0]);
			const std::string text = engine::admin::JoinArgs(words, 1);
			SendToConnection(target->connId, "[Avertissement de la modération] " + text);
			Reply(ctx.connId, ctx.sessionId, "Avertissement envoyé à " + target->characterName + " (consigné : /history " + target->characterName + ").");
		});

		// /history <joueur|login> [n] — commandes réservées visant ce joueur ou tapées par lui.
		m_s.router->RegisterEx("/history", AccountRole::Moderator, [this](const CommandCallContext& ctx, std::string_view args) {
			const auto words = engine::admin::SplitArgs(args);
			if (words.empty())
				return Reply(ctx.connId, ctx.sessionId, "Syntaxe : /history <joueur|login> [nombre]");
			if (m_s.db == nullptr)
				return Reply(ctx.connId, ctx.sessionId, "Historique indisponible (base non configurée).");
			size_t limit = 15u;
			if (words.size() >= 2u)
			{
				if (const auto n = engine::admin::ParseInt(words[1]); n && *n > 0)
					limit = static_cast<size_t>((std::min<int64_t>)(*n, 50));
			}
			std::string name = words[0];
			if (!name.empty() && name.front() == '@')
				name.erase(name.begin());
			std::vector<GmAuditRecord> rows;
			if (!ListGmAudit(*m_s.db, name, limit, rows))
				return Reply(ctx.connId, ctx.sessionId, "Historique illisible (base indisponible ou migration 0085 absente).");
			if (rows.empty())
				return Reply(ctx.connId, ctx.sessionId, "Aucune commande réservée ne concerne " + name + ".");
			Reply(ctx.connId, ctx.sessionId, "Historique de " + name + " (" + std::to_string(rows.size()) + " ligne(s)) :");
			for (const GmAuditRecord& r : rows)
			{
				Reply(ctx.connId, ctx.sessionId, "  " + r.createdAtUtc + " " + (r.login.empty() ? "#" + std::to_string(r.accountId) : r.login)
					+ " : /" + r.command + (r.args.empty() ? std::string() : " " + r.args) + (r.result != "ok" ? " → " + r.result : std::string()));
			}
		});

		// /notify <message> — annonce système à tous les joueurs connectés (maître de jeu).
		m_s.router->RegisterEx("/notify", AccountRole::GameMaster, [this](const CommandCallContext& ctx, std::string_view args) {
			const std::string text(engine::admin::Trim(args));
			if (text.empty())
				return Reply(ctx.connId, ctx.sessionId, "Syntaxe : /notify <message>");
			const size_t n = BroadcastLine("[Annonce] " + text);
			LOG_INFO(Net, "[MasterGmCommands] ADM.8 /notify par account={} vers {} connexion(s)", ctx.accountId, n);
		});

		// /gmw <joueur> <message> — message direct de l'équipe (modérateur), alias /gmwhisper.
		auto gmw = [this, actorLabel](const CommandCallContext& ctx, std::string_view args) {
			const auto words = engine::admin::SplitArgs(args);
			if (words.size() < 2u)
				return Reply(ctx.connId, ctx.sessionId, "Syntaxe : /gmw <joueur> <message>");
			const auto target = FindOnlineCharacter(words[0]);
			if (!target)
				return Reply(ctx.connId, ctx.sessionId, "Joueur introuvable (pas en jeu) : " + words[0]);
			const std::string text = engine::admin::JoinArgs(words, 1);
			SendToConnection(target->connId, "[Équipe] " + actorLabel(ctx) + " : " + text);
			Reply(ctx.connId, ctx.sessionId, "→ " + target->characterName + " : " + text);
		};
		m_s.router->RegisterEx("/gmw", AccountRole::Moderator, gmw);
		m_s.router->RegisterEx("/gmwhisper", AccountRole::Moderator, gmw);

		// /csr <joueur> <commande> [arguments] — exécute une commande du shard pour le
		// compte d'un joueur (administrateur ; idée reprise de City of Heroes).
		m_s.router->RegisterEx("/csr", AccountRole::Administrator, [this](const CommandCallContext& ctx, std::string_view args) {
			const auto words = engine::admin::SplitArgs(args);
			if (words.size() < 2u)
				return Reply(ctx.connId, ctx.sessionId, "Syntaxe : /csr <joueur> <commande> [arguments]");
			const auto target = FindOnlineCharacter(words[0]);
			if (!target)
				return Reply(ctx.connId, ctx.sessionId, "Joueur introuvable (pas en jeu) : " + words[0]);
			const SlashCommandEntry* entry = m_s.registry->Lookup(words[1]);
			if (entry == nullptr || entry->status != kSlashStatusShardCommand)
				return Reply(ctx.connId, ctx.sessionId, words[1] + " n'est pas une commande exécutée par le shard (voir /help).");
			Reply(ctx.connId, ctx.sessionId, "Exécution de " + entry->command + " pour " + target->characterName + " :");
			ForwardToShard(ctx, *entry, engine::admin::JoinArgs(words, 2), target->characterId, target->shardId);
		});
	}
}
