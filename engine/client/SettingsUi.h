#pragma once

#include "engine/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::client
{
	/// Settings panel tabs (M39.2).
	enum class SettingsTab : uint8_t
	{
		Graphics = 0,
		Audio    = 1,
		Controls = 2,
		Gameplay = 3
	};

	/// Graphics quality preset levels (M39.2).
	enum class QualityPreset : uint8_t
	{
		Low   = 0,
		Medium = 1,
		High  = 2,
		Ultra = 3
	};

	/// Graphics settings (M39.2).
	struct GraphicsSettings
	{
		uint32_t     resolutionWidth  = 1920;
		uint32_t     resolutionHeight = 1080;
		QualityPreset qualityPreset   = QualityPreset::High;
		bool         vsync            = true;
		bool         fullscreen       = false;
		float        fov              = 70.0f; ///< Field of view in degrees [60, 120].
	};

	/// Audio settings (M39.2).
	struct AudioSettings
	{
		float        masterVolume = 80.0f;  ///< [0, 100]
		float        sfxVolume    = 80.0f;  ///< [0, 100]
		float        musicVolume  = 60.0f;  ///< [0, 100]
		float        voiceVolume  = 80.0f;  ///< [0, 100]
		std::string  outputDevice;          ///< Empty = system default.
	};

	/// Controls settings (M39.2).
	struct ControlsSettings
	{
		float mouseSensitivity = 1.0f; ///< Multiplier > 0.
		bool  invertY          = false;
		/// Action name → key name mapping (e.g. "move_forward" → "W").
		std::unordered_map<std::string, std::string> keybindings;
	};

	/// Gameplay settings (M39.2).
	struct GameplaySettings
	{
		bool  autoLoot        = false;
		bool  combatText      = true;
		bool  timestamps      = false;
		float cameraDistance  = 5.0f;  ///< Metres [2, 10].
	};

	/// State of the settings panel produced by SettingsUiPresenter (M39.2).
	struct SettingsPanelState
	{
		SettingsTab activeTab = SettingsTab::Graphics;

		GraphicsSettings graphics{};
		AudioSettings    audio{};
		ControlsSettings controls{};
		GameplaySettings gameplay{};

		/// True when the currently active tab has unsaved changes.
		bool dirty = false;

		/// When non-empty: the action currently waiting for a new key assignment.
		std::string rebindingAction;

		/// Conflict message set when a keybinding would clash with an existing one.
		std::string keybindConflict;

		/// Human-readable status line shown at the bottom of the settings panel.
		std::string statusMessage;
	};

	/// Settings UI presenter: manages the settings panel state, persists to a
	/// local config file, and provides keybinding rebind with conflict detection (M39.2).
	class SettingsUiPresenter final
	{
	public:
		SettingsUiPresenter()                              = default;
		~SettingsUiPresenter();

		SettingsUiPresenter(const SettingsUiPresenter&)    = delete;
		SettingsUiPresenter& operator=(const SettingsUiPresenter&) = delete;

		/// Initialise the presenter.
		/// Loads user settings from \p settingsRelativePath (relative to paths.content).
		/// If the file is missing the built-in defaults are used.
		/// Default keybindings are loaded from \p defaultKeybindingsRelativePath.
		bool Init(const engine::core::Config& config,
		          std::string_view settingsRelativePath           = "settings/user_settings.json",
		          std::string_view defaultKeybindingsRelativePath = "settings/default_keybindings.json");

		/// Shutdown and release resources.
		void Shutdown();

		/// Switch the visible tab.
		void SetTab(SettingsTab tab);

		// ── Graphics ────────────────────────────────────────────────────────
		void SetResolution(uint32_t width, uint32_t height);
		void SetQualityPreset(QualityPreset preset);
		void SetVsync(bool enabled);
		void SetFullscreen(bool enabled);
		void SetFov(float degrees);      ///< Clamped to [60, 120].

		// ── Audio ────────────────────────────────────────────────────────────
		void SetMasterVolume(float v);   ///< Clamped to [0, 100].
		void SetSfxVolume(float v);
		void SetMusicVolume(float v);
		void SetVoiceVolume(float v);
		void SetOutputDevice(std::string_view device);

		// ── Controls ─────────────────────────────────────────────────────────
		void SetMouseSensitivity(float v); ///< Must be > 0.
		void SetInvertY(bool enabled);

		/// Begin rebinding an action.  The presenter enters a "waiting for key" state.
		void StartRebind(std::string_view action);

		/// Commit the new key for the action currently being rebound.
		/// Returns false and sets m_state.keybindConflict if the key is already used.
		bool CommitRebind(std::string_view newKey);

		/// Cancel an in-progress rebind.
		void CancelRebind();

		// ── Gameplay ─────────────────────────────────────────────────────────
		void SetAutoLoot(bool enabled);
		void SetCombatText(bool enabled);
		void SetTimestamps(bool enabled);
		void SetCameraDistance(float metres); ///< Clamped to [2, 10].

		// ── Persistence ──────────────────────────────────────────────────────

		/// Persist current settings to the configured local settings file.
		/// Returns false on write failure (m_state.statusMessage updated).
		bool Save(const engine::core::Config& config);

		/// Restore all settings to built-in defaults.
		void ResetToDefaults();

		/// Immutable panel state.
		const SettingsPanelState& GetState() const { return m_state; }

		bool IsInitialized() const { return m_initialized; }

	private:
		/// Load persisted user settings (non-fatal: falls back to defaults).
		void LoadUserSettings(const engine::core::Config& config,
		                      std::string_view relativePath);

		/// Load default keybindings from the content JSON.
		void LoadDefaultKeybindings(const engine::core::Config& config,
		                            std::string_view relativePath);

		/// Apply built-in default values to all settings structs.
		void ApplyBuiltinDefaults();

		/// Clamp a float to [lo, hi].
		static float Clamp(float v, float lo, float hi)
		{
			return v < lo ? lo : (v > hi ? hi : v);
		}

		SettingsPanelState m_state{};
		std::string        m_settingsRelativePath;
		bool               m_initialized = false;
	};

} // namespace engine::client
