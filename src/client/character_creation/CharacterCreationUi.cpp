#include "src/client/character_creation/CharacterCreationUi.h"

#include "src/shared/core/Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace engine::client
{
	// =========================================================================
	// Helpers
	// =========================================================================

	namespace
	{
		/// Parse a JSON array of strings at a given key prefix, e.g. "races[0].racials".
		/// Returns the vector; empty when the array has zero entries.
		std::vector<std::string> ReadStringArray(const engine::core::Config& cfg,
		                                         const std::string&          prefix)
		{
			std::vector<std::string> out;
			size_t i = 0;
			while (true)
			{
				const std::string key = prefix + "[" + std::to_string(i) + "]";
				if (!cfg.Has(key))
					break;
				out.push_back(cfg.GetString(key, ""));
				++i;
			}
			return out;
		}
	}

	// =========================================================================
	// Lifecycle
	// =========================================================================

	CharacterCreationPresenter::~CharacterCreationPresenter()
	{
		if (m_initialized)
			Shutdown();
	}

	bool CharacterCreationPresenter::Init(const engine::core::Config& config)
	{
		if (m_initialized)
			Shutdown();

		// Content-relative paths (overridable from config.json).
		const std::string racesRel   = config.GetString("char_creation.races_path",   "races/races.json");
		const std::string classesRel = config.GetString("char_creation.classes_path", "races/classes.json");

		if (!LoadRaces(config, racesRel))
		{
			LOG_WARN(Core, "[CharacterCreation] Init: could not load races from '{}' — using empty list", racesRel);
			// Non-fatal: the screen can still be shown without race data.
		}

		if (!LoadClasses(config, classesRel))
		{
			LOG_WARN(Core, "[CharacterCreation] Init: could not load classes from '{}' — using empty list", classesRel);
		}

		// Système de customisation (limites par race + presets de proportions),
		// chargé depuis <paths.content>/configuration/. Non-fatal s'il échoue :
		// l'écran reste utilisable, le panneau « Apparence physique » se masque.
		{
			const std::string contentRoot = config.GetString("paths.content", "game/data");
			if (!m_customization.Initialize(contentRoot + "/configuration"))
			{
				LOG_WARN(Core, "[CharacterCreation] Init: customization configs not loaded "
				               "(panneau proportions désactivé)");
			}
		}

		// Reset creation state to sane defaults.
		m_state = CharacterCreationState{};
		m_selectedClassFiltered = 0;
		m_initialized = true;

		LOG_INFO(Core, "[CharacterCreation] Init OK (races={} classes={})", m_races.size(), m_classes.size());
		return true;
	}

	void CharacterCreationPresenter::Shutdown()
	{
		m_races.clear();
		m_classes.clear();
		m_state = CharacterCreationState{};
		m_initialized = false;
		LOG_INFO(Core, "[CharacterCreation] Shutdown complete");
	}

	// =========================================================================
	// Step navigation
	// =========================================================================

	bool CharacterCreationPresenter::Next()
	{
		switch (m_state.step)
		{
		case CharacterCreationStep::RaceSelect:
			if (m_races.empty())
			{
				LOG_WARN(Core, "[CharacterCreation] Next: no races loaded, cannot advance");
				return false;
			}
			m_state.step = CharacterCreationStep::ClassSelect;
			m_state.selectedClassIndex = 0;
			m_selectedClassFiltered    = 0;
			LOG_INFO(Core, "[CharacterCreation] Step → ClassSelect (race={})",
			         m_races[m_state.selectedRaceIndex].id);
			return true;

		case CharacterCreationStep::ClassSelect:
		{
			const auto filtered = GetFilteredClasses();
			if (filtered.empty())
			{
				LOG_WARN(Core, "[CharacterCreation] Next: no class selected");
				return false;
			}
			m_state.step = CharacterCreationStep::Customization;
			LOG_INFO(Core, "[CharacterCreation] Step → Customization (class={})",
			         filtered[m_selectedClassFiltered]->id);
			return true;
		}

		case CharacterCreationStep::Customization:
			m_state.step = CharacterCreationStep::NameInput;
			LOG_INFO(Core, "[CharacterCreation] Step → NameInput");
			return true;

		case CharacterCreationStep::NameInput:
		{
			std::string err;
			if (!ValidateName(m_state.characterName, err))
			{
				m_state.nameValid        = false;
				m_state.nameErrorMessage = err;
				LOG_WARN(Core, "[CharacterCreation] Name '{}' rejected: {}", m_state.characterName, err);
				return false;
			}
			m_state.nameValid        = true;
			m_state.nameErrorMessage.clear();
			m_state.step = CharacterCreationStep::Confirming;
			LOG_INFO(Core, "[CharacterCreation] Step → Confirming (name='{}')", m_state.characterName);
			return true;
		}

		default:
			return false;
		}
	}

	void CharacterCreationPresenter::Back()
	{
		switch (m_state.step)
		{
		case CharacterCreationStep::ClassSelect:
			m_state.step = CharacterCreationStep::RaceSelect;
			break;
		case CharacterCreationStep::Customization:
			m_state.step = CharacterCreationStep::ClassSelect;
			break;
		case CharacterCreationStep::NameInput:
			m_state.step = CharacterCreationStep::Customization;
			break;
		default:
			break;
		}
		LOG_INFO(Core, "[CharacterCreation] Step ← Back → {}", static_cast<uint8_t>(m_state.step));
	}

	// =========================================================================
	// Race selection
	// =========================================================================

	bool CharacterCreationPresenter::SelectRace(uint32_t index)
	{
		if (index >= m_races.size())
		{
			LOG_WARN(Core, "[CharacterCreation] SelectRace: index {} out of range ({})", index, m_races.size());
			return false;
		}
		m_state.selectedRaceIndex = index;
		// Reset downstream selections.
		m_selectedClassFiltered    = 0;
		m_state.selectedClassIndex = 0;
		m_state.skinColorIdx       = 0;
		m_state.hairColorIdx       = 0;
		m_state.eyeColorIdx        = 0;
		LOG_INFO(Core, "[CharacterCreation] Race selected: {}", m_races[index].id);
		return true;
	}

	const RaceDefinition* CharacterCreationPresenter::GetSelectedRace() const
	{
		if (m_races.empty() || m_state.selectedRaceIndex >= m_races.size())
			return nullptr;
		return &m_races[m_state.selectedRaceIndex];
	}

	// =========================================================================
	// Class selection
	// =========================================================================

	bool CharacterCreationPresenter::SelectClass(uint32_t index)
	{
		const auto filtered = GetFilteredClasses();
		if (index >= filtered.size())
		{
			LOG_WARN(Core, "[CharacterCreation] SelectClass: index {} out of range ({})", index, filtered.size());
			return false;
		}
		m_selectedClassFiltered    = index;
		m_state.selectedClassIndex = index;
		LOG_INFO(Core, "[CharacterCreation] Class selected: {}", filtered[index]->id);
		return true;
	}

	std::vector<const ClassDefinition*> CharacterCreationPresenter::GetFilteredClasses() const
	{
		const RaceDefinition* race = GetSelectedRace();
		std::vector<const ClassDefinition*> out;
		for (const ClassDefinition& cls : m_classes)
		{
			// If allowedRaceIds is empty, class is available to all races.
			if (cls.allowedRaceIds.empty() || race == nullptr)
			{
				out.push_back(&cls);
				continue;
			}
			for (const std::string& rid : cls.allowedRaceIds)
			{
				if (rid == race->id)
				{
					out.push_back(&cls);
					break;
				}
			}
		}
		return out;
	}

	const ClassDefinition* CharacterCreationPresenter::GetSelectedClass() const
	{
		const auto filtered = GetFilteredClasses();
		if (filtered.empty() || m_selectedClassFiltered >= filtered.size())
			return nullptr;
		return filtered[m_selectedClassFiltered];
	}

	// =========================================================================
	// Customization
	// =========================================================================

	void CharacterCreationPresenter::SetFaceType(uint8_t v)
	{
		m_state.faceType = v < GetFaceTypeCount() ? v : 0u;
	}

	void CharacterCreationPresenter::SetHairStyle(uint8_t v)
	{
		m_state.hairStyle = v < GetHairStyleCount() ? v : 0u;
	}

	void CharacterCreationPresenter::SetSkinColorIdx(uint8_t v)
	{
		const uint8_t cnt = GetSkinColorCount();
		m_state.skinColorIdx = cnt > 0 ? (v < cnt ? v : 0u) : 0u;
	}

	void CharacterCreationPresenter::SetHairColorIdx(uint8_t v)
	{
		const uint8_t cnt = GetHairColorCount();
		m_state.hairColorIdx = cnt > 0 ? (v < cnt ? v : 0u) : 0u;
	}

	void CharacterCreationPresenter::SetEyeColorIdx(uint8_t v)
	{
		const uint8_t cnt = GetEyeColorCount();
		m_state.eyeColorIdx = cnt > 0 ? (v < cnt ? v : 0u) : 0u;
	}

	uint8_t CharacterCreationPresenter::GetSkinColorCount() const
	{
		const RaceDefinition* r = GetSelectedRace();
		return r ? static_cast<uint8_t>(std::min<size_t>(r->skinColorHex.size(), 255u)) : 1u;
	}

	uint8_t CharacterCreationPresenter::GetHairColorCount() const
	{
		const RaceDefinition* r = GetSelectedRace();
		return r ? static_cast<uint8_t>(std::min<size_t>(r->hairColorHex.size(), 255u)) : 1u;
	}

	uint8_t CharacterCreationPresenter::GetEyeColorCount() const
	{
		const RaceDefinition* r = GetSelectedRace();
		return r ? static_cast<uint8_t>(std::min<size_t>(r->eyeColorHex.size(), 255u)) : 1u;
	}

	// =========================================================================
	// 3-D preview rotation
	// =========================================================================

	void CharacterCreationPresenter::ApplyPreviewDrag(float deltaPixels, float sensitivity)
	{
		m_state.previewRotationDeg += deltaPixels * sensitivity;
		// Wrap into [0, 360).
		while (m_state.previewRotationDeg >= 360.0f) m_state.previewRotationDeg -= 360.0f;
		while (m_state.previewRotationDeg <    0.0f) m_state.previewRotationDeg += 360.0f;
	}

	// =========================================================================
	// Name input + validation
	// =========================================================================

	void CharacterCreationPresenter::SetCharacterName(const std::string& name)
	{
		m_state.characterName = name;
		std::string err;
		m_state.nameValid        = ValidateName(name, err);
		m_state.nameErrorMessage = m_state.nameValid ? std::string{} : err;
	}

	bool CharacterCreationPresenter::ValidateName(const std::string& name, std::string& outError) const
	{
		// Length check (spec: 3-12 characters).
		if (name.size() < 3u)
		{
			outError = "Le nom doit contenir au moins 3 caractères.";
			return false;
		}
		if (name.size() > 12u)
		{
			outError = "Le nom ne peut pas dépasser 12 caractères.";
			return false;
		}

		// Character set: alphanumeric + underscore (mirrors server CharacterCreateHandler logic).
		for (unsigned char c : name)
		{
			if (!std::isalnum(c) && c != '_')
			{
				outError = "Seuls les caractères alphanumériques et '_' sont autorisés.";
				return false;
			}
		}

		// Profanity filter (basic blacklist — server performs the authoritative check).
		std::string lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (IsProfane(lower))
		{
			outError = "Ce nom n'est pas autorisé.";
			return false;
		}

		return true;
	}

	/*static*/ bool CharacterCreationPresenter::IsProfane(const std::string& lower)
	{
		// Minimal English blacklist — authoritative profanity check is server-side.
		static const char* const kBlacklist[] = {
			"fuck", "shit", "ass", "bitch", "cunt", "dick", "cock",
			"nigger", "nigga", "faggot", "retard", "whore", "slut",
		};
		for (const char* word : kBlacklist)
		{
			if (lower.find(word) != std::string::npos)
				return true;
		}
		return false;
	}

	// =========================================================================
	// Payload builder
	// =========================================================================

	engine::network::CharacterCreateRequestPayload
	CharacterCreationPresenter::BuildRequestPayload() const
	{
		engine::network::CharacterCreateRequestPayload payload;
		payload.name = m_state.characterName;

		const RaceDefinition*  race = GetSelectedRace();
		const ClassDefinition* cls  = GetSelectedClass();
		payload.raceId  = race ? race->id  : "";
		payload.classId = cls  ? cls->id   : "";

		payload.customization.faceType     = m_state.faceType;
		payload.customization.hairStyle    = m_state.hairStyle;
		payload.customization.skinColorIdx = m_state.skinColorIdx;
		payload.customization.hairColorIdx = m_state.hairColorIdx;
		payload.customization.eyeColorIdx  = m_state.eyeColorIdx;

		return payload;
	}

	void CharacterCreationPresenter::MarkSuccess()
	{
		m_state.step = CharacterCreationStep::Done;
		LOG_INFO(Core, "[CharacterCreation] Character created successfully (name='{}')",
		         m_state.characterName);
	}

	void CharacterCreationPresenter::MarkError(const std::string& reason)
	{
		m_state.step         = CharacterCreationStep::Error;
		m_state.errorMessage = reason;
		LOG_WARN(Core, "[CharacterCreation] Character creation failed: {}", reason);
	}

	// =========================================================================
	// Debug text
	// =========================================================================

	std::string CharacterCreationPresenter::BuildDebugText() const
	{
		std::ostringstream ss;
		ss << "[CharacterCreation M39.1]\n";
		ss << "  step=" << static_cast<uint8_t>(m_state.step) << "\n";

		const RaceDefinition*  race = GetSelectedRace();
		const ClassDefinition* cls  = GetSelectedClass();
		ss << "  race="  << (race ? race->displayName  : "(none)") << "\n";
		ss << "  class=" << (cls  ? cls->displayName   : "(none)") << "\n";
		ss << "  skin="  << static_cast<uint32_t>(m_state.skinColorIdx)
		   << " hair="   << static_cast<uint32_t>(m_state.hairColorIdx)
		   << " eye="    << static_cast<uint32_t>(m_state.eyeColorIdx)  << "\n";
		ss << "  face="  << static_cast<uint32_t>(m_state.faceType)
		   << " hairstyle=" << static_cast<uint32_t>(m_state.hairStyle) << "\n";
		ss << "  rotation=" << m_state.previewRotationDeg << " deg\n";
		ss << "  name='" << m_state.characterName << "'"
		   << (m_state.nameValid ? " [OK]" : " [INVALID: " + m_state.nameErrorMessage + "]") << "\n";
		if (m_state.step == CharacterCreationStep::Error)
			ss << "  ERROR: " << m_state.errorMessage << "\n";
		return ss.str();
	}

	// =========================================================================
	// Data loading
	// =========================================================================

	bool CharacterCreationPresenter::LoadRaces(const engine::core::Config& config,
	                                           const std::string&          relPath)
	{
		m_races.clear();

		const std::string contentRoot = config.GetString("paths.content", "game/data");
		const std::string fullPath    = contentRoot + "/" + relPath;

		engine::core::Config raceCfg;
		if (!raceCfg.LoadFromFile(fullPath))
		{
			LOG_WARN(Core, "[CharacterCreation] LoadRaces FAILED: cannot open '{}'", fullPath);
			return false;
		}

		size_t i = 0;
		while (raceCfg.Has("races[" + std::to_string(i) + "].id"))
		{
			const std::string pfx = "races[" + std::to_string(i) + "]";

			RaceDefinition def;
			def.id          = raceCfg.GetString(pfx + ".id",          "");
			def.displayName = raceCfg.GetString(pfx + ".displayName", def.id);
			def.description = raceCfg.GetString(pfx + ".description", "");
			def.racials     = ReadStringArray(raceCfg, pfx + ".racials");
			def.skinColorHex = ReadStringArray(raceCfg, pfx + ".defaultSkinColors");
			def.hairColorHex = ReadStringArray(raceCfg, pfx + ".defaultHairColors");
			def.eyeColorHex  = ReadStringArray(raceCfg, pfx + ".defaultEyeColors");
			def.meshPath     = raceCfg.GetString(pfx + ".meshPath", "");

			if (!def.id.empty())
			{
				m_races.push_back(std::move(def));
				LOG_INFO(Core, "[CharacterCreation] Loaded race '{}' (racials={} skinColors={} meshPath='{}')",
				         m_races.back().id, m_races.back().racials.size(),
				         m_races.back().skinColorHex.size(), m_races.back().meshPath);
			}
			++i;
		}

		if (m_races.empty())
		{
			LOG_WARN(Core, "[CharacterCreation] LoadRaces: no valid entries in '{}'", fullPath);
			return false;
		}

		LOG_INFO(Core, "[CharacterCreation] Loaded {} race(s) from '{}'", m_races.size(), fullPath);
		return true;
	}

	bool CharacterCreationPresenter::LoadClasses(const engine::core::Config& config,
	                                             const std::string&          relPath)
	{
		m_classes.clear();

		const std::string contentRoot = config.GetString("paths.content", "game/data");
		const std::string fullPath    = contentRoot + "/" + relPath;

		engine::core::Config clsCfg;
		if (!clsCfg.LoadFromFile(fullPath))
		{
			LOG_WARN(Core, "[CharacterCreation] LoadClasses FAILED: cannot open '{}'", fullPath);
			return false;
		}

		size_t i = 0;
		while (clsCfg.Has("classes[" + std::to_string(i) + "].id"))
		{
			const std::string pfx = "classes[" + std::to_string(i) + "]";

			ClassDefinition def;
			def.id              = clsCfg.GetString(pfx + ".id",          "");
			def.displayName     = clsCfg.GetString(pfx + ".displayName", def.id);
			def.description     = clsCfg.GetString(pfx + ".description", "");
			def.role            = clsCfg.GetString(pfx + ".role",        "dps");
			def.abilities       = ReadStringArray(clsCfg, pfx + ".abilities");
			def.allowedRaceIds  = ReadStringArray(clsCfg, pfx + ".allowedRaces");

			if (!def.id.empty())
			{
				m_classes.push_back(std::move(def));
				LOG_INFO(Core, "[CharacterCreation] Loaded class '{}' (role={} abilities={} races={})",
				         m_classes.back().id, m_classes.back().role,
				         m_classes.back().abilities.size(),
				         m_classes.back().allowedRaceIds.size());
			}
			++i;
		}

		if (m_classes.empty())
		{
			LOG_WARN(Core, "[CharacterCreation] LoadClasses: no valid entries in '{}'", fullPath);
			return false;
		}

		LOG_INFO(Core, "[CharacterCreation] Loaded {} class(es) from '{}'", m_classes.size(), fullPath);
		return true;
	}

} // namespace engine::client
