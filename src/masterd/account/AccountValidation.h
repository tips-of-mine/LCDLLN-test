// AccountValidation.h — Normalisation et validation des champs d'inscription/authentification.
// Définit les contraintes de schéma v1 (longueurs, jeu de caractères, politique de mot de passe)
// et expose les fonctions de normalisation (trim, minuscules) utilisées en amont de toute validation.
// Sans état, sans effet de bord. Utilisé par AuthRegisterHandler, PasswordResetHandler et les stores.
// Non thread-safe (fonctions pures, aucun état partagé — utilisation concurrente sans restriction).

#pragma once

#include "src/shared/network/NetErrorCode.h"

#include <cstddef>
#include <optional>
#include <string_view>

namespace engine::server
{
	/// Longueur maximale d'un e-mail normalisé (RFC 5321 limite à 254 octets ; on arrondit à 256).
	constexpr size_t kAccountEmailMaxLength = 256u;

	/// Longueur minimale d'un login (3 caractères pour éviter les noms de compte trop courts).
	constexpr size_t kAccountLoginMinLength = 3u;

	/// Longueur maximale d'un login (64 caractères, compatible MySQL VARCHAR(64) et affichage UI).
	constexpr size_t kAccountLoginMaxLength = 64u;

	/// Longueur minimale du mot de passe en clair (8 caractères — politique de sécurité v1).
	constexpr size_t kAccountPasswordMinLength = 8u;

	/// Longueur maximale du mot de passe en clair (256 caractères — prévient les attaques DoS
	/// par hash de mots de passe gigantesques sur Argon2).
	constexpr size_t kAccountPasswordMaxLength = 256u;

	/// Normalise une adresse e-mail : supprime les espaces/tabulations/CR/LF en début et fin,
	/// puis convertit tous les caractères en minuscules.
	/// Le résultat normalisé doit être passé à ValidateEmail() et stocké dans AccountRecord::email.
	/// @param input E-mail brut reçu du client (peut contenir des espaces, majuscules, etc.).
	/// @return Chaîne normalisée (trimmée + minuscules) ; vide si l'entrée est vide ou uniquement whitespace.
	std::string NormaliseEmail(std::string_view input);

	/// Normalise un login pour validation : supprime uniquement les espaces/tabulations/CR/LF en début/fin.
	/// Contrairement à NormaliseEmail(), la casse est préservée (les logins sont insensibles à la casse
	/// via la normalisation des clés de recherche, mais stockés tels qu'inscrits).
	/// Retourne une vue sur le buffer d'origine — invalide dès que \a input est libéré.
	/// @param input Login brut reçu du client.
	/// @return Vue trimmée sans allocation. Vide si l'entrée est vide ou uniquement whitespace.
	std::string_view NormaliseLoginView(std::string_view input);

	/// Valide le format et la longueur d'une adresse e-mail normalisée.
	/// Vérifications : non-vide, longueur ≤ kAccountEmailMaxLength, présence d'un '@' non en position 0,
	/// domaine non vide, domaine contenant au moins un '.'.
	/// N'effectue pas de résolution DNS ni de vérification d'existence de la boîte.
	/// @param normalisedEmail E-mail après NormaliseEmail().
	/// @return NetErrorCode::OK si valide, INVALID_EMAIL en cas d'erreur de format ou longueur.
	engine::network::NetErrorCode ValidateEmail(std::string_view normalisedEmail);

	/// Valide le jeu de caractères et la longueur d'un login normalisé.
	/// Caractères autorisés : lettres ASCII (a-z, A-Z), chiffres (0-9), underscore (_).
	/// Longueur : kAccountLoginMinLength (3) à kAccountLoginMaxLength (64) inclus.
	/// @param normalisedLogin Login après NormaliseLoginView().
	/// @return NetErrorCode::OK si valide, INVALID_LOGIN si longueur hors bornes ou caractère interdit.
	engine::network::NetErrorCode ValidateLogin(std::string_view normalisedLogin);

	/// Valide la politique de complexité du mot de passe en clair (politique v1).
	/// Exigences : longueur entre kAccountPasswordMinLength (8) et kAccountPasswordMaxLength (256),
	/// au moins un chiffre (0-9) et au moins une lettre (a-z ou A-Z).
	/// Attention : cette fonction reçoit le mot de passe en clair uniquement pour validation ;
	/// il ne doit jamais être stocké ni journalisé.
	/// @param password Mot de passe en clair, non normalisé (la casse et les espaces internes sont préservés).
	/// @return NetErrorCode::OK si valide, WEAK_PASSWORD si longueur insuffisante ou complexité manquante.
	engine::network::NetErrorCode ValidatePassword(std::string_view password);
}
