// Lignes maritimes et bateaux (voir SeaLines.h).

#include "src/shardd/world/SeaLines.h"

#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace engine::server
{
	namespace cells = engine::world::cells;

	namespace
	{
		constexpr double kPi = 3.14159265358979323846;

		/// Angle ramené dans [-π, π].
		double Wrap(double a) { return std::remainder(a, 2.0 * kPi); }
		/// Interpolation d'angle par le plus court chemin (t dans [0, 1]).
		double LerpAngle(double a, double b, double t) { return Wrap(a + Wrap(b - a) * t); }
		/// Cap d'un déplacement (dx, dz) : 0 = +Z, convention de Mat4::RotateY.
		double Heading(double dx, double dz) { return std::atan2(dx, dz); }
	}

	bool SeaLines::AddFromText(std::string_view json, std::string& outError)
	{
		engine::core::Config c;
		if (!c.LoadFromString(json)) { outError = "sea_lines: JSON invalide"; return false; }
		const int64_t schema = c.GetInt("schema_version", 0);
		if (schema != 1 && schema != 2) { outError = "sea_lines: schema_version 1 ou 2 attendu"; return false; }
		const std::string world = c.GetString("world", "");
		const std::optional<cells::CellMap> map = cells::CellMapFromWorldId(world);
		if (!map) { outError = "sea_lines: monde inconnu '" + world + "'"; return false; }
		const double seaLevel = c.GetDouble("sea_level_m", 40.0);
		std::vector<SeaLine> added;
		for (int li = 0; li < 256; ++li)
		{
			const std::string b = std::format("lines[{}].", li);
			if (!c.Has(b + "id")) break;
			SeaLine line;
			line.id = c.GetString(b + "id", "");
			line.name = c.GetString(b + "name", line.id);
			line.map = *map;
			line.loop = c.GetBool(b + "loop", false);
			line.priceGoldPerLeg = static_cast<uint32_t>(std::clamp<int64_t>(c.GetInt(b + "price_gold_per_leg", 10), 0, 100000));
			line.seaLevelM = std::isfinite(seaLevel) ? seaLevel : 40.0;
			bool docks = schema == 2;
			for (int k = 0; k < 64; ++k)
			{
				const std::string s = std::format("{}stops[{}].", b, k);
				if (!c.Has(s + "city")) break;
				if (!c.Has(s + "x") || !c.Has(s + "z")) continue; // escale sans ponton
				SeaLineStop st;
				st.cityId = c.GetString(s + "city", "");
				st.name = c.GetString(s + "name", st.cityId);
				st.x = c.GetDouble(s + "x", 0.0);
				st.y = c.GetDouble(s + "y", 0.0);
				st.z = c.GetDouble(s + "z", 0.0);
				st.yawDeg = static_cast<float>(c.GetDouble(s + "yaw_deg", 0.0));
				docks = docks && c.Has(s + "dock_x") && c.Has(s + "dock_z");
				st.dockX = c.GetDouble(s + "dock_x", st.x);
				st.dockZ = c.GetDouble(s + "dock_z", st.z);
				st.dockYawDeg = static_cast<float>(c.GetDouble(s + "dock_yaw_deg", 0.0));
				if (std::isfinite(st.x) && std::isfinite(st.y) && std::isfinite(st.z) && std::isfinite(st.dockX) && std::isfinite(st.dockZ))
					line.stops.push_back(std::move(st));
			}
			if (line.id.empty() || line.stops.size() < 2)
				continue;
			// Schéma 2 : étapes (sens aller) et bateaux. Une ligne n'a de bateau que si toutes
			// ses étapes sont tracées.
			if (docks)
			{
				for (int k = 0; k < 64; ++k)
				{
					const std::string l = std::format("{}legs[{}].", b, k);
					if (!c.Has(l + "from")) break;
					SeaLineLeg leg;
					leg.from = static_cast<size_t>(std::max<int64_t>(0, c.GetInt(l + "from", 0)));
					leg.to = static_cast<size_t>(std::max<int64_t>(0, c.GetInt(l + "to", 0)));
					for (int p = 0; p < 4096; ++p)
					{
						const std::string q = std::format("{}points[{}]", l, p);
						if (!c.Has(q + "[0]")) break;
						const double x = c.GetDouble(q + "[0]", 0.0), z = c.GetDouble(q + "[1]", 0.0);
						if (!std::isfinite(x) || !std::isfinite(z)) { leg.xs.clear(); break; }
						leg.xs.push_back(x);
						leg.zs.push_back(z);
					}
					if (leg.xs.size() >= 2 && leg.from < line.stops.size() && leg.to < line.stops.size() && leg.from != leg.to)
						line.legs.push_back(std::move(leg));
				}
				const size_t n = line.stops.size();
				const size_t want = line.loop && n > 2 ? n : n - 1;
				bool complete = true;
				for (size_t k = 0; k < want; ++k)
				{
					const size_t a = k, z = (k + 1) % n;
					complete = complete && std::any_of(line.legs.begin(), line.legs.end(), [&](const SeaLineLeg& g) {
						return (g.from == a && g.to == z) || (g.from == z && g.to == a);
					});
				}
				if (complete)
				{
					line.boats = static_cast<uint32_t>(std::clamp<int64_t>(c.GetInt(b + "boats", 2), 0, 4));
					line.stopSeconds = std::clamp(c.GetDouble(b + "stop_seconds", 30.0), 5.0, 600.0);
				}
			}
			added.push_back(std::move(line));
		}
		for (SeaLine& l : added)
		{
			auto it = std::find_if(m_lines.begin(), m_lines.end(), [&](const SeaLine& o) { return o.id == l.id; });
			if (it != m_lines.end()) *it = std::move(l);
			else m_lines.push_back(std::move(l));
		}
		return true;
	}

	bool SeaLines::LoadForCellWorld(const engine::core::Config& config, std::string& outError)
	{
		Clear();
		for (int i = 0; i < cells::kCellMapCount; ++i)
		{
			const std::string rel = "world/" + std::string(cells::WorldIdOf(static_cast<cells::CellMap>(i))) + "/sea_lines.json";
			const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, rel);
			if (text.empty()) continue;
			std::string err;
			if (!AddFromText(text, err))
			{
				Clear();
				outError = err + " (" + rel + ")";
				return false;
			}
		}
		return true;
	}

	const SeaLine* SeaLines::Find(std::string_view id) const
	{
		for (const SeaLine& l : m_lines)
			if (l.id == id) return &l;
		return nullptr;
	}

	// ------------------------------------------------------------------ bateaux

	void BoatFleet::Build(const SeaLines& lines, double speedMps)
	{
		m_cycles.clear();
		m_boats.clear();
		m_speed = std::isfinite(speedMps) ? std::clamp(speedMps, 1.0, 60.0) : 8.0;
		for (const SeaLine& line : lines.All())
		{
			if (line.boats == 0 || line.stops.size() < 2) continue;
			const size_t n = line.stops.size();
			// Ordre des escales du cycle.
			std::vector<size_t> order;
			for (size_t k = 0; k < n; ++k) order.push_back(k);
			if (!line.loop || n == 2)
				for (size_t k = n - 1; k-- > 1;) order.push_back(k);
			Cycle cy;
			cy.line = &line;
			for (size_t v = 0; v < order.size(); ++v)
			{
				Visit vi;
				vi.stop = order[v];
				vi.next = order[(v + 1) % order.size()];
				for (const SeaLineLeg& g : line.legs)
				{
					if (g.from == vi.stop && g.to == vi.next) { vi.xs = g.xs; vi.zs = g.zs; break; }
					if (g.from == vi.next && g.to == vi.stop)
					{
						vi.xs.assign(g.xs.rbegin(), g.xs.rend());
						vi.zs.assign(g.zs.rbegin(), g.zs.rend());
						break;
					}
				}
				if (vi.xs.size() < 2) { cy.visits.clear(); break; }
				vi.cum.assign(vi.xs.size(), 0.0);
				for (size_t p = 1; p < vi.xs.size(); ++p)
					vi.cum[p] = vi.cum[p - 1] + std::hypot(vi.xs[p] - vi.xs[p - 1], vi.zs[p] - vi.zs[p - 1]);
				vi.length = vi.cum.back();
				// Cap à quai à l'arrivée : celui du quai (±π) le plus proche du dernier cap.
				const size_t m = vi.xs.size();
				const double last = Heading(vi.xs[m - 1] - vi.xs[m - 2], vi.zs[m - 1] - vi.zs[m - 2]);
				const double dock = line.stops[vi.next].dockYawDeg * kPi / 180.0;
				vi.arrivalYaw = std::fabs(Wrap(dock - last)) <= kPi * 0.5 ? Wrap(dock) : Wrap(dock + kPi);
				cy.visits.push_back(std::move(vi));
			}
			if (cy.visits.empty()) continue;
			for (const Visit& vi : cy.visits) cy.period += line.stopSeconds + vi.length / m_speed;
			if (!(cy.period > 0.0)) continue;
			m_cycles.push_back(std::move(cy));
			const size_t ci = m_cycles.size() - 1;
			for (uint32_t r = 0; r < line.boats; ++r)
				m_boats.push_back(Boat{ ci, r, m_cycles[ci].period * r / line.boats });
		}
	}

	BoatPose BoatFleet::Evaluate(size_t boat, double t) const
	{
		BoatPose p;
		if (boat >= m_boats.size()) return p;
		const Boat& b = m_boats[boat];
		const Cycle& cy = m_cycles[b.cycle];
		const double stopS = cy.line->stopSeconds;
		p.y = cy.line->seaLevelM;
		const double u = (std::isfinite(t) ? t : 0.0) + b.offset;
		const double laps = std::floor(u / cy.period);
		double phase = u - laps * cy.period;
		const int64_t base = static_cast<int64_t>(laps) * static_cast<int64_t>(cy.visits.size());
		for (size_t v = 0; v < cy.visits.size(); ++v)
		{
			const Visit& vi = cy.visits[v];
			const Visit& prev = cy.visits[(v + cy.visits.size() - 1) % cy.visits.size()];
			const double sail = vi.length / m_speed;
			if (phase < stopS)
			{
				// À quai, cap d'arrivée de l'étape précédente.
				const SeaLineStop& st = cy.line->stops[vi.stop];
				p.x = st.dockX; p.z = st.dockZ;
				p.yawRad = static_cast<float>(prev.arrivalYaw);
				p.docked = true;
				p.stop = p.fromStop = vi.stop;
				p.secondsLeft = stopS - phase;
				p.departures = base + static_cast<int64_t>(v);
				return p;
			}
			phase -= stopS;
			if (phase < sail || v + 1 == cy.visits.size())
			{
				const double s = std::clamp(phase * m_speed, 0.0, vi.length);
				size_t i = 0;
				while (i + 2 < vi.cum.size() && vi.cum[i + 1] <= s) ++i;
				const double segLen = vi.cum[i + 1] - vi.cum[i];
				const double f = segLen > 1e-9 ? (s - vi.cum[i]) / segLen : 0.0;
				p.x = vi.xs[i] + (vi.xs[i + 1] - vi.xs[i]) * f;
				p.z = vi.zs[i] + (vi.zs[i + 1] - vi.zs[i]) * f;
				auto segHeading = [&](size_t k) { return Heading(vi.xs[k + 1] - vi.xs[k], vi.zs[k + 1] - vi.zs[k]); };
				double yaw = segHeading(i);
				// Virages aux sommets : demi-virage de part et d'autre (kTurnM / 2 au plus, et
				// la moitié du segment voisin au plus).
				const double r0 = std::min(kTurnM * 0.5, segLen * 0.5);
				if (i > 0 && s - vi.cum[i] < r0)
				{
					const double rr = std::min(r0, (vi.cum[i] - vi.cum[i - 1]) * 0.5);
					if (rr > 1e-6 && s - vi.cum[i] < rr)
						yaw = LerpAngle(segHeading(i - 1), yaw, 0.5 + 0.5 * (s - vi.cum[i]) / rr);
				}
				if (i + 2 < vi.cum.size() && vi.cum[i + 1] - s < r0)
				{
					const double rr = std::min(r0, (vi.cum[i + 2] - vi.cum[i + 1]) * 0.5);
					if (rr > 1e-6 && vi.cum[i + 1] - s < rr)
						yaw = LerpAngle(yaw, segHeading(i + 1), 0.5 - 0.5 * (vi.cum[i + 1] - s) / rr);
				}
				// Sortie et entrée de quai : du cap à quai au cap de route sur kTurnM.
				const double turn = std::min(kTurnM, vi.length * 0.5);
				if (turn > 1e-6 && s < turn) yaw = LerpAngle(prev.arrivalYaw, yaw, s / turn);
				if (turn > 1e-6 && vi.length - s < turn) yaw = LerpAngle(vi.arrivalYaw, yaw, (vi.length - s) / turn);
				p.yawRad = static_cast<float>(yaw);
				p.speed = static_cast<float>(m_speed);
				p.docked = false;
				p.fromStop = vi.stop;
				p.stop = vi.next;
				p.secondsLeft = std::max(0.0, sail - phase);
				p.departures = base + static_cast<int64_t>(v) + 1;
				return p;
			}
			phase -= sail;
		}
		return p;
	}

	void BoatFleet::LocalToWorld(const BoatPose& p, double lx, double lz, double& outX, double& outZ)
	{
		engine::world::boat::LocalToWorld(p.x, p.z, p.yawRad, lx, lz, outX, outZ);
	}

	void BoatFleet::WorldToLocal(const BoatPose& p, double wx, double wz, double& outLx, double& outLz)
	{
		engine::world::boat::WorldToLocal(p.x, p.z, p.yawRad, wx, wz, outLx, outLz);
	}

	bool BoatFleet::ClampToDeck(float& lx, float& lz)
	{
		return engine::world::boat::ClampToDeck(lx, lz);
	}
}
