#include "src/shardd/dbscripts/DBScriptRuntime.h"

#include "src/shared/core/Log.h"

namespace engine::server::dbscripts
{
	/// Helper interne : fabrique une ScriptCommand atomique (kind +
	/// delay + dataString optionnelle). Garde les sites d'appel de
	/// SeedV1Scripts lisibles.
	static ScriptCommand MakeCmd(CommandKind k, uint32_t delayMs,
		uint32_t p1 = 0, const char* text = nullptr)
	{
		ScriptCommand c;
		c.kind   = k;
		c.delayMs = delayMs;
		c.param1 = p1;
		if (text) c.dataString = text;
		return c;
	}

	/// Seed V1 hardcode : deux scripts de demonstration. Les valeurs
	/// (param1, dataString) sont arbitraires — futures iterations les
	/// chargeront depuis la DB (table dbscripts).
	void DBScriptRuntime::SeedV1Scripts()
	{
		m_scripts.clear();
		m_active.clear();
		m_totalFires = 0;

		// Script 1 : "greet" - NPC qui dit bonjour, attend 2s, puis
		// dit une seconde ligne. Demontre Say + Wait via delayMs.
		Script greet;
		greet.scriptId = 1;
		greet.commands.push_back(MakeCmd(CommandKind::Say, 0,    0, "Greetings, traveler."));
		greet.commands.push_back(MakeCmd(CommandKind::Wait, 2000));
		greet.commands.push_back(MakeCmd(CommandKind::Say, 0,    0, "Safe travels."));
		m_scripts.emplace(greet.scriptId, std::move(greet));

		// Script 2 : "patrol" - NPC qui salue (emote), se deplace
		// 1s plus tard, puis re-emote. param1=1 emoteId, param1=42
		// waypointId : arbitraires V1.
		Script patrol;
		patrol.scriptId = 2;
		patrol.commands.push_back(MakeCmd(CommandKind::Emote,  0, 1));
		patrol.commands.push_back(MakeCmd(CommandKind::MoveTo, 1000, 42));
		patrol.commands.push_back(MakeCmd(CommandKind::Emote,  0, 1));
		m_scripts.emplace(patrol.scriptId, std::move(patrol));
	}

	/// Verifie l'existence puis demarre une execution. La file
	/// d'executions n'est pas bornee V1 — en prod, ajouter un cap
	/// (max scripts actifs simultanes) pour eviter une fuite si un
	/// declencheur s'emballe.
	bool DBScriptRuntime::RunScript(uint32_t scriptId, uint64_t nowMs)
	{
		auto it = m_scripts.find(scriptId);
		if (it == m_scripts.end())
			return false;
		ScriptVMState state;
		m_vm.Start(state, it->second, nowMs);
		m_active.push_back(state);
		return true;
	}

	/// Avance toutes les executions actives. Les commandes firees sont
	/// loggees ici pour V1 ; les PRs futures brancheront un dispatch
	/// reel (Say -> ChatRelayHandler, MoveTo -> MotionGenerator, etc.).
	///
	/// Pour purger les executions terminees, on parcourt en place et
	/// on retire via swap-and-pop : ordre non preserve mais ok V1.
	std::size_t DBScriptRuntime::Tick(uint64_t nowMs)
	{
		std::size_t firedThisTick = 0;
		std::vector<std::size_t> firedIdx;
		for (std::size_t i = 0; i < m_active.size(); /*increment conditionnel*/)
		{
			ScriptVMState& st = m_active[i];
			auto it = m_scripts.find(st.scriptId);
			if (it == m_scripts.end())
			{
				// Script disparu du catalogue (theoriquement impossible
				// V1) : on retire l'execution.
				m_active[i] = m_active.back();
				m_active.pop_back();
				continue;
			}

			firedIdx.clear();
			const bool stillRunning = m_vm.Step(st, it->second, nowMs, firedIdx);
			for (std::size_t cmdIdx : firedIdx)
			{
				LOG_DEBUG(DBScripts,
					"[DBScripts] script {} fired cmd idx {} kind {}",
					st.scriptId, cmdIdx,
					static_cast<int>(it->second.commands[cmdIdx].kind));
			}
			firedThisTick += firedIdx.size();

			if (!stillRunning)
			{
				m_active[i] = m_active.back();
				m_active.pop_back();
			}
			else
			{
				++i;
			}
		}
		m_totalFires += firedThisTick;
		return firedThisTick;
	}
}
