#include "engine/client/CombatAnalyticsUi.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <ctime>
#include <sstream>

namespace engine::client
{
	// ── Helpers ───────────────────────────────────────────────────────────────

	std::string CombatAnalyticsUiPresenter::FormatTimestamp(uint64_t wallClockMs)
	{
		const time_t secs    = static_cast<time_t>(wallClockMs / 1000u);
		const struct tm* tm_ = std::gmtime(&secs);
		if (!tm_) return "[00:00:00]";

		char buf[16];
		std::snprintf(buf, sizeof(buf), "[%02d:%02d:%02d]",
		    tm_->tm_hour, tm_->tm_min, tm_->tm_sec);
		return std::string(buf);
	}

	// ── Lifecycle ─────────────────────────────────────────────────────────────

	CombatAnalyticsUiPresenter::~CombatAnalyticsUiPresenter()
	{
		Shutdown();
	}

	bool CombatAnalyticsUiPresenter::Init()
	{
		m_state        = {};
		m_dmgRecords.clear();
		m_threatRecords.clear();
		m_allLogEntries.clear();
		m_allLogEntries.reserve(kCombatLogMaxLines);
		m_inCombat       = false;
		m_elapsedSeconds = 0.0f;
		m_initialized    = true;

		LOG_INFO(Core, "[CombatAnalyticsUi] Init OK (logCapacity={})", kCombatLogMaxLines);
		return true;
	}

	void CombatAnalyticsUiPresenter::Shutdown()
	{
		if (!m_initialized) return;
		m_state         = {};
		m_dmgRecords.clear();
		m_threatRecords.clear();
		m_allLogEntries.clear();
		m_initialized   = false;
		LOG_INFO(Core, "[CombatAnalyticsUi] Shutdown");
	}

	// ── DPS meter ─────────────────────────────────────────────────────────────

	void CombatAnalyticsUiPresenter::OnCombatEvent(
	    engine::server::EntityId attackerEntityId,
	    std::string_view         attackerDisplayName,
	    uint32_t                 damage,
	    uint64_t                 wallClockMs)
	{
		if (!m_inCombat)
		{
			m_inCombat        = true;
			m_elapsedSeconds  = 0.0f;
			LOG_DEBUG(Core, "[CombatAnalyticsUi] Combat started");
		}

		// Find or create damage record for this attacker.
		auto it = std::find_if(m_dmgRecords.begin(), m_dmgRecords.end(),
		    [attackerEntityId](const DmgRecord& r) { return r.entityId == attackerEntityId; });

		if (it == m_dmgRecords.end())
		{
			DmgRecord rec{};
			rec.entityId    = attackerEntityId;
			rec.displayName = std::string(attackerDisplayName);
			rec.totalDamage = damage;
			m_dmgRecords.push_back(std::move(rec));
		}
		else
		{
			it->totalDamage += damage;
		}

		// Also add to combat log.
		const std::string ts   = FormatTimestamp(wallClockMs);
		const std::string text =
		    std::string(attackerDisplayName) + " deals " + std::to_string(damage) + " damage";
		AddLogEntry(ts, text, CombatLogCategory::Damage);

		RebuildDpsMeter();
	}

	void CombatAnalyticsUiPresenter::Tick(float deltaSeconds)
	{
		if (!m_inCombat) return;

		m_elapsedSeconds             += deltaSeconds;
		m_state.dps.elapsedSeconds    = m_elapsedSeconds;
		m_state.dps.inCombat          = true;

		// Recompute DPS for all tracked entities.
		const float elapsed = (m_elapsedSeconds > 0.0f) ? m_elapsedSeconds : 1.0f;
		for (DmgRecord& rec : m_dmgRecords)
		{
			// DPS = total damage / fight duration
			// (the DPS value is stored in the DpsMeterEntry)
		}
		(void)elapsed;

		RebuildDpsMeter();
	}

	void CombatAnalyticsUiPresenter::OnCombatEnd()
	{
		m_inCombat           = false;
		m_state.dps.inCombat = false;
		RebuildDpsMeter();
		LOG_INFO(Core, "[CombatAnalyticsUi] Combat ended (elapsed={:.1f}s)", m_elapsedSeconds);
	}

	void CombatAnalyticsUiPresenter::ResetDpsMeter()
	{
		m_dmgRecords.clear();
		m_inCombat           = false;
		m_elapsedSeconds     = 0.0f;
		m_state.dps          = {};
		LOG_INFO(Core, "[CombatAnalyticsUi] DPS meter reset");
	}

	void CombatAnalyticsUiPresenter::RebuildDpsMeter()
	{
		const float elapsed = (m_elapsedSeconds > 0.0f) ? m_elapsedSeconds : 1.0f;

		// Build sorted DPS entries (all damage records).
		std::vector<DpsMeterEntry> entries;
		entries.reserve(m_dmgRecords.size());
		for (const DmgRecord& rec : m_dmgRecords)
		{
			DpsMeterEntry e{};
			e.entityId    = rec.entityId;
			e.displayName = rec.displayName;
			e.totalDamage = rec.totalDamage;
			e.dps         = static_cast<float>(rec.totalDamage) / elapsed;
			entries.push_back(e);
		}

		// Sort descending by DPS.
		std::sort(entries.begin(), entries.end(),
		    [](const DpsMeterEntry& a, const DpsMeterEntry& b) { return a.dps > b.dps; });

		// Cap to top-5.
		if (entries.size() > kMeterMaxEntries)
			entries.resize(kMeterMaxEntries);

		// Compute bar fractions relative to top DPS.
		const float topDps = entries.empty() ? 1.0f
		                                     : (entries.front().dps > 0.0f ? entries.front().dps : 1.0f);
		for (DpsMeterEntry& e : entries)
			e.barFraction = e.dps / topDps;

		m_state.dps.entries        = std::move(entries);
		m_state.dps.elapsedSeconds = m_elapsedSeconds;
		m_state.dps.inCombat       = m_inCombat;
	}

	// ── Threat meter ──────────────────────────────────────────────────────────

	void CombatAnalyticsUiPresenter::OnThreatUpdate(
	    engine::server::EntityId targetEntityId,
	    engine::server::EntityId entityId,
	    std::string_view         displayName,
	    float                    threatValue)
	{
		m_state.threat.targetEntityId = targetEntityId;

		// Find or insert the threat record.
		auto it = std::find_if(m_threatRecords.begin(), m_threatRecords.end(),
		    [entityId](const ThreatRecord& r) { return r.entityId == entityId; });

		if (it == m_threatRecords.end())
		{
			ThreatRecord rec{};
			rec.entityId    = entityId;
			rec.displayName = std::string(displayName);
			rec.threatValue = threatValue;
			m_threatRecords.push_back(std::move(rec));
		}
		else
		{
			it->threatValue = threatValue;
		}

		RebuildThreatMeter();
	}

	void CombatAnalyticsUiPresenter::ResetThreatMeter()
	{
		m_threatRecords.clear();
		m_state.threat = {};
		LOG_INFO(Core, "[CombatAnalyticsUi] Threat meter reset");
	}

	void CombatAnalyticsUiPresenter::RebuildThreatMeter()
	{
		// Sort descending by threat value.
		std::vector<ThreatMeterEntry> entries;
		entries.reserve(m_threatRecords.size());
		for (const ThreatRecord& rec : m_threatRecords)
		{
			ThreatMeterEntry e{};
			e.entityId    = rec.entityId;
			e.displayName = rec.displayName;
			e.threatValue = rec.threatValue;
			entries.push_back(e);
		}
		std::sort(entries.begin(), entries.end(),
		    [](const ThreatMeterEntry& a, const ThreatMeterEntry& b) {
			    return a.threatValue > b.threatValue;
		    });

		if (entries.size() > kMeterMaxEntries)
			entries.resize(kMeterMaxEntries);

		const float topThreat = entries.empty() ? 1.0f
		    : (entries.front().threatValue > 0.0f ? entries.front().threatValue : 1.0f);

		for (uint32_t i = 0; i < static_cast<uint32_t>(entries.size()); ++i)
		{
			ThreatMeterEntry& e = entries[i];
			e.barFraction = e.threatValue / topThreat;
			// Color: top = pulling aggro (red), >=80% = warning (yellow), rest = safe (green).
			if (i == 0)
				e.color = ThreatColor::Pulling;
			else if (e.barFraction >= 0.80f)
				e.color = ThreatColor::Warning;
			else
				e.color = ThreatColor::Safe;
		}

		m_state.threat.entries = std::move(entries);
	}

	// ── Combat log ────────────────────────────────────────────────────────────

	void CombatAnalyticsUiPresenter::AddLogEntry(std::string_view    timestamp,
	                                              std::string_view    text,
	                                              CombatLogCategory   category)
	{
		CombatLogEntry entry{};
		entry.timestamp = std::string(timestamp);
		entry.text      = std::string(text);
		entry.category  = category;

		// Ring buffer: evict oldest entry when full.
		if (m_allLogEntries.size() >= kCombatLogMaxLines)
			m_allLogEntries.erase(m_allLogEntries.begin());

		m_allLogEntries.push_back(std::move(entry));
		RebuildVisibleLog();
	}

	void CombatAnalyticsUiPresenter::AddLogEntryNow(uint64_t          wallClockMs,
	                                                 std::string_view  text,
	                                                 CombatLogCategory category)
	{
		AddLogEntry(FormatTimestamp(wallClockMs), text, category);
	}

	void CombatAnalyticsUiPresenter::SetLogFilter(CombatLogCategory category, bool enabled)
	{
		switch (category)
		{
		case CombatLogCategory::Damage:  m_state.log.filter.showDamage  = enabled; break;
		case CombatLogCategory::Healing: m_state.log.filter.showHealing = enabled; break;
		case CombatLogCategory::Buff:    m_state.log.filter.showBuff    = enabled; break;
		case CombatLogCategory::Death:   m_state.log.filter.showDeath   = enabled; break;
		case CombatLogCategory::Other:   m_state.log.filter.showOther   = enabled; break;
		}
		RebuildVisibleLog();
		LOG_DEBUG(Core, "[CombatAnalyticsUi] Log filter category={} enabled={}",
		    static_cast<int>(category), enabled);
	}

	void CombatAnalyticsUiPresenter::ScrollLog(int32_t delta)
	{
		const auto maxOffset = static_cast<int32_t>(m_state.log.visibleLines.size());
		int32_t newOffset    = static_cast<int32_t>(m_state.log.scrollOffset) + delta;
		newOffset            = std::max(0, std::min(newOffset, maxOffset));
		m_state.log.scrollOffset = static_cast<uint32_t>(newOffset);
	}

	void CombatAnalyticsUiPresenter::RebuildVisibleLog()
	{
		m_state.log.lines = m_allLogEntries;
		m_state.log.visibleLines.clear();
		m_state.log.visibleLines.reserve(m_state.log.lines.size());

		for (const CombatLogEntry& entry : m_state.log.lines)
		{
			bool visible = false;
			switch (entry.category)
			{
			case CombatLogCategory::Damage:  visible = m_state.log.filter.showDamage;  break;
			case CombatLogCategory::Healing: visible = m_state.log.filter.showHealing; break;
			case CombatLogCategory::Buff:    visible = m_state.log.filter.showBuff;    break;
			case CombatLogCategory::Death:   visible = m_state.log.filter.showDeath;   break;
			case CombatLogCategory::Other:   visible = m_state.log.filter.showOther;   break;
			}
			if (visible)
				m_state.log.visibleLines.push_back(&entry);
		}

		// Clamp scroll offset to new list size.
		const auto maxOff = static_cast<uint32_t>(m_state.log.visibleLines.size());
		if (m_state.log.scrollOffset > maxOff)
			m_state.log.scrollOffset = maxOff;
	}

	bool CombatAnalyticsUiPresenter::ExportLogCsv(const engine::core::Config& config,
	                                               std::string_view relativePath) const
	{
		std::ostringstream csv;
		csv << "Timestamp,Category,Text\n";

		for (const CombatLogEntry& entry : m_allLogEntries)
		{
			const char* catName = "Other";
			switch (entry.category)
			{
			case CombatLogCategory::Damage:  catName = "Damage";  break;
			case CombatLogCategory::Healing: catName = "Healing"; break;
			case CombatLogCategory::Buff:    catName = "Buff";    break;
			case CombatLogCategory::Death:   catName = "Death";   break;
			case CombatLogCategory::Other:   catName = "Other";   break;
			}
			// Escape double-quotes in text per CSV spec.
			std::string safeText = entry.text;
			size_t pos = 0;
			while ((pos = safeText.find('"', pos)) != std::string::npos)
			{
				safeText.insert(pos, 1, '"');
				pos += 2;
			}
			csv << entry.timestamp << ',' << catName << ',' << '"' << safeText << '"' << '\n';
		}

		if (!engine::platform::FileSystem::WriteAllTextContent(config, relativePath, csv.str()))
		{
			LOG_ERROR(Core, "[CombatAnalyticsUi] CSV export FAILED (path={})", relativePath);
			return false;
		}

		LOG_INFO(Core, "[CombatAnalyticsUi] CSV exported ({} lines) to {}",
		    m_allLogEntries.size(), relativePath);
		return true;
	}

} // namespace engine::client
