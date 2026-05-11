// CMANGOS.01 (Phase 2.01a) — Tests ChatSanitizer.
// Pure : aucune dépendance DB / pool / réseau.

#include "src/masterd/chat/ChatSanitizer.h"
#include "src/shared/core/Log.h"

#include <string>

namespace
{
	using engine::server::chat::ChatSanitizerConfig;
	using engine::server::chat::ChatSanitizeResult;
	using engine::server::chat::Sanitize;

	bool ExpectAccepted(const std::string& label, const std::string_view input,
		const std::string_view expectedText, const ChatSanitizerConfig& cfg)
	{
		const auto r = Sanitize(input, cfg);
		if (!r.accepted)
		{
			LOG_ERROR(Core, "[ChatSanitizerTests] {}: expected accepted, got rejected ({})",
				label, r.rejectReason);
			return false;
		}
		if (r.text != expectedText)
		{
			LOG_ERROR(Core, "[ChatSanitizerTests] {}: text mismatch (got '{}' expected '{}')",
				label, r.text, std::string(expectedText));
			return false;
		}
		return true;
	}

	bool ExpectRejected(const std::string& label, const std::string_view input,
		const std::string_view expectedReason, const ChatSanitizerConfig& cfg)
	{
		const auto r = Sanitize(input, cfg);
		if (r.accepted)
		{
			LOG_ERROR(Core, "[ChatSanitizerTests] {}: expected rejected, got accepted", label);
			return false;
		}
		if (r.rejectReason != expectedReason)
		{
			LOG_ERROR(Core, "[ChatSanitizerTests] {}: reject reason '{}' expected '{}'",
				label, r.rejectReason, std::string(expectedReason));
			return false;
		}
		return true;
	}

	bool TestEmpty()
	{
		ChatSanitizerConfig cfg;
		if (!ExpectRejected("empty", "", "empty", cfg)) return false;
		LOG_INFO(Core, "[ChatSanitizerTests] empty OK");
		return true;
	}

	bool TestSimpleAscii()
	{
		ChatSanitizerConfig cfg;
		if (!ExpectAccepted("ascii basic", "hello world", "hello world", cfg)) return false;
		LOG_INFO(Core, "[ChatSanitizerTests] simple ascii OK");
		return true;
	}

	bool TestUtf8SafeTruncation()
	{
		// "héllo" en UTF-8 : h=0x68, é=0xC3 0xA9, l=0x6C, l=0x6C, o=0x6F → 6 bytes.
		// maxBytes=4 → on doit couper APRÈS 'h' + 'é' (3 bytes) ou avant 'é' (1 byte),
		// jamais entre 0xC3 et 0xA9. Le code recule depuis maxBytes=4 jusqu'à un byte
		// indexes : 0=h 1=0xC3 2=0xA9 3=l 4=l 5=o (6 bytes : "é" = 2 bytes UTF-8).
		// maxBytes=4 → cut=4 → s[4]='l'=0x6C (non-continuation) → coupe à 4
		// → substr(0,4) = [0x68, 0xC3, 0xA9, 0x6C] = "h\xC3\xA9l" = "hél"
		// (3 chars visibles, 4 bytes). L'ancien commentaire prétendait "héll
		// (4 bytes)" mais c'est faux : "héll" UTF-8 = 5 bytes (h=1 + é=2 +
		// l=1 + l=1). L'implementation respecte strictement maxMessageBytes.
		ChatSanitizerConfig cfg;
		cfg.maxMessageBytes = 4;
		if (!ExpectAccepted("utf8 safe trunc 4", "h\xC3\xA9llo", "h\xC3\xA9l", cfg))
			return false;

		// maxBytes=2 → cut=2, s[2]=0xA9 (continuation 10xxxxxx) → recule à 1, s[1]=0xC3
		// (start 110xxxxx, NON-continuation) → coupe à 1 → "h".
		cfg.maxMessageBytes = 2;
		if (!ExpectAccepted("utf8 safe trunc 2", "h\xC3\xA9llo", "h", cfg))
			return false;

		// maxBytes=3 → cut=3, s[3]='l' (non-continuation) → coupe à 3 → "hé".
		cfg.maxMessageBytes = 3;
		if (!ExpectAccepted("utf8 safe trunc 3", "h\xC3\xA9llo", "h\xC3\xA9", cfg))
			return false;

		LOG_INFO(Core, "[ChatSanitizerTests] utf8 safe truncation OK");
		return true;
	}

	bool TestStripZeroWidth()
	{
		// U+200B (zero-width space) en UTF-8 : 0xE2 0x80 0x8B.
		// "ab" + ZWSP + "cd" → "abcd" attendu.
		ChatSanitizerConfig cfg;
		const std::string in = std::string("ab") + "\xE2\x80\x8B" + "cd";
		if (!ExpectAccepted("strip ZWSP", in, "abcd", cfg)) return false;

		// U+FEFF (BOM) : 0xEF 0xBB 0xBF.
		const std::string in2 = std::string("hi") + "\xEF\xBB\xBF" + "there";
		if (!ExpectAccepted("strip BOM", in2, "hithere", cfg)) return false;

		// U+202E (RTL override) : 0xE2 0x80 0xAE.
		const std::string in3 = std::string("safe") + "\xE2\x80\xAE" + "evil";
		if (!ExpectAccepted("strip bidi", in3, "safeevil", cfg)) return false;

		// stripZeroWidth = false → on garde les ZW.
		cfg.stripZeroWidth = false;
		if (!ExpectAccepted("keep ZWSP when disabled", in, in, cfg)) return false;

		LOG_INFO(Core, "[ChatSanitizerTests] strip zero-width OK");
		return true;
	}

	bool TestPostStripEmpty()
	{
		// Message qui n'est QUE des zero-width chars → après strip = "" → reject.
		ChatSanitizerConfig cfg;
		const std::string in = std::string("\xE2\x80\x8B") + "\xEF\xBB\xBF";
		if (!ExpectRejected("post strip empty", in, "post_strip_empty", cfg))
			return false;
		LOG_INFO(Core, "[ChatSanitizerTests] post-strip empty OK");
		return true;
	}

	bool TestHyperlinkAllowed()
	{
		ChatSanitizerConfig cfg;
		const std::string in = "Look at |Hitem:12345:0:0:0:0:0:0:0|h[Sword]|h !";
		if (!ExpectAccepted("hyperlink item", in, in, cfg)) return false;

		const std::string in2 = "Quest: |Hquest:99|h[Wanted]|h";
		if (!ExpectAccepted("hyperlink quest", in2, in2, cfg)) return false;

		const std::string in3 = "|Hachievement:1234|h[Hero]|h";
		if (!ExpectAccepted("hyperlink achievement", in3, in3, cfg)) return false;

		const std::string in4 = "|Hspell:42|h[Magic]|h";
		if (!ExpectAccepted("hyperlink spell", in4, in4, cfg)) return false;

		LOG_INFO(Core, "[ChatSanitizerTests] hyperlink allowed OK");
		return true;
	}

	bool TestHyperlinkBlocked()
	{
		ChatSanitizerConfig cfg;
		// "trade" n'est PAS dans la whitelist.
		const std::string in = "Cheat: |Htrade:foo|h[click]|h";
		if (!ExpectRejected("hyperlink trade blocked", in, "hyperlink_blocked", cfg))
			return false;

		// Hyperlink mal formé (pas de ':' après |H) → rejet.
		const std::string in2 = "broken |H no colon |h";
		if (!ExpectRejected("hyperlink malformed", in2, "hyperlink_blocked", cfg))
			return false;

		// Type vide entre |H et : → ne match aucun whitelisted → reject.
		const std::string in3 = "empty |H:foo|h[x]|h";
		if (!ExpectRejected("hyperlink empty type", in3, "hyperlink_blocked", cfg))
			return false;

		LOG_INFO(Core, "[ChatSanitizerTests] hyperlink blocked OK");
		return true;
	}

	bool TestNoHyperlinkPasses()
	{
		ChatSanitizerConfig cfg;
		// Message normal sans hyperlink → accepté.
		if (!ExpectAccepted("no hyperlink", "no link here", "no link here", cfg))
			return false;
		LOG_INFO(Core, "[ChatSanitizerTests] no hyperlink passes OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestEmpty()
		&& TestSimpleAscii()
		&& TestUtf8SafeTruncation()
		&& TestStripZeroWidth()
		&& TestPostStripEmpty()
		&& TestHyperlinkAllowed()
		&& TestHyperlinkBlocked()
		&& TestNoHyperlinkPasses();

	if (ok)
		LOG_INFO(Core, "[ChatSanitizerTests] ALL OK");
	else
		LOG_ERROR(Core, "[ChatSanitizerTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
