#include "engine/server/packetlog/PacketLog.h"
#include "engine/core/Log.h"

namespace
{
	using namespace engine::server::packetlog;

	bool TestPushAndSnapshot()
	{
		PacketLog l(4, 8);
		uint8_t data[] = {1, 2, 3, 4, 5};
		l.Push(Direction::In,  10, 1000, data, 5);
		l.Push(Direction::Out, 20, 1100, data, 5);
		l.Push(Direction::In,  30, 1200, data, 5);
		auto snap = l.Snapshot();
		if (snap.size() != 3) return false;
		if (snap[0].opcode != 10 || snap[1].opcode != 20 || snap[2].opcode != 30) return false;
		LOG_INFO(Core, "[PacketLogTests] push+snapshot OK");
		return true;
	}

	bool TestRingOverflow()
	{
		PacketLog l(3, 4);
		uint8_t data[] = {0xFF};
		// Push 5 elements dans un ring de 3 : doit garder les 3 derniers
		for (uint16_t i = 1; i <= 5; ++i)
			l.Push(Direction::In, i, 1000 + i, data, 1);
		auto snap = l.Snapshot();
		if (snap.size() != 3) return false;
		// snap doit contenir 3, 4, 5 dans l'ordre
		if (snap[0].opcode != 3 || snap[1].opcode != 4 || snap[2].opcode != 5) return false;
		LOG_INFO(Core, "[PacketLogTests] ring overflow OK");
		return true;
	}

	bool TestPrefixTruncation()
	{
		PacketLog l(2, 4);
		uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};
		l.Push(Direction::In, 1, 1000, data, 8);
		auto snap = l.Snapshot();
		if (snap.size() != 1) return false;
		// taille originale conservee
		if (snap[0].size != 8) return false;
		// prefix tronque a 4 octets
		if (snap[0].prefix.size() != 4) return false;
		if (snap[0].prefix[0] != 1 || snap[0].prefix[3] != 4) return false;
		LOG_INFO(Core, "[PacketLogTests] prefix truncation OK");
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

	const bool ok = TestPushAndSnapshot() && TestRingOverflow() && TestPrefixTruncation();
	if (ok) LOG_INFO(Core, "[PacketLogTests] ALL OK");
	else LOG_ERROR(Core, "[PacketLogTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
