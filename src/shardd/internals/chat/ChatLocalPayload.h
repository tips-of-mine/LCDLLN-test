#pragma once
// CMANGOS.01 (Phase 2.01c) — ChatLocalPayload : encodage / decodage
// pur du payload `ChatLocal` (proximity chat shard-side : say / yell /
// emote). Diffuse aux clients dans le rayon d'audibilite (cf. audit).
//
// **Pur** dans cette PR : pas encore d'integration ServerProtocol
// (pas de MessageKind alloue, pas de bump kProtocolVersion). C'est le
// builder en isolation testable. L'integration wire viendra dans une
// PR finale dediee.
//
// **Layout** binaire little-endian, sans padding :
//
//   uint64 senderGuid          // ObjectGuid de l'emetteur (cf. CMANGOS.02)
//   uint8  channel             // ChatChannel byte (Say/Yell/Emote)
//   float  posX, posY, posZ    // position de l'emetteur (pour culling client)
//   uint16 senderNameLen
//   senderNameLen bytes        // sender display name (UTF-8)
//   uint16 textLen
//   textLen bytes              // chat text (UTF-8, sanitized par CMANGOS.01 sub-PR 1)
//
// Le shard (ChatLocalRelay) decide de la liste de destinataires
// (proximity check via SpatialPartition). Pas de DB, pas de validation
// sender ici — sanitize/gate sont applique au master AVANT que le
// shard ne broadcaste.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::server::shard::chat
{
	/// Limite text en bytes (UTF-8). Aligne sur le sanitizer master
	/// (#486, default 255). Le decode rejette au-dela.
	inline constexpr uint16_t kMaxChatLocalTextBytes = 1024;

	/// Limite nom expediteur (cf. character_name max).
	inline constexpr uint16_t kMaxChatLocalSenderNameBytes = 64;

	struct ChatLocalPayload
	{
		uint64_t    senderGuid = 0;
		uint8_t     channel    = 0;     ///< Say=0, Yell=1, Emote=2 (cf. ChatChannel byte)
		float       posX = 0.0f, posY = 0.0f, posZ = 0.0f;
		std::string senderName;
		std::string text;
	};

	enum class ChatLocalDecodeResult : uint8_t
	{
		OK = 0,
		BufferTooSmall   = 1,
		SenderNameTooLong = 2,
		TextTooLong      = 3,
		LengthMismatch   = 4,
	};

	std::vector<uint8_t> EncodeChatLocal(const ChatLocalPayload& msg);
	ChatLocalDecodeResult DecodeChatLocal(std::span<const uint8_t> in,
		ChatLocalPayload& out);
}
