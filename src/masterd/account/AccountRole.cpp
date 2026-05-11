#include "src/masterd/account/AccountRole.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace engine::server
{
	std::string_view RoleToString(AccountRole role) noexcept
	{
		switch (role)
		{
			case AccountRole::Player:        return "player";
			case AccountRole::Moderator:     return "moderator";
			case AccountRole::GameMaster:    return "game_master";
			case AccountRole::Administrator: return "administrator";
			case AccountRole::Console:       return "console";
		}
		return "player";  // sentinel sûr
	}

	AccountRole ParseRole(std::string_view s) noexcept
	{
		// Lowercase pour case-insensitive.
		std::string lc;
		lc.reserve(s.size());
		for (char c : s)
			lc.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

		if (lc == "player")           return AccountRole::Player;
		if (lc == "moderator")        return AccountRole::Moderator;
		if (lc == "game_master")      return AccountRole::GameMaster;
		if (lc == "gm")               return AccountRole::GameMaster;  // alias court
		if (lc == "administrator")    return AccountRole::Administrator;
		if (lc == "admin")            return AccountRole::Administrator; // retrocompat
		if (lc == "console")          return AccountRole::Console;
		return AccountRole::Player;   // unknown → safe default
	}
}
