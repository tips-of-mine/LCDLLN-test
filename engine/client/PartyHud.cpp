#include "engine/client/PartyHud.h"

#include "engine/core/Log.h"

#include <algorithm>

namespace engine::client
{
	// -------------------------------------------------------------------------
	// Lifecycle
	// -------------------------------------------------------------------------

	PartyHudPresenter::~PartyHudPresenter()
	{
		if (m_initialized)
			Shutdown();
	}

	bool PartyHudPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Render, "[PartyHud] Init called on already-initialized presenter");
			return true;
		}

		m_state       = {};
		m_initialized = true;
		LOG_INFO(Render, "[PartyHud] Init OK");
		return true;
	}

	void PartyHudPresenter::Shutdown()
	{
		m_state       = {};
		m_initialized = false;
		LOG_INFO(Render, "[PartyHud] Destroyed");
	}

	// -------------------------------------------------------------------------
	// Layout
	// -------------------------------------------------------------------------

	bool PartyHudPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_initialized)
		{
			LOG_WARN(Render, "[PartyHud] SetViewportSize called before Init");
			return false;
		}
		if (width == 0 || height == 0)
		{
			LOG_WARN(Render, "[PartyHud] SetViewportSize ignored: invalid size {}x{}", width, height);
			return false;
		}

		m_viewportWidth  = width;
		m_viewportHeight = height;
		RebuildLayout();
		m_state.layoutValid = true;
		return true;
	}

	void PartyHudPresenter::RebuildLayout()
	{
		// Frames are stacked vertically along the left edge of the screen.
		for (size_t i = 0; i < kMaxPartyFrames; ++i)
		{
			PartyMemberFrame& f = m_state.frames[i];

			// Frame outer bounds.
			f.frameBounds.x      = kFramePadding;
			f.frameBounds.y      = kFrameTopOffset + static_cast<float>(i) * (kFrameHeight + kFrameGap);
			f.frameBounds.width  = kFrameWidth;
			f.frameBounds.height = kFrameHeight;

			// HP bar: upper half of the frame.
			f.hpBar.bounds.x      = f.frameBounds.x + 2.0f;
			f.hpBar.bounds.y      = f.frameBounds.y + 4.0f;
			f.hpBar.bounds.width  = kFrameWidth - 4.0f;
			f.hpBar.bounds.height = (kFrameHeight * 0.45f);
			f.hpBar.label         = "HP";

			// Mana bar: lower half of the frame.
			f.manaBar.bounds.x      = f.frameBounds.x + 2.0f;
			f.manaBar.bounds.y      = f.frameBounds.y + kFrameHeight * 0.52f;
			f.manaBar.bounds.width  = kFrameWidth - 4.0f;
			f.manaBar.bounds.height = (kFrameHeight * 0.35f);
			f.manaBar.label         = "MP";
		}
	}

	// -------------------------------------------------------------------------
	// Model application
	// -------------------------------------------------------------------------

	bool PartyHudPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)
	{
		if (!m_initialized)
		{
			LOG_WARN(Render, "[PartyHud] ApplyModel called before Init");
			return false;
		}

		if ((changeMask & UIModelChangeParty) == 0)
			return true; // No party change — nothing to do.

		m_state.inParty      = model.inParty;
		m_state.visibleCount = 0;
		m_state.lootModeLabel = model.partyLootModeLabel;

		// Reset all frames first.
		for (size_t i = 0; i < kMaxPartyFrames; ++i)
		{
			m_state.frames[i].visible = false;
			m_state.frames[i].hpBar.visible   = false;
			m_state.frames[i].manaBar.visible  = false;
		}

		if (!model.inParty)
			return true;

		const size_t count = std::min(model.partyMembers.size(), kMaxPartyFrames);
		for (size_t i = 0; i < count; ++i)
			BuildFrame(i, model.partyMembers[i], model.partyMembers[i].isLeader);

		m_state.visibleCount = static_cast<uint8_t>(count);

		LOG_DEBUG(Render, "[PartyHud] ApplyModel: frames={}, loot_mode={}",
		    count, m_state.lootModeLabel);
		return true;
	}

	void PartyHudPresenter::BuildFrame(size_t slot, const UIPartyMemberEntry& entry, bool isLeader)
	{
		PartyMemberFrame& f = m_state.frames[slot];
		f.displayName = entry.displayName;
		f.isLeader    = isLeader;
		f.visible     = true;

		f.hpBar.currentValue = entry.currentHealth;
		f.hpBar.maxValue     = entry.maxHealth;
		f.hpBar.visible      = true;

		f.manaBar.currentValue = entry.currentMana;
		f.manaBar.maxValue     = entry.maxMana;
		f.manaBar.visible      = (entry.maxMana > 0);
	}
}
