// ProtocolV1Constants.h — Constantes numériques du protocole réseau V1 du MMORPG.
// Contient : tailles d'en-tête/paquet, longueur max des chaînes, et tous les opcodes.
// Référence normative : tickets/docs/protocol_v1.md.
// Règle d'évolution : ne jamais réaffecter une valeur existante ; incrémenter la version du protocole.
// Thread-safety : constantes pures, aucune contrainte.

#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::network
{
	// -------------------------------------------------------------------------
	// Paramètres de cadrage du protocole V1
	// -------------------------------------------------------------------------

	/// Taille fixe de l'en-tête de paquet V1 en octets (18 = 2 size + 2 opcode + 2 flags + 4 requestId + 8 sessionId).
	/// Tout paquet doit avoir au moins ce nombre d'octets pour être valide.
	constexpr uint16_t kProtocolV1HeaderSize = 18u;

	/// Taille maximale d'un paquet complet (en-tête + payload) en octets.
	/// Valeur : 16 384 octets (16 Ko). Tout paquet dépassant cette limite est rejeté (PACKET_OVERSIZE).
	/// Alignée sur la capacité du bucket « large » du NetworkBufferPool.
	constexpr uint32_t kProtocolV1MaxPacketSize = 16384u;

	/// Longueur maximale d'une chaîne encodée dans un paquet V1, en octets UTF-8.
	/// Le champ longueur est un uint16, mais on limite à 8 192 pour éviter les abus mémoire.
	constexpr uint32_t kProtocolV1MaxStringLength = 8192u;

	// -------------------------------------------------------------------------
	// Opcodes d'authentification et d'enregistrement (valeurs 1–4)
	// Référence : tickets/docs/protocol_v1.md — section Auth/Register.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeAuthRequest      = 1u; ///< Client→Master : demande d'authentification (login + client_hash).
	constexpr uint16_t kOpcodeAuthResponse     = 2u; ///< Master→Client : réponse auth (session_id ou code erreur).
	constexpr uint16_t kOpcodeRegisterRequest  = 3u; ///< Client→Master : création de compte (login, email, client_hash).
	constexpr uint16_t kOpcodeRegisterResponse = 4u; ///< Master→Client : résultat de la création de compte.

	// -------------------------------------------------------------------------
	// Opcodes système (valeurs 7–8)
	// -------------------------------------------------------------------------

	/// Client→Master ou Client↔Shard : keep-alive périodique.
	/// Payload minimal ; session_id obligatoire dans l'en-tête après authentification.
	constexpr uint16_t kOpcodeHeartbeat = 7u;

	/// Master/Shard→Client : paquet d'erreur générique.
	/// Payload : uint32 NetErrorCode. Voir NetErrorCode.h pour les valeurs et les règles de déconnexion.
	constexpr uint16_t kOpcodeError = 8u;

	// -------------------------------------------------------------------------
	// Opcodes internes Shard↔Master (valeurs 10–13)
	// Référence : M22.2 — Enregistrement des shards auprès du Master.
	// Ces opcodes ne transitent jamais vers les clients.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeShardRegister      = 10u; ///< Shard→Master : demande d'enregistrement du shard (id, adresse, capacité).
	constexpr uint16_t kOpcodeShardRegisterOk    = 11u; ///< Master→Shard : enregistrement accepté.
	constexpr uint16_t kOpcodeShardRegisterError = 12u; ///< Master→Shard : enregistrement refusé (conflit d'id, version incompatible…).
	constexpr uint16_t kOpcodeShardHeartbeat     = 13u; ///< Shard→Master : keep-alive interne (charge, nb connexions).

	// -------------------------------------------------------------------------
	// Opcodes de ticket Shard (valeurs 14–18)
	// Référence : M22.4 — Mécanisme de ticket à usage unique pour rejoindre un Shard.
	// Flux : Client demande ticket au Master → Master émet ticket → Client présente ticket au Shard.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeRequestShardTicket  = 14u; ///< Client→Master : demande un ticket pour un shard donné.
	constexpr uint16_t kOpcodeShardTicketResponse = 15u; ///< Master→Client : ticket signé (UUID + TTL) ou erreur.
	constexpr uint16_t kOpcodePresentShardTicket  = 16u; ///< Client→Shard : présentation du ticket obtenu auprès du Master.
	constexpr uint16_t kOpcodeShardTicketAccepted = 17u; ///< Shard→Client : ticket valide, connexion acceptée.
	constexpr uint16_t kOpcodeShardTicketRejected = 18u; ///< Shard→Client : ticket invalide ou expiré, connexion refusée.

	// -------------------------------------------------------------------------
	// Opcodes de liste des serveurs (valeurs 19–20)
	// Référence : M22.5 — Liste des shards disponibles exposée aux clients.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeServerListRequest  = 19u; ///< Client→Master : demande la liste des shards disponibles (session requise).
	constexpr uint16_t kOpcodeServerListResponse = 20u; ///< Master→Client : tableau des shards (nom, statut, nb joueurs).

	// -------------------------------------------------------------------------
	// Opcodes de réinitialisation de mot de passe (valeurs 21–24)
	// Référence : M33.2 — Flux de réinitialisation par email.
	// -------------------------------------------------------------------------

	/// Client→Master : demande un lien de réinitialisation pour une adresse email.
	/// La réponse est toujours un succès (même si l'email est inconnu) afin d'éviter l'énumération de comptes.
	constexpr uint16_t kOpcodeForgotPasswordRequest  = 21u;
	constexpr uint16_t kOpcodeForgotPasswordResponse = 22u; ///< Master→Client : accusé de réception (toujours OK côté client).

	/// Client→Master : soumission du token de réinitialisation et du nouveau client_hash.
	constexpr uint16_t kOpcodeResetPasswordRequest  = 23u;
	constexpr uint16_t kOpcodeResetPasswordResponse = 24u; ///< Master→Client : succès ou code erreur (TOKEN_INVALID, TOKEN_EXPIRED…).

	// -------------------------------------------------------------------------
	// Opcodes de vérification d'email (valeurs 25–26)
	// Référence : M33.2 — Validation du compte par code à 6 chiffres.
	// -------------------------------------------------------------------------

	/// Client→Master : soumet l'account_id et le code à 6 chiffres reçu par email.
	constexpr uint16_t kOpcodeVerifyEmailRequest  = 25u;
	constexpr uint16_t kOpcodeVerifyEmailResponse = 26u; ///< Master→Client : succès ou VERIFICATION_CODE_INVALID.

	// -------------------------------------------------------------------------
	// Opcodes CGU / Conditions Générales d'Utilisation (valeurs 27–32)
	// Session requise pour accéder au contenu et enregistrer l'acceptation.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeTermsStatusRequest   = 27u; ///< Client→Master : l'utilisateur a-t-il déjà accepté la version actuelle des CGU ?
	constexpr uint16_t kOpcodeTermsStatusResponse  = 28u; ///< Master→Client : statut (accepté / non accepté) + édition courante.
	constexpr uint16_t kOpcodeTermsContentRequest  = 29u; ///< Client→Master : demande le texte complet des CGU (édition courante).
	constexpr uint16_t kOpcodeTermsContentResponse = 30u; ///< Master→Client : texte UTF-8 des CGU (peut être long, proche de kProtocolV1MaxStringLength).
	constexpr uint16_t kOpcodeTermsAcceptRequest   = 31u; ///< Client→Master : l'utilisateur accepte les CGU de l'édition indiquée.
	constexpr uint16_t kOpcodeTermsAcceptResponse  = 32u; ///< Master→Client : acceptation enregistrée, ou TERMS_EDITION_NOT_FOUND.

	// -------------------------------------------------------------------------
	// Opcodes de création de personnage (valeurs 33–34)
	// Session requise sur le Master.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeCharacterCreateRequest  = 33u; ///< Client→Master : demande de création d'un personnage (nom, race, classe…).
	constexpr uint16_t kOpcodeCharacterCreateResponse = 34u; ///< Master→Client : personnage créé (character_id) ou erreur.

	// -------------------------------------------------------------------------
	// Opcodes de disponibilité de pseudo (valeurs 35–36)
	// Peut être utilisé sans session (non authentifié).
	// -------------------------------------------------------------------------

	/// Client→Master : vérifie si un identifiant de connexion (login) est déjà utilisé.
	/// Accessible sans authentification pour éviter les inscriptions inutiles.
	constexpr uint16_t kOpcodeUsernameAvailableRequest  = 35u;
	constexpr uint16_t kOpcodeUsernameAvailableResponse = 36u; ///< Master→Client : disponible (true/false) ou LOGIN_ALREADY_TAKEN.

	// -------------------------------------------------------------------------
	// Opcodes de renvoi du code de vérification (valeurs 37–38)
	// Référence : M33.2-bis — Renvoyer le code à 6 chiffres si l'email n'a pas été reçu.
	// -------------------------------------------------------------------------

	/// Client→Master : demande l'envoi d'un nouveau code à 6 chiffres pour un compte en attente de vérification.
	/// La réponse est toujours un succès côté client si le rate-limit n'est pas atteint.
	constexpr uint16_t kOpcodeResendVerificationRequest  = 37u;
	constexpr uint16_t kOpcodeResendVerificationResponse = 38u; ///< Master→Client : succès ou INTERNAL_ERROR (rate-limit silencieux côté protocole).

	// -------------------------------------------------------------------------
	// Opcodes de liste des personnages (valeurs 39–40)
	// Référence : Phase 1 du flux post-auth — le client demande la liste de ses
	// personnages sur le shard sélectionné pour décider entre CharacterSelect
	// (≥1 perso) et CharacterCreate (0 perso). Session requise sur le Master.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeCharacterListRequest  = 39u; ///< Client→Master : demande la liste des personnages du compte sur un server_id donné.
	constexpr uint16_t kOpcodeCharacterListResponse = 40u; ///< Master→Client : tableau des personnages (id, slot, nom, race, classe, niveau, last_seen).

	// -------------------------------------------------------------------------
	// Opcodes de suppression de personnage (valeurs 41–42)
	// Référence : Phase 3.9 — soft-delete (positionne `characters.deleted_at`).
	// Session requise sur le Master ; vérifie que le perso appartient au compte.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeCharacterDeleteRequest  = 41u; ///< Client→Master : demande la suppression (logique) d'un personnage par character_id.
	constexpr uint16_t kOpcodeCharacterDeleteResponse = 42u; ///< Master→Client : succès / erreur de la suppression.

	// -------------------------------------------------------------------------
	// Opcodes de sauvegarde de position (valeurs 43–44)
	// Référence : Phase 3.6.5 — le client pousse périodiquement la position courante
	// du personnage actif au master pour persistance dans characters.spawn_*.
	// Session requise sur le Master ; vérifie que le perso appartient au compte.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeCharacterSavePositionRequest  = 43u; ///< Client→Master : sauvegarde la position courante (x, y, z, yaw_deg, pitch_deg) d'un personnage.
	constexpr uint16_t kOpcodeCharacterSavePositionResponse = 44u; ///< Master→Client : succès / erreur (NOT_FOUND si perso pas possédé par le compte).

	// -------------------------------------------------------------------------
	// Opcodes de chat (valeurs 45–46)
	// Référence : MVP chat réseau — le client envoie un message texte au master
	// (CHAT_SEND_REQUEST), le master broadcast à toutes les sessions actives
	// via CHAT_RELAY (push asynchrone, request_id=0).
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeChatSendRequest = 45u; ///< Client→Master : envoie un message chat (channel + texte).
	constexpr uint16_t kOpcodeChatRelay       = 46u; ///< Master→Client : push d'un message à afficher (timestamp + channel + sender + texte).
}
