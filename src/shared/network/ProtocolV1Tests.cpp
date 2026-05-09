/**
 * M19.2: Unit tests for protocol v1 serialization (ByteWriter, ByteReader, PacketBuilder, PacketView).
 * No external test framework; returns 0 if all pass, non-zero on first failure.
 */

#include "engine/network/ByteReader.h"
#include "engine/network/ByteWriter.h"
#include "engine/network/PacketBuilder.h"
#include "engine/network/PacketView.h"
#include "engine/network/ProtocolV1Constants.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

	template<typename T>
	void AssertEq(T a, T b, const char* msg)
	{
		if (a != b)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << " (expected " << static_cast<uint64_t>(b) << " got " << static_cast<uint64_t>(a) << ")" << std::endl;
		}
	}
}

using namespace engine::network;

static bool TestRoundtripHeader()
{
	uint8_t buf[64] = {};
	ByteWriter w(buf, sizeof(buf));
	bool ok = w.WriteU16(32) && w.WriteU16(1) && w.WriteU16(0) && w.WriteU32(1) && w.WriteU64(0);
	Assert(ok && w.Offset() == 18u, "roundtrip header write");
	if (!ok) return false;
	ByteReader r(buf, sizeof(buf));
	uint16_t size = 0, opcode = 0, flags = 0;
	uint32_t reqId = 0;
	uint64_t sessId = 0;
	ok = r.ReadU16(size) && r.ReadU16(opcode) && r.ReadU16(flags) && r.ReadU32(reqId) && r.ReadU64(sessId);
	Assert(ok, "roundtrip header read");
	AssertEq(size, uint16_t(32), "header size");
	AssertEq(opcode, uint16_t(1), "header opcode");
	AssertEq(reqId, uint32_t(1), "header request_id");
	return s_failCount == 0;
}

static bool TestStringRoundtrip()
{
	uint8_t buf[256] = {};
	ByteWriter w(buf, sizeof(buf));
	const std::string hello = "user";
	Assert(w.WriteString(hello), "write string");
	Assert(w.WriteString("secret"), "write string 2");
	size_t written = w.Offset();
	ByteReader r(buf, written);
	std::string out1, out2;
	Assert(r.ReadString(out1) && out1 == "user", "read string 1");
	Assert(r.ReadString(out2) && out2 == "secret", "read string 2");
	Assert(r.Remaining() == 0, "string roundtrip no remainder");
	return s_failCount == 0;
}

static bool TestArrayCountRoundtrip()
{
	uint8_t buf[32] = {};
	ByteWriter w(buf, sizeof(buf));
	Assert(w.WriteArrayCount(3), "write array count");
	ByteReader r(buf, 2);
	uint16_t count = 0;
	Assert(r.ReadArrayCount(count) && count == 3, "read array count");
	return s_failCount == 0;
}

static bool TestOversize()
{
	PacketBuilder builder;
	ByteWriter payload = builder.PayloadWriter();
	// Try to finalize with payload larger than 16KB - 18
	size_t tooBig = kProtocolV1MaxPacketSize - kProtocolV1HeaderSize + 1;
	Assert(!builder.Finalize(1, 0, 1, 0, tooBig), "finalize oversize must fail");
	// PacketView: size > 16KB -> Invalid
	uint8_t badHeader[18] = {};
	badHeader[0] = 0xFF;
	badHeader[1] = 0x7F; // 0x7FFF = 32767, or use 0x01 0x40 for 16385
	badHeader[2] = 0x01;
	badHeader[3] = 0x00;
	PacketView view;
	PacketParseResult res = PacketView::Parse(badHeader, 18, view);
	// size 0x7FFF = 32767 > 16384 -> Invalid
	Assert(res == PacketParseResult::Invalid, "parse size > 16KB returns Invalid");
	uint8_t size16385[2] = { 0x01, 0x40 }; // 16385 LE
	std::memcpy(badHeader, size16385, 2);
	res = PacketView::Parse(badHeader, 18, view);
	Assert(res == PacketParseResult::Invalid, "parse size 16385 returns Invalid");
	return s_failCount == 0;
}

static bool TestBufferTruncated()
{
	// Complete header says size=100, but we only have 20 bytes
	uint8_t buf[20] = {};
	buf[0] = 100;
	buf[1] = 0;
	buf[2] = 1; // opcode
	buf[3] = 0;
	PacketView view;
	PacketParseResult res = PacketView::Parse(buf, 20, view);
	Assert(res == PacketParseResult::Incomplete, "truncated buffer returns Incomplete");
	// Truncated so we don't even have full header
	res = PacketView::Parse(buf, 4, view);
	Assert(res == PacketParseResult::Incomplete, "short buffer returns Incomplete");
	return s_failCount == 0;
}

static bool TestRoundtripPacketBuilderView()
{
	PacketBuilder builder;
	ByteWriter payload = builder.PayloadWriter();
	Assert(payload.WriteString("user"), "builder payload write user");
	Assert(payload.WriteString("secret"), "builder payload write secret");
	size_t payloadLen = payload.Offset();
	Assert(builder.Finalize(1, 0, 1, 0, payloadLen), "builder finalize");
	Assert(builder.DataSize() == 18u + payloadLen, "builder size");
	const std::vector<uint8_t>& data = builder.Data();
	PacketView view;
	PacketParseResult res = PacketView::Parse(data.data(), data.size(), view);
	Assert(res == PacketParseResult::Ok, "parse built packet");
	AssertEq(view.Size(), static_cast<uint16_t>(data.size()), "view size");
	AssertEq(view.Opcode(), uint16_t(1), "view opcode");
	AssertEq(view.RequestId(), uint32_t(1), "view request_id");
	ByteReader payloadReader(view.Payload(), view.PayloadSize());
	std::string u, p;
	Assert(payloadReader.ReadString(u) && u == "user", "view payload read user");
	Assert(payloadReader.ReadString(p) && p == "secret", "view payload read secret");
	return s_failCount == 0;
}

static bool TestRandomBufferNoCrash()
{
	uint8_t random[256] = {};
	for (size_t i = 0; i < sizeof(random); ++i)
		random[i] = static_cast<uint8_t>(i * 31 + 17);
	PacketView view;
	for (size_t len = 0; len <= sizeof(random); ++len)
	{
		PacketParseResult res = PacketView::Parse(random, len, view);
		Assert(res == PacketParseResult::Incomplete || res == PacketParseResult::Invalid || res == PacketParseResult::Ok, "random buffer no crash");
	}
	ByteReader r(random, sizeof(random));
	std::string s;
	while (r.Ok() && r.Remaining() > 0)
		r.ReadString(s);
	Assert(!r.Ok() || r.Remaining() == 0, "random read eventually stops");
	return s_failCount == 0;
}

static bool TestSizeTooSmallInvalid()
{
	uint8_t buf[18] = {};
	buf[0] = 10; // size 10 < 18
	buf[1] = 0;
	PacketView view;
	PacketParseResult res = PacketView::Parse(buf, 18, view);
	Assert(res == PacketParseResult::Invalid, "size < header returns Invalid");
	return s_failCount == 0;
}

int main()
{
	TestRoundtripHeader();
	TestStringRoundtrip();
	TestArrayCountRoundtrip();
	TestOversize();
	TestBufferTruncated();
	TestRoundtripPacketBuilderView();
	TestRandomBufferNoCrash();
	TestSizeTooSmallInvalid();
	if (s_failCount != 0)
	{
		std::cerr << "Total failures: " << s_failCount << std::endl;
		return 1;
	}
	std::cout << "Protocol v1 tests: all passed." << std::endl;
	return 0;
}
