#pragma once

#include "engine/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::client
{
	/// Identifiers for the HUD elements that can be repositioned/scaled (M39.3).
	enum class HudElementId : uint8_t
	{
		PlayerFrame  = 0,
		TargetFrame  = 1,
		Minimap      = 2,
		ActionBars   = 3,
		Chat         = 4,
		Count
	};

	/// Built-in layout presets (M39.3).
	enum class HudLayoutPreset : uint8_t
	{
		Default = 0,  ///< Centered elements, comfortable for most displays.
		Minimal = 1,  ///< Small scale, compact corners.
		Compact = 2   ///< Condensed elements, maximum screen real-estate.
	};

	/// Per-element layout data stored and loaded from JSON (M39.3).
	/// Position is expressed as a percentage [0, 1] of the viewport size.
	struct HudElementLayout
	{
		float xPct  = 0.0f;  ///< Horizontal anchor as fraction of viewport width  [0, 1].
		float yPct  = 0.0f;  ///< Vertical anchor as fraction of viewport height   [0, 1].
		float scale = 1.0f;  ///< Uniform scale applied to the element             [0.5, 2.0].
		bool  locked = false; ///< When true, element cannot be moved/resized in edit mode.
	};

	/// Complete HUD layout – one entry per element (M39.3).
	using HudLayout = std::unordered_map<HudElementId, HudElementLayout>;

	/// Mouse interaction state for the drag/resize editor (M39.3).
	struct HudDragState
	{
		bool        active     = false;
		HudElementId elementId = HudElementId::Count; ///< Count = no element.
		bool        isResize   = false;  ///< false = drag-move, true = corner-resize.
		/// Offset between the mouse-down point and the element anchor (NDC %).
		float       grabOffsetX = 0.0f;
		float       grabOffsetY = 0.0f;
	};

	/// Fully resolved editor state returned to the rendering layer (M39.3).
	struct HudLayoutEditorState
	{
		bool editModeActive = false;  ///< When true, show handles and borders.
		HudLayout       layout{};
		HudDragState    drag{};
		std::string     statusMessage;
	};

	/// HUD layout editor: manages per-element position/scale, drag-and-drop,
	/// layout presets, and JSON persistence per character (M39.3).
	///
	/// Coordinate system:
	///   - Positions are stored as fractions [0, 1] of the viewport (x%, y%).
	///   - The rendering layer converts these to pixel coordinates at draw time.
	class HudLayoutEditor final
	{
	public:
		/// Scale limits per spec: min 0.5, max 2.0.
		static constexpr float kScaleMin = 0.5f;
		static constexpr float kScaleMax = 2.0f;

		HudLayoutEditor()                            = default;
		~HudLayoutEditor();

		HudLayoutEditor(const HudLayoutEditor&)      = delete;
		HudLayoutEditor& operator=(const HudLayoutEditor&) = delete;

		/// Initialise the editor.
		/// Attempts to load the character layout from
		///   `hud/layouts/char_<characterId>.json` (relative to paths.content).
		/// Falls back to the Default preset when the file is absent.
		/// \param characterId  Stable character identifier used to name the save file.
		bool Init(const engine::core::Config& config, uint32_t characterId = 0);

		/// Shutdown and release resources.
		void Shutdown();

		// ── Edit mode ─────────────────────────────────────────────────────────

		/// Toggle the edit mode on/off.
		void ToggleEditMode();

		/// Enable or disable edit mode explicitly.
		void SetEditMode(bool active);

		bool IsEditMode() const { return m_state.editModeActive; }

		// ── Drag / resize ─────────────────────────────────────────────────────

		/// Begin dragging the element whose anchor is closest to (mouseXPct, mouseYPct).
		/// \param isResize  true = resize interaction, false = move interaction.
		/// \return false when no element is near enough, or the element is locked.
		bool BeginDrag(float mouseXPct, float mouseYPct, bool isResize = false);

		/// Update the dragged element's position (or scale when resizing).
		/// \param snapToGrid  When true, quantise position to 10-pixel grid (optional spec).
		void UpdateDrag(float mouseXPct, float mouseYPct, float viewportWidth, float viewportHeight,
		                bool snapToGrid = false);

		/// Finish the current drag interaction.
		void EndDrag();

		// ── Element manipulation ──────────────────────────────────────────────

		/// Directly set position for an element.
		void SetPosition(HudElementId id, float xPct, float yPct);

		/// Set scale for an element (clamped to [kScaleMin, kScaleMax]).
		void SetScale(HudElementId id, float scale);

		/// Toggle the locked flag for an element.
		void SetLocked(HudElementId id, bool locked);

		// ── Presets ───────────────────────────────────────────────────────────

		/// Load a built-in layout preset, discarding the current layout.
		void ApplyPreset(HudLayoutPreset preset);

		/// Reload the Default preset (reset button shortcut).
		void ResetToDefault();

		// ── Persistence ───────────────────────────────────────────────────────

		/// Save the current layout to
		///   `hud/layouts/char_<characterId>.json` (paths.content relative).
		/// Returns false on write failure.
		bool SaveLayout(const engine::core::Config& config) const;

		/// Load a layout from
		///   `hud/layouts/char_<characterId>.json`, or apply Default on failure.
		bool LoadLayout(const engine::core::Config& config);

		/// Return the current immutable state.
		const HudLayoutEditorState& GetState() const { return m_state; }

		bool IsInitialized() const { return m_initialized; }

		/// Human-readable name for a HUD element (for debug/logging).
		static const char* ElementName(HudElementId id);

	private:
		/// Populate m_state.layout with the Default preset values.
		static void BuildDefaultLayout(HudLayout& out);

		/// Populate m_state.layout with the Minimal preset values.
		static void BuildMinimalLayout(HudLayout& out);

		/// Populate m_state.layout with the Compact preset values.
		static void BuildCompactLayout(HudLayout& out);

		/// Clamp a float to [lo, hi].
		static float Clamp(float v, float lo, float hi)
		{
			return v < lo ? lo : (v > hi ? hi : v);
		}

		/// Build the relative path used to save/load the character layout file.
		std::string CharLayoutPath() const;

		HudLayoutEditorState m_state{};
		engine::core::Config m_config{};
		uint32_t             m_characterId = 0;
		bool                 m_initialized = false;
	};

} // namespace engine::client
