#include "src/client/crafting/CraftingUi.h"

#include "src/shared/core/Log.h"

#include <algorithm>

namespace engine::client
{
	namespace
	{
		/// Return the human-readable name for a craft quality tier (M36.3).
		const char* QualityTierLabel(uint8_t tier)
		{
			switch (tier)
			{
			case 0:  return "Normal";
			case 1:  return "Uncommon";
			case 2:  return "Rare";
			case 3:  return "Epic";
			default: return "Normal";
			}
		}

		/// Return the normalised RGB border colour for a craft quality tier (M36.3).
		/// Colours match the spec: white, green, blue, purple.
		QualityBorderColor QualityTierColor(uint8_t tier)
		{
			switch (tier)
			{
			case 0:  return { 1.000f, 1.000f, 1.000f }; ///< Normal   — white  (#FFFFFF)
			case 1:  return { 0.118f, 1.000f, 0.000f }; ///< Uncommon — green  (#1EFF00)
			case 2:  return { 0.000f, 0.439f, 0.867f }; ///< Rare     — blue   (#0070DD)
			case 3:  return { 0.639f, 0.208f, 0.933f }; ///< Epic     — purple (#A335EE)
			default: return { 1.000f, 1.000f, 1.000f };
			}
		}
	} // anonymous namespace
	CraftingUiPresenter::~CraftingUiPresenter()
	{
		if (m_initialized)
		{
			Shutdown();
		}
	}

	bool CraftingUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Gameplay, "[CraftingUi] Init ignored: already initialized");
			return true;
		}
		m_state       = {};
		m_initialized = true;
		LOG_INFO(Gameplay, "[CraftingUi] Init OK");
		return true;
	}

	void CraftingUiPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}
		m_state       = {};
		m_initialized = false;
		LOG_INFO(Gameplay, "[CraftingUi] Destroyed");
	}

	bool CraftingUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_initialized)
		{
			LOG_WARN(Gameplay, "[CraftingUi] SetViewportSize: not initialized");
			return false;
		}
		if (width == 0u || height == 0u)
		{
			LOG_WARN(Gameplay, "[CraftingUi] SetViewportSize: invalid {}x{}", width, height);
			return false;
		}
		m_viewportWidth  = width;
		m_viewportHeight = height;
		LOG_DEBUG(Gameplay, "[CraftingUi] Viewport set to {}x{}", width, height);
		return true;
	}

	bool CraftingUiPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)
	{
		if (!m_initialized)
		{
			LOG_WARN(Gameplay, "[CraftingUi] ApplyModel: not initialized");
			return false;
		}
		if ((changeMask & UIModelChangeCrafting) == 0)
		{
			return true;
		}

		const UICraftingState& crafting = model.crafting;

		/// Panel is visible when at least one profession is known.
		m_state.isVisible = !crafting.professions.empty();

		if (!m_state.isVisible)
		{
			m_state.layoutValid = false;
			m_state.statusText  = "No profession learned yet.";
			RebuildDebugText(model);
			return true;
		}

		RebuildLayout(model);
		RebuildDebugText(model);

		/// Cast bar fill fraction.
		m_state.craftFillFraction = crafting.isCrafting ? crafting.craftFillFraction : 0.0f;

		/// M36.3 — update quality tier display fields.
		m_state.lastQualityTier  = crafting.lastQualityTier;
		m_state.lastQualityLabel = QualityTierLabel(crafting.lastQualityTier);
		m_state.lastQualityColor = QualityTierColor(crafting.lastQualityTier);

		/// Status text (includes quality tier name for non-normal results per M36.3 spec).
		if (crafting.isCrafting)
		{
			m_state.statusText = "Crafting: " + crafting.craftingRecipeId + "…";
		}
		else if (crafting.lastSkillGained != 0)
		{
			m_state.statusText = "Skill up! Level " + std::to_string(crafting.lastNewSkillLevel);
			if (crafting.lastQualityTier > 0)
			{
				m_state.statusText += " [" + m_state.lastQualityLabel + "]";
			}
		}
		else if (crafting.lastQualityTier > 0)
		{
			/// Critical craft with no skill gain — still report quality in status.
			m_state.statusText = "Crafted [" + m_state.lastQualityLabel + "]";
		}
		else
		{
			m_state.statusText.clear();
		}

		LOG_DEBUG(Gameplay,
		          "[CraftingUi] Model applied (professions={} recipes={} crafting={} quality={})",
		          crafting.professions.size(), crafting.recipes.size(),
		          crafting.isCrafting ? 1 : 0, m_state.lastQualityLabel);
		return true;
	}

	bool CraftingUiPresenter::Tick(float deltaSeconds)
	{
		(void)deltaSeconds;
		if (!m_initialized)
		{
			return true;
		}
		/// Clamp fill fraction defensively; actual advancement is done by the UIModel.
		m_state.craftFillFraction = std::clamp(m_state.craftFillFraction, 0.0f, 1.0f);
		return true;
	}

	int CraftingUiPresenter::HitTestRecipeRow(float mouseX, float mouseY) const
	{
		for (const CraftRecipeRowRegion& row : m_state.recipeRows)
		{
			if (mouseX >= row.x && mouseX <= row.x + row.width &&
			    mouseY >= row.y && mouseY <= row.y + row.height)
			{
				return static_cast<int>(row.rowIndex);
			}
		}
		return -1;
	}

	bool CraftingUiPresenter::HitCraftButton(float mouseX, float mouseY) const
	{
		return m_state.craftBtnEnabled &&
		       mouseX >= m_state.craftBtnX && mouseX <= m_state.craftBtnX + m_state.craftBtnWidth &&
		       mouseY >= m_state.craftBtnY && mouseY <= m_state.craftBtnY + m_state.craftBtnHeight;
	}

	void CraftingUiPresenter::RebuildLayout(const UIModel& model)
	{
		if (m_viewportWidth == 0 || m_viewportHeight == 0)
		{
			m_state.layoutValid = false;
			return;
		}

		const UICraftingState& crafting = model.crafting;

		/// Panel: left side of screen, 30% width, 55% height.
		constexpr float kPad      = 8.0f;
		constexpr float kRowH     = 22.0f;
		constexpr float kTabH     = 26.0f;
		constexpr float kBtnH     = 28.0f;
		constexpr float kBarH     = 14.0f;

		m_state.panelWidth  = static_cast<float>(m_viewportWidth)  * 0.30f;
		m_state.panelHeight = static_cast<float>(m_viewportHeight) * 0.55f;
		m_state.panelX      = kPad;
		m_state.panelY      = (static_cast<float>(m_viewportHeight) - m_state.panelHeight) * 0.5f;

		/// Profession tabs.
		m_state.professionTabs.clear();
		for (const UIProfessionEntry& e : crafting.professions)
		{
			m_state.professionTabs.push_back(e.professionKey);
		}

		/// Recipe rows.
		m_state.recipeRows.clear();
		const float innerX = m_state.panelX + kPad;
		const float innerW = m_state.panelWidth - kPad * 2.0f;
		float rowY = m_state.panelY + kTabH + kPad;
		const float maxRowY = m_state.panelY + m_state.panelHeight - kBtnH - kBarH - kPad * 3.0f;

		for (size_t i = 0; i < crafting.recipes.size(); ++i)
		{
			if (rowY + kRowH > maxRowY)
			{
				LOG_WARN(Gameplay, "[CraftingUi] Recipe row {} clipped by panel height", i);
				break;
			}
			CraftRecipeRowRegion row{};
			row.rowIndex = static_cast<uint32_t>(i);
			row.x        = innerX;
			row.y        = rowY;
			row.width    = innerW;
			row.height   = kRowH;
			m_state.recipeRows.push_back(row);
			rowY += kRowH + 2.0f;
		}

		/// Craft button (bottom of panel).
		m_state.craftBtnX      = innerX;
		m_state.craftBtnY      = m_state.panelY + m_state.panelHeight - kBtnH - kBarH - kPad * 2.0f;
		m_state.craftBtnWidth  = innerW;
		m_state.craftBtnHeight = kBtnH;
		m_state.craftBtnEnabled =
			!crafting.isCrafting &&
			crafting.selectedRecipeIndex != UINT32_MAX &&
			crafting.selectedRecipeIndex < crafting.recipes.size();

		m_state.selectedRow  = crafting.selectedRecipeIndex;
		m_state.layoutValid  = true;

		LOG_DEBUG(Gameplay, "[CraftingUi] Layout rebuilt (rows={} craftBtnEnabled={})",
		          m_state.recipeRows.size(), m_state.craftBtnEnabled ? 1 : 0);
	}

	void CraftingUiPresenter::RebuildDebugText(const UIModel& model)
	{
		const UICraftingState& crafting = model.crafting;
		m_state.debugText.clear();
		m_state.debugText += "[Crafting M36.2/M36.3]\n";

		if (crafting.professions.empty())
		{
			m_state.debugText += " No professions learned.\n";
			return;
		}

		m_state.debugText += " Professions:\n";
		for (const UIProfessionEntry& e : crafting.professions)
		{
			m_state.debugText += "  " + e.professionKey + " (skill=" + std::to_string(e.skillLevel);
			if (e.isPrimary) m_state.debugText += " PRIMARY";
			m_state.debugText += ")\n";
		}

		if (!crafting.activeProfessionKey.empty())
		{
			m_state.debugText += " Active: " + crafting.activeProfessionKey + " — ";
			m_state.debugText += std::to_string(crafting.recipes.size()) + " recipes\n";
			for (size_t i = 0; i < crafting.recipes.size(); ++i)
			{
				const UICraftRecipeRow& r = crafting.recipes[i];
				m_state.debugText += "  [" + std::to_string(i + 1) + "] " + r.recipeId;
				m_state.debugText += " (skill=" + std::to_string(r.skillRequired) + ")\n";
			}
		}

		if (crafting.isCrafting)
		{
			m_state.debugText += " Crafting: " + crafting.craftingRecipeId;
			m_state.debugText += " fill=" + std::to_string(static_cast<int>(crafting.craftFillFraction * 100.0f)) + "%\n";
		}

		/// M36.3 — quality tier display with tooltip-equivalent text.
		m_state.debugText += " Last craft quality: [";
		m_state.debugText += QualityTierLabel(crafting.lastQualityTier);
		m_state.debugText += "]";
		if (crafting.lastQualityTier > 0)
		{
			/// Show stat multiplier from spec (+5% per tier).
			const uint32_t multiplierPct = 100u + static_cast<uint32_t>(crafting.lastQualityTier) * 5u;
			m_state.debugText += " +" + std::to_string(multiplierPct - 100u) + "% stats";
		}
		m_state.debugText += "\n";
		m_state.debugText += " Quality color: rgb("
			+ std::to_string(static_cast<int>(m_state.lastQualityColor.r * 255.0f)) + ","
			+ std::to_string(static_cast<int>(m_state.lastQualityColor.g * 255.0f)) + ","
			+ std::to_string(static_cast<int>(m_state.lastQualityColor.b * 255.0f)) + ")\n";
	}
}
