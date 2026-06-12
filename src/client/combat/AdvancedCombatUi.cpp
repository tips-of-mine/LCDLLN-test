#include "src/client/combat/AdvancedCombatUi.h"

#include "src/shared/core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace engine::client
{
	// =========================================================================
	// Helpers
	// =========================================================================

	namespace
	{
		/// Format elapsed seconds as [HH:MM:SS] timestamp string.
		std::string FormatTimestamp(float elapsedSec)
		{
			const auto total = static_cast<uint32_t>(elapsedSec);
			const uint32_t h = total / 3600u;
			const uint32_t m = (total % 3600u) / 60u;
			const uint32_t s = total % 60u;
			char buf[16];
			std::snprintf(buf, sizeof(buf), "[%02u:%02u:%02u]", h, m, s);
			return buf;
		}

		/// Map a CombatLogLineType to its CombatLogFilter bit.
		uint32_t TypeToFilterBit(CombatLogLineType t)
		{
			switch (t)
			{
			case CombatLogLineType::Damage:  return CombatLogFilterDamage;
			case CombatLogLineType::Healing: return CombatLogFilterHealing;
			case CombatLogLineType::Buff:    return CombatLogFilterBuffs;
			case CombatLogLineType::Debuff:  return CombatLogFilterBuffs;
			case CombatLogLineType::Death:   return CombatLogFilterDeaths;
			default:                         return CombatLogFilterAll;
			}
		}
	}

	// =========================================================================
	// AdvancedCombatPresenter — lifecycle
	// =========================================================================

	AdvancedCombatPresenter::~AdvancedCombatPresenter()
	{
		Shutdown();
	}

	bool AdvancedCombatPresenter::Init()
	{
		if (m_initialized)
		{
			Shutdown();
		}

		m_state              = AdvancedCombatState{};
		m_state.activeFilter = CombatLogFilterAll;
		m_damageMap.clear();
		m_threatMap.clear();
		m_logBuffer.clear();
		m_lastCombatSeq = 0;
		m_wallClockSec  = 0.0f;

		m_initialized = true;
		LOG_INFO(Core, "[AdvancedCombatUi] Init OK (logCapacity={}, dpsRows={})",
		         kCombatLogCapacity, kDpsMeterMaxRows);
		return true;
	}

	void AdvancedCombatPresenter::Shutdown()
	{
		if (!m_initialized)
			return;

		m_damageMap.clear();
		m_threatMap.clear();
		m_logBuffer.clear();
		m_state       = AdvancedCombatState{};
		m_initialized = false;
		LOG_INFO(Core, "[AdvancedCombatUi] Shutdown complete");
	}

	// =========================================================================
	// Per-frame update
	// =========================================================================

	void AdvancedCombatPresenter::Tick(float deltaSeconds)
	{
		if (!m_initialized || deltaSeconds <= 0.0f)
			return;

		m_wallClockSec += deltaSeconds;

		if (m_state.inCombat)
		{
			m_state.fightElapsedSec += deltaSeconds;
			// Recompute DPS values once per tick.
			RebuildDpsMeter();
		}
	}

	void AdvancedCombatPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)
	{
		if (!m_initialized)
			return;
		if (!(changeMask & UIModelChangeCombat))
			return;

		// Process new combat events since m_lastCombatSeq.
		bool hasNewEvents = false;
		for (const UICombatLogEntry& entry : model.combatLog)
		{
			if (entry.sequence <= m_lastCombatSeq)
				continue;

			m_lastCombatSeq = entry.sequence;
			hasNewEvents    = true;

			// ---- DPS accumulation ----
			if (entry.damage > 0)
			{
				m_damageMap[entry.attackerEntityId] += entry.damage;
				if (!m_state.inCombat)
				{
					m_state.inCombat        = true;
					m_state.fightElapsedSec = 0.0f;
					LOG_INFO(Core, "[AdvancedCombatUi] Combat started (first damage event seq={})",
					         entry.sequence);
				}
			}

			// ---- Combat log ----
			PushLogLine(FormatCombatEvent(entry));
		}

		if (hasNewEvents)
		{
			RebuildDpsMeter();
			RebuildDebugText();
		}
	}

	// =========================================================================
	// Threat feed
	// =========================================================================

	void AdvancedCombatPresenter::UpdateThreat(engine::server::EntityId targetId,
	                                            engine::server::EntityId playerId,
	                                            std::string_view         displayName,
	                                            uint64_t                 threatValue)
	{
		if (!m_initialized)
			return;

		m_state.threatTargetId = targetId;

		auto& playerEntry            = m_threatMap[targetId][playerId];
		playerEntry.entityId         = playerId;
		playerEntry.displayName      = displayName.empty()
		                               ? MakeDisplayName(playerId)
		                               : std::string(displayName);
		playerEntry.threatValue      = threatValue;

		RebuildThreatMeter();
	}

	void AdvancedCombatPresenter::ClearThreat(engine::server::EntityId targetId)
	{
		if (!m_initialized)
			return;

		if (targetId == 0)
		{
			m_threatMap.clear();
			m_state.threatMeter.clear();
			m_state.threatTargetId = 0;
		}
		else
		{
			m_threatMap.erase(targetId);
			if (m_state.threatTargetId == targetId)
			{
				m_state.threatTargetId = 0;
				m_state.threatMeter.clear();
			}
		}
		// Validation v12 — Engine appelle ClearThreat à CHAQUE frame avant de
		// repousser la table de menace courante : ce log en INFO inondait le
		// fichier (constat log client : des milliers de lignes par minute).
		LOG_DEBUG(Core, "[AdvancedCombatUi] Threat cleared (targetId={})", targetId);
	}

	// =========================================================================
	// DPS controls
	// =========================================================================

	void AdvancedCombatPresenter::ResetDps()
	{
		if (!m_initialized)
			return;

		m_damageMap.clear();
		m_state.dpsMeter.clear();
		m_state.inCombat        = false;
		m_state.fightElapsedSec = 0.0f;
		AddInfoLine("--- Combat reset ---");
		LOG_INFO(Core, "[AdvancedCombatUi] DPS reset");
	}

	// =========================================================================
	// Combat log controls
	// =========================================================================

	void AdvancedCombatPresenter::SetLogFilter(uint32_t filterMask)
	{
		if (!m_initialized)
			return;
		m_state.activeFilter = filterMask;
		m_state.scrollOffset = 0;
		RebuildVisibleLog();
	}

	void AdvancedCombatPresenter::ScrollLog(int32_t deltaLines)
	{
		if (!m_initialized)
			return;

		// Build a quick count of filtered lines.
		size_t filteredCount = 0;
		for (const CombatLogLine& l : m_logBuffer)
		{
			if (TypeToFilterBit(l.type) & m_state.activeFilter)
				++filteredCount;
		}

		const size_t maxVisible = std::min(kCombatLogVisibleLines, filteredCount);
		const size_t maxOffset  = filteredCount > maxVisible ? filteredCount - maxVisible : 0u;

		if (deltaLines > 0)
			m_state.scrollOffset = std::min(m_state.scrollOffset + static_cast<size_t>(deltaLines), maxOffset);
		else if (deltaLines < 0 && m_state.scrollOffset >= static_cast<size_t>(-deltaLines))
			m_state.scrollOffset -= static_cast<size_t>(-deltaLines);
		else if (deltaLines < 0)
			m_state.scrollOffset = 0;

		RebuildVisibleLog();
	}

	void AdvancedCombatPresenter::ScrollToBottom()
	{
		if (!m_initialized)
			return;
		m_state.scrollOffset = SIZE_MAX; // Will be clamped in RebuildVisibleLog.
		RebuildVisibleLog();
	}

	void AdvancedCombatPresenter::AddInfoLine(std::string_view message)
	{
		if (!m_initialized)
			return;
		CombatLogLine line;
		line.text       = FormatTimestamp(m_wallClockSec) + " " + std::string(message);
		line.type       = CombatLogLineType::Info;
		line.elapsedSec = m_wallClockSec;
		PushLogLine(std::move(line));
	}

	// =========================================================================
	// Visibility toggles
	// =========================================================================

	void AdvancedCombatPresenter::SetDpsMeterVisible(bool visible)
	{
		m_state.dpsMeterVisible = visible;
	}

	void AdvancedCombatPresenter::SetThreatMeterVisible(bool visible)
	{
		m_state.threatMeterVisible = visible;
	}

	void AdvancedCombatPresenter::SetCombatLogVisible(bool visible)
	{
		m_state.combatLogVisible = visible;
	}

	// =========================================================================
	// Export to CSV (optional, M39.4 step 4)
	// =========================================================================

	bool AdvancedCombatPresenter::ExportLogToCsv(const std::string& filePath) const
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[AdvancedCombatUi] ExportLogToCsv: not initialized");
			return false;
		}
		if (filePath.empty())
		{
			LOG_WARN(Core, "[AdvancedCombatUi] ExportLogToCsv: empty file path");
			return false;
		}

		std::ofstream out(filePath, std::ios::out | std::ios::trunc);
		if (!out)
		{
			LOG_ERROR(Core, "[AdvancedCombatUi] ExportLogToCsv FAILED: cannot open '{}'", filePath);
			return false;
		}

		out << "elapsed_sec,type,text\n";
		for (const CombatLogLine& l : m_logBuffer)
		{
			out << l.elapsedSec << ','
			    << static_cast<uint32_t>(l.type) << ','
			    << '"' << l.text << '"' << '\n';
		}

		LOG_INFO(Core, "[AdvancedCombatUi] Log exported to '{}' ({} lines)", filePath, m_logBuffer.size());
		return true;
	}

	// =========================================================================
	// Private — DPS helpers
	// =========================================================================

	void AdvancedCombatPresenter::RebuildDpsMeter()
	{
		m_state.dpsMeter.clear();
		if (m_damageMap.empty())
			return;

		const float elapsed = m_state.fightElapsedSec > 0.0f ? m_state.fightElapsedSec : 1.0f;

		// Collect all entries.
		std::vector<DpsMeterEntry> all;
		all.reserve(m_damageMap.size());
		for (const auto& [eid, dmg] : m_damageMap)
		{
			DpsMeterEntry e;
			e.entityId    = eid;
			e.displayName = MakeDisplayName(eid);
			e.totalDamage = dmg;
			e.dps         = static_cast<float>(dmg) / elapsed;
			all.push_back(e);
		}

		// Sort descending by DPS.
		std::sort(all.begin(), all.end(), [](const DpsMeterEntry& a, const DpsMeterEntry& b)
		{
			return a.dps > b.dps;
		});

		// Keep top-N.
		const size_t count = std::min(all.size(), kDpsMeterMaxRows);
		const float  topDps = count > 0 ? all[0].dps : 1.0f;

		for (size_t i = 0; i < count; ++i)
		{
			all[i].rank        = static_cast<uint32_t>(i + 1u);
			all[i].barFraction = topDps > 0.0f ? all[i].dps / topDps : 0.0f;
			m_state.dpsMeter.push_back(all[i]);
		}
	}

	/*static*/ std::string AdvancedCombatPresenter::MakeDisplayName(
	    engine::server::EntityId entityId, std::string_view knownName)
	{
		if (!knownName.empty())
			return std::string(knownName);
		return "Entity_" + std::to_string(entityId);
	}

	// =========================================================================
	// Private — threat helpers
	// =========================================================================

	void AdvancedCombatPresenter::RebuildThreatMeter()
	{
		m_state.threatMeter.clear();

		const auto it = m_threatMap.find(m_state.threatTargetId);
		if (it == m_threatMap.end())
			return;

		// Collect all player entries for this target.
		uint64_t maxThreat = 0u;
		for (const auto& [pid, entry] : it->second)
			maxThreat = std::max(maxThreat, entry.threatValue);

		std::vector<ThreatMeterEntry> entries;
		entries.reserve(it->second.size());
		for (auto& [pid, entry] : it->second)
		{
			ThreatMeterEntry e  = entry;
			const float frac    = maxThreat > 0u
			                      ? static_cast<float>(e.threatValue) / static_cast<float>(maxThreat)
			                      : 0.0f;
			e.threatPercent     = frac * 100.0f;
			e.barFraction       = frac;
			// Color code: red if at max, yellow if >= 80%, else green.
			if (e.threatValue == maxThreat)
				e.color = ThreatColor::Red;
			else if (frac >= 0.80f)
				e.color = ThreatColor::Yellow;
			else
				e.color = ThreatColor::Green;
			entries.push_back(e);
		}

		// Sort descending by threat.
		std::sort(entries.begin(), entries.end(), [](const ThreatMeterEntry& a,
		                                              const ThreatMeterEntry& b)
		{
			return a.threatValue > b.threatValue;
		});

		m_state.threatMeter = std::move(entries);
	}

	// =========================================================================
	// Private — combat log helpers
	// =========================================================================

	void AdvancedCombatPresenter::PushLogLine(CombatLogLine line)
	{
		if (m_logBuffer.size() >= kCombatLogCapacity)
			m_logBuffer.pop_front();

		m_logBuffer.push_back(std::move(line));
		m_state.totalLogLines = m_logBuffer.size();
		RebuildVisibleLog();
	}

	void AdvancedCombatPresenter::RebuildVisibleLog()
	{
		m_state.visibleLogLines.clear();

		// Collect filtered lines.
		std::vector<const CombatLogLine*> filtered;
		filtered.reserve(m_logBuffer.size());
		for (const CombatLogLine& l : m_logBuffer)
		{
			if (TypeToFilterBit(l.type) & m_state.activeFilter)
				filtered.push_back(&l);
		}

		const size_t total    = filtered.size();
		const size_t maxVis   = std::min(kCombatLogVisibleLines, total);
		const size_t maxOff   = total > maxVis ? total - maxVis : 0u;

		// Clamp scroll offset.
		m_state.scrollOffset = std::min(m_state.scrollOffset, maxOff);

		// Copy the visible slice (oldest→newest relative to scroll).
		for (size_t i = m_state.scrollOffset;
		     i < filtered.size() && m_state.visibleLogLines.size() < maxVis;
		     ++i)
		{
			m_state.visibleLogLines.push_back(*filtered[i]);
		}
	}

	CombatLogLine AdvancedCombatPresenter::FormatCombatEvent(const UICombatLogEntry& entry) const
	{
		CombatLogLine line;
		line.elapsedSec = m_wallClockSec;
		line.type       = CombatLogLineType::Damage;

		const std::string ts    = FormatTimestamp(m_wallClockSec);
		const std::string atk   = MakeDisplayName(entry.attackerEntityId);
		const std::string tgt   = MakeDisplayName(entry.targetEntityId);
		const std::string dmgStr = std::to_string(entry.damage);

		// Combat SP2 — libellés raté/critique depuis les flags wire v10.
		const std::string critSuffix = entry.wasCrit ? " (CRIT)" : "";
		if (entry.wasMiss)
		{
			if (entry.playerWasAttacker)
				line.text = ts + " You miss " + tgt;
			else if (entry.playerWasTarget)
				line.text = ts + " " + atk + " misses you";
			else
				line.text = ts + " " + atk + " misses " + tgt;
			return line;
		}

		if (entry.playerWasAttacker)
		{
			line.text = ts + " You hit " + tgt + " for " + dmgStr + critSuffix;
		}
		else if (entry.playerWasTarget)
		{
			line.text = ts + " " + atk + " hits you for " + dmgStr + critSuffix;
		}
		else
		{
			line.text = ts + " " + atk + " hits " + tgt + " for " + dmgStr + critSuffix;
		}

		return line;
	}

	// =========================================================================
	// Private — debug text
	// =========================================================================

	void AdvancedCombatPresenter::RebuildDebugText()
	{
		std::ostringstream ss;
		ss << "[AdvancedCombatUi M39.4]"
		   << " inCombat=" << m_state.inCombat
		   << " elapsed=" << m_state.fightElapsedSec << "s"
		   << " log=" << m_logBuffer.size() << "/" << kCombatLogCapacity << "\n";

		ss << "DPS meter (" << m_state.dpsMeter.size() << " entries):\n";
		for (const DpsMeterEntry& e : m_state.dpsMeter)
		{
			ss << "  #" << e.rank << " " << e.displayName
			   << " dps=" << e.dps
			   << " total=" << e.totalDamage << "\n";
		}

		ss << "Threat meter (" << m_state.threatMeter.size() << " entries):\n";
		for (const ThreatMeterEntry& e : m_state.threatMeter)
		{
			const char* col = e.color == ThreatColor::Red   ? "RED"
			                : e.color == ThreatColor::Yellow ? "YEL" : "GRN";
			ss << "  " << e.displayName
			   << " " << e.threatPercent << "% [" << col << "]\n";
		}

		m_state.debugText = ss.str();
	}

} // namespace engine::client
