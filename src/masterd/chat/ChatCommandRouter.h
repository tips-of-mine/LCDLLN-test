#pragma once
// CMANGOS.01 (Phase 2.01b) — ChatCommandRouter : dispatcher table-driven
// pour les slash-commands côté master, avec contrôle de rôle minimal.
//
// Le master reçoit un `kOpcodeChatSendRequest` (cf. ChatRelayHandler).
// Si le texte commence par '/', on le confie à ChatCommandRouter avant
// le routage de canal. Si la commande est connue ET le rôle suffisant,
// on dispatche au handler enregistré ; sinon on rejette (`UnknownCommand`
// ou `InsufficientRole`) et le caller décide quoi montrer à l'utilisateur.

#include "src/masterd/account/AccountRole.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::server::chat
{
	/// Résultat d'un appel `Dispatch`.
	enum class CommandDispatchResult : uint8_t
	{
		/// Le texte ne commence PAS par '/' → pas une commande, le caller
		/// continue le routage de canal normal.
		NotACommand = 0,
		/// Commande non enregistrée (orthographe, casse, etc.).
		UnknownCommand = 1,
		/// Commande connue mais rôle insuffisant (audit log côté caller).
		InsufficientRole = 2,
		/// Dispatch effectué — le handler a été appelé.
		Dispatched = 3,
	};

	/// Handler invoqué après dispatch. `args` est le reste du texte après
	/// le nom de la commande, déjà trimé (espaces de début/fin retirés).
	using CommandHandlerFn = std::function<void(uint64_t accountId, std::string_view args)>;

	/// Entry de la table interne. minRole inclus : un GM peut appeler une
	/// commande de minRole=Moderator (cf. RequireMinRole).
	struct CommandEntry
	{
		AccountRole       minRole;
		CommandHandlerFn  handler;
	};

	class ChatCommandRouter
	{
	public:
		ChatCommandRouter() = default;

		/// Enregistre une commande. \param name doit commencer par '/'
		/// (sinon ajout automatique). Le nom est stocké en lower-case
		/// ASCII pour matcher case-insensitive. Idempotent : un appel
		/// avec le même nom remplace l'entry précédente.
		void Register(std::string_view name, AccountRole minRole, CommandHandlerFn handler);

		/// Désinscrit une commande. Idempotent : no-op si pas trouvée.
		void Unregister(std::string_view name);

		/// Tente de dispatcher un texte. Si \p text ne commence pas par
		/// '/' → `NotACommand`. Si commande inconnue → `UnknownCommand`.
		/// Si rôle insuffisant → `InsufficientRole`. Si OK → invoque le
		/// handler enregistré et retourne `Dispatched`. \p text peut
		/// être déjà sanitizé en amont.
		///
		/// La commande est extraite du premier "mot" (jusqu'au premier
		/// espace), comparée case-insensitive, et le reste devient \p args.
		///
		/// \param text       Message brut, ex. "/kick BadGuy spam".
		/// \param accountId  Account ID de l'expéditeur.
		/// \param role       Rôle de l'expéditeur, comparé à minRole via
		///                   `AccountRoleService::RequireMinRole(role, minRole)`.
		/// \param outName    Optionnel : reçoit le nom de la commande
		///                   parsée (lower-case, sans le '/'). Pratique
		///                   pour log et audit du caller.
		CommandDispatchResult Dispatch(std::string_view text,
			uint64_t accountId, AccountRole role,
			std::string* outName = nullptr) const;

		/// Retourne true si une commande est enregistrée (case-insensitive).
		bool IsRegistered(std::string_view name) const;

		/// Nombre de commandes enregistrées (utile pour tests/debug).
		size_t Size() const { return m_table.size(); }

	private:
		/// Normalise un nom de commande : strip leading '/' + lower-case ASCII.
		static std::string Normalize(std::string_view name);

		std::unordered_map<std::string, CommandEntry> m_table;
	};
}
