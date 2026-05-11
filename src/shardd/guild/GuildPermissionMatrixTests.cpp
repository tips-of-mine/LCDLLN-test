#include "src/shardd/guild/GuildPermissionMatrix.h"
#include "src/shared/core/Log.h"

namespace
{
	using namespace engine::server::guild;

	bool TestUnknown()
	{
		GuildPermissionMatrix m;
		if (m.HasPerm(1, 0, Permission::Invite)) return false;
		LOG_INFO(Core, "[GuildPermsTests] unknown rejected OK");
		return true;
	}

	bool TestExplicitMask()
	{
		GuildPermissionMatrix m;
		m.SetRank(10, 5, Bit(Permission::BankView) | Bit(Permission::Invite));
		if (!m.HasPerm(10, 5, Permission::BankView)) return false;
		if (!m.HasPerm(10, 5, Permission::Invite)) return false;
		if (m.HasPerm(10, 5, Permission::Disband)) return false;
		LOG_INFO(Core, "[GuildPermsTests] explicit mask OK");
		return true;
	}

	bool TestWowDefaults()
	{
		GuildPermissionMatrix m;
		m.SetupWowDefaults(20);
		// GM (rank 0) : tout
		if (!m.HasPerm(20, 0, Permission::Disband)) return false;
		if (!m.HasPerm(20, 0, Permission::Invite)) return false;
		// Officer (rank 1) : pas Disband
		if (m.HasPerm(20, 1, Permission::Disband)) return false;
		if (!m.HasPerm(20, 1, Permission::Invite)) return false;
		// Member (rank 5) : pas Invite
		if (m.HasPerm(20, 5, Permission::Invite)) return false;
		if (!m.HasPerm(20, 5, Permission::BankView)) return false;
		// Initiate (rank 9) : rien
		if (m.HasPerm(20, 9, Permission::BankView)) return false;
		LOG_INFO(Core, "[GuildPermsTests] WoW defaults OK");
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

	const bool ok = TestUnknown() && TestExplicitMask() && TestWowDefaults();
	if (ok) LOG_INFO(Core, "[GuildPermsTests] ALL OK");
	else LOG_ERROR(Core, "[GuildPermsTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
