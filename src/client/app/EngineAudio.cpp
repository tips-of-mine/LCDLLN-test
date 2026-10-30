// SND.2 / SND.3 / SND.4 / SND.5 — Audio en jeu côté Engine : oreille (caméra),
// pas du personnage, sons d'interface (clics, fenêtres, évènements de jeu),
// ambiances (lieux, heure, météo), combat et sorts.
//
// Fonctions membres d'Engine regroupées ici pour ne pas alourdir Engine.cpp.
// Thread : main thread.

#include "src/client/app/Engine.h"

#include "src/client/audio/Ambience.h"
#include "src/client/audio/CombatAudioCues.h"
#include "src/client/audio/Footsteps.h"
#include "src/client/audio/UiAudioCues.h"
#include "src/shared/world/WorldCells.h"

#include <chrono>
#include <cmath>
#include <random>

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

	engine::audio::AmbienceInputs Engine::BuildAmbienceInputs() const
	{
		namespace cells = engine::world::cells;
		engine::audio::AmbienceInputs in;
		const engine::math::Vec3 pos = m_characterController.GetPosition();
		in.altitude = pos.y;

		// Heure du jour et météo.
		in.daylight = engine::audio::DaylightAt(m_dayNight.GetState().timeOfDay);
		const auto weather = m_weatherSystem.GetCurrentState();
		const float intensity = m_weatherSystem.GetIntensity();
		if (weather == engine::render::WeatherState::Rain)
			in.rain = intensity;
		else if (weather == engine::render::WeatherState::Storm)
		{
			in.rain = intensity;
			in.storm = true;
		}

		if (!m_cellWorld || !m_cellHeights)
			return in; // zone historique : pas de biome connu (plaine par défaut)

		const double gx = m_renderFrame.ToGlobalX(pos.x);
		const double gz = m_renderFrame.ToGlobalZ(pos.z);
		const cells::CellCoord cell = cells::CellFromPosition(gx, gz);
		const cells::CellPoint mn = cells::CellMinCorner(cell);
		for (const CellRenderTile& tile : m_cellTiles)
		{
			if (tile.cell.n != cell.n || tile.cell.e != cell.e)
				continue;
			in.biome = engine::audio::BiomeAt(tile.biomeGrid, tile.biomeGridSize, gx, gz, mn.x, mn.z, cells::kCellSizeMeters);
			if (tile.surface)
				in.snowGround = tile.surface->LayerAt(gx, gz, mn.x, mn.z, cells::kCellSizeMeters) == 3;
			if (tile.scatter)
			{
				// Arbres (instances « occultantes ») dans un rayon de ~30 m, repère de la cellule.
				const cells::CellPoint c = m_renderFrame.CellCenterRender(tile.cell);
				const engine::math::Vec3 local(pos.x - static_cast<float>(c.x), pos.y, pos.z - static_cast<float>(c.z));
				static thread_local std::vector<uint32_t> near;
				tile.scatter->GatherNearSegment(local, local, 30.0f, near);
				in.treeDensity = std::min(1.0f, static_cast<float>(near.size()) / 30.0f);
			}
			break;
		}

		// Eau proche : sondes en couronne à 30 m (mer = surface au niveau de la mer).
		constexpr float kSeaLevelM = 40.0f; // world_master.json : scale.sea_level_m
		for (int k = 0; k < 8; ++k)
		{
			const float a = static_cast<float>(k) * 0.7853982f;
			const float px = pos.x + 30.0f * std::cos(a), pz = pos.z + 30.0f * std::sin(a);
			if (!m_cellHeights->IsLoadedAt(px, pz))
				continue;
			engine::gameplay::IWorldCollider::WaterQuery wq;
			const engine::math::Vec3 probe(px, m_cellHeights->HeightAt(px, pz) + 0.2f, pz);
			if (m_cellCollider.QueryWater(probe, wq) && wq.inWater)
			{
				if (wq.surfaceY < kSeaLevelM + 1.5f)
					in.seaNearby = true;
				else
					in.waterNearby = true;
			}
		}

		// Ville : dans le rayon d'une agglomération de places.json.
		if (m_worldPlaces.IsLoaded())
			in.inTown = m_worldPlaces.CityAt(static_cast<float>(gx), static_cast<float>(gz), 10.0f) != nullptr;
		return in;
	}

	void Engine::UpdateAmbienceAudio(float dt, bool inGame)
	{
		if (!m_audioEngine.IsDeviceReady())
			return;
		using engine::audio::AmbienceLayer;
		std::array<float, engine::audio::kAmbienceLayerCount> targets{};
		if (inGame)
		{
			// Recalcul deux fois par seconde : les fondus font le reste.
			m_ambienceTimer -= dt;
			if (m_ambienceTimer > 0.0f)
				return;
			m_ambienceTimer = 0.5f;
			targets = engine::audio::AmbienceTargets(BuildAmbienceInputs());
		}
		for (size_t i = 0; i < engine::audio::kAmbienceLayerCount; ++i)
		{
			const float target = targets[i];
			uint32_t& voice = m_ambienceVoices[i];
			const std::string_view id = engine::audio::AmbienceSoundId(static_cast<AmbienceLayer>(i));
			if (target > 0.01f)
			{
				if (voice == 0)
				{
					voice = m_audioEngine.PlayLoop(id, target, 3000);
					m_ambienceLevels[i] = target;
				}
				else if (std::abs(target - m_ambienceLevels[i]) > 0.02f)
				{
					m_audioEngine.SetSoundVolume(voice, target, 2500);
					m_ambienceLevels[i] = target;
				}
			}
			else if (voice != 0)
			{
				m_audioEngine.StopSound(voice, inGame ? 3000u : 800u);
				voice = 0;
				m_ambienceLevels[i] = 0.0f;
			}
		}

		// Orage : un coup de tonnerre lointain toutes les 12 à 40 s.
		if (inGame && targets[static_cast<size_t>(AmbienceLayer::RainHeavy)] > 0.01f)
		{
			m_thunderTimer -= 0.5f;
			if (m_thunderTimer <= 0.0f)
			{
				static std::mt19937 rng{ 0x7468756Eu };
				m_thunderTimer = std::uniform_real_distribution<float>(12.0f, 40.0f)(rng);
				m_audioEngine.Play2D("amb_thunder");
			}
		}
	}
	engine::audio::CombatAudioSnapshot Engine::BuildCombatAudioSnapshot() const
	{
		engine::audio::CombatAudioSnapshot snap;
		const engine::client::UIModel& m = m_uiModelBinding.GetModel();
		snap.valid = m.playerStats.playerEntityId != 0;
		snap.playerId = m.playerStats.playerEntityId;
		snap.playerProfile = m.playerStats.profileId;
		snap.playerHealth = m.playerStats.currentHealth;
		snap.playerMaxHealth = m.playerStats.maxHealth;
		snap.hits.reserve(m.combatLog.size());
		for (const engine::client::UICombatLogEntry& e : m.combatLog)
			snap.hits.push_back({ e.sequence, e.attackerEntityId, e.targetEntityId, e.playerWasAttacker,
				e.playerWasTarget, e.wasCrit, e.wasMiss });
		for (const engine::client::UIRemoteEntity& re : m.remoteEntities)
		{
			if (re.maxHealth == 0)
				continue; // sacs de butin, nœuds de récolte : pas de PV
			snap.entityHealth[re.entityId] = re.currentHealth;
			snap.entityIsCreature[re.entityId] = re.archetypeId != 0;
		}
		snap.castActive = m.castBar.active;
		snap.castStartNs = m.castBar.startedAtNs;
		snap.castDurationMs = m.castBar.durationMs;
		const auto auras = m.entityAuras.find(snap.playerId);
		if (auras != m.entityAuras.end())
			for (const engine::client::UIAuraEntry& a : auras->second)
				snap.playerAuras[a.spellId] = a.effectType;
		return snap;
	}

	void Engine::UpdateCombatAudio(bool inGame)
	{
		namespace s = engine::audio::combat_sounds;
		if (!inGame)
		{
			if (m_heartbeatVoice != 0)
			{
				m_audioEngine.StopSound(m_heartbeatVoice, 500);
				m_heartbeatVoice = 0;
			}
			m_combatAudioPrev = engine::audio::CombatAudioSnapshot{};
			return;
		}
		engine::audio::CombatAudioSnapshot cur = BuildCombatAudioSnapshot();
		const uint64_t nowNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		const engine::client::UIModel& m = m_uiModelBinding.GetModel();
		for (const engine::audio::CombatAudioCue& cue : engine::audio::DiffCombatAudio(m_combatAudioPrev, cur, nowNs))
		{
			if (cue.entityId == 0 || cue.entityId == cur.playerId)
			{
				m_audioEngine.Play2D(cue.soundId, cue.volume);
				continue;
			}
			// Position de l'entité : lissée (TD.3) si connue, sinon brute du snapshot.
			bool found = false;
			engine::math::Vec3 pos{};
			const auto sit = m_remoteSmoothed.find(cue.entityId);
			if (sit != m_remoteSmoothed.end() && sit->second.valid)
			{
				pos = engine::math::Vec3(sit->second.x, sit->second.y + 1.0f, sit->second.z);
				found = true;
			}
			else
				for (const engine::client::UIRemoteEntity& re : m.remoteEntities)
					if (re.entityId == cue.entityId)
					{
						pos = engine::math::Vec3(re.positionX, re.positionY + 1.0f, re.positionZ);
						found = true;
						break;
					}
			if (found)
				m_audioEngine.Play3D(pos, cue.soundId, cue.volume);
			else
				m_audioEngine.Play2D(cue.soundId, cue.volume * 0.7f); // hors AoI : atténué
		}

		// Battement de cœur sous 25 % des PV (boucle, fondus courts).
		const bool beat = engine::audio::LowHealthHeartbeat(cur.playerHealth, cur.playerMaxHealth);
		if (beat && m_heartbeatVoice == 0)
			m_heartbeatVoice = m_audioEngine.PlayLoop(s::kHeartbeat, 1.0f, 400);
		else if (!beat && m_heartbeatVoice != 0)
		{
			m_audioEngine.StopSound(m_heartbeatVoice, 800);
			m_heartbeatVoice = 0;
		}
		m_combatAudioPrev = std::move(cur);
	}
}
