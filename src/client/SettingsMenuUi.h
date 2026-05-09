#pragma once

#include "engine/core/Config.h"
#include "engine/platform/Input.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::client
{
	// =========================================================================
	// Settings tab enum (M39.2)
	// =========================================================================

	enum class SettingsTab : uint8_t
	{
		Graphics = 0,
		Audio    = 1,
		Controls = 2,
		Gameplay = 3,
	};

	// =========================================================================
	// Per-category settings structs
	// =========================================================================

	/// Graphics settings (M39.2).
	struct GraphicsSettings
	{
		int   resolutionWidth  = 1920;
		int   resolutionHeight = 1080;
		/// 0 = Low, 1 = Medium, 2 = High, 3 = Ultra.
		int   qualityPreset    = 2;
		bool  vsync            = true;
		bool  fullscreen       = true;
		/// Vertical field-of-view in degrees [60, 120].
		float fov              = 70.0f;
	};

	/// Audio settings (M39.2).
	struct AudioSettings
	{
		/// [0.0, 1.0] — master volume multiplier.
		float masterVolume = 1.0f;
		/// [0.0, 1.0] — music bus volume multiplier.
		float musicVolume  = 0.8f;
		/// [0.0, 1.0] — SFX bus volume multiplier.
		float sfxVolume    = 1.0f;
		/// [0.0, 1.0] — voice/dialogue volume multiplier.
		float voiceVolume  = 1.0f;
		/// Index into the available output device list (0 = system default).
		int   outputDevice = 0;
	};

	/// One rebindable key action (M39.2).
	struct KeyBinding
	{
		std::string              actionId;     ///< Stable identifier, e.g. "move_forward".
		std::string              displayName;  ///< Human-readable name, e.g. "Move Forward".
		engine::platform::Key   currentKey;   ///< Currently bound key.
		engine::platform::Key   defaultKey;   ///< Factory-default binding.
	};

	/// Controls / keybindings settings (M39.2).
	struct ControlsSettings
	{
		/// Mouse sensitivity in radians per pixel [0.0001, 0.01].
		float mouseSensitivity = 0.002f;
		bool  invertY          = false;
		std::vector<KeyBinding> keybindings;
	};

	/// Gameplay settings (M39.2).
	struct GameplaySettings
	{
		bool  autoLoot        = true;
		bool  combatText      = true;
		bool  timestamps      = false;
		/// Camera follow distance in metres [2.0, 10.0].
		float cameraDistance  = 5.0f;
	};

	// =========================================================================
	// Pending-apply command structs (consumed by Engine)
	// =========================================================================

	/// Command produced when the player applies graphics settings (M39.2).
	struct GraphicsSettingsCmd
	{
		bool          applyRequested  = false;
		bool          restartRequired = false; ///< e.g. resolution change on some platforms.
		GraphicsSettings settings;
	};

	/// Command produced when the player applies audio settings (M39.2).
	struct AudioSettingsCmd
	{
		bool applyRequested = false;
		AudioSettings settings;
	};

	/// Command produced when the player applies controls / keybindings (M39.2).
	struct ControlsSettingsCmd
	{
		bool applyRequested = false;
		ControlsSettings settings;
	};

	/// Command produced when the player applies gameplay settings (M39.2).
	struct GameplaySettingsCmd
	{
		bool applyRequested = false;
		GameplaySettings settings;
	};

	// =========================================================================
	// SettingsMenuPresenter
	// =========================================================================

	/// CPU-side presenter for the in-game settings menu (M39.2).
	///
	/// Manages four tabs (Graphics / Audio / Controls / Gameplay), holds
	/// current settings values, validates input, tracks keybinding conflicts,
	/// and produces pending-apply commands for the Engine to consume.
	///
	/// Keybinding defaults are loaded from the content-relative path
	/// `settings/default_keybindings.json` (key overridable via
	/// `settings.keybindings_path` in config.json).
	///
	/// Persistence: call \ref SaveToFile to write `user_settings.json`
	/// (flat JSON, same format read by Engine::LoadUserSettings).
	class SettingsMenuPresenter final
	{
	public:
		SettingsMenuPresenter() = default;
		~SettingsMenuPresenter();

		SettingsMenuPresenter(const SettingsMenuPresenter&) = delete;
		SettingsMenuPresenter& operator=(const SettingsMenuPresenter&) = delete;

		// ---- Lifecycle ----

		/// Load current values from \p config and keybinding defaults from the
		/// content-relative JSON file. Returns true on full success; partial
		/// success (e.g. missing keybindings file) still returns true but logs
		/// a warning.
		bool Init(const engine::core::Config& config);

		/// Release all resources and reset to default state.
		void Shutdown();

		// ---- Visibility ----

		/// True when the settings menu is open (visible).
		bool IsOpen() const { return m_isOpen; }

		void Open()  { m_isOpen = true;  }
		void Close() { m_isOpen = false; }

		// ---- Tab selection ----

		void         SetActiveTab(SettingsTab tab) { m_activeTab = tab; }
		SettingsTab  GetActiveTab() const          { return m_activeTab; }

		// ---- Graphics setters ----

		void SetResolution(int width, int height);
		void SetQualityPreset(int preset);     ///< 0=Low 1=Med 2=High 3=Ultra
		void SetVsync(bool vsync);
		void SetFullscreen(bool fullscreen);
		void SetFov(float fovDegrees);         ///< Clamped to [60, 120].

		// ---- Audio setters ----

		void SetMasterVolume(float v);         ///< Clamped to [0, 1].
		void SetMusicVolume(float v);
		void SetSfxVolume(float v);
		void SetVoiceVolume(float v);
		void SetOutputDevice(int deviceIndex);

		// ---- Controls setters ----

		void SetMouseSensitivity(float s);     ///< Clamped to [0.0001, 0.01].
		void SetInvertY(bool invert);

		/// Start rebinding \p actionId; the next OnKeyPressed() call completes it.
		/// Returns false if the actionId is unknown.
		bool BeginRebind(const std::string& actionId);

		/// Feed a raw key-press event.  If a rebind is in progress, finalise
		/// it and return true; otherwise return false (event not consumed).
		/// Conflict detection: if the key is already bound to another action the
		/// rebind is rejected and \p outConflict is set to that action's displayName.
		bool OnKeyPressed(engine::platform::Key key, std::string& outConflict);

		/// Cancel an in-progress rebind without changing the binding.
		void CancelRebind();

		/// True when waiting for the next key press to complete a rebind.
		bool IsRebinding() const { return !m_rebindingAction.empty(); }

		/// actionId currently being rebound (empty when not rebinding).
		const std::string& GetRebindingActionId() const { return m_rebindingAction; }

		/// Reset all keybindings to factory defaults.
		void ResetKeybindingsToDefault();

		// ---- Gameplay setters ----

		void SetAutoLoot(bool v);
		void SetCombatText(bool v);
		void SetTimestamps(bool v);
		void SetCameraDistance(float metres);  ///< Clamped to [2, 10].

		// ---- State accessors (read-only) ----

		const GraphicsSettings& GetGraphics() const { return m_graphics; }
		const AudioSettings&    GetAudio()    const { return m_audio; }
		const ControlsSettings& GetControls() const { return m_controls; }
		const GameplaySettings& GetGameplay() const { return m_gameplay; }

		// ---- Apply / consume ----

		/// Mark current graphics settings as pending application.
		void ApplyGraphics();
		/// Mark current audio settings as pending application.
		void ApplyAudio();
		/// Mark current controls / keybindings as pending application.
		void ApplyControls();
		/// Mark current gameplay settings as pending application.
		void ApplyGameplay();

		/// Consume the pending graphics command (clears the pending flag).
		GraphicsSettingsCmd ConsumePendingGraphics();
		/// Consume the pending audio command.
		AudioSettingsCmd    ConsumePendingAudio();
		/// Consume the pending controls command.
		ControlsSettingsCmd ConsumePendingControls();
		/// Consume the pending gameplay command.
		GameplaySettingsCmd ConsumePendingGameplay();

		// ---- Persistence ----

		/// Write all current settings to \p filePath as flat JSON (same format
		/// read by Engine::LoadUserSettings).  Returns true on success.
		bool SaveToFile(const std::filesystem::path& filePath) const;

		// ---- Debug ----

		/// Text dump of the current settings state for debug overlay rendering.
		std::string BuildDebugText() const;

		bool IsInitialized() const { return m_initialized; }

	private:
		/// Load keybinding defaults from the content-relative JSON path.
		bool LoadKeybindings(const engine::core::Config& config);

		/// Return a mutable pointer to a KeyBinding by actionId, or nullptr.
		KeyBinding* FindBinding(const std::string& actionId);

		/// Return a const pointer to a KeyBinding by actionId, or nullptr.
		const KeyBinding* FindBinding(const std::string& actionId) const;

		static float Clamp01(float v);
		static float ClampRange(float v, float lo, float hi);

		// ---- State ----

		GraphicsSettings m_graphics;
		AudioSettings    m_audio;
		ControlsSettings m_controls;
		GameplaySettings m_gameplay;

		SettingsTab  m_activeTab = SettingsTab::Graphics;
		bool         m_isOpen   = false;

		/// Default keybindings loaded at Init (used by ResetKeybindingsToDefault).
		std::vector<KeyBinding> m_defaultBindings;

		/// actionId of the binding being rebound; empty when none.
		std::string m_rebindingAction;

		// ---- Pending apply flags ----

		bool m_graphicsPending  = false;
		bool m_audioPending     = false;
		bool m_controlsPending  = false;
		bool m_gameplayPending  = false;

		bool m_initialized = false;
	};

} // namespace engine::client
