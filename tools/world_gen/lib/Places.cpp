// tools/world_gen — lieux PROCÉDURAUX d'une cellule (enrichissement).
//
// Une ville du lore occupe moins d'un km² sur les 100 km² d'une cellule. Pour
// que la nature ne soit pas vide, chaque cellule générée reçoit, selon son
// biome dominant, des hameaux, fermes isolées, camps de bûcherons, repaires
// de bandits, ruines et mines. Tirage DÉTERMINISTE (graine + indices de
// cellule) ; chaque lieu reste à plus de 700 m des bords (il ne déborde jamais
// sur une cellule voisine) et à distance des villes, rivières, lacs et routes.
// Les chemins qui les relient au réseau sont tracés par ApplyRoads.

#include "WorldGenContext.h"
#include "Random.h"

#include <algorithm>
#include <cmath>

namespace tools::world_gen
{
	namespace
	{
		struct PlaceQuota { const char* type; int count; };

		/// Répartition des lieux selon le code de biome dominant de la cellule.
		std::vector<PlaceQuota> QuotasFor(char code)
		{
			switch (code)
			{
			case '.': case 'r': case 'o': case '_': case ',':
				return { { "hameau", 2 }, { "ferme", 4 }, { "ruine", 1 }, { "bandits", 1 } };
			case 'T': case 'R': case 'L': case 't':
				return { { "hameau", 1 }, { "bucherons", 2 }, { "ferme", 1 }, { "bandits", 1 }, { "ruine", 1 } };
			case '~': case ':': case 'l':
				return { { "hameau", 1 }, { "ruine", 2 }, { "bandits", 1 } };
			case '^': case 'M': case '*':
				return { { "mine", 2 }, { "ruine", 1 }, { "bandits", 1 } };
			case 'V':
				return { { "ruine", 2 }, { "bandits", 1 } };
			case '%': case '&':
				return { { "hameau", 1 }, { "ruine", 1 }, { "bandits", 1 } };
			default:
				return {};
			}
		}

		/// Nom propre pseudo-médiéval tiré de syllabes (déterministe).
		std::string ProperName(Rng& rng)
		{
			static const char* a[] = { "Bra", "Cor", "Dun", "El", "Fen", "Gal", "Hol", "Ker", "Lan", "Mor", "Ner", "Ost",
				"Pel", "Quen", "Ros", "Sal", "Tor", "Val", "Wen", "Ys", "Ar", "Bel", "Cal", "Dra" };
			static const char* b[] = { "bourg", "dale", "mont", "val", "fort", "lin", "mar", "ric", "sel", "teuil",
				"venne", "court", "brun", "hal", "noir", "vel" };
			return std::string(a[rng.Below(24)]) + b[rng.Below(16)];
		}

		std::string DisplayName(const std::string& type, const std::string& root)
		{
			if (type == "hameau") return "Hameau de " + root;
			if (type == "ferme") return "Ferme " + root;
			if (type == "bucherons") return "Camp de bucherons de " + root;
			if (type == "bandits") return "Repaire de " + root;
			if (type == "ruine") return "Ruines de " + root;
			if (type == "mine") return "Mine de " + root;
			return root;
		}
	}

	namespace
	{
		/// Étangs et mares de la cellule (hors déserts, montagnes, volcans) :
		/// eau calme intérieure, loin des bords, des lieux et des routes.
		void BuildPonds(World& w)
		{
			const WorldMaster& m = w.master;
			const double sea = m.scale.seaLevelM;
			const char code = LoreCode(m, w.cell.n, w.cell.e);
			int target = 0;
			if (code == '.' || code == 'r' || code == 'T' || code == 'R' || code == 't' || code == 'L') target = 5;
			else if (code == '%' || code == '&') target = 8;
			else if (code == ':' || code == '^' || code == 'o') target = 2;
			Rng rng(Hash(m.seed, static_cast<uint64_t>(w.cell.n + 1000), static_cast<uint64_t>(w.cell.e + 1000), 6161));
			int made = 0;
			for (int attempt = 0; attempt < 200 && made < target; ++attempt)
			{
				const double R = rng.Range(25.0, 85.0);
				const Vec2 c{ w.terrainOriginX + 400.0 + rng.Next() * (w.terrainSizeM - 800.0),
				              w.terrainOriginZ + 400.0 + rng.Next() * (w.terrainSizeM - 800.0) };
				bool ok = true;
				for (const CityDef& city : w.cities)
					if (std::hypot(c.x - city.center.x, c.z - city.center.z) < city.radiusM + R + 300.0) ok = false;
				for (const Lake& l : w.lakes)
					if (std::hypot(c.x - l.center.x, c.z - l.center.z) < l.radiusM + R + 200.0) ok = false;
				for (const Road& r : w.roads)
					for (size_t k = 0; ok && k < r.points.size(); k += 6)
						if (std::hypot(c.x - r.points[k].x, c.z - r.points[k].z) < R + 50.0) ok = false;
				for (const River& r : w.rivers)
					for (size_t k = 0; ok && k < r.points.size(); k += 6)
						if (std::hypot(c.x - r.points[k].x, c.z - r.points[k].z) < R + 80.0) ok = false;
				if (!ok) continue;
				// Cuvette naturelle : niveau = moyenne du relief - 0,8 m ; refusée sur pente.
				double sum = 0.0, cnt = 0.0, lo = 1e9, hi = -1e9;
				for (double dz = -R - 20.0; dz <= R + 20.0; dz += 10.0)
					for (double dx = -R - 20.0; dx <= R + 20.0; dx += 10.0)
					{
						if (dx * dx + dz * dz > (R + 20.0) * (R + 20.0)) continue;
						const double h = MacroHeight(m, c.x + dx, c.z + dz);
						sum += h; cnt += 1.0; lo = std::min(lo, h); hi = std::max(hi, h);
					}
				if (cnt <= 0.0 || hi - lo > 9.0) continue;
				Lake pond;
				pond.name = w.cellName + "_etang_" + std::to_string(made);
				pond.center = c;
				pond.radiusM = R;
				pond.waterY = std::max(sea + 0.5, sum / cnt - 0.8);
				const int verts = std::clamp(static_cast<int>(R / 3.0), 20, 48);
				for (int k = 0; k < verts; ++k)
				{
					const double a = 6.283185307179586 * k / verts;
					const double rr = R * (1.0 + 0.12 * Fbm(std::cos(a) * 3.0, std::sin(a) * 3.0, 1.0, 2, Hash(m.seed, HashString(pond.name.c_str()))));
					pond.polygon.push_back({ c.x + std::cos(a) * rr, c.z + std::sin(a) * rr });
				}
				w.lakes.push_back(std::move(pond));
				++made;
			}
		}
	}

	void BuildPlaces(World& w)
	{
		const WorldMaster& m = w.master;
		const double sea = m.scale.seaLevelM;
		const char code = LoreCode(m, w.cell.n, w.cell.e);
		const std::vector<PlaceQuota> quotas = QuotasFor(code);
		if (quotas.empty()) { BuildPonds(w); return; }
		Rng rng(Hash(m.seed, static_cast<uint64_t>(w.cell.n + 1000), static_cast<uint64_t>(w.cell.e + 1000), 8080));

		// Peuple des lieux : celui de la ville du lore la plus proche (≤ 4 cellules).
		std::string race = "humains";
		{
			const engine::world::cells::CellPoint ctr = engine::world::cells::CellCenter(w.cell);
			double bd = 4.0 * engine::world::cells::kCellSizeMeters;
			for (const CityDef& c : w.g->cities)
			{
				const double d = std::hypot(c.center.x - ctr.x, c.center.z - ctr.z);
				if (d < bd && c.race != "neutre") { bd = d; race = c.race; }
			}
		}

		auto farFromEverything = [&](Vec2 p, double R) {
			for (const CityDef& c : w.cities)
				if (std::hypot(p.x - c.center.x, p.z - c.center.z) < c.radiusM + R + 900.0) return false;
			for (const Lake& l : w.lakes)
				if (std::hypot(p.x - l.center.x, p.z - l.center.z) < l.radiusM + R + 150.0) return false;
			for (const River& r : w.rivers)
				for (size_t k = 0; k < r.points.size(); k += 4)
					if (std::hypot(p.x - r.points[k].x, p.z - r.points[k].z) < R + 120.0) return false;
			for (const Road& r : w.roads)
				for (size_t k = 0; k < r.points.size(); k += 4)
					if (std::hypot(p.x - r.points[k].x, p.z - r.points[k].z) < R + 40.0) return false;
			return true;
		};
		auto roadDistance = [&](Vec2 p) {
			double d = 1e18;
			for (const Road& r : w.roads)
				for (size_t k = 0; k < r.points.size(); k += 8)
					d = std::min(d, std::hypot(p.x - r.points[k].x, p.z - r.points[k].z));
			return d;
		};

		int index = 0;
		for (const PlaceQuota& q : quotas)
		{
			const std::string type = q.type;
			for (int k = 0; k < q.count; ++k)
			{
				CityDef c;
				c.procedural = type;
				c.kind = (type == "bucherons" || type == "bandits" || type == "mine") ? "camp" : "hameau";
				c.state = (type == "ruine") ? "ruine" : ((c.kind == "camp") ? "camp" : "intact");
				c.race = (type == "bandits") ? "neutre" : (type == "mine" && race != "orcs" && race != "elfes" ? "nains" : race);
				c.radiusM = (type == "ferme") ? 26.0 : (type == "mine" ? 34.0 : (c.kind == "camp" ? 55.0 : 45.0));
				c.n = w.cell.n; c.e = w.cell.e;
				bool placed = false;
				for (int attempt = 0; attempt < 400 && !placed; ++attempt)
				{
					const Vec2 p{ w.terrainOriginX + 700.0 + rng.Next() * (w.terrainSizeM - 1400.0),
					              w.terrainOriginZ + 700.0 + rng.Next() * (w.terrainSizeM - 1400.0) };
					const double h0 = MacroHeight(m, p.x, p.z);
					if (h0 < sea + 6.0) continue;
					// Les mines s'accrochent aux pentes, le reste cherche du plat.
					double worst = 0.0;
					const double R = c.radiusM + 30.0;
					for (int s = 0; s < 8; ++s)
					{
						const double a = s * 0.7853981633974483;
						worst = std::max(worst, std::fabs(MacroHeight(m, p.x + std::cos(a) * R, p.z + std::sin(a) * R) - h0));
					}
					const double slope = std::atan(worst / R) * 57.29577951308232;
					if (slope > (type == "mine" ? 26.0 : 12.0)) continue;
					if (!farFromEverything(p, c.radiusM)) continue;
					// Fermes et hameaux près d'une route ; repaires de bandits à l'écart.
					const double rd = roadDistance(p);
					if ((type == "hameau" || type == "ferme") && rd < 1e17 && rd > 2500.0 && attempt < 300) continue;
					if (type == "bandits" && rd < 900.0 && attempt < 300) continue;
					const char b = MacroBiome(m, p.x, p.z);
					if (b == ' ' || b == '-') continue;
					c.center = p;
					placed = true;
				}
				if (!placed) continue;
				const std::string root = ProperName(rng);
				c.id = w.cellName + "_" + type + "_" + std::to_string(index++);
				c.name = DisplayName(type, root);
				c.faction = (type == "bandits") ? "bandits" : "neutre";
				c.description = "Lieu genere (" + type + ") dans la cellule " + w.cellName + ".";
				c.source = "[GENERE] world_gen";
				w.cities.push_back(std::move(c));
				++w.report.proceduralPlaces;
			}
		}
		BuildPonds(w); // après les lieux : les étangs les évitent
	}
}
