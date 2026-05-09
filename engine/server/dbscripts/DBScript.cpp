#include "engine/server/dbscripts/DBScript.h"

namespace engine::server::dbscripts
{
	void ScriptVM::Start(ScriptVMState& state, const Script& script, uint64_t nowMs)
	{
		state.scriptId       = script.scriptId;
		state.nextCommandIdx = 0;
		state.nextRunTickMs  = (script.commands.empty()) ? nowMs
			: nowMs + script.commands[0].delayMs;
		state.finished       = script.commands.empty();
	}

	bool ScriptVM::Step(ScriptVMState& state, const Script& script, uint64_t nowMs,
		std::vector<size_t>& outFiredCommands)
	{
		outFiredCommands.clear();
		if (state.finished) return false;

		while (state.nextCommandIdx < script.commands.size()
			&& nowMs >= state.nextRunTickMs)
		{
			outFiredCommands.push_back(state.nextCommandIdx);
			++state.nextCommandIdx;
			if (state.nextCommandIdx < script.commands.size())
			{
				state.nextRunTickMs += script.commands[state.nextCommandIdx].delayMs;
			}
			else
			{
				state.finished = true;
			}
		}
		return !state.finished;
	}
}
