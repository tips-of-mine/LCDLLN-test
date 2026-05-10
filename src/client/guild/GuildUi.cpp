// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Implementation GuildUiPresenter.

#include "src/client/guild/GuildUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <chrono>

namespace engine::client
{
	// -------------------------------------------------------------------------
	// ExpandPermissionMask — static helper.
	// -------------------------------------------------------------------------

	namespace
	{
		// Bits matching server-side (cf. Permission enum dans GuildPermissionMatrix.h).
		// On les duplique ici en client pour eviter le coupling au header server.
		constexpr uint32_t kPermInvite       = 0x001u;
		constexpr uint32_t kPermRemove       = 0x002u;
		constexpr uint32_t kPermPromote      = 0x004u;
		constexpr uint32_t kPermDemote       = 0x008u;
		constexpr uint32_t kPermEditMotd     = 0x010u;
		constexpr uint32_t kPermEditTabard   = 0x020u;
		constexpr uint32_t kPermWithdrawGold = 0x040u;
		constexpr uint32_t kPermBankView     = 0x080u;
		constexpr uint32_t kPermBankDeposit  = 0x100u;
		constexpr uint32_t kPermBankWithdraw = 0x200u;
		constexpr uint32_t kPermDisband      = 0x400u;

		/// Retourne le steady_clock now en ms depuis le boot pour l'horodatage
		/// local des toasts (5s d'affichage).
		uint64_t SteadyMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}
	}

	std::vector<std::string> ExpandPermissionMask(uint32_t mask)
	{
		std::vector<std::string> out;
		if (mask & kPermInvite)       out.push_back("Invite");
		if (mask & kPermRemove)       out.push_back("Remove");
		if (mask & kPermPromote)      out.push_back("Promote");
		if (mask & kPermDemote)       out.push_back("Demote");
		if (mask & kPermEditMotd)     out.push_back("EditMotd");
		if (mask & kPermEditTabard)   out.push_back("EditTabard");
		if (mask & kPermWithdrawGold) out.push_back("WithdrawGold");
		if (mask & kPermBankView)     out.push_back("BankView");
		if (mask & kPermBankDeposit)  out.push_back("BankDeposit");
		if (mask & kPermBankWithdraw) out.push_back("BankWithdraw");
		if (mask & kPermDisband)      out.push_back("Disband");
		return out;
	}

	// -------------------------------------------------------------------------
	// Presenter lifecycle
	// -------------------------------------------------------------------------

	GuildUiPresenter::~GuildUiPresenter()
	{
		Shutdown();
	}

	bool GuildUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[GuildUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		LOG_INFO(Core, "[GuildUiPresenter] Init OK");
		return true;
	}

	void GuildUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		LOG_INFO(Core, "[GuildUiPresenter] Destroyed");
	}

	// -------------------------------------------------------------------------
	// Outgoing requests
	// -------------------------------------------------------------------------

	void GuildUiPresenter::RequestList()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[GuildUiPresenter] RequestList: no send callback");
			return;
		}
		const auto payload = engine::network::BuildGuildListRequestPayload();
		if (!m_send(engine::network::kOpcodeGuildListRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (liste guildes).";
			LOG_WARN(Net, "[GuildUiPresenter] RequestList: send failed");
			return;
		}
		LOG_DEBUG(Net, "[GuildUiPresenter] Guild ListRequest queued");
	}

	void GuildUiPresenter::SelectGuild(uint32_t guildId)
	{
		// Reset les sub-states avant de declencher 3 requetes paralleles.
		m_state.selectedGuildId      = guildId;
		m_state.selectedMembers.clear();
		m_state.membersLoaded        = false;
		m_state.selectedRanks.clear();
		m_state.ranksLoaded          = false;
		m_state.selectedBank.clear();
		m_state.bankLoaded           = false;
		m_state.lastErrorText.clear();

		RequestMembers(guildId);
		RequestPermissions(guildId);
		RequestBank(guildId, 0u);
		LOG_INFO(Core, "[GuildUiPresenter] SelectGuild guildId={} (members + perms + bank0 queued)",
			guildId);
	}

	void GuildUiPresenter::RequestMembers(uint32_t guildId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[GuildUiPresenter] RequestMembers: no send callback");
			return;
		}
		const auto payload = engine::network::BuildGuildMembersRequestPayload(guildId);
		if (!m_send(engine::network::kOpcodeGuildMembersRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (membres guilde).";
			LOG_WARN(Net, "[GuildUiPresenter] RequestMembers: send failed guildId={}", guildId);
			return;
		}
		LOG_DEBUG(Net, "[GuildUiPresenter] Guild MembersRequest queued guildId={}", guildId);
	}

	void GuildUiPresenter::RequestPermissions(uint32_t guildId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[GuildUiPresenter] RequestPermissions: no send callback");
			return;
		}
		const auto payload = engine::network::BuildGuildPermissionsRequestPayload(guildId);
		if (!m_send(engine::network::kOpcodeGuildPermissionsRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (permissions guilde).";
			LOG_WARN(Net, "[GuildUiPresenter] RequestPermissions: send failed guildId={}", guildId);
			return;
		}
		LOG_DEBUG(Net, "[GuildUiPresenter] Guild PermissionsRequest queued guildId={}", guildId);
	}

	void GuildUiPresenter::RequestBank(uint32_t guildId, uint8_t tabIndex)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[GuildUiPresenter] RequestBank: no send callback");
			return;
		}
		const auto payload = engine::network::BuildGuildBankRequestPayload(guildId, tabIndex);
		if (!m_send(engine::network::kOpcodeGuildBankRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (bank guilde).";
			LOG_WARN(Net, "[GuildUiPresenter] RequestBank: send failed guildId={} tab={}",
				guildId, static_cast<unsigned>(tabIndex));
			return;
		}
		LOG_DEBUG(Net, "[GuildUiPresenter] Guild BankRequest queued guildId={} tab={}",
			guildId, static_cast<unsigned>(tabIndex));
	}

	// -------------------------------------------------------------------------
	// Incoming responses / push
	// -------------------------------------------------------------------------

	void GuildUiPresenter::OnListResponse(const engine::network::GuildListResponsePayload& resp)
	{
		using engine::network::GuildErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<GuildErrorCode>(resp.error);
			if (err == GuildErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur Guild inconnue.";
			LOG_WARN(Net, "[GuildUiPresenter] OnListResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.guilds.clear();
		m_state.guilds.reserve(resp.guilds.size());
		for (const auto& g : resp.guilds)
		{
			GuildSummary local;
			local.guildId     = g.guildId;
			local.name        = g.name;
			local.motd        = g.motd;
			local.memberCount = g.memberCount;
			local.leaderName  = g.leaderName;
			m_state.guilds.push_back(std::move(local));
		}
		m_state.guildsLoaded = true;

		LOG_INFO(Net, "[GuildUiPresenter] OnListResponse OK count={}",
			m_state.guilds.size());
	}

	void GuildUiPresenter::OnMembersResponse(const engine::network::GuildMembersResponsePayload& resp)
	{
		using engine::network::GuildErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<GuildErrorCode>(resp.error);
			switch (err)
			{
			case GuildErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			case GuildErrorCode::UnknownGuild:
				m_state.lastErrorText = "Guilde introuvable.";
				break;
			default:
				m_state.lastErrorText = "Erreur Guild inconnue.";
				break;
			}
			LOG_WARN(Net, "[GuildUiPresenter] OnMembersResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.selectedMembers.clear();
		m_state.selectedMembers.reserve(resp.members.size());
		for (const auto& m : resp.members)
		{
			GuildMember local;
			local.accountName = m.accountName;
			local.rankId      = m.rankId;
			local.rankName    = m.rankName;
			local.online      = m.online;
			m_state.selectedMembers.push_back(std::move(local));
		}
		m_state.membersLoaded = true;

		LOG_INFO(Net, "[GuildUiPresenter] OnMembersResponse OK count={}",
			m_state.selectedMembers.size());
	}

	void GuildUiPresenter::OnPermissionsResponse(const engine::network::GuildPermissionsResponsePayload& resp)
	{
		using engine::network::GuildErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<GuildErrorCode>(resp.error);
			switch (err)
			{
			case GuildErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			case GuildErrorCode::UnknownGuild:
				m_state.lastErrorText = "Guilde introuvable.";
				break;
			default:
				m_state.lastErrorText = "Erreur Guild inconnue.";
				break;
			}
			LOG_WARN(Net, "[GuildUiPresenter] OnPermissionsResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.selectedRanks.clear();
		m_state.selectedRanks.reserve(resp.ranks.size());
		for (const auto& p : resp.ranks)
		{
			GuildRankPerms local;
			local.rankId   = p.rankId;
			local.rankName = p.rankName;
			local.mask     = p.mask;
			m_state.selectedRanks.push_back(std::move(local));
		}
		m_state.ranksLoaded = true;

		LOG_INFO(Net, "[GuildUiPresenter] OnPermissionsResponse OK ranks={}",
			m_state.selectedRanks.size());
	}

	void GuildUiPresenter::OnBankResponse(const engine::network::GuildBankResponsePayload& resp)
	{
		using engine::network::GuildErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<GuildErrorCode>(resp.error);
			switch (err)
			{
			case GuildErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			case GuildErrorCode::UnknownGuild:
				m_state.lastErrorText = "Guilde introuvable.";
				break;
			case GuildErrorCode::NoPermission:
				m_state.lastErrorText = "Acces refuse a la banque.";
				break;
			default:
				m_state.lastErrorText = "Erreur Guild inconnue.";
				break;
			}
			LOG_WARN(Net, "[GuildUiPresenter] OnBankResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.selectedBank.clear();
		m_state.selectedBank.reserve(resp.items.size());
		for (const auto& it : resp.items)
		{
			GuildBankItem local;
			local.slotIndex = it.slotIndex;
			local.itemName  = it.itemName;
			local.count     = it.count;
			m_state.selectedBank.push_back(std::move(local));
		}
		m_state.bankLoaded = true;

		LOG_INFO(Net, "[GuildUiPresenter] OnBankResponse OK items={}",
			m_state.selectedBank.size());
	}

	void GuildUiPresenter::OnMotdUpdateNotification(const engine::network::GuildMotdUpdateNotificationPayload& note)
	{
		// Mise a jour locale du cache guildes : remplace motd si trouve.
		for (auto& g : m_state.guilds)
		{
			if (g.guildId == note.guildId)
			{
				g.motd = note.newMotd;
				break;
			}
		}
		// Met a jour le toast (lastMotd*).
		m_state.lastMotdGuildId      = note.guildId;
		m_state.lastMotdNewValue     = note.newMotd;
		m_state.lastMotdChangeTimeMs = SteadyMs();
		LOG_DEBUG(Net, "[GuildUiPresenter] OnMotdUpdateNotification gid={} motdLen={}",
			note.guildId, note.newMotd.size());
	}
}
