#pragma once
// CMANGOS.06 (Phase 1c) — AccountRole : hiérarchie 5 niveaux + helpers
// HasLowerSecurity / RequireMinRole. Console est un sentinel runtime
// (jamais persisté en DB).

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace engine::server
{
	/// Hiérarchie des rôles. Numérotation monotone croissante (un rôle plus
	/// haut a plus de droits). Comparaison via operator<=> (C++20).
	///
	/// Persistance DB : 4 valeurs (Player..Administrator) stockées comme
	/// ENUM string ('player'/'moderator'/'game_master'/'administrator').
	/// `Console` est sentinel runtime exclusivement — RCON, commandes stdin.
	enum class AccountRole : uint8_t
	{
		Player        = 0,    ///< Default, gameplay normal.
		Moderator     = 1,    ///< .mute, .kick, .warn — pas de ban.
		GameMaster    = 2,    ///< Mod + .ban, .tele, .spawn (test items), .go.
		Administrator = 3,    ///< GM + .account create/delete, .set role, logs.
		Console       = 4,    ///< Sentinel runtime — toutes commandes (shutdown, reload all).
	};

	/// Comparaison ordinale type-safe (C++20 spaceship). Permet
	/// `if (role >= AccountRole::Moderator)` directement.
	inline constexpr auto operator<=>(AccountRole a, AccountRole b) noexcept
	{
		return static_cast<uint8_t>(a) <=> static_cast<uint8_t>(b);
	}
	inline constexpr bool operator==(AccountRole a, AccountRole b) noexcept
	{
		return static_cast<uint8_t>(a) == static_cast<uint8_t>(b);
	}

	/// Convertit AccountRole → string (snake_case, aligné avec l'ENUM SQL).
	/// `Console` retourne "console" mais n'est pas censé être persisté.
	std::string_view RoleToString(AccountRole role) noexcept;

	/// Parse un string vers AccountRole. Accepte les 4 valeurs SQL +
	/// "console". Pour la rétrocompatibilité, "admin" est mappé à
	/// `Administrator` (ne devrait plus exister après migration 0043).
	/// Retourne `Player` si la valeur est inconnue (sentinel sûr).
	AccountRole ParseRole(std::string_view s) noexcept;
}
