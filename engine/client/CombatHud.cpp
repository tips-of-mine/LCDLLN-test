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
		m_state.cooldowns.push_back(HudCooldownWidget{ {}, "secondary",   0.0f, 0.0f, false });
		m_state.cooldowns.push_back(HudCooldownWidget{ {}, "utility",     0.0f, 0.0f, false });
		// Pre-size 10 action bar slots (keys 1-0).
		m_state.actionSlots.resize(10u);
		for (uint8_t i = 0u; i < 10u; ++i)
			m_state.actionSlots[i].slotIndex = i;
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
		UpdateWalletFromModel(model);
		UpdateMinimap(model);
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

	// =========================================================================
	// Design system setters
	// =========================================================================

	void CombatHudPresenter::SetPlayerIdentity(const std::string& name, uint32_t level)
	{
		m_state.playerName  = name;
		m_state.playerLevel = level;
	}

	void CombatHudPresenter::SetPlayerXp(float xpPct)
	{
		m_state.playerXpPct = std::clamp(xpPct, 0.0f, 1.0f);
	}

	void CombatHudPresenter::SetZoneInfo(const std::string& zoneName, const std::string& direction)
	{
		m_state.zoneName       = zoneName;
		m_state.zoneDirection  = direction;
		m_state.minimapVisible = !zoneName.empty();
	}

	// =========================================================================
	// Minimap
	// =========================================================================

	void CombatHudPresenter::UpdateMinimap(const UIModel& model)
	{
		m_state.minimapBlips.clear();
		if (!model.targetStats.hasTarget || !model.targetStats.hasPosition)
		{
			return;
		}
		// Map target relative to player into [0, 1] over a ±100-unit window.
		constexpr float kWindowHalf = 100.0f;
		const float dx = model.targetStats.positionX - model.playerStats.positionX;
		const float dy = model.targetStats.positionZ - model.playerStats.positionZ;
		HudMinimapBlip blip;
		blip.xPct = std::clamp(0.5f + dx / (2.0f * kWindowHalf), 0.0f, 1.0f);
		blip.yPct = std::clamp(0.5f + dy / (2.0f * kWindowHalf), 0.0f, 1.0f);
		blip.kind = 0; // hostile
		m_state.minimapBlips.push_back(blip);
	}

	// =========================================================================
	// Action bar
	// =========================================================================

	void CombatHudPresenter::RebuildActionBar()
	{
		constexpr uint32_t kSlotCount = 10u;
		constexpr float    kSlotSize  = 46.0f;
		constexpr float    kSlotGap   = 6.0f;
		constexpr float    kPadding   = 8.0f;

		const float vw = static_cast<float>(m_viewportWidth  == 0u ? 1280u : m_viewportWidth);
		const float vh = static_cast<float>(m_viewportHeight == 0u ?  720u : m_viewportHeight);
		const float barW = kSlotCount * kSlotSize + (kSlotCount - 1u) * kSlotGap + 2.0f * kPadding;
		const float barH = kSlotSize + 2.0f * kPadding;

		m_state.actionBarBounds = {
			(vw - barW) * 0.5f,
			vh - 18.0f - barH,
			barW,
			barH
		};

		m_state.actionSlots.resize(kSlotCount);
		for (uint32_t i = 0u; i < kSlotCount; ++i)
		{
			HudActionSlot& slot = m_state.actionSlots[i];
			slot.slotIndex = static_cast<uint8_t>(i);
			slot.bounds = {
				m_state.actionBarBounds.x + kPadding + static_cast<float>(i) * (kSlotSize + kSlotGap),
				m_state.actionBarBounds.y + kPadding,
				kSlotSize,
				kSlotSize
			};
		}
	}

	void CombatHudPresenter::RebuildLayout()
	{
		const float viewportWidth  = static_cast<float>(m_viewportWidth  == 0u ? 1280u : m_viewportWidth);
		const float viewportHeight = static_cast<float>(m_viewportHeight == 0u ?  720u : m_viewportHeight);
		const float margin      = std::max(16.0f, viewportWidth * 0.025f);
		const float panelWidth  = std::clamp(viewportWidth * 0.28f, 280.0f, 420.0f);
		// Design system: portrait panel is top-left (18 px from edges, matching HudOverlay).
		constexpr float kPortraitSize  = 52.0f;
		constexpr float kPortraitPad   = 10.0f;
		const float barAreaWidth = panelWidth - kPortraitSize - 3.0f * kPortraitPad;
		const float barHeight    = 5.0f; // thin bars matching design (PV/MN/XP)
		const float barSpacing   = 4.0f;
		const float panelHeight  = kPortraitSize + 2.0f * kPortraitPad;

		// Top-left player portrait panel.
		m_state.panelBounds   = { margin, margin, panelWidth, panelHeight };
		m_state.portraitBounds = { margin + kPortraitPad, margin + kPortraitPad, kPortraitSize, kPortraitSize };

		const float barX = margin + kPortraitPad + kPortraitSize + kPortraitPad;
		// Name label sits at barX, margin+14 (rendered by glyph pass — no separate bounds needed).
		const float firstBarY = margin + kPortraitPad + 20.0f; // below name label (~16 px)
		m_state.playerHealthBar.bounds = { barX, firstBarY, barAreaWidth, barHeight };
		m_state.playerManaBar.bounds   = { barX, firstBarY + barHeight + barSpacing, barAreaWidth, barHeight };
		m_state.playerXpBarBounds      = { barX, firstBarY + 2.0f * (barHeight + barSpacing), barAreaWidth, barHeight };

		// Target frame (top-centre).
		const float targetWidth  = std::clamp(viewportWidth * 0.24f, 220.0f, 360.0f);
		const float targetHeight = std::clamp(viewportHeight * 0.1f, 64.0f, 96.0f);
		m_state.targetFrameBounds = { (viewportWidth - targetWidth) * 0.5f, margin, targetWidth, targetHeight };
		m_state.targetHealthBar.bounds = {
			m_state.targetFrameBounds.x + 12.0f,
			m_state.targetFrameBounds.y + 32.0f,
			targetWidth - 24.0f,
			std::max(14.0f, targetHeight * 0.22f)
		};

		// Combat log (below portrait panel, left side).
		const float logHeight = std::clamp(viewportHeight * 0.16f, 84.0f, 160.0f);
		m_state.combatLogBounds = { margin, margin + panelHeight + 8.0f, panelWidth, logHeight };

		// Legacy cooldown widgets (right of portrait panel).
		if (!m_state.cooldowns.empty())
		{
			const float cooldownSize = std::clamp(viewportWidth * 0.05f, 40.0f, 64.0f);
			for (size_t index = 0; index < m_state.cooldowns.size(); ++index)
			{
				m_state.cooldowns[index].bounds = {
					m_state.panelBounds.x + panelWidth + 12.0f + static_cast<float>(index) * (cooldownSize + 8.0f),
					margin,
					cooldownSize,
					cooldownSize
				};
			}
		}

		// M35.1 — wallet strip (top-right, above minimap).
		{
			const float walletW = std::clamp(viewportWidth * 0.18f, 160.0f, 280.0f);
			m_state.walletBounds = { viewportWidth - margin - walletW, margin, walletW, 26.0f };
		}

		// Minimap (top-right, below wallet).
		{
			constexpr float kMinimapSize = 160.0f;
			const float mmW = kMinimapSize + 20.0f; // 10 px padding each side
			const float mmH = kMinimapSize + 36.0f; // 26 px label + 10 px padding
			m_state.minimapBounds = { viewportWidth - margin - mmW, margin + 32.0f, mmW, mmH };
		}

		// Bottom-centre action bar (10 slots).
		RebuildActionBar();

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

		m_state.playerHasCombo = model.playerStats.hasCombo;
		m_state.playerComboPoints = model.playerStats.hasCombo ? model.playerStats.comboPoints : 0u;
		m_state.playerMaxComboPoints = model.playerStats.hasCombo ? model.playerStats.maxCombo : 0u;
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

	void CombatHudPresenter::UpdateWalletFromModel(const UIModel& model)
	{
		if (!model.wallet.hasWallet)
		{
			m_state.walletVisible = false;
			m_state.walletGoldLine.clear();
			return;
		}

		m_state.walletVisible = true;
		m_state.walletGoldLine = "[G] " + std::to_string(model.wallet.gold);
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
		m_state.debugText += "player: ";
		m_state.debugText += m_state.playerName.empty() ? "(unnamed)" : m_state.playerName;
		m_state.debugText += " lv=";
		m_state.debugText += std::to_string(m_state.playerLevel);
		m_state.debugText += " xp=";
		m_state.debugText += std::to_string(static_cast<int>(m_state.playerXpPct * 100.0f));
		m_state.debugText += "%\n";
		m_state.debugText += "zone: ";
		m_state.debugText += m_state.zoneName.empty() ? "(n/a)" : m_state.zoneName;
		m_state.debugText += " ";
		m_state.debugText += m_state.zoneDirection;
		m_state.debugText += " blips=";
		m_state.debugText += std::to_string(m_state.minimapBlips.size());
		m_state.debugText += "\n";
		m_state.debugText += "wallet: ";
		m_state.debugText += m_state.walletVisible ? m_state.walletGoldLine : "(n/a)";
		m_state.debugText += "\n";
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
		m_state.debugText += " combo=";
		m_state.debugText += m_state.playerHasCombo ? std::to_string(m_state.playerComboPoints) : "n/a";
		m_state.debugText += "/";
		m_state.debugText += m_state.playerHasCombo ? std::to_string(m_state.playerMaxComboPoints) : "n/a";
		m_state.debugText += " actionSlots=";
		m_state.debugText += std::to_string(m_state.actionSlots.size());
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
