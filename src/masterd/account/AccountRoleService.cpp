#include "src/masterd/account/AccountRoleService.h"

#include "src/masterd/account/AccountStore.h"
#include "src/shared/security/SecurityAuditLog.h"
#include "src/shared/core/Log.h"

#include <string>

namespace engine::server
{
	AccountRoleService::AccountRoleService(AccountStore& store, SecurityAuditLog* auditLog)
		: m_store(store)
		, m_auditLog(auditLog)
	{
	}

	AccountRole AccountRoleService::GetRole(uint64_t account_id) const
	{
		return m_store.GetRole(account_id);
	}

	bool AccountRoleService::SetRole(uint64_t target_account_id, AccountRole new_role, uint64_t actor_id)
	{
		if (new_role == AccountRole::Console)
			return false;  // jamais persisté

		const AccountRole old_role = m_store.GetRole(target_account_id);
		const bool ok = m_store.SetRole(target_account_id, new_role);
		if (!ok)
			return false;

		if (m_auditLog)
		{
			// Utilise LogModerationAction (signature existante de SecurityAuditLog) :
			// (action, actor_display, target_display, detail).
			const std::string actor_display = (actor_id == 0)
				? std::string("system")
				: ("account:" + std::to_string(actor_id));
			const std::string target_display = "account:" + std::to_string(target_account_id);
			std::string detail = "old=";
			detail += RoleToString(old_role);
			detail += " new=";
			detail += RoleToString(new_role);
			m_auditLog->LogModerationAction("role_change", actor_display, target_display, detail);
		}
		return true;
	}

	bool AccountRoleService::HasLowerSecurity(uint64_t target_account_id, uint64_t source_account_id) const
	{
		const AccountRole target = m_store.GetRole(target_account_id);
		const AccountRole source = m_store.GetRole(source_account_id);
		return HasLowerSecurity(target, source);
	}

	bool AccountRoleService::RequireMinRole(uint64_t account_id, AccountRole min_required) const
	{
		const AccountRole have = m_store.GetRole(account_id);
		return RequireMinRole(have, min_required);
	}

	bool AccountRoleService::HasLowerSecurity(AccountRole target, AccountRole source) noexcept
	{
		return target < source;  // strict (<), égalité = false
	}

	bool AccountRoleService::RequireMinRole(AccountRole have, AccountRole min_required) noexcept
	{
		return have >= min_required;
	}
}
