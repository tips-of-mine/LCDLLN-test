#include "src/client/character_creation/AppearanceCatalog.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>

namespace engine::client
{
	using engine::network::AppearanceProp;
	using engine::network::AppearanceSlot;
	using engine::network::CharacterAppearance;
	using engine::network::kAppearanceSlotCount;

	namespace
	{
		/// Clés JSON des emplacements, dans l'ordre de AppearanceSlot.
		constexpr const char* kSlotKeys[kAppearanceSlotCount] = {
			"face", "skinTone", "hair", "hairColor", "eyeColor", "beard",
			"moustache", "ears", "horns", "tail", "wings", "tusks",
		};

		/// Libellés par défaut (si le catalogue n'en fournit pas).
		constexpr const char* kDefaultLabels[kAppearanceSlotCount] = {
			"Visage", "Teinte de peau", "Coiffure", "Couleur des cheveux", "Couleur des yeux", "Barbe",
			"Moustache", "Oreilles", "Cornes", "Queue", "Ailes", "Défenses",
		};

		/// Lit un tableau de chaînes « prefix[i] ».
		std::vector<std::string> ReadStrings(const engine::core::Config& cfg, const std::string& prefix)
		{
			std::vector<std::string> out;
			for (size_t i = 0; cfg.Has(prefix + "[" + std::to_string(i) + "]"); ++i)
				out.push_back(cfg.GetString(prefix + "[" + std::to_string(i) + "]", ""));
			return out;
		}

		/// Indice borné : hors liste -> 0.
		std::size_t Clamp(uint8_t index, std::size_t count)
		{
			return (count == 0u || index >= count) ? 0u : index;
		}
	}

	uint32_t ParseHexColorRgba8(const std::string& hex, uint32_t fallback)
	{
		if (hex.size() != 7u || hex[0] != '#')
			return fallback;
		uint32_t rgb = 0;
		for (std::size_t i = 1; i < 7u; ++i)
		{
			const char c = hex[i];
			uint32_t v = 0;
			if (c >= '0' && c <= '9') v = static_cast<uint32_t>(c - '0');
			else if (c >= 'A' && c <= 'F') v = static_cast<uint32_t>(c - 'A' + 10);
			else if (c >= 'a' && c <= 'f') v = static_cast<uint32_t>(c - 'a' + 10);
			else return fallback;
			rgb = (rgb << 4) | v;
		}
		const uint32_t r = (rgb >> 16) & 0xFFu, g = (rgb >> 8) & 0xFFu, b = rgb & 0xFFu;
		return r | (g << 8) | (b << 16) | (0xFFu << 24);
	}

	const char* AppearanceSlotKey(AppearanceSlot s)
	{
		const auto i = static_cast<std::size_t>(s);
		return i < kAppearanceSlotCount ? kSlotKeys[i] : "";
	}

	bool AppearanceCatalog::Init(const engine::core::Config& config)
	{
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, kRelativePath);
		if (text.empty())
		{
			LOG_WARN(Core, "[AppearanceCatalog] Catalogue absent ({}) — personnalisation désactivée", kRelativePath);
			return false;
		}
		std::string err;
		if (!LoadFromText(text, err))
		{
			LOG_WARN(Core, "[AppearanceCatalog] Catalogue invalide ({}) : {}", kRelativePath, err);
			return false;
		}
		LOG_INFO(Core, "[AppearanceCatalog] Init OK ({} races)", m_races.size());
		return true;
	}

	bool AppearanceCatalog::LoadFromText(std::string_view jsonText, std::string& outError)
	{
		engine::core::Config cfg;
		if (!cfg.LoadFromString(jsonText))
		{
			outError = "JSON illisible";
			return false;
		}
		std::unordered_map<std::string, AppearanceRaceGender> entries;
		std::vector<std::string> races = ReadStrings(cfg, "raceOrder");
		if (races.empty())
			races = { "humains", "elfes", "nains", "orcs", "demons" };
		std::vector<std::string> present;
		for (const std::string& race : races)
		{
			bool any = false;
			for (const char* gender : { "male", "female" })
			{
				const std::string base = "races." + race + "." + gender;
				if (!cfg.Has(base + ".body"))
					continue;
				AppearanceRaceGender rg;
				rg.body = cfg.GetString(base + ".body", "");
				rg.hiddenBodyMeshes = ReadStrings(cfg, base + ".hiddenBodyMeshes");
				for (std::size_t s = 0; s < kAppearanceSlotCount; ++s)
				{
					const std::string sp = base + ".slots." + kSlotKeys[s];
					for (size_t i = 0; cfg.Has(sp + "[" + std::to_string(i) + "].id"); ++i)
					{
						const std::string op = sp + "[" + std::to_string(i) + "]";
						AppearanceOption o;
						o.id = cfg.GetString(op + ".id", "");
						o.name = cfg.GetString(op + ".name", o.id);
						o.lods = ReadStrings(cfg, op + ".lods");
						o.texture = cfg.GetString(op + ".texture", cfg.GetString(op + ".basecolor", ""));
						o.hex = cfg.GetString(op + ".hex", "");
						o.tintRgba8 = ParseHexColorRgba8(cfg.GetString(op + ".tint", ""));
						rg.slots[s].push_back(std::move(o));
					}
				}
				if (rg.body.empty() || rg.slots[static_cast<std::size_t>(AppearanceSlot::Face)].empty())
				{
					outError = "race " + race + "/" + gender + " : corps ou visages absents";
					return false;
				}
				entries.emplace(Key(race, gender), std::move(rg));
				any = true;
			}
			if (any)
				present.push_back(race);
		}
		if (entries.empty())
		{
			outError = "aucune race";
			return false;
		}
		m_entries = std::move(entries);
		m_races = std::move(present);
		for (std::size_t s = 0; s < kAppearanceSlotCount; ++s)
			m_slotLabels[s] = cfg.GetString(std::string("slotLabels.") + kSlotKeys[s], kDefaultLabels[s]);
		m_skinMaterials = ReadStrings(cfg, "materials.skin");
		if (m_skinMaterials.empty())
			m_skinMaterials = { "MI_Regular_Male", "MI_Regular_Female" };
		m_hairMaterial = cfg.GetString("materials.hair", "MI_Hair");
		m_eyeMaterial = cfg.GetString("materials.eye", "MI_Eye");
		m_hornMaterial = cfg.GetString("materials.horn", "MI_Horn");
		m_tuskMaterial = cfg.GetString("materials.tusk", "MI_Tusk");
		m_hornTexture = cfg.GetString("hornTexture", "");
		m_tuskTexture = cfg.GetString("tuskTexture", "");
		return true;
	}

	const AppearanceRaceGender* AppearanceCatalog::Find(const std::string& raceId, const std::string& gender) const
	{
		const auto it = m_entries.find(Key(raceId, gender == "female" ? "female" : "male"));
		return it == m_entries.end() ? nullptr : &it->second;
	}

	void AppearanceCatalog::Sanitize(const std::string& raceId, const std::string& gender, CharacterAppearance& a) const
	{
		const AppearanceRaceGender* rg = Find(raceId, gender);
		if (rg == nullptr)
			return;
		for (std::size_t s = 0; s < kAppearanceSlotCount; ++s)
			a.slots[s] = static_cast<uint8_t>(Clamp(a.slots[s], rg->slots[s].size()));
	}

	ResolvedAppearance AppearanceCatalog::Resolve(const std::string& raceId, const std::string& gender,
	                                              const CharacterAppearance& a, int lod) const
	{
		ResolvedAppearance out;
		const AppearanceRaceGender* rg = Find(raceId, gender);
		if (rg == nullptr)
			return out;
		lod = std::clamp(lod, 0, 2);
		out.body = rg->body;
		out.hiddenBodyMeshes = rg->hiddenBodyMeshes;
		auto option = [&](AppearanceSlot s) -> const AppearanceOption* {
			const auto& opts = rg->Options(s);
			if (opts.empty())
				return nullptr;
			return &opts[Clamp(a.Get(s), opts.size())];
		};
		// Pièces géométriques, dans un ordre stable (visage d'abord).
		for (AppearanceSlot s : { AppearanceSlot::Face, AppearanceSlot::Hair, AppearanceSlot::Beard,
		                          AppearanceSlot::Moustache, AppearanceSlot::Ears, AppearanceSlot::Horns,
		                          AppearanceSlot::Tail, AppearanceSlot::Wings, AppearanceSlot::Tusks })
		{
			const AppearanceOption* o = option(s);
			if (o == nullptr || o->lods.empty())
				continue;
			const std::size_t li = std::min<std::size_t>(static_cast<std::size_t>(lod), o->lods.size() - 1u);
			out.parts.push_back(o->lods[li]);
		}
		if (const AppearanceOption* o = option(AppearanceSlot::SkinTone)) { out.skinTexture = o->texture; out.skinTint = o->tintRgba8; }
		if (const AppearanceOption* o = option(AppearanceSlot::HairColor)) { out.hairTexture = o->texture; out.hairTint = o->tintRgba8; }
		if (const AppearanceOption* o = option(AppearanceSlot::EyeColor)) { out.eyeTexture = o->texture; out.eyeTint = o->tintRgba8; }
		out.heightScale = a.ProportionFactor(AppearanceProp::Height);
		out.widthScale = std::clamp(a.ProportionFactor(AppearanceProp::TorsoWidth) * 0.4f
			+ a.ProportionFactor(AppearanceProp::ShoulderWidth) * 0.3f
			+ a.ProportionFactor(AppearanceProp::BodyMass) * 0.3f, 0.85f, 1.15f);
		return out;
	}

	std::vector<std::string> AppearanceCatalog::AllReferencedFiles() const
	{
		std::vector<std::string> out;
		auto add = [&out](const std::string& p) {
			if (!p.empty() && std::find(out.begin(), out.end(), p) == out.end())
				out.push_back(p);
		};
		add(m_hornTexture);
		add(m_tuskTexture);
		for (const auto& kv : m_entries)
		{
			add(kv.second.body);
			for (const auto& opts : kv.second.slots)
			{
				for (const auto& o : opts)
				{
					for (const auto& l : o.lods)
						add(l);
					add(o.texture);
				}
			}
		}
		return out;
	}
}
