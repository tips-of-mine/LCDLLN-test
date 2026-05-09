#pragma once
// CMANGOS.06 (Phase 1c) — AccountRoleService : façade au-dessus
// d'AccountStore qui expose HasLowerSecurity + RequireMinRole et câble
// l'audit via SecurityAuditLog.

#include "src/masterd/account/AccountRole.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::server
{
	class AccountStore;
	class SecurityAuditLog;

	/// Façade combinant AccountStore (persistance) + SecurityAuditLog
	/// (traçabilité). Toutes les méthodes sont thread-safe via la
	/// thread-safety des composants sous-jacents.
	class AccountRoleService
	{
	public:
		/// \param store      Référence au store de comptes (non-owning).
		/// \param auditLog   Référence à l'audit (non-owning) ; peut être nullptr
		///                   pour désactiver l'audit (utile en tests pure logic).
		AccountRoleService(AccountStore& store, SecurityAuditLog* auditLog);

		/// Lecture du rôle (delegate vers Store).
		AccountRole GetRole(uint64_t account_id) const;

		/// Mise à jour du rôle + audit. Le `actor_id` est l'auteur du
		/// changement (ex. l'admin qui promote un joueur). Si actor_id = 0,
		/// l'audit indique "system" (ex. seed initial).
		bool SetRole(uint64_t target_account_id, AccountRole new_role, uint64_t actor_id);

		/// Retourne true si `target` a un rôle STRICTEMENT INFÉRIEUR à `source`.
		/// Égalité = false (cf. règle ticket §3 : un GM ne peut pas ban un GM).
		/// Lit les rôles via le store (1 ou 2 lookups DB).
		bool HasLowerSecurity(uint64_t target_account_id, uint64_t source_account_id) const;

		/// Retourne true si le compte a au moins le rôle minimum requis.
		bool RequireMinRole(uint64_t account_id, AccountRole min_required) const;

		/// Variante stateless : compare deux rôles directement (utile quand
		/// le caller a déjà les rôles en RAM, évite des lookups DB redondants).
		static bool HasLowerSecurity(AccountRole target, AccountRole source) noexcept;
		static bool RequireMinRole(AccountRole have, AccountRole min_required) noexcept;

	private:
		AccountStore& m_store;
		SecurityAuditLog* m_auditLog;  // optional
	};
}
