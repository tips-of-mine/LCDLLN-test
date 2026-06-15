#pragma once
// Grimoire / Carnet de techniques — renderer ImGui. Lit GrimoireUiPresenter,
// propage le drag&drop d'assignation des slots. Aucun fetch/parse (presenter).

#include <cstdint>

namespace engine::client { class GrimoireUiPresenter; }

namespace engine::render
{
	class GrimoireImGuiRenderer
	{
	public:
		GrimoireImGuiRenderer() = default;

		void SetPresenter(engine::client::GrimoireUiPresenter* presenter) { m_presenter = presenter; }
		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const { return m_enabled; }
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// À appeler entre ImGui::NewFrame() et ImGui::Render() si presenter valide.
		void Render();

	private:
		engine::client::GrimoireUiPresenter* m_presenter = nullptr;
		bool m_enabled = false;
		uint32_t m_viewportW = 0;
		uint32_t m_viewportH = 0;
	};
}
