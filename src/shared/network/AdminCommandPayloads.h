#pragma once
// Wire payloads pour le systeme AdminCommand (opcodes 195/196). Pattern
// centralise pour TOUTES les slash commands avec RBAC + audit log
// (cf. game/data/config/slash_commands.json + docs/slash_commands_rbac.md).
//
// Format wire little-endian. Les champs string sont prefixes par leur
// longueur en uint16_t LE (max 65535 octets), les listes (vector<string>)
// sont prefixees par leur cardinalite en uint16_t LE puis chaque element
// est encode comme un string. La taille totale d'un payload est bornee
// par la limite de 16 KB des paquets v1 (PacketBuilder::Finalize).
//
// Format binaire :
//   AdminCommandRequest :
//     u16 command_len + bytes(command_len)
//     u16 args_count + repeat[ u16 arg_len + bytes(arg_len) ]
//
//   AdminCommandResponse :
//     u8 status (0..5, cf. AdminCommandStatus)
//     u16 command_len + bytes(command_len)
//     u16 result_count + repeat[ u16 v_len + bytes(v_len) ]
//     u16 message_len + bytes(message_len)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::network::admin
{
	/// Resultat d'execution d'une slash command. La valeur 0 indique succes ;
	/// toute autre valeur identifie la categorie de refus (logge serveur).
	enum class AdminCommandStatus : uint8_t
	{
		Ok             = 0,  ///< Execution reussie.
		Unauthorized   = 1,  ///< Pas de session valide (avant lookup role).
		Denied         = 2,  ///< Role insuffisant pour la commande demandee.
		UnknownCommand = 3,  ///< Commande absente du registre slash_commands.json.
		InvalidArgs    = 4,  ///< Arguments mal formes (validation specifique).
		ServerError    = 5,  ///< Erreur lors de l'execution (logique metier).
	};

	/// Requete client -> master : "/sky moon 7" -> command="/sky moon", args=["7"].
	struct AdminCommandRequest
	{
		std::string              command;  ///< Nom canonique avec slash (ex: "/sky moon").
		std::vector<std::string> args;     ///< Arguments positionnels (peut etre vide).
	};

	/// Reponse master -> client : status + echo de la commande + result key=value
	/// + message human-readable. Le client dispatche sur \c command pour appliquer
	/// l'effet local quand status == Ok.
	struct AdminCommandResponse
	{
		AdminCommandStatus       status  = AdminCommandStatus::Ok;
		std::string              command;  ///< Echo de la commande pour dispatch client.
		std::vector<std::string> result;   ///< Generic key=value strings, libre par command.
		std::string              message;  ///< Texte UI (ex: "Permission refusee : role administrator requis").
	};

	/// Build* : ecrit le payload binaire dans \p out (clear + push_back).
	/// Le caller integrera ensuite le buffer dans un PacketBuilder
	/// (Finalize ajoute le header protocol_v1).
	void BuildAdminCommandRequestPayload(const AdminCommandRequest& msg, std::vector<uint8_t>& out);
	void BuildAdminCommandResponsePayload(const AdminCommandResponse& msg, std::vector<uint8_t>& out);

	/// Parse* : retourne true si le buffer est valide (taille exacte attendue,
	/// pas d'octet residuel). Reject-short et reject-extra strict.
	bool ParseAdminCommandRequestPayload(const uint8_t* data, size_t size, AdminCommandRequest& out);
	bool ParseAdminCommandResponsePayload(const uint8_t* data, size_t size, AdminCommandResponse& out);
}
