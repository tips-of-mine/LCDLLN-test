// src/world_editor/world/CollisionPreviewCamera.h
#pragma once

#include "src/shared/math/Math.h"

namespace engine::editor::world
{
	/// Mini-caméra orbitale pour le preview 3D du CollisionEditorPanel (M100.12).
	/// La caméra orbite autour de l'origine (0,0,0). Drag souris ajuste yaw/pitch ;
	/// molette ajuste distance. Pas d'état Vulkan ou ImGui — pure math.
	class CollisionPreviewCamera
	{
	public:
		/// Ajuste yaw/pitch en radians (sensibilité fixée).
		/// Pitch est clampé [-π/2 + 0.05, π/2 - 0.05] pour éviter gimbal lock.
		void HandleDrag(float deltaX, float deltaY) noexcept;

		/// Ajuste distance (zoom). deltaWheel positif = zoom in (plus proche).
		/// Distance clampée [0.5, 20.0].
		void HandleZoom(float deltaWheel) noexcept;

		/// Reset à valeurs par défaut (yaw=0.7, pitch=0.4, distance=3.0).
		void Reset() noexcept;

		/// Projette un point 3D world-space vers les coordonnées pixel
		/// (top-left origin) dans une zone de viewport `viewportW × viewportH`.
		/// \return true si le point est devant la caméra et dans le viewport.
		bool Project(engine::math::Vec3 worldPos,
			float viewportW, float viewportH,
			float& outScreenX, float& outScreenY) const;

		/// Yaw courant converti en degrés (pour affichage HUD du panel).
		float GetYawDegrees() const noexcept;
		/// Pitch courant converti en degrés (pour affichage HUD du panel).
		float GetPitchDegrees() const noexcept;
		/// Distance caméra-origine en mètres (pour affichage HUD du panel).
		float GetDistance() const noexcept { return m_distance; }

	private:
		float m_yaw      = 0.7f;  // radians
		float m_pitch    = 0.4f;  // radians
		float m_distance = 3.0f;  // meters from origin
	};
}
