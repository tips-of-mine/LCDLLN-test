#pragma once
#include "engine/editor/world/IPanel.h"

namespace engine::editor::world::panels
{
	/// Panneau Console du shell éditeur monde (M100.1 — placeholder).
	/// Reflètera les LOG_*(EditorWorld, ...) via un sink engine::core::Log
	/// dans un ticket ultérieur (sink mécanisme non encore exposé par
	/// Log.h — déféré à M100.2 ou plus tard).
	class ConsolePanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Console"; }

		/// Rend le placeholder texte du panneau Console.
		/// Effet de bord : crée une window ImGui nommée "Console".
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

	private:
		bool m_visible = true;
	};
}
