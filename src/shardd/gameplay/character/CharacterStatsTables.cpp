#include "src/shardd/gameplay/character/CharacterStatsTables.h"
#include "src/shared/core/Config.h"

#include <string>

namespace engine::server::gameplay
{
	namespace
	{
		using engine::core::Config;

		/// Lit un profil d'archétype (class_profiles.<p>) en multiplicateurs.
		/// \param p préfixe complet du profil (ex. "class_profiles.melee").
		/// Clé absente = 1.0 (neutre).
		ClassProfile ParseClassProfile(const Config& c, const std::string& p)
		{
			ClassProfile cp;
			cp.hp         = c.GetDouble(p + ".hp", 1.0);
			cp.resource   = c.GetDouble(p + ".resource", 1.0);
			cp.damage     = c.GetDouble(p + ".damage", 1.0);
			cp.accuracy   = c.GetDouble(p + ".accuracy", 1.0);
			cp.range      = c.GetDouble(p + ".range", 1.0);
			cp.crit_rate  = c.GetDouble(p + ".crit_rate", 1.0);
			cp.crit_mult  = c.GetDouble(p + ".crit_mult", 1.0);
			cp.speed      = c.GetDouble(p + ".speed", 1.0);
			cp.perception = c.GetDouble(p + ".perception", 1.0);
			cp.stealth    = c.GetDouble(p + ".stealth", 1.0);
			return cp;
		}

		/// Lit un profil de race (race_profiles.<p>) en multiplicateurs.
		/// speed_walk/speed_run sont distincts (vs class_profiles.speed unique).
		RaceProfile ParseRaceProfile(const Config& c, const std::string& p)
		{
			RaceProfile rp;
			rp.hp         = c.GetDouble(p + ".hp", 1.0);
			rp.resource   = c.GetDouble(p + ".resource", 1.0);
			rp.damage     = c.GetDouble(p + ".damage", 1.0);
			rp.accuracy   = c.GetDouble(p + ".accuracy", 1.0);
			rp.range      = c.GetDouble(p + ".range", 1.0);
			rp.crit_rate  = c.GetDouble(p + ".crit_rate", 1.0);
			rp.crit_mult  = c.GetDouble(p + ".crit_mult", 1.0);
			rp.speed_walk = c.GetDouble(p + ".speed_walk", 1.0);
			rp.speed_run  = c.GetDouble(p + ".speed_run", 1.0);
			rp.perception = c.GetDouble(p + ".perception", 1.0);
			rp.stealth    = c.GetDouble(p + ".stealth", 1.0);
			return rp;
		}

		/// Lit un côté (H ou F) d'un sex_profile. Ne stocke que les clés présentes
		/// dans le JSON : une clé absente reste neutre (1.0) au moment du calcul.
		/// \param p préfixe du côté (ex. "sex_profiles.melee.H").
		/// \param out map remplie en place (effet de bord).
		void ParseSexSide(const Config& c, const std::string& p, std::unordered_map<std::string,double>& out)
		{
			static const char* kStats[] = { "hp","resource","damage","accuracy","range",
			                                 "crit_mult","speed_walk","speed_run","perception","stealth" };
			for (const char* s : kStats)
			{
				const std::string key = p + "." + s;
				if (c.Has(key)) out[s] = c.GetDouble(key, 1.0);
			}
		}
	}

	std::optional<CharacterStatsTables> CharacterStatsTables::FromEmbedded(
		const char* statsJson, const char* factionsJson)
	{
		if (!statsJson || !factionsJson) return std::nullopt;

		Config s;
		if (!s.LoadFromString(statsJson)) return std::nullopt;
		Config f;
		if (!f.LoadFromString(factionsJson)) return std::nullopt;

		CharacterStatsTables t;
		t.levelMax = static_cast<uint32_t>(s.GetInt("level_max", 100));
		t.xpBase   = s.GetDouble("xp.base", 0.0);
		t.xpFactor = s.GetDouble("xp.factor", 0.0);
		// xp.base / xp.factor sont obligatoires dès maintenant (contrat de la donnée embarquée) même si ComputeStats ne les consomme pas encore — la couche XP les utilisera.
		if (t.levelMax < 2 || t.xpBase <= 0.0 || t.xpFactor <= 0.0) return std::nullopt;

		auto base = [&](const char* name) {
			StatBase b; b.lvl1 = s.GetDouble(std::string("bases.") + name + ".lvl1", 0.0);
			b.lvl100 = s.GetDouble(std::string("bases.") + name + ".lvl100", 0.0); return b; };
		t.bases["hp"]        = base("hp");
		t.bases["resource"]  = base("resource");
		t.bases["damage"]    = base("damage");
		t.bases["accuracy"]  = base("accuracy");
		t.bases["range"]     = base("range");
		t.bases["crit_rate"] = base("crit_rate");
		t.bases["stamina"]   = base("stamina");
		t.critMultBase     = s.GetDouble("bases.crit_mult.base", 1.5);
		t.critRateCap      = s.GetDouble("bases.crit_rate.cap", 10.0);
		t.speedWalkBase    = s.GetDouble("bases.speed_walk", 2.0);
		t.speedRunBase     = s.GetDouble("bases.speed_run", 5.0);
		t.speedSprintBase  = s.GetDouble("bases.speed_sprint", 8.0);
		t.perceptionLvl1   = s.GetDouble("bases.perception_lvl1", 10.0);
		t.perceptionPerLevel = s.GetDouble("bases.perception_per_level", 0.5);

		// NB: ces listes doivent rester synchronisées avec character_stats.json — un profil/race présent dans le JSON mais absent ici est ignoré silencieusement.
		for (const char* p : { "tank","melee","sacre","distance","pisteur","voleur","healer","lanceur" })
			t.classProfiles[p] = ParseClassProfile(s, std::string("class_profiles.") + p);
		for (const char* r : { "humains","nains","orcs","elfes","demons" })
			t.raceProfiles[r] = ParseRaceProfile(s, std::string("race_profiles.") + r);
		for (const char* p : { "tank","melee","sacre","distance","pisteur","voleur","healer","lanceur" })
		{
			SexMods sm;
			ParseSexSide(s, std::string("sex_profiles.") + p + ".H", sm.H);
			ParseSexSide(s, std::string("sex_profiles.") + p + ".F", sm.F);
			t.sexProfiles[p] = std::move(sm);
		}

		size_t i = 0;
		while (f.Has("factions[" + std::to_string(i) + "].id"))
		{
			const std::string fp = "factions[" + std::to_string(i) + "]";
			FactionEntry fe;
			fe.id         = f.GetString(fp + ".id", "");
			fe.race       = f.GetString(fp + ".race", "");
			fe.selectable = f.GetBool(fp + ".selectable", false);
			size_t j = 0;
			while (f.Has(fp + ".classes[" + std::to_string(j) + "].id"))
			{
				const std::string cp = fp + ".classes[" + std::to_string(j) + "]";
				ClassEntry ce;
				ce.id       = f.GetString(cp + ".id", "");
				ce.profile  = f.GetString(cp + ".profile", "");
				ce.resource = f.GetString(cp + ".resource", "");
				if (!ce.id.empty()) fe.classesById[ce.id] = std::move(ce);
				++j;
			}
			if (!fe.id.empty()) t.factions[fe.id] = std::move(fe);
			++i;
		}
		if (t.factions.empty()) return std::nullopt;
		return t;
	}
}
