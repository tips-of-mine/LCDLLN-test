// ADM.9 — Commandes « serveur » exécutées par le master.
//
// /serverinfo, /motd, /setmotd, /closed. (/saveall est exécutée par le shard,
// /shutdown et /promote restent dans AdminCommandHandler.)
//
// « /closed on » refuse les nouvelles entrées en jeu (choix du personnage et
// ticket de shard) aux comptes sans rôle de modérateur ou plus ; les joueurs déjà
// en jeu restent connectés. L'état n'est pas persisté : un redémarrage du master
// rouvre le serveur.

#include "src/masterd/admin/MasterGmCommands.h"

#include "src/masterd/account/AccountStore.h"
#include "src/masterd/chat/ChatCommandRouter.h"
#include "src/masterd/chat/CommandHelp.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionCharacterMap.h"
#include "src/masterd/shards/ShardRegistry.h"
#include "src/shared/admin/GmCommandText.h"
#include "src/shared/core/Log.h"

#include <ctime>

namespace engine::server
{
	bool MasterGmCommands::IsAdmissionRefused(uint64_t accountId) const
	{
		if (!m_closed)
			return false;
		return RoleOf(accountId) < AccountRole::Moderator;
	}

	void MasterGmCommands::OnEnteredWorld(uint32_t connId, uint64_t accountId) const
	{
		if (!m_motd.empty())
			SendToConnection(connId, "[Message du jour] " + m_motd);
		if (m_closed && RoleOf(accountId) >= AccountRole::Moderator)
			SendToConnection(connId, "Rappel : le serveur est fermé aux joueurs (/closed off pour rouvrir).");
	}

	void MasterGmCommands::RegisterServer()
	{
		using chat::CommandCallContext;

		// /serverinfo — démarrage, durée de fonctionnement, ouverture, message du jour.
		m_s.router->RegisterEx("/serverinfo", AccountRole::Moderator, [this](const CommandCallContext& ctx, std::string_view) {
			const auto up = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_startedAt).count();
			const std::time_t started = std::chrono::system_clock::to_time_t(m_startedAtWall);
			std::tm tmv{};
			gmtime_r(&started, &tmv);
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d UTC", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
				tmv.tm_hour, tmv.tm_min);
			Reply(ctx.connId, ctx.sessionId, std::string("Master démarré le ") + buf + " (depuis "
				+ engine::admin::FormatDurationFr(static_cast<uint64_t>(up)) + ").");
			const size_t connections = m_s.connMap != nullptr ? m_s.connMap->Snapshot().size() : 0u;
			const size_t inWorld = m_s.charMap != nullptr ? m_s.charMap->Count() : 0u;
			const size_t shards = m_s.shards != nullptr ? m_s.shards->ListShards().size() : 0u;
			Reply(ctx.connId, ctx.sessionId, std::to_string(connections) + " connexion(s), " + std::to_string(inWorld)
				+ " en jeu, " + std::to_string(shards) + " shard(s) — détail : /serverstats.");
			Reply(ctx.connId, ctx.sessionId, std::string("Accès : ") + (m_closed ? "FERMÉ aux joueurs" + (m_closedReason.empty()
				? std::string() : " (" + m_closedReason + ")") : std::string("ouvert")) + " — message du jour : "
				+ (m_motd.empty() ? std::string("aucun") : "« " + m_motd + " »") + ".");
		});

		// /motd — affiche le message du jour.
		m_s.router->RegisterEx("/motd", AccountRole::Player, [this](const CommandCallContext& ctx, std::string_view) {
			Reply(ctx.connId, ctx.sessionId, m_motd.empty() ? std::string("Pas de message du jour.") : "[Message du jour] " + m_motd);
		});

		// /setmotd <texte> | clear — change le message du jour et le diffuse (administrateur).
		m_s.router->RegisterEx("/setmotd", AccountRole::Administrator, [this](const CommandCallContext& ctx, std::string_view args) {
			const std::string text(engine::admin::Trim(args));
			if (text.empty())
				return Reply(ctx.connId, ctx.sessionId, "Syntaxe : /setmotd <texte> | /setmotd clear");
			if (engine::admin::EqualsIgnoreCase(text, "clear") || engine::admin::EqualsIgnoreCase(text, "off"))
			{
				m_motd.clear();
				return Reply(ctx.connId, ctx.sessionId, "Message du jour supprimé.");
			}
			m_motd = engine::admin::TruncateUtf8(text, 200);
			const size_t n = BroadcastLine("[Message du jour] " + m_motd);
			Reply(ctx.connId, ctx.sessionId, "Message du jour changé et diffusé à " + std::to_string(n) + " connexion(s).");
		});

		// /closed [on|off] [motif] — ferme ou rouvre le serveur aux joueurs (administrateur).
		m_s.router->RegisterEx("/closed", AccountRole::Administrator, [this](const CommandCallContext& ctx, std::string_view args) {
			const auto words = engine::admin::SplitArgs(args);
			if (words.empty())
			{
				return Reply(ctx.connId, ctx.sessionId, std::string("Serveur ") + (m_closed ? "FERMÉ aux joueurs." : "ouvert.")
					+ " Syntaxe : /closed on|off [motif]");
			}
			const auto on = engine::admin::ParseOnOff(words[0]);
			if (!on)
				return Reply(ctx.connId, ctx.sessionId, "Syntaxe : /closed on|off [motif]");
			m_closed = *on;
			m_closedReason = *on ? engine::admin::TruncateUtf8(engine::admin::JoinArgs(words, 1), 120) : std::string();
			LOG_WARN(Net, "[MasterGmCommands] ADM.9 serveur {} par account={} (motif='{}')", *on ? "fermé" : "rouvert",
				ctx.accountId, m_closedReason);
			Reply(ctx.connId, ctx.sessionId, *on
				? std::string("Serveur fermé : les joueurs (hors modérateurs, MJ, administrateurs) ne peuvent plus entrer en jeu ; ceux en jeu restent connectés.")
				: std::string("Serveur rouvert à tous."));
		});
	}
}
