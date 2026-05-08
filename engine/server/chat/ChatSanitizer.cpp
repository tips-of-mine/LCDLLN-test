#include "engine/server/chat/ChatSanitizer.h"

#include <array>
#include <cstdint>

namespace engine::server::chat
{
	namespace
	{
		/// Whitelist des types d'hyperlink autorisés (cf. spec ticket
		/// CMANGOS.01 §Sanitizer Step 3). Démarrer strict — élargir uniquement
		/// après audit de chaque nouveau type.
		constexpr std::array<std::string_view, 4> kAllowedHyperlinkTypes = {
			"item", "quest", "achievement", "spell"
		};

		/// Trouve la dernière frontière de codepoint UTF-8 valide ≤ maxBytes.
		/// Garantit qu'on ne coupe jamais au milieu d'un codepoint multi-byte.
		///
		/// UTF-8 layout :
		///   0xxxxxxx → ASCII 1-byte
		///   10xxxxxx → continuation byte (jamais début de codepoint)
		///   110xxxxx → début 2-byte
		///   1110xxxx → début 3-byte
		///   11110xxx → début 4-byte
		size_t Utf8SafeTruncatePoint(std::string_view s, size_t maxBytes)
		{
			if (s.size() <= maxBytes)
				return s.size();
			// Reculer depuis maxBytes jusqu'à un byte qui n'est PAS continuation (10xxxxxx).
			size_t cut = maxBytes;
			while (cut > 0)
			{
				const auto byte = static_cast<uint8_t>(s[cut]);
				// Si on est sur un byte de début de codepoint (high bit 0 ou 110/1110/11110), on coupe ICI.
				// `0xxxxxxx` (ASCII) ou `11xxxxxx` (start of multi-byte) : OK pour couper avant.
				if ((byte & 0xC0) != 0x80)
					return cut;
				--cut;
			}
			return 0;
		}

		/// Vérifie si codepoint est un caractère "invisible" zero-width / bidi.
		/// Liste basée sur la spec ticket : U+200B..U+200F, U+FEFF, U+202A..U+202E.
		bool IsZeroWidthCodepoint(uint32_t cp) noexcept
		{
			if (cp >= 0x200B && cp <= 0x200F) return true;
			if (cp == 0xFEFF) return true;
			if (cp >= 0x202A && cp <= 0x202E) return true;
			return false;
		}

		/// Décode un codepoint UTF-8 à la position `pos`. Retourne le codepoint
		/// + le nombre de bytes consommés. Sur erreur (séquence invalide),
		/// retourne (cp=byte, consumed=1) — comportement gracieux : on ne
		/// reject pas le message pour une séquence invalide, on traite le
		/// byte comme ASCII.
		struct Utf8Decoded { uint32_t cp; size_t consumed; };
		Utf8Decoded DecodeUtf8(std::string_view s, size_t pos) noexcept
		{
			if (pos >= s.size())
				return {0, 0};
			const auto b0 = static_cast<uint8_t>(s[pos]);
			if (b0 < 0x80) return {b0, 1};
			if ((b0 & 0xE0) == 0xC0 && pos + 1 < s.size())
			{
				const auto b1 = static_cast<uint8_t>(s[pos + 1]);
				if ((b1 & 0xC0) == 0x80)
					return {static_cast<uint32_t>((b0 & 0x1F) << 6 | (b1 & 0x3F)), 2};
			}
			if ((b0 & 0xF0) == 0xE0 && pos + 2 < s.size())
			{
				const auto b1 = static_cast<uint8_t>(s[pos + 1]);
				const auto b2 = static_cast<uint8_t>(s[pos + 2]);
				if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80)
					return {static_cast<uint32_t>((b0 & 0x0F) << 12 | (b1 & 0x3F) << 6 | (b2 & 0x3F)), 3};
			}
			if ((b0 & 0xF8) == 0xF0 && pos + 3 < s.size())
			{
				const auto b1 = static_cast<uint8_t>(s[pos + 1]);
				const auto b2 = static_cast<uint8_t>(s[pos + 2]);
				const auto b3 = static_cast<uint8_t>(s[pos + 3]);
				if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80)
					return {static_cast<uint32_t>((b0 & 0x07) << 18 | (b1 & 0x3F) << 12 | (b2 & 0x3F) << 6 | (b3 & 0x3F)), 4};
			}
			// Séquence invalide : traiter byte comme ASCII (graceful fallback).
			return {b0, 1};
		}

		/// Strip les zero-width characters (Step 3). Re-encode en UTF-8 sans
		/// les codepoints filtrés.
		std::string StripZeroWidth(std::string_view in)
		{
			std::string out;
			out.reserve(in.size());
			for (size_t i = 0; i < in.size(); )
			{
				const auto [cp, n] = DecodeUtf8(in, i);
				if (n == 0) break;
				if (!IsZeroWidthCodepoint(cp))
					out.append(in.substr(i, n));
				i += n;
			}
			return out;
		}

		/// Vérifie que tous les hyperlinks `|H<type>:...|h<text>|h` ont un
		/// `<type>` whitelisté. Retourne false si au moins un type interdit
		/// est présent. Si aucun hyperlink, retourne true.
		///
		/// Format reconnu : `|H<type>:<args>|h<text>|h`. Pas de validation
		/// stricte du contenu interne (args/text peuvent contenir n'importe
		/// quoi tant que le `<type>` est whitelisté).
		bool ValidateHyperlinks(std::string_view s) noexcept
		{
			size_t pos = 0;
			while ((pos = s.find("|H", pos)) != std::string_view::npos)
			{
				const size_t typeStart = pos + 2;
				const size_t colonPos = s.find(':', typeStart);
				if (colonPos == std::string_view::npos)
					return false;  // mal formé
				const std::string_view type = s.substr(typeStart, colonPos - typeStart);
				bool allowed = false;
				for (const auto& w : kAllowedHyperlinkTypes)
				{
					if (type == w) { allowed = true; break; }
				}
				if (!allowed)
					return false;
				pos = colonPos + 1;
			}
			return true;
		}
	}

	ChatSanitizeResult Sanitize(std::string_view input, const ChatSanitizerConfig& cfg)
	{
		ChatSanitizeResult result;

		// Step 1 : reject si vide.
		if (input.empty())
		{
			result.rejectReason = "empty";
			return result;
		}

		// Step 2 : UTF-8 safe truncation.
		const size_t cut = Utf8SafeTruncatePoint(input, cfg.maxMessageBytes);
		std::string truncated(input.substr(0, cut));

		// Step 3 : strip zero-width.
		std::string stripped = cfg.stripZeroWidth ? StripZeroWidth(truncated) : std::move(truncated);

		// Step 4 : hyperlinks whitelist.
		if (!ValidateHyperlinks(stripped))
		{
			result.rejectReason = "hyperlink_blocked";
			return result;
		}

		// Step 5 : reject si post-strip vide.
		if (stripped.empty())
		{
			result.rejectReason = "post_strip_empty";
			return result;
		}

		result.text = std::move(stripped);
		result.accepted = true;
		return result;
	}
}
