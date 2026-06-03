/// Tests unitaires CPU pour EditorLogSink (sous-projet 1, bloc E).
/// Vérifie l'éviction par capacité (ring buffer), l'ordre chronologique, et le
/// filtrage par niveau minimum. Singleton process -> Clear() entre sous-tests.
/// Pur CPU, ctest Linux.

#include "src/world_editor/console/EditorLogSink.h"

#include <cstdio>
#include <string>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::core::LogLevel;
	using engine::editor::world::console::EditorLogSink;
	using engine::editor::world::console::LogEntry;

	void Test_CapacityEviction()
	{
		EditorLogSink& sink = EditorLogSink::Instance();
		sink.Clear();
		sink.SetCapacity(3);
		for (int i = 0; i < 5; ++i)
		{
			sink.Push(LogLevel::Info, "Test", "m" + std::to_string(i));
		}
		REQUIRE(sink.Size() == 3u);
		const std::vector<LogEntry> all = sink.Snapshot(LogLevel::Trace);
		REQUIRE(all.size() == 3u);
		// Les 3 dernières, dans l'ordre.
		REQUIRE(all[0].message == "m2");
		REQUIRE(all[1].message == "m3");
		REQUIRE(all[2].message == "m4");
	}

	void Test_LevelFilter()
	{
		EditorLogSink& sink = EditorLogSink::Instance();
		sink.Clear();
		sink.SetCapacity(100);
		sink.Push(LogLevel::Debug, "T", "dbg");
		sink.Push(LogLevel::Info,  "T", "info");
		sink.Push(LogLevel::Warn,  "T", "warn");
		sink.Push(LogLevel::Error, "T", "err");

		REQUIRE(sink.Snapshot(LogLevel::Trace).size() == 4u);
		REQUIRE(sink.Snapshot(LogLevel::Info).size()  == 3u);
		const std::vector<LogEntry> warnPlus = sink.Snapshot(LogLevel::Warn);
		REQUIRE(warnPlus.size() == 2u);
		REQUIRE(warnPlus[0].message == "warn");
		REQUIRE(warnPlus[1].message == "err");
	}

	void Test_NullSubsystem()
	{
		EditorLogSink& sink = EditorLogSink::Instance();
		sink.Clear();
		sink.Push(LogLevel::Info, nullptr, "x");
		const std::vector<LogEntry> all = sink.Snapshot(LogLevel::Trace);
		REQUIRE(all.size() == 1u);
		REQUIRE(all[0].subsystem == "?");
	}
}

int main()
{
	Test_CapacityEviction();
	Test_LevelFilter();
	Test_NullSubsystem();

	if (g_failed == 0)
	{
		std::printf("[PASS] EditorLogSinkTests\n");
		return 0;
	}
	std::printf("[FAIL] EditorLogSinkTests: %d failure(s)\n", g_failed);
	return 1;
}
