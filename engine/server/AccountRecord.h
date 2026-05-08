// AccountRecord.h — Définition de la structure de données centrale d'un compte joueur et de son statut.
// Représente une ligne de la table `accounts` MySQL ou son équivalent en RAM (InMemoryAccountStore).
// Utilisé en lecture par AuthRegisterHandler, PasswordResetHandler et AccountStore pour toutes les
// opérations d'authentification, d'inscription et de réinitialisation de mot de passe.
// Pas de thread-safety propre : la protection est assurée par le store qui le contient.

#pragma once

#include "engine/server/LocalizedEmail.h"
#include "engine/server/AccountRole.h"

#include <cstdint>
#include <string>

namespace engine::server
{
	/// Statut d'un compte côté serveur d'authentification.
	/// Aligné sur la colonne `accounts.account_status` en base MySQL :
	/// Active=0 (accès normal), Locked=2 (compte suspendu ou banni).
	/// Les valeurs numériques sont fixes et ne doivent pas changer
	/// sans migration de base de données.
	enum class AccountStatus : uint8_t
	{
		/// Compte actif — authentification autorisée. Valeur MySQL : 0.
		Active = 0,
		/// Compte verrouillé — authentification refusée avec ACCOUNT_LOCKED.
		/// Peut résulter d'une suspension manuelle ou d'une détection de fraude. Valeur MySQL : 2.
		Locked = 2,
	};

	/// Enregistrement complet d'un compte joueur, côté master-server.
	/// Chargé depuis MySQL (MySqlAccountStore) ou maintenu en RAM (InMemoryAccountStore).
	/// Copié par valeur lors des recherches — ne pas conserver de pointeur vers cette structure.
	struct AccountRecord
	{
		/// Identifiant unique du compte. Clé primaire AUTO_INCREMENT de la table `accounts`.
		/// 0 = non initialisé / valeur sentinelle d'échec dans CreateAccount().
		uint64_t account_id = 0;

		/// Identifiant de connexion du joueur, normalisé (trimé, casse originale préservée).
		/// Longueur : 3–64 caractères alphanumériques + underscore (cf. AccountValidation).
		std::string login;

		/// Adresse e-mail normalisée (trimée + minuscules). Peut être vide si non fournie à l'inscription.
		/// Utilisée pour les e-mails de vérification et de réinitialisation de mot de passe.
		std::string email;

		/// Prénom du joueur tel que saisi à l'inscription. Non normalisé, non validé côté serveur.
		std::string first_name;

		/// Nom de famille du joueur tel que saisi à l'inscription. Non normalisé, non validé côté serveur.
		std::string last_name;

		/// Date de naissance au format libre (ex. "1990-05-21") telle que fournie par le client.
		/// Non parsée ni validée côté serveur actuellement.
		std::string birth_date;

		/// Hash final du mot de passe stocké en base (colonne `password_hash`).
		/// Format : encodage Argon2id avec sel serveur, produit par engine::auth::Hash().
		/// Ne jamais journaliser ni exposer côté réseau.
		std::string final_hash;

		/// Statut actuel du compte. Vérifié à chaque tentative d'authentification.
		/// Valeur par défaut : Active (0).
		AccountStatus status = AccountStatus::Active;

		/// Indique si l'adresse e-mail a été confirmée via le code de vérification à 6 chiffres.
		/// false tant que HandleVerifyEmail() n'a pas validé le code. Requis pour certaines fonctionnalités.
		bool email_verified = false;

		/// Langue préférée pour les e-mails transactionnels (vérification, reset).
		/// Déterminée à l'inscription via le champ locale_tag du payload. Défaut : English.
		AccountEmailLocale email_locale = AccountEmailLocale::English;

		/// Code pays ISO-3166-1 alpha-2 (ex. "FR", "US"). Vide si non fourni à l'inscription.
		/// Utilisé pour générer le préfixe du TAG-ID (ex. "FR" → "FR60400123").
		std::string country_code;

		/// Identifiant TAG-ID unique généré à l'inscription (ex. "FR60400123").
		/// Format : [CC][année%10][mois sur 2][account_id sur 5]. Vide si non encore généré.
		std::string tag_id;

		/// Rôle hiérarchique du compte (CMANGOS.06 Phase 1c). Default = Player.
		/// Persisté en DB (ENUM 4 valeurs : player/moderator/game_master/
		/// administrator). `Console` est runtime-only (jamais sérialisé).
		AccountRole role = AccountRole::Player;
	};
}
