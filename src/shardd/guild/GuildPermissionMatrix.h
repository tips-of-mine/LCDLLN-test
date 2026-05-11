#pragma once
// CMANGOS.21 (Phase 3.21a) — GuildPermissionMatrix : matrice de permissions
// par rang de guilde (data-driven). Differe de GuildSystem.cpp deja present
// en se focalisant sur la check de permission, pas la persistance/wire.
// Header-only.

#include <cstdint>
#include <unordered_map>

namespace engine::server::guild
{
	using GuildId = uint32_t;
	using RankId  = uint8_t;  ///< 0 = Guild Master, 9 = Initiate (a la WoW)

	enum class Permission : uint32_t
	{
		None         = 0,
		Invite       = 1u << 0,
		Remove       = 1u << 1,
		Promote      = 1u << 2,
		Demote       = 1u << 3,
		EditMotd     = 1u << 4,
		EditTabard   = 1u << 5,
		WithdrawGold = 1u << 6,
		BankView     = 1u << 7,
		BankDeposit  = 1u << 8,
		BankWithdraw = 1u << 9,
		Disband      = 1u << 10,
	};

	inline uint32_t Bit(Permission p) { return static_cast<uint32_t>(p); }

	struct RankPerms
	{
		uint32_t mask = 0; ///< OR de Permission::* bits
	};

	class GuildPermissionMatrix
	{
	public:
		void SetRank(GuildId g, RankId r, uint32_t mask) { m_perms[g][r].mask = mask; }

		bool HasPerm(GuildId g, RankId r, Permission p) const
		{
			auto git = m_perms.find(g);
			if (git == m_perms.end()) return false;
			auto rit = git->second.find(r);
			if (rit == git->second.end()) return false;
			return (rit->second.mask & Bit(p)) != 0;
		}

		/// Renvoie le mask brut d'un rang (0 si guilde ou rang inconnu).
		/// Utilise par GuildHandler pour serialiser la matrice complete sur
		/// le wire (rang -> mask).
		uint32_t GetMask(GuildId g, RankId r) const
		{
			auto git = m_perms.find(g);
			if (git == m_perms.end()) return 0u;
			auto rit = git->second.find(r);
			if (rit == git->second.end()) return 0u;
			return rit->second.mask;
		}

		/// Indique si une guilde a au moins un rang configure dans la matrice.
		/// Utilise par GuildHandler pour valider qu'un guildId existe avant de
		/// servir une requete permissions/members/bank.
		bool HasGuild(GuildId g) const
		{
			return m_perms.find(g) != m_perms.end();
		}

		/// Configure le defaut WoW-like : GM=tout, Officer=invite/remove/promote +
		/// bank, Member=bank view/deposit, Initiate=rien.
		void SetupWowDefaults(GuildId g)
		{
			const uint32_t all =
				Bit(Permission::Invite) | Bit(Permission::Remove) |
				Bit(Permission::Promote) | Bit(Permission::Demote) |
				Bit(Permission::EditMotd) | Bit(Permission::EditTabard) |
				Bit(Permission::WithdrawGold) | Bit(Permission::BankView) |
				Bit(Permission::BankDeposit) | Bit(Permission::BankWithdraw) |
				Bit(Permission::Disband);
			SetRank(g, 0, all);

			const uint32_t officer =
				Bit(Permission::Invite) | Bit(Permission::Remove) |
				Bit(Permission::Promote) | Bit(Permission::Demote) |
				Bit(Permission::BankView) | Bit(Permission::BankDeposit) |
				Bit(Permission::BankWithdraw);
			SetRank(g, 1, officer);

			const uint32_t member =
				Bit(Permission::BankView) | Bit(Permission::BankDeposit);
			SetRank(g, 5, member);

			SetRank(g, 9, 0); // Initiate
		}

	private:
		std::unordered_map<GuildId, std::unordered_map<RankId, RankPerms>> m_perms;
	};
}
