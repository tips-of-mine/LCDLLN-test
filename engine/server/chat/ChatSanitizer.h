#pragma once
// CMANGOS.01 (Phase 2.01a) — ChatSanitizer : validation pure de chaîne
// avant routage (UTF-8 safe truncation + hyperlinks whitelist + strip
// zero-width). Pure function, testable sans dépendance.

#include <cstddef>
#include <string>
#include <string_view>

namespace engine::server::chat
{
	/// Configuration du sanitizer.
	struct ChatSanitizerConfig
	{
		/// Limite max en octets (UTF-8 safe truncation à cette taille).
		size_t maxMessageBytes = 255;

		/// Strip des caractères invisibles (zero-width joiner U+200B..U+200F,
		/// U+FEFF, bidi U+202A..U+202E).
		bool stripZeroWidth = true;
	};

	/// Résultat du sanitize.
	struct ChatSanitizeResult
	{
		/// Texte sanitizé (vide si rejeté). Tronqué UTF-8 safe + zero-width
		/// strippés selon config.
		std::string text;

		/// True si le message est accepté (texte non vide + hyperlinks
		/// whitelistés + post-truncation non vide). False sinon.
		bool accepted = false;

		/// Raison du rejet (court tag pour log/audit). Vide si accepté.
		/// Valeurs : "empty", "hyperlink_blocked", "post_strip_empty".
		std::string rejectReason;
	};

	/// Sanitize un message chat.
	///
	/// Étapes (en série) :
	/// 1. Reject si vide.
	/// 2. UTF-8 safe truncation à `cfg.maxMessageBytes` (jamais couper au
	///    milieu d'un codepoint multi-byte).
	/// 3. Strip zero-width characters si `cfg.stripZeroWidth`.
	/// 4. Reject si hyperlinks `|H<type>:<args>|h<text>|h` avec `<type>`
	///    hors whitelist {item, quest, achievement, spell}. Hyperlinks
	///    valides passent tels quels.
	/// 5. Reject si post-strip le texte est vide.
	///
	/// Pure function : aucune dépendance externe, thread-safe par nature.
	ChatSanitizeResult Sanitize(std::string_view input, const ChatSanitizerConfig& cfg);
}
