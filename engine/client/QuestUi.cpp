#include "engine/client/QuestUi.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <array>

namespace engine::client
{
	namespace
	{
		/// Trim spaces and tabs from both ends of one text view.
		std::string_view Trim(std::string_view value)
		{
			size_t begin = 0;
			size_t end = value.size();
			while (begin < end && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r'))
			{
				++begin;
			}
			while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r'))
			{
				--end;
			}
			return value.substr(begin, end - begin);
		}

		/// Split one line into at most three `|` separated columns.
		std::array<std::string_view, 3> SplitColumns(std::string_view line, size_t& outCount)
		{
			std::array<std::string_view, 3> columns{};
			outCount = 0;
			size_t start = 0;
			while (start <= line.size() && outCount < columns.size())
			{
				const size_t separator = line.find('|', start);
				if (separator == std::string_view::npos)
				{
					columns[outCount++] = line.substr(start);
					break;
				}

				columns[outCount++] = line.substr(start, separator - start);
				start = separator + 1;
			}
			return columns;
		}
	}

	QuestUiPresenter::~QuestUiPresenter()
	{
		Shutdown();
	}

	bool QuestUiPresenter::Init(const engine::core::Config& config)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[QuestUiPresenter] Init ignored: already initialized");
			return true;
		}

		m_relativeZoneMetadataPath = config.GetString("ui.minimap_zones_path", "ui/minimap_zones.txt");
		if (!LoadZoneMetadata(config))
		{
			LOG_ERROR(Core, "[QuestUiPresenter] Init FAILED: minimap metadata load failed ({})", m_relativeZoneMetadataPath);
			return false;
		}

		m_initialized = true;
		RebuildLayout();
		RebuildDebugText();
		LOG_INFO(Core, "[QuestUiPresenter] Init OK (zones={}, path={})", m_zoneMetadata.size(), m_relativeZoneMetadataPath);
		return true;
	}

	void QuestUiPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_initialized = false;
		m_state = {};
		m_lastModel = {};
		m_zoneMetadata.clear();
		m_selectedQuestId.clear();
		m_viewportWidth = 0;
		m_viewportHeight = 0;
		m_relativeZoneMetadataPath.clear();
		LOG_INFO(Core, "[QuestUiPresenter] Destroyed");
	}

	bool QuestUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[QuestUiPresenter] SetViewportSize FAILED: presenter not initialized");
			return false;
		}

		if (width == 0 || height == 0)
		{
			LOG_WARN(Core, "[QuestUiPresenter] SetViewportSize FAILED: invalid viewport {}x{}", width, height);
			return false;
		}

		m_viewportWidth = width;
		m_viewportHeight = height;
		RebuildLayout();
		RebuildDebugText();
		LOG_INFO(Core, "[QuestUiPresenter] Viewport updated ({}x{})", width, height);
		return true;
	}

	bool QuestUiPresenter::ApplyModel(const UIModel& model, uint32_t changeMask)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[QuestUiPresenter] ApplyModel FAILED: presenter not initialized");
			return false;
		}

		if (!m_state.layoutValid)
		{
			LOG_WARN(Core, "[QuestUiPresenter] ApplyModel using fallback layout: viewport not set");
			RebuildLayout();
		}

		m_lastModel = model;
		RebuildJournal(model);
		RebuildTracker(model);
		RebuildMinimap(model);
		RebuildDebugText();
		LOG_DEBUG(Core, "[QuestUiPresenter] Model applied (change_mask={}, quests={}, zone={})",
			changeMask,
			model.quests.size(),
			model.playerStats.zoneId);
		return true;
	}

	bool QuestUiPresenter::SelectQuest(std::string_view questId)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[QuestUiPresenter] SelectQuest FAILED: presenter not initialized");
			return false;
		}

		m_selectedQuestId = std::string(questId);
		RebuildJournal(m_lastModel);
		RebuildTracker(m_lastModel);
		RebuildMinimap(m_lastModel);
		RebuildDebugText();
		LOG_INFO(Core, "[QuestUiPresenter] Quest selected ({})", m_selectedQuestId);
		return true;
	}

	bool QuestUiPresenter::LoadZoneMetadata(const engine::core::Config& config)
	{
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, m_relativeZoneMetadataPath);
		if (text.empty())
		{
			LOG_ERROR(Core, "[QuestUiPresenter] Zone metadata load FAILED: empty or missing file {}", m_relativeZoneMetadataPath);
			return false;
		}

		m_zoneMetadata.clear();
		size_t lineStart = 0;
		while (lineStart <= text.size())
		{
			const size_t lineEnd = text.find('\n', lineStart);
			const std::string_view rawLine = (lineEnd == std::string::npos)
				? std::string_view(text).substr(lineStart)
				: std::string_view(text).substr(lineStart, lineEnd - lineStart);
			const std::string_view line = Trim(rawLine);
			if (!line.empty() && !line.starts_with('#'))
			{
				MinimapZoneMetadata metadata{};
				if (ParseZoneMetadataLine(line, metadata))
				{
					m_zoneMetadata.push_back(std::move(metadata));
				}
				else
				{
					LOG_WARN(Core, "[QuestUiPresenter] Zone metadata line ignored: {}", std::string(line));
				}
			}

			if (lineEnd == std::string::npos)
			{
				break;
			}
			lineStart = lineEnd + 1;
		}

		if (m_zoneMetadata.empty())
		{
			LOG_ERROR(Core, "[QuestUiPresenter] Zone metadata load FAILED: no valid entries ({})", m_relativeZoneMetadataPath);
			return false;
		}

		LOG_INFO(Core, "[QuestUiPresenter] Zone metadata loaded (entries={}, path={})", m_zoneMetadata.size(), m_relativeZoneMetadataPath);
		return true;
	}

	bool QuestUiPresenter::ParseZoneMetadataLine(std::string_view line, MinimapZoneMetadata& outMetadata) const
	{
		size_t columnCount = 0;
		const std::array<std::string_view, 3> columns = SplitColumns(line, columnCount);
		if (columnCount != 3)
		{
			return false;
		}

		const std::string_view zoneIdView = Trim(columns[0]);
		if (zoneIdView.empty())
		{
			return false;
		}

		uint32_t zoneId = 0;
		for (const char character : zoneIdView)
		{
			if (character < '0' || character > '9')
			{
				return false;
			}
			zoneId = (zoneId * 10u) + static_cast<uint32_t>(character - '0');
		}

		const std::string_view sizeView = Trim(columns[2]);
		if (sizeView.empty())
		{
			return false;
		}

		float zoneSize = 0.0f;
		for (const char character : sizeView)
		{
			if (character < '0' || character > '9')
			{
				return false;
			}
			zoneSize = (zoneSize * 10.0f) + static_cast<float>(character - '0');
		}

		outMetadata.zoneId = zoneId;
		outMetadata.texturePath = std::string(Trim(columns[1]));
		outMetadata.zoneSizeMeters = zoneSize;
		return !outMetadata.texturePath.empty() && outMetadata.zoneSizeMeters > 0.0f;
	}

	void QuestUiPresenter::RebuildLayout()
	{
		const float viewportWidth = static_cast<float>(m_viewportWidth == 0 ? 1280u : m_viewportWidth);
		const float viewportHeight = static_cast<float>(m_viewportHeight == 0 ? 720u : m_viewportHeight);
		const float margin = std::max(20.0f, viewportWidth * 0.025f);

		const float journalWidth = std::clamp(viewportWidth * 0.28f, 280.0f, 420.0f);
		const float journalHeight = std::clamp(viewportHeight * 0.42f, 260.0f, 360.0f);
		m_state.journalPanelBounds = { margin, margin, journalWidth, journalHeight };

		const float trackerWidth = std::clamp(viewportWidth * 0.24f, 240.0f, 360.0f);
		const float trackerHeight = std::clamp(viewportHeight * 0.20f, 120.0f, 200.0f);
		m_state.trackerBounds = { viewportWidth - margin - trackerWidth, margin, trackerWidth, trackerHeight };

		const float minimapSize = std::clamp(viewportWidth * 0.18f, 180.0f, 260.0f);
		m_state.minimapBounds = { viewportWidth - margin - minimapSize, viewportHeight - margin - minimapSize, minimapSize, minimapSize };
		m_state.layoutValid = true;
	}

	void QuestUiPresenter::RebuildJournal(const UIModel& model)
	{
		m_state.journalEntries.clear();
		m_state.selectedQuestSteps.clear();
		m_state.selectedQuestId.clear();
		m_state.selectedQuestStatus = 0;

		if (m_selectedQuestId.empty() && !model.quests.empty())
		{
			m_selectedQuestId = model.quests.front().questId;
		}

		bool selectedQuestFound = false;
		for (const UIQuestEntry& quest : model.quests)
		{
			QuestJournalEntryView entry{};
			entry.questId = quest.questId;
			entry.status = quest.status;
			entry.totalSteps = static_cast<uint32_t>(quest.steps.size());
			for (const UIQuestStep& step : quest.steps)
			{
				if (step.requiredCount > 0 && step.currentCount >= step.requiredCount)
				{
					++entry.completedSteps;
				}
			}
			entry.selected = (quest.questId == m_selectedQuestId);
			m_state.journalEntries.push_back(entry);

			if (!entry.selected)
			{
				continue;
			}

			selectedQuestFound = true;
			m_state.selectedQuestId = quest.questId;
			m_state.selectedQuestStatus = quest.status;
			for (const UIQuestStep& step : quest.steps)
			{
				QuestStepView view{};
				view.label = BuildQuestStepLabel(step);
				view.currentCount = step.currentCount;
				view.requiredCount = step.requiredCount;
				view.completed = (step.requiredCount > 0 && step.currentCount >= step.requiredCount);
				m_state.selectedQuestSteps.push_back(std::move(view));
			}
		}

		if (!selectedQuestFound)
		{
			m_selectedQuestId.clear();
		}
	}

	void QuestUiPresenter::RebuildTracker(const UIModel& model)
	{
		m_state.trackerSteps.clear();
		for (const UIQuestEntry& quest : model.quests)
		{
			if (quest.status != 1)
			{
				continue;
			}

			for (const UIQuestStep& step : quest.steps)
			{
				QuestStepView view{};
				view.label = quest.questId + ": " + BuildQuestStepLabel(step);
				view.currentCount = step.currentCount;
				view.requiredCount = step.requiredCount;
				view.completed = (step.requiredCount > 0 && step.currentCount >= step.requiredCount);
				m_state.trackerSteps.push_back(std::move(view));
			}

			if (m_state.trackerSteps.size() >= 4)
			{
				break;
			}
		}
	}

	void QuestUiPresenter::RebuildMinimap(const UIModel& model)
	{
		m_state.minimapZoneId = model.playerStats.zoneId;
		m_state.questPois.clear();
		m_state.playerMarker = {};
		m_state.targetMarker = {};

		const MinimapZoneMetadata* zoneMetadata = FindZoneMetadata(model.playerStats.zoneId);
		if (!zoneMetadata)
		{
			m_state.minimapTexturePath.clear();
			LOG_WARN(Core, "[QuestUiPresenter] Minimap zone missing metadata (zone_id={})", model.playerStats.zoneId);
			return;
		}

		m_state.minimapTexturePath = zoneMetadata->texturePath;

		float u = 0.0f;
		float v = 0.0f;
		if (TryConvertWorldToUv(model.playerStats.zoneId, model.playerStats.positionX, model.playerStats.positionZ, u, v))
		{
			m_state.playerMarker.u = u;
			m_state.playerMarker.v = v;
			m_state.playerMarker.label = "Player";
			m_state.playerMarker.visible = true;
		}

		if (model.targetStats.hasTarget && model.targetStats.hasPosition
			&& TryConvertWorldToUv(model.playerStats.zoneId, model.targetStats.positionX, model.targetStats.positionZ, u, v))
		{
			m_state.targetMarker.u = u;
			m_state.targetMarker.v = v;
			m_state.targetMarker.label = "Target";
			m_state.targetMarker.visible = true;
		}

		for (const UIQuestEntry& quest : model.quests)
		{
			for (const UIQuestStep& step : quest.steps)
			{
				if (!step.targetId.starts_with("zone:"))
				{
					continue;
				}

				MinimapPoiView poi{};
				poi.u = 0.5f;
				poi.v = 0.5f;
				poi.label = quest.questId + ": " + step.targetId;
				poi.visible = true;
				m_state.questPois.push_back(std::move(poi));
			}
		}
	}

	const MinimapZoneMetadata* QuestUiPresenter::FindZoneMetadata(uint32_t zoneId) const
	{
		for (const MinimapZoneMetadata& metadata : m_zoneMetadata)
		{
			if (metadata.zoneId == zoneId)
			{
				return &metadata;
			}
		}

		return nullptr;
	}

	bool QuestUiPresenter::TryConvertWorldToUv(uint32_t zoneId, float positionX, float positionZ, float& outU, float& outV) const
	{
		const MinimapZoneMetadata* metadata = FindZoneMetadata(zoneId);
		if (!metadata || metadata->zoneSizeMeters <= 0.0f)
		{
			return false;
		}

		const float invSize = 1.0f / metadata->zoneSizeMeters;
		outU = std::clamp(positionX * invSize, 0.0f, 1.0f);
		outV = std::clamp(positionZ * invSize, 0.0f, 1.0f);
		return true;
	}

	std::string QuestUiPresenter::BuildQuestStepLabel(const UIQuestStep& step) const
	{
		switch (step.stepType)
		{
		case 1:
			return "Kill " + step.targetId;
		case 2:
			return "Collect " + step.targetId;
		case 3:
			return "Talk " + step.targetId;
		case 4:
			return "Enter " + step.targetId;
		default:
			return "Step " + step.targetId;
		}
	}

	void QuestUiPresenter::RebuildDebugText()
	{
		m_state.debugText.clear();
		m_state.debugText += "[QuestUi]\n";
		m_state.debugText += "quests=";
		m_state.debugText += std::to_string(m_state.journalEntries.size());
		m_state.debugText += " selected=";
		m_state.debugText += m_state.selectedQuestId;
		m_state.debugText += "\n";
		m_state.debugText += "tracker=";
		m_state.debugText += std::to_string(m_state.trackerSteps.size());
		m_state.debugText += " zone=";
		m_state.debugText += std::to_string(m_state.minimapZoneId);
		m_state.debugText += " minimap=";
		m_state.debugText += m_state.minimapTexturePath;
		m_state.debugText += "\n";
		m_state.debugText += "player_uv=(";
		m_state.debugText += std::to_string(m_state.playerMarker.u);
		m_state.debugText += ", ";
		m_state.debugText += std::to_string(m_state.playerMarker.v);
		m_state.debugText += ")\n";
	}
}
