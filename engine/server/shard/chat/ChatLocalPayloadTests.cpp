// CMANGOS.01 (Phase 2.01c) — Tests ChatLocalPayload round-trip.

#include "engine/server/shard/chat/ChatLocalPayload.h"
#include "engine/core/Log.h"

#include <string>

namespace
{
	using engine::server::shard::chat::ChatLocalDecodeResult;
	using engine::server::shard::chat::ChatLocalPayload;
	using engine::server::shard::chat::DecodeChatLocal;
	using engine::server::shard::chat::EncodeChatLocal;
	using engine::server::shard::chat::kMaxChatLocalSenderNameBytes;
	using engine::server::shard::chat::kMaxChatLocalTextBytes;

	bool TestRoundTripBasic()
	{
		ChatLocalPayload p;
		p.senderGuid = 0xCAFEBABEDEADBEEFull;
		p.channel = 1;       // Yell
		p.posX = 100.0f;
		p.posY = 0.0f;
		p.posZ = -50.5f;
		p.senderName = "Hero";
		p.text = "Hello world! caractères accentués é ñ";

		const auto blob = EncodeChatLocal(p);
		ChatLocalPayload r;
		if (DecodeChatLocal(blob, r) != ChatLocalDecodeResult::OK) return false;
		if (r.senderGuid != p.senderGuid) return false;
		if (r.channel != 1) return false;
		if (r.posX != 100.0f || r.posZ != -50.5f) return false;
		if (r.senderName != "Hero") return false;
		if (r.text != p.text) return false;
		LOG_INFO(Core, "[ChatLocalPayloadTests] basic roundtrip OK");
		return true;
	}

	bool TestEmptyText()
	{
		ChatLocalPayload p;
		p.senderGuid = 1;
		p.senderName = "X";
		// Texte vide : valide.
		const auto blob = EncodeChatLocal(p);
		ChatLocalPayload r;
		if (DecodeChatLocal(blob, r) != ChatLocalDecodeResult::OK) return false;
		if (!r.text.empty()) return false;
		if (r.senderName != "X") return false;
		LOG_INFO(Core, "[ChatLocalPayloadTests] empty text OK");
		return true;
	}

	bool TestBufferTooSmall()
	{
		std::vector<uint8_t> tooShort(10, 0);
		ChatLocalPayload r;
		if (DecodeChatLocal(tooShort, r) != ChatLocalDecodeResult::BufferTooSmall)
			return false;
		LOG_INFO(Core, "[ChatLocalPayloadTests] buffer too small OK");
		return true;
	}

	bool TestSenderNameTooLong()
	{
		// Forge a la main : senderNameLen = 65 (> max 64).
		std::vector<uint8_t> blob;
		for (int i = 0; i < 21; ++i) blob.push_back(0);  // header (8+1+12)
		// senderNameLen = 65 little-endian.
		blob.push_back(65);
		blob.push_back(0);
		// Pas besoin du contenu — le decoder valide AVANT de lire.
		ChatLocalPayload r;
		if (DecodeChatLocal(blob, r) != ChatLocalDecodeResult::SenderNameTooLong)
			return false;
		LOG_INFO(Core, "[ChatLocalPayloadTests] sender name too long detected OK");
		return true;
	}

	bool TestCapsAtMax()
	{
		// Le encode cap a kMaxChatLocalTextBytes, donc on peut envoyer
		// 9999 chars et la sortie sera tronquee a kMaxChatLocalTextBytes.
		ChatLocalPayload p;
		p.senderGuid = 1;
		p.senderName = "Hero";
		p.text = std::string(kMaxChatLocalTextBytes + 100, 'A');

		const auto blob = EncodeChatLocal(p);
		ChatLocalPayload r;
		if (DecodeChatLocal(blob, r) != ChatLocalDecodeResult::OK) return false;
		if (r.text.size() != kMaxChatLocalTextBytes) return false;
		LOG_INFO(Core, "[ChatLocalPayloadTests] cap at max OK");
		return true;
	}

	bool TestLengthMismatch()
	{
		ChatLocalPayload p;
		p.senderGuid = 1;
		p.senderName = "X";
		p.text = "ok";
		auto blob = EncodeChatLocal(p);
		blob.push_back(0xFF);  // byte parasite
		ChatLocalPayload r;
		if (DecodeChatLocal(blob, r) != ChatLocalDecodeResult::LengthMismatch)
			return false;
		LOG_INFO(Core, "[ChatLocalPayloadTests] length mismatch detected OK");
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

	const bool ok = TestRoundTripBasic()
		&& TestEmptyText()
		&& TestBufferTooSmall()
		&& TestSenderNameTooLong()
		&& TestCapsAtMax()
		&& TestLengthMismatch();

	if (ok)
		LOG_INFO(Core, "[ChatLocalPayloadTests] ALL OK");
	else
		LOG_ERROR(Core, "[ChatLocalPayloadTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
