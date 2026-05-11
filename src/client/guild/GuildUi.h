#pragma once
// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Presenter client de la fenetre
// Guildes. Maintient un cache local de la liste des guildes + selection
// active (membres / permissions / bank tab 0) + dernier MOTD update reçu
// (pour toast UI).
//
// Pas de rendu ImGui : le panneau est drawe par GuildImGuiRenderer qui lit
// l'etat via GetState() et propage les inputs UI (RequestList /
// SelectGuild / RequestMembers / RequestPermissions / RequestBank) via les
// methodes du presenter.
//
// Send : fire-and-forget via un callback (cf. m_send dans GameEventUi).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes
// 165/167/169/171/172 vers les OnXxx du presenter.

#include "src/shared/network/GuildPayloads.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::client
{
	/// Resume d'une guilde expose au layer UI. Mirror direct de
	/// engine::network::GuildSummary.
	struct GuildSummary
	{
		uint32_t    guildId     = 0;
		std::string name;
		std::string motd;
		uint32_t    memberCount = 0;
		std::string leaderName;
	};

	/// Membre d'une guilde expose au layer UI. Mirror direct de
	/// engine::network::GuildMember.
	struct GuildMember
	{
		std::string accountName;
		uint8_t     rankId   = 0;
		std::string rankName;
		bool        online   = false;
	};

	/// Permissions d'un rang expose au layer UI. Mirror direct de
	/// engine::network::GuildRankPerms.
	struct GuildRankPerms
	{
		uint8_t     rankId = 0;
		std::string rankName;
		uint32_t    mask   = 0;
	};

	/// Item de bank expose au layer UI. Mirror direct de
	/// engine::network::GuildBankItem.
	struct GuildBankItem
	{
		uint32_t    slotIndex = 0;
		std::string itemName;
		uint32_t    count     = 0;
	};

	/// Etat snapshot expose au renderer ImGui. Le panneau lit ces champs en
	/// lecture seule et appelle les methodes du presenter pour les muter.
	struct GuildState
	{
		std::vector<GuildSummary>         guilds;
		bool                              guildsLoaded = false;

		/// Guilde selectionnee : si set, on a demande (ou recu) members /
		/// permissions / bank pour cette guilde.
		std::optional<uint32_t>           selectedGuildId;
		std::vector<GuildMember>          selectedMembers;
		bool                              membersLoaded = false;
		std::vector<GuildRankPerms>       selectedRanks;
		bool                              ranksLoaded   = false;
		std::vector<GuildBankItem>        selectedBank;
		bool                              bankLoaded    = false;

		/// Dernier MotdUpdate reçu : guildId + nouvelle valeur + instant
		/// d'arrivee (ms steady_clock cote client). Utilise par le renderer
		/// pour afficher un toast 5s apres reception.
		std::optional<uint64_t>           lastMotdChangeTimeMs;
		uint32_t                          lastMotdGuildId = 0;
		std::string                       lastMotdNewValue;

		/// Vide si pas d'erreur transitoire. Sinon affiche en rouge.
		std::string lastErrorText;
		/// Texte d'info transitoire. Vide par defaut.
		std::string lastInfoText;
	};

	/// Static helper : explose un mask de permissions WoW en libelles ASCII
	/// (e.g. ["Invite", "Remove", ...]). Bits matching les constantes
	/// serveur :
	///   0x001 Invite, 0x002 Remove, 0x004 Promote, 0x008 Demote,
	///   0x010 EditMotd, 0x020 EditTabard, 0x040 WithdrawGold, 0x080 BankView,
	///   0x100 BankDeposit, 0x200 BankWithdraw, 0x400 Disband.
	std::vector<std::string> ExpandPermissionMask(uint32_t mask);

	/// Presenter pour la fenetre Guildes cote client. Doit etre Init() avant
	/// tout usage du callback. Thread : main (comme les autres presenters UI).
	class GuildUiPresenter final
	{
	public:
		GuildUiPresenter() = default;

		GuildUiPresenter(const GuildUiPresenter&)            = delete;
		GuildUiPresenter& operator=(const GuildUiPresenter&) = delete;

		~GuildUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.21 step 3+4 Guilds)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout RequestList / SelectGuild.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie GUILD_LIST_REQUEST. Reponse via OnListResponse.
		void RequestList();

		/// Selectionne une guilde et declenche RequestMembers + RequestPermissions
		/// + RequestBank tab 0 sur cette guilde. Le rendu ImGui basculera vers
		/// l'onglet detail apres reception.
		void SelectGuild(uint32_t guildId);

		/// Envoie GUILD_MEMBERS_REQUEST. Reponse via OnMembersResponse.
		void RequestMembers(uint32_t guildId);

		/// Envoie GUILD_PERMISSIONS_REQUEST. Reponse via OnPermissionsResponse.
		void RequestPermissions(uint32_t guildId);

		/// Envoie GUILD_BANK_REQUEST. Reponse via OnBankResponse.
		void RequestBank(uint32_t guildId, uint8_t tabIndex = 0u);

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit GUILD_LIST_RESPONSE. Remplace le cache local des guildes.
		void OnListResponse(const engine::network::GuildListResponsePayload& resp);

		/// Recoit GUILD_MEMBERS_RESPONSE. Met a jour selectedMembers si la
		/// guilde correspond a selectedGuildId, sinon cache l'erreur.
		void OnMembersResponse(const engine::network::GuildMembersResponsePayload& resp);

		/// Recoit GUILD_PERMISSIONS_RESPONSE. Met a jour selectedRanks.
		void OnPermissionsResponse(const engine::network::GuildPermissionsResponsePayload& resp);

		/// Recoit GUILD_BANK_RESPONSE. Met a jour selectedBank.
		void OnBankResponse(const engine::network::GuildBankResponsePayload& resp);

		/// Recoit un push GUILD_MOTD_UPDATE_NOTIFICATION : update motd dans
		/// le cache des guildes + met a jour lastMotd* pour le toast UI.
		void OnMotdUpdateNotification(const engine::network::GuildMotdUpdateNotificationPayload& note);

		// ---------------------------------------------------------------------
		// State access
		// ---------------------------------------------------------------------

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const GuildState& GetState() const { return m_state; }

	private:
		bool             m_initialized = false;
		GuildState       m_state{};
		SendCallback     m_send;
	};
}
