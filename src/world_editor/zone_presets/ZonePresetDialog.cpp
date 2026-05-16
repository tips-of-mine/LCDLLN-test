#include "src/world_editor/zone_presets/ZonePresetDialog.h"

#include "src/shared/core/Log.h"
#include "src/world_editor/core/WorldEditorShell.h"
#include "src/world_editor/volumes/arches/ArchTool.h"
#include "src/world_editor/volumes/caves/CaveTool.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalTool.h"
#include "src/world_editor/volumes/overhangs/OverhangTool.h"
#include "src/world_editor/zone_presets/OperationDispatcher.h"
#include "src/world_editor/zone_presets/ZonePreset.h"
#include "src/world_editor/zone_presets/ZonePresetRegistry.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

#include <chrono>
#include <cstdint>

namespace engine::editor::world::zone_presets
{
	ZonePresetDialog::ZonePresetDialog()  = default;
	ZonePresetDialog::~ZonePresetDialog() = default;

	void ZonePresetDialog::Open()
	{
		m_openRequested = true;
		m_screen        = Screen::Select;
	}

	/// Construit le `DispatchContext` à partir des refs Shell et lance
	/// l'exécution synchrone. Le main thread ImGui est bloqué pendant la
	/// durée — convention single-thread acceptée (cf. ZonePresetExecutor.h).
	/// Renseigne `m_lastSummary`, `m_lastPresetId`, `m_lastDurationMs`
	/// puis bascule à l'écran résultat.
	void ZonePresetDialog::RunSelectedPreset(engine::editor::world::WorldEditorShell& shell)
	{
		const auto& presets = ZonePresetRegistry::Instance().Presets();
		if (m_selectedIndex >= presets.size())
			return;
		const ZonePreset& preset = presets[m_selectedIndex];

		CustomizationParams custom;
		custom.reliefMultiplier       = m_relief;
		custom.waterDensityMultiplier = m_waterDensity;
		custom.drynessMultiplier      = m_dryness;
		custom.seed                   = m_seed;

		const DispatchContext ctx{
			shell.MutableTerrainDocument(),
			shell.MutableWaterDocument(),
			shell.MutableMeshInsertDocument(),
			shell.MutableDungeonPortalDocument(),
			shell.GetCaveTool().Catalog(),
			shell.GetOverhangTool().Catalog(),
			shell.GetArchTool().Catalog(),
			shell.GetDungeonPortalTool().Catalog(),
		};

		LOG_INFO(EditorWorld,
			"[ZonePresetDialog] Application '{}' (relief=x{:.2f} water=x{:.2f} dryness=x{:.2f} seed={})",
			preset.id, custom.reliefMultiplier, custom.waterDensityMultiplier,
			custom.drynessMultiplier, custom.seed);

		using clock = std::chrono::steady_clock;
		const auto t0 = clock::now();

		ZonePresetExecutor executor;
		// Callback inerte : on ne peut pas pumper ImGui pendant Execute()
		// (single-thread). La progression part au log via les LOG_INFO du
		// ZonePresetExecutor.
		m_lastSummary = executor.Execute(preset, custom,
			shell.MutableCommandStack(), ctx,
			[](const ExecutionProgress&) { return true; });

		const auto t1     = clock::now();
		m_lastDurationMs  = std::chrono::duration<double, std::milli>(t1 - t0).count();
		m_lastPresetId    = preset.id;
		m_screen          = Screen::Result;

		LOG_INFO(EditorWorld,
			"[ZonePresetDialog] '{}' terminé en {:.0f} ms (pushed={}, skipped={}, failed={})",
			preset.id, m_lastDurationMs, m_lastSummary.commandsPushed,
			m_lastSummary.unsupportedSkipped, m_lastSummary.failed);
	}

#if defined(_WIN32)
	/// Dessine la popup modale en deux écrans (sélection / résultat).
	/// Doit être appelée chaque frame depuis WorldEditorImGui::BuildUi.
	/// Effet de bord : pose un popup ImGui actif sur le premier frame
	/// après `Open()`.
	void ZonePresetDialog::Draw(engine::editor::world::WorldEditorShell& shell)
	{
		if (m_openRequested)
		{
			ImGui::OpenPopup("Zone Presets##zp_modal");
			m_openRequested = false;
			m_isOpen        = true;
		}
		if (!m_isOpen)
			return;

		ImGui::SetNextWindowSize(ImVec2(720.0f, 520.0f), ImGuiCond_FirstUseEver);

		bool open = true;
		if (!ImGui::BeginPopupModal("Zone Presets##zp_modal", &open,
			ImGuiWindowFlags_NoSavedSettings))
		{
			m_isOpen = false;
			return;
		}

		if (m_screen == Screen::Select)
			DrawSelectScreen(shell);
		else
			DrawResultScreen();

		ImGui::EndPopup();

		if (!open)
			m_isOpen = false;
	}

	/// Écran de sélection : liste à gauche, détails + sliders à droite.
	void ZonePresetDialog::DrawSelectScreen(engine::editor::world::WorldEditorShell& shell)
	{
		ImGui::TextUnformatted("Applique un template de zone à la carte courante.");
		ImGui::TextWrapped("ATTENTION : terrain, water, mesh inserts et portails de donjon "
			"de la zone seront vidés avant exécution. Sauvegardez d'abord si nécessaire.");
		ImGui::Separator();

		const auto& presets = ZonePresetRegistry::Instance().Presets();

		ImGui::BeginChild("##zp_list", ImVec2(220.0f, 320.0f), true);
		if (presets.empty())
		{
			ImGui::TextDisabled("Aucun preset chargé.");
			ImGui::TextWrapped("Vérifie game/data/editor/zone_presets/*.json + paths.content.");
		}
		else
		{
			for (size_t i = 0; i < presets.size(); ++i)
			{
				const auto& p     = presets[i];
				const std::string label = !p.displayName.fr.empty()
					? p.displayName.fr : p.id;
				const bool selected = (i == m_selectedIndex);
				ImGui::PushID(static_cast<int>(i));
				if (ImGui::Selectable(label.c_str(), selected))
					m_selectedIndex = i;
				ImGui::PopID();
			}
		}
		ImGui::EndChild();

		ImGui::SameLine();

		ImGui::BeginChild("##zp_details", ImVec2(0.0f, 320.0f), false);
		if (m_selectedIndex >= presets.size())
		{
			ImGui::TextDisabled("Sélectionne un preset à gauche.");
		}
		else
		{
			const auto& p = presets[m_selectedIndex];
			ImGui::Text("Preset : %s", !p.displayName.fr.empty()
				? p.displayName.fr.c_str() : p.id.c_str());
			ImGui::Text("Id : %s", p.id.c_str());
			ImGui::Text("Opérations : %zu  -  Durée estimée : ~%.0f s",
				p.operations.size(),
				static_cast<double>(p.estimatedExecutionSeconds));
			ImGui::Separator();
			ImGui::TextWrapped("%s", p.description.fr.c_str());
			ImGui::Separator();
			ImGui::TextUnformatted("Customisation :");
			ImGui::SliderFloat("Relief",         &m_relief,       0.25f, 2.0f, "x%.2f");
			ImGui::SliderFloat("Densite d'eau",  &m_waterDensity, 0.25f, 2.0f, "x%.2f");
			ImGui::SliderFloat("Secheresse",     &m_dryness,      0.25f, 2.0f, "x%.2f");

			int seed = static_cast<int>(m_seed);
			if (ImGui::InputInt("Seed RNG", &seed, 1, 100))
				m_seed = static_cast<uint32_t>(seed < 0 ? 0 : seed);
		}
		ImGui::EndChild();

		ImGui::Separator();

		const bool canApply = (m_selectedIndex < presets.size());
		if (!canApply) ImGui::BeginDisabled();
		if (ImGui::Button("Appliquer", ImVec2(140.0f, 0.0f)) && canApply)
			RunSelectedPreset(shell);
		if (!canApply) ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Annuler", ImVec2(140.0f, 0.0f)))
		{
			m_isOpen = false;
			ImGui::CloseCurrentPopup();
		}
	}

	/// Écran de résultat post-exécution.
	void ZonePresetDialog::DrawResultScreen()
	{
		ImGui::TextUnformatted("Résumé de l'application");
		ImGui::Separator();
		ImGui::Text("Preset                 : %s", m_lastPresetId.c_str());
		ImGui::Spacing();
		ImGui::Text("Opérations totales     : %u", m_lastSummary.totalSteps);
		ImGui::Text("Commandes poussées     : %u", m_lastSummary.commandsPushed);
		ImGui::Text("Ignorées (non câblées) : %u", m_lastSummary.unsupportedSkipped);
		ImGui::Text("Échecs                 : %u", m_lastSummary.failed);
		ImGui::Text("Annulé                 : %s",
			m_lastSummary.wasCancelled ? "oui" : "non");
		ImGui::Text("Durée                  : %.0f ms", m_lastDurationMs);
		ImGui::Separator();
		if (m_lastSummary.unsupportedSkipped > 0u)
		{
			ImGui::TextWrapped("Note : certaines opérations (érosions, river_network, "
				"coastline) n'ont pas encore de chemin d'exécution sans UI et sont "
				"ignorées (M100.46 incrément 2e à venir).");
			ImGui::Separator();
		}
		if (m_lastSummary.failed > 0u)
		{
			ImGui::TextWrapped("Certaines opérations ont échoué (catalogId introuvable, "
				"paramètres invalides...). Voir les logs EditorWorld pour le détail.");
			ImGui::Separator();
		}
		if (ImGui::Button("Retour à la sélection", ImVec2(180.0f, 0.0f)))
		{
			m_screen = Screen::Select;
		}
		ImGui::SameLine();
		if (ImGui::Button("Fermer", ImVec2(140.0f, 0.0f)))
		{
			m_isOpen = false;
			ImGui::CloseCurrentPopup();
		}
	}

#else
	// Pas d'ImGui hors Windows : le world editor est Windows-only. On garde
	// les symboles linkés pour que l'unité de compilation soit utilisable
	// dans engine_core sans condition. Les méthodes sont no-op.
	void ZonePresetDialog::Draw(engine::editor::world::WorldEditorShell&)        { }
	void ZonePresetDialog::DrawSelectScreen(engine::editor::world::WorldEditorShell&) { }
	void ZonePresetDialog::DrawResultScreen()                                    { }
#endif
}
