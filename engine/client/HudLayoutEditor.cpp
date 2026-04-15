#include "engine/client/HudLayoutEditor.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace engine::client
{
	// ── Constants ─────────────────────────────────────────────────────────────

	/// How close (as a fraction of the screen) the mouse must be to an element
	/// anchor to begin dragging it.
	static constexpr float kPickRadiusPct = 0.04f;

	// ── Static helpers ────────────────────────────────────────────────────────

	const char* HudLayoutEditor::ElementName(HudElementId id)
	{
		switch (id)
		{
		case HudElementId::PlayerFrame:  return "player_frame";
		case HudElementId::TargetFrame:  return "target_frame";
		case HudElementId::Minimap:      return "minimap";
		case HudElementId::ActionBars:   return "action_bars";
		case HudElementId::Chat:         return "chat";
		default:                         return "unknown";
		}
	}

	void HudLayoutEditor::BuildDefaultLayout(HudLayout& out)
	{
		out.clear();
		out[HudElementId::PlayerFrame]  = { 0.02f, 0.02f,  1.0f, false };
		out[HudElementId::TargetFrame]  = { 0.02f, 0.12f,  1.0f, false };
		out[HudElementId::Minimap]      = { 0.80f, 0.02f,  1.0f, false };
		out[HudElementId::ActionBars]   = { 0.30f, 0.88f,  1.0f, false };
		out[HudElementId::Chat]         = { 0.02f, 0.72f,  1.0f, false };
	}

	void HudLayoutEditor::BuildMinimalLayout(HudLayout& out)
	{
		out.clear();
		out[HudElementId::PlayerFrame]  = { 0.01f, 0.01f,  0.7f, false };
		out[HudElementId::TargetFrame]  = { 0.01f, 0.08f,  0.7f, false };
		out[HudElementId::Minimap]      = { 0.86f, 0.01f,  0.6f, false };
		out[HudElementId::ActionBars]   = { 0.35f, 0.90f,  0.75f, false };
		out[HudElementId::Chat]         = { 0.01f, 0.78f,  0.65f, false };
	}

	void HudLayoutEditor::BuildCompactLayout(HudLayout& out)
	{
		out.clear();
		out[HudElementId::PlayerFrame]  = { 0.01f, 0.01f,  0.85f, false };
		out[HudElementId::TargetFrame]  = { 0.20f, 0.01f,  0.85f, false };
		out[HudElementId::Minimap]      = { 0.88f, 0.01f,  0.75f, false };
		out[HudElementId::ActionBars]   = { 0.32f, 0.87f,  0.9f,  false };
		out[HudElementId::Chat]         = { 0.01f, 0.74f,  0.75f, false };
	}

	std::string HudLayoutEditor::CharLayoutPath() const
	{
		return "hud/layouts/char_" + std::to_string(m_characterId) + ".json";
	}

	// ── HudLayoutEditor public API ────────────────────────────────────────────

	HudLayoutEditor::~HudLayoutEditor()
	{
		Shutdown();
	}

	bool HudLayoutEditor::Init(const engine::core::Config& config, uint32_t characterId)
	{
		m_config      = config;
		m_characterId = characterId;

		// Try to load a persisted layout; fall back to default on failure.
		if (!LoadLayout(config))
		{
			BuildDefaultLayout(m_state.layout);
			LOG_INFO(Core, "[HudLayoutEditor] No saved layout found for char {}, using Default",
			    characterId);
		}

		m_state.editModeActive = false;
		m_state.statusMessage  = "HUD layout loaded.";
		m_initialized          = true;

		LOG_INFO(Core, "[HudLayoutEditor] Init OK (characterId={})", characterId);
		return true;
	}

	void HudLayoutEditor::Shutdown()
	{
		if (!m_initialized) return;
		m_state       = {};
		m_initialized = false;
		LOG_INFO(Core, "[HudLayoutEditor] Shutdown");
	}

	// ── Edit mode ─────────────────────────────────────────────────────────────

	void HudLayoutEditor::ToggleEditMode()
	{
		SetEditMode(!m_state.editModeActive);
	}

	void HudLayoutEditor::SetEditMode(bool active)
	{
		if (m_state.editModeActive == active) return;

		m_state.editModeActive = active;
		EndDrag(); // Cancel any in-progress drag when toggling.

		m_state.statusMessage = active ? "Edit mode ON — drag elements to reposition."
		                                : "Edit mode OFF.";
		LOG_INFO(Core, "[HudLayoutEditor] Edit mode {}", active ? "enabled" : "disabled");
	}

	// ── Drag / resize ─────────────────────────────────────────────────────────

	bool HudLayoutEditor::BeginDrag(float mouseXPct, float mouseYPct, bool isResize)
	{
		if (!m_state.editModeActive)
			return false;

		// Find the closest unlocked element within the pick radius.
		float    bestDist = kPickRadiusPct * kPickRadiusPct;
		HudElementId best = HudElementId::Count;

		for (auto& [id, elem] : m_state.layout)
		{
			if (elem.locked) continue;

			const float dx  = mouseXPct - elem.xPct;
			const float dy  = mouseYPct - elem.yPct;
			const float d2  = dx * dx + dy * dy;

			if (d2 < bestDist)
			{
				bestDist = d2;
				best     = id;
			}
		}

		if (best == HudElementId::Count)
			return false;

		m_state.drag.active     = true;
		m_state.drag.elementId  = best;
		m_state.drag.isResize   = isResize;
		const auto& elem        = m_state.layout.at(best);
		m_state.drag.grabOffsetX = mouseXPct - elem.xPct;
		m_state.drag.grabOffsetY = mouseYPct - elem.yPct;

		LOG_DEBUG(Core, "[HudLayoutEditor] BeginDrag element={} isResize={}",
		    ElementName(best), isResize);
		return true;
	}

	void HudLayoutEditor::UpdateDrag(float mouseXPct, float mouseYPct,
	                                 float viewportWidth, float viewportHeight,
	                                 bool snapToGrid)
	{
		if (!m_state.drag.active)
			return;

		auto it = m_state.layout.find(m_state.drag.elementId);
		if (it == m_state.layout.end())
			return;

		HudElementLayout& elem = it->second;

		if (!m_state.drag.isResize)
		{
			// Move: apply grab offset so the element doesn't jump.
			float newX = mouseXPct - m_state.drag.grabOffsetX;
			float newY = mouseYPct - m_state.drag.grabOffsetY;

			if (snapToGrid && viewportWidth > 0.0f && viewportHeight > 0.0f)
			{
				// Snap to 10-pixel grid (optional spec step 5).
				const float gridX = 10.0f / viewportWidth;
				const float gridY = 10.0f / viewportHeight;
				newX = std::round(newX / gridX) * gridX;
				newY = std::round(newY / gridY) * gridY;
			}

			elem.xPct = Clamp(newX, 0.0f, 1.0f);
			elem.yPct = Clamp(newY, 0.0f, 1.0f);
		}
		else
		{
			// Resize: distance from anchor maps to scale change.
			const float dist = std::sqrt(
			    mouseXPct * mouseXPct + mouseYPct * mouseYPct);
			const float refDist = 0.10f; // 10% of screen = reference 1.0 scale
			if (refDist > 0.0f)
			{
				const float rawScale = dist / refDist;
				elem.scale = Clamp(rawScale, kScaleMin, kScaleMax);
			}
		}
	}

	void HudLayoutEditor::EndDrag()
	{
		if (m_state.drag.active)
		{
			LOG_DEBUG(Core, "[HudLayoutEditor] EndDrag element={}",
			    ElementName(m_state.drag.elementId));
		}
		m_state.drag = {};
	}

	// ── Element manipulation ──────────────────────────────────────────────────

	void HudLayoutEditor::SetPosition(HudElementId id, float xPct, float yPct)
	{
		auto it = m_state.layout.find(id);
		if (it == m_state.layout.end()) return;
		it->second.xPct = Clamp(xPct, 0.0f, 1.0f);
		it->second.yPct = Clamp(yPct, 0.0f, 1.0f);
	}

	void HudLayoutEditor::SetScale(HudElementId id, float scale)
	{
		auto it = m_state.layout.find(id);
		if (it == m_state.layout.end()) return;
		it->second.scale = Clamp(scale, kScaleMin, kScaleMax);
	}

	void HudLayoutEditor::SetLocked(HudElementId id, bool locked)
	{
		auto it = m_state.layout.find(id);
		if (it == m_state.layout.end()) return;
		it->second.locked = locked;
		LOG_DEBUG(Core, "[HudLayoutEditor] Element '{}' locked={}", ElementName(id), locked);
	}

	// ── Presets ───────────────────────────────────────────────────────────────

	void HudLayoutEditor::ApplyPreset(HudLayoutPreset preset)
	{
		switch (preset)
		{
		case HudLayoutPreset::Default:
			BuildDefaultLayout(m_state.layout);
			m_state.statusMessage = "Default preset applied.";
			break;
		case HudLayoutPreset::Minimal:
			BuildMinimalLayout(m_state.layout);
			m_state.statusMessage = "Minimal preset applied.";
			break;
		case HudLayoutPreset::Compact:
			BuildCompactLayout(m_state.layout);
			m_state.statusMessage = "Compact preset applied.";
			break;
		}
		LOG_INFO(Core, "[HudLayoutEditor] Preset applied: {}", static_cast<int>(preset));
	}

	void HudLayoutEditor::ResetToDefault()
	{
		ApplyPreset(HudLayoutPreset::Default);
		LOG_INFO(Core, "[HudLayoutEditor] Layout reset to Default");
	}

	// ── Persistence ───────────────────────────────────────────────────────────

	bool HudLayoutEditor::SaveLayout(const engine::core::Config& config) const
	{
		const std::string relPath = CharLayoutPath();

		// Serialise as flat INI-compatible key=value (Config-loadable).
		std::ostringstream out;
		for (const auto& [id, elem] : m_state.layout)
		{
			const std::string prefix =
			    std::string("hud.") + ElementName(id) + ".";
			out << prefix << "x="      << elem.xPct  << "\n";
			out << prefix << "y="      << elem.yPct  << "\n";
			out << prefix << "scale="  << elem.scale  << "\n";
			out << prefix << "locked=" << (elem.locked ? "true" : "false") << "\n";
		}

		if (!engine::platform::FileSystem::WriteAllTextContent(config, relPath, out.str()))
		{
			LOG_ERROR(Core, "[HudLayoutEditor] Save FAILED (path={})", relPath);
			return false;
		}

		LOG_INFO(Core, "[HudLayoutEditor] Layout saved to {}", relPath);
		return true;
	}

	bool HudLayoutEditor::LoadLayout(const engine::core::Config& config)
	{
		const std::string relPath = CharLayoutPath();
		const auto fullPath = engine::platform::FileSystem::ResolveContentPath(config, relPath);

		if (!engine::platform::FileSystem::Exists(fullPath))
		{
			LOG_DEBUG(Core, "[HudLayoutEditor] Layout file absent ({})", relPath);
			return false;
		}

		engine::core::Config layoutCfg;
		if (!layoutCfg.LoadFromFile(fullPath.string()))
		{
			LOG_WARN(Core, "[HudLayoutEditor] Layout file parse FAILED ({})", relPath);
			return false;
		}

		// Parse each known element; skip silently when a key is missing.
		static constexpr HudElementId kAll[] = {
		    HudElementId::PlayerFrame, HudElementId::TargetFrame,
		    HudElementId::Minimap,     HudElementId::ActionBars,
		    HudElementId::Chat
		};
		for (const HudElementId id : kAll)
		{
			const std::string prefix = std::string("hud.") + ElementName(id) + ".";
			HudElementLayout elem{};
			if (!layoutCfg.Has(prefix + "x")) continue; // Element absent in file.

			elem.xPct   = static_cast<float>(layoutCfg.GetDouble(prefix + "x",     0.0));
			elem.yPct   = static_cast<float>(layoutCfg.GetDouble(prefix + "y",     0.0));
			elem.scale  = Clamp(
			    static_cast<float>(layoutCfg.GetDouble(prefix + "scale", 1.0)),
			    kScaleMin, kScaleMax);
			elem.locked = layoutCfg.GetBool(prefix + "locked", false);
			m_state.layout[id] = elem;
		}

		LOG_INFO(Core, "[HudLayoutEditor] Layout loaded from {}", relPath);
		return true;
	}

} // namespace engine::client
