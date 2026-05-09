#pragma once
// CMANGOS.14 (Phase 3.14a) — DBScripts : DSL minimal data-driven pour
// scripts narratifs (talk to NPC → say + give item + start quest).
// Chaque script est une liste de commandes typees, chargee depuis DB
// puis executee par un VM step-by-step.
//
// Cette PR : data structures + VM step-by-step. Pas encore de loader
// DB ni d'integration QuestRuntime / talk handler.

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server::dbscripts
{
	enum class CommandKind : uint8_t
	{
		Say          = 0,   ///< NPC dit un texte (param: stringId; data: text)
		Emote        = 1,   ///< NPC fait un emote (param: emoteId)
		MoveTo       = 2,   ///< NPC se deplace (param: waypointId)
		Wait         = 3,   ///< Attendre param ms avant la prochaine commande
		GiveItem     = 4,   ///< Donne item (param: itemTemplateId, count)
		StartQuest   = 5,   ///< Start quest (param: questId)
		FinishQuest  = 6,   ///< Complete quest
		PlaySound    = 7,   ///< (param: soundId)
		Teleport     = 8,   ///< (param: mapId, posX, posY, posZ packed)
		Custom       = 99,
	};

	struct ScriptCommand
	{
		CommandKind kind        = CommandKind::Custom;
		uint32_t    delayMs     = 0;       ///< delai avant execution
		uint32_t    param1      = 0;       ///< depend du kind
		uint32_t    param2      = 0;
		uint32_t    param3      = 0;
		std::string dataString;            ///< pour Say + Custom
	};

	struct Script
	{
		uint32_t              scriptId = 0;
		std::vector<ScriptCommand> commands;
	};

	/// Etat d'execution d'un script.
	struct ScriptVMState
	{
		uint32_t scriptId       = 0;
		size_t   nextCommandIdx = 0;
		uint64_t nextRunTickMs  = 0;
		bool     finished       = false;
	};

	/// Step-by-step VM. A chaque tick, appeler `Step(state, script,
	/// nowMs, outFiredCommands)` qui execute toutes les commandes dont
	/// le delay est ecoule. \p outFiredCommands recoit les indices des
	/// commandes a appliquer (le caller dispatch les side-effects).
	class ScriptVM
	{
	public:
		void Start(ScriptVMState& state, const Script& script, uint64_t nowMs);

		/// Avance le state machine. Retourne false quand le script est
		/// termine.
		bool Step(ScriptVMState& state, const Script& script, uint64_t nowMs,
			std::vector<size_t>& outFiredCommands);
	};
}
