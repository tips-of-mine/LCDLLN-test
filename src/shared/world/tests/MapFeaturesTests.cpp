// FIX.12 — Tests du format map_features.bin (routes / bâtiments de la carte du monde).

#include "src/shared/world/MapFeatures.h"

#include <cmath>
#include <iostream>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const char* msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
}

int main()
{
	namespace mf = engine::world::mapfeatures;

	// 1. Aller-retour : contenu identique, boîtes englobantes recalculées.
	{
		mf::MapFeatures f;
		mf::MapRoad r;
		r.kind = mf::MapRoadKind::Major;
		r.widthM = 6.5f;
		r.xz = { 100.0f, -50.0f, 120.0f, -40.0f, 90.0f, 10.0f };
		f.roads.push_back(r);
		mf::MapRoad s;
		s.kind = mf::MapRoadKind::Street;
		s.widthM = 3.0f;
		s.xz = { 0.0f, 0.0f, 5.0f, 5.0f };
		f.roads.push_back(s);
		f.buildings.push_back({ 10.0f, 20.0f, 0.5f, 3.0f, 4.0f });

		const std::vector<uint8_t> bytes = mf::Serialize(f);
		mf::MapFeatures g;
		std::string err;
		Check(mf::Deserialize(bytes, g, err), "aller-retour accepte");
		Check(g.roads.size() == 2u && g.buildings.size() == 1u, "aller-retour : tailles");
		if (g.roads.size() == 2u)
		{
			Check(g.roads[0].kind == mf::MapRoadKind::Major && Near(g.roads[0].widthM, 6.5f), "route 0 : type et largeur");
			Check(g.roads[0].xz == r.xz, "route 0 : points");
			Check(Near(g.roads[0].minX, 90.0f) && Near(g.roads[0].maxX, 120.0f), "route 0 : bornes X");
			Check(Near(g.roads[0].minZ, -50.0f) && Near(g.roads[0].maxZ, 10.0f), "route 0 : bornes Z");
			Check(g.roads[1].kind == mf::MapRoadKind::Street, "route 1 : rue");
		}
		if (g.buildings.size() == 1u)
			Check(Near(g.buildings[0].x, 10.0f) && Near(g.buildings[0].halfD, 4.0f) && Near(g.buildings[0].yawRad, 0.5f), "batiment relu");

		// 2. Rejets : tronqué, magic faux, version inconnue.
		std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 3);
		Check(!mf::Deserialize(cut, g, err) && g.Empty(), "fichier tronque refuse (sortie videe)");
		std::vector<uint8_t> bad = bytes;
		bad[0] ^= 0xFFu;
		Check(!mf::Deserialize(bad, g, err), "magic faux refuse");
		std::vector<uint8_t> ver = bytes;
		ver[4] = 9u;
		Check(!mf::Deserialize(ver, g, err), "version inconnue refusee");
		// Nombre de points annoncé énorme : refus sans allocation.
		std::vector<uint8_t> huge = bytes;
		huge[12 + 1 + 4] = 0xFFu; huge[12 + 1 + 5] = 0xFFu; huge[12 + 1 + 6] = 0xFFu; huge[12 + 1 + 7] = 0x7Fu;
		Check(!mf::Deserialize(huge, g, err), "nombre de points aberrant refuse");
		Check(!mf::Deserialize({}, g, err), "fichier vide refuse");
	}

	// 3. Allègement : premier et dernier points gardés, pas ≥ minStep.
	{
		std::vector<float> line;
		for (int i = 0; i <= 100; ++i) { line.push_back(static_cast<float>(i)); line.push_back(0.0f); } // 0..100 m, pas 1 m
		const std::vector<float> d = mf::DecimatePolyline(line, 10.0f);
		Check(d.size() == 22u, "allegement : 11 points (0, 10, …, 100)");
		Check(Near(d.front(), 0.0f) && Near(d[d.size() - 2], 100.0f), "allegement : extremites gardees");
		// Dernier point trop proche : il remplace le précédent (pas de micro-segment).
		std::vector<float> l2 = { 0.0f, 0.0f, 10.0f, 0.0f, 12.0f, 0.0f };
		const std::vector<float> d2 = mf::DecimatePolyline(l2, 10.0f);
		Check(d2.size() == 4u && Near(d2[2], 12.0f), "allegement : dernier point proche fusionne");
		// Deux points proches seulement : les deux restent (le tracé existe).
		const std::vector<float> d3 = mf::DecimatePolyline({ 0.0f, 0.0f, 1.0f, 0.0f }, 10.0f);
		Check(d3.size() == 4u, "allegement : segment court conserve");
		Check(mf::DecimatePolyline({}, 10.0f).empty(), "allegement : vide");
	}

	// 4. Coins d'un bâtiment : sans rotation, rectangle aligné ; à 90°, axes échangés
	//    selon la convention de tools/world_gen (dx = c·lx + s·lz ; dz = -s·lx + c·lz).
	{
		float c[8];
		mf::BuildingCorners({ 100.0f, 200.0f, 0.0f, 3.0f, 2.0f }, c);
		Check(Near(c[0], 97.0f) && Near(c[1], 198.0f) && Near(c[4], 103.0f) && Near(c[5], 202.0f), "coins sans rotation");
		mf::BuildingCorners({ 0.0f, 0.0f, 1.57079633f, 3.0f, 2.0f }, c);
		// Coin local (-3, -2) : dx = 0·(-3) + 1·(-2) = -2 ; dz = -1·(-3) + 0 = 3.
		Check(Near(c[0], -2.0f) && Near(c[1], 3.0f), "coins a 90 degres");
	}

	// 5. FIX.15 — plans d'eau (v2) : aller-retour, boîtes, centre de lac ; un
	//    fichier v1 (sans plans d'eau) se lit toujours.
	{
		mf::MapFeatures f;
		mf::MapWater river;
		river.kind = mf::MapWaterKind::River;
		river.widthM = 10.0f;
		river.xz = { 0.0f, 0.0f, 100.0f, 0.0f };
		f.waters.push_back(river);
		mf::MapWater lake;
		lake.kind = mf::MapWaterKind::Lake;
		lake.xz = { 10.0f, 10.0f, 30.0f, 10.0f, 30.0f, 30.0f, 10.0f, 30.0f };
		f.waters.push_back(lake);
		Check(!f.Empty(), "plans d'eau seuls : non vide");
		const std::vector<uint8_t> bytes = mf::Serialize(f);
		mf::MapFeatures g;
		std::string err;
		Check(mf::Deserialize(bytes, g, err) && g.waters.size() == 2u, "aller-retour des plans d'eau");
		if (g.waters.size() == 2u)
		{
			Check(g.waters[0].kind == mf::MapWaterKind::River && Near(g.waters[0].widthM, 10.0f), "riviere relue");
			Check(Near(g.waters[0].minZ, -5.0f) && Near(g.waters[0].maxZ, 5.0f), "boite de riviere elargie de la demi-largeur");
			Check(g.waters[1].kind == mf::MapWaterKind::Lake && g.waters[1].PointCount() == 4u, "lac relu");
			float cx = 0.0f, cz = 0.0f;
			mf::LakeCenter(g.waters[1], cx, cz);
			Check(Near(cx, 20.0f) && Near(cz, 20.0f), "centre du lac");
		}
		// Fichier v1 : même début, version 1, sans le bloc des plans d'eau.
		const std::vector<uint8_t> v2Empty = mf::Serialize(mf::MapFeatures{});
		std::vector<uint8_t> v1(v2Empty.begin(), v2Empty.end() - 4);
		v1[4] = 1u;
		Check(mf::Deserialize(v1, g, err) && g.waters.empty(), "fichier v1 toujours lisible");
	}

	if (s_failCount == 0)
		std::cout << "[OK] map_features_tests" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
