#include "src/shardd/gameplay/character/CharacterStatsEngine.h"

#include <algorithm>
#include <cmath>

namespace engine::server::gameplay
{
	namespace
	{
		/// Interpolation linéaire d'une StatBase entre lvl1 et levelMax.
		/// \param level niveau demandé (clampé [1, levelMax]).
		/// \return la valeur de base au niveau donné.
		double BaseAt(const StatBase& b, uint32_t level, uint32_t levelMax)
		{
			const uint32_t N = std::clamp<uint32_t>(level, 1u, levelMax);
			if (levelMax <= 1) return b.lvl1;
			return b.lvl1 + (static_cast<double>(N) - 1.0) * (b.lvl100 - b.lvl1)
			              / (static_cast<double>(levelMax) - 1.0);
		}

		/// Lit un multiplicateur de sexe ; absent = 1.0 (neutre).
		double SexGet(const std::unordered_map<std::string,double>& m, const char* stat)
		{
			auto it = m.find(stat);
			return it == m.end() ? 1.0 : it->second;
		}

		/// Arrondi au plus proche entier non signé (valeurs <= 0 -> 0).
		uint32_t RoundU(double v) { return v <= 0.0 ? 0u : static_cast<uint32_t>(v + 0.5); }
	}

	std::optional<DerivedStats> ComputeStats(const CharacterStatsTables& t,
	                                          const std::string& factionId,
	                                          const std::string& classId,
	                                          Sex sex, uint32_t level)
	{
		auto fit = t.factions.find(factionId);
		if (fit == t.factions.end()) return std::nullopt;
		auto cit = fit->second.classesById.find(classId);
		if (cit == fit->second.classesById.end()) return std::nullopt;

		const std::string& profile = cit->second.profile;
		const std::string& race    = fit->second.race;

		auto pit = t.classProfiles.find(profile);
		auto rit = t.raceProfiles.find(race);
		auto sit = t.sexProfiles.find(profile);
		if (pit == t.classProfiles.end() || rit == t.raceProfiles.end()) return std::nullopt;

		const ClassProfile& cp = pit->second;
		const RaceProfile&  rp = rit->second;
		static const std::unordered_map<std::string,double> kEmptySex;
		const auto& sx = (sit != t.sexProfiles.end())
			? (sex == Sex::Male ? sit->second.H : sit->second.F)
			: kEmptySex;

		const uint32_t lvlMax = t.levelMax;
		auto B = [&](const char* name) -> double {
			auto it = t.bases.find(name); return it == t.bases.end() ? 0.0 : BaseAt(it->second, level, lvlMax); };

		DerivedStats d;
		d.resourceKey = cit->second.resource;

		d.hp       = RoundU(B("hp")       * cp.hp       * rp.hp       * SexGet(sx, "hp"));
		d.resource = RoundU(B("resource") * cp.resource * rp.resource * SexGet(sx, "resource"));
		d.damage   = RoundU(B("damage")   * cp.damage   * rp.damage   * SexGet(sx, "damage"));
		d.stamina  = RoundU(B("stamina"));

		if (cp.range == 0.0)
		{
			d.range = 0.0f;
			d.accuracy = 0.0f;
		}
		else
		{
			d.range    = static_cast<float>(B("range")    * cp.range    * rp.range    * SexGet(sx, "range"));
			d.accuracy = static_cast<float>(B("accuracy") * cp.accuracy * rp.accuracy * SexGet(sx, "accuracy"));
		}

		const double critRaw = B("crit_rate") * cp.crit_rate * rp.crit_rate; // crit neutre au sexe
		d.critRate = static_cast<float>(std::min(t.critRateCap, critRaw));

		d.critMult = static_cast<float>(t.critMultBase * cp.crit_mult * rp.crit_mult * SexGet(sx, "crit_mult"));

		d.speedWalk   = static_cast<float>(t.speedWalkBase  * cp.speed * rp.speed_walk * SexGet(sx, "speed_walk"));
		d.speedRun    = static_cast<float>(t.speedRunBase   * cp.speed * rp.speed_run  * SexGet(sx, "speed_run"));
		d.speedSprint = static_cast<float>(t.speedSprintBase* cp.speed * rp.speed_run  * SexGet(sx, "speed_run"));

		const double perceptionBase = t.perceptionLvl1 + t.perceptionPerLevel * (static_cast<double>(std::max(1u, level)) - 1.0);
		d.perception = static_cast<float>(perceptionBase * cp.perception * rp.perception * SexGet(sx, "perception"));

		const double stealthDiv = cp.stealth * rp.stealth * SexGet(sx, "stealth");
		d.stealth = static_cast<float>(stealthDiv > 0.0 ? (perceptionBase / 2.0) / stealthDiv : 0.0);

		return d;
	}
}
