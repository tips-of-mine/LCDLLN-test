#include "engine/server/dbscripts/DBScript.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::dbscripts::CommandKind;
	using engine::server::dbscripts::Script;
	using engine::server::dbscripts::ScriptCommand;
	using engine::server::dbscripts::ScriptVM;
	using engine::server::dbscripts::ScriptVMState;

	bool TestEmptyScriptFinishesImmediately()
	{
		Script s;
		s.scriptId = 1;
		ScriptVMState state;
		ScriptVM vm;
		vm.Start(state, s, 0);
		std::vector<size_t> fired;
		if (vm.Step(state, s, 100, fired)) return false;  // already finished
		if (!fired.empty()) return false;
		LOG_INFO(Core, "[DBScriptTests] empty script OK");
		return true;
	}

	bool TestSequenceWithDelays()
	{
		Script s;
		s.scriptId = 1;
		s.commands.push_back({CommandKind::Say, 0, 1, 0, 0, "Hi"});
		s.commands.push_back({CommandKind::Wait, 100, 0, 0, 0, ""});
		s.commands.push_back({CommandKind::Emote, 50, 5, 0, 0, ""});

		ScriptVMState state;
		ScriptVM vm;
		vm.Start(state, s, 0);
		std::vector<size_t> fired;

		// t=0 : commande 0 fire (delay 0).
		vm.Step(state, s, 0, fired);
		if (fired.size() != 1 || fired[0] != 0) return false;

		// t=50 : pas encore (cmd 1 a delay 100).
		vm.Step(state, s, 50, fired);
		if (!fired.empty()) return false;

		// t=100 : cmd 1 fire (Wait).
		vm.Step(state, s, 100, fired);
		if (fired.size() != 1 || fired[0] != 1) return false;

		// t=150 : cmd 2 fire (Emote, delay 50 ⇒ run at 100+50=150).
		vm.Step(state, s, 150, fired);
		if (fired.size() != 1 || fired[0] != 2) return false;

		// t=200 : termine.
		bool stillRunning = vm.Step(state, s, 200, fired);
		if (stillRunning) return false;
		if (!state.finished) return false;
		LOG_INFO(Core, "[DBScriptTests] sequence with delays OK");
		return true;
	}

	bool TestCatchUpMultipleCommands()
	{
		// Si on Step apres un long gap, plusieurs commandes peuvent
		// fire dans le meme appel.
		Script s;
		s.scriptId = 1;
		for (int i = 0; i < 5; ++i)
			s.commands.push_back({CommandKind::Say, 100, static_cast<uint32_t>(i), 0, 0, ""});

		ScriptVMState state;
		ScriptVM vm;
		vm.Start(state, s, 0);
		std::vector<size_t> fired;
		// Step at t=10000 : tous les 5 doivent avoir fire.
		vm.Step(state, s, 10000, fired);
		if (fired.size() != 5) return false;
		LOG_INFO(Core, "[DBScriptTests] catch-up OK");
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

	const bool ok = TestEmptyScriptFinishesImmediately()
		&& TestSequenceWithDelays() && TestCatchUpMultipleCommands();

	if (ok) LOG_INFO(Core, "[DBScriptTests] ALL OK");
	else LOG_ERROR(Core, "[DBScriptTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
