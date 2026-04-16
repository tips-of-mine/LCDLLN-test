#pragma once

#include "engine/core/Config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
	// =========================================================================
	// HUD element types and constants (M39.3)
	// =========================================================================

	/// Minimum allowed scale factor for any HUD element (spec: 0.5).
	inline constexpr float kHudScaleMin = 0.5f;
	/// Maximum allowed scale factor for any HUD element (spec: 2.0).
	inline constexpr float kHudScaleMax = 2.0f;
	/// Grid snap increment in normalised screen units (10 px at 1080p ≈ 0.0093).
	inline constexpr float kHudSnapGridPx = 10.0f;

	/// Corner of an HUD element used as a resize handle (M39.3).
	enum class HudResizeCorner : uint8_t
	{
		None        = 0,
		TopLeft     = 1,
		TopRight    = 2,
		BottomLeft  = 3,
		BottomRight = 4,
	};

	// =========================================================================
	// HUD element layout data (M39.3)
	// =========================================================================

	/// Persistent layout for one HUD element (M39.3).
	///
	/// Position is expressed as a normalised fraction of the viewport
	/// ([0.0, 1.0] for both X and Y), so it is resolution-independent.
	/// Scale is a uniform multiplier in [kHudScaleMin, kHudScaleMax].
	struct HudElementLayout
	{
		/// Stable identifier.  Known elements: player_frame, target_frame,
		/// minimap, action_bars, chat.
		std::string id;

		/// Horizontal anchor as fraction of viewport width  [0.0, 1.0].
		float posX  = 0.0f;
		/// Vertical anchor as fraction of viewport height [0.0, 1.0].
		float posY  = 0.0f;

		/// Uniform scale multiplier [kHudScaleMin, kHudScaleMax].
		float scale = 1.0f;

		/// When true the element cannot be dragged or resized in edit mode.
		bool locked = false;
	};

	// =========================================================================
	// Edit-mode runtime state (M39.3)
	// =========================================================================

	/// Full runtime state exposed by HudLayoutEditor for debug / UI rendering.
	struct HudLayoutEditorState
	{
		/// All known HUD element layouts in their current state.
		std::vector<HudElementLayout> elements;

		/// True while the layout editor is active (borders shown, elements draggable).
		bool editModeActive = false;

		/// ID of the element currently being dragged / resized; empty when idle.
		std::string activeElementId;

		/// True during an ongoing drag operation.
		bool isDragging  = false;
		/// True during an ongoing resize operation.
		bool isResizing  = false;
		/// Corner handle being used for the current resize.
		HudResizeCorner resizeCorner = HudResizeCorner::None;

		/// True when snap-to-grid is enabled (optional feature, spec step 5).
		bool snapToGrid  = false;

		/// Human-readable debug dump for debug overlay rendering.
		std::string debugText;
	};

	// =========================================================================
	// HudLayoutEditor presenter (M39.3)
	// =========================================================================

	/// CPU-side HUD layout editor presenter (M39.3).
	///
	/// Manages the in-game HUD customisation flow:
	///   - Edit mode toggle (show element borders, enable drag/resize)
	///   - Drag & drop repositioning of HUD elements
	///   - Corner-handle resizing (scale change)
	///   - Per-character layout save/load (content-relative JSON)
	///   - Layout presets (default / minimal / compact)
	///   - Optional snap-to-grid (10 px increments)
	///
	/// Layout JSON format (per ticket spec: elementId, x, y, scale):
	/// ```json
	/// { "elements": [{ "id": "player_frame", "x": 0.01, "y": 0.80,
	///                   "scale": 1.0, "locked": false }, ...] }
	/// ```
	///
	/// Content paths are resolved via `paths.content` (config.json).
	/// Per-character save path: `hud/layouts/character_<characterId>.json`.
	/// Preset paths:            `hud/layouts/<presetName>.json`.
	class HudLayoutEditor final
	{
	public:
		HudLayoutEditor() = default;
		~HudLayoutEditor();

		HudLayoutEditor(const HudLayoutEditor&) = delete;
		HudLayoutEditor& operator=(const HudLayoutEditor&) = delete;

		// ---- Lifecycle ----

		/// Initialise the editor and load the default preset from content.
		/// Must be called before any other method.
		/// @param config  Engine configuration used to resolve content paths.
		/// @return true on success; false if the default preset could not be read
		///         (falls back to hardcoded defaults and still returns true with
		///         a warning — this is non-fatal).
		bool Init(const engine::core::Config& config);

		/// Shutdown the editor, discarding all unsaved state.
		void Shutdown();

		// ---- Viewport ----

		/// Notify the editor of the current viewport size in pixels.
		/// Required for pixel ↔ normalised-fraction conversions.
		void SetViewportSize(uint32_t width, uint32_t height);

		// ---- Edit mode ----

		/// Toggle the layout editor on/off.
		/// In edit mode all elements show borders and are draggable/resizable.
		void ToggleEditMode();

		bool IsEditModeActive() const { return m_state.editModeActive; }

		// ---- Preset loading ----

		/// Load a named preset from `hud/layouts/<presetName>.json`.
		/// Known names: "default", "minimal", "compact".
		/// Returns true on success; warns and keeps current layout on failure.
		bool LoadPreset(const engine::core::Config& config, const std::string& presetName);

		/// Reset to the "default" preset.  Convenience wrapper around LoadPreset.
		bool ResetToDefault(const engine::core::Config& config);

		// ---- Per-character persistence ----

		/// Load a character-specific layout from `hud/layouts/character_<id>.json`.
		/// Falls back to the default preset when the file is missing.
		bool LoadCharacterLayout(const engine::core::Config& config, uint64_t characterId);

		/// Save the current layout to `hud/layouts/character_<id>.json`.
		bool SaveCharacterLayout(const engine::core::Config& config, uint64_t characterId) const;

		// ---- Element access ----

		/// Return the current layout state.
		const HudLayoutEditorState& GetState() const { return m_state; }

		/// Find a layout element by id; returns nullptr when not found.
		const HudElementLayout* FindElement(const std::string& id) const;

		// ---- Direct element manipulation ----

		/// Set the normalised position of an element directly.
		/// posX / posY are clamped to [0.0, 1.0].
		bool SetElementPosition(const std::string& id, float posX, float posY);

		/// Set the scale of an element directly.
		/// Clamped to [kHudScaleMin, kHudScaleMax].
		bool SetElementScale(const std::string& id, float scale);

		/// Lock or unlock an element.
		bool SetElementLocked(const std::string& id, bool locked);

		// ---- Drag operations (step 3) ----

		/// Begin dragging \p elementId.
		/// \p mouseNX, \p mouseNY  — normalised mouse position [0, 1] this frame.
		/// Returns false when the element is locked or not found.
		bool BeginDrag(const std::string& elementId, float mouseNX, float mouseNY);

		/// Update an ongoing drag with the current normalised mouse position.
		void UpdateDrag(float mouseNX, float mouseNY);

		/// Finish the drag and keep the new position.
		void EndDrag();

		/// Cancel the drag and restore the element's original position.
		void CancelDrag();

		// ---- Resize operations (step 4) ----

		/// Begin resizing \p elementId from \p corner.
		/// \p mouseNX, \p mouseNY — normalised mouse position [0, 1].
		/// Returns false when the element is locked or not found.
		bool BeginResize(const std::string& elementId, HudResizeCorner corner,
		                 float mouseNX, float mouseNY);

		/// Update an ongoing resize with the current normalised mouse position.
		void UpdateResize(float mouseNX, float mouseNY);

		/// Finish the resize and keep the new scale.
		void EndResize();

		/// Cancel the resize and restore the original scale.
		void CancelResize();

		// ---- Snap to grid (optional, step 5) ----

		/// Enable or disable snap-to-grid (10 px increments).
		void SetSnapToGrid(bool enabled) { m_state.snapToGrid = enabled; }

		// ---- Debug ----

		/// Rebuild the debug text dump (call before reading m_state.debugText).
		void RebuildDebugText();

		bool IsInitialized() const { return m_initialized; }

	private:
		// ---- Helpers ----

		/// Find a mutable layout element by id; returns nullptr when not found.
		HudElementLayout* FindElementMut(const std::string& id);

		/// Load layout elements from a JSON file already read into \p text.
		/// Populates \p outElements; returns true when at least one element loaded.
		bool ParseLayoutJson(const std::string& jsonText,
		                     std::vector<HudElementLayout>& outElements);

		/// Serialise the current elements to a JSON string.
		std::string SerialiseLayoutJson() const;

		/// Apply snap-to-grid to a position value (in normalised space).
		float SnapPosition(float pos) const;

		/// Clamp a scale value to [kHudScaleMin, kHudScaleMax].
		static float ClampScale(float s);

		/// Clamp a normalised position to [0.0, 1.0].
		static float ClampPos(float p);

		// ---- State ----

		HudLayoutEditorState m_state{};

		uint32_t m_viewportWidth  = 1920;
		uint32_t m_viewportHeight = 1080;

		// ---- Drag state ----
		float m_dragStartMouseNX  = 0.0f;
		float m_dragStartMouseNY  = 0.0f;
		float m_dragStartPosX     = 0.0f;
		float m_dragStartPosY     = 0.0f;

		// ---- Resize state ----
		float m_resizeStartMouseNX = 0.0f;
		float m_resizeStartMouseNY = 0.0f;
		float m_resizeStartScale   = 1.0f;

		bool m_initialized = false;
	};

} // namespace engine::client
