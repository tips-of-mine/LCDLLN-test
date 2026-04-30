#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::network
{
	/// Chat MVP — Client → Master : envoi d'un message chat.
	/// Wire format :
	///   uint8 channel       (cf. engine::net::ChatChannel raw value 0..9)
	///   string text         (length-prefixed UTF-8, max kMaxChatTextBytes)
	struct ChatSendRequestPayload
	{
		uint8_t     channel = 0;
		std::string text;
	};

	/// Chat MVP — Master → Client : push d'un message à afficher.
	/// Wire format :
	///   uint64 timestamp_unix_ms   (UTC, stamp serveur)
	///   uint8  channel             (echo du channel envoyé par l'expéditeur)
	///   string sender              (login ou character display name)
	///   string text                (UTF-8)
	struct ChatRelayPayload
	{
		uint64_t    timestampUnixMs = 0;
		uint8_t     channel         = 0;
		std::string sender;
		std::string text;
	};

	/// Plafond serveur sur la taille du texte (UTF-8 bytes). Aligné sur kMaxInputUtf8Bytes
	/// côté client (\ref engine::client::ChatUiPresenter) pour qu'un message saisi à la
	/// limite passe sans troncation. Au-delà, master renvoie un ChatRelay "Server" notice.
	inline constexpr size_t kMaxChatTextBytes = 256u;

	std::optional<ChatSendRequestPayload> ParseChatSendRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildChatSendRequestPayload(uint8_t channel, std::string_view text);

	std::optional<ChatRelayPayload> ParseChatRelayPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildChatRelayPacket(uint64_t timestampUnixMs, uint8_t channel,
	                                          std::string_view sender, std::string_view text,
	                                          uint64_t sessionIdHeader);
}
