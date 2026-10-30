// SND.2 — Audio en jeu côté Engine : oreille (caméra) et pas du personnage.
//
// Fonctions membres d'Engine regroupées ici pour ne pas alourdir Engine.cpp.
// Thread : main thread.

#include "src/client/app/Engine.h"

#include "src/client/audio/Footsteps.h"
#include "src/shared/world/WorldCells.h"

#include <cmath>

namespace engine
{
	void Engine::UpdateAudioListener(const engine::render::Camera& camera)
	{
		// Direction du regard (cf. Camera.cpp : la caméra regarde vers -Z local).
		const float sy = std::sin(camera.yaw), cy = std::cos(camera.yaw);
		const float sp = std::sin(camera.pitch), cp = std::cos(camera.pitch);
		const engine::math::Vec3 forward(-sy * cp, -sp, -cy * cp);
		m_audioEngine.SetListenerOrientation(forward, engine::math::Vec3(0.0f, 1.0f, 0.0f));
	}

	void Engine::UpdateFootstepAudio(float dt)
	{
		if (!m_audioEngine.IsDeviceReady())
			return;
		const engine::math::Vec3 vel = m_characterController.GetVelocity();
		const float speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
		const bool swimming = m_characterController.IsInWater();
		const engine::audio::FootstepEvent ev =
			m_footstepCadence.Update(dt, speed, m_characterController.IsGrounded(), swimming);
		if (ev == engine::audio::FootstepEvent::None)
			return;

		// Pied : bas de la capsule (le contrôleur suit le centre).
		const engine::math::Vec3 center = m_characterController.GetPosition();
		const auto capsule = m_characterController.GetCapsule();
		const engine::math::Vec3 foot(center.x, center.y - capsule.height * 0.5f - capsule.radius, center.z);

		engine::audio::FootstepContext ctx;
		ctx.swimming = swimming;
		if (m_cellWorld && m_cellHeights)
		{
			// Monde en cellules : couche dominante du splat sous le pied, hauteur
			// au-dessus du terrain (plancher d'un bâtiment ou d'un pont).
			const double gx = m_renderFrame.ToGlobalX(foot.x);
			const double gz = m_renderFrame.ToGlobalZ(foot.z);
			const engine::world::cells::CellCoord cell = engine::world::cells::CellFromPosition(gx, gz);
			for (const CellRenderTile& tile : m_cellTiles)
			{
				if (tile.cell.n != cell.n || tile.cell.e != cell.e || !tile.surface)
					continue;
				const engine::world::cells::CellPoint mn = engine::world::cells::CellMinCorner(cell);
				const int layer = tile.surface->LayerAt(gx, gz, mn.x, mn.z, engine::world::cells::kCellSizeMeters);
				ctx.layer = static_cast<engine::audio::GroundLayer>(layer);
				break;
			}
			if (m_cellHeights->IsLoadedAt(foot.x, foot.z))
				ctx.heightAboveTerrain = foot.y - m_cellHeights->HeightAt(foot.x, foot.z);
		}

		float volume = engine::audio::FootstepVolume(speed);
		if (ev == engine::audio::FootstepEvent::Land)
			volume = 1.0f;
		const std::string_view id = engine::audio::FootstepSoundId(ctx);
		m_audioEngine.Play3D(foot, id, volume);
	}
}
