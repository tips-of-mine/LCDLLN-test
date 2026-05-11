#include "src/client/crafting/HarvestCastBar.h"

#include "src/shared/core/Log.h"

#include <algorithm>

namespace engine::client
{
	HarvestCastBarPresenter::~HarvestCastBarPresenter()
	{
		if (m_initialized)
		{
			Shutdown();
		}
	}

	bool HarvestCastBarPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Gameplay, "[HarvestCastBar] Init ignored: already initialized");
			return true;
		}
		m_state       = {};
		m_initialized = true;
		LOG_INFO(Gameplay, "[HarvestCastBar] Init OK");
		return true;
	}

	void HarvestCastBarPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}
		m_state       = {};
		m_initialized = false;
		LOG_INFO(Gameplay, "[HarvestCastBar] Destroyed");
	}

	bool HarvestCastBarPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_initialized)
		{
			LOG_WARN(Gameplay, "[HarvestCastBar] SetViewportSize: not initialized");
			return false;
		}
		m_viewportWidth  = width;
		m_viewportHeight = height;
		RebuildLayout();
		return true;
	}

	bool HarvestCastBarPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)
	{
		if (!m_initialized)
		{
			LOG_WARN(Gameplay, "[HarvestCastBar] ApplyModel: not initialized");
			return false;
		}
		if ((changeMask & UIModelChangeHarvest) == 0)
		{
			return true;
		}

		const UIHarvestProgress& harvest = model.harvest;

		if (!harvest.inProgress)
		{
			m_state.visible      = false;
			m_state.fillFraction = 0.0f;
			m_state.label.clear();
			return true;
		}

		m_state.visible      = true;
		m_state.fillFraction = harvest.fillFraction;
		m_state.label        = "Harvesting…";

		RebuildLayout();

		LOG_DEBUG(Gameplay, "[HarvestCastBar] Applied (fill={:.2f})", m_state.fillFraction);
		return true;
	}

	bool HarvestCastBarPresenter::Tick(float deltaSeconds)
	{
		if (!m_initialized || !m_state.visible)
		{
			return true;
		}

		/// Advance fill fraction based on elapsed time and total duration.
		/// The UIModel stores the actual progress; here we just clamp defensively.
		m_state.fillFraction = std::clamp(m_state.fillFraction, 0.0f, 1.0f);
		(void)deltaSeconds;
		return true;
	}

	void HarvestCastBarPresenter::RebuildLayout()
	{
		if (m_viewportWidth == 0 || m_viewportHeight == 0)
		{
			return;
		}

		/// Cast bar: centred horizontally, anchored 20 % above the bottom of the screen.
		constexpr float kBarWidthFraction  = 0.25f;
		constexpr float kBarHeight         = 18.0f;
		constexpr float kBottomOffsetFrac  = 0.20f;

		m_state.barWidth  = static_cast<float>(m_viewportWidth)  * kBarWidthFraction;
		m_state.barHeight = kBarHeight;
		m_state.barX      = (static_cast<float>(m_viewportWidth)  - m_state.barWidth)  * 0.5f;
		m_state.barY      = static_cast<float>(m_viewportHeight) * (1.0f - kBottomOffsetFrac) - kBarHeight;

		LOG_TRACE(Gameplay, "[HarvestCastBar] Layout: x={} y={} w={} h={}",
		          m_state.barX, m_state.barY, m_state.barWidth, m_state.barHeight);
	}
}
