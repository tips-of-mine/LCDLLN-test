#pragma once
// CMANGOS.18 (Phase 3.18 step 3) — Wire payloads pour les opcodes Mail (49–58).
//
// Cinq paires Request/Response :
//   - Send (49/50)
//   - ListInbox (51/52)
//   - Read (53/54)
//   - TakeAttachments (55/56)
//   - Delete (57/58)
//
// Format wire : ByteReader/ByteWriter little-endian, strings length-prefixed UTF-8
// (uint16 length puis octets bruts), vectors préfixés par un uint16 count via
// WriteArrayCount/ReadArrayCount (cf. ProtocolV1).
//
// Cette PR ne câble pas d'items attachés sur le wire — la MVP du step 3 se
// limite à gold + COD + texte. Les items pourront être ajoutés en step 3.b
// (ré-utilisation des helpers Inventory déjà spécifiés dans le ticket).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// MAIL_SEND — Client → Master : envoi d'un mail.
	// =========================================================================

	/// Code d'erreur retourné dans \ref MailSendResponsePayload.
	/// 0 = OK, mailId valide. Sinon mailId = 0.
	enum class MailSendErrorCode : uint8_t
	{
		Ok                  = 0,
		RecipientNotFound   = 1, ///< accountId destinataire inconnu côté master.
		SubjectTooLong      = 2, ///< > kMaxMailSubjectBytes.
		BodyTooLong         = 3, ///< > kMaxMailBodyBytes.
		InsufficientGold    = 4, ///< wallet sender insuffisant pour copperGold + frais.
		AttachmentsTooMany  = 5,
		Unauthorized        = 6, ///< pas de session valide.
	};

	/// Wire format :
	///   uint64 recipientAccountId
	///   string subject
	///   string body
	///   uint64 copperGold
	///   uint64 copperCod
	struct MailSendRequestPayload
	{
		uint64_t    recipientAccountId = 0; ///< Le client a déjà résolu le login → account_id.
		std::string subject;
		std::string body;
		uint64_t    copperGold = 0;
		uint64_t    copperCod  = 0;
	};

	/// Wire format :
	///   uint8  error (cf. MailSendErrorCode)
	///   uint64 mailId  (0 si error != Ok)
	struct MailSendResponsePayload
	{
		uint8_t  error  = 0;
		uint64_t mailId = 0;
	};

	// =========================================================================
	// MAIL_LIST_INBOX — Client → Master : liste les mails de l'account courant.
	// =========================================================================

	/// Wire format : (vide). L'account est dérivé de la session côté master,
	/// jamais transmis par le client (anti-énumération).
	struct MailListInboxRequestPayload
	{
		// (vide)
	};

	/// Une entrée résumée de la boîte de réception (sans body).
	/// Le body complet n'est servi que sur MAIL_READ (économise la bande passante).
	struct MailInboxEntry
	{
		uint64_t    mailId           = 0;
		uint64_t    senderAccountId  = 0;
		std::string subject;
		uint64_t    sentTsMs         = 0;
		uint64_t    expiresTsMs      = 0;
		uint8_t     state            = 0; ///< 0=Unread, 1=Read, 2=Returned, 3=Deleted (cf. MailState).
		uint64_t    copperGold       = 0;
		uint64_t    copperCod        = 0;
	};

	/// Wire format :
	///   uint8  error (0=Ok, 6=Unauthorized)
	///   uint16 count
	///   <count> entries
	struct MailListInboxResponsePayload
	{
		uint8_t                     error = 0;
		std::vector<MailInboxEntry> mails;
	};

	// =========================================================================
	// MAIL_READ — Client → Master : marque comme lu et récupère le body.
	// =========================================================================

	enum class MailReadErrorCode : uint8_t
	{
		Ok            = 0,
		NotFound      = 1,
		WrongReceiver = 2,
		Unauthorized  = 6,
	};

	/// Wire format : uint64 mailId (8 octets).
	struct MailReadRequestPayload
	{
		uint64_t mailId = 0;
	};

	/// Wire format :
	///   uint8  error
	///   uint64 mailId
	///   string body  (vide si error != Ok)
	struct MailReadResponsePayload
	{
		uint8_t     error  = 0;
		uint64_t    mailId = 0;
		std::string body;
	};

	// =========================================================================
	// MAIL_TAKE_ATTACHMENTS — Client → Master : retire gold (et COD si requis).
	// =========================================================================

	enum class MailTakeErrorCode : uint8_t
	{
		Ok            = 0,
		NotFound      = 1,
		WrongReceiver = 2,
		AlreadyTaken  = 3, ///< Gold déjà retiré.
		CodNotPaid    = 4, ///< paidCopperCod < copperCod du mail.
		Unauthorized  = 6,
	};

	/// Wire format :
	///   uint64 mailId
	///   uint64 paidCopperCod  (gold que le client confirme avoir prélevé pour le COD)
	struct MailTakeAttachmentsRequestPayload
	{
		uint64_t mailId        = 0;
		uint64_t paidCopperCod = 0;
	};

	/// Wire format :
	///   uint8  error
	///   uint64 mailId
	///   uint64 copperGoldTaken
	struct MailTakeAttachmentsResponsePayload
	{
		uint8_t  error            = 0;
		uint64_t mailId           = 0;
		uint64_t copperGoldTaken  = 0;
	};

	// =========================================================================
	// MAIL_DELETE — Client → Master : supprime un mail.
	// =========================================================================

	enum class MailDeleteErrorCode : uint8_t
	{
		Ok              = 0,
		NotFound        = 1,
		WrongReceiver   = 2,
		HasAttachments  = 3, ///< Items / gold encore présents (faire Take avant Delete).
		Unauthorized    = 6,
	};

	/// Wire format : uint64 mailId.
	struct MailDeleteRequestPayload
	{
		uint64_t mailId = 0;
	};

	/// Wire format :
	///   uint8  error
	///   uint64 mailId
	struct MailDeleteResponsePayload
	{
		uint8_t  error  = 0;
		uint64_t mailId = 0;
	};

	// -------------------------------------------------------------------------
	// Plafonds canoniques wire (alignés sur src/masterd/mail/Mail.h pour
	// kMaxMailSubjectBytes, mais le body wire est plafonné à
	// kProtocolV1MaxStringLength = 8192. Si le storage DB autorise plus, le
	// client recevra ce qui rentre dans la string protocol_v1 et tronquera
	// silencieusement. Pour cette MVP step 3 c'est suffisant.
	// -------------------------------------------------------------------------
	inline constexpr size_t kMaxMailSubjectBytes = 255u;
	inline constexpr size_t kMaxMailBodyBytes    = 8192u;

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	/// Parse le payload d'un MAIL_SEND_REQUEST.
	/// \return nullopt si le payload est tronqué ou malformé.
	std::optional<MailSendRequestPayload> ParseMailSendRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Parse le payload d'un MAIL_LIST_INBOX_REQUEST. Toujours OK (payload vide accepté).
	std::optional<MailListInboxRequestPayload> ParseMailListInboxRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Parse le payload d'un MAIL_READ_REQUEST. \return nullopt si payloadSize < 8.
	std::optional<MailReadRequestPayload> ParseMailReadRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Parse le payload d'un MAIL_TAKE_ATTACHMENTS_REQUEST. \return nullopt si payloadSize < 16.
	std::optional<MailTakeAttachmentsRequestPayload> ParseMailTakeAttachmentsRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Parse le payload d'un MAIL_DELETE_REQUEST. \return nullopt si payloadSize < 8.
	std::optional<MailDeleteRequestPayload> ParseMailDeleteRequestPayload(const uint8_t* payload, size_t payloadSize);

	/// Construit le payload (sans header) d'un MAIL_SEND_REQUEST.
	std::vector<uint8_t> BuildMailSendRequestPayload(uint64_t recipientAccountId,
	                                                 std::string_view subject,
	                                                 std::string_view body,
	                                                 uint64_t copperGold,
	                                                 uint64_t copperCod);

	/// Construit le payload (vide) d'un MAIL_LIST_INBOX_REQUEST.
	std::vector<uint8_t> BuildMailListInboxRequestPayload();

	/// Construit le payload d'un MAIL_READ_REQUEST.
	std::vector<uint8_t> BuildMailReadRequestPayload(uint64_t mailId);

	/// Construit le payload d'un MAIL_TAKE_ATTACHMENTS_REQUEST.
	std::vector<uint8_t> BuildMailTakeAttachmentsRequestPayload(uint64_t mailId, uint64_t paidCopperCod);

	/// Construit le payload d'un MAIL_DELETE_REQUEST.
	std::vector<uint8_t> BuildMailDeleteRequestPayload(uint64_t mailId);

	// -------------------------------------------------------------------------
	// Parse / Build — Responses
	// -------------------------------------------------------------------------

	std::optional<MailSendResponsePayload>             ParseMailSendResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::optional<MailListInboxResponsePayload>        ParseMailListInboxResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::optional<MailReadResponsePayload>             ParseMailReadResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::optional<MailTakeAttachmentsResponsePayload>  ParseMailTakeAttachmentsResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::optional<MailDeleteResponsePayload>           ParseMailDeleteResponsePayload(const uint8_t* payload, size_t payloadSize);

	/// Construit un paquet complet (header + payload) MAIL_SEND_RESPONSE prêt à envoyer.
	std::vector<uint8_t> BuildMailSendResponsePacket(uint8_t error, uint64_t mailId,
	                                                 uint32_t requestId, uint64_t sessionIdHeader);

	/// Construit un paquet complet MAIL_LIST_INBOX_RESPONSE.
	std::vector<uint8_t> BuildMailListInboxResponsePacket(uint8_t error, const std::vector<MailInboxEntry>& mails,
	                                                      uint32_t requestId, uint64_t sessionIdHeader);

	/// Construit un paquet complet MAIL_READ_RESPONSE.
	std::vector<uint8_t> BuildMailReadResponsePacket(uint8_t error, uint64_t mailId, std::string_view body,
	                                                 uint32_t requestId, uint64_t sessionIdHeader);

	/// Construit un paquet complet MAIL_TAKE_ATTACHMENTS_RESPONSE.
	std::vector<uint8_t> BuildMailTakeAttachmentsResponsePacket(uint8_t error, uint64_t mailId, uint64_t copperGoldTaken,
	                                                             uint32_t requestId, uint64_t sessionIdHeader);

	/// Construit un paquet complet MAIL_DELETE_RESPONSE.
	std::vector<uint8_t> BuildMailDeleteResponsePacket(uint8_t error, uint64_t mailId,
	                                                    uint32_t requestId, uint64_t sessionIdHeader);

	// -------------------------------------------------------------------------
	// Build payload-only (utile côté tests round-trip — symétrie avec les
	// Parse*Response qui lisent le payload nu, sans header).
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildMailSendResponsePayload(uint8_t error, uint64_t mailId);
	std::vector<uint8_t> BuildMailListInboxResponsePayload(uint8_t error, const std::vector<MailInboxEntry>& mails);
	std::vector<uint8_t> BuildMailReadResponsePayload(uint8_t error, uint64_t mailId, std::string_view body);
	std::vector<uint8_t> BuildMailTakeAttachmentsResponsePayload(uint8_t error, uint64_t mailId, uint64_t copperGoldTaken);
	std::vector<uint8_t> BuildMailDeleteResponsePayload(uint8_t error, uint64_t mailId);
}
