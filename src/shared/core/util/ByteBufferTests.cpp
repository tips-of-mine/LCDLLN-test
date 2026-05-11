/**
 * Unit tests for engine::core::util::ByteBuffer.
 * No external test framework; returns 0 if all pass, non-zero on first failure.
 */

#include "src/shared/core/util/ByteBuffer.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
	int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	template <typename T>
	void AssertEq(T a, T b, const char* msg)
	{
		if (!(a == b))
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << " (a != b)" << std::endl;
		}
	}
}

using engine::core::util::ByteBuffer;

static void TestRoundtripArithmetic()
{
	ByteBuffer buf;
	buf << std::uint8_t(0xAB)
		<< std::uint16_t(0x1234)
		<< std::uint32_t(0xDEADBEEFu)
		<< std::uint64_t(0x0123456789ABCDEFull)
		<< std::int32_t(-42)
		<< float(3.14f)
		<< double(-2.718281828);

	std::uint8_t u8 = 0;
	std::uint16_t u16 = 0;
	std::uint32_t u32 = 0;
	std::uint64_t u64 = 0;
	std::int32_t i32 = 0;
	float f = 0.0f;
	double d = 0.0;
	buf >> u8 >> u16 >> u32 >> u64 >> i32 >> f >> d;

	Assert(!buf.HasError(), "roundtrip arithmetic: no error");
	AssertEq(u8, std::uint8_t(0xAB), "u8 roundtrip");
	AssertEq(u16, std::uint16_t(0x1234), "u16 roundtrip");
	AssertEq(u32, std::uint32_t(0xDEADBEEFu), "u32 roundtrip");
	AssertEq(u64, std::uint64_t(0x0123456789ABCDEFull), "u64 roundtrip");
	AssertEq(i32, std::int32_t(-42), "i32 roundtrip");
	Assert(f == 3.14f, "float roundtrip");
	Assert(d == -2.718281828, "double roundtrip");
}

static void TestLittleEndianOrder()
{
	// 0x1234 doit s'écrire {0x34, 0x12} en LE.
	ByteBuffer buf;
	buf << std::uint16_t(0x1234);
	auto data = buf.Data();
	Assert(data.size() == 2, "u16 produces 2 bytes");
	AssertEq(data[0], std::uint8_t(0x34), "LE low byte first");
	AssertEq(data[1], std::uint8_t(0x12), "LE high byte second");
}

static void TestStringRoundtrip()
{
	ByteBuffer buf;
	buf << std::string_view("hello") << std::string_view("") << std::string_view("\xF0\x9F\x98\x80"); // emoji 😀

	std::string a, b, c;
	buf >> a >> b >> c;

	Assert(!buf.HasError(), "string roundtrip: no error");
	Assert(a == "hello", "string a");
	Assert(b.empty(), "string b empty");
	Assert(c == "\xF0\x9F\x98\x80", "string c emoji");
}

static void TestBitLevel()
{
	ByteBuffer buf;
	// Écrit 13 bits : 1 0 1 1 0 0 1 1 | 1 0 1 0 1
	buf.WriteBit(true);
	buf.WriteBit(false);
	buf.WriteBit(true);
	buf.WriteBit(true);
	buf.WriteBit(false);
	buf.WriteBit(false);
	buf.WriteBit(true);
	buf.WriteBit(true); // octet plein → flush auto
	buf.WriteBit(true);
	buf.WriteBit(false);
	buf.WriteBit(true);
	buf.WriteBit(false);
	buf.WriteBit(true);
	buf.FlushBits(); // pad les 3 bits restants à 0

	// 2 octets attendus : LSB-first
	// Octet 1 : bit0=1, bit1=0, bit2=1, bit3=1, bit4=0, bit5=0, bit6=1, bit7=1 → 0b11001101 = 0xCD
	// Octet 2 : bit0=1, bit1=0, bit2=1, bit3=0, bit4=1, bit5=0, bit6=0, bit7=0 → 0b00010101 = 0x15
	auto data = buf.Data();
	Assert(data.size() == 2, "13 bits flushed → 2 bytes");
	AssertEq(data[0], std::uint8_t(0xCD), "first byte bit pattern");
	AssertEq(data[1], std::uint8_t(0x15), "second byte bit pattern");

	// Relit les 13 bits et vérifie.
	const bool expected[13] = {true, false, true, true, false, false, true, true,
							   true, false, true, false, true};
	for (int i = 0; i < 13; ++i)
	{
		const bool got = buf.ReadBit();
		Assert(got == expected[i], "bit roundtrip");
	}
	Assert(!buf.HasError(), "bit-level: no error after 13 reads");
}

static void TestMixedBitAndByte()
{
	// Écrit 3 bits, flush, puis un uint16, puis 2 bits.
	ByteBuffer buf;
	buf.WriteBit(true);
	buf.WriteBit(true);
	buf.WriteBit(false);
	// AppendArithmetic doit déclencher le flush automatiquement.
	buf << std::uint16_t(0xBEEF);
	buf.WriteBit(true);
	buf.WriteBit(false);
	buf.FlushBits();

	auto data = buf.Data();
	// 1 octet (3 bits + 5 zéros pad) + 2 octets uint16 + 1 octet (2 bits + 6 zéros)
	Assert(data.size() == 4, "mixed bit/byte → 4 bytes");
	AssertEq(data[0], std::uint8_t(0b00000011), "first 3 bits 110 LSB-first → 0x03");
	AssertEq(data[1], std::uint8_t(0xEF), "uint16 LE byte 0");
	AssertEq(data[2], std::uint8_t(0xBE), "uint16 LE byte 1");
	AssertEq(data[3], std::uint8_t(0b00000001), "last 2 bits 10 LSB-first → 0x01");

	// Relecture symétrique.
	bool b0 = buf.ReadBit(), b1 = buf.ReadBit(), b2 = buf.ReadBit();
	Assert(b0 && b1 && !b2, "first 3 bits read");
	std::uint16_t u16 = 0;
	buf >> u16;
	AssertEq(u16, std::uint16_t(0xBEEF), "u16 read after AlignReadToByte");
	bool b3 = buf.ReadBit(), b4 = buf.ReadBit();
	Assert(b3 && !b4, "last 2 bits read");
	Assert(!buf.HasError(), "mixed read no error");
}

static void TestOverflowRead()
{
	ByteBuffer buf;
	buf << std::uint32_t(42);

	std::uint32_t a = 0, b = 0;
	buf >> a >> b;

	Assert(buf.HasError(), "second u32 read on 4-byte buffer triggers HasError");
	AssertEq(a, std::uint32_t(42), "first read still succeeds");
}

static void TestStringTooLong()
{
	ByteBuffer buf;
	std::string huge(0x10000, 'x'); // 65536 octets > kMaxStringLength
	buf << huge;
	Assert(buf.HasError(), "string > 65535 → HasError");
}

static void TestClear()
{
	ByteBuffer buf;
	buf << std::uint32_t(7);
	std::uint32_t junk = 0;
	buf >> junk >> junk; // déclenche HasError
	Assert(buf.HasError(), "error set before Clear");

	buf.Clear();
	Assert(!buf.HasError(), "Clear resets error");
	Assert(buf.WritePos() == 0, "Clear resets writePos");
	Assert(buf.ReadPos() == 0, "Clear resets readPos");

	buf << std::uint8_t(99);
	std::uint8_t v = 0;
	buf >> v;
	AssertEq(v, std::uint8_t(99), "buffer reusable after Clear");
}

static void TestSeekWritePatch()
{
	// Pattern courant : réserve un uint32 size, écrit le payload, reseek, patch
	// la taille calculée.
	ByteBuffer buf;
	buf << std::uint32_t(0); // placeholder
	const std::size_t payloadStart = buf.WritePos();
	buf << std::uint16_t(0xAAAA) << std::uint16_t(0xBBBB);
	const std::size_t payloadSize = buf.WritePos() - payloadStart;

	const std::size_t end = buf.WritePos();
	buf.SeekWrite(0);
	buf << static_cast<std::uint32_t>(payloadSize);
	buf.SeekWrite(end);

	auto data = buf.Data();
	Assert(data.size() == 8, "patch leaves total size unchanged");
	std::uint32_t lenField = 0;
	std::memcpy(&lenField, data.data(), 4);
	AssertEq(lenField, std::uint32_t(4), "patched length matches payload");
}

int main()
{
	TestRoundtripArithmetic();
	TestLittleEndianOrder();
	TestStringRoundtrip();
	TestBitLevel();
	TestMixedBitAndByte();
	TestOverflowRead();
	TestStringTooLong();
	TestClear();
	TestSeekWritePatch();

	if (s_failCount > 0)
	{
		std::cerr << "[ByteBufferTests] " << s_failCount << " failure(s)" << std::endl;
		return 1;
	}
	std::cout << "[ByteBufferTests] OK" << std::endl;
	return 0;
}
