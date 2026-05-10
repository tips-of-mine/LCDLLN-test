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

	// -------------------------------------------------------------------------
	// Opcodes EnterWorld (valeurs 47–48)
	// Référence : Phase 4 chat — le client annonce au master quel personnage il
	// joue actuellement (post-EnterWorld). Le master enregistre le mapping
	// connId → (character_id, character_name) pour : (a) le sender display dans
	// CHAT_RELAY ; (b) la résolution de cible pour /whisper.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeCharacterEnterWorldRequest  = 47u; ///< Client→Master : déclare le personnage actif après EnterWorld.
	constexpr uint16_t kOpcodeCharacterEnterWorldResponse = 48u; ///< Master→Client : ACK ou erreur (NOT_OWNED, NAME_MISMATCH, …).

	// -------------------------------------------------------------------------
	// Opcodes Mail (valeurs 49–58)
	// Référence : Phase 3 CMANGOS.18 step 3. Wire client→master pour la
	// messagerie in-game. Le step 1 (MailManager + Mail.h) et le step 2
	// (MysqlMailStore + migration 0045) sont déjà mergés ; cette série
	// expose les opérations CRUD au client via 5 paires request/response.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeMailSendRequest             = 49u; ///< Client→Master : envoie un mail (subject, body, recipient, copperGold/Cod).
	constexpr uint16_t kOpcodeMailSendResponse            = 50u; ///< Master→Client : ACK avec mail_id ou erreur (RECIPIENT_NOT_FOUND, INSUFFICIENT_GOLD, …).
	constexpr uint16_t kOpcodeMailListInboxRequest        = 51u; ///< Client→Master : demande la liste des mails reçus du compte courant.
	constexpr uint16_t kOpcodeMailListInboxResponse       = 52u; ///< Master→Client : liste des mails (id, sender, subject, sent_ts, expires_ts, state).
	constexpr uint16_t kOpcodeMailReadRequest             = 53u; ///< Client→Master : demande le body complet d'un mail (marque comme lu).
	constexpr uint16_t kOpcodeMailReadResponse            = 54u; ///< Master→Client : body + items attachés.
	constexpr uint16_t kOpcodeMailTakeAttachmentsRequest  = 55u; ///< Client→Master : récupère les items + gold attachés à un mail.
	constexpr uint16_t kOpcodeMailTakeAttachmentsResponse = 56u; ///< Master→Client : ACK avec liste items pris + gold versé, ou erreur.
	constexpr uint16_t kOpcodeMailDeleteRequest           = 57u; ///< Client→Master : supprime un mail de l'inbox.
	constexpr uint16_t kOpcodeMailDeleteResponse          = 58u; ///< Master→Client : ACK ou erreur (NOT_FOUND, NOT_OWNER).

	// -------------------------------------------------------------------------
	// Opcodes Quest (valeurs 59–67)
	// Référence : Phase 5 CMANGOS.23 step 3+4. Wire client→master pour la
	// machine d'état Quest (None → Available → Accepted → Completed → Rewarded).
	// Le step 1 (QuestStateTracker header-only) et le step 2 (MysqlQuestStateStore
	// + migration 0048) sont déjà mergés. Cette série expose les opérations
	// au client via 4 paires request/response + 1 push (state update).
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeQuestAcceptRequest    = 59u; ///< Client→Master : accepte une quête (questId).
	constexpr uint16_t kOpcodeQuestAcceptResponse   = 60u; ///< Master→Client : OK + nouveau status, ou WrongStatus / Unauthorized.
	constexpr uint16_t kOpcodeQuestCompleteRequest  = 61u; ///< Client→Master : marque une quête Completed.
	constexpr uint16_t kOpcodeQuestCompleteResponse = 62u; ///< Master→Client : OK + nouveau status, ou WrongStatus.
	constexpr uint16_t kOpcodeQuestRewardRequest    = 63u; ///< Client→Master : récupère la récompense (Completed → Rewarded).
	constexpr uint16_t kOpcodeQuestRewardResponse   = 64u; ///< Master→Client : OK + nouveau status, ou NotImplementedYet (V1).
	constexpr uint16_t kOpcodeQuestListRequest      = 65u; ///< Client→Master : liste les quêtes connues du compte (vide, account dérivé de la session).
	constexpr uint16_t kOpcodeQuestListResponse     = 66u; ///< Master→Client : tableau {questId, status} ou Unauthorized.
	constexpr uint16_t kOpcodeQuestStateUpdate      = 67u; ///< Master→Client (push, request_id=0) : le serveur a changé l'état d'une quête (admin reset, etc.).

	// -------------------------------------------------------------------------
	// Opcodes IgnoreList (valeurs 68–73)
	// Référence : Phase 3 CMANGOS.25 step 3+4. Wire client→master pour la liste
	// d'ignore : un joueur peut silencieusement bloquer les whispers/chat
	// venant d'un autre account. Le step 1 (IgnoreListManager + IIgnoreStore)
	// et le step 2 (MysqlIgnoreStore + migration 0049) sont déjà mergés.
	// Cette série expose les opérations au client via 3 paires request/response :
	//   - Add (68/69)
	//   - Remove (70/71)
	//   - List (72/73)
	// La résolution se fait par account_id direct (V1) — la résolution par
	// character_name viendra avec PartySystem display ultérieurement.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeIgnoreAddRequest     = 68u; ///< Client→Master : ajoute un account_id à la liste d'ignore.
	constexpr uint16_t kOpcodeIgnoreAddResponse    = 69u; ///< Master→Client : OK ou AlreadyIgnored / ListFull / SelfIgnore.
	constexpr uint16_t kOpcodeIgnoreRemoveRequest  = 70u; ///< Client→Master : retire un account_id de la liste d'ignore.
	constexpr uint16_t kOpcodeIgnoreRemoveResponse = 71u; ///< Master→Client : OK ou NotIgnored.
	constexpr uint16_t kOpcodeIgnoreListRequest    = 72u; ///< Client→Master : demande la liste complète des account_id ignorés (vide).
	constexpr uint16_t kOpcodeIgnoreListResponse   = 73u; ///< Master→Client : tableau d'account_id ignorés ou Unauthorized.

		// -------------------------------------------------------------------------
		// Opcodes GmTickets (valeurs 76-82)
		// Reference : Phase 5 CMANGOS.32 step 3+4. Wire client->master pour le
		// support GM. Le step 1 (GmTicketSystem header-only) et le step 2
		// (MysqlGmTicketStore + migration 0046) sont deja merges. Cette serie
		// expose les operations cote joueur au client via 3 paires request/response
		// + 1 push notification :
		//   - Open   (76/77)               : le joueur ouvre un nouveau ticket support.
		//   - ListMine (78/79)             : le joueur liste ses propres tickets ouverts.
		//   - Cancel (80/81)               : le joueur annule son propre ticket.
		//   - ResolvedNotification (82, push) : le master notifie le joueur quand
		//     un GM a resolu son ticket.
		// La V1 ne couvre pas l'UI GM cote support (admin tools, viendra plus tard).
		// -------------------------------------------------------------------------

		constexpr uint16_t kOpcodeGmTicketOpenRequest          = 76u; ///< Client to Master : ouvre un nouveau ticket (body texte).
		constexpr uint16_t kOpcodeGmTicketOpenResponse         = 77u; ///< Master to Client : OK + ticketId, ou BodyEmpty / BodyTooLong.
		constexpr uint16_t kOpcodeGmTicketListMineRequest      = 78u; ///< Client to Master : liste les tickets ouverts par ce joueur (vide).
		constexpr uint16_t kOpcodeGmTicketListMineResponse     = 79u; ///< Master to Client : tableau {id, createdTsMs, state, resolvedTsMs} ou Unauthorized.
		constexpr uint16_t kOpcodeGmTicketCancelRequest        = 80u; ///< Client to Master : annule son propre ticket.
		constexpr uint16_t kOpcodeGmTicketCancelResponse       = 81u; ///< Master to Client : OK / NotFound / NotOwner / AlreadyResolved.
		constexpr uint16_t kOpcodeGmTicketResolvedNotification = 82u; ///< Master to Client (push, request_id=0) : un GM a resolu un ticket de ce joueur.

		// -------------------------------------------------------------------------
		// Opcodes Trade (valeurs 83-94)
		// Reference : Phase 4 CMANGOS.27 step 3+4. Wire client<->master pour le
		// systeme d'echange direct entre 2 joueurs (TradeSession FSM :
		// Open -> LockedA/B -> BothLocked -> Committed/Cancelled). Le step 1
		// (TradeSession header-only) est deja merge ; pas de step 2 DB
		// (sessions de trade transitoires, pas persistees).
		//
		// Architecture : le master gere un TradeSessionRegistry qui mappe
		// account_id -> TradeSession active. Toute transition d'etat (SetOffer,
		// Lock, Commit, Cancel) declenche une push notification a l'autre
		// participant pour synchroniser son UI.
		//
		// V1 limitations :
		//   - Resolution par account_id direct (pas encore par character_name).
		//   - Pas d'application reelle du delta inventory au Commit (TODO wallet
		//     integration). Le Commit valide juste la FSM.
		//
		// Decoupage opcode :
		//   - Begin       (83/84) + push BeginNotification (85)
		//   - SetOffer    (86/87)
		//   - Lock        (88/89)
		//   - StateUpdate push notification (90)  -> pousse aux 2 a chaque
		//     changement d'offer/lock pour mise a jour reciproque de l'UI.
		//   - Commit      (91/92)
		//   - Cancel      (93) + push CancelNotification (94)
		// -------------------------------------------------------------------------

		constexpr uint16_t kOpcodeTradeBeginRequest             = 83u; ///< Client to Master : initie une demande de trade vers targetAccountId.
		constexpr uint16_t kOpcodeTradeBeginResponse            = 84u; ///< Master to Client (initiateur) : ACK avec sessionId + partnerAccountId, ou erreur.
		constexpr uint16_t kOpcodeTradeBeginNotification        = 85u; ///< Master to Client (push, request_id=0) : envoye au target pour annoncer la trade entrante.
		constexpr uint16_t kOpcodeTradeSetOfferRequest          = 86u; ///< Client to Master : modifie l'offer (gold + items) cote sender. Refuse si la session est lockee cote sender.
		constexpr uint16_t kOpcodeTradeSetOfferResponse         = 87u; ///< Master to Client : ACK ou erreur (WrongState, NotPartOfSession, ...).
		constexpr uint16_t kOpcodeTradeLockRequest              = 88u; ///< Client to Master : verrouille l'offer cote sender.
		constexpr uint16_t kOpcodeTradeLockResponse             = 89u; ///< Master to Client : ACK avec newState (0=Open, 1=LockedA, 2=LockedB, 3=BothLocked, ...).
		constexpr uint16_t kOpcodeTradeStateUpdateNotification  = 90u; ///< Master to Client (push, request_id=0) : envoye au partenaire a chaque changement (offer/lock/commit/cancel).
		constexpr uint16_t kOpcodeTradeCommitRequest            = 91u; ///< Client to Master : finalise l'echange (n'est valide qu'en BothLocked).
		constexpr uint16_t kOpcodeTradeCommitResponse           = 92u; ///< Master to Client : ACK (OK) ou erreur (WrongState, ...).
		constexpr uint16_t kOpcodeTradeCancelRequest            = 93u; ///< Client to Master : annule la trade (depuis n'importe quel etat non-terminal).
		constexpr uint16_t kOpcodeTradeCancelNotification       = 94u; ///< Master to Client (push, request_id=0) : envoye aux 2 participants pour annoncer l'annulation.

	// -------------------------------------------------------------------------
	// Opcodes Reputation (valeurs 95-97)
	// Reference : Phase 3 CMANGOS.24 step 3+4. Wire client<->master pour la
	// reputation par account/faction. Le step 1 (ReputationManager header-only,
	// avec spillover bitmask) et le step 2 (MysqlReputationStore + migration
	// 0047) sont deja merges. La reputation est read-only cote client : le
	// serveur seul decide quand l'incrementer (quete reward, kill, etc.).
	//
	// Decoupage opcode :
	//   - List   (95/96)               : le client demande la liste de ses
	//     reputations (faction, value, standing).
	//   - UpdateNotification (97, push) : le serveur notifie le client d'un
	//     changement (suite a un game event). V1 ne pousse que la faction
	//     primaire — les valeurs spillover sont persistees mais pas push.
	//
	// V1 limitations :
	//   - Faction names hardcodes "Faction #N" cote client (FactionTemplate
	//     ticket viendra fournir le map id->name).
	//   - Pas de rate limiting sur les push (un par GainReputation direct).
	//   - 98 et 99 reserves pour usage futur.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeReputationListRequest        = 95u; ///< Client to Master : liste les reputations de l'account courant (vide).
	constexpr uint16_t kOpcodeReputationListResponse       = 96u; ///< Master to Client : tableau {factionId, value, standing} ou Unauthorized.
	constexpr uint16_t kOpcodeReputationUpdateNotification = 97u; ///< Master to Client (push, request_id=0) : changement d'une reputation (factionId, newValue, newStanding, delta).

	// -------------------------------------------------------------------------
	// Opcodes Lfg / LookForGroup (valeurs 100-107)
	// Reference : Phase 5 CMANGOS.33 step 3+4. Wire client<->master pour le
	// matchmaking dungeon (LookForGroup queue + match proposal). Le step 1
	// (LfgQueue header-only, queue par dungeon avec roles Tank/Healer/Damage)
	// est deja merge ; pas de step 2 DB (queue transient, perdue au reboot).
	//
	// Architecture : le master gere une LfgQueue transient. TickMatchmaking
	// (declenche manuellement en V1, periodique sub-PR future) appelle
	// TryMatch() sur chaque dungeon connu et envoie une push proposal a
	// chaque membre du groupe forme.
	//
	// V1 limitations :
	//   - Pas d'etat « proposal » cote master : MatchAccept est juste loggee
	//     et retourne Ok (le proposal lifecycle viendra dans une sub-PR).
	//   - estimatedWaitSec hardcode 60s (sera calcule statistiquement plus tard).
	//   - Tick matchmaking pas appele en boucle automatique : sera cable a
	//     un timer dans une sub-PR future (e.g. toutes les 5s).
	//
	// Decoupage opcode :
	//   - Queue       (100/101)               : le joueur s'inscrit dans la queue.
	//   - Leave       (102/103)               : le joueur quitte la queue.
	//   - Status      (104/105)               : interroge l'etat de la queue
	//     (en queue ? role/dungeon ? duree ecoulee ?).
	//   - MatchProposalNotification (106, push) : un groupe a ete forme ;
	//     liste des membres. Envoye a chaque membre.
	//   - MatchAccept (107)                   : le client accepte ou rejette
	//     le match propose. V1 : pas de response payload separee, juste
	//     log cote master.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeLfgQueueRequest               = 100u; ///< Client to Master : s'inscrit dans la queue dungeon (role + dungeonId).
	constexpr uint16_t kOpcodeLfgQueueResponse              = 101u; ///< Master to Client : OK + estimatedWaitSec, ou AlreadyQueued / InvalidRole / InvalidDungeon / Unauthorized.
	constexpr uint16_t kOpcodeLfgLeaveRequest               = 102u; ///< Client to Master : quitte la queue (vide ; account derive de la session).
	constexpr uint16_t kOpcodeLfgLeaveResponse              = 103u; ///< Master to Client : OK ou NotInQueue / Unauthorized.
	constexpr uint16_t kOpcodeLfgStatusRequest              = 104u; ///< Client to Master : interroge l'etat de queue (vide).
	constexpr uint16_t kOpcodeLfgStatusResponse             = 105u; ///< Master to Client : inQueue + role + dungeonId + elapsedSec, ou Unauthorized.
	constexpr uint16_t kOpcodeLfgMatchProposalNotification  = 106u; ///< Master to Client (push, request_id=0) : un groupe a ete forme (proposalId, dungeonId, members).
	constexpr uint16_t kOpcodeLfgMatchAcceptRequest         = 107u; ///< Client to Master : accepte ou rejette un match propose (proposalId + accept bool). V1 : log + ack.

	// -------------------------------------------------------------------------
	// Opcodes Cinematics (valeurs 108-112)
	// Reference : Phase 5 CMANGOS.30 step 3+4. Wire master->client (push) +
	// client->master (ack/skip) pour la lecture de cinematiques scriptees
	// (intro de zone, fin de quete, etc.). Le step 1 (CinematicSequence
	// header-only avec keyframes camera + sound + SampleAt linear interp)
	// est deja merge ; pas de step 2 DB (les sequences sont content-driven,
	// chargees depuis fichiers data).
	//
	// Architecture : la cinematique est entierement server-pushed. Le master
	// (ou le shard via le master) decide quand declencher une cinematique
	// pour un client donne (entree zone, fin de quete, intro). Le client
	// charge la sequence depuis ses fichiers data locaux (game/data/
	// cinematics/seq<id>.json) et execute la lecture (interpolation lineaire
	// de la camera + sound cues + input desactive). A la fin, le client
	// notifie le master par un ack request, et peut demander a skip via
	// le skip request si l'utilisateur appuie sur Esc.
	//
	// V1 limitations :
	//   - Skip toujours autorise (allowed=true). Future PR introduira un
	//     catalog "non-skippable sequences" cote master.
	//   - Pas de tracking server-side d'active cinematic (le master se
	//     contente de log et repondre Ok aux acks).
	//   - 113 reserve pour usage futur (e.g. CinematicPause si besoin).
	//
	// Decoupage opcode :
	//   - PlayNotification (108, push)    : master annonce une cinematic au client.
	//   - Ack             (109/110)       : client signale completion / cancellation.
	//   - Skip            (111/112)       : client demande a skip (permis V1).
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeCinematicPlayNotification = 108u; ///< Master to Client (push, request_id=0) : declenche la lecture d'une cinematique (sequenceId + reason).
	constexpr uint16_t kOpcodeCinematicAckRequest       = 109u; ///< Client to Master : signale la fin (ou interruption) de lecture (sequenceId + completionState).
	constexpr uint16_t kOpcodeCinematicAckResponse      = 110u; ///< Master to Client : ACK Ok ou erreur (UnknownSequence, NoActiveCinematic, ...).
	constexpr uint16_t kOpcodeCinematicSkipRequest      = 111u; ///< Client to Master : le user a appuye sur Esc et demande a skip la cinematique en cours.
	constexpr uint16_t kOpcodeCinematicSkipResponse     = 112u; ///< Master to Client : OK + allowed (true V1) ou SkipNotAllowed (cinematique obligatoire).

	// -------------------------------------------------------------------------
	// Opcodes Skills (valeurs 113-119)
	// Reference : Phase 4 CMANGOS.39 step 3+4. Wire client<->master pour le
	// SkillBook (cooking, herbalism, mining, lockpicking, weapon skills).
	// Le step 1 (SkillBook header-only avec Get/Set/Gain/Effective) et le
	// step 2 (tests) sont deja merges. Cette serie expose les operations
	// au client via 3 paires request/response + 1 push notification.
	//
	// Architecture : le master tient en memoire la skill book par account
	// (V1, starter set hardcode). Toute mutation (Learn / Use gain) declenche
	// une push SkillUpgradeNotification au client pour synchroniser son UI.
	//
	// V1 limitations :
	//   - Starter set hardcode cote master (Cooking=1, Herbalism=2, Mining=3,
	//     FirstAid=4, Lockpicking=5). Trainer real venant en CMANGOS.41.
	//   - Use random 70% success (V1) ; calibration economie a venir.
	//   - Pas encore de SyncSkill RPC entre master et shardd (master autoritaire V1).
	//   - Pas de Crafting hook -> SkillUpgrade (future PR avec CraftingSystem).
	//
	// Decoupage opcode :
	//   - List   (113/114)               : le client demande la liste de ses
	//     skills (value, cap, bonus).
	//   - Learn  (115/116)               : le client demande apprendre un skill
	//     (typiquement depuis un trainer).
	//   - Use    (117/118)               : le client utilise un skill
	//     non-combat (lockpicking, fishing).
	//   - UpgradeNotification (119, push) : le master notifie le client d'un
	//     gain (value+1) ou d'un changement de cap.
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeSkillsListRequest        = 113u; ///< Client to Master : demande la liste des skills (vide).
	constexpr uint16_t kOpcodeSkillsListResponse       = 114u; ///< Master to Client : liste {skillId, value, cap, bonus} ou Unauthorized.
	constexpr uint16_t kOpcodeSkillLearnRequest        = 115u; ///< Client to Master : demande apprendre un skill (skillId, par ex. cooking depuis trainer).
	constexpr uint16_t kOpcodeSkillLearnResponse       = 116u; ///< Master to Client : OK + cap initial, ou AlreadyLearned / UnknownSkill / Unauthorized.
	constexpr uint16_t kOpcodeSkillUseRequest          = 117u; ///< Client to Master : utilise un skill non-combat (lockpicking, fishing).
	constexpr uint16_t kOpcodeSkillUseResponse         = 118u; ///< Master to Client : OK + result + delta value, ou SkillNotLearned / SkillFailed / Unauthorized.
	constexpr uint16_t kOpcodeSkillUpgradeNotification = 119u; ///< Master to Client (push, request_id=0) : skill upgrade (skillId, newValue, newCap, delta).

	// -------------------------------------------------------------------------
	// Opcodes Arena (valeurs 120-129)
	// Reference : Phase 5 CMANGOS.21 step 3+4. Wire client<->master pour le
	// systeme d'arenes (queue 2v2/3v3/5v5, match proposal, result push avec
	// rating ELO). Le step 1+2 (ArenaTeam + ArenaTeamRegistry header-only avec
	// ApplyEloUpdate / RecordMatch / ResetWeekly) est deja merge.
	//
	// Architecture : le master tient en memoire un ArenaTeamRegistry (V1, seed
	// hardcode par account au premier acces : 3 teams 2v2/3v3/5v5 a rating 1500)
	// + une queue par account + un map de proposals actifs. Quand un account
	// rejoint la queue, le master cree immediatement un proposal contre une AI
	// fictive ("AI Team Alpha" rating 1500) et push une notification (V1).
	//
	// V1 limitations :
	//   - Seed teams hardcode par account (3 teams 2v2/3v3/5v5). Vraie creation
	//     via CMANGOS.41 (Trainers + ArenaTeam create UI).
	//   - Match contre AI Team Alpha fictif ; vrai pairing 2 accounts a venir.
	//   - Result win/loss random 50% (V1) ; vraie simulation match a venir.
	//   - Pas de SyncArena RPC entre master et shardd (master autoritaire V1).
	//
	// Decoupage opcode :
	//   - TeamList   (120/121)  : le client demande la liste de ses arena teams.
	//   - Queue      (122/123)  : inscription en queue (teamId + size 2/3/5).
	//   - LeaveQueue (124/125)  : quitte la queue.
	//   - MatchProposalNotification (126, push) : un match a ete forme
	//     (proposalId, opponentTeamName, opponentRating).
	//   - MatchAccept (127/128) : accepte ou rejette un match propose.
	//   - MatchResultNotification (129, push) : fin de match (win, oldRating,
	//     newRating, opponentName).
	// -------------------------------------------------------------------------

	constexpr uint16_t kOpcodeArenaTeamListRequest           = 120u; ///< Client to Master : liste des arena teams (vide).
	constexpr uint16_t kOpcodeArenaTeamListResponse          = 121u; ///< Master to Client : liste {teamId, size, name, rating, weeklyGames, weeklyWins} ou Unauthorized.
	constexpr uint16_t kOpcodeArenaQueueRequest              = 122u; ///< Client to Master : s'inscrit en queue arena (teamId + size 2/3/5).
	constexpr uint16_t kOpcodeArenaQueueResponse             = 123u; ///< Master to Client : OK + estimatedWaitSec, ou AlreadyQueued / TeamNotFound / InvalidSize / Unauthorized.
	constexpr uint16_t kOpcodeArenaLeaveQueueRequest         = 124u; ///< Client to Master : quitte la queue arena.
	constexpr uint16_t kOpcodeArenaLeaveQueueResponse        = 125u; ///< Master to Client : OK ou NotInQueue / Unauthorized.
	constexpr uint16_t kOpcodeArenaMatchProposalNotification = 126u; ///< Master to Client (push, request_id=0) : un match a ete forme (proposalId, opponentTeamName, opponentRating).
	constexpr uint16_t kOpcodeArenaMatchAcceptRequest        = 127u; ///< Client to Master : accepte (true) ou rejette (false) le match propose (proposalId + accept bool).
	constexpr uint16_t kOpcodeArenaMatchAcceptResponse       = 128u; ///< Master to Client : ACK Ok ou ProposalExpired / UnknownProposal / Unauthorized.
	constexpr uint16_t kOpcodeArenaMatchResultNotification   = 129u; ///< Master to Client (push, request_id=0) : fin de match (win bool, oldRating, newRating, opponentName).

	// =====================================================================
	// CMANGOS.10 step 3+4 — BattleGround wire (BG list, queue par faction
	// Alliance/Horde, match start, score updates, match end). Master tient
	// en memoire un BattleGroundQueue + matches actifs (V1).
	//
	// Architecture : 3 BG hardcodes au boot (Warsong Gulch, Arathi Basin,
	// Alterac Valley). Le master tient en memoire la queue par account et
	// les matches actifs. V1 simplifie : a la queue, master cree
	// immediatement un match contre AI bot (faction opposee) et push la
	// sequence Start -> Score(s) -> End. Pas besoin de Tick().
	//
	// V1 limitations :
	//   - 3 BG hardcodes (Warsong/Arathi/Alterac). Vrais BG via M40+ futur.
	//   - Match vs AI bot fictif (V1) ; vrai matchmaking 2 factions a venir.
	//   - Score evolution simulee instantanee (V1) ; vrai gameplay BG via shardd futur.
	//   - Pas de SyncBg RPC entre master et shardd (master autoritaire V1).
	//
	// Decoupage opcode :
	//   - List       (130/131)               : liste des BG disponibles.
	//   - Queue      (132/133)               : inscription en queue (bgType + faction 0/1).
	//   - LeaveQueue (134/135)               : quitte la queue.
	//   - MatchStartNotification (136, push) : match commence (matchId, mapName, counts).
	//   - ScoreUpdateNotification (137, push): scores changes (matchId, scores, elapsed).
	//   - MatchEndNotification (138, push)   : fin de match (winnerFaction, scores, duration).
	//   - LeaveMatch (139)                   : forfait V1 (push fire-and-forget, pas de Response paire).
	// =====================================================================
	constexpr uint16_t kOpcodeBgListRequest                = 130u; ///< Client to Master : liste des BG disponibles (vide).
	constexpr uint16_t kOpcodeBgListResponse               = 131u; ///< Master to Client : liste {bgType, name, teamSize, mapName} ou Unauthorized.
	constexpr uint16_t kOpcodeBgQueueRequest               = 132u; ///< Client to Master : s'inscrit en queue BG (bgType + faction Alliance=0/Horde=1).
	constexpr uint16_t kOpcodeBgQueueResponse              = 133u; ///< Master to Client : OK + estimatedWaitSec + queuePosition, ou AlreadyQueued / UnknownBg / InvalidFaction / Unauthorized.
	constexpr uint16_t kOpcodeBgLeaveQueueRequest          = 134u; ///< Client to Master : quitte la queue BG (vide).
	constexpr uint16_t kOpcodeBgLeaveQueueResponse         = 135u; ///< Master to Client : OK ou NotInQueue / Unauthorized.
	constexpr uint16_t kOpcodeBgMatchStartNotification     = 136u; ///< Master to Client (push, request_id=0) : match commence (bgType, mapName, allianceCount, hordeCount, matchId).
	constexpr uint16_t kOpcodeBgScoreUpdateNotification    = 137u; ///< Master to Client (push, request_id=0) : score changes (matchId, allianceScore, hordeScore, elapsedSec).
	constexpr uint16_t kOpcodeBgMatchEndNotification       = 138u; ///< Master to Client (push, request_id=0) : fin de match (matchId, winnerFaction 0/1/2 = Alliance/Horde/Draw, allianceScore, hordeScore, durationSec).
	constexpr uint16_t kOpcodeBgLeaveMatchRequest          = 139u; ///< Client to Master : quitte le match en cours (forfait V1) ou vide.

	// =====================================================================
	// CMANGOS.36 step 3+4 — OutdoorPvP wire (zones contestees, objectifs
	// capturables par faction, score par zone, push capture progress + complete).
	// Master tient en memoire un OutdoorPvPManager (V1 seed hardcode).
	//
	// Architecture : 2 zones contestees au boot (Hellfire Peninsula 3
	// objectifs, Eastern Plaguelands 4 objectifs). Le master tient en
	// memoire les subscriptions par account et l'etat des objectifs.
	// V1 simplifie : a chaque CaptureStartRequest, master simule la
	// capture instantanee : push 4 progress (25/50/75/100) puis call
	// TickCapture(100) qui transitionne l'owner et incremente le score,
	// puis push CaptureCompletedNotification.
	//
	// V1 limitations :
	//   - 2 zones hardcodees (Hellfire, Eastern Plaguelands). Vraies
	//     zones via M40+ futur.
	//   - Capture simulee instantanement (V1) ; vrai gameplay capture
	//     via shardd futur.
	//   - Subscriptions in-memory (pas de persistance V1).
	//   - Pas de SyncOutdoorPvp RPC entre master et shardd (master
	//     autoritaire V1).
	//
	// Decoupage opcode :
	//   - ZoneList    (140/141)               : liste des zones + objectifs + scores.
	//   - Subscribe   (142/143)               : s'abonne aux push d'une zone.
	//   - Unsubscribe (144/145)               : se desabonne.
	//   - CaptureStart (146/147)              : lance la capture d'un objectif.
	//   - CaptureProgressNotification (148, push)  : progression capture.
	//   - CaptureCompletedNotification (149, push) : fin capture (owner change + scores).
	// =====================================================================
	constexpr uint16_t kOpcodeOutdoorPvpZoneListRequest              = 140u; ///< Client to Master : liste des zones contestees (vide).
	constexpr uint16_t kOpcodeOutdoorPvpZoneListResponse             = 141u; ///< Master to Client : liste {zoneId, name, allianceScore, hordeScore, objectives[]} ou Unauthorized.
	constexpr uint16_t kOpcodeOutdoorPvpSubscribeRequest             = 142u; ///< Client to Master : s'abonne aux push d'une zone (zoneId).
	constexpr uint16_t kOpcodeOutdoorPvpSubscribeResponse            = 143u; ///< Master to Client : OK ou UnknownZone / Unauthorized.
	constexpr uint16_t kOpcodeOutdoorPvpUnsubscribeRequest           = 144u; ///< Client to Master : se desabonne (zoneId).
	constexpr uint16_t kOpcodeOutdoorPvpUnsubscribeResponse          = 145u; ///< Master to Client : OK ou NotSubscribed / Unauthorized.
	constexpr uint16_t kOpcodeOutdoorPvpCaptureStartRequest          = 146u; ///< Client to Master : commence la capture d'un objectif (zoneId, objectiveId, faction).
	constexpr uint16_t kOpcodeOutdoorPvpCaptureStartResponse         = 147u; ///< Master to Client : OK ou UnknownObjective / InvalidFaction / Unauthorized.
	constexpr uint16_t kOpcodeOutdoorPvpCaptureProgressNotification  = 148u; ///< Master to Client (push, request_id=0) : progression capture (zoneId, objectiveId, capturePct, capturingBy).
	constexpr uint16_t kOpcodeOutdoorPvpCaptureCompletedNotification = 149u; ///< Master to Client (push, request_id=0) : capture finie (zoneId, objectiveId, newOwner, allianceScore, hordeScore).

	// =====================================================================
	// CMANGOS.42 step 3+4 — Weather wire (zone subscribe, push current state
	// + push state change). Master tient en memoire un WeatherManager (V1
	// seed hardcode, tick simule a la demande).
	//
	// Architecture : 3 zones meteo seedees au boot (Stormwind Plains avec
	// pClear=0.6/pRain=0.3/pStorm=0.1, Frozen Tundra avec
	// pClear=0.4/pSnow=0.5/pFog=0.1, Tanaris Desert avec
	// pClear=0.5/pSandstorm=0.4/pFog=0.1). Le master tient en memoire la
	// liste des subscribers par zone et l'etat courant. V1 simplifie : a
	// chaque SubscribeRequest, master force un reroll (Tick) ; si le state
	// d'une zone change, broadcast WeatherUpdateNotification a tous les
	// subscribers de la zone.
	//
	// V1 limitations :
	//   - 3 zones meteo hardcodees (Stormwind Plains, Frozen Tundra,
	//     Tanaris Desert). Vraies zones via M40+ futur.
	//   - Tick simule a chaque SubscribeRequest (force reroll). Vrai tick
	//     periodique via shardd futur.
	//   - Subscriptions in-memory (pas de persistance V1).
	//   - Pas de SyncWeather RPC entre master et shardd (master autoritaire V1).
	//
	// Decoupage opcode :
	//   - List       (150/151)               : liste des zones meteo + state.
	//   - Subscribe  (152/153)               : s'abonne aux push d'une zone.
	//   - Unsubscribe (154/155)              : se desabonne.
	//   - UpdateNotification (156, push)     : changement meteo (zoneId, kind, intensity).
	//
	// Wire format kind : uint8 (Clear=0, Rain=1, Snow=2, Storm=3,
	// Sandstorm=4, Fog=5). Wire format intensity : float32 LE (0..1).
	// =====================================================================
	constexpr uint16_t kOpcodeWeatherListRequest         = 150u; ///< Client to Master : liste des zones meteo (vide).
	constexpr uint16_t kOpcodeWeatherListResponse        = 151u; ///< Master to Client : liste {zoneId, name, kind, intensity} ou Unauthorized.
	constexpr uint16_t kOpcodeWeatherSubscribeRequest    = 152u; ///< Client to Master : s'abonne au push d'une zone (zoneId).
	constexpr uint16_t kOpcodeWeatherSubscribeResponse   = 153u; ///< Master to Client : OK + current {kind, intensity}, ou UnknownZone / Unauthorized.
	constexpr uint16_t kOpcodeWeatherUnsubscribeRequest  = 154u; ///< Client to Master : se desabonne (zoneId).
	constexpr uint16_t kOpcodeWeatherUnsubscribeResponse = 155u; ///< Master to Client : OK ou NotSubscribed / Unauthorized.
	constexpr uint16_t kOpcodeWeatherUpdateNotification  = 156u; ///< Master to Client (push, request_id=0) : changement meteo (zoneId, kind, intensity).

	// =====================================================================
	// CMANGOS.31 step 3+4 — GameEvents wire (events saisonniers, list,
	// subscribe global, push state change). Master tient en memoire un
	// GameEventManager (V1 seed 4 events : Halloween, Winter Veil, Lunar
	// Festival, Midsummer Fire Festival).
	//
	// Architecture : 4 events seedees au boot avec timestamps absolus
	// (ms depuis epoch) + duration + recur period. Le master tient en
	// memoire la liste des subscribers globaux (abonnement non par event
	// mais general aux changements). V1 simplifie : a chaque
	// SubscribeRequest, master envoie un snapshot complet des events au
	// nouvel abonne via une serie de StateChangeNotification (un par event
	// dont l'etat differe du dernier broadcast). Pas de tick periodique
	// V1.
	//
	// V1 limitations :
	//   - 4 events hardcodes (Halloween, Winter Veil, Lunar Festival,
	//     Midsummer Fire). Future PR : DB seed via MysqlGameEventStore.
	//   - Subscribe = snapshot one-shot pour le nouvel abonne (pas de
	//     broadcast cross-subscribers V1). Vrai broadcast quand tick
	//     periodique sera branche.
	//   - Subscriptions in-memory (perdues au reboot).
	//   - Pas de SyncGameEvents RPC entre master et shardd (master
	//     autoritaire V1).
	//
	// Decoupage opcode :
	//   - List       (157/158)               : liste des events + state.
	//   - Subscribe  (159/160)               : abonnement global aux push.
	//   - Unsubscribe (161/162)              : retire l'abonnement global.
	//   - StateChangeNotification (163, push) : changement etat event
	//                                           (eventId, newState, untilTsMs).
	//
	// Wire format state : uint8 (Inactive=0, Active=1). untilTsMs : uint64
	// LE (timestamp absolu ms epoch, prochaine bascule ; 0 = pas de
	// recurrence et event termine definitivement).
	// =====================================================================
	constexpr uint16_t kOpcodeGameEventListRequest             = 157u; ///< Client to Master : liste des events saisonniers (vide).
	constexpr uint16_t kOpcodeGameEventListResponse            = 158u; ///< Master to Client : liste {eventId, name, state, startTsMs, durationMs, recurMs} ou Unauthorized.
	constexpr uint16_t kOpcodeGameEventSubscribeRequest        = 159u; ///< Client to Master : s'abonne aux push (vide, abonnement global).
	constexpr uint16_t kOpcodeGameEventSubscribeResponse       = 160u; ///< Master to Client : OK ou AlreadySubscribed / Unauthorized.
	constexpr uint16_t kOpcodeGameEventUnsubscribeRequest      = 161u; ///< Client to Master : se desabonne (vide).
	constexpr uint16_t kOpcodeGameEventUnsubscribeResponse     = 162u; ///< Master to Client : OK ou NotSubscribed / Unauthorized.
	constexpr uint16_t kOpcodeGameEventStateChangeNotification = 163u; ///< Master to Client (push, request_id=0) : changement etat event (eventId, newState, untilTsMs).

	// =====================================================================
	// CMANGOS.21 step 3+4 — Guilds wire (guild list, members, bank, permissions
	// par rang). Master tient en memoire un registry guildes (V1 seed hardcode
	// 2 guildes + GuildPermissionMatrix WoW defaults).
	//
	// Architecture : 2 guildes seedees au boot (Les Gardiens + L'Ombre) avec
	// leurs membres, bank tab 0 et permissions par rang. Le master tient en
	// memoire un std::vector<InMemoryGuild> et la matrice de permissions
	// (cf. GuildPermissionMatrix). V1 simplifie : pas de filtrage par account
	// membership (n'importe quel client logged-in peut consulter n'importe
	// quelle guilde) ; vraie ACL via la matrice viendra plus tard.
	//
	// V1 limitations :
	//   - 2 guildes hardcodees (Les Gardiens, L'Ombre). Future PR : DB seed
	//     via MysqlGuildStore + creation in-game via /guild create.
	//   - Pas de filtrage par account membership (lecture seule globale V1).
	//   - Bank tab 0 only (V1). Future PR : multi-onglets.
	//   - Pas de modification client (Invite/Remove/Promote/Demote V1 read-only).
	//   - Pas de SyncGuilds RPC entre master et shardd (master autoritaire V1).
	//
	// Decoupage opcode :
	//   - List         (164/165) : liste des guildes (summary).
	//   - Members      (166/167) : liste des membres d'une guilde.
	//   - Permissions  (168/169) : matrice mask par rang.
	//   - Bank         (170/171) : contenu de la bank tab 0.
	//   - MotdUpdate   (172, push, request_id=0) : changement de MOTD.
	// =====================================================================
	constexpr uint16_t kOpcodeGuildListRequest            = 164u; ///< Client to Master : liste des guildes (vide).
	constexpr uint16_t kOpcodeGuildListResponse           = 165u; ///< Master to Client : liste {guildId, name, motd, memberCount, leaderName} ou Unauthorized.
	constexpr uint16_t kOpcodeGuildMembersRequest         = 166u; ///< Client to Master : liste des membres d'une guilde (guildId).
	constexpr uint16_t kOpcodeGuildMembersResponse        = 167u; ///< Master to Client : liste {accountName, rankId, rankName, online} ou UnknownGuild / Unauthorized.
	constexpr uint16_t kOpcodeGuildPermissionsRequest     = 168u; ///< Client to Master : matrice permissions (guildId).
	constexpr uint16_t kOpcodeGuildPermissionsResponse    = 169u; ///< Master to Client : liste {rankId, rankName, mask} ou UnknownGuild / Unauthorized.
	constexpr uint16_t kOpcodeGuildBankRequest            = 170u; ///< Client to Master : contenu bank d'une guilde (guildId, tabIndex).
	constexpr uint16_t kOpcodeGuildBankResponse           = 171u; ///< Master to Client : liste {slotIndex, itemName, count} ou UnknownGuild / NoPermission / Unauthorized.
	constexpr uint16_t kOpcodeGuildMotdUpdateNotification = 172u; ///< Master to Client (push, request_id=0) : changement MOTD (guildId, newMotd).
}
