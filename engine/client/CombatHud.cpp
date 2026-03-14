#include "engine/client/CombatHud.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <string>
#include <utility>

namespace engine::client
{
	namespace
	{
		inline constexpr float kAutoAttackCooldownSeconds = 0.5f;

		/// Build one short combat log label from a retained combat entry.
		std::string BuildCombatLogText(const UICombatLogEntry& entry)
		{
			if (entry.playerWasAttacker)
			{
				return "Hit target " + std::to_string(entry.targetEntityId) + " for " + std::to_string(entry.damage);
			}

			if (entry.playerWasTarget)
			{
				return "Took " + std::to_string(entry.damage) + " from " + std::to_string(entry.attackerEntityId);
			}

			return "Combat " + std::to_string(entry.damage);
		}
	}

	CombatHudPresenter::~CombatHudPresenter()
	{
		Shutdown();
	}

	bool CombatHudPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[CombatHudPresenter] Init ignored: already initialized");
			return true;
		}

		m_state.cooldowns.clear();
		m_state.cooldowns.push_back(HudCooldownWidget{ {}, "auto_attack", 0.0f, 0.0f, false });
		m_state.cooldowns.push_back(HudCooldownWidget{ {}, "secondary", 0.0f, 0.0f, false });
		m_state.cooldowns.push_back(HudCooldownWidget{ {}, "utility", 0.0f, 0.0f, false });
		m_initialized = true;
		RebuildDebugText();
		LOG_INFO(Core, "[CombatHudPresenter] Init OK (cooldowns={})", m_state.cooldowns.size());
		return true;
	}

	void CombatHudPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_initialized = false;
		m_state = {};
		m_viewportWidth = 0;
		m_viewportHeight = 0;
		m_lastCombatSequence = 0;
		LOG_INFO(Core, "[CombatHudPresenter] Destroyed");
	}

	bool CombatHudPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[CombatHudPresenter] SetViewportSize FAILED: presenter not initialized");
			return false;
		}

		if (width == 0 || height == 0)
		{
			LOG_WARN(Core, "[CombatHudPresenter] SetViewportSize FAILED: invalid viewport {}x{}", width, height);
			return false;
		}

		m_viewportWidth = width;
		m_viewportHeight = height;
		RebuildLayout();
		RebuildDebugText();
		LOG_INFO(Core, "[CombatHudPresenter] Viewport updated ({}x{})", m_viewportWidth, m_viewportHeight);
		return true;
	}

	bool CombatHudPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[CombatHudPresenter] ApplyModel FAILED: presenter not initialized");
			return false;
		}

		if (!m_state.layoutValid)
		{
			LOG_WARN(Core, "[CombatHudPresenter] ApplyModel using fallback layout: viewport not set");
			RebuildLayout();
		}

		UpdatePlayerBars(model);
		UpdateTargetFrame(model);
		UpdateCombatLog(model);
		RefreshCooldowns(model);
		RebuildDebugText();
		LOG_DEBUG(Core, "[CombatHudPresenter] Model applied (change_mask={}, target_visible={}, combat_lines={})",
			changeMask,
			m_state.targetVisible ? "true" : "false",
			m_state.combatLogLines.size());
		return true;
	}

	bool CombatHudPresenter::Tick(float deltaSeconds)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[CombatHudPresenter] Tick FAILED: presenter not initialized");
			return false;
		}

		if (deltaSeconds < 0.0f)
		{
			LOG_WARN(Core, "[CombatHudPresenter] Tick FAILED: negative delta {}", deltaSeconds);
			return false;
		}

		bool changed = false;
		for (HudCooldownWidget& cooldown : m_state.cooldowns)
		{
			if (!cooldown.active)
			{
				continue;
			}

			cooldown.remainingSeconds = std::max(0.0f, cooldown.remainingSeconds - deltaSeconds);
			if (cooldown.remainingSeconds <= 0.0f)
			{
				cooldown.active = false;
				LOG_INFO(Core, "[CombatHudPresenter] Cooldown ready ({})", cooldown.actionId);
			}

			changed = true;
		}

		if (changed)
		{
			RebuildDebugText();
		}

		return true;
	}

	void CombatHudPresenter::RebuildLayout()
	{
		const float viewportWidth = static_cast<float>(m_viewportWidth == 0 ? 1280u : m_viewportWidth);
		const float viewportHeight = static_cast<float>(m_viewportHeight == 0 ? 720u : m_viewportHeight);
		const float margin = std::max(16.0f, viewportWidth * 0.025f);
		const float panelWidth = std::clamp(viewportWidth * 0.28f, 280.0f, 420.0f);
		const float panelHeight = std::clamp(viewportHeight * 0.14f, 88.0f, 140.0f);
		const float barHeight = std::max(18.0f, panelHeight * 0.25f);
		const float barSpacing = std::max(8.0f, panelHeight * 0.1f);

		m_state.panelBounds = { margin, viewportHeight - margin - panelHeight, panelWidth, panelHeight };
		m_state.playerHealthBar.bounds = { margin + 12.0f, m_state.panelBounds.y + 12.0f, panelWidth - 24.0f, barHeight };
		m_state.playerManaBar.bounds = { margin + 12.0f, m_state.playerHealthBar.bounds.y + barHeight + barSpacing, panelWidth - 24.0f, barHeight };

		const float targetWidth = std::clamp(viewportWidth * 0.24f, 220.0f, 360.0f);
		const float targetHeight = std::clamp(viewportHeight * 0.1f, 64.0f, 96.0f);
		m_state.targetFrameBounds = { (viewportWidth - targetWidth) * 0.5f, margin, targetWidth, targetHeight };
		m_state.targetHealthBar.bounds = {
			m_state.targetFrameBounds.x + 12.0f,
			m_state.targetFrameBounds.y + 32.0f,
			targetWidth - 24.0f,
			std::max(18.0f, targetHeight * 0.28f)
		};

		const float logHeight = std::clamp(viewportHeight * 0.16f, 84.0f, 160.0f);
		m_state.combatLogBounds = { margin, m_state.panelBounds.y - 12.0f - logHeight, panelWidth, logHeight };

		if (!m_state.cooldowns.empty())
		{
			const float cooldownSize = std::clamp(viewportWidth * 0.05f, 40.0f, 64.0f);
			for (size_t index = 0; index < m_state.cooldowns.size(); ++index)
			{
				m_state.cooldowns[index].bounds = {
					m_state.panelBounds.x + panelWidth + 12.0f + (static_cast<float>(index) * (cooldownSize + 8.0f)),
					m_state.panelBounds.y + panelHeight - cooldownSize,
					cooldownSize,
					cooldownSize
				};
			}
		}

		m_state.layoutValid = true;
	}

	void CombatHudPresenter::UpdatePlayerBars(const UIModel& model)
	{
		m_state.playerHealthBar.label = "HP";
		m_state.playerHealthBar.currentValue = model.playerStats.currentHealth;
		m_state.playerHealthBar.maxValue = model.playerStats.maxHealth;
		m_state.playerHealthBar.visible = model.playerStats.hasSnapshot;

		m_state.playerManaBar.label = model.playerStats.hasMana ? "Mana" : "Mana (N/A)";
		m_state.playerManaBar.currentValue = model.playerStats.hasMana ? model.playerStats.currentMana : 0u;
		m_state.playerManaBar.maxValue = model.playerStats.hasMana ? model.playerStats.maxMana : 0u;
		m_state.playerManaBar.visible = model.playerStats.hasSnapshot;
	}

	void CombatHudPresenter::UpdateTargetFrame(const UIModel& model)
	{
		m_state.targetVisible = model.targetStats.hasTarget;
		m_state.targetLabel = model.targetStats.hasTarget
			? ("Target " + std::to_string(model.targetStats.entityId))
			: "No target";
		m_state.targetHealthBar.label = "Target HP";
		m_state.targetHealthBar.currentValue = model.targetStats.currentHealth;
		m_state.targetHealthBar.maxValue = model.targetStats.maxHealth;
		m_state.targetHealthBar.visible = model.targetStats.hasTarget;
	}

	void CombatHudPresenter::UpdateCombatLog(const UIModel& model)
	{
		m_state.combatLogLines.clear();
		m_state.combatLogLines.reserve(model.combatLog.size());
		for (const UICombatLogEntry& entry : model.combatLog)
		{
			HudCombatLogLine line{};
			line.text = BuildCombatLogText(entry);
			line.damage = entry.damage;
			line.incoming = entry.playerWasTarget;
			m_state.combatLogLines.push_back(std::move(line));
		}
	}

	void CombatHudPresenter::RefreshCooldowns(const UIModel& model)
	{
		if (model.combatLog.empty())
		{
			m_lastCombatSequence = 0;
			return;
		}

		for (const UICombatLogEntry& entry : model.combatLog)
		{
			if (entry.sequence <= m_lastCombatSequence)
			{
				continue;
			}

			m_lastCombatSequence = entry.sequence;
			if (!entry.playerWasAttacker || m_state.cooldowns.empty())
			{
				continue;
			}

			HudCooldownWidget& cooldown = m_state.cooldowns[0];
			cooldown.durationSeconds = kAutoAttackCooldownSeconds;
			cooldown.remainingSeconds = kAutoAttackCooldownSeconds;
			cooldown.active = true;
			LOG_INFO(Core, "[CombatHudPresenter] Cooldown started ({}, duration_s={:.2f})",
				cooldown.actionId,
				cooldown.durationSeconds);
		}
	}

	void CombatHudPresenter::RebuildDebugText()
	{
		m_state.debugText.clear();
		m_state.debugText += "[CombatHud]\n";
		m_state.debugText += "viewport=";
		m_state.debugText += std::to_string(m_viewportWidth);
		m_state.debugText += "x";
		m_state.debugText += std::to_string(m_viewportHeight);
		m_state.debugText += " layout=";
		m_state.debugText += m_state.layoutValid ? "true" : "false";
		m_state.debugText += "\n";
		m_state.debugText += "player hp=";
		m_state.debugText += std::to_string(m_state.playerHealthBar.currentValue);
		m_state.debugText += "/";
		m_state.debugText += std::to_string(m_state.playerHealthBar.maxValue);
		m_state.debugText += " mana=";
		m_state.debugText += std::to_string(m_state.playerManaBar.currentValue);
		m_state.debugText += "/";
		m_state.debugText += std::to_string(m_state.playerManaBar.maxValue);
		m_state.debugText += "\n";
		m_state.debugText += "target visible=";
		m_state.debugText += m_state.targetVisible ? "true" : "false";
		m_state.debugText += " hp=";
		m_state.debugText += std::to_string(m_state.targetHealthBar.currentValue);
		m_state.debugText += "/";
		m_state.debugText += std::to_string(m_state.targetHealthBar.maxValue);
		m_state.debugText += "\n";
		m_state.debugText += "combat lines=";
		m_state.debugText += std::to_string(m_state.combatLogLines.size());
		m_state.debugText += "\n";
		for (const HudCooldownWidget& cooldown : m_state.cooldowns)
		{
			m_state.debugText += "cooldown ";
			m_state.debugText += cooldown.actionId;
			m_state.debugText += " active=";
			m_state.debugText += cooldown.active ? "true" : "false";
			m_state.debugText += " remaining=";
			m_state.debugText += std::to_string(cooldown.remainingSeconds);
			m_state.debugText += "\n";
		}
	}
}
