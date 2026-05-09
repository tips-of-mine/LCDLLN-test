#pragma once

#include "engine/client/UIModel.h"
#include "engine/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::client
{
	/// Pixel-space rectangle used by the quest UI presenter layout.
	struct QuestUiRect
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	/// One quest list entry ready for a journal panel.
	struct QuestJournalEntryView
	{
		std::string questId;
		uint8_t status = 0;
		uint32_t completedSteps = 0;
		uint32_t totalSteps = 0;
		bool selected = false;
	};

	/// One quest step line ready for detail and tracker panels.
	struct QuestStepView
	{
		std::string label;
		uint32_t currentCount = 0;
		uint32_t requiredCount = 0;
		bool completed = false;
	};

	/// One minimap POI marker in normalized UV coordinates.
	struct MinimapPoiView
	{
		float u = 0.0f;
		float v = 0.0f;
		std::string label;
		bool visible = false;
	};

	/// Zone metadata loaded from content for minimap rendering.
	struct MinimapZoneMetadata
	{
		uint32_t zoneId = 0;
		std::string texturePath;
		float zoneSizeMeters = 0.0f;
	};

	/// Fully resolved quest journal + tracker + minimap state.
	struct QuestUiState
	{
		QuestUiRect journalPanelBounds{};
		QuestUiRect trackerBounds{};
		QuestUiRect minimapBounds{};
		std::vector<QuestJournalEntryView> journalEntries;
		std::string selectedQuestId;
		uint8_t selectedQuestStatus = 0;
		std::vector<QuestStepView> selectedQuestSteps;
		std::vector<QuestStepView> trackerSteps;
		std::string minimapTexturePath;
		uint32_t minimapZoneId = 0;
		MinimapPoiView playerMarker{};
		MinimapPoiView targetMarker{};
		std::vector<MinimapPoiView> questPois;
		std::string debugText;
		bool layoutValid = false;
	};

	/// Builds quest journal, quest tracker and minimap widget state from the UI model.
	class QuestUiPresenter final
	{
	public:
		/// Construct an uninitialized quest presenter.
		QuestUiPresenter() = default;

		/// Release quest presenter resources.
		~QuestUiPresenter();

		/// Initialize the presenter and load minimap zone metadata from content.
		bool Init(const engine::core::Config& config);

		/// Shutdown the presenter and release cached state.
		void Shutdown();

		/// Update viewport-dependent layout for journal, tracker and minimap widgets.
		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Apply one UI model snapshot and rebuild quest journal, tracker and minimap state.
		bool ApplyModel(const UIModel& model, uint32_t changeMask);

		/// Select one quest to display in the journal detail panel.
		bool SelectQuest(std::string_view questId);

		/// Return the immutable resolved quest UI state.
		const QuestUiState& GetState() const { return m_state; }

	private:
		/// Load minimap zone metadata from the configured content-relative file.
		bool LoadZoneMetadata(const engine::core::Config& config);

		/// Parse one metadata line using the `zone|texture|size` format.
		bool ParseZoneMetadataLine(std::string_view line, MinimapZoneMetadata& outMetadata) const;

		/// Recompute widget rectangles after a viewport change.
		void RebuildLayout();

		/// Rebuild the journal list and keep the selected quest valid.
		void RebuildJournal(const UIModel& model);

		/// Rebuild the tracker HUD from active quests.
		void RebuildTracker(const UIModel& model);

		/// Rebuild minimap markers from player, target and tracked quest data.
		void RebuildMinimap(const UIModel& model);

		/// Return metadata for one zone id, or null when missing.
		const MinimapZoneMetadata* FindZoneMetadata(uint32_t zoneId) const;

		/// Convert one zone-local xz position to normalized minimap UV coordinates.
		bool TryConvertWorldToUv(uint32_t zoneId, float positionX, float positionZ, float& outU, float& outV) const;

		/// Build a human-readable label for one quest step.
		std::string BuildQuestStepLabel(const UIQuestStep& step) const;

		/// Rebuild the textual dump of the resolved quest UI state.
		void RebuildDebugText();

		QuestUiState m_state{};
		UIModel m_lastModel{};
		std::vector<MinimapZoneMetadata> m_zoneMetadata;
		std::string m_selectedQuestId;
		uint32_t m_viewportWidth = 0;
		uint32_t m_viewportHeight = 0;
		std::string m_relativeZoneMetadataPath;
		bool m_initialized = false;
	};
}
