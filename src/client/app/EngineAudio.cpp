// SND.2 / SND.3 — Audio en jeu côté Engine : oreille (caméra), pas du
// personnage, sons d'interface (clics, fenêtres, évènements de jeu).
//
// Fonctions membres d'Engine regroupées ici pour ne pas alourdir Engine.cpp.
// Thread : main thread.

#include "src/client/app/Engine.h"

#include "src/client/audio/Footsteps.h"
#include "src/client/audio/UiAudioCues.h"
#include "src/shared/world/WorldCells.h"

#include <cmath>

#if defined(_WIN32)
#	include "imgui.h"
#endif

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

	engine::audio::UiAudioSnapshot Engine::BuildUiAudioSnapshot() const
	{
		engine::audio::UiAudioSnapshot snap;
		const engine::client::UIModel& m = m_uiModelBinding.GetModel();
		snap.valid = true;
		snap.level = m.playerStats.hasXp ? m.playerStats.level : 0;
		for (const engine::client::UIQuestEntry& q : m.quests)
		{
			uint32_t progress = 0;
			for (const engine::client::UIQuestStep& st : q.steps)
				progress += st.currentCount;
			snap.quests[q.questId] = { q.status, progress };
		}
		if (!m.chatLines.empty())
		{
			const engine::client::UIChatLineEntry& last = m.chatLines.back();
			snap.chatSerial = last.timestampUnixMs ^ (static_cast<uint64_t>(m.chatLines.size()) << 48);
			snap.lastChatChannel = last.channelWire;
			snap.lastChatFromSelf = !m_localCharacterNameForAudio.empty() && last.sender == m_localCharacterNameForAudio;
		}
		snap.gold = m.wallet.gold;
		for (const auto& it : m.inventory)
			snap.inventoryUnits += it.quantity;
		for (const auto& e : m.equipment)
			snap.equipmentHash = snap.equipmentHash * 1000003u + (static_cast<uint64_t>(e.slot) << 32 | e.itemId);
		snap.lootVisible = m.lootWindow.visible;
		snap.partyInvite = m.partyInvite.pending;
		// Fenêtres : un bit par panneau (la carte a ses propres sons).
		const bool panels[] = { m_mailVisible, m_reputationVisible, m_characterSheetVisible, m_lfgVisible,
			m_skillBookVisible, m_grimoireVisible, m_classSkillTreeVisible, m_arenaVisible, m_battleGroundVisible,
			m_outdoorPvpVisible, m_weatherVisible, m_gameEventVisible, m_guildVisible, m_auctionHouseVisible,
			m_lootRollVisible, m_craftingVisible, m_advancedCombatVisible, m_inGamePauseMenuVisible,
			m_inGameOptionsPanelVisible, m_gmTicketsVisible };
		for (size_t i = 0; i < sizeof(panels) / sizeof(panels[0]); ++i)
			if (panels[i])
				snap.openPanels |= 1u << i;
		snap.mapOpen = m_worldMapVisible;
		snap.placeBanner = m_placeBanner.Visible();
		return snap;
	}

	void Engine::UpdateUiAudio(bool inGame)
	{
		if (!m_audioEngine.IsDeviceReady())
			return;
#if defined(_WIN32)
		// Clic : un élément ImGui devient actif à la frame d'un appui (bouton,
		// case, onglet, curseur…). Appelée avant ImGui::Render().
		if (ImGui::GetCurrentContext() != nullptr)
		{
			const bool active = ImGui::IsAnyItemActive();
			if (engine::audio::IsUiClick(m_uiAudioWasActive, active, ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
				m_audioEngine.Play2D(engine::audio::ui_sounds::kClick);
			m_uiAudioWasActive = active;
		}
#endif
		engine::audio::UiAudioSnapshot cur = inGame ? BuildUiAudioSnapshot() : engine::audio::UiAudioSnapshot{};
		// Après l'entrée en jeu, l'état (quêtes, inventaire, bourse…) arrive en
		// plusieurs paquets : on n'annonce rien pendant ce délai de chauffe.
		if (m_uiAudioWarmupFrames > 0)
			--m_uiAudioWarmupFrames;
		else
			for (std::string_view id : engine::audio::DiffUiAudio(m_uiAudioPrev, cur))
				m_audioEngine.Play2D(id);
		m_uiAudioPrev = std::move(cur);
	}
}
