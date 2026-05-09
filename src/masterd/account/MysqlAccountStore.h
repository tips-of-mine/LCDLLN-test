#pragma once

/// @file MysqlAccountStore.h
/// @brief Implémentation MySQL de l'interface AccountStore pour la persistance des comptes joueurs.
///
/// Ce fichier déclare MysqlAccountStore, qui réalise toutes les opérations CRUD sur la table
/// `accounts` (MySQL master via ConnectionPool). Les comptes sans adresse e-mail réelle reçoivent
/// un e-mail de substitution de la forme `<login>@lcdlln.no-email.local` afin de satisfaire la
/// contrainte UNIQUE de la colonne. Les mots de passe ne sont jamais stockés en clair : seul le
/// hash Argon2 final (sel serveur + hash client) est écrit en base.
///
/// Interactions : AccountStore (interface), engine::server::db::ConnectionPool,
///                engine::auth::Argon2Hash, engine::server::AccountValidation.
/// Thread-safety : non (le pool sérialise les connexions, mais les appels eux-mêmes ne sont pas
///                 protégés par un mutex propre — à utiliser depuis un seul thread ou sous verrou externe).
/// Dépendance MySQL : chaque méthode publique acquiert une connexion via ConnectionPool::Acquire().
///                    Si le pool n'est pas initialisé ou si la connexion échoue, la méthode retourne
///                    0 / false / nullopt selon son type de retour.

#include "src/masterd/account/AccountStore.h"

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server
{
	/// Persistance des comptes dans la table `accounts` (pool MySQL master).
	/// Implémente AccountStore ; toutes les opérations passent par un ConnectionPool partagé.
	/// Si la connexion MySQL est perdue (pool non initialisé ou Acquire() échoue), chaque méthode
	/// retourne immédiatement la valeur d'échec documentée sans lever d'exception.
	class MysqlAccountStore final : public AccountStore
	{
	public:
		/// Construit le store en associant le pool de connexions MySQL master.
		/// @param pool Pointeur non-owning vers le ConnectionPool initialisé par le serveur.
		///             Ne doit pas être nullptr pendant toute la durée de vie de cet objet.
		explicit MysqlAccountStore(engine::server::db::ConnectionPool* pool);

		/// Crée un nouveau compte en base et retourne son account_id.
		///
		/// Étapes internes :
		///  1. Normalisation et validation du login (NormaliseLoginView + ValidateLogin).
		///  2. Normalisation et validation de l'e-mail (NormaliseEmail + ValidateEmail).
		///  3. Vérification d'unicité login/e-mail (deux SELECT séparés avant acquisition).
		///  4. Calcul du TAG-ID : préfixe CCYMM (code pays 2 lettres + dernier chiffre de l'année
		///     + mois sur 2 chiffres, UTC) + numéro de séquence 5 chiffres (SELECT MAX sur `accounts`).
		///  5. Génération d'un sel serveur aléatoire + hash Argon2 (client_hash + sel).
		///  6. INSERT INTO accounts (email, login, password_hash, account_status, email_locale,
		///                           email_verified, country_code, tag_id).
		///
		/// Comportement si MySQL est perdu : retourne 0 sans écriture.
		/// En cas de doublon (ER_DUP_ENTRY 1062) : retourne 0 (login ou e-mail déjà pris).
		///
		/// @param login          Login brut (sera normalisé en interne).
		/// @param email          Adresse e-mail brute (peut être vide — un placeholder sera utilisé).
		/// @param client_hash    Hash côté client transmis par le joueur (jamais stocké tel quel).
		/// @param first_name     Prénom (non stocké en v1, ignoré).
		/// @param last_name      Nom de famille (non stocké en v1, ignoré).
		/// @param birth_date     Date de naissance (non stockée en v1, ignorée).
		/// @param country_code   Code pays ISO 3166-1 alpha-2 (ex. "FR"). Utilisé pour le TAG-ID.
		/// @param tag_id_out     [out] TAG-ID généré (ex. "FR60200001"). Rempli uniquement si retour != 0.
		/// @param email_locale   Langue des e-mails transactionnels (défaut : anglais).
		/// @return account_id (> 0) en cas de succès, 0 en cas d'échec.
		uint64_t CreateAccount(std::string_view login, std::string_view email, std::string_view client_hash,
			std::string_view first_name, std::string_view last_name, std::string_view birth_date,
			std::string_view country_code,
			std::string& tag_id_out,
			AccountEmailLocale email_locale = AccountEmailLocale::English) override;

		/// Recherche un compte par login normalisé.
		/// SQL : SELECT id, email, login, password_hash, account_status, email_verified, email_locale
		///       FROM accounts WHERE login=? LIMIT 1
		/// @param normalisedLogin Login déjà normalisé (minuscules, trim).
		/// @return AccountRecord rempli si trouvé, nullopt sinon ou si connexion perdue.
		std::optional<AccountRecord> FindByLogin(std::string_view normalisedLogin) override;

		/// Recherche un compte par son identifiant numérique.
		/// SQL : SELECT ... FROM accounts WHERE id=? LIMIT 1
		/// @param account_id Identifiant auto-incrémenté de la table `accounts`.
		/// @return AccountRecord rempli si trouvé, nullopt sinon ou si connexion perdue.
		std::optional<AccountRecord> FindByAccountId(uint64_t account_id) override;

		/// Vérifie si une adresse e-mail normalisée est déjà enregistrée.
		/// SQL : SELECT 1 FROM accounts WHERE email=? LIMIT 1
		/// Retourne false si normalisedEmail est vide (les placeholders sont ignorés côté appelant).
		/// @param normalisedEmail E-mail déjà normalisé (lowercase, trim).
		/// @return true si l'e-mail est déjà pris, false sinon ou si connexion perdue.
		bool ExistsEmail(std::string_view normalisedEmail) override;

		/// Vérifie si un login normalisé est déjà enregistré.
		/// SQL : SELECT 1 FROM accounts WHERE login=? LIMIT 1
		/// @param normalisedLogin Login déjà normalisé.
		/// @return true si le login est déjà pris, false sinon ou si connexion perdue.
		bool ExistsLogin(std::string_view normalisedLogin) override;

		/// Recherche un compte par adresse e-mail normalisée.
		/// SQL : SELECT ... FROM accounts WHERE email=? LIMIT 1
		/// @param normalisedEmail E-mail déjà normalisé.
		/// @return AccountRecord rempli si trouvé, nullopt si vide, non trouvé, ou connexion perdue.
		std::optional<AccountRecord> FindByEmail(std::string_view normalisedEmail) override;

		/// Marque l'e-mail d'un compte comme vérifié.
		/// SQL : UPDATE accounts SET email_verified=1 WHERE id=? LIMIT 1
		/// @param account_id Identifiant du compte à mettre à jour.
		/// @return true si la mise à jour a affecté exactement 1 ligne, false sinon (compte inexistant
		///         ou connexion perdue).
		bool SetEmailVerified(uint64_t account_id) override;

		/// Met à jour le hash de mot de passe d'un compte.
		/// SQL : UPDATE accounts SET password_hash=? WHERE id=? LIMIT 1
		/// Le hash doit être le hash Argon2 final (déjà haché côté serveur) — jamais le mot de passe brut.
		/// @param account_id    Identifiant du compte.
		/// @param new_final_hash Nouveau hash Argon2 final (chaîne encodée).
		/// @return true si la mise à jour a affecté 1 ligne, false sinon ou si connexion perdue.
		bool UpdatePasswordHash(uint64_t account_id, std::string_view new_final_hash) override;

		/// Persiste un code de vérification d'e-mail (durée de vie 15 minutes).
		/// SQL :
		///   DELETE FROM email_verifications WHERE account_id=? AND verified_at IS NULL
		///   INSERT INTO email_verifications (account_id, code, expires_at)
		///          VALUES (?, ?, NOW() + INTERVAL 15 MINUTE)
		/// Supprime tout code non-vérifié existant avant d'insérer le nouveau afin d'éviter les doublons.
		/// En cas d'échec d'INSERT, logue un avertissement sans lever d'exception.
		/// @param account_id Identifiant du compte destinataire.
		/// @param code       Code de vérification (chaîne aléatoire, ex. UUID ou token hex).
		void PersistEmailVerificationCode(uint64_t account_id, const std::string& code) override;

		/// Retourne le rôle du compte via SELECT role FROM accounts WHERE id=?.
		/// CMANGOS.06 (Phase 1c).
		AccountRole GetRole(uint64_t account_id) override;

		/// Met à jour le rôle d'un compte via UPDATE accounts SET role=? WHERE id=?.
		/// Refuse role=Console (runtime-only). CMANGOS.06 (Phase 1c).
		bool SetRole(uint64_t account_id, AccountRole role) override;

	private:
		/// Pointeur non-owning vers le ConnectionPool MySQL master.
		/// Null uniquement si le constructeur a reçu nullptr — comportement indéfini si accédé après.
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
