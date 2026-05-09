#include "src/client/settings/SettingsMenuUi.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace engine::client
{
	// =========================================================================
	// Lifecycle
	// =========================================================================

	SettingsMenuPresenter::~SettingsMenuPresenter()
	{
		if (m_initialized)
			Shutdown();
	}

	bool SettingsMenuPresenter::Init(const engine::core::Config& config)
	{
		if (m_initialized)
			Shutdown();

		// ---- Load current values from the engine config ----
		m_graphics.vsync            = config.GetBool  ("render.vsync",         true);
		m_graphics.fullscreen        = config.GetBool  ("render.fullscreen",    true);
		m_graphics.fov               = static_cast<float>(config.GetDouble("render.fov", 70.0));
		m_graphics.qualityPreset     = static_cast<int>(config.GetInt("render.quality_preset", 2));

		m_audio.masterVolume = static_cast<float>(config.GetDouble("audio.master_volume", 1.0));
		m_audio.musicVolume  = static_cast<float>(config.GetDouble("audio.music_volume",  0.8));
		m_audio.sfxVolume    = static_cast<float>(config.GetDouble("audio.sfx_volume",    1.0));
		m_audio.voiceVolume  = static_cast<float>(config.GetDouble("audio.voice_volume",  1.0));
		m_audio.outputDevice = static_cast<int>  (config.GetInt   ("audio.output_device", 0));

		m_controls.mouseSensitivity = static_cast<float>(
			config.GetDouble("camera.mouse_sensitivity", 0.002));
		m_controls.invertY = config.GetBool("controls.invert_y", false);

		m_gameplay.autoLoot       = config.GetBool  ("gameplay.auto_loot",      true);
		m_gameplay.combatText     = config.GetBool  ("gameplay.combat_text",     true);
		m_gameplay.timestamps     = config.GetBool  ("gameplay.timestamps",      false);
		m_gameplay.cameraDistance = static_cast<float>(
			config.GetDouble("gameplay.camera_distance", 5.0));

		// ---- Load keybindings from content-relative JSON ----
		const bool bindingsOk = LoadKeybindings(config);
		if (!bindingsOk)
		{
			LOG_WARN(Core, "[SettingsMenu] Keybindings not loaded — using empty list");
		}

		m_initialized = true;
		LOG_INFO(Core, "[SettingsMenu] Init OK (vsync={}, fullscreen={}, fov={:.0f}, "
		               "master={:.1f}, music={:.1f}, sfx={:.1f}, voice={:.1f}, "
		               "sens={:.4f}, invertY={}, keybindings={})",
			m_graphics.vsync, m_graphics.fullscreen, m_graphics.fov,
			m_audio.masterVolume, m_audio.musicVolume, m_audio.sfxVolume, m_audio.voiceVolume,
			m_controls.mouseSensitivity, m_controls.invertY,
			m_controls.keybindings.size());
		return true;
	}

	void SettingsMenuPresenter::Shutdown()
	{
		m_controls.keybindings.clear();
		m_defaultBindings.clear();
		m_rebindingAction.clear();
		m_graphicsPending  = false;
		m_audioPending     = false;
		m_controlsPending  = false;
		m_gameplayPending  = false;
		m_isOpen           = false;
		m_initialized      = false;
		LOG_INFO(Core, "[SettingsMenu] Shutdown complete");
	}

	// =========================================================================
	// Graphics setters
	// =========================================================================

	void SettingsMenuPresenter::SetResolution(int width, int height)
	{
		if (width > 0 && height > 0)
		{
			m_graphics.resolutionWidth  = width;
			m_graphics.resolutionHeight = height;
		}
	}

	void SettingsMenuPresenter::SetQualityPreset(int preset)
	{
		m_graphics.qualityPreset = std::clamp(preset, 0, 3);
	}

	void SettingsMenuPresenter::SetVsync(bool vsync)
	{
		m_graphics.vsync = vsync;
	}

	void SettingsMenuPresenter::SetFullscreen(bool fullscreen)
	{
		m_graphics.fullscreen = fullscreen;
	}

	void SettingsMenuPresenter::SetFov(float fovDegrees)
	{
		m_graphics.fov = ClampRange(fovDegrees, 60.0f, 120.0f);
	}

	// =========================================================================
	// Audio setters
	// =========================================================================

	void SettingsMenuPresenter::SetMasterVolume(float v)
	{
		m_audio.masterVolume = Clamp01(v);
	}

	void SettingsMenuPresenter::SetMusicVolume(float v)
	{
		m_audio.musicVolume = Clamp01(v);
	}

	void SettingsMenuPresenter::SetSfxVolume(float v)
	{
		m_audio.sfxVolume = Clamp01(v);
	}

	void SettingsMenuPresenter::SetVoiceVolume(float v)
	{
		m_audio.voiceVolume = Clamp01(v);
	}

	void SettingsMenuPresenter::SetOutputDevice(int deviceIndex)
	{
		if (deviceIndex >= 0)
			m_audio.outputDevice = deviceIndex;
	}

	// =========================================================================
	// Controls setters
	// =========================================================================

	void SettingsMenuPresenter::SetMouseSensitivity(float s)
	{
		m_controls.mouseSensitivity = ClampRange(s, 0.0001f, 0.01f);
	}

	void SettingsMenuPresenter::SetInvertY(bool invert)
	{
		m_controls.invertY = invert;
	}

	bool SettingsMenuPresenter::BeginRebind(const std::string& actionId)
	{
		if (FindBinding(actionId) == nullptr)
		{
			LOG_WARN(Core, "[SettingsMenu] BeginRebind: unknown action '{}'", actionId);
			return false;
		}
		m_rebindingAction = actionId;
		LOG_INFO(Core, "[SettingsMenu] Rebinding started for action '{}'", actionId);
		return true;
	}

	bool SettingsMenuPresenter::OnKeyPressed(engine::platform::Key key, std::string& outConflict)
	{
		if (m_rebindingAction.empty())
			return false;

		// Escape cancels rebind without applying.
		if (key == engine::platform::Key::Escape)
		{
			LOG_INFO(Core, "[SettingsMenu] Rebind cancelled by Escape for action '{}'",
			         m_rebindingAction);
			m_rebindingAction.clear();
			return true;
		}

		// Conflict detection: check if key is already used by another action.
		outConflict.clear();
		for (const KeyBinding& b : m_controls.keybindings)
		{
			if (b.actionId != m_rebindingAction && b.currentKey == key)
			{
				outConflict = b.displayName;
				LOG_WARN(Core, "[SettingsMenu] Rebind conflict: key {} already bound to '{}'",
				         static_cast<uint16_t>(key), b.displayName);
				// Reject the rebind but keep rebinding state so the user can try again.
				return true;
			}
		}

		// Apply the new binding.
		KeyBinding* binding = FindBinding(m_rebindingAction);
		if (binding)
		{
			LOG_INFO(Core, "[SettingsMenu] Rebound '{}' from {} to {}",
			         m_rebindingAction,
			         static_cast<uint16_t>(binding->currentKey),
			         static_cast<uint16_t>(key));
			binding->currentKey = key;
		}
		m_rebindingAction.clear();
		return true;
	}

	void SettingsMenuPresenter::CancelRebind()
	{
		if (!m_rebindingAction.empty())
		{
			LOG_INFO(Core, "[SettingsMenu] Rebind cancelled for action '{}'", m_rebindingAction);
			m_rebindingAction.clear();
		}
	}

	void SettingsMenuPresenter::ResetKeybindingsToDefault()
	{
		m_controls.keybindings = m_defaultBindings;
		LOG_INFO(Core, "[SettingsMenu] Keybindings reset to defaults (count={})",
		         m_controls.keybindings.size());
	}

	// =========================================================================
	// Gameplay setters
	// =========================================================================

	void SettingsMenuPresenter::SetAutoLoot(bool v)        { m_gameplay.autoLoot       = v; }
	void SettingsMenuPresenter::SetCombatText(bool v)      { m_gameplay.combatText     = v; }
	void SettingsMenuPresenter::SetTimestamps(bool v)      { m_gameplay.timestamps     = v; }

	void SettingsMenuPresenter::SetCameraDistance(float metres)
	{
		m_gameplay.cameraDistance = ClampRange(metres, 2.0f, 10.0f);
	}

	// =========================================================================
	// Apply / consume
	// =========================================================================

	void SettingsMenuPresenter::ApplyGraphics()
	{
		m_graphicsPending = true;
		LOG_INFO(Core, "[SettingsMenu] Graphics apply pending (vsync={}, fullscreen={}, fov={:.0f}, preset={})",
		         m_graphics.vsync, m_graphics.fullscreen, m_graphics.fov, m_graphics.qualityPreset);
	}

	void SettingsMenuPresenter::ApplyAudio()
	{
		m_audioPending = true;
		LOG_INFO(Core, "[SettingsMenu] Audio apply pending (master={:.1f}, music={:.1f}, sfx={:.1f}, voice={:.1f})",
		         m_audio.masterVolume, m_audio.musicVolume, m_audio.sfxVolume, m_audio.voiceVolume);
	}

	void SettingsMenuPresenter::ApplyControls()
	{
		m_controlsPending = true;
		LOG_INFO(Core, "[SettingsMenu] Controls apply pending (sens={:.4f}, invertY={}, bindings={})",
		         m_controls.mouseSensitivity, m_controls.invertY, m_controls.keybindings.size());
	}

	void SettingsMenuPresenter::ApplyGameplay()
	{
		m_gameplayPending = true;
		LOG_INFO(Core, "[SettingsMenu] Gameplay apply pending (autoLoot={}, combatText={}, timestamps={}, camDist={:.1f})",
		         m_gameplay.autoLoot, m_gameplay.combatText, m_gameplay.timestamps, m_gameplay.cameraDistance);
	}

	GraphicsSettingsCmd SettingsMenuPresenter::ConsumePendingGraphics()
	{
		GraphicsSettingsCmd cmd;
		if (m_graphicsPending)
		{
			cmd.applyRequested  = true;
			cmd.restartRequired = false; // Resolution changes apply without restart on this platform.
			cmd.settings        = m_graphics;
			m_graphicsPending   = false;
		}
		return cmd;
	}

	AudioSettingsCmd SettingsMenuPresenter::ConsumePendingAudio()
	{
		AudioSettingsCmd cmd;
		if (m_audioPending)
		{
			cmd.applyRequested = true;
			cmd.settings       = m_audio;
			m_audioPending     = false;
		}
		return cmd;
	}

	ControlsSettingsCmd SettingsMenuPresenter::ConsumePendingControls()
	{
		ControlsSettingsCmd cmd;
		if (m_controlsPending)
		{
			cmd.applyRequested = true;
			cmd.settings       = m_controls;
			m_controlsPending  = false;
		}
		return cmd;
	}

	GameplaySettingsCmd SettingsMenuPresenter::ConsumePendingGameplay()
	{
		GameplaySettingsCmd cmd;
		if (m_gameplayPending)
		{
			cmd.applyRequested = true;
			cmd.settings       = m_gameplay;
			m_gameplayPending  = false;
		}
		return cmd;
	}

	// =========================================================================
	// Persistence
	// =========================================================================

	bool SettingsMenuPresenter::SaveToFile(const std::filesystem::path& filePath) const
	{
		// Generate flat JSON matching the format read by Engine::LoadUserSettings.
		std::ostringstream json;
		json << "{\n";

		// Graphics
		json << "    \"render.vsync\": "    << (m_graphics.vsync     ? "true" : "false") << ",\n";
		json << "    \"render.fullscreen\": " << (m_graphics.fullscreen ? "true" : "false") << ",\n";
		json << "    \"render.fov\": "       << m_graphics.fov        << ",\n";
		json << "    \"render.quality_preset\": " << m_graphics.qualityPreset << ",\n";
		json << "    \"render.resolution_width\": "  << m_graphics.resolutionWidth  << ",\n";
		json << "    \"render.resolution_height\": " << m_graphics.resolutionHeight << ",\n";

		// Audio
		json << "    \"audio.master_volume\": " << m_audio.masterVolume << ",\n";
		json << "    \"audio.music_volume\": "  << m_audio.musicVolume  << ",\n";
		json << "    \"audio.sfx_volume\": "    << m_audio.sfxVolume    << ",\n";
		json << "    \"audio.voice_volume\": "  << m_audio.voiceVolume  << ",\n";
		json << "    \"audio.output_device\": " << m_audio.outputDevice << ",\n";

		// Controls
		json << "    \"camera.mouse_sensitivity\": " << m_controls.mouseSensitivity << ",\n";
		json << "    \"controls.invert_y\": "  << (m_controls.invertY ? "true" : "false") << ",\n";

		// Keybindings — stored as individual keys per action.
		for (const KeyBinding& b : m_controls.keybindings)
		{
			json << "    \"keybindings." << b.actionId << "\": "
			     << static_cast<uint16_t>(b.currentKey) << ",\n";
		}

		// Gameplay
		json << "    \"gameplay.auto_loot\": "     << (m_gameplay.autoLoot    ? "true" : "false") << ",\n";
		json << "    \"gameplay.combat_text\": "   << (m_gameplay.combatText  ? "true" : "false") << ",\n";
		json << "    \"gameplay.timestamps\": "    << (m_gameplay.timestamps  ? "true" : "false") << ",\n";
		json << "    \"gameplay.camera_distance\": " << m_gameplay.cameraDistance << "\n";

		json << "}\n";

		const std::string text = json.str();
		if (!engine::platform::FileSystem::WriteAllText(filePath, text))
		{
			LOG_ERROR(Core, "[SettingsMenu] SaveToFile FAILED: cannot write '{}'",
			          filePath.string());
			return false;
		}

		LOG_INFO(Core, "[SettingsMenu] Settings saved to '{}'", filePath.string());
		return true;
	}

	// =========================================================================
	// Debug
	// =========================================================================

	std::string SettingsMenuPresenter::BuildDebugText() const
	{
		std::ostringstream ss;
		ss << "[Settings M39.2]\n";
		ss << "  tab=" << static_cast<uint8_t>(m_activeTab)
		   << " open=" << m_isOpen << "\n";
		ss << "  GFX: vsync=" << m_graphics.vsync
		   << " full=" << m_graphics.fullscreen
		   << " fov=" << m_graphics.fov
		   << " preset=" << m_graphics.qualityPreset << "\n";
		ss << "  Audio: master=" << m_audio.masterVolume
		   << " music=" << m_audio.musicVolume
		   << " sfx=" << m_audio.sfxVolume
		   << " voice=" << m_audio.voiceVolume << "\n";
		ss << "  Ctrl: sens=" << m_controls.mouseSensitivity
		   << " invertY=" << m_controls.invertY
		   << " bindings=" << m_controls.keybindings.size() << "\n";
		if (!m_rebindingAction.empty())
			ss << "  [REBINDING: " << m_rebindingAction << "]\n";
		ss << "  Gameplay: autoLoot=" << m_gameplay.autoLoot
		   << " combatText=" << m_gameplay.combatText
		   << " timestamps=" << m_gameplay.timestamps
		   << " camDist=" << m_gameplay.cameraDistance << "\n";
		return ss.str();
	}

	// =========================================================================
	// Private helpers
	// =========================================================================

	bool SettingsMenuPresenter::LoadKeybindings(const engine::core::Config& config)
	{
		// Resolve content-relative path.
		const std::string relPath =
			config.GetString("settings.keybindings_path", "settings/default_keybindings.json");
		const std::string contentRoot = config.GetString("paths.content", "game/data");
		const std::string fullPath    = contentRoot + "/" + relPath;

		engine::core::Config kbCfg;
		if (!kbCfg.LoadFromFile(fullPath))
		{
			LOG_WARN(Core, "[SettingsMenu] LoadKeybindings: file not found '{}'", fullPath);
			return false;
		}

		m_defaultBindings.clear();

		// The JSON uses flat array indices: keybindings[0].action, [0].displayName, [0].key, ...
		size_t index = 0;
		while (true)
		{
			const std::string prefix = "keybindings[" + std::to_string(index) + "]";
			if (!kbCfg.Has(prefix + ".action"))
				break;

			const std::string actionId   = kbCfg.GetString(prefix + ".action",      "");
			const std::string displayName = kbCfg.GetString(prefix + ".displayName", actionId);
			const int         keyCode     = static_cast<int>(kbCfg.GetInt(prefix + ".key", 0));

			if (!actionId.empty() && keyCode > 0)
			{
				KeyBinding b;
				b.actionId    = actionId;
				b.displayName = displayName;
				b.currentKey  = static_cast<engine::platform::Key>(keyCode);
				b.defaultKey  = b.currentKey;
				m_defaultBindings.push_back(b);
			}
			++index;
		}

		if (m_defaultBindings.empty())
		{
			LOG_WARN(Core, "[SettingsMenu] LoadKeybindings: no valid bindings found in '{}'",
			         fullPath);
			return false;
		}

		// Apply any user overrides from the config (loaded from user_settings.json).
		m_controls.keybindings = m_defaultBindings;
		for (KeyBinding& b : m_controls.keybindings)
		{
			const std::string key = "keybindings." + b.actionId;
			if (config.Has(key))
			{
				const int userKey = static_cast<int>(config.GetInt(key, static_cast<int16_t>(b.defaultKey)));
				if (userKey > 0)
					b.currentKey = static_cast<engine::platform::Key>(userKey);
			}
		}

		LOG_INFO(Core, "[SettingsMenu] LoadKeybindings OK ({} actions, path='{}')",
		         m_controls.keybindings.size(), fullPath);
		return true;
	}

	KeyBinding* SettingsMenuPresenter::FindBinding(const std::string& actionId)
	{
		for (KeyBinding& b : m_controls.keybindings)
		{
			if (b.actionId == actionId)
				return &b;
		}
		return nullptr;
	}

	const KeyBinding* SettingsMenuPresenter::FindBinding(const std::string& actionId) const
	{
		for (const KeyBinding& b : m_controls.keybindings)
		{
			if (b.actionId == actionId)
				return &b;
		}
		return nullptr;
	}

	/*static*/ float SettingsMenuPresenter::Clamp01(float v)
	{
		if (v < 0.0f) return 0.0f;
		if (v > 1.0f) return 1.0f;
		return v;
	}

	/*static*/ float SettingsMenuPresenter::ClampRange(float v, float lo, float hi)
	{
		if (v < lo) return lo;
		if (v > hi) return hi;
		return v;
	}

} // namespace engine::client
