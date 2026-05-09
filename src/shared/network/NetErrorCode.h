// NetErrorCode.h — Codes d'erreur centralisés du protocole réseau V1.
// Transmis dans le payload d'un paquet kOpcodeError (opcode 8) sous forme de uint32.
// Référence normative : tickets/docs/protocol_v1.md (section « Error codes »).
// Règle de stabilité : NE PAS réaffecter une valeur numérique existante.
//   Toute suppression ou modification d'une valeur nécessite une montée de version du protocole.
// Thread-safety : enum pur, aucune contrainte.

#pragma once

#include <cstdint>

namespace engine::network
{
	/// Codes d'erreur V1 stables, encodés sur 32 bits non signés dans le payload ERROR.
	/// Les plages de valeurs délimitent les domaines fonctionnels — respecter ces plages lors des ajouts.
	/// Règle de réaction côté client : voir tickets/docs/protocol_v1.md (« ERROR vs disconnect »).
	enum class NetErrorCode : uint32_t
	{
		/// Aucune erreur. Utilisé en interne ; ne devrait pas apparaître dans un paquet ERROR.
		OK = 0,

		// -------------------------------------------------------------------------
		// Erreurs de protocole / cadrage (97–99)
		// Généralement fatales : la connexion est fermée après envoi.
		// -------------------------------------------------------------------------

		PACKET_OVERSIZE  = 97,  ///< Paquet annoncé supérieur à kProtocolV1MaxPacketSize (16 Ko). Connexion fermée.
		UNKNOWN_OPCODE   = 98,  ///< Opcode non reconnu par le serveur pour cet état de session.
		INVALID_PACKET   = 99,  ///< Paquet mal formé : champ manquant, longueur incohérente, etc.

		// -------------------------------------------------------------------------
		// Erreurs de requête / authentification (100–104)
		// -------------------------------------------------------------------------

		BAD_REQUEST         = 100, ///< Requête sémantiquement invalide (champ vide interdit, type inattendu…).
		INVALID_CREDENTIALS = 101, ///< Login ou mot de passe incorrect.
		ACCOUNT_LOCKED      = 102, ///< Compte verrouillé suite à des tentatives répétées ou action administrative.
		ACCOUNT_NOT_FOUND   = 103, ///< Aucun compte correspondant au login fourni.
		ALREADY_LOGGED_IN   = 104, ///< Une session active existe déjà pour ce compte (connexion simultanée refusée).

		// -------------------------------------------------------------------------
		// Erreurs d'inscription (200–205)
		// -------------------------------------------------------------------------

		REGISTRATION_DISABLED = 200, ///< Les inscriptions sont désactivées sur ce serveur (maintenance, accès fermé…).
		REGISTRATION_INVALID  = 201, ///< Données d'inscription invalides (cohérence globale du payload non satisfaite).
		LOGIN_ALREADY_TAKEN   = 202, ///< Le login demandé est déjà utilisé par un autre compte.
		INVALID_EMAIL         = 203, ///< Format d'email invalide ou domaine blacklisté.
		WEAK_PASSWORD         = 204, ///< Le client_hash ou le mot de passe ne satisfait pas la politique de sécurité.
		INVALID_LOGIN         = 205, ///< Format de login invalide (longueur, caractères autorisés…).

		// -------------------------------------------------------------------------
		// Erreurs de réinitialisation de mot de passe et vérification d'email (210–213)
		// -------------------------------------------------------------------------

		TOKEN_INVALID             = 210, ///< Token de réinitialisation inconnu ou mal formé.
		TOKEN_EXPIRED             = 211, ///< Token de réinitialisation valide mais expiré (TTL dépassé).
		EMAIL_ALREADY_VERIFIED    = 212, ///< Le compte est déjà vérifié ; nouvelle vérification inutile.
		VERIFICATION_CODE_INVALID = 213, ///< Code à 6 chiffres incorrect ou expiré.

		// -------------------------------------------------------------------------
		// Erreurs CGU / Conditions Générales d'Utilisation (220–223)
		// Note : 222 est délibérément absent (réservé).
		// -------------------------------------------------------------------------

		TERMS_ACCEPTANCE_REQUIRED   = 220, ///< L'utilisateur doit accepter les CGU avant de continuer.
		TERMS_EDITION_NOT_FOUND     = 221, ///< L'édition de CGU référencée dans la requête n'existe pas.
		EMAIL_VERIFICATION_REQUIRED = 223, ///< L'email du compte doit être vérifié avant d'effectuer cette action.

		// -------------------------------------------------------------------------
		// Erreurs de liste des serveurs (300)
		// -------------------------------------------------------------------------

		SERVER_LIST_UNAVAILABLE = 300, ///< Le Master ne peut pas fournir la liste des shards (base indisponible, etc.).

		// -------------------------------------------------------------------------
		// Erreurs internes / timeout (500–501)
		// -------------------------------------------------------------------------

		INTERNAL_ERROR = 500, ///< Erreur interne du serveur non spécifique (log côté serveur obligatoire).
		TIMEOUT        = 501, ///< Délai d'attente dépassé (heartbeat manquant, opération trop longue).
	};
}
