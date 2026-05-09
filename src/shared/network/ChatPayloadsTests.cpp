/**
 * Chat MVP — Round-trip tests for CHAT_SEND_REQUEST / CHAT_RELAY payloads.
 * Pure encoding tests — no DB / no network. Returns 0 on success, non-zero on first failure.
 */

#include "engine/network/ChatPayloads.h"
#include "engine/network/PacketView.h"
#include "engine/network/ProtocolV1Constants.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	static int s_failCount = 0;
	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

using namespace engine::network;

static void TestSendRequestRoundTrip()
{
	auto buf = BuildChatSendRequestPayload(6u, "", "bonjour les amis");
	Assert(!buf.empty(), "Build send empty check");
	auto parsed = ParseChatSendRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Parse send OK");
	if (parsed)
	{
		Assert(parsed->channel == 6u, "channel round-trips");
		Assert(parsed->targetToken.empty(), "targetToken empty for non-whisper");
		Assert(parsed->text == "bonjour les amis", "text round-trips");
	}
}

static void TestSendRequestWhisperRoundTrip()
{
	auto buf = BuildChatSendRequestPayload(2u, "Alyx", "tu es la ?");
	auto parsed = ParseChatSendRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->channel == 2u, "whisper channel round-trips");
	Assert(parsed && parsed->targetToken == "Alyx", "whisper targetToken round-trips");
	Assert(parsed && parsed->text == "tu es la ?", "whisper text round-trips");
}

static void TestSendRequestRejectsShort()
{
	auto parsed = ParseChatSendRequestPayload(nullptr, 1u);
	Assert(!parsed.has_value(), "Parse send rejects nullptr");
	// MSVC refuse les tableaux de taille 0 (extension gcc) : on passe un buffer 1 octet
	// avec size=0 pour tester le rejet "size < 1".
	uint8_t emptyOneByte[1]{};
	auto parsed2 = ParseChatSendRequestPayload(emptyOneByte, 0u);
	Assert(!parsed2.has_value(), "Parse send rejects size=0");
}

static void TestSendRequestUtf8()
{
	// Wire format byte-pour-byte : on encode des octets >= 0x80 directement (\xc3\xa9 = 'é'
	// en UTF-8). Pas de literal UTF-8 dans la source pour rester portable MSVC sans /utf-8.
	const std::string utf8Sample = "\xc3\xa9\xc3\xa9 multibyte";
	auto buf = BuildChatSendRequestPayload(2u, "", utf8Sample);
	auto parsed = ParseChatSendRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->text == utf8Sample, "UTF-8 multibyte round-trips byte-perfect");
}

static void TestRelayRoundTrip()
{
	auto pkt = BuildChatRelayPacket(1730000000123ull, 7u, "system", "server restart in 5 min", 0xC0FFEEFFull);
	Assert(!pkt.empty(), "Build relay packet not empty");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	Assert(view.Opcode() == kOpcodeChatRelay, "Opcode is CHAT_RELAY");
	Assert(view.RequestId() == 0u, "RequestId is 0 (push)");
	Assert(view.SessionId() == 0xC0FFEEFFull, "SessionId preserved");
	auto parsed = ParseChatRelayPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value(), "Parse relay OK");
	if (parsed)
	{
		Assert(parsed->timestampUnixMs == 1730000000123ull, "ts round-trips");
		Assert(parsed->channel == 7u, "channel round-trips");
		Assert(parsed->sender == "system", "sender round-trips");
		Assert(parsed->text == "server restart in 5 min", "text round-trips");
	}
}

static void TestRelayRejectsShort()
{
	uint8_t shortBuf[8]{};
	auto parsed = ParseChatRelayPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed.has_value(), "Parse relay rejects payload < 9 bytes");
	auto parsed2 = ParseChatRelayPayload(nullptr, 32u);
	Assert(!parsed2.has_value(), "Parse relay rejects nullptr");
}

int main()
{
	TestSendRequestRoundTrip();
	TestSendRequestWhisperRoundTrip();
	TestSendRequestRejectsShort();
	TestSendRequestUtf8();
	TestRelayRoundTrip();
	TestRelayRejectsShort();
	std::cerr << (s_failCount == 0 ? "[OK] all chat payload tests passed\n" : "[FAIL] some tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
