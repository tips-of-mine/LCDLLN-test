#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::network
{
	/// Mode de jeu d'un serveur (shard), choisi au démarrage. Transmis sur le fil (u8)
	/// et exposé dans l'API /status. Remplace le terme « PvE » figé de l'écran de choix.
	enum class ShardGameMode : uint8_t
	{
		PvE = 0,
		PvP = 1,
	};

	/// Règle / style de jeu d'un serveur, choisie dans une liste fermée au démarrage.
	/// Transmise sur le fil (u8) et exposée dans l'API /status. Remplace le terme
	/// « COOPERATIVE » figé de l'écran de choix.
	enum class ShardRuleset : uint8_t
	{
		Cooperative = 0,
		Competitive = 1,
		Hardcore = 2,
		RolePlay = 3,
	};

	inline constexpr uint8_t kShardGameModeCount = 2u;
	inline constexpr uint8_t kShardRulesetCount = 4u;

	/// Borne une valeur u8 reçue sur le fil (potentiellement hors plage) à un mode
	/// connu. Repli PvE si la valeur est inconnue (compat ascendante / paquet corrompu).
	inline ShardGameMode ClampGameMode(uint8_t raw)
	{
		return (raw < kShardGameModeCount) ? static_cast<ShardGameMode>(raw) : ShardGameMode::PvE;
	}

	/// Idem ClampGameMode pour la règle. Repli Cooperative si inconnue.
	inline ShardRuleset ClampRuleset(uint8_t raw)
	{
		return (raw < kShardRulesetCount) ? static_cast<ShardRuleset>(raw) : ShardRuleset::Cooperative;
	}

	/// Jeton canonique (minuscules ASCII) du mode. Sert à la fois de valeur JSON
	/// (/status) et de suffixe de clé de localisation client
	/// (« auth.shard_pick.mode.<token> »).
	inline std::string_view GameModeToken(ShardGameMode m)
	{
		switch (m)
		{
		case ShardGameMode::PvP: return "pvp";
		case ShardGameMode::PvE:
		default: return "pve";
		}
	}

	/// Jeton canonique (minuscules ASCII) de la règle. Sert de valeur JSON (/status)
	/// et de suffixe de clé de localisation client (« auth.shard_pick.ruleset.<token> »).
	inline std::string_view RulesetToken(ShardRuleset r)
	{
		switch (r)
		{
		case ShardRuleset::Competitive: return "competitive";
		case ShardRuleset::Hardcore: return "hardcore";
		case ShardRuleset::RolePlay: return "roleplay";
		case ShardRuleset::Cooperative:
		default: return "cooperative";
		}
	}

	namespace detail
	{
		/// Minusculise en ASCII uniquement (les jetons de config sont ASCII). Évite
		/// toute dépendance à la locale C++.
		inline std::string ToLowerAscii(std::string_view s)
		{
			std::string out;
			out.reserve(s.size());
			for (char c : s)
				out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
			return out;
		}
	}

	/// Parse un mode depuis une valeur de config (« pve » / « pvp », insensible à la
	/// casse). Repli \p fallback si non reconnu.
	inline ShardGameMode ParseGameMode(std::string_view s, ShardGameMode fallback = ShardGameMode::PvE)
	{
		const std::string t = detail::ToLowerAscii(s);
		if (t == "pvp") return ShardGameMode::PvP;
		if (t == "pve") return ShardGameMode::PvE;
		return fallback;
	}

	/// Parse une règle depuis une valeur de config (jeton canonique ou alias usuel,
	/// insensible à la casse). Repli \p fallback si non reconnu.
	inline ShardRuleset ParseRuleset(std::string_view s, ShardRuleset fallback = ShardRuleset::Cooperative)
	{
		const std::string t = detail::ToLowerAscii(s);
		if (t == "cooperative" || t == "coop" || t == "cooperatif") return ShardRuleset::Cooperative;
		if (t == "competitive" || t == "competitif") return ShardRuleset::Competitive;
		if (t == "hardcore") return ShardRuleset::Hardcore;
		if (t == "roleplay" || t == "rp" || t == "jdr") return ShardRuleset::RolePlay;
		return fallback;
	}
}
