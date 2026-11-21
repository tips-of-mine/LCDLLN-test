// src/client/world/water/WaterMeshBuilder.cpp
#include "src/client/world/water/WaterMeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace engine::world::water
{
	namespace
	{
		/// Point 2D (XZ) en double, exprimé dans le repère local du lac.
		struct P2 { double x, z; };

		/// Aire signée 2D (XZ) en double. Positive si CCW.
		/// v1.0.133 : calcul en double sur des coordonnées locales. En float, sur un
		/// lac situé à des dizaines de km de l'origine de rendu, les produits x·z
		/// (~10^10) noyaient l'aire réelle : le signe s'inversait, le polygone était
		/// retourné à tort et le découpage échouait (lac invisible en jeu).
		double SignedArea2D(const std::vector<P2>& poly)
		{
			double a = 0.0;
			for (size_t i = 0; i < poly.size(); ++i)
			{
				const P2& cur = poly[i];
				const P2& next = poly[(i + 1) % poly.size()];
				a += cur.x * next.z - next.x * cur.z;
			}
			return a * 0.5;
		}

		/// Produit vectoriel 2D (b - a) × (c - b) : > 0 si le coin (a, b, c) tourne à gauche (CCW).
		double Cross(const P2& a, const P2& b, const P2& c)
		{
			return (b.x - a.x) * (c.z - b.z) - (b.z - a.z) * (c.x - b.x);
		}

		/// True si le point p est dans le triangle (a, b, c), bords compris.
		bool PointInTriangle(const P2& p, const P2& a, const P2& b, const P2& c)
		{
			const double d1 = (p.x - b.x) * (a.z - b.z) - (a.x - b.x) * (p.z - b.z);
			const double d2 = (p.x - c.x) * (b.z - c.z) - (b.x - c.x) * (p.z - c.z);
			const double d3 = (p.x - a.x) * (c.z - a.z) - (c.x - a.x) * (p.z - a.z);
			const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
			const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
			return !(hasNeg && hasPos);
		}

		/// Ear clipping : true si un sommet restant (autre que prev/curr/next) est dans l'oreille.
		bool TriangleContainsAnyOther(const std::vector<uint32_t>& remaining, size_t earIdx, const std::vector<P2>& poly)
		{
			const size_t n = remaining.size();
			const uint32_t prev = remaining[(earIdx + n - 1) % n];
			const uint32_t curr = remaining[earIdx];
			const uint32_t next = remaining[(earIdx + 1) % n];
			for (size_t i = 0; i < n; ++i)
			{
				const uint32_t v = remaining[i];
				if (v == prev || v == curr || v == next) continue;
				if (PointInTriangle(poly[v], poly[prev], poly[curr], poly[next]))
					return true;
			}
			return false;
		}
	}

	bool BuildLakeMesh(const LakeInstance& lake,
		WaterMeshCpu& out, std::string& err)
	{
		out.vertices.clear();
		out.indices.clear();

		if (lake.polygon.size() < 3)
		{
			err = "BuildLakeMesh: polygon needs >= 3 vertices";
			return false;
		}

		// v1.0.133 — repère local en double : origine au premier sommet. Les sommets
		// de sortie gardent leurs positions d'origine (float, repère de rendu).
		const double ox = lake.polygon[0].x, oz = lake.polygon[0].z;
		std::vector<P2> loc;
		std::vector<engine::math::Vec3> src;
		loc.reserve(lake.polygon.size());
		src.reserve(lake.polygon.size());
		// Sommets consécutifs confondus (moins de 1 cm, fermeture dupliquée comprise) retirés.
		for (const auto& v : lake.polygon)
		{
			const P2 q{ static_cast<double>(v.x) - ox, static_cast<double>(v.z) - oz };
			if (!loc.empty() && std::hypot(q.x - loc.back().x, q.z - loc.back().z) < 0.01) continue;
			loc.push_back(q);
			src.push_back(v);
		}
		while (loc.size() > 3 && std::hypot(loc.front().x - loc.back().x, loc.front().z - loc.back().z) < 0.01)
		{
			loc.pop_back();
			src.pop_back();
		}
		// Sommets alignés (aire du coin < 1e-4 m²) retirés : ils bloquent le découpage.
		for (bool removed = true; removed && loc.size() > 3;)
		{
			removed = false;
			for (size_t i = 0; i < loc.size() && loc.size() > 3; ++i)
			{
				const size_t n = loc.size();
				if (std::fabs(Cross(loc[(i + n - 1) % n], loc[i], loc[(i + 1) % n])) < 2e-4)
				{
					loc.erase(loc.begin() + static_cast<std::ptrdiff_t>(i));
					src.erase(src.begin() + static_cast<std::ptrdiff_t>(i));
					removed = true;
					break;
				}
			}
		}
		if (loc.size() < 3)
		{
			err = "BuildLakeMesh: degenerate polygon";
			return false;
		}

		// Force CCW
		if (SignedArea2D(loc) < 0.0)
		{
			std::reverse(loc.begin(), loc.end());
			std::reverse(src.begin(), src.end());
		}

		// Indices restants à découper
		std::vector<uint32_t> remaining(loc.size());
		std::iota(remaining.begin(), remaining.end(), 0u);
		bool clipped = true;
		while (remaining.size() > 3)
		{
			bool foundEar = false;
			for (size_t i = 0; i < remaining.size(); ++i)
			{
				const size_t n = remaining.size();
				const uint32_t prev = remaining[(i + n - 1) % n];
				const uint32_t curr = remaining[i];
				const uint32_t next = remaining[(i + 1) % n];
				if (Cross(loc[prev], loc[curr], loc[next]) <= 0.0) continue;
				if (TriangleContainsAnyOther(remaining, i, loc)) continue;
				out.indices.push_back(prev);
				out.indices.push_back(curr);
				out.indices.push_back(next);
				remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
				foundEar = true;
				break;
			}
			if (!foundEar) { clipped = false; break; }
		}
		if (clipped)
		{
			// Dernier triangle
			out.indices.push_back(remaining[0]);
			out.indices.push_back(remaining[1]);
			out.indices.push_back(remaining[2]);
		}

		// Vertices : positions XZ du polygone, Y = waterLevelY
		out.vertices.reserve(src.size() + 1);
		for (const auto& p : src)
			out.vertices.push_back({ engine::math::Vec3{ p.x, lake.waterLevelY, p.z } });

		if (!clipped)
		{
			// Polygone non simple malgré tout : éventail depuis le centre (exact pour
			// les lacs en étoile du générateur) plutôt que d'abandonner le lac.
			// L'appelant reçoit un message, mais le maillage est utilisable.
			out.indices.clear();
			double cx = 0.0, cz = 0.0;
			for (const P2& q : loc) { cx += q.x; cz += q.z; }
			cx /= static_cast<double>(loc.size());
			cz /= static_cast<double>(loc.size());
			const uint32_t centre = static_cast<uint32_t>(out.vertices.size());
			out.vertices.push_back({ engine::math::Vec3{ static_cast<float>(cx + ox), lake.waterLevelY, static_cast<float>(cz + oz) } });
			for (uint32_t i = 0; i < static_cast<uint32_t>(src.size()); ++i)
			{
				out.indices.push_back(centre);
				out.indices.push_back(i);
				out.indices.push_back((i + 1) % static_cast<uint32_t>(src.size()));
			}
			err = "BuildLakeMesh: ear-clipping failed, fan fallback used";
		}
		return true;
	}

	bool BuildRiverMesh(const RiverInstance& river,
		WaterMeshCpu& out, std::string& err)
	{
		out.vertices.clear();
		out.indices.clear();

		if (river.nodes.size() < 2)
		{
			err = "BuildRiverMesh: river needs >= 2 nodes";
			return false;
		}

		out.vertices.reserve(2 * river.nodes.size());
		out.indices.reserve(6 * (river.nodes.size() - 1));

		for (size_t i = 0; i < river.nodes.size(); ++i)
		{
			engine::math::Vec3 tangent;
			if (i == 0)
				tangent = river.nodes[1].position - river.nodes[0].position;
			else if (i == river.nodes.size() - 1)
				tangent = river.nodes[i].position - river.nodes[i - 1].position;
			else
				tangent = river.nodes[i + 1].position - river.nodes[i - 1].position;

			const float tlen = std::sqrt(tangent.x * tangent.x + tangent.z * tangent.z);
			const float perpX = (tlen > 0.0f) ? (-tangent.z / tlen) : 1.0f;
			const float perpZ = (tlen > 0.0f) ? ( tangent.x / tlen) : 0.0f;
			const float halfW = river.nodes[i].widthMeters * 0.5f;

			const auto& n = river.nodes[i];
			out.vertices.push_back({ engine::math::Vec3{
				n.position.x + perpX * halfW, n.position.y, n.position.z + perpZ * halfW } });
			out.vertices.push_back({ engine::math::Vec3{
				n.position.x - perpX * halfW, n.position.y, n.position.z - perpZ * halfW } });
		}

		for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(river.nodes.size()); ++i)
		{
			const uint32_t a = i * 2 + 0;
			const uint32_t b = i * 2 + 1;
			const uint32_t c = (i + 1) * 2 + 0;
			const uint32_t d = (i + 1) * 2 + 1;
			out.indices.push_back(a); out.indices.push_back(c); out.indices.push_back(d);
			out.indices.push_back(a); out.indices.push_back(d); out.indices.push_back(b);
		}
		return true;
	}

	std::vector<engine::math::Vec3> ComputeFlowDirections(const RiverInstance& river)
	{
		std::vector<engine::math::Vec3> flows;
		if (river.nodes.size() < 2) return flows;
		flows.reserve(river.nodes.size() - 1);
		for (size_t i = 0; i + 1 < river.nodes.size(); ++i)
		{
			const float dx = river.nodes[i + 1].position.x - river.nodes[i].position.x;
			const float dz = river.nodes[i + 1].position.z - river.nodes[i].position.z;
			const float len = std::sqrt(dx * dx + dz * dz);
			if (len > 0.0f)
				flows.push_back({ dx / len, 0.0f, dz / len });
			else
				flows.push_back({ 1.0f, 0.0f, 0.0f });
		}
		return flows;
	}
}
