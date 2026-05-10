#pragma once
// Wave 5 Persistence (Phase 5.21b) - MysqlGuildStore : wrapper MySQL
// pour persister les guildes (definition + membres + bank tab 0).
// Migration 0055_guilds_master.sql. Cible UNIX (master).
//
// Le runtime continue de manipuler des InMemoryGuild (defini dans
// GuildHandler.h). Le store traduit ces structs vers les 3 tables
// guilds_master / guild_members / guild_bank (cf. migration 0055).
//
// Lifecycle :
//   - main_linux : instancie le store -> guildHandler.SetGuildStore.
//   - SeedV1Guilds : si store branche, charge LoadAll ; sinon fallback
//     sur le seed hardcode (2 guildes).
//   - V1 read-only cote client : InsertGuild / InsertMember / UpdateMotd
//     sont exposes pour de futurs handlers (Create / Promote / SetMotd)
//     mais ne sont pas appeles par les opcodes V1 actuels.
//
// Tous les appels au store sont best-effort : un retour false logge
// un warning mais n'interrompt pas la requete client (coherence eventuelle).
// L'AccountId pour les membres est utilise tel quel ; la resolution
// accountId -> accountName reste a la charge du caller (V1 : table
// hardcoded dans GuildHandler).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server { struct InMemoryGuild; }

namespace engine::server::guilds_db
{
	/// Ligne membre brute telle que persistee. Le rankId 0..9 correspond
	/// aux noms de rangs WoW (cf. GuildHandler::RankName).
	struct GuildMemberRow
	{
		uint32_t guildId          = 0;
		uint64_t accountId        = 0;
		uint8_t  rankId           = 5u;
		uint64_t joinedAtUnixMs   = 0u;
	};

	/// Ligne bank tab 0 brute telle que persistee.
	struct GuildBankItemRow
	{
		uint32_t    guildId         = 0;
		uint8_t     tabIndex        = 0u;
		uint32_t    slotIndex       = 0u;
		uint32_t    itemTemplateId  = 0u;
		std::string itemName;
		uint32_t    count           = 0u;
	};

	/// Ligne guilde brute telle que persistee.
	struct GuildMasterRow
	{
		uint32_t                       guildId           = 0;
		std::string                    name;
		std::string                    motd;
		uint64_t                       leaderAccountId   = 0;
		uint64_t                       createdAtUnixMs   = 0;
		std::vector<GuildMemberRow>    members;
		std::vector<GuildBankItemRow>  bank0;
	};

	/// MySQL backed store pour GuildHandler. Toutes les operations
	/// retournent false / vide si le pool n'est pas initialise (le
	/// caller en deduit "fallback in-memory").
	class MysqlGuildStore final
	{
	public:
		explicit MysqlGuildStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		/// Retourne true si le store est en mode DB (pool initialise).
		bool IsAvailable() const noexcept;

		/// Charge toutes les guildes + leurs membres + leur bank tab 0.
		/// Appele au boot par GuildHandler::SeedV1Guilds pour reconstruire
		/// m_guilds. Vide si DB indisponible.
		///
		/// Note : la resolution accountId -> accountName n'est PAS faite
		/// ici (le store ne joint pas la table accounts). Le caller
		/// (GuildHandler) doit completer accountName apres LoadAll en
		/// utilisant sa table de lookup interne ou un AccountStore.
		std::vector<GuildMasterRow> LoadAll() const;

		/// Insere une nouvelle guilde. La cle guildId peut etre 0 (la DB
		/// genere l'id via AUTO_INCREMENT) ou une valeur explicite. Le
		/// caller recupere l'id alloue via la valeur de retour.
		///
		/// \param name              nom unique de la guilde (UTF-8, <= 64).
		/// \param motd              MOTD initial (UTF-8, <= 256).
		/// \param leaderAccountId   account_id du leader.
		/// \param createdAtUnixMs   timestamp creation (system_clock ms).
		/// \return guildId nouvellement insere (>0) ou 0 en cas d'echec.
		uint32_t InsertGuild(std::string_view name, std::string_view motd,
			uint64_t leaderAccountId, uint64_t createdAtUnixMs);

		/// Insere un membre dans une guilde existante. Echoue si le tuple
		/// (guildId, accountId) existe deja (PRIMARY KEY).
		///
		/// \param guildId          id guilde cible.
		/// \param accountId        account a ajouter.
		/// \param rankId           rang 0..9 (defaut 5=Member).
		/// \param joinedAtUnixMs   timestamp d'adhesion.
		/// \return true en succes, false sinon.
		bool InsertMember(uint32_t guildId, uint64_t accountId,
			uint8_t rankId, uint64_t joinedAtUnixMs);

		/// Met a jour le MOTD d'une guilde. Retourne true si la ligne
		/// existe et a ete updatee.
		bool UpdateMotd(uint32_t guildId, std::string_view newMotd);

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
