#include "engine/client/SettingsUi.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <sstream>

namespace engine::client
{
	// ── Helpers ────────────────────────────────────────────────────────────────

	namespace
	{
		const char* QualityPresetName(QualityPreset p)
		{
			switch (p)
			{
			case QualityPreset::Low:    return "Low";
			case QualityPreset::Medium: return "Medium";
			case QualityPreset::High:   return "High";
			case QualityPreset::Ultra:  return "Ultra";
			}
			return "High";
		}

		QualityPreset QualityPresetFromString(std::string_view s)
		{
			if (s == "Low")    return QualityPreset::Low;
			if (s == "Medium") return QualityPreset::Medium;
			if (s == "Ultra")  return QualityPreset::Ultra;
			return QualityPreset::High;
		}

		/// Return true when \p action has a keybinding collision with another
		/// action already mapped to \p key.
		bool HasConflict(const std::unordered_map<std::string,std::string>& bindings,
		                 std::string_view action, std::string_view key)
		{
			for (const auto& [a, k] : bindings)
			{
				if (a != action && k == key)
					return true;
			}
			return false;
		}
	} // anonymous namespace

	// ── SettingsUiPresenter ────────────────────────────────────────────────────

	SettingsUiPresenter::~SettingsUiPresenter()
	{
		Shutdown();
	}

	bool SettingsUiPresenter::Init(const engine::core::Config& config,
	                               std::string_view settingsRelativePath,
	                               std::string_view defaultKeybindingsRelativePath)
	{
		m_settingsRelativePath = std::string(settingsRelativePath);

		ApplyBuiltinDefaults();
		LoadDefaultKeybindings(config, defaultKeybindingsRelativePath);
		LoadUserSettings(config, settingsRelativePath);

		m_state.activeTab    = SettingsTab::Graphics;
		m_state.dirty        = false;
		m_state.statusMessage = "Settings loaded.";
		m_initialized = true;

		LOG_INFO(Core, "[SettingsUi] Init OK (settingsPath={})", settingsRelativePath);
		return true;
	}

	void SettingsUiPresenter::Shutdown()
	{
		if (!m_initialized) return;
		m_state       = {};
		m_initialized = false;
		LOG_INFO(Core, "[SettingsUi] Shutdown");
	}

	void SettingsUiPresenter::SetTab(SettingsTab tab)
	{
		m_state.activeTab = tab;
		m_state.keybindConflict.clear();
		CancelRebind();
		LOG_DEBUG(Core, "[SettingsUi] Tab switched to {}", static_cast<int>(tab));
	}

	// ── Graphics ─────────────────────────────────────────────────────────────

	void SettingsUiPresenter::SetResolution(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
		{
			LOG_WARN(Core, "[SettingsUi] SetResolution rejected: {}x{} is invalid", width, height);
			return;
		}
		m_state.graphics.resolutionWidth  = width;
		m_state.graphics.resolutionHeight = height;
		m_state.dirty = true;
	}

	void SettingsUiPresenter::SetQualityPreset(QualityPreset preset)
	{
		m_state.graphics.qualityPreset = preset;
		m_state.dirty = true;
		LOG_INFO(Core, "[SettingsUi] Quality preset set to {}", QualityPresetName(preset));
	}

	void SettingsUiPresenter::SetVsync(bool enabled)
	{
		m_state.graphics.vsync = enabled;
		m_state.dirty = true;
	}

	void SettingsUiPresenter::SetFullscreen(bool enabled)
	{
		m_state.graphics.fullscreen = enabled;
		m_state.dirty = true;
	}

	void SettingsUiPresenter::SetFov(float degrees)
	{
		m_state.graphics.fov = Clamp(degrees, 60.0f, 120.0f);
		m_state.dirty = true;
	}

	// ── Audio ─────────────────────────────────────────────────────────────────

	void SettingsUiPresenter::SetMasterVolume(float v)
	{
		m_state.audio.masterVolume = Clamp(v, 0.0f, 100.0f);
		m_state.dirty = true;
	}

	void SettingsUiPresenter::SetSfxVolume(float v)
	{
		m_state.audio.sfxVolume = Clamp(v, 0.0f, 100.0f);
		m_state.dirty = true;
	}

	void SettingsUiPresenter::SetMusicVolume(float v)
	{
		m_state.audio.musicVolume = Clamp(v, 0.0f, 100.0f);
		m_state.dirty = true;
	}

	void SettingsUiPresenter::SetVoiceVolume(float v)
	{
		m_state.audio.voiceVolume = Clamp(v, 0.0f, 100.0f);
		m_state.dirty = true;
	}

	void SettingsUiPresenter::SetOutputDevice(std::string_view device)
	{
		m_state.audio.outputDevice = std::string(device);
		m_state.dirty = true;
	}

	// ── Controls ─────────────────────────────────────────────────────────────

	void SettingsUiPresenter::SetMouseSensitivity(float v)
	{
		if (v <= 0.0f)
		{
			LOG_WARN(Core, "[SettingsUi] SetMouseSensitivity: value must be > 0 (got {})", v);
			return;
		}
		m_state.controls.mouseSensitivity = v;
		m_state.dirty = true;
	}

	void SettingsUiPresenter::SetInvertY(bool enabled)
	{
		m_state.controls.invertY = enabled;
		m_state.dirty = true;
	}

	void SettingsUiPresenter::StartRebind(std::string_view action)
	{
		m_state.rebindingAction = std::string(action);
		m_state.keybindConflict.clear();
		LOG_DEBUG(Core, "[SettingsUi] StartRebind action='{}'", action);
	}

	bool SettingsUiPresenter::CommitRebind(std::string_view newKey)
	{
		if (m_state.rebindingAction.empty())
			return false;

		if (HasConflict(m_state.controls.keybindings, m_state.rebindingAction, newKey))
		{
			// Find which action already uses this key for the conflict message.
			for (const auto& [a, k] : m_state.controls.keybindings)
			{
				if (k == newKey && a != m_state.rebindingAction)
				{
					m_state.keybindConflict =
					    "Key '" + std::string(newKey) + "' is already used by '" + a + "'.";
					break;
				}
			}
			LOG_WARN(Core, "[SettingsUi] Keybind conflict: {}", m_state.keybindConflict);
			return false;
		}

		m_state.controls.keybindings[m_state.rebindingAction] = std::string(newKey);
		LOG_INFO(Core, "[SettingsUi] Rebound '{}' -> '{}'", m_state.rebindingAction, newKey);
		m_state.rebindingAction.clear();
		m_state.keybindConflict.clear();
		m_state.dirty = true;
		return true;
	}

	void SettingsUiPresenter::CancelRebind()
	{
		m_state.rebindingAction.clear();
		m_state.keybindConflict.clear();
	}

	// ── Gameplay ─────────────────────────────────────────────────────────────

	void SettingsUiPresenter::SetAutoLoot(bool enabled)
	{
		m_state.gameplay.autoLoot = enabled;
		m_state.dirty = true;
	}

	void SettingsUiPresenter::SetCombatText(bool enabled)
	{
		m_state.gameplay.combatText = enabled;
		m_state.dirty = true;
	}

	void SettingsUiPresenter::SetTimestamps(bool enabled)
	{
		m_state.gameplay.timestamps = enabled;
		m_state.dirty = true;
	}

	void SettingsUiPresenter::SetCameraDistance(float metres)
	{
		m_state.gameplay.cameraDistance = Clamp(metres, 2.0f, 10.0f);
		m_state.dirty = true;
	}

	// ── Persistence ───────────────────────────────────────────────────────────

	bool SettingsUiPresenter::Save(const engine::core::Config& config)
	{
		const auto fullPath = engine::platform::FileSystem::ResolveContentPath(
		    config, m_settingsRelativePath);

		// Build a flat INI-like representation using engine::core::Config's own format.
		std::ostringstream out;
		// Graphics
		out << "settings.graphics.resolution_width="  << m_state.graphics.resolutionWidth  << "\n";
		out << "settings.graphics.resolution_height=" << m_state.graphics.resolutionHeight << "\n";
		out << "settings.graphics.quality_preset="    << QualityPresetName(m_state.graphics.qualityPreset) << "\n";
		out << "settings.graphics.vsync="             << (m_state.graphics.vsync      ? "true" : "false") << "\n";
		out << "settings.graphics.fullscreen="        << (m_state.graphics.fullscreen ? "true" : "false") << "\n";
		out << "settings.graphics.fov="               << m_state.graphics.fov << "\n";
		// Audio
		out << "settings.audio.master_volume=" << m_state.audio.masterVolume << "\n";
		out << "settings.audio.sfx_volume="    << m_state.audio.sfxVolume    << "\n";
		out << "settings.audio.music_volume="  << m_state.audio.musicVolume  << "\n";
		out << "settings.audio.voice_volume="  << m_state.audio.voiceVolume  << "\n";
		if (!m_state.audio.outputDevice.empty())
			out << "settings.audio.output_device=" << m_state.audio.outputDevice << "\n";
		// Controls
		out << "settings.controls.mouse_sensitivity=" << m_state.controls.mouseSensitivity << "\n";
		out << "settings.controls.invert_y="          << (m_state.controls.invertY ? "true" : "false") << "\n";
		for (const auto& [action, key] : m_state.controls.keybindings)
			out << "settings.controls.keybind." << action << "=" << key << "\n";
		// Gameplay
		out << "settings.gameplay.auto_loot="        << (m_state.gameplay.autoLoot    ? "true" : "false") << "\n";
		out << "settings.gameplay.combat_text="      << (m_state.gameplay.combatText  ? "true" : "false") << "\n";
		out << "settings.gameplay.timestamps="       << (m_state.gameplay.timestamps  ? "true" : "false") << "\n";
		out << "settings.gameplay.camera_distance="  << m_state.gameplay.cameraDistance << "\n";

		if (!engine::platform::FileSystem::WriteAllTextContent(config, m_settingsRelativePath, out.str()))
		{
			m_state.statusMessage = "Failed to save settings.";
			LOG_ERROR(Core, "[SettingsUi] Save FAILED (path={})", m_settingsRelativePath);
			return false;
		}

		m_state.dirty         = false;
		m_state.statusMessage = "Settings saved.";
		LOG_INFO(Core, "[SettingsUi] Settings saved to {}", m_settingsRelativePath);
		return true;
	}

	void SettingsUiPresenter::ResetToDefaults()
	{
		// Preserve keybindings (rebuilt from defaults below) then reset everything.
		const auto savedKeybindings = m_state.controls.keybindings;
		ApplyBuiltinDefaults();
		m_state.dirty         = true;
		m_state.statusMessage = "Settings reset to defaults.";
		LOG_INFO(Core, "[SettingsUi] Settings reset to defaults");
	}

	// ── Private ───────────────────────────────────────────────────────────────

	void SettingsUiPresenter::ApplyBuiltinDefaults()
	{
		m_state.graphics = {};
		m_state.audio    = {};
		m_state.controls = {};
		m_state.gameplay = {};

		// Built-in keybinding defaults (overridden by default_keybindings.json if present).
		auto& kb = m_state.controls.keybindings;
		kb["move_forward"]  = "W";
		kb["move_backward"] = "S";
		kb["move_left"]     = "A";
		kb["move_right"]    = "D";
		kb["jump"]          = "Space";
		kb["interact"]      = "F";
		kb["attack"]        = "Mouse1";
		kb["skill_1"]       = "1";
		kb["skill_2"]       = "2";
		kb["skill_3"]       = "3";
		kb["skill_4"]       = "4";
		kb["map"]           = "M";
		kb["inventory"]     = "I";
		kb["quest_log"]     = "J";
		kb["chat"]          = "Return";
	}

	void SettingsUiPresenter::LoadDefaultKeybindings(const engine::core::Config& config,
	                                                 std::string_view relativePath)
	{
		const auto fullPath = engine::platform::FileSystem::ResolveContentPath(config, relativePath);
		if (!engine::platform::FileSystem::Exists(fullPath))
		{
			LOG_DEBUG(Core, "[SettingsUi] Default keybindings file missing ({}), using built-in defaults",
			    relativePath);
			return;
		}

		engine::core::Config kbCfg;
		if (!kbCfg.LoadFromFile(fullPath.string()))
		{
			LOG_WARN(Core, "[SettingsUi] Default keybindings JSON parse failed ({})", relativePath);
			return;
		}

		// Expected format: flat keys "bindings.<action>=<key>"
		// We don't enumerate all actions; instead we check a known set.
		static constexpr const char* kActions[] = {
		    "move_forward", "move_backward", "move_left", "move_right",
		    "jump", "interact", "attack",
		    "skill_1", "skill_2", "skill_3", "skill_4",
		    "map", "inventory", "quest_log", "chat"
		};
		for (const char* action : kActions)
		{
			const std::string cfgKey = std::string("bindings.") + action;
			if (kbCfg.Has(cfgKey))
				m_state.controls.keybindings[action] = kbCfg.GetString(cfgKey);
		}
		LOG_INFO(Core, "[SettingsUi] Default keybindings loaded from {}", relativePath);
	}

	void SettingsUiPresenter::LoadUserSettings(const engine::core::Config& config,
	                                           std::string_view relativePath)
	{
		const auto fullPath = engine::platform::FileSystem::ResolveContentPath(config, relativePath);
		if (!engine::platform::FileSystem::Exists(fullPath))
		{
			LOG_DEBUG(Core, "[SettingsUi] No user settings file found ({}), using defaults",
			    relativePath);
			return;
		}

		engine::core::Config cfg;
		if (!cfg.LoadFromFile(fullPath.string()))
		{
			LOG_WARN(Core, "[SettingsUi] User settings JSON parse failed ({}), using defaults",
			    relativePath);
			return;
		}

		// Graphics
		m_state.graphics.resolutionWidth  = static_cast<uint32_t>(
		    cfg.GetInt("settings.graphics.resolution_width",  1920));
		m_state.graphics.resolutionHeight = static_cast<uint32_t>(
		    cfg.GetInt("settings.graphics.resolution_height", 1080));
		m_state.graphics.qualityPreset = QualityPresetFromString(
		    cfg.GetString("settings.graphics.quality_preset", "High"));
		m_state.graphics.vsync      = cfg.GetBool("settings.graphics.vsync",      true);
		m_state.graphics.fullscreen = cfg.GetBool("settings.graphics.fullscreen", false);
		m_state.graphics.fov = Clamp(
		    static_cast<float>(cfg.GetDouble("settings.graphics.fov", 70.0)), 60.0f, 120.0f);

		// Audio
		m_state.audio.masterVolume = Clamp(
		    static_cast<float>(cfg.GetDouble("settings.audio.master_volume", 80.0)), 0.0f, 100.0f);
		m_state.audio.sfxVolume    = Clamp(
		    static_cast<float>(cfg.GetDouble("settings.audio.sfx_volume",    80.0)), 0.0f, 100.0f);
		m_state.audio.musicVolume  = Clamp(
		    static_cast<float>(cfg.GetDouble("settings.audio.music_volume",  60.0)), 0.0f, 100.0f);
		m_state.audio.voiceVolume  = Clamp(
		    static_cast<float>(cfg.GetDouble("settings.audio.voice_volume",  80.0)), 0.0f, 100.0f);
		m_state.audio.outputDevice = cfg.GetString("settings.audio.output_device");

		// Controls
		float sens = static_cast<float>(
		    cfg.GetDouble("settings.controls.mouse_sensitivity", 1.0));
		if (sens > 0.0f) m_state.controls.mouseSensitivity = sens;
		m_state.controls.invertY = cfg.GetBool("settings.controls.invert_y", false);

		// Keybindings: override defaults where present in the file.
		for (auto& [action, defaultKey] : m_state.controls.keybindings)
		{
			const std::string cfgKey = "settings.controls.keybind." + action;
			if (cfg.Has(cfgKey))
				defaultKey = cfg.GetString(cfgKey);
		}

		// Gameplay
		m_state.gameplay.autoLoot   = cfg.GetBool("settings.gameplay.auto_loot",   false);
		m_state.gameplay.combatText = cfg.GetBool("settings.gameplay.combat_text",  true);
		m_state.gameplay.timestamps = cfg.GetBool("settings.gameplay.timestamps",   false);
		m_state.gameplay.cameraDistance = Clamp(
		    static_cast<float>(cfg.GetDouble("settings.gameplay.camera_distance", 5.0)), 2.0f, 10.0f);

		LOG_INFO(Core, "[SettingsUi] User settings loaded from {}", relativePath);
	}

} // namespace engine::client
