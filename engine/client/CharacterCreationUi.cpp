#include "engine/client/CharacterCreationUi.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <cctype>

namespace engine::client
{
	namespace
	{
		// ── Config array helpers (same pattern as FXManager) ─────────────────

		bool HasIndexedKey(const engine::core::Config& cfg,
		                   std::string_view baseKey, size_t index,
		                   std::string_view field)
		{
			const std::string key =
			    std::string(baseKey) + "[" + std::to_string(index) + "]." + std::string(field);
			return cfg.Has(key);
		}

		std::string GetIndexedString(const engine::core::Config& cfg,
		                             std::string_view baseKey, size_t index,
		                             std::string_view field,
		                             std::string_view fallback = "")
		{
			const std::string key =
			    std::string(baseKey) + "[" + std::to_string(index) + "]." + std::string(field);
			return cfg.GetString(key, fallback);
		}

		int64_t GetIndexedInt(const engine::core::Config& cfg,
		                      std::string_view baseKey, size_t index,
		                      std::string_view field, int64_t fallback = 0)
		{
			const std::string key =
			    std::string(baseKey) + "[" + std::to_string(index) + "]." + std::string(field);
			return cfg.GetInt(key, fallback);
		}

		// ── Minimal profanity blacklist ────────────────────────────────────────
		// A small set of globally-refused substrings (case-insensitive).
		bool ContainsProfanity(std::string_view name)
		{
			static constexpr const char* kBlacklist[] = {
			    "admin", "gm", "null", "undefined",
			};
			std::string lower;
			lower.reserve(name.size());
			for (const char c : name)
				lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

			for (const char* word : kBlacklist)
			{
				if (lower.find(word) != std::string::npos)
					return true;
			}
			return false;
		}
	} // anonymous namespace

	// ── CharacterCreationUiPresenter ──────────────────────────────────────────

	CharacterCreationUiPresenter::~CharacterCreationUiPresenter()
	{
		Shutdown();
	}

	bool CharacterCreationUiPresenter::Init(const engine::core::Config& config,
	                                        std::string_view racesRelativePath,
	                                        std::string_view classesRelativePath)
	{
		m_state = {};

		if (!LoadRaces(config, racesRelativePath))
		{
			LOG_ERROR(Core, "[CharacterCreationUi] Init FAILED: races load failed ({})",
			    racesRelativePath);
			return false;
		}
		if (!LoadClasses(config, classesRelativePath))
		{
			LOG_ERROR(Core, "[CharacterCreationUi] Init FAILED: classes load failed ({})",
			    classesRelativePath);
			return false;
		}

		// Default selection: first race and first class.
		if (!m_state.races.empty())
			m_state.selectedRaceId = m_state.races.front().id;
		if (!m_state.classes.empty())
			m_state.selectedClassId = m_state.classes.front().id;

		m_state.step         = CharCreationStep::RaceSelection;
		m_state.layoutValid  = true;
		m_initialized        = true;

		RebuildDebugText();
		LOG_INFO(Core, "[CharacterCreationUi] Init OK (races={}, classes={})",
		    m_state.races.size(), m_state.classes.size());
		return true;
	}

	void CharacterCreationUiPresenter::Shutdown()
	{
		if (!m_initialized) return;
		m_state       = {};
		m_initialized = false;
		LOG_INFO(Core, "[CharacterCreationUi] Shutdown");
	}

	void CharacterCreationUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		m_viewportWidth  = width;
		m_viewportHeight = height;
		LOG_DEBUG(Core, "[CharacterCreationUi] SetViewportSize ({}x{})", width, height);
	}

	void CharacterCreationUiPresenter::SetStep(CharCreationStep step)
	{
		m_state.step = step;
		RebuildDebugText();
		LOG_DEBUG(Core, "[CharacterCreationUi] SetStep step={}", static_cast<int>(step));
	}

	bool CharacterCreationUiPresenter::SelectRace(uint32_t raceId)
	{
		const auto it = std::find_if(m_state.races.begin(), m_state.races.end(),
		    [raceId](const CharRaceDefinition& r) { return r.id == raceId; });
		if (it == m_state.races.end())
		{
			LOG_WARN(Core, "[CharacterCreationUi] SelectRace: unknown id {}", raceId);
			return false;
		}
		m_state.selectedRaceId = raceId;
		RebuildDebugText();
		LOG_INFO(Core, "[CharacterCreationUi] Race selected: {} ({})", it->displayName, raceId);
		return true;
	}

	bool CharacterCreationUiPresenter::SelectClass(uint32_t classId)
	{
		const auto it = std::find_if(m_state.classes.begin(), m_state.classes.end(),
		    [classId](const CharClassDefinition& c) { return c.id == classId; });
		if (it == m_state.classes.end())
		{
			LOG_WARN(Core, "[CharacterCreationUi] SelectClass: unknown id {}", classId);
			return false;
		}
		m_state.selectedClassId = classId;
		RebuildDebugText();
		LOG_INFO(Core, "[CharacterCreationUi] Class selected: {} ({})", it->displayName, classId);
		return true;
	}

	void CharacterCreationUiPresenter::SetCustomization(const CharCustomizationParams& params)
	{
		m_state.customization = params;
		RebuildDebugText();
	}

	void CharacterCreationUiPresenter::SetFaceType(uint8_t v)
	{
		m_state.customization.faceType = v;
		RebuildDebugText();
	}

	void CharacterCreationUiPresenter::SetHairStyle(uint8_t v)
	{
		m_state.customization.hairStyle = v;
		RebuildDebugText();
	}

	void CharacterCreationUiPresenter::SetSkinColor(uint8_t v)
	{
		m_state.customization.skinColorIndex = v;
		RebuildDebugText();
	}

	void CharacterCreationUiPresenter::SetHairColor(uint8_t v)
	{
		m_state.customization.hairColorIndex = v;
		RebuildDebugText();
	}

	void CharacterCreationUiPresenter::SetEyeColor(uint8_t v)
	{
		m_state.customization.eyeColorIndex = v;
		RebuildDebugText();
	}

	bool CharacterCreationUiPresenter::SetName(std::string_view name)
	{
		m_state.characterName       = std::string(name);
		m_state.nameValidationError = ValidateName(name);
		m_state.nameIsValid         = m_state.nameValidationError.empty();
		RebuildDebugText();
		if (!m_state.nameIsValid)
		{
			LOG_DEBUG(Core, "[CharacterCreationUi] Name validation failed: '{}'",
			    m_state.nameValidationError);
		}
		return m_state.nameIsValid;
	}

	void CharacterCreationUiPresenter::ApplyPreviewRotation(float deltaDeg)
	{
		m_state.previewRotationDeg =
		    std::fmod(m_state.previewRotationDeg + deltaDeg + 360.0f, 360.0f);
	}

	std::optional<engine::network::CharacterCreateRequestPayload>
	CharacterCreationUiPresenter::BuildCreateRequest() const
	{
		if (!m_state.nameIsValid)
		{
			LOG_WARN(Core, "[CharacterCreationUi] BuildCreateRequest: name not valid");
			return std::nullopt;
		}
		if (m_state.selectedRaceId == UINT32_MAX)
		{
			LOG_WARN(Core, "[CharacterCreationUi] BuildCreateRequest: no race selected");
			return std::nullopt;
		}
		if (m_state.selectedClassId == UINT32_MAX)
		{
			LOG_WARN(Core, "[CharacterCreationUi] BuildCreateRequest: no class selected");
			return std::nullopt;
		}

		engine::network::CharacterCreateRequestPayload payload{};
		payload.name           = m_state.characterName;
		payload.raceId         = m_state.selectedRaceId;
		payload.classId        = m_state.selectedClassId;
		payload.faceType       = m_state.customization.faceType;
		payload.hairStyle      = m_state.customization.hairStyle;
		payload.skinColorIndex = m_state.customization.skinColorIndex;
		payload.hairColorIndex = m_state.customization.hairColorIndex;
		payload.eyeColorIndex  = m_state.customization.eyeColorIndex;

		LOG_INFO(Core,
		    "[CharacterCreationUi] BuildCreateRequest OK (name='{}', race={}, class={})",
		    payload.name, payload.raceId, payload.classId);
		return payload;
	}

	// ── Private helpers ───────────────────────────────────────────────────────

	bool CharacterCreationUiPresenter::LoadRaces(const engine::core::Config& config,
	                                             std::string_view relativePath)
	{
		const auto fullPath = engine::platform::FileSystem::ResolveContentPath(config, relativePath);
		if (!engine::platform::FileSystem::Exists(fullPath))
		{
			LOG_ERROR(Core, "[CharacterCreationUi] Races file missing: {}", relativePath);
			return false;
		}

		engine::core::Config raceCfg;
		if (!raceCfg.LoadFromFile(fullPath.string()))
		{
			LOG_ERROR(Core, "[CharacterCreationUi] Races JSON parse failed: {}", relativePath);
			return false;
		}

		m_state.races.clear();
		for (size_t i = 0; HasIndexedKey(raceCfg, "races", i, "id"); ++i)
		{
			CharRaceDefinition r{};
			r.id          = static_cast<uint32_t>(GetIndexedInt(raceCfg, "races", i, "id"));
			r.key         = GetIndexedString(raceCfg, "races", i, "key");
			r.displayName = GetIndexedString(raceCfg, "races", i, "displayName");
			r.description = GetIndexedString(raceCfg, "races", i, "description");
			r.iconPath    = GetIndexedString(raceCfg, "races", i, "iconPath");
			r.racialBonus = GetIndexedString(raceCfg, "races", i, "racialBonus");

			if (r.id == 0 || r.key.empty())
			{
				LOG_WARN(Core, "[CharacterCreationUi] Races[{}] skipped: missing id/key", i);
				continue;
			}
			m_state.races.push_back(std::move(r));
		}

		if (m_state.races.empty())
		{
			LOG_ERROR(Core, "[CharacterCreationUi] Races load FAILED: no valid entries in {}",
			    relativePath);
			return false;
		}

		LOG_INFO(Core, "[CharacterCreationUi] Races loaded: {} races from {}",
		    m_state.races.size(), relativePath);
		return true;
	}

	bool CharacterCreationUiPresenter::LoadClasses(const engine::core::Config& config,
	                                               std::string_view relativePath)
	{
		const auto fullPath = engine::platform::FileSystem::ResolveContentPath(config, relativePath);
		if (!engine::platform::FileSystem::Exists(fullPath))
		{
			LOG_ERROR(Core, "[CharacterCreationUi] Classes file missing: {}", relativePath);
			return false;
		}

		engine::core::Config classCfg;
		if (!classCfg.LoadFromFile(fullPath.string()))
		{
			LOG_ERROR(Core, "[CharacterCreationUi] Classes JSON parse failed: {}", relativePath);
			return false;
		}

		m_state.classes.clear();
		for (size_t i = 0; HasIndexedKey(classCfg, "classes", i, "id"); ++i)
		{
			CharClassDefinition c{};
			c.id              = static_cast<uint32_t>(GetIndexedInt(classCfg, "classes", i, "id"));
			c.key             = GetIndexedString(classCfg, "classes", i, "key");
			c.displayName     = GetIndexedString(classCfg, "classes", i, "displayName");
			c.description     = GetIndexedString(classCfg, "classes", i, "description");
			c.iconPath        = GetIndexedString(classCfg, "classes", i, "iconPath");
			c.roleDescription = GetIndexedString(classCfg, "classes", i, "roleDescription");

			if (c.id == 0 || c.key.empty())
			{
				LOG_WARN(Core, "[CharacterCreationUi] Classes[{}] skipped: missing id/key", i);
				continue;
			}
			m_state.classes.push_back(std::move(c));
		}

		if (m_state.classes.empty())
		{
			LOG_ERROR(Core, "[CharacterCreationUi] Classes load FAILED: no valid entries in {}",
			    relativePath);
			return false;
		}

		LOG_INFO(Core, "[CharacterCreationUi] Classes loaded: {} classes from {}",
		    m_state.classes.size(), relativePath);
		return true;
	}

	std::string CharacterCreationUiPresenter::ValidateName(std::string_view name)
	{
		if (name.size() < kCharNameMinLength)
			return "Name must be at least " + std::to_string(kCharNameMinLength) + " characters.";
		if (name.size() > kCharNameMaxLength)
			return "Name may not exceed " + std::to_string(kCharNameMaxLength) + " characters.";

		for (const char c : name)
		{
			if (!std::isalpha(static_cast<unsigned char>(c)) &&
			    !std::isdigit(static_cast<unsigned char>(c)))
			{
				return "Name may only contain letters and digits.";
			}
		}

		if (ContainsProfanity(name))
			return "Name contains a reserved or disallowed word.";

		return {};
	}

	void CharacterCreationUiPresenter::RebuildDebugText()
	{
		std::string raceName, className;
		for (const auto& r : m_state.races)
			if (r.id == m_state.selectedRaceId) { raceName = r.displayName; break; }
		for (const auto& c : m_state.classes)
			if (c.id == m_state.selectedClassId) { className = c.displayName; break; }

		m_state.debugText =
		    "Step=" + std::to_string(static_cast<int>(m_state.step)) +
		    " Race=" + raceName +
		    " Class=" + className +
		    " Name=" + m_state.characterName +
		    (m_state.nameIsValid ? " [OK]" : " [INVALID]") +
		    " Rotation=" + std::to_string(static_cast<int>(m_state.previewRotationDeg)) + "deg";
	}

} // namespace engine::client
