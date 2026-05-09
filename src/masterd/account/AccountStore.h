// AccountStore.h — Interface abstraite d'accès au stockage des comptes joueurs.
// Découple la logique métier (AuthRegisterHandler, PasswordResetHandler) de l'implémentation
// physique : InMemoryAccountStore (tests / fallback RAM) ou MySqlAccountStore (production).
// Chaque implémentation est responsable de sa propre thread-safety.
// Les entrées login et email doivent être normalisées par l'appelant avant tout appel.

#pragma once

#include "src/masterd/account/AccountRecord.h"
#include "src/masterd/account/AccountRole.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace engine::server
{
	/// Interface abstraite du stockage de comptes joueurs.
	/// Toutes les méthodes sont virtuelles pures — les implémentations concrètes
	/// (InMemoryAccountStore, MySqlAccountStore) fournissent la persistance.
	/// Les arguments login/email doivent être préalablement normalisés via
	/// NormaliseLoginView() et NormaliseEmail() (AccountValidation.h).
	class AccountStore
	{
	public:
		virtual ~AccountStore() = default;

		/// Crée un nouveau compte joueur et génère son TAG-ID unique.
		/// Valide login et email, s'assure de leur unicité, puis hash le client_hash
		/// avec un sel serveur aléatoire (Argon2) avant stockage.
		/// @param login          Login normalisé (trimmé, 3–64 caractères alphanumériques + _).
		/// @param email          E-mail normalisé (trimmé + minuscules). Peut être vide.
		/// @param client_hash    Hash client du mot de passe (pré-hashé côté client avant envoi réseau).
		/// @param first_name     Prénom brut, tel que fourni par le client.
		/// @param last_name      Nom de famille brut, tel que fourni par le client.
		/// @param birth_date     Date de naissance brute, telle que fournie par le client.
		/// @param country_code   Code ISO-3166-1 alpha-2 (ex. "FR") pour le préfixe du TAG-ID.
		/// @param tag_id_out     [out] TAG-ID généré (ex. "FR60400123") ; vide si échec.
		/// @param email_locale   Langue des e-mails transactionnels (défaut : English).
		/// @return account_id (> 0) en cas de succès, 0 en cas d'échec (login/email déjà pris, erreur interne).
		virtual uint64_t CreateAccount(std::string_view login, std::string_view email, std::string_view client_hash,
			std::string_view first_name, std::string_view last_name, std::string_view birth_date,
			std::string_view country_code,
			std::string& tag_id_out,
			AccountEmailLocale email_locale = AccountEmailLocale::English) = 0;

		/// Recherche un compte par son login normalisé.
		/// @param normalisedLogin Login après NormaliseLoginView().
		/// @return AccountRecord si trouvé, nullopt sinon. Retourne une copie par valeur.
		virtual std::optional<AccountRecord> FindByLogin(std::string_view normalisedLogin) = 0;

		/// Recherche un compte par son identifiant numérique unique.
		/// Peut nécessiter une itération linéaire dans les implémentations sans index secondaire (RAM).
		/// @param account_id Identifiant unique du compte (AccountRecord::account_id).
		/// @return AccountRecord si trouvé, nullopt sinon.
		virtual std::optional<AccountRecord> FindByAccountId(uint64_t account_id) = 0;

		/// Vérifie si une adresse e-mail est déjà utilisée par un compte existant.
		/// @param normalisedEmail E-mail après NormaliseEmail().
		/// @return true si l'e-mail est déjà enregistré, false sinon.
		virtual bool ExistsEmail(std::string_view normalisedEmail) = 0;

		/// Vérifie si un login est déjà utilisé par un compte existant.
		/// @param normalisedLogin Login après NormaliseLoginView().
		/// @return true si le login est déjà enregistré, false sinon.
		virtual bool ExistsLogin(std::string_view normalisedLogin) = 0;

		/// Recherche un compte par son adresse e-mail normalisée.
		/// Utilisé par PasswordResetHandler::HandleForgotPassword() pour retrouver le compte
		/// sans exposer si l'e-mail est enregistré (la réponse réseau est toujours succès).
		/// @param normalisedEmail E-mail après NormaliseEmail().
		/// @return AccountRecord si trouvé, nullopt sinon.
		virtual std::optional<AccountRecord> FindByEmail(std::string_view normalisedEmail) = 0;

		/// Marque l'e-mail du compte comme vérifié dans le stockage persistant.
		/// Appelé après validation du code à 6 chiffres par PasswordResetHandler::HandleVerifyEmail().
		/// @param account_id Identifiant du compte à mettre à jour.
		/// @return true si la mise à jour a réussi, false si le compte n'existe pas.
		virtual bool SetEmailVerified(uint64_t account_id) = 0;

		/// Met à jour le hash de mot de passe d'un compte après une réinitialisation réussie.
		/// Le nouveau hash doit être le résultat d'engine::auth::Hash() avec un sel frais.
		/// @param account_id      Identifiant du compte à modifier.
		/// @param new_final_hash  Nouveau hash Argon2 final (colonne `password_hash`).
		/// @return true si la mise à jour a réussi, false si le compte n'existe pas.
		virtual bool UpdatePasswordHash(uint64_t account_id, std::string_view new_final_hash) = 0;

		/// Persiste le code de vérification e-mail à 6 chiffres dans le stockage durable.
		/// Sur MySQL : écrit dans la table `email_verifications` avec expiration 15 min,
		/// en remplaçant toute entrée existante non vérifiée pour ce compte.
		/// No-op sur InMemoryAccountStore : la gestion des codes y est assurée par PasswordResetStore.
		/// @param account_id Identifiant du compte concerné.
		/// @param code       Code à 6 chiffres généré par PasswordResetStore::CreateVerificationCode().
		virtual void PersistEmailVerificationCode(uint64_t account_id, const std::string& code) = 0;

		/// Retourne le rôle d'un compte (CMANGOS.06 Phase 1c).
		/// \param account_id Identifiant du compte.
		/// \return Le rôle stocké, ou `AccountRole::Player` si le compte n'existe pas.
		virtual AccountRole GetRole(uint64_t account_id) = 0;

		/// Met à jour le rôle d'un compte. Persiste en DB (MySql) ou en RAM
		/// (InMemory). N'écrit PAS d'audit log (responsabilité de
		/// `AccountRoleService` qui orchestre la combinaison Store + audit).
		/// \param account_id Compte cible.
		/// \param role       Nouveau rôle. \pre `role != AccountRole::Console`
		///                   (Console est runtime-only, jamais persisté). Si
		///                   appelé avec Console, l'implémentation doit
		///                   retourner `false` sans modifier l'état.
		/// \return true si la mise à jour a réussi, false si compte inexistant
		///         ou si role == Console.
		virtual bool SetRole(uint64_t account_id, AccountRole role) = 0;
	};
}
