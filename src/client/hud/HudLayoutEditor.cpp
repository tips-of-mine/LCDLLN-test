#include "src/client/hud/HudLayoutEditor.h"

#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace engine::client
{
	// =========================================================================
	// Hardcoded default element list (fallback when the JSON cannot be read)
	// =========================================================================

	namespace
	{
		/// Returns the built-in default element layouts used when the preset JSON
		/// file is missing or unreadable.
		std::vector<HudElementLayout> MakeBuiltinDefaults()
		{
			return {
				{ "player_frame", 0.01f,  0.80f, 1.0f, false },
				{ "target_frame", 0.20f,  0.80f, 1.0f, false },
				{ "minimap",      0.835f, 0.02f, 1.0f, false },
				{ "action_bars",  0.30f,  0.92f, 1.0f, false },
				{ "chat",         0.01f,  0.60f, 1.0f, false },
			};
		}
	}

	// =========================================================================
	// Lifecycle
	// =========================================================================

	HudLayoutEditor::~HudLayoutEditor()
	{
		if (m_initialized)
			Shutdown();
	}

	bool HudLayoutEditor::Init(const engine::core::Config& config)
	{
		if (m_initialized)
			Shutdown();

		// Attempt to load the "default" preset from content.
		std::vector<HudElementLayout> loaded;
		const std::string contentRoot = config.GetString("paths.content", "game/data");
		const std::string presetPath  = contentRoot + "/hud/layouts/default.json";
		const std::string jsonText    = engine::platform::FileSystem::ReadAllText(presetPath);

		if (!jsonText.empty() && ParseLayoutJson(jsonText, loaded))
		{
			m_state.elements = std::move(loaded);
			LOG_INFO(Core, "[HudLayoutEditor] Init OK — default preset loaded ({} elements) from '{}'",
			         m_state.elements.size(), presetPath);
		}
		else
		{
			// Non-fatal: fall back to hard-coded defaults.
			m_state.elements = MakeBuiltinDefaults();
			LOG_WARN(Core, "[HudLayoutEditor] Init: default preset '{}' not loaded — using built-in defaults ({} elements)",
			         presetPath, m_state.elements.size());
		}

		m_state.editModeActive = false;
		m_state.activeElementId.clear();
		m_state.isDragging  = false;
		m_state.isResizing  = false;
		m_state.resizeCorner = HudResizeCorner::None;
		m_state.snapToGrid  = false;

		m_initialized = true;
		RebuildDebugText();
		LOG_INFO(Core, "[HudLayoutEditor] Init complete (elements={})", m_state.elements.size());
		return true;
	}

	void HudLayoutEditor::Shutdown()
	{
		m_state = HudLayoutEditorState{};
		m_initialized = false;
		LOG_INFO(Core, "[HudLayoutEditor] Shutdown complete");
	}

	// =========================================================================
	// Viewport
	// =========================================================================

	void HudLayoutEditor::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
		{
			LOG_WARN(Core, "[HudLayoutEditor] SetViewportSize: invalid size {}x{}", width, height);
			return;
		}
		m_viewportWidth  = width;
		m_viewportHeight = height;
	}

	// =========================================================================
	// Edit mode
	// =========================================================================

	void HudLayoutEditor::ToggleEditMode()
	{
		m_state.editModeActive = !m_state.editModeActive;

		// Cancel any in-progress drag/resize when leaving edit mode.
		if (!m_state.editModeActive)
		{
			CancelDrag();
			CancelResize();
		}

		LOG_INFO(Core, "[HudLayoutEditor] Edit mode {}", m_state.editModeActive ? "ON" : "OFF");
		RebuildDebugText();
	}

	// =========================================================================
	// Preset loading
	// =========================================================================

	bool HudLayoutEditor::LoadPreset(const engine::core::Config& config,
	                                 const std::string& presetName)
	{
		const std::string contentRoot = config.GetString("paths.content", "game/data");
		const std::string path = contentRoot + "/hud/layouts/" + presetName + ".json";
		const std::string jsonText = engine::platform::FileSystem::ReadAllText(path);

		if (jsonText.empty())
		{
			LOG_WARN(Core, "[HudLayoutEditor] LoadPreset: cannot read '{}'", path);
			return false;
		}

		std::vector<HudElementLayout> loaded;
		if (!ParseLayoutJson(jsonText, loaded))
		{
			LOG_WARN(Core, "[HudLayoutEditor] LoadPreset: parse failed for '{}'", path);
			return false;
		}

		m_state.elements = std::move(loaded);
		LOG_INFO(Core, "[HudLayoutEditor] Preset '{}' loaded ({} elements)", presetName, m_state.elements.size());
		RebuildDebugText();
		return true;
	}

	bool HudLayoutEditor::ResetToDefault(const engine::core::Config& config)
	{
		LOG_INFO(Core, "[HudLayoutEditor] Resetting to default preset");
		const bool ok = LoadPreset(config, "default");
		if (!ok)
		{
			// Fall back to built-in defaults.
			m_state.elements = MakeBuiltinDefaults();
			LOG_WARN(Core, "[HudLayoutEditor] ResetToDefault: using built-in defaults ({} elements)",
			         m_state.elements.size());
			RebuildDebugText();
		}
		return ok;
	}

	// =========================================================================
	// Per-character persistence
	// =========================================================================

	bool HudLayoutEditor::LoadCharacterLayout(const engine::core::Config& config,
	                                          uint64_t characterId)
	{
		const std::string contentRoot = config.GetString("paths.content", "game/data");
		const std::string relPath = "hud/layouts/character_" + std::to_string(characterId) + ".json";
		const std::string fullPath = contentRoot + "/" + relPath;
		const std::string jsonText = engine::platform::FileSystem::ReadAllText(fullPath);

		if (jsonText.empty())
		{
			// No saved layout for this character — load default preset.
			LOG_INFO(Core, "[HudLayoutEditor] No saved layout for character {} — using default", characterId);
			return ResetToDefault(config);
		}

		std::vector<HudElementLayout> loaded;
		if (!ParseLayoutJson(jsonText, loaded))
		{
			LOG_WARN(Core, "[HudLayoutEditor] LoadCharacterLayout: parse failed '{}' — using default", fullPath);
			return ResetToDefault(config);
		}

		m_state.elements = std::move(loaded);
		LOG_INFO(Core, "[HudLayoutEditor] Character {} layout loaded ({} elements) from '{}'",
		         characterId, m_state.elements.size(), fullPath);
		RebuildDebugText();
		return true;
	}

	bool HudLayoutEditor::SaveCharacterLayout(const engine::core::Config& config,
	                                          uint64_t characterId) const
	{
		const std::string relPath = "hud/layouts/character_" + std::to_string(characterId) + ".json";
		const std::string jsonText = SerialiseLayoutJson();

		if (!engine::platform::FileSystem::WriteAllTextContent(config, relPath, jsonText))
		{
			LOG_ERROR(Core, "[HudLayoutEditor] SaveCharacterLayout: cannot write '{}'", relPath);
			return false;
		}

		LOG_INFO(Core, "[HudLayoutEditor] Character {} layout saved ({} elements) to '{}'",
		         characterId, m_state.elements.size(), relPath);
		return true;
	}

	// =========================================================================
	// Element access
	// =========================================================================

	const HudElementLayout* HudLayoutEditor::FindElement(const std::string& id) const
	{
		for (const HudElementLayout& el : m_state.elements)
		{
			if (el.id == id)
				return &el;
		}
		return nullptr;
	}

	HudElementLayout* HudLayoutEditor::FindElementMut(const std::string& id)
	{
		for (HudElementLayout& el : m_state.elements)
		{
			if (el.id == id)
				return &el;
		}
		return nullptr;
	}

	// =========================================================================
	// Direct manipulation
	// =========================================================================

	bool HudLayoutEditor::SetElementPosition(const std::string& id, float posX, float posY)
	{
		HudElementLayout* el = FindElementMut(id);
		if (!el)
		{
			LOG_WARN(Core, "[HudLayoutEditor] SetElementPosition: unknown element '{}'", id);
			return false;
		}
		el->posX = ClampPos(posX);
		el->posY = ClampPos(posY);
		RebuildDebugText();
		return true;
	}

	bool HudLayoutEditor::SetElementScale(const std::string& id, float scale)
	{
		HudElementLayout* el = FindElementMut(id);
		if (!el)
		{
			LOG_WARN(Core, "[HudLayoutEditor] SetElementScale: unknown element '{}'", id);
			return false;
		}
		el->scale = ClampScale(scale);
		RebuildDebugText();
		return true;
	}

	bool HudLayoutEditor::SetElementLocked(const std::string& id, bool locked)
	{
		HudElementLayout* el = FindElementMut(id);
		if (!el)
		{
			LOG_WARN(Core, "[HudLayoutEditor] SetElementLocked: unknown element '{}'", id);
			return false;
		}
		el->locked = locked;
		LOG_INFO(Core, "[HudLayoutEditor] Element '{}' {}", id, locked ? "locked" : "unlocked");
		RebuildDebugText();
		return true;
	}

	// =========================================================================
	// Drag operations (spec step 3)
	// =========================================================================

	bool HudLayoutEditor::BeginDrag(const std::string& elementId,
	                                float mouseNX, float mouseNY)
	{
		if (!m_state.editModeActive)
			return false;

		const HudElementLayout* el = FindElement(elementId);
		if (!el)
		{
			LOG_WARN(Core, "[HudLayoutEditor] BeginDrag: unknown element '{}'", elementId);
			return false;
		}
		if (el->locked)
		{
			LOG_DEBUG(Core, "[HudLayoutEditor] BeginDrag: element '{}' is locked", elementId);
			return false;
		}

		m_state.activeElementId = elementId;
		m_state.isDragging      = true;
		m_dragStartMouseNX      = mouseNX;
		m_dragStartMouseNY      = mouseNY;
		m_dragStartPosX         = el->posX;
		m_dragStartPosY         = el->posY;

		LOG_DEBUG(Core, "[HudLayoutEditor] BeginDrag '{}' at mouse ({:.3f},{:.3f})",
		          elementId, mouseNX, mouseNY);
		return true;
	}

	void HudLayoutEditor::UpdateDrag(float mouseNX, float mouseNY)
	{
		if (!m_state.isDragging)
			return;

		HudElementLayout* el = FindElementMut(m_state.activeElementId);
		if (!el)
		{
			EndDrag();
			return;
		}

		const float deltaNX = mouseNX - m_dragStartMouseNX;
		const float deltaNY = mouseNY - m_dragStartMouseNY;

		float newX = m_dragStartPosX + deltaNX;
		float newY = m_dragStartPosY + deltaNY;

		// Snap to grid if enabled (spec step 5 — optional).
		if (m_state.snapToGrid)
		{
			newX = SnapPosition(newX);
			newY = SnapPosition(newY);
		}

		el->posX = ClampPos(newX);
		el->posY = ClampPos(newY);
	}

	void HudLayoutEditor::EndDrag()
	{
		if (!m_state.isDragging)
			return;

		LOG_DEBUG(Core, "[HudLayoutEditor] EndDrag '{}' final pos ({:.3f},{:.3f})",
		          m_state.activeElementId,
		          FindElement(m_state.activeElementId) ? FindElement(m_state.activeElementId)->posX : 0.0f,
		          FindElement(m_state.activeElementId) ? FindElement(m_state.activeElementId)->posY : 0.0f);

		m_state.isDragging = false;
		m_state.activeElementId.clear();
		RebuildDebugText();
	}

	void HudLayoutEditor::CancelDrag()
	{
		if (!m_state.isDragging)
			return;

		// Restore the original position recorded at BeginDrag.
		HudElementLayout* el = FindElementMut(m_state.activeElementId);
		if (el)
		{
			el->posX = m_dragStartPosX;
			el->posY = m_dragStartPosY;
		}
		m_state.isDragging = false;
		m_state.activeElementId.clear();
		RebuildDebugText();
	}

	// =========================================================================
	// Resize operations (spec step 4)
	// =========================================================================

	bool HudLayoutEditor::BeginResize(const std::string& elementId,
	                                  HudResizeCorner corner,
	                                  float mouseNX, float mouseNY)
	{
		if (!m_state.editModeActive)
			return false;
		if (corner == HudResizeCorner::None)
			return false;

		const HudElementLayout* el = FindElement(elementId);
		if (!el)
		{
			LOG_WARN(Core, "[HudLayoutEditor] BeginResize: unknown element '{}'", elementId);
			return false;
		}
		if (el->locked)
		{
			LOG_DEBUG(Core, "[HudLayoutEditor] BeginResize: element '{}' is locked", elementId);
			return false;
		}

		m_state.activeElementId  = elementId;
		m_state.isResizing       = true;
		m_state.resizeCorner     = corner;
		m_resizeStartMouseNX     = mouseNX;
		m_resizeStartMouseNY     = mouseNY;
		m_resizeStartScale       = el->scale;

		LOG_DEBUG(Core, "[HudLayoutEditor] BeginResize '{}' corner={} scale={:.2f}",
		          elementId, static_cast<uint8_t>(corner), el->scale);
		return true;
	}

	void HudLayoutEditor::UpdateResize(float mouseNX, float mouseNY)
	{
		if (!m_state.isResizing)
			return;

		HudElementLayout* el = FindElementMut(m_state.activeElementId);
		if (!el)
		{
			EndResize();
			return;
		}

		// Compute drag delta from the corner where the drag started.
		const float deltaNX = mouseNX - m_resizeStartMouseNX;
		const float deltaNY = mouseNY - m_resizeStartMouseNY;

		// Use the Euclidean drag distance as a scale delta.
		// Dragging away from the element center increases scale; toward it decreases.
		// We use the sign of the dominant axis to determine direction.
		float signedDelta = 0.0f;
		switch (m_state.resizeCorner)
		{
		case HudResizeCorner::TopLeft:
			signedDelta = -(deltaNX + deltaNY) * 0.5f;
			break;
		case HudResizeCorner::TopRight:
			signedDelta = (deltaNX - deltaNY) * 0.5f;
			break;
		case HudResizeCorner::BottomLeft:
			signedDelta = (-deltaNX + deltaNY) * 0.5f;
			break;
		case HudResizeCorner::BottomRight:
			signedDelta = (deltaNX + deltaNY) * 0.5f;
			break;
		default:
			return;
		}

		// Sensitivity: 1.0 of normalised distance = 2.0 scale units (0-10 scale).
		constexpr float kResizeSensitivity = 2.0f;
		const float newScale = m_resizeStartScale + signedDelta * kResizeSensitivity;
		el->scale = ClampScale(newScale);
	}

	void HudLayoutEditor::EndResize()
	{
		if (!m_state.isResizing)
			return;

		LOG_DEBUG(Core, "[HudLayoutEditor] EndResize '{}' final scale={:.2f}",
		          m_state.activeElementId,
		          FindElement(m_state.activeElementId) ? FindElement(m_state.activeElementId)->scale : 1.0f);

		m_state.isResizing   = false;
		m_state.resizeCorner = HudResizeCorner::None;
		m_state.activeElementId.clear();
		RebuildDebugText();
	}

	void HudLayoutEditor::CancelResize()
	{
		if (!m_state.isResizing)
			return;

		// Restore the original scale recorded at BeginResize.
		HudElementLayout* el = FindElementMut(m_state.activeElementId);
		if (el)
			el->scale = m_resizeStartScale;

		m_state.isResizing   = false;
		m_state.resizeCorner = HudResizeCorner::None;
		m_state.activeElementId.clear();
		RebuildDebugText();
	}

	// =========================================================================
	// Debug text
	// =========================================================================

	void HudLayoutEditor::RebuildDebugText()
	{
		std::ostringstream ss;
		ss << "[HudLayoutEditor M39.3] editMode=" << (m_state.editModeActive ? "ON" : "OFF")
		   << " snap=" << (m_state.snapToGrid ? "ON" : "OFF") << "\n";

		for (const HudElementLayout& el : m_state.elements)
		{
			ss << "  " << el.id
			   << " pos=(" << el.posX << "," << el.posY << ")"
			   << " scale=" << el.scale
			   << (el.locked ? " [LOCKED]" : "");
			if (el.id == m_state.activeElementId)
			{
				if (m_state.isDragging)  ss << " [DRAGGING]";
				if (m_state.isResizing)  ss << " [RESIZING]";
			}
			ss << "\n";
		}
		m_state.debugText = ss.str();
	}

	// =========================================================================
	// Private helpers
	// =========================================================================

	bool HudLayoutEditor::ParseLayoutJson(const std::string& jsonText,
	                                      std::vector<HudElementLayout>& outElements)
	{
		outElements.clear();

		// Load the JSON text into a Config object to leverage the existing parser.
		engine::core::Config cfg;
		// Write to a temp file approach is not desirable; instead we use
		// Config::LoadFromFile on a string — which doesn't exist.
		// Instead we do a simple, minimal hand-rolled parse of our known format.
		//
		// Expected format:
		//   { "elements": [ { "id": "...", "x": 0.0, "y": 0.0, "scale": 1.0, "locked": false }, ... ] }

		// Find the "elements" array.
		const std::string elemKey = "\"elements\"";
		auto arrStart = jsonText.find(elemKey);
		if (arrStart == std::string::npos)
			return false;

		// Walk through each object in the array.
		size_t pos = jsonText.find('[', arrStart);
		if (pos == std::string::npos)
			return false;

		auto readString = [&](size_t& cursor, const std::string& key) -> std::string
		{
			const std::string search = "\"" + key + "\"";
			auto k = jsonText.find(search, cursor);
			if (k == std::string::npos)
				return {};
			auto colon = jsonText.find(':', k);
			if (colon == std::string::npos)
				return {};
			auto q1 = jsonText.find('"', colon + 1);
			if (q1 == std::string::npos)
				return {};
			auto q2 = jsonText.find('"', q1 + 1);
			if (q2 == std::string::npos)
				return {};
			cursor = q2 + 1;
			return jsonText.substr(q1 + 1, q2 - q1 - 1);
		};

		auto readFloat = [&](size_t& cursor, const std::string& key) -> float
		{
			const std::string search = "\"" + key + "\"";
			auto k = jsonText.find(search, cursor);
			if (k == std::string::npos)
				return 0.0f;
			auto colon = jsonText.find(':', k);
			if (colon == std::string::npos)
				return 0.0f;
			size_t valStart = colon + 1;
			while (valStart < jsonText.size() && std::isspace(static_cast<unsigned char>(jsonText[valStart])))
				++valStart;
			try
			{
				size_t consumed = 0;
				const float v = std::stof(jsonText.substr(valStart), &consumed);
				cursor = valStart + consumed;
				return v;
			}
			catch (...)
			{
				return 0.0f;
			}
		};

		auto readBool = [&](size_t& cursor, const std::string& key) -> bool
		{
			const std::string search = "\"" + key + "\"";
			auto k = jsonText.find(search, cursor);
			if (k == std::string::npos)
				return false;
			auto colon = jsonText.find(':', k);
			if (colon == std::string::npos)
				return false;
			size_t valStart = colon + 1;
			while (valStart < jsonText.size() && std::isspace(static_cast<unsigned char>(jsonText[valStart])))
				++valStart;
			if (jsonText.compare(valStart, 4, "true") == 0)
			{
				cursor = valStart + 4;
				return true;
			}
			cursor = valStart + 5; // "false"
			return false;
		};

		size_t cur = pos;
		while (true)
		{
			auto objStart = jsonText.find('{', cur);
			if (objStart == std::string::npos)
				break;
			auto objEnd = jsonText.find('}', objStart);
			if (objEnd == std::string::npos)
				break;

			// Check we haven't stepped past the array end.
			auto arrEnd = jsonText.find(']', pos + 1);
			if (arrEnd != std::string::npos && objStart > arrEnd)
				break;

			size_t objCur = objStart;
			const std::string id    = readString(objCur, "id");
			objCur = objStart; // reset cursor for each field
			const float x           = readFloat(objCur, "x");
			objCur = objStart;
			const float y           = readFloat(objCur, "y");
			objCur = objStart;
			const float scale       = readFloat(objCur, "scale");
			objCur = objStart;
			const bool locked       = readBool(objCur, "locked");

			if (!id.empty())
			{
				HudElementLayout el;
				el.id     = id;
				el.posX   = ClampPos(x);
				el.posY   = ClampPos(y);
				el.scale  = ClampScale(scale > 0.0f ? scale : 1.0f);
				el.locked = locked;
				outElements.push_back(std::move(el));
			}

			cur = objEnd + 1;
		}

		return !outElements.empty();
	}

	std::string HudLayoutEditor::SerialiseLayoutJson() const
	{
		std::ostringstream json;
		json << "{\n  \"elements\": [\n";

		for (size_t i = 0; i < m_state.elements.size(); ++i)
		{
			const HudElementLayout& el = m_state.elements[i];
			json << "    { \"id\": \"" << el.id << "\""
			     << ", \"x\": " << el.posX
			     << ", \"y\": " << el.posY
			     << ", \"scale\": " << el.scale
			     << ", \"locked\": " << (el.locked ? "true" : "false")
			     << " }";
			if (i + 1 < m_state.elements.size())
				json << ",";
			json << "\n";
		}

		json << "  ]\n}\n";
		return json.str();
	}

	float HudLayoutEditor::SnapPosition(float pos) const
	{
		// Convert to pixel space, snap, convert back.
		const float scaleX = static_cast<float>(m_viewportWidth);
		const float px     = pos * scaleX;
		const float snapped = std::round(px / kHudSnapGridPx) * kHudSnapGridPx;
		return snapped / scaleX;
	}

	/*static*/ float HudLayoutEditor::ClampScale(float s)
	{
		if (s < kHudScaleMin) return kHudScaleMin;
		if (s > kHudScaleMax) return kHudScaleMax;
		return s;
	}

	/*static*/ float HudLayoutEditor::ClampPos(float p)
	{
		if (p < 0.0f) return 0.0f;
		if (p > 1.0f) return 1.0f;
		return p;
	}

} // namespace engine::client
