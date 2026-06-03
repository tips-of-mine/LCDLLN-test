#pragma once
#include "src/world_editor/core/IPanel.h"

namespace engine::editor::world::panels
{
	/// Panneau Console du shell éditeur monde (sous-projet 1, bloc E). Affiche
	/// les dernières lignes de log capturées par `EditorLogSink` (alimenté par
	/// le sink global `Log::SetSink` posé au boot éditeur), avec filtre par
	/// niveau minimum, auto-scroll et bouton Effacer.
	class ConsolePanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Console"; }

		/// Rend la console (filtre + liste des lignes).
		/// Effet de bord : window ImGui "Console" ; le bouton Effacer vide le
		/// tampon partagé `EditorLogSink`.
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

	private:
		bool m_visible = true;
		/// Index du niveau minimum affiché : 0=Trace,1=Debug,2=Info,3=Warn,4=Error.
		int  m_minLevelIdx = 2;
		bool m_autoScroll = true;
	};
}
