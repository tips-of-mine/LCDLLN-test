// Implementation AdminCommandHandler : dispatch + audit log obligatoire.
// Cf. AdminCommandHandler.h pour la spec et docs/slash_commands_rbac.md
// pour la convention.

#include "src/masterd/handlers/admin/AdminCommandHandler.h"

#include "src/masterd/account/AccountRecord.h"
#include "src/masterd/account/AccountRole.h"
#include "src/masterd/account/AccountRoleService.h"
#include "src/masterd/account/AccountStore.h"
#include "src/masterd/account/AccountValidation.h"
#include "src/masterd/admin/SlashCommandRegistry.h"
#include "src/masterd/gmtickets/GmTicketSystem.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shardd/world/LunarCalendar.h"
#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"
#include "src/shared/net/ChatSystem.h"
#include "src/shared/network/AdminCommandPayloads.h"
#include "src/shared/network/ChatPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <mysql.h>

#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace engine::server
{
	namespace
	{
		/// Convertit AdminCommandStatus -> mot-cle court pour le format de log.
		const char* StatusToLogTag(engine::network::admin::AdminCommandStatus s)
		{
			using namespace engine::network::admin;
			switch (s)
			{
			case AdminCommandStatus::Ok:             return "OK";
			case AdminCommandStatus::Unauthorized:   return "UNAUTHORIZED";
			case AdminCommandStatus::Denied:         return "DENIED";
			case AdminCommandStatus::UnknownCommand: return "UNKNOWN_COMMAND";
			case AdminCommandStatus::InvalidArgs:    return "INVALID_ARGS";
			case AdminCommandStatus::ServerError:    return "ERROR";
			}
			return "UNKNOWN";
		}

		/// Concatene les arguments en "[a1,a2,...]" pour le log audit.
		std::string FormatArgs(const std::vector<std::string>& args)
		{
			std::string out;
			out.push_back('[');
			for (size_t i = 0; i < args.size(); ++i)
			{
				if (i > 0) out.push_back(',');
				out += args[i];
			}
			out.push_back(']');
			return out;
		}

		/// Emet le log audit obligatoire selon le format
		/// docs/slash_commands_rbac.md. Subsystem "Audit" (les macros
		/// LOG_* utilisent une string runtime, donc n'importe quel nom
		/// fonctionne ; les filtres log.subsystem_files peuvent router).
		void LogAudit(uint64_t accountId,
		              engine::server::AccountRole role,
		              const std::string& command,
		              const std::vector<std::string>& args,
		              engine::network::admin::AdminCommandStatus status)
		{
			LOG_INFO(Audit,
				"[AdminCommand] account_id={} role={} command=\"{}\" args={} result={}",
				static_cast<unsigned long long>(accountId),
				engine::server::RoleToString(role),
				command,
				FormatArgs(args),
				StatusToLogTag(status));
		}

		/// Envoie la reponse 196 sur la connexion donnee. Si \p server est nullptr,
		/// no-op silencieux (le log audit a deja ete emis par le caller).
		void SendResponse(NetServer* server, uint32_t connId, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const engine::network::admin::AdminCommandResponse& resp)
		{
			using namespace engine::network::admin;
			using namespace engine::network;
			std::vector<uint8_t> payload;
			BuildAdminCommandResponsePayload(resp, payload);

			PacketBuilder builder;
			ByteWriter w = builder.PayloadWriter();
			if (w.Remaining() < payload.size()) return;
			if (!w.WriteBytes(payload.data(), payload.size())) return;
			if (!builder.Finalize(kOpcodeAdminCommandResponse, 0u, requestId,
			                      sessionIdHeader, payload.size())) return;
			if (server) server->Send(connId, builder.Data());
		}

		/// Resout (sessionId, accountId) a partir du connId + sessionIdHeader.
		/// Retourne true si l'auth est valide. Sortie : \p outAccountId.
		bool ResolveAuth(ConnectionSessionMap* connMap, SessionManager* sessionMgr,
		                 uint32_t connId, uint64_t sessionIdHeader,
		                 uint64_t& outAccountId)
		{
			outAccountId = 0;
			if (!connMap || !sessionMgr) return false;
			auto sessIdOpt = connMap->GetSessionId(connId);
			if (!sessIdOpt || *sessIdOpt == 0u || sessionIdHeader == 0u
			    || *sessIdOpt != sessionIdHeader)
				return false;
			auto accIdOpt = sessionMgr->GetAccountId(*sessIdOpt);
			if (!accIdOpt || *accIdOpt == 0u) return false;
			outAccountId = *accIdOpt;
			return true;
		}

		/// Liste des UI panel commands (Wave 3 RBAC migration). Pour ces
		/// commandes, le master ne fait rien d'autre que valider l'auth +
		/// emettre le log audit ; le client applique le toggle visuel
		/// localement, l'effet metier est nul cote serveur. Toutes ces
		/// commandes ont minRole=player et sont systematiquement Ok.
		///
		/// Le set inclut les formes canoniques ET les alias (ex: "/quest"
		/// et "/quests") pour que le HandleRequest puisse trancher sans
		/// re-resoudre via le registre.
		const std::unordered_set<std::string>& UiPanelCommandSet()
		{
			static const std::unordered_set<std::string> kSet = {
				"/mail",
				"/quest", "/quests",
				"/guild", "/guilds",
				"/ah", "/auction",
				"/skills", "/skill",
				"/lfg",
				"/arena",
				"/bg",
				"/pvp",
				"/weather",
				"/events",
				"/rep", "/reputation",
				"/ticket", "/gmticket",
			};
			return kSet;
		}

		/// Timestamp Unix en millisecondes (UTC), aligne sur ChatRelayHandler.
		uint64_t NowUnixMsUtc()
		{
			using namespace std::chrono;
			return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
		}

		/// Echappe une chaine pour SQL (utilise par /mute : INSERT). Aligne
		/// sur le helper DbHelpers (mais defini ici car on n'a pas
		/// EscapeMysql exporte publiquement).
		std::string EscapeForSql(MYSQL* mysql, std::string_view input)
		{
			if (!mysql)
				return std::string(input);
			std::string out(input.size() * 2u + 1u, '\0');
			const unsigned long len = mysql_real_escape_string(mysql,
				out.data(), input.data(), static_cast<unsigned long>(input.size()));
			out.resize(len);
			return out;
		}

		/// Dispatch metier pour "/sky moon <phase>". Valide l'argument et
		/// remplit \p resp avec status + result echo.
		///
		/// \return true si l'execution a reussi (status Ok rempli), false
		///         si arguments invalides (status InvalidArgs rempli).
		bool DispatchSkyMoon(const std::vector<std::string>& args,
		                     engine::network::admin::AdminCommandResponse& resp)
		{
			using namespace engine::network::admin;
			if (args.size() != 1)
			{
				resp.status = AdminCommandStatus::InvalidArgs;
				resp.message = "Usage : /sky moon <phase 0..15>";
				return false;
			}
			int phase = -1;
			try { phase = std::stoi(args[0]); }
			catch (...) { phase = -1; }
			if (phase < 0 || phase > 15)
			{
				resp.status = AdminCommandStatus::InvalidArgs;
				resp.message = "phase doit etre dans 0..15";
				return false;
			}
			const float illumination =
				engine::server::world::LunarCalendar::ComputeIllumination(static_cast<uint8_t>(phase));

			char phaseBuf[32];
			char illumBuf[32];
			std::snprintf(phaseBuf, sizeof(phaseBuf), "phase=%d", phase);
			std::snprintf(illumBuf, sizeof(illumBuf), "illumination=%.3f", illumination);

			resp.status = AdminCommandStatus::Ok;
			resp.result.push_back(phaseBuf);
			resp.result.push_back(illumBuf);
			resp.message = "OK";
			return true;
		}

		/// Dispatch metier pour "/sky time <hours>". Override visuel local cote
		/// client ; le master valide juste la plage [0..24) et echo la valeur.
		/// Le state serveur du cycle jour/nuit reste inchange (preview client).
		///
		/// \return true si execution Ok, false si arguments invalides.
		bool DispatchSkyTime(const std::vector<std::string>& args,
		                     engine::network::admin::AdminCommandResponse& resp)
		{
			using namespace engine::network::admin;
			if (args.size() != 1)
			{
				resp.status = AdminCommandStatus::InvalidArgs;
				resp.message = "Usage : /sky time <hours 0..24>";
				return false;
			}
			float hours = -1.0f;
			try { hours = std::stof(args[0]); }
			catch (...) { hours = -1.0f; }
			if (!(hours >= 0.0f) || hours >= 24.0f)
			{
				resp.status = AdminCommandStatus::InvalidArgs;
				resp.message = "Hours hors plage [0..24) : " + args[0];
				return false;
			}

			char hoursBuf[32];
			std::snprintf(hoursBuf, sizeof(hoursBuf), "hours=%.3f", static_cast<double>(hours));

			resp.status = AdminCommandStatus::Ok;
			resp.result.push_back(hoursBuf);
			resp.message = "Time set to " + args[0] + "h";
			return true;
		}

		/// Dispatch metier pour "/sky info". Commande lecture seule cote
		/// gameplay : le master ne fait rien d'autre qu'acquitter (le client
		/// dispose deja du DayNight state local pour afficher). L'audit log
		/// capture qui a inspecte et quand.
		bool DispatchSkyInfo(const std::vector<std::string>& args,
		                     engine::network::admin::AdminCommandResponse& resp)
		{
			using namespace engine::network::admin;
			if (!args.empty())
			{
				resp.status = AdminCommandStatus::InvalidArgs;
				resp.message = "Usage : /sky info (pas d'argument)";
				return false;
			}
			resp.status = AdminCommandStatus::Ok;
			resp.message = "Sky info inspection authorized";
			return true;
		}

		/// Dispatch metier pour "/loot". V1 : la commande toggle simplement
		/// le panneau Loot Roll cote client (UI debug, bouton Simulate gate
		/// admin-only). Le master valide juste le role + log audit. Le client
		/// applique le toggle local independamment de la reponse master.
		bool DispatchLoot(const std::vector<std::string>& args,
		                  engine::network::admin::AdminCommandResponse& resp)
		{
			using namespace engine::network::admin;
			(void)args;
			resp.status = AdminCommandStatus::Ok;
			resp.message = "Loot Roll panel toggled (audit logged)";
			return true;
		}

		/// Dispatch metier pour "/promote <account_id> <role>". Outil admin
		/// permettant de promouvoir/retrograder un compte sans acces direct
		/// DB. Valide les 2 arguments, applique via AccountRoleService::SetRole
		/// qui ecrit en DB + emet l'audit role_change.
		///
		/// \param actorAccountId  identifiant du compte qui execute la commande
		///                        (admin). Persiste dans l'audit role_change.
		/// \param roleService     service AccountRole (non-owning) ; si nullptr
		///                        la commande retourne ServerError.
		bool DispatchPromote(const std::vector<std::string>& args,
		                     uint64_t actorAccountId,
		                     engine::server::AccountRoleService* roleService,
		                     engine::network::admin::AdminCommandResponse& resp)
		{
			using namespace engine::network::admin;
			if (args.size() != 2)
			{
				resp.status = AdminCommandStatus::InvalidArgs;
				resp.message = "Usage : /promote <account_id> <role> "
				               "(role : player/moderator/game_master/administrator)";
				return false;
			}
			uint64_t targetId = 0;
			try { targetId = std::stoull(args[0]); }
			catch (...) { targetId = 0; }
			if (targetId == 0u)
			{
				resp.status = AdminCommandStatus::InvalidArgs;
				resp.message = "account_id invalide : " + args[0];
				return false;
			}
			// ParseRole retombe sur Player pour toute valeur inconnue : il faut
			// donc valider explicitement avant l'appel pour distinguer un vrai
			// "player" d'une typo. On accepte la forme snake_case stricte.
			const std::string_view roleArg{args[1]};
			const bool roleKnown =
				roleArg == "player" || roleArg == "moderator"
				|| roleArg == "game_master" || roleArg == "administrator";
			if (!roleKnown)
			{
				resp.status = AdminCommandStatus::InvalidArgs;
				resp.message = "role invalide : " + args[1]
				               + " (attendu : player/moderator/game_master/administrator)";
				return false;
			}
			const engine::server::AccountRole newRole = engine::server::ParseRole(roleArg);
			if (!roleService)
			{
				resp.status = AdminCommandStatus::ServerError;
				resp.message = "AccountRoleService indisponible";
				return false;
			}
			if (!roleService->SetRole(targetId, newRole, actorAccountId))
			{
				resp.status = AdminCommandStatus::ServerError;
				resp.message = "Echec mise a jour role (account_id introuvable ou DB en erreur)";
				return false;
			}
			resp.status = AdminCommandStatus::Ok;
			resp.message = "Role mis a jour : account_id="
			               + std::to_string(targetId) + " -> "
			               + std::string(engine::server::RoleToString(newRole));
			resp.result.push_back("account_id=" + std::to_string(targetId));
			resp.result.push_back(std::string("new_role=")
			                     + std::string(engine::server::RoleToString(newRole)));
			return true;
		}
	}

	void AdminCommandHandler::HandlePacket(uint32_t connId, uint16_t opcode,
	                                       uint32_t requestId, uint64_t sessionIdHeader,
	                                       const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (opcode != kOpcodeAdminCommandRequest) return;
		HandleRequest(connId, requestId, sessionIdHeader, payload, payloadSize);
	}

	void AdminCommandHandler::HandleRequest(uint32_t connId, uint32_t requestId,
	                                        uint64_t sessionIdHeader,
	                                        const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network::admin;

		AdminCommandRequest req;
		AdminCommandResponse resp;

		// Parse le payload : si invalide, on log avec command="" et InvalidArgs.
		if (!ParseAdminCommandRequestPayload(payload, payloadSize, req))
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "payload mal forme";
			LogAudit(0, AccountRole::Player, "<malformed>", {}, resp.status);
			SendResponse(m_server, connId, requestId, sessionIdHeader, resp);
			return;
		}
		resp.command = req.command;

		// Auth : resolve session + accountId.
		uint64_t accountId = 0;
		if (!ResolveAuth(m_connMap, m_sessionMgr, connId, sessionIdHeader, accountId))
		{
			resp.status = AdminCommandStatus::Unauthorized;
			resp.message = "Session invalide";
			LogAudit(0, AccountRole::Player, req.command, req.args, resp.status);
			SendResponse(m_server, connId, requestId, sessionIdHeader, resp);
			return;
		}

		// Role lookup. Sans roleService, on considere Player par defaut.
		AccountRole userRole = AccountRole::Player;
		if (m_roleService)
			userRole = m_roleService->GetRole(accountId);

		// Lookup commande dans le registre.
		if (!m_registry)
		{
			resp.status = AdminCommandStatus::ServerError;
			resp.message = "Registre slash_commands non charge";
			LogAudit(accountId, userRole, req.command, req.args, resp.status);
			SendResponse(m_server, connId, requestId, sessionIdHeader, resp);
			return;
		}
		const auto check = m_registry->Check(req.command, userRole);
		if (!check.found)
		{
			resp.status = AdminCommandStatus::UnknownCommand;
			resp.message = "Commande inconnue : " + req.command;
			LogAudit(accountId, userRole, req.command, req.args, resp.status);
			SendResponse(m_server, connId, requestId, sessionIdHeader, resp);
			return;
		}
		if (!check.allowed)
		{
			resp.status = AdminCommandStatus::Denied;
			resp.message = "Permission refusee : role ";
			resp.message += engine::server::RoleToString(check.minRequired);
			resp.message += " requis";
			LogAudit(accountId, userRole, req.command, req.args, resp.status);
			SendResponse(m_server, connId, requestId, sessionIdHeader, resp);
			return;
		}

		// Dispatch metier par nom de commande. L'accountId est l'identifiant
		// de l'utilisateur qui a emis la commande (audit + acteur de /promote).
		bool handled = false;
		if (req.command == "/sky moon")
		{
			DispatchSkyMoon(req.args, resp);
			handled = true;
		}
		else if (req.command == "/sky time")
		{
			DispatchSkyTime(req.args, resp);
			handled = true;
		}
		else if (req.command == "/sky info")
		{
			DispatchSkyInfo(req.args, resp);
			handled = true;
		}
		else if (req.command == "/loot")
		{
			DispatchLoot(req.args, resp);
			handled = true;
		}
		else if (req.command == "/promote")
		{
			DispatchPromote(req.args, accountId, m_roleService, resp);
			handled = true;
		}
		else if (req.command == "/who")
		{
			DispatchWho(resp);
			handled = true;
		}
		else if (req.command == "/report")
		{
			DispatchReport(accountId, req.args, resp);
			handled = true;
		}
		else if (req.command == "/kick")
		{
			DispatchKick(accountId, req.args, resp);
			handled = true;
		}
		else if (req.command == "/mute")
		{
			DispatchMute(accountId, req.args, resp);
			handled = true;
		}
		else if (req.command == "/ban")
		{
			DispatchBan(accountId, req.args, resp);
			handled = true;
		}
		else if (req.command == "/announce")
		{
			DispatchAnnounce(accountId, req.args, resp);
			handled = true;
		}
		else if (UiPanelCommandSet().count(req.command) > 0)
		{
			// Wave 3 RBAC migration : les UI panel commands sont log-only.
			// Le client applique le toggle visuel localement ; le master
			// ne fait que valider l'auth + role + emettre le log audit.
			// Pas de result echo (le client n'a pas besoin de payload).
			resp.status = AdminCommandStatus::Ok;
			resp.message = "UI panel toggle audit logged";
			handled = true;
		}

		// V1 stub : pour les autres commandes connues, status Ok mais result vide.
		// Les futures PR ajouteront les dispatchers specifiques (kick, ban, etc.).
		if (!handled)
		{
			resp.status = AdminCommandStatus::Ok;
			resp.message = "OK (V1 stub : commande connue mais effet metier pas encore implemente)";
		}

		LogAudit(accountId, userRole, req.command, req.args, resp.status);
		SendResponse(m_server, connId, requestId, sessionIdHeader, resp);
	}

	// ============================================================
	// Wave 2 dispatchers : moderation tools.
	// ============================================================

	/// Liste tous les joueurs connectes (etat Authenticated / Active).
	/// Renvoie result = ["count=N", "logins=alice,bob,charlie"]. Visible
	/// par minRole player (registry decide) ; resout login via store.
	void AdminCommandHandler::DispatchWho(engine::network::admin::AdminCommandResponse& resp)
	{
		using namespace engine::network::admin;
		if (!m_sessionMgr || !m_accountStore)
		{
			resp.status = AdminCommandStatus::ServerError;
			resp.message = "/who : sessionMgr ou accountStore non cable";
			return;
		}
		const auto accountIds = m_sessionMgr->ListActiveAccountIds();
		std::string logins;
		size_t resolved = 0u;
		for (size_t i = 0; i < accountIds.size(); ++i)
		{
			auto rec = m_accountStore->FindByAccountId(accountIds[i]);
			if (!rec) continue;
			if (resolved > 0u) logins.push_back(',');
			logins += rec->login;
			++resolved;
		}

		char countBuf[32];
		std::snprintf(countBuf, sizeof(countBuf), "count=%zu", resolved);
		resp.status = AdminCommandStatus::Ok;
		resp.result.push_back(countBuf);
		resp.result.push_back(std::string("logins=") + logins);
		resp.message = "OK";
	}

	/// Cree un ticket GM cote master. Le body du ticket est un message
	/// generique mentionnant le report ; le GM consultera la liste pour
	/// suivre. Pas d'effet client direct hormis l'ACK avec ticket_id.
	void AdminCommandHandler::DispatchReport(uint64_t actorAccountId,
		const std::vector<std::string>& args,
		engine::network::admin::AdminCommandResponse& resp)
	{
		using namespace engine::network::admin;
		if (args.size() < 1u)
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "Usage : /report <player>";
			return;
		}
		if (!m_accountStore || !m_gmTicketSys)
		{
			resp.status = AdminCommandStatus::ServerError;
			resp.message = "/report : accountStore ou gmTicketSystem non cable";
			return;
		}
		const std::string& targetLogin = args[0];
		const auto normLogin = engine::server::NormaliseLoginView(targetLogin);
		auto target = m_accountStore->FindByLogin(normLogin);
		if (!target)
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "Joueur introuvable : " + targetLogin;
			return;
		}

		// Body du ticket : reference le compte cible + le timestamp.
		const uint64_t nowMs = NowUnixMsUtc();
		std::string body = "Report contre '";
		body += target->login;
		body += "' (account_id=";
		body += std::to_string(target->account_id);
		body += ").";

		const auto ticketId = m_gmTicketSys->Open(actorAccountId, body, nowMs);

		char idBuf[48];
		std::snprintf(idBuf, sizeof(idBuf), "ticket_id=%llu",
			static_cast<unsigned long long>(ticketId));
		resp.status = AdminCommandStatus::Ok;
		resp.result.push_back(idBuf);
		resp.message = "Ticket cree (id=" + std::to_string(ticketId) + ")";
	}

	/// Kick : ferme la connexion TCP du joueur cible. Verifie role
	/// inferieur strict (un mod ne peut pas kick un autre mod). Trouve
	/// la connection via Snapshot + GetAccountId.
	void AdminCommandHandler::DispatchKick(uint64_t actorAccountId,
		const std::vector<std::string>& args,
		engine::network::admin::AdminCommandResponse& resp)
	{
		using namespace engine::network::admin;
		if (args.size() < 1u)
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "Usage : /kick <player>";
			return;
		}
		if (!m_accountStore || !m_sessionMgr || !m_connMap || !m_server)
		{
			resp.status = AdminCommandStatus::ServerError;
			resp.message = "/kick : dependences non cablees";
			return;
		}
		const auto normLogin = engine::server::NormaliseLoginView(args[0]);
		auto target = m_accountStore->FindByLogin(normLogin);
		if (!target)
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "Joueur introuvable : " + args[0];
			return;
		}

		// Check : role cible strictement inferieur a celui de l'acteur.
		// Un moderator ne peut pas kick un autre moderator (regle ticket).
		if (m_roleService && !m_roleService->HasLowerSecurity(target->account_id, actorAccountId))
		{
			resp.status = AdminCommandStatus::Denied;
			resp.message = "Impossible de kicker un compte de rang egal ou superieur";
			return;
		}

		// Trouve la connId du target via la snapshot connMap.
		const auto snapshot = m_connMap->Snapshot();
		uint32_t targetConnId = 0u;
		bool found = false;
		for (const auto& [connId, sessId] : snapshot)
		{
			auto accIdOpt = m_sessionMgr->GetAccountId(sessId);
			if (accIdOpt && *accIdOpt == target->account_id)
			{
				targetConnId = connId;
				found = true;
				break;
			}
		}

		if (!found)
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "Joueur '" + args[0] + "' n'est pas connecte";
			return;
		}

		m_server->CloseConnection(targetConnId, DisconnectReason::PeerClosed);
		LOG_INFO(Audit, "[AdminCommand] Kick actor={} target_account={} target_login={} conn={}",
			static_cast<unsigned long long>(actorAccountId),
			static_cast<unsigned long long>(target->account_id),
			target->login,
			static_cast<unsigned>(targetConnId));

		resp.status = AdminCommandStatus::Ok;
		resp.result.push_back(std::string("kicked=") + target->login);
		resp.message = "Joueur kicke : " + target->login;
	}

	/// Mute : INSERT/REPLACE dans chat_mutes avec until_ts dans le futur.
	/// Necessite un pool MySQL cable. Verifie role inferieur strict.
	void AdminCommandHandler::DispatchMute(uint64_t actorAccountId,
		const std::vector<std::string>& args,
		engine::network::admin::AdminCommandResponse& resp)
	{
		using namespace engine::network::admin;
		if (args.size() < 2u)
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "Usage : /mute <player> <duration_minutes>";
			return;
		}
		if (!m_accountStore || !m_dbPool)
		{
			resp.status = AdminCommandStatus::ServerError;
			resp.message = "/mute : accountStore ou dbPool non cable";
			return;
		}

		int durationMin = -1;
		try { durationMin = std::stoi(args[1]); }
		catch (...) { durationMin = -1; }
		if (durationMin <= 0 || durationMin > 60 * 24 * 365)
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "duree invalide (minutes, 1..525600)";
			return;
		}

		const auto normLogin = engine::server::NormaliseLoginView(args[0]);
		auto target = m_accountStore->FindByLogin(normLogin);
		if (!target)
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "Joueur introuvable : " + args[0];
			return;
		}
		if (m_roleService && !m_roleService->HasLowerSecurity(target->account_id, actorAccountId))
		{
			resp.status = AdminCommandStatus::Denied;
			resp.message = "Impossible de mute un compte de rang egal ou superieur";
			return;
		}

		const uint64_t nowMs = NowUnixMsUtc();
		const uint64_t untilTsMs = nowMs + static_cast<uint64_t>(durationMin) * 60ull * 1000ull;
		const std::string reason = std::string("muted by account_id=")
			+ std::to_string(actorAccountId);

		auto guard = m_dbPool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
		{
			resp.status = AdminCommandStatus::ServerError;
			resp.message = "DB indisponible";
			return;
		}
		const std::string escReason = EscapeForSql(mysql, reason);
		char sqlBuf[512];
		std::snprintf(sqlBuf, sizeof(sqlBuf),
			"REPLACE INTO chat_mutes (account_id, until_ts, reason) "
			"VALUES (%llu, %llu, '%s')",
			static_cast<unsigned long long>(target->account_id),
			static_cast<unsigned long long>(untilTsMs),
			escReason.c_str());
		if (!engine::server::db::DbExecute(mysql, sqlBuf))
		{
			resp.status = AdminCommandStatus::ServerError;
			resp.message = "DB write failed";
			return;
		}

		char untilBuf[64];
		std::snprintf(untilBuf, sizeof(untilBuf), "until_ts_ms=%llu",
			static_cast<unsigned long long>(untilTsMs));
		resp.status = AdminCommandStatus::Ok;
		resp.result.push_back(std::string("muted=") + target->login);
		resp.result.push_back(untilBuf);
		resp.message = "Joueur mute : " + target->login;
	}

	/// Ban : UPDATE accounts.status=Locked. Si online, kick aussi pour
	/// effet immediat. Verifie role inferieur strict.
	void AdminCommandHandler::DispatchBan(uint64_t actorAccountId,
		const std::vector<std::string>& args,
		engine::network::admin::AdminCommandResponse& resp)
	{
		using namespace engine::network::admin;
		if (args.size() < 1u)
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "Usage : /ban <player> [reason]";
			return;
		}
		if (!m_accountStore)
		{
			resp.status = AdminCommandStatus::ServerError;
			resp.message = "/ban : accountStore non cable";
			return;
		}

		const auto normLogin = engine::server::NormaliseLoginView(args[0]);
		auto target = m_accountStore->FindByLogin(normLogin);
		if (!target)
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "Joueur introuvable : " + args[0];
			return;
		}
		if (m_roleService && !m_roleService->HasLowerSecurity(target->account_id, actorAccountId))
		{
			resp.status = AdminCommandStatus::Denied;
			resp.message = "Impossible de ban un compte de rang egal ou superieur";
			return;
		}

		std::string reason;
		for (size_t i = 1; i < args.size(); ++i)
		{
			if (i > 1u) reason.push_back(' ');
			reason += args[i];
		}

		if (!m_accountStore->SetAccountStatus(target->account_id, AccountStatus::Locked))
		{
			resp.status = AdminCommandStatus::ServerError;
			resp.message = "Echec mise a jour status compte";
			return;
		}

		// Si online, deconnecte aussi (ban prend effet immediat).
		if (m_sessionMgr && m_connMap && m_server)
		{
			const auto snapshot = m_connMap->Snapshot();
			for (const auto& [connId, sessId] : snapshot)
			{
				auto accIdOpt = m_sessionMgr->GetAccountId(sessId);
				if (accIdOpt && *accIdOpt == target->account_id)
				{
					m_server->CloseConnection(connId, DisconnectReason::PeerClosed);
					break;
				}
			}
		}

		LOG_INFO(Audit, "[AdminCommand] Ban actor={} target_account={} target_login={} reason='{}'",
			static_cast<unsigned long long>(actorAccountId),
			static_cast<unsigned long long>(target->account_id),
			target->login,
			reason);

		resp.status = AdminCommandStatus::Ok;
		resp.result.push_back(std::string("banned=") + target->login);
		if (!reason.empty())
			resp.result.push_back(std::string("reason=") + reason);
		resp.message = "Joueur banni : " + target->login;
	}

	/// Announce : broadcast un ChatRelay channel=Server a toutes les
	/// sessions actives. Pas de filtre IgnoreList (un announce admin
	/// doit toucher tout le monde).
	void AdminCommandHandler::DispatchAnnounce(uint64_t actorAccountId,
		const std::vector<std::string>& args,
		engine::network::admin::AdminCommandResponse& resp)
	{
		using namespace engine::network::admin;
		if (args.empty())
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "Usage : /announce <message>";
			return;
		}
		if (!m_server || !m_connMap || !m_sessionMgr)
		{
			resp.status = AdminCommandStatus::ServerError;
			resp.message = "/announce : dependences non cablees";
			return;
		}

		// Reconstruit le message a partir des args.
		std::string message;
		for (size_t i = 0; i < args.size(); ++i)
		{
			if (i > 0u) message.push_back(' ');
			message += args[i];
		}
		if (message.empty())
		{
			resp.status = AdminCommandStatus::InvalidArgs;
			resp.message = "Message vide";
			return;
		}

		// Sender : tag generique + login si dispo.
		std::string sender = "[Announce]";
		if (m_accountStore)
		{
			auto rec = m_accountStore->FindByAccountId(actorAccountId);
			if (rec)
			{
				sender = "[Announce] ";
				sender += rec->login;
			}
		}

		const uint64_t ts = NowUnixMsUtc();
		const uint8_t channel = static_cast<uint8_t>(engine::net::ChatChannel::Server);
		const auto snapshot = m_connMap->Snapshot();
		size_t delivered = 0u;
		for (const auto& [connId, sessId] : snapshot)
		{
			auto pkt = engine::network::BuildChatRelayPacket(ts, channel, sender, message, sessId);
			if (pkt.empty()) continue;
			if (m_server->Send(connId, pkt))
				++delivered;
		}

		LOG_INFO(Audit, "[AdminCommand] Announce actor={} delivered={}/{} text='{}'",
			static_cast<unsigned long long>(actorAccountId),
			delivered, snapshot.size(),
			message.size() > 200u ? message.substr(0, 200u) + "...[truncated]" : message);

		char deliveredBuf[48];
		std::snprintf(deliveredBuf, sizeof(deliveredBuf), "delivered=%zu", delivered);
		resp.status = AdminCommandStatus::Ok;
		resp.result.push_back(deliveredBuf);
		resp.message = "Announce diffuse";
	}
}
