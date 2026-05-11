#pragma once
// Wave 8 — Wrapper runtime DBScripts : detient un catalogue de scripts
// (Script) charges au boot et une liste d'executions actives
// (ScriptVMState). V1 minimaliste : 1-2 scripts hardcodes pour prouver
// que le path "tick + Step VM + dispatch fired commands" est exerce
// en boucle main (1Hz), plutot que rester un module orphelin.
//
// Cette PR : 1 script "greet" (Say + Wait + Say) + 1 script "patrol"
// (Emote + MoveTo + Emote). Tick(nowMs) execute Step sur chaque
// ScriptVMState actif et dispatche les fired commands via un sink
// minimal (log "[DBScripts] script N fired cmd idx K"). Le vrai
// dispatcher (vers ChatRelay pour Say, vers MovementGenerator pour
// MoveTo, etc.) sera branche par les PRs futures.
//
// Future iteration : SeedFromDb() qui remplace SeedV1Scripts() ; et
// integration QuestRuntime / talk handler pour RunScript(scriptId)
// declenche par un evenement de jeu (creature interaction, etc.).

#include "src/shardd/dbscripts/DBScript.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::server::dbscripts
{
	/// Wrapper minimaliste autour de Script + ScriptVM. Detient le
	/// catalogue (m_scripts) et la liste des executions actives
	/// (m_active). Une execution = un ScriptVMState lie a un scriptId.
	class DBScriptRuntime
	{
	public:
		DBScriptRuntime() = default;

		/// Charge 1-2 scripts V1 hardcodes :
		///   - scriptId 1 ("greet")  : Say 0ms + Wait 2000ms + Say 0ms
		///   - scriptId 2 ("patrol") : Emote 0ms + MoveTo 1000ms + Emote 0ms
		/// Effet de bord : reset m_scripts, m_active, m_totalFires.
		/// Idempotent : peut etre rappele pour reset (jamais en prod).
		void SeedV1Scripts();

		/// Demarre l'execution du script \p scriptId. Si le script
		/// n'existe pas, retourne false sans rien faire. Sinon ajoute
		/// un ScriptVMState a m_active et appelle Start() dessus.
		///
		/// \param nowMs Horloge wall-clock en millisecondes. Le premier
		///   command (idx 0) fire quand nowMs >= now + commands[0].delayMs.
		bool RunScript(uint32_t scriptId, uint64_t nowMs);

		/// Avance toutes les executions actives. Pour chaque
		/// ScriptVMState, appelle ScriptVM::Step ; les indices de
		/// commandes firees sont logs pour l'instant (futur : dispatch
		/// vers les sous-systemes concernes). Les executions terminees
		/// sont retirees de m_active.
		///
		/// Retourne le nombre total de commandes firees ce tick (pour
		/// log periodique).
		///
		/// \param nowMs Horloge wall-clock en millisecondes (system_clock).
		std::size_t Tick(uint64_t nowMs);

		/// Nombre de scripts dans le catalogue. Sert pour log boot
		/// "[DBScripts] N scripts loaded at boot".
		std::size_t ScriptCount() const noexcept { return m_scripts.size(); }

		/// Nombre d'executions actives a l'instant t.
		std::size_t ActiveCount() const noexcept { return m_active.size(); }

		/// Cumul total de commandes firees depuis le boot.
		std::uint64_t TotalFires() const noexcept { return m_totalFires; }

	private:
		std::unordered_map<uint32_t, Script> m_scripts;
		std::vector<ScriptVMState>           m_active;
		ScriptVM                             m_vm;
		std::uint64_t                        m_totalFires = 0;
	};
}
