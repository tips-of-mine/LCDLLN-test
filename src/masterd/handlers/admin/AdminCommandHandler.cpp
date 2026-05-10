// Implementation AdminCommandHandler : dispatch + audit log obligatoire.
// Cf. AdminCommandHandler.h pour la spec et docs/slash_commands_rbac.md
// pour la convention.

#include "src/masterd/handlers/admin/AdminCommandHandler.h"

#include "src/masterd/account/AccountRole.h"
#include "src/masterd/account/AccountRoleService.h"
#include "src/masterd/admin/SlashCommandRegistry.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shardd/world/LunarCalendar.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/AdminCommandPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

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
}
