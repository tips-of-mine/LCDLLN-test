#pragma once

// M100.33 — Machine d'état du mode Playtest (F5). Header-only. Sauvegarde l'état
// caméra éditeur à l'entrée, le restaure à la sortie. Le branchement réel sur
// le contrôleur de personnage de PRODUCTION (sans fork) est fait côté
// intégration (différé) ; ici la logique d'état est testable headless.

#include "src/shared/math/Math.h"

namespace engine::editor::world
{
	/// Pose caméra éditeur sauvegardée (simplifiée).
	struct EditorCameraPose
	{
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		float yawDeg = 0.0f;
		float pitchDeg = 0.0f;
	};

	class PlaytestMode
	{
	public:
		bool IsActive() const { return m_active; }
		const engine::math::Vec3& PlayerStart() const { return m_playerStart; }

		/// Entre en playtest : sauvegarde la pose éditeur, place le joueur au
		/// curseur. No-op si déjà actif.
		void Enter(const EditorCameraPose& editorPose, const engine::math::Vec3& cursorWorld)
		{
			if (m_active) return;
			m_savedPose = editorPose;
			m_playerStart = cursorWorld;
			m_active = true;
		}

		/// Sort du playtest : restaure et renvoie la pose éditeur sauvegardée.
		EditorCameraPose Exit()
		{
			m_active = false;
			return m_savedPose;
		}

		/// Bascule entrée/sortie ; renvoie le nouvel état actif.
		bool Toggle(const EditorCameraPose& editorPose, const engine::math::Vec3& cursorWorld)
		{
			if (m_active) { Exit(); return false; }
			Enter(editorPose, cursorWorld);
			return true;
		}

	private:
		bool m_active = false;
		EditorCameraPose m_savedPose;
		engine::math::Vec3 m_playerStart{ 0.0f, 0.0f, 0.0f };
	};
}
