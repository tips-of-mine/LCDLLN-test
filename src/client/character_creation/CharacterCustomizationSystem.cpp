#include "src/client/character_creation/CharacterCustomizationSystem.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <optional>

namespace engine::client
{
	namespace
	{
		/// Noms de features raciales reconnus (probe lors du chargement).
		/// Doit rester synchronisé avec tools/asset_pipeline/gen_race_configs.py.
		const std::vector<std::string> kKnownRacialFeatures = {
		    "tusks", "horns", "tails", "ears", "scales", "wings", "halos", "mutations",
		};

		std::vector<std::string> ReadStringArray(const engine::core::Config& cfg,
		                                         const std::string&          prefix)
		{
			std::vector<std::string> out;
			size_t i = 0;
			while (cfg.Has(prefix + "[" + std::to_string(i) + "]"))
			{
				out.push_back(cfg.GetString(prefix + "[" + std::to_string(i) + "]", ""));
				++i;
			}
			return out;
		}

		std::vector<CustomizationModule> ReadModules(const engine::core::Config& cfg,
		                                             const std::string&          prefix)
		{
			std::vector<CustomizationModule> out;
			size_t i = 0;
			while (cfg.Has(prefix + "[" + std::to_string(i) + "].id"))
			{
				const std::string p = prefix + "[" + std::to_string(i) + "]";
				CustomizationModule m;
				m.id          = cfg.GetString(p + ".id", "");
				m.model       = cfg.GetString(p + ".model", "");
				m.displayName = cfg.GetString(p + ".displayName", m.id);
				out.push_back(std::move(m));
				++i;
			}
			return out;
		}

		std::vector<ColorOption> ReadColors(const engine::core::Config& cfg,
		                                    const std::string&          prefix)
		{
			std::vector<ColorOption> out;
			size_t i = 0;
			while (cfg.Has(prefix + "[" + std::to_string(i) + "].id"))
			{
				const std::string p = prefix + "[" + std::to_string(i) + "]";
				ColorOption c;
				c.id          = cfg.GetString(p + ".id", "");
				c.displayName = cfg.GetString(p + ".displayName", c.id);
				c.hex         = cfg.GetString(p + ".hex", "");
				c.diffuse     = cfg.GetString(p + ".diffuse", "");
				c.normal      = cfg.GetString(p + ".normal", "");
				c.orm         = cfg.GetString(p + ".orm", "");
				c.texture     = cfg.GetString(p + ".texture", "");
				c.emissive    = cfg.GetBool(p + ".emissive", false);
				out.push_back(std::move(c));
				++i;
			}
			return out;
		}

		std::vector<MorphTargetDef> ReadMorphs(const engine::core::Config& cfg,
		                                       const std::string&          prefix)
		{
			std::vector<MorphTargetDef> out;
			size_t i = 0;
			while (cfg.Has(prefix + "[" + std::to_string(i) + "].name"))
			{
				const std::string p = prefix + "[" + std::to_string(i) + "]";
				MorphTargetDef m;
				m.name         = cfg.GetString(p + ".name", "");
				m.displayName  = cfg.GetString(p + ".displayName", m.name);
				m.min          = cfg.GetDouble(p + ".min", -1.0);
				m.max          = cfg.GetDouble(p + ".max", 1.0);
				m.defaultValue = cfg.GetDouble(p + ".default", 0.0);
				out.push_back(std::move(m));
				++i;
			}
			return out;
		}

		// ---- Helpers JSON pour CharacterCustomization::To/FromJson ----

		void JsonEscapeInto(std::string& out, const std::string& s)
		{
			for (char ch : s)
			{
				if (ch == '"' || ch == '\\')
					out.push_back('\\');
				out.push_back(ch);
			}
		}

		std::optional<std::string> JsonRawValue(const std::string& j, const std::string& key)
		{
			const std::string needle = "\"" + key + "\"";
			const size_t k = j.find(needle);
			if (k == std::string::npos)
				return std::nullopt;
			size_t c = j.find(':', k + needle.size());
			if (c == std::string::npos)
				return std::nullopt;
			size_t i = c + 1;
			while (i < j.size() && std::isspace(static_cast<unsigned char>(j[i])))
				++i;
			if (i >= j.size())
				return std::nullopt;

			if (j[i] == '"')
			{
				std::string out;
				size_t end = i + 1;
				while (end < j.size() && j[end] != '"')
				{
					if (j[end] == '\\' && end + 1 < j.size())
					{
						++end;
						out.push_back(j[end]);
					}
					else
					{
						out.push_back(j[end]);
					}
					++end;
				}
				return out;
			}

			size_t end = i;
			while (end < j.size() && j[end] != ',' && j[end] != '}' &&
			       !std::isspace(static_cast<unsigned char>(j[end])))
				++end;
			return j.substr(i, end - i);
		}

		std::optional<double> JsonNumber(const std::string& j, const std::string& key)
		{
			auto raw = JsonRawValue(j, key);
			if (!raw)
				return std::nullopt;
			try
			{
				return std::stod(*raw);
			}
			catch (...)
			{
				return std::nullopt;
			}
		}
	} // namespace

	// ========================================================================
	// RaceConfiguration
	// ========================================================================

	bool RaceConfiguration::LoadFromFile(const std::string& jsonPath, RaceConfiguration& out)
	{
		engine::core::Config cfg;
		if (!cfg.LoadFromFile(jsonPath))
		{
			LOG_WARN(CharCustom, "[Customization] LoadFromFile: cannot open '{}'", jsonPath);
			return false;
		}

		out.raceId = cfg.GetString("raceId", "");
		if (out.raceId.empty())
		{
			LOG_WARN(CharCustom, "[Customization] LoadFromFile: no raceId in '{}'", jsonPath);
			return false;
		}

		out.displayName  = cfg.GetString("displayName", out.raceId);
		out.description  = cfg.GetString("description", "");
		out.baseSkeleton = cfg.GetString("baseSkeleton", "humanoid_base");
		out.animationSet = cfg.GetString("animationSet", "humanoid_base");

		auto& h               = out.physicalLimits.height;
		h.baseMeters          = cfg.GetDouble("physicalLimits.height.baseMeters", 1.75);
		h.scaleMin            = cfg.GetDouble("physicalLimits.height.scaleRange.min", 0.9);
		h.scaleMax            = cfg.GetDouble("physicalLimits.height.scaleRange.max", 1.1);
		h.scaleDefault        = cfg.GetDouble("physicalLimits.height.scaleRange.default", 1.0);

		out.physicalLimits.bodyMass.min          = cfg.GetDouble("physicalLimits.bodyMass.range.min", 0.0);
		out.physicalLimits.bodyMass.max          = cfg.GetDouble("physicalLimits.bodyMass.range.max", 0.0);
		out.physicalLimits.bodyMass.defaultValue = cfg.GetDouble("physicalLimits.bodyMass.range.default", 0.0);

		auto readProp = [&cfg](const std::string& name, ValueRange& r) {
			const std::string base = "physicalLimits.proportions." + name;
			r.min          = cfg.GetDouble(base + ".range.min", 1.0);
			r.max          = cfg.GetDouble(base + ".range.max", 1.0);
			r.defaultValue = cfg.GetDouble(base + ".default", 1.0);
		};
		readProp("legLength", out.physicalLimits.proportions.legLength);
		readProp("shoulderWidth", out.physicalLimits.proportions.shoulderWidth);
		readProp("torsoWidth", out.physicalLimits.proportions.torsoWidth);

		out.collisionDefaults.radius = cfg.GetDouble("collisionDefaults.radius", 0.45);
		out.collisionDefaults.height = cfg.GetDouble("collisionDefaults.height", 1.75);

		out.genders = ReadStringArray(cfg, "genders");
		if (out.genders.empty())
			out.genders = {"male", "female"};

		for (const auto& g : out.genders)
		{
			out.bodyTypes[g]  = ReadModules(cfg, "bodyTypes." + g);
			out.heads[g]      = ReadModules(cfg, "heads." + g);
			out.hairStyles[g] = ReadModules(cfg, "hairStyles." + g);
			auto fh           = ReadModules(cfg, "facialHair." + g);
			if (!fh.empty())
				out.facialHair[g] = std::move(fh);
		}

		for (const auto& feat : kKnownRacialFeatures)
		{
			auto mods = ReadModules(cfg, "racialFeatures." + feat);
			if (!mods.empty())
				out.racialFeatures[feat] = std::move(mods);
		}

		out.skinTones  = ReadColors(cfg, "skinTones");
		out.hairColors = ReadColors(cfg, "hairColors");
		out.eyeColors  = ReadColors(cfg, "eyeColors");

		out.faceMorphs = ReadMorphs(cfg, "morphTargets.face");
		out.bodyMorphs = ReadMorphs(cfg, "morphTargets.body");

		out.additionalAnimations = ReadStringArray(cfg, "additionalAnimations");

		return true;
	}

	const std::vector<CustomizationModule>* RaceConfiguration::ModulesFor(
	    const std::unordered_map<std::string, std::vector<CustomizationModule>>& map,
	    const std::string& gender) const
	{
		const auto it = map.find(gender);
		return (it != map.end()) ? &it->second : nullptr;
	}

	bool RaceConfiguration::HasGender(const std::string& gender) const
	{
		return std::find(genders.begin(), genders.end(), gender) != genders.end();
	}

	// ========================================================================
	// CharacterCustomizationSystem
	// ========================================================================

	CharacterCustomizationSystem::CharacterCustomizationSystem()
	    : m_rng(std::random_device{}())
	{
	}

	CharacterCustomizationSystem::~CharacterCustomizationSystem() = default;

	bool CharacterCustomizationSystem::Initialize(const std::string& configBasePath)
	{
		m_raceConfigs.clear();

		namespace fs = std::filesystem;
		const fs::path racesDir = fs::path(configBasePath) / "races";

		std::error_code ec;
		if (!fs::is_directory(racesDir, ec))
		{
			LOG_WARN(CharCustom, "[Customization] Init: races dir not found '{}'",
			         racesDir.string());
			return false;
		}

		for (const auto& entry : fs::directory_iterator(racesDir, ec))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".json")
				continue;

			RaceConfiguration cfg;
			if (RaceConfiguration::LoadFromFile(entry.path().string(), cfg))
			{
				auto sp                  = std::make_shared<RaceConfiguration>(std::move(cfg));
				const std::string raceId = sp->raceId;
				m_raceConfigs[raceId]    = std::move(sp);
				LOG_INFO(CharCustom, "[Customization] Loaded race '{}'", raceId);
			}
		}

		LOG_INFO(CharCustom, "[Customization] Initialized with {} race(s)", m_raceConfigs.size());
		return !m_raceConfigs.empty();
	}

	bool CharacterCustomizationSystem::InRange(double v, const ValueRange& r)
	{
		constexpr double kEps = 1e-4;
		return v >= r.min - kEps && v <= r.max + kEps;
	}

	const ColorOption* CharacterCustomizationSystem::FindColor(
	    const std::vector<ColorOption>& palette, const std::string& id)
	{
		for (const auto& c : palette)
			if (c.id == id)
				return &c;
		return nullptr;
	}

	const RaceConfiguration* CharacterCustomizationSystem::GetRaceConfig(
	    const std::string& raceId) const
	{
		const auto it = m_raceConfigs.find(raceId);
		return (it != m_raceConfigs.end()) ? it->second.get() : nullptr;
	}

	std::vector<std::string> CharacterCustomizationSystem::GetAvailableRaces() const
	{
		std::vector<std::string> races;
		races.reserve(m_raceConfigs.size());
		for (const auto& [id, cfg] : m_raceConfigs)
			races.push_back(id);
		std::sort(races.begin(), races.end());
		return races;
	}

	bool CharacterCustomizationSystem::ValidateCustomization(
	    const CharacterCustomization& custom) const
	{
		return GetValidationErrors(custom).empty();
	}

	std::vector<std::string> CharacterCustomizationSystem::GetValidationErrors(
	    const CharacterCustomization& custom) const
	{
		std::vector<std::string> errors;

		const RaceConfiguration* race = GetRaceConfig(custom.raceId);
		if (!race)
		{
			errors.push_back("Unknown race ID: " + custom.raceId);
			return errors;
		}

		if (!race->HasGender(custom.gender))
		{
			errors.push_back("Invalid gender '" + custom.gender + "' for race " + custom.raceId);
			return errors; // sans genre valide on ne peut pas indexer les modules
		}

		const auto& limits = race->physicalLimits;
		if (custom.bodyMetrics.heightScale < limits.height.scaleMin - 1e-4 ||
		    custom.bodyMetrics.heightScale > limits.height.scaleMax + 1e-4)
			errors.push_back("heightScale out of range");
		if (!InRange(custom.bodyMetrics.bodyMassIndex, limits.bodyMass))
			errors.push_back("bodyMassIndex out of range");
		if (!InRange(custom.bodyMetrics.legLengthRatio, limits.proportions.legLength))
			errors.push_back("legLengthRatio out of range");
		if (!InRange(custom.bodyMetrics.shoulderWidthRatio, limits.proportions.shoulderWidth))
			errors.push_back("shoulderWidthRatio out of range");
		if (!InRange(custom.bodyMetrics.torsoWidthRatio, limits.proportions.torsoWidth))
			errors.push_back("torsoWidthRatio out of range");

		const auto* bodyTypes = race->ModulesFor(race->bodyTypes, custom.gender);
		if (!bodyTypes || bodyTypes->empty())
		{
			errors.push_back("No body types for gender " + custom.gender);
		}
		else
		{
			const bool found = std::any_of(bodyTypes->begin(), bodyTypes->end(),
			    [&](const CustomizationModule& m) { return m.id == custom.bodyTypeId; });
			if (!found)
				errors.push_back("Unknown bodyTypeId: " + custom.bodyTypeId);
		}

		const auto* heads = race->ModulesFor(race->heads, custom.gender);
		if (!heads || custom.headIndex >= heads->size())
			errors.push_back("Invalid headIndex");

		const auto* hair = race->ModulesFor(race->hairStyles, custom.gender);
		if (!hair || custom.hairStyleIndex >= hair->size())
			errors.push_back("Invalid hairStyleIndex");

		const auto* facial = race->ModulesFor(race->facialHair, custom.gender);
		if (facial && !facial->empty() && custom.facialHairIndex >= facial->size())
			errors.push_back("Invalid facialHairIndex");

		if (!FindColor(race->skinTones, custom.skinToneId))
			errors.push_back("Unknown skinToneId: " + custom.skinToneId);
		if (!FindColor(race->hairColors, custom.hairColorId))
			errors.push_back("Unknown hairColorId: " + custom.hairColorId);
		if (!FindColor(race->eyeColors, custom.eyeColorId))
			errors.push_back("Unknown eyeColorId: " + custom.eyeColorId);

		// Traits raciaux optionnels.
		auto checkFeature = [&](const std::optional<uint32_t>& idx, const std::string& feat) {
			if (!idx.has_value())
				return;
			const auto it = race->racialFeatures.find(feat);
			if (it == race->racialFeatures.end())
				errors.push_back("Race " + custom.raceId + " has no feature '" + feat + "'");
			else if (idx.value() >= it->second.size())
				errors.push_back("Invalid index for feature '" + feat + "'");
		};
		checkFeature(custom.tuskIndex, "tusks");
		checkFeature(custom.hornIndex, "horns");
		checkFeature(custom.tailIndex, "tails");
		checkFeature(custom.earIndex, "ears");
		checkFeature(custom.scalesIndex, "scales");
		checkFeature(custom.wingsIndex, "wings");

		return errors;
	}

	ResolvedCharacterAssets CharacterCustomizationSystem::ResolveCustomization(
	    const CharacterCustomization& custom) const
	{
		ResolvedCharacterAssets out;

		const auto errs = GetValidationErrors(custom);
		if (!errs.empty())
		{
			LOG_WARN(CharCustom, "[Customization] Resolve: invalid customization ({} error(s))",
			         errs.size());
			return out;
		}

		const RaceConfiguration* race = GetRaceConfig(custom.raceId);
		const std::string&       g    = custom.gender;

		// Corps.
		if (const auto* bodyTypes = race->ModulesFor(race->bodyTypes, g))
			for (const auto& bt : *bodyTypes)
				if (bt.id == custom.bodyTypeId)
				{
					out.bodyMeshPath = bt.model;
					break;
				}

		// Tête + cheveux.
		if (const auto* heads = race->ModulesFor(race->heads, g))
			out.attachments.push_back({"head", "head", (*heads)[custom.headIndex].model});
		if (const auto* hair = race->ModulesFor(race->hairStyles, g))
			out.attachments.push_back({"hair", "head", (*hair)[custom.hairStyleIndex].model});

		// Pilosité faciale (ignorer "none").
		if (const auto* facial = race->ModulesFor(race->facialHair, g);
		    facial && custom.facialHairIndex < facial->size())
		{
			const auto& fh = (*facial)[custom.facialHairIndex];
			if (fh.id != "none")
				out.attachments.push_back({"facial_hair", "head", fh.model});
		}

		// Traits raciaux.
		auto attachFeature = [&](const std::optional<uint32_t>& idx, const std::string& feat,
		                         const std::string& socket) {
			if (!idx.has_value())
				return;
			const auto it = race->racialFeatures.find(feat);
			if (it == race->racialFeatures.end() || idx.value() >= it->second.size())
				return;
			const auto& mod = it->second[idx.value()];
			if (mod.id != "none")
				out.attachments.push_back({feat, socket, mod.model});
		};
		attachFeature(custom.tuskIndex, "tusks", "head");
		attachFeature(custom.hornIndex, "horns", "head");
		attachFeature(custom.earIndex, "ears", "head");
		attachFeature(custom.tailIndex, "tails", "pelvis");
		attachFeature(custom.scalesIndex, "scales", "spine_02");
		attachFeature(custom.wingsIndex, "wings", "spine_03");

		// Couleurs.
		if (const auto* skin = FindColor(race->skinTones, custom.skinToneId))
		{
			out.skinHex     = skin->hex;
			out.skinDiffuse = skin->diffuse;
			out.skinNormal  = skin->normal;
			out.skinOrm     = skin->orm;
		}
		if (const auto* hc = FindColor(race->hairColors, custom.hairColorId))
		{
			out.hairColorHex     = hc->hex;
			out.hairColorTexture = hc->texture;
		}
		if (const auto* ec = FindColor(race->eyeColors, custom.eyeColorId))
		{
			out.eyeColorHex     = ec->hex;
			out.eyeColorTexture = ec->texture;
		}

		// Métriques.
		const auto& bm  = custom.bodyMetrics;
		out.rootScale   = bm.heightScale;

		out.boneScales.push_back({"thigh_left", 1.0f, bm.legLengthRatio, 1.0f});
		out.boneScales.push_back({"thigh_right", 1.0f, bm.legLengthRatio, 1.0f});
		out.boneScales.push_back({"calf_left", 1.0f, bm.legLengthRatio, 1.0f});
		out.boneScales.push_back({"calf_right", 1.0f, bm.legLengthRatio, 1.0f});
		out.boneScales.push_back({"clavicle_left", bm.shoulderWidthRatio, 1.0f, 1.0f});
		out.boneScales.push_back({"clavicle_right", bm.shoulderWidthRatio, 1.0f, 1.0f});
		out.boneScales.push_back({"spine_01", bm.torsoWidthRatio, 1.0f, bm.torsoWidthRatio});

		const double maxWidth = std::max(bm.torsoWidthRatio, bm.shoulderWidthRatio);
		out.collisionRadius =
		    static_cast<float>(race->collisionDefaults.radius * bm.heightScale * maxWidth);
		out.collisionHeight =
		    static_cast<float>(race->collisionDefaults.height * bm.heightScale * bm.legLengthRatio);

		out.valid = true;
		return out;
	}

	void CharacterCustomizationSystem::ApplyCustomization(
	    const CharacterCustomization& custom) const
	{
		// STUB DOCUMENTÉ — voir en-tête : aucun système GameObject/Skeleton/
		// composant n'existe encore dans le moteur. On résout les assets et on
		// trace le plan d'application ; le câblage GPU/scène est différé.
		const ResolvedCharacterAssets assets = ResolveCustomization(custom);
		if (!assets.valid)
		{
			LOG_WARN(CharCustom, "[Customization] Apply: customization invalid, nothing applied");
			return;
		}

		LOG_INFO(CharCustom,
		         "[Customization] Apply (resolved, not yet wired to renderer): race='{}' gender='{}' "
		         "body='{}' attachments={} rootScale={:.3f} collision(r={:.3f},h={:.3f})",
		         custom.raceId, custom.gender, assets.bodyMeshPath, assets.attachments.size(),
		         assets.rootScale, assets.collisionRadius, assets.collisionHeight);
		for (const auto& a : assets.attachments)
			LOG_DEBUG(CharCustom, "[Customization]   attach {} -> socket '{}' : {}", a.kind,
			          a.socket, a.modelPath);
	}

	CharacterCustomization CharacterCustomizationSystem::MakeDefaultCustomization(
	    const std::string& raceId, const std::string& gender) const
	{
		CharacterCustomization c;
		const RaceConfiguration* race = GetRaceConfig(raceId);
		if (!race || !race->HasGender(gender))
		{
			LOG_WARN(CharCustom, "[Customization] MakeDefault: invalid race/gender '{}'/'{}'",
			         raceId, gender);
			return c;
		}

		c.raceId = raceId;
		c.gender = gender;

		if (const auto* bt = race->ModulesFor(race->bodyTypes, gender); bt && !bt->empty())
			c.bodyTypeId = (*bt)[0].id;

		if (!race->skinTones.empty())
			c.skinToneId = race->skinTones[0].id;
		if (!race->hairColors.empty())
			c.hairColorId = race->hairColors[0].id;
		if (!race->eyeColors.empty())
			c.eyeColorId = race->eyeColors[0].id;

		const auto& lim                = race->physicalLimits;
		c.bodyMetrics.heightScale      = static_cast<float>(lim.height.scaleDefault);
		c.bodyMetrics.legLengthRatio   = static_cast<float>(lim.proportions.legLength.defaultValue);
		c.bodyMetrics.shoulderWidthRatio = static_cast<float>(lim.proportions.shoulderWidth.defaultValue);
		c.bodyMetrics.torsoWidthRatio  = static_cast<float>(lim.proportions.torsoWidth.defaultValue);
		c.bodyMetrics.bodyMassIndex    = static_cast<float>(lim.bodyMass.defaultValue);

		for (const auto& m : race->faceMorphs)
			c.morphWeights[m.name] = static_cast<float>(m.defaultValue);
		for (const auto& m : race->bodyMorphs)
			c.morphWeights[m.name] = static_cast<float>(m.defaultValue);

		return c;
	}

	CharacterCustomization CharacterCustomizationSystem::GenerateRandomCustomization(
	    const std::string& raceId, const std::string& gender)
	{
		CharacterCustomization c = MakeDefaultCustomization(raceId, gender);
		const RaceConfiguration* race = GetRaceConfig(raceId);
		if (!race || c.raceId.empty())
			return c;

		auto pick = [this](size_t n) -> uint32_t {
			if (n == 0)
				return 0;
			std::uniform_int_distribution<uint32_t> d(0, static_cast<uint32_t>(n - 1));
			return d(m_rng);
		};

		if (const auto* bt = race->ModulesFor(race->bodyTypes, gender); bt && !bt->empty())
			c.bodyTypeId = (*bt)[pick(bt->size())].id;
		if (const auto* heads = race->ModulesFor(race->heads, gender); heads && !heads->empty())
			c.headIndex = pick(heads->size());
		if (const auto* hair = race->ModulesFor(race->hairStyles, gender); hair && !hair->empty())
			c.hairStyleIndex = pick(hair->size());
		if (const auto* facial = race->ModulesFor(race->facialHair, gender);
		    facial && !facial->empty())
			c.facialHairIndex = pick(facial->size());

		if (!race->skinTones.empty())
			c.skinToneId = race->skinTones[pick(race->skinTones.size())].id;
		if (!race->hairColors.empty())
			c.hairColorId = race->hairColors[pick(race->hairColors.size())].id;
		if (!race->eyeColors.empty())
			c.eyeColorId = race->eyeColors[pick(race->eyeColors.size())].id;

		// Traits raciaux disponibles -> index aléatoire.
		auto maybeFeature = [&](std::optional<uint32_t>& slot, const std::string& feat) {
			const auto it = race->racialFeatures.find(feat);
			if (it != race->racialFeatures.end() && !it->second.empty())
				slot = pick(it->second.size());
		};
		maybeFeature(c.tuskIndex, "tusks");
		maybeFeature(c.hornIndex, "horns");
		maybeFeature(c.tailIndex, "tails");
		maybeFeature(c.earIndex, "ears");
		maybeFeature(c.scalesIndex, "scales");
		maybeFeature(c.wingsIndex, "wings");

		return c;
	}

	// ========================================================================
	// CharacterCustomization (serialization / validation)
	// ========================================================================

	bool CharacterCustomization::IsValid() const
	{
		return !raceId.empty() && (gender == "male" || gender == "female");
	}

	std::string CharacterCustomization::ToJson() const
	{
		std::string j = "{";
		auto addStr = [&](const std::string& key, const std::string& val, bool comma = true) {
			j += "\"" + key + "\":\"";
			JsonEscapeInto(j, val);
			j += "\"";
			if (comma)
				j += ",";
		};
		auto addNum = [&](const std::string& key, double val, bool comma = true) {
			j += "\"" + key + "\":" + std::to_string(val);
			if (comma)
				j += ",";
		};

		addStr("raceId", raceId);
		addStr("gender", gender);
		addStr("bodyTypeId", bodyTypeId);
		addNum("headIndex", headIndex);
		addNum("hairStyleIndex", hairStyleIndex);
		addNum("facialHairIndex", facialHairIndex);
		addStr("skinToneId", skinToneId);
		addStr("hairColorId", hairColorId);
		addStr("eyeColorId", eyeColorId);
		addNum("bodyMetrics.heightScale", bodyMetrics.heightScale);
		addNum("bodyMetrics.legLengthRatio", bodyMetrics.legLengthRatio);
		addNum("bodyMetrics.torsoWidthRatio", bodyMetrics.torsoWidthRatio);
		addNum("bodyMetrics.shoulderWidthRatio", bodyMetrics.shoulderWidthRatio);
		addNum("bodyMetrics.bodyMassIndex", bodyMetrics.bodyMassIndex);

		for (const auto& [name, w] : morphWeights)
			addNum("morph." + name, w);

		// Optionnels (émis seulement si présents).
		auto addOpt = [&](const std::string& key, const std::optional<uint32_t>& v) {
			if (v.has_value())
				addNum(key, v.value());
		};
		addOpt("tuskIndex", tuskIndex);
		addOpt("hornIndex", hornIndex);
		addOpt("tailIndex", tailIndex);
		addOpt("earIndex", earIndex);
		addOpt("scalesIndex", scalesIndex);
		addOpt("wingsIndex", wingsIndex);

		// "version" en dernier (sans virgule traînante).
		j += "\"version\":\"1.0.0\"}";
		return j;
	}

	CharacterCustomization CharacterCustomization::FromJson(const std::string& jsonStr)
	{
		CharacterCustomization c;

		if (auto v = JsonRawValue(jsonStr, "raceId"))
			c.raceId = *v;
		if (auto v = JsonRawValue(jsonStr, "gender"))
			c.gender = *v;
		if (auto v = JsonRawValue(jsonStr, "bodyTypeId"))
			c.bodyTypeId = *v;
		if (auto v = JsonRawValue(jsonStr, "skinToneId"))
			c.skinToneId = *v;
		if (auto v = JsonRawValue(jsonStr, "hairColorId"))
			c.hairColorId = *v;
		if (auto v = JsonRawValue(jsonStr, "eyeColorId"))
			c.eyeColorId = *v;

		if (auto v = JsonNumber(jsonStr, "headIndex"))
			c.headIndex = static_cast<uint32_t>(*v);
		if (auto v = JsonNumber(jsonStr, "hairStyleIndex"))
			c.hairStyleIndex = static_cast<uint32_t>(*v);
		if (auto v = JsonNumber(jsonStr, "facialHairIndex"))
			c.facialHairIndex = static_cast<uint32_t>(*v);

		if (auto v = JsonNumber(jsonStr, "bodyMetrics.heightScale"))
			c.bodyMetrics.heightScale = static_cast<float>(*v);
		if (auto v = JsonNumber(jsonStr, "bodyMetrics.legLengthRatio"))
			c.bodyMetrics.legLengthRatio = static_cast<float>(*v);
		if (auto v = JsonNumber(jsonStr, "bodyMetrics.torsoWidthRatio"))
			c.bodyMetrics.torsoWidthRatio = static_cast<float>(*v);
		if (auto v = JsonNumber(jsonStr, "bodyMetrics.shoulderWidthRatio"))
			c.bodyMetrics.shoulderWidthRatio = static_cast<float>(*v);
		if (auto v = JsonNumber(jsonStr, "bodyMetrics.bodyMassIndex"))
			c.bodyMetrics.bodyMassIndex = static_cast<float>(*v);

		for (auto& [name, w] : c.morphWeights)
			if (auto v = JsonNumber(jsonStr, "morph." + name))
				w = static_cast<float>(*v);

		auto readOpt = [&](const std::string& key, std::optional<uint32_t>& slot) {
			if (auto v = JsonNumber(jsonStr, key))
				slot = static_cast<uint32_t>(*v);
		};
		readOpt("tuskIndex", c.tuskIndex);
		readOpt("hornIndex", c.hornIndex);
		readOpt("tailIndex", c.tailIndex);
		readOpt("earIndex", c.earIndex);
		readOpt("scalesIndex", c.scalesIndex);
		readOpt("wingsIndex", c.wingsIndex);

		return c;
	}

} // namespace engine::client
