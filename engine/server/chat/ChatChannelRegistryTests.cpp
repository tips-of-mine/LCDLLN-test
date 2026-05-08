// CMANGOS.01 (Phase 2.01b) — Tests ChatChannelRegistry.
// Pure : aucune dépendance externe. RAM-only.

#include "engine/server/chat/ChatChannelRegistry.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <vector>

namespace
{
	using engine::server::chat::ChannelJoinResult;
	using engine::server::chat::ChatChannelRegistry;

	bool TestJoinCreatesChannel()
	{
		ChatChannelRegistry r;
		const auto j = r.Join(1, "world");
		if (j != ChannelJoinResult::OK)
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] Join expected OK, got {}",
				static_cast<int>(j));
			return false;
		}
		auto info = r.Info("world");
		if (!info || info->ownerAccountId != 1 || info->memberCount != 1
			|| info->hasPassword)
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] info wrong after first Join");
			return false;
		}
		LOG_INFO(Core, "[ChatChannelRegistryTests] Join creates channel + ownership OK");
		return true;
	}

	bool TestJoinIdempotent()
	{
		ChatChannelRegistry r;
		r.Join(1, "trade");
		const auto j2 = r.Join(1, "trade");
		if (j2 != ChannelJoinResult::AlreadyMember)
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] re-Join should be AlreadyMember, got {}",
				static_cast<int>(j2));
			return false;
		}
		LOG_INFO(Core, "[ChatChannelRegistryTests] Join idempotent OK");
		return true;
	}

	bool TestPasswordWrong()
	{
		ChatChannelRegistry r;
		r.Join(1, "secret");
		if (!r.SetPassword(1, "secret", "hush"))
			return false;
		const auto j = r.Join(2, "secret", "wrong");
		if (j != ChannelJoinResult::WrongPassword)
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] wrong password should be rejected");
			return false;
		}
		const auto j2 = r.Join(2, "secret", "hush");
		if (j2 != ChannelJoinResult::OK)
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] correct password should accept");
			return false;
		}
		LOG_INFO(Core, "[ChatChannelRegistryTests] password OK");
		return true;
	}

	bool TestBanFlow()
	{
		ChatChannelRegistry r;
		r.Join(1, "elite");
		r.Join(2, "elite");
		// Owner=1 bannit 2.
		if (!r.Ban(1, "elite", 2))
			return false;
		if (!r.IsBanned(2, "elite"))
			return false;
		if (r.IsMember(2, "elite"))
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] banned member should be ejected");
			return false;
		}
		// 2 tente de rejoindre → Banned.
		if (r.Join(2, "elite") != ChannelJoinResult::Banned)
			return false;

		// Non-owner tente de bannir → refus.
		r.Join(3, "elite");
		if (r.Ban(3, "elite", 1))
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] non-owner Ban should fail");
			return false;
		}

		// Unban remet en jeu.
		if (!r.Unban(1, "elite", 2))
			return false;
		if (r.IsBanned(2, "elite"))
			return false;
		if (r.Join(2, "elite") != ChannelJoinResult::OK)
			return false;

		LOG_INFO(Core, "[ChatChannelRegistryTests] ban/unban flow OK");
		return true;
	}

	bool TestLeaveAndOwnerTransfer()
	{
		ChatChannelRegistry r;
		r.Join(1, "lobby");
		r.Join(2, "lobby");
		r.Join(3, "lobby");
		auto info = r.Info("lobby");
		if (!info || info->ownerAccountId != 1)
			return false;

		// Owner part : transfer.
		r.Leave(1, "lobby");
		info = r.Info("lobby");
		if (!info)
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] channel should still exist after owner leave");
			return false;
		}
		if (info->ownerAccountId == 1)
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] ownership should have transferred");
			return false;
		}
		if (info->ownerAccountId != 2 && info->ownerAccountId != 3)
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] new owner unexpected");
			return false;
		}

		// Tous partent : canal supprimé.
		r.Leave(2, "lobby");
		r.Leave(3, "lobby");
		if (r.Info("lobby"))
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] channel should be deleted after last leave");
			return false;
		}
		LOG_INFO(Core, "[ChatChannelRegistryTests] Leave + owner transfer OK");
		return true;
	}

	bool TestLeaveAll()
	{
		ChatChannelRegistry r;
		r.Join(1, "a");
		r.Join(1, "b");
		r.Join(1, "c");
		r.Join(2, "a");

		r.LeaveAll(1);
		// b et c étaient solo (1 seul membre = owner) → supprimés.
		// a avait aussi 2 → reste, ownership transféré à 2.
		if (r.Info("b") || r.Info("c"))
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] solo channels of LeaveAll should be deleted");
			return false;
		}
		auto info = r.Info("a");
		if (!info || info->ownerAccountId != 2)
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] LeaveAll : a should still exist with owner=2");
			return false;
		}
		LOG_INFO(Core, "[ChatChannelRegistryTests] LeaveAll OK");
		return true;
	}

	bool TestNameNormalizationAndValidation()
	{
		ChatChannelRegistry r;
		// Case-insensitive : "World" et "world" sont le même canal.
		r.Join(1, "World");
		const auto j2 = r.Join(2, "WORLD");
		if (j2 != ChannelJoinResult::OK)
			return false;
		auto info = r.Info("world");
		if (!info || info->memberCount != 2)
			return false;

		// Nom invalide (vide).
		if (r.Join(1, "") != ChannelJoinResult::InvalidName)
			return false;
		// Nom invalide (trop long).
		std::string tooLong(33, 'a');
		if (r.Join(1, tooLong) != ChannelJoinResult::InvalidName)
			return false;
		// Nom invalide ('/').
		if (r.Join(1, "ab/cd") != ChannelJoinResult::InvalidName)
			return false;
		// Nom invalide (espace).
		if (r.Join(1, "ab cd") != ChannelJoinResult::InvalidName)
			return false;

		LOG_INFO(Core, "[ChatChannelRegistryTests] name normalization + validation OK");
		return true;
	}

	bool TestMembersAndCount()
	{
		ChatChannelRegistry r;
		r.Join(1, "x");
		r.Join(2, "x");
		r.Join(3, "x");
		auto m = r.Members("x");
		std::sort(m.begin(), m.end());
		if (m.size() != 3 || m[0] != 1 || m[1] != 2 || m[2] != 3)
		{
			LOG_ERROR(Core, "[ChatChannelRegistryTests] Members snapshot wrong");
			return false;
		}
		if (r.ChannelCount() != 1)
			return false;
		LOG_INFO(Core, "[ChatChannelRegistryTests] Members + ChannelCount OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestJoinCreatesChannel()
		&& TestJoinIdempotent()
		&& TestPasswordWrong()
		&& TestBanFlow()
		&& TestLeaveAndOwnerTransfer()
		&& TestLeaveAll()
		&& TestNameNormalizationAndValidation()
		&& TestMembersAndCount();

	if (ok)
		LOG_INFO(Core, "[ChatChannelRegistryTests] ALL OK");
	else
		LOG_ERROR(Core, "[ChatChannelRegistryTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
