// Payloads d'authentification et d'inscription — protocole v1.
// Chaque struct correspond à un échange réseau spécifique (requête ou réponse).
// Les fonctions Parse* désérialisent depuis un buffer binaire reçu ;
// les fonctions Build* produisent un buffer ou un paquet complet prêt à l'envoi.
// Thread-safety : toutes les fonctions sont stateless et thread-safe.
#pragma once

#include "src/shared/network/NetErrorCode.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	/// Payload AUTH_REQUEST reçu par le serveur (Client → Master).
	/// Format binaire : [ uint16 login_len ][ login_utf8 ][ uint16 hash_len ][ client_hash_utf8 ]
	/// client_hash : chaîne encodée Argon2 calculée côté client (M20.2).
	struct AuthRequestPayload
	{
		std::string login;       ///< Identifiant de connexion saisi par l'utilisateur.
		std::string client_hash; ///< Hash Argon2 du mot de passe (jamais le mot de passe en clair).
	};

	/// Payload REGISTER_REQUEST reçu par le serveur (Client → Master).
	/// Format binaire (tous les champs sont des strings préfixées uint16, dans cet ordre) :
	///   login, client_hash, email [, first_name, last_name, birth_date, captcha_token, locale_tag, country_code]
	/// Les champs à partir de first_name sont optionnels (backward-compatible avec les anciens clients).
	struct RegisterRequestPayload
	{
		std::string login;         ///< Identifiant souhaité.
		std::string email;         ///< Adresse email pour vérification et mails transactionnels.
		std::string client_hash;   ///< Hash Argon2 du mot de passe (jamais le mot de passe en clair).
		std::string first_name;    ///< Prénom (optionnel selon version client).
		std::string last_name;     ///< Nom de famille (optionnel selon version client).
		std::string birth_date;    ///< Date de naissance au format yyyy-mm-dd (optionnel).
		std::string captcha_token; ///< Token CAPTCHA widget (M33.3). Vide si absent.
		std::string locale_tag;    ///< Code langue ISO (ex. "fr", "en") pour les emails. Vide = anglais.
		std::string country_code;  ///< Code pays ISO-2 (ex. "FR"). Vide si absent (anciens clients).
	};

	/// Désérialise un payload AUTH_REQUEST.
	/// @return Payload parsé, ou nullopt si le buffer est tronqué ou invalide.
	std::optional<AuthRequestPayload> ParseAuthRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Désérialise un payload REGISTER_REQUEST. Les champs optionnels (à partir de first_name)
	/// sont ignorés silencieusement si absents — compatibilité avec les anciens clients.
	/// @return Payload parsé, ou nullopt si les champs obligatoires (login, client_hash) sont tronqués.
	std::optional<RegisterRequestPayload> ParseRegisterRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Sérialise un payload AUTH_REQUEST (Client → Master).
	/// Format : [ uint16 login_len ][ login ][ uint16 hash_len ][ client_hash ]
	/// @return Payload sérialisé, ou vecteur vide si une chaîne dépasse kProtocolV1MaxStringLength.
	std::vector<uint8_t> BuildAuthRequestPayload(std::string_view login, std::string_view client_hash);

	/// Sérialise un payload REGISTER_REQUEST (Client → Master).
	/// Les champs optionnels ne sont sérialisés que si au moins un d'eux est non-vide,
	/// garantissant la compatibilité binaire avec les anciens serveurs.
	/// @return Payload sérialisé, ou vecteur vide si la sérialisation échoue.
	std::vector<uint8_t> BuildRegisterRequestPayload(std::string_view login, std::string_view email, std::string_view client_hash,
	                                                 std::string_view first_name = {},
	                                                 std::string_view last_name = {},
	                                                 std::string_view birth_date = {},
	                                                 std::string_view captcha_token = {},
	                                                 std::string_view locale_tag = {},
	                                                 std::string_view country_code = {});

	/// Payload AUTH_RESPONSE reçu par le client (Master → Client).
	/// Format si success=1 : [ uint8 success=1 ][ uint64 session_id ][ uint64 server_time_sec ][ uint32 version_gate ]
	/// Format si success=0 : [ uint8 success=0 ][ uint32 error_code ] (ou paquet ERROR séparé)
	struct AuthResponsePayload
	{
		uint8_t success = 0;               ///< 1 = authentification réussie, 0 = échec.
		uint64_t session_id = 0;           ///< Session active (valide uniquement si success=1).
		uint64_t server_time_sec = 0;      ///< Temps Unix serveur au moment de la réponse (succès uniquement).
		uint32_t version_gate = 0;         ///< Version minimale client requise par le serveur (succès uniquement).
		NetErrorCode error_code = NetErrorCode::OK; ///< Code d'erreur (valide uniquement si success=0).
	};

	/// Désérialise un payload AUTH_RESPONSE.
	/// @return Payload parsé, ou nullopt si le buffer est tronqué (< 1 octet ou champs manquants).
	std::optional<AuthResponsePayload> ParseAuthResponsePayload(const uint8_t* payload, size_t payloadSize);

	/// Payload REGISTER_RESPONSE reçu par le client (Master → Client).
	/// Format si success=1 : [ uint8 success=1 ][ uint64 account_id ][ uint16 tag_len ][ tag_id ]
	/// Format si success=0 : [ uint8 success=0 ][ uint32 error_code ]
	struct RegisterResponsePayload
	{
		uint8_t success = 0;               ///< 1 = compte créé, 0 = échec.
		uint64_t account_id = 0;           ///< Identifiant numérique du nouveau compte (succès uniquement).
		NetErrorCode error_code = NetErrorCode::OK; ///< Code d'erreur (échec uniquement).
		std::string tag_id;                ///< TAG-ID généré (ex. "FR60400123"). Vide en cas d'échec.
	};

	/// Désérialise un payload REGISTER_RESPONSE.
	/// @return Payload parsé, ou nullopt si le buffer est tronqué.
	std::optional<RegisterResponsePayload> ParseRegisterResponsePayload(const uint8_t* payload, size_t payloadSize);

	/// Construit le paquet AUTH_RESPONSE de succès (success=1) avec session_id, server_time_sec, version_gate.
	/// @param responseHeaderSessionId session_id dans l'en-tête du paquet réponse (0 avant auth).
	/// @return Paquet complet prêt à l'envoi, ou vecteur vide si la sérialisation échoue.
	std::vector<uint8_t> BuildAuthResponsePacket(uint8_t success, uint64_t sessionId, uint64_t serverTimeSec,
		uint32_t versionGate, uint32_t requestId, uint64_t responseHeaderSessionId);

	/// Construit un paquet AUTH_RESPONSE d'échec (success=0) avec le code d'erreur dans le payload.
	std::vector<uint8_t> BuildAuthResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t responseHeaderSessionId);

	/// Construit le paquet REGISTER_RESPONSE de succès (success=1) avec account_id et tag_id.
	/// En cas d'échec, utiliser BuildRegisterResponseErrorPacket à la place.
	std::vector<uint8_t> BuildRegisterResponsePacket(uint8_t success, uint64_t accountId,
	    std::string_view tag_id, uint32_t requestId, uint64_t sessionIdHeader);

	/// Construit un paquet REGISTER_RESPONSE d'échec (success=0) avec le code d'erreur.
	std::vector<uint8_t> BuildRegisterResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t sessionIdHeader);

	// -------------------------------------------------------------------------
	// M33.2 — Réinitialisation de mot de passe + vérification d'email
	// -------------------------------------------------------------------------

	/// Payload FORGOT_PASSWORD_REQUEST (Client → Master).
	/// Format : [ uint16 email_len ][ email_utf8 ]
	struct ForgotPasswordRequestPayload
	{
		std::string email; ///< Adresse email pour l'envoi du lien de réinitialisation.
	};

	/// Désérialise un payload FORGOT_PASSWORD_REQUEST.
	/// @return Payload parsé, ou nullopt si email manquant ou buffer tronqué.
	std::optional<ForgotPasswordRequestPayload> ParseForgotPasswordRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Sérialise un payload FORGOT_PASSWORD_REQUEST.
	std::vector<uint8_t> BuildForgotPasswordRequestPayload(std::string_view email);

	/// Construit le paquet FORGOT_PASSWORD_RESPONSE (success=1 toujours, pour éviter l'énumération d'emails).
	std::vector<uint8_t> BuildForgotPasswordResponsePacket(uint32_t requestId, uint64_t sessionIdHeader);

	/// Payload RESET_PASSWORD_REQUEST (Client → Master).
	/// Format : [ uint16 token_len ][ token_hex ][ uint16 hash_len ][ new_client_hash ]
	struct ResetPasswordRequestPayload
	{
		std::string token;           ///< Token hexadécimal 32 caractères extrait du lien email.
		std::string new_client_hash; ///< Nouveau hash Argon2 du mot de passe calculé côté client.
	};

	/// Désérialise un payload RESET_PASSWORD_REQUEST.
	/// @return Payload parsé, ou nullopt si token ou new_client_hash manquants.
	std::optional<ResetPasswordRequestPayload> ParseResetPasswordRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Sérialise un payload RESET_PASSWORD_REQUEST.
	std::vector<uint8_t> BuildResetPasswordRequestPayload(std::string_view token, std::string_view new_client_hash);

	/// Construit un paquet RESET_PASSWORD_RESPONSE. success=1 OK, success=0 erreur (avec error_code).
	std::vector<uint8_t> BuildResetPasswordResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader);

	/// Construit un paquet RESET_PASSWORD_RESPONSE d'échec avec le code d'erreur.
	std::vector<uint8_t> BuildResetPasswordResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t sessionIdHeader);

	/// Payload VERIFY_EMAIL_REQUEST (Client → Master).
	/// Format : [ uint64 account_id ][ uint16 code_len ][ code_utf8 ]
	struct VerifyEmailRequestPayload
	{
		uint64_t account_id = 0; ///< Identifiant du compte dont l'email est à vérifier.
		std::string code;         ///< Code numérique à 6 chiffres reçu par email.
	};

	/// Désérialise un payload VERIFY_EMAIL_REQUEST.
	/// @return Payload parsé, ou nullopt si account_id ou code manquants (payloadSize < 8).
	std::optional<VerifyEmailRequestPayload> ParseVerifyEmailRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Sérialise un payload VERIFY_EMAIL_REQUEST.
	std::vector<uint8_t> BuildVerifyEmailRequestPayload(uint64_t account_id, std::string_view code);

	/// Construit un paquet VERIFY_EMAIL_RESPONSE. success=1 OK, success=0 erreur.
	std::vector<uint8_t> BuildVerifyEmailResponsePacket(uint8_t success, uint32_t requestId, uint64_t sessionIdHeader);

	/// Construit un paquet VERIFY_EMAIL_RESPONSE d'échec avec le code d'erreur.
	std::vector<uint8_t> BuildVerifyEmailResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t sessionIdHeader);

	// -------------------------------------------------------------------------
	// M33.2-bis — Renvoi du code de vérification email
	// -------------------------------------------------------------------------

	/// Payload RESEND_VERIFICATION_REQUEST (Client → Master).
	/// Format : [ uint64 account_id ]
	struct ResendVerificationRequestPayload
	{
		uint64_t account_id = 0; ///< Compte dont le code de vérification doit être renvoyé.
	};

	/// Désérialise un payload RESEND_VERIFICATION_REQUEST.
	/// @return Payload parsé, ou nullopt si account_id manquant.
	std::optional<ResendVerificationRequestPayload> ParseResendVerificationRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Sérialise un payload RESEND_VERIFICATION_REQUEST.
	std::vector<uint8_t> BuildResendVerificationRequestPayload(uint64_t account_id);

	/// Construit le paquet RESEND_VERIFICATION_RESPONSE (success=1 si code renvoyé).
	std::vector<uint8_t> BuildResendVerificationResponsePacket(uint32_t requestId, uint64_t sessionIdHeader);

	/// Construit un paquet RESEND_VERIFICATION_RESPONSE d'échec avec le code d'erreur.
	std::vector<uint8_t> BuildResendVerificationResponseErrorPacket(NetErrorCode errorCode, uint32_t requestId, uint64_t sessionIdHeader);

	// -------------------------------------------------------------------------
	// Plan C — Vérification de disponibilité du pseudo
	// -------------------------------------------------------------------------

	/// Payload USERNAME_AVAILABLE_REQUEST (Client → Master). Aucune session requise.
	/// Format : [ uint16 login_len ][ login_utf8 ][ uint32 seq ]
	struct UsernameAvailableRequestPayload
	{
		std::string login;   ///< Pseudo à tester.
		uint32_t    seq = 0; ///< Numéro de séquence client, répercuté dans la réponse pour détection de réponses obsolètes.
	};

	/// Désérialise un payload USERNAME_AVAILABLE_REQUEST.
	std::optional<UsernameAvailableRequestPayload> ParseUsernameAvailableRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Sérialise un payload USERNAME_AVAILABLE_REQUEST.
	std::vector<uint8_t> BuildUsernameAvailableRequestPayload(std::string_view login, uint32_t seq);

	/// Payload USERNAME_AVAILABLE_RESPONSE (Master → Client).
	/// Format : [ uint8 available ][ uint32 seq ]
	struct UsernameAvailableResponsePayload
	{
		uint8_t  available = 0; ///< 1 = pseudo disponible, 0 = déjà pris ou invalide.
		uint32_t seq = 0;       ///< Numéro de séquence repris de la requête (stale-detection côté client).
	};

	/// Désérialise un payload USERNAME_AVAILABLE_RESPONSE.
	std::optional<UsernameAvailableResponsePayload> ParseUsernameAvailableResponsePayload(const uint8_t* payload, size_t payloadSize);

	/// Construit le paquet USERNAME_AVAILABLE_RESPONSE.
	std::vector<uint8_t> BuildUsernameAvailableResponsePacket(uint8_t available, uint32_t seq, uint32_t requestId, uint64_t sessionIdHeader);
}
