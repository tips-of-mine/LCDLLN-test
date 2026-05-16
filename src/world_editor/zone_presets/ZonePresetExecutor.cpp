#include "src/world_editor/zone_presets/ZonePresetExecutor.h"

#include "src/shared/core/Log.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/zone_presets/WorldMapEditDocumentReset.h"

#include <memory>
#include <utility>

namespace engine::editor::world::zone_presets
{
	ExecutionSummary ZonePresetExecutor::Execute(const ZonePreset& preset,
		const CustomizationParams& custom,
		engine::editor::world::CommandStack& commandStack,
		const DispatchContext& dispatchCtx,
		engine::editor::world::WaterDocument& water,
		const ProgressCallback& progressCallback)
	{
		m_cancelRequested.store(false, std::memory_order_release);

		ExecutionSummary summary;
		summary.totalSteps = static_cast<uint32_t>(preset.operations.size());

		// 1) Reset destructif de la zone (M100.46 §D.3).
		ResetEditedZoneDocuments(dispatchCtx.terrain, water,
			dispatchCtx.meshInserts, dispatchCtx.dungeonPortals);
		LOG_INFO(EditorWorld, "[ZonePresetExecutor] Zone reset pour '{}' ({} ops)",
			preset.id, summary.totalSteps);

		// 2) Boucle d'exécution séquentielle.
		for (uint32_t i = 0; i < summary.totalSteps; ++i)
		{
			if (IsCancelRequested())
			{
				summary.wasCancelled = true;
				LOG_INFO(EditorWorld, "[ZonePresetExecutor] Annulé après {} ops", i);
				break;
			}

			const ZonePresetOperation& op = preset.operations[i];
			std::unique_ptr<engine::editor::world::ICommand> cmd;
			const DispatchResult result = DispatchOperation(op, custom, dispatchCtx, cmd);

			switch (result)
			{
				case DispatchResult::Ok:
					commandStack.Push(std::move(cmd));
					++summary.commandsPushed;
					break;
				case DispatchResult::Unsupported:
					++summary.unsupportedSkipped;
					break;
				case DispatchResult::Failed:
					++summary.failed;
					break;
			}

			if (progressCallback)
			{
				ExecutionProgress prog;
				prog.currentStep      = i + 1u;
				prog.totalSteps       = summary.totalSteps;
				prog.currentStepLabel = "Étape " + std::to_string(i + 1u)
					+ "/" + std::to_string(summary.totalSteps) + " : " + op.type;
				prog.fractionComplete = static_cast<float>(i + 1u)
					/ static_cast<float>(summary.totalSteps);
				prog.isCancelled      = IsCancelRequested();
				const bool keepGoing = progressCallback(prog);
				if (!keepGoing) RequestCancel();
			}
		}

		LOG_INFO(EditorWorld,
			"[ZonePresetExecutor] '{}' terminé : {} commandes / {} skipped (non câblés) / {} failed{}",
			preset.id, summary.commandsPushed, summary.unsupportedSkipped, summary.failed,
			summary.wasCancelled ? " (annulé)" : "");
		return summary;
	}
}
