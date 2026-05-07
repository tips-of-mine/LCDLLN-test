// engine/editor/world/panels/SurfaceTablePanel.h
#pragma once

#include "engine/editor/world/IPanel.h"
#include "engine/world/surface/SurfaceTable.h"

#include <filesystem>
#include <string>

namespace engine::editor::world::panels
{
	/// Panel ImGui lecture seule listant les 13 surfaces de
	/// `assets/gameplay/surface_table.json` (M100.11). Aucune édition runtime
	/// — modifier le JSON via éditeur externe + bouton [Reload].
	class SurfaceTablePanel final : public engine::editor::world::IPanel
	{
	public:
		const char* GetName() const override { return "Surface Table"; }
		void Render() override;
		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool v) override { m_visible = v; }

		/// Charge le JSON depuis `<contentRoot>/assets/gameplay/surface_table.json`.
		/// `contentRoot` typique : "game/data". Appelé une fois par WorldEditorShell::Init.
		/// Effet de bord : remplit `m_table` et `m_status`.
		void LoadFromContentRoot(const std::filesystem::path& contentRoot);

	private:
		bool m_visible = false;  // panel masqué par défaut
		engine::world::surface::SurfaceTable m_table;
		std::string m_status;     // "Loaded ✓ (13 entries)" / "Parse error: ..."
		std::filesystem::path m_jsonPath;
	};
}
