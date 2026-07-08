#include "src/client/quest/QuestUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::client
{
	void WorldToRadarUv(float px, float pz, float playerX, float playerZ, float radiusM,
		float& outU, float& outV, bool& outOffRadar)
	{
		if (radiusM <= 0.0f)
		{
			outU = 0.5f;
			outV = 0.5f;
			outOffRadar = true;
			return;
		}

		const float dx = px - playerX;
		const float dz = pz - playerZ;
		outU = 0.5f + dx / (2.0f * radiusM);
		outV = 0.5f + dz / (2.0f * radiusM);

		const float dist = std::sqrt(dx * dx + dz * dz);
		outOffRadar = dist > radiusM;

		if (outOffRadar)
		{
			outU = std::clamp(outU, 0.0f, 1.0f);
			outV = std::clamp(outV, 0.0f, 1.0f);
		}
	}

	bool ShouldShowQuestInJournal(uint8_t status)
	{
		// Journal = uniquement les quêtes acceptées et en cours : Active (2) et
		// ReadyToTurnIn (3). Offered (1)/Locked (0)/Completed (4) exclus (cf. doc h).
		return status == 2u || status == 3u;
	}

	int ClampZoomIndex(int index)
	{
		if (index < 0)
			return 0;
		if (index >= kMinimapZoomLevelCount)
			return kMinimapZoomLevelCount - 1;
		return index;
	}

	int StepZoomIndex(int index, int wheelDelta)
	{
		// Molette vers le haut (wheelDelta > 0) = zoom IN = rayon plus petit =
		// index décroît (même convention que le zoom caméra).
		return ClampZoomIndex(index - wheelDelta);
	}

	float RadiusForZoomIndex(int index)
	{
		return kMinimapZoomLevelsM[ClampZoomIndex(index)];
	}

	RadarScreenRect ComputeRadarScreenRect(const engine::core::Config& cfg,
		float displayW, float displayH)
	{
		(void)displayH;
		RadarScreenRect rect{};
		rect.enabled = cfg.GetBool("client.quest.minimap.enabled", true);
		const float sizePx = static_cast<float>(cfg.GetInt("client.quest.minimap.size_px", 200));
		if (!rect.enabled || sizePx <= 0.0f)
		{
			rect.enabled = false;
			return rect;
		}
		// Ancrage IDENTIQUE à QuestImGuiRenderer::RenderMinimap : coin haut-droit,
		// sous le HUD météo + la boussole (marge 16 + dégagement haut 116 + 8 px).
		// Ces constantes DOIVENT rester synchronisées avec RenderMinimap (qui utilise
		// désormais ce même helper) sous peine de dérive géométrie rendu/hit-test.
		constexpr float kMargin = 16.0f;
		constexpr float kTopHudClearancePx = 116.0f;
		rect.size = sizePx;
		rect.x0 = displayW - sizePx - kMargin;
		rect.y0 = kMargin + kTopHudClearancePx + 8.0f;
		return rect;
	}

	ScreenPoint RadarZoomTickPos(const RadarScreenRect& rect, int tickIndex)
	{
		const int k = ClampZoomIndex(tickIndex);
		const float cx = rect.x0 + rect.size * 0.5f;
		const float cy = rect.y0 + rect.size * 0.5f;
		const float rArc = rect.size * 0.5f + 6.0f; // juste à l'extérieur de la bordure
		constexpr float kDegToRad = 3.14159265f / 180.0f;
		// 150° (haut-gauche) -> 30° (haut-droite), 30° entre chaque repère. Repère
		// écran : y vers le bas, donc +sin(theta) monte (d'où le -sin sur y).
		const float theta = (150.0f - 30.0f * static_cast<float>(k)) * kDegToRad;
		ScreenPoint p;
		p.x = cx + rArc * std::cos(theta);
		p.y = cy - rArc * std::sin(theta);
		return p;
	}

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

		// Si la sélection courante n'est plus une quête acceptée (rendue, abandonnée,
		// ou jamais acceptée), on la réassigne à la première quête acceptée dispo.
		bool selectionStillShown = false;
		for (const UIQuestEntry& quest : model.quests)
		{
			if (ShouldShowQuestInJournal(quest.status) && quest.questId == m_selectedQuestId)
			{
				selectionStillShown = true;
				break;
			}
		}
		if (!selectionStillShown)
		{
			m_selectedQuestId.clear();
			for (const UIQuestEntry& quest : model.quests)
			{
				if (ShouldShowQuestInJournal(quest.status))
				{
					m_selectedQuestId = quest.questId;
					break;
				}
			}
		}

		bool selectedQuestFound = false;
		for (const UIQuestEntry& quest : model.quests)
		{
			// Le journal ne liste QUE les quêtes acceptées (Active/ReadyToTurnIn) ;
			// les quêtes seulement proposées par un PNJ (Offered) ou verrouillées
			// n'y apparaissent pas — elles ne sont visibles que chez le PNJ.
			if (!ShouldShowQuestInJournal(quest.status))
			{
				continue;
			}
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
			if (quest.status != 2)
			{
				// 2 == QuestStatus::Active (status wire système B copié par
				// ApplyQuestDelta : Offered=1, Active=2, cf. QuestRuntime.h).
				// Correctif SP3 : l'ancien `!= 1` ne gardait que les Offered.
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
		m_state.minimapTexturePath = zoneMetadata ? zoneMetadata->texturePath : std::string();
		if (!zoneMetadata)
		{
			LOG_WARN(Core, "[QuestUiPresenter] Minimap zone missing metadata (zone_id={})", model.playerStats.zoneId);
			// Pas de texture de fond connue, mais le radar SP3 est centré sur
			// le joueur (indépendant de la texture de zone) : on continue.
		}

		const float playerX = model.playerStats.positionX;
		const float playerZ = model.playerStats.positionZ;

		// SP3 — radar centré joueur : le joueur est toujours au centre exact.
		m_state.playerMarker.u = 0.5f;
		m_state.playerMarker.v = 0.5f;
		m_state.playerMarker.label = "Player";
		m_state.playerMarker.visible = true;

		if (model.targetStats.hasTarget && model.targetStats.hasPosition)
		{
			float u = 0.0f;
			float v = 0.0f;
			bool offRadar = false;
			WorldToRadarUv(model.targetStats.positionX, model.targetStats.positionZ, playerX, playerZ,
				m_minimapRadiusM, u, v, offRadar);
			m_state.targetMarker.u = u;
			m_state.targetMarker.v = v;
			m_state.targetMarker.label = "Target";
			m_state.targetMarker.visible = true;
		}

		// SP3 — marqueurs de POI de quête : positions résolues via la table
		// de POI injectée (\ref m_poiTable), une par targetId d'étape active
		// et non terminée. Remplace l'ancien placeholder `zone:` central.
		for (const UIQuestEntry& quest : model.quests)
		{
			if (quest.status != 2)
			{
				// 2 == QuestStatus::Active. UIQuestEntry.status est le status
				// wire système B copié par UIModelBinding::ApplyQuestDelta
				// (Locked=0, Offered=1, Active=2, ReadyToTurnIn=3, Completed=4 ;
				// cf. QuestRuntime.h). Seules les quêtes ACCEPTÉES et EN COURS
				// affichent des marqueurs de POI (pas Offered/Locked/Completed).
				continue;
			}

			for (const UIQuestStep& step : quest.steps)
			{
				if (step.currentCount >= step.requiredCount)
				{
					// Étape déjà terminée : rien à pointer sur le radar.
					continue;
				}

				// Etape « tuer mob:X » : pointer les mobs VIVANTS correspondants (bien plus
				// utile que des points de spawn statiques -- retour joueur 2026-07-04 :
				// « pas d'info radar pendant la quete »). Les mobs bougent/meurent, on lit
				// donc les entites repliquees en direct (model.remoteEntities).
				if (step.stepType == 1 /*Kill*/ && step.targetId.rfind("mob:", 0) == 0)
				{
					uint32_t archetypeId = 0;
					bool parsed = step.targetId.size() > 4;
					for (size_t ci = 4; ci < step.targetId.size(); ++ci)
					{
						const char c = step.targetId[ci];
						if (c < '0' || c > '9') { parsed = false; break; }
						archetypeId = archetypeId * 10u + static_cast<uint32_t>(c - '0');
					}
					if (parsed)
					{
						const std::string mobLabel = BuildQuestStepLabel(step);
						for (const UIRemoteEntity& re : model.remoteEntities)
						{
							if (re.archetypeId != archetypeId) continue; // mauvaise creature
							if (re.playerClientId != 0u) continue;        // pas un joueur
							if ((re.stateFlags & 1u) != 0u) continue;     // pas un mob mort
							float mu = 0.0f, mv = 0.0f; bool moff = false;
							WorldToRadarUv(re.positionX, re.positionZ, playerX, playerZ, m_minimapRadiusM, mu, mv, moff);
							MinimapPoiView mpoi{};
							mpoi.u = mu;
							mpoi.v = mv;
							mpoi.label = mobLabel;
							mpoi.visible = true;
							mpoi.stepType = step.stepType;
							m_state.questPois.push_back(std::move(mpoi));
						}
						continue; // etape traitee par les mobs vivants (pas de POI statique)
					}
				}

				const std::vector<QuestPoiPosition>* positions =
					m_poiTable ? m_poiTable->Positions(step.targetId) : nullptr;
				if (!positions)
				{
					continue;
				}

				const std::string label = BuildQuestStepLabel(step);
				for (const QuestPoiPosition& pos : *positions)
				{
					float u = 0.0f;
					float v = 0.0f;
					bool offRadar = false;
					WorldToRadarUv(pos.x, pos.z, playerX, playerZ, m_minimapRadiusM, u, v, offRadar);

					MinimapPoiView poi{};
					poi.u = u;
					poi.v = v;
					poi.label = label;
					poi.visible = true;
					// SP3 Task 3 — miroir de QuestStepType, consomme par
					// QuestImGuiRenderer::RenderMinimap pour teinter le marqueur.
					poi.stepType = step.stepType;
					m_state.questPois.push_back(std::move(poi));
				}
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
