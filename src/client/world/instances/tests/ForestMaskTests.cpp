// FORET.1 — forêt de remplissage côté client : format terrain_forest.frms
// (round-trip, rejets), densité bilinéaire, génération déterministe des arbres
// et du sous-bois autour d'un point (densité « un arbre tous les 4 à 5 m » en
// pleine forêt, rien hors forêt, aucun saut quand le centre se déplace).

#include "src/shared/world/instances/ForestMask.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <tuple>
#include <vector>

namespace
{
	int g_failures = 0;

	void Check(bool ok, const char* what)
	{
		std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
		if (!ok) ++g_failures;
	}

	using engine::world::instances::ForestMask;

	/// Carte carrée de \p sizeM m, coin min (\p ox, \p oz), densité uniforme \p v.
	ForestMask Uniform(double ox, double oz, double sizeM, uint32_t res, uint8_t v)
	{
		ForestMask m;
		m.width = m.height = res;
		m.originX = ox; m.originZ = oz; m.sizeM = sizeM;
		m.density.assign(static_cast<size_t>(res) * res, v);
		return m;
	}

	void TestFormat()
	{
		using namespace engine::world::instances;
		ForestMask a = Uniform(-5000.0, 15000.0, 10000.0, 16, 0);
		for (size_t k = 0; k < a.density.size(); ++k) a.density[k] = static_cast<uint8_t>(k * 7u);
		const std::vector<uint8_t> bytes = SaveForestMask(a);
		ForestMask b; std::string err;
		Check(LoadForestMask(bytes, b, err), "format : relecture");
		Check(b.width == 16 && b.height == 16 && b.originX == a.originX && b.originZ == a.originZ && b.sizeM == a.sizeM
			&& b.density == a.density, "format : round-trip a l'identique");
		std::vector<uint8_t> bad = bytes; bad[0] ^= 0xFFu;
		Check(!LoadForestMask(bad, b, err), "format : magic invalide rejete");
		std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 3);
		Check(!LoadForestMask(cut, b, err), "format : fichier tronque rejete");
	}

	void TestDensity()
	{
		ForestMask m = Uniform(0.0, 0.0, 100.0, 10, 0);
		for (uint32_t j = 0; j < 10; ++j)
			for (uint32_t i = 5; i < 10; ++i) m.density[j * 10 + i] = 255;
		Check(m.DensityAt(5.0, 50.0) == 0.0f && m.DensityAt(95.0, 50.0) == 1.0f, "densite : zones vide et pleine");
		const float mid = m.DensityAt(50.0, 50.0);
		Check(mid > 0.4f && mid < 0.6f, "densite : interpolation bilineaire a la frontiere");
		Check(m.DensityAt(-1.0, 50.0) == 0.0f && m.DensityAt(100.0, 50.0) == 0.0f, "densite : nulle hors emprise (bord max exclu)");
	}

	void TestFiller()
	{
		using namespace engine::world::instances;
		const ForestMask full = Uniform(0.0, 0.0, 2000.0, 64, 255);
		ForestFillerParams p;
		p.radiusM = 100.0;
		ScatterFile a;
		GenerateForestFiller(full, 1000.0, 1000.0, p, {}, a);
		std::vector<const ScatterInstance*> trees;
		size_t under = 0;
		bool flagsOk = true;
		for (const ScatterInstance& in : a.instances)
		{
			const bool tree = in.cullClass == static_cast<uint8_t>(ScatterCullClass::Large);
			if (tree)
			{
				trees.push_back(&in);
				flagsOk = flagsOk && (in.flags & kScatterFlagSolid) && (in.flags & kScatterFlagOccluder) && in.collisionRadius > 0.2f;
			}
			else
			{
				++under;
				flagsOk = flagsOk && in.flags == 0;
			}
			flagsOk = flagsOk && in.meshIndex < a.meshPaths.size();
		}
		// Surface du disque / arbres = surface moyenne par arbre ; pleine forêt
		// « un arbre tous les 4 à 5 m » = 16 à 25 m² par arbre.
		const double areaPerTree = 3.14159265 * p.radiusM * p.radiusM / static_cast<double>(std::max<size_t>(1, trees.size()));
		std::printf("  pleine foret : %zu arbres, %zu plantes de sous-bois, %.1f m2 par arbre\n", trees.size(), under, areaPerTree);
		Check(areaPerTree >= 16.0 && areaPerTree <= 25.0, "remplissage : un arbre tous les 4 a 5 m en pleine foret");
		Check(under > trees.size() / 2, "remplissage : sous-bois fourni");
		Check(flagsOk, "remplissage : arbres solides et occulteurs, sous-bois traversable, meshes valides");
		// Écart minimal entre arbres voisins (jitter borné à 70 % de la case).
		double minD = 1e9;
		for (size_t i = 0; i < trees.size(); ++i)
			for (size_t j = i + 1; j < trees.size(); ++j)
			{
				const double dx = trees[i]->x - trees[j]->x, dz = trees[i]->z - trees[j]->z;
				minD = std::min(minD, std::sqrt(dx * dx + dz * dz));
			}
		Check(minD >= 0.3 * p.treeStepM - 1e-3, "remplissage : deux troncs jamais colles");

		// Déterminisme : un centre décalé de 30 m redonne exactement les mêmes
		// instances dans la zone commune (pas de saut quand le joueur avance).
		ScatterFile b;
		GenerateForestFiller(full, 1030.0, 1000.0, p, {}, b);
		std::map<std::tuple<float, float>, std::string> inB;
		for (const ScatterInstance& in : b.instances) inB[{ in.x, in.z }] = b.meshPaths[in.meshIndex];
		bool same = true;
		size_t common = 0;
		for (const ScatterInstance& in : a.instances)
		{
			const double dA = std::hypot(in.x - 1000.0, in.z - 1000.0), dB = std::hypot(in.x - 1030.0, in.z - 1000.0);
			// v1.0.133 : couvre-sol (classe Small) comparé dans son propre rayon.
			const double lim = in.cullClass == static_cast<uint8_t>(ScatterCullClass::Small) ? p.groundRadiusM - 1.0 : 99.0;
			if (dA > lim || dB > lim) continue;
			++common;
			const auto it = inB.find({ in.x, in.z });
			same = same && it != inB.end() && it->second == a.meshPaths[in.meshIndex];
		}
		Check(common > 100 && same, "remplissage : deterministe (memes arbres quel que soit le centre)");

		// Hors forêt : rien ; lisière (densité ≈ 0,38) : nettement moins dense.
		ScatterFile none;
		GenerateForestFiller(Uniform(0.0, 0.0, 2000.0, 64, 0), 1000.0, 1000.0, p, {}, none);
		Check(none.instances.empty(), "remplissage : rien hors foret");
		ScatterFile edge;
		GenerateForestFiller(Uniform(0.0, 0.0, 2000.0, 64, 97), 1000.0, 1000.0, p, {}, edge);
		size_t edgeTrees = 0;
		for (const ScatterInstance& in : edge.instances)
			if (in.cullClass == static_cast<uint8_t>(ScatterCullClass::Large)) ++edgeTrees;
		Check(edgeTrees > trees.size() / 4 && edgeTrees < trees.size() / 2, "remplissage : lisiere moins dense (la vue passe)");

		// v1.0.133 — prairie : aucun arbre, couvre-sol fourni (herbes, fleurs), buissons rares,
		// rien au-delà du rayon du couvre-sol hormis les buissons.
		ScatterFile meadow;
		GenerateForestFiller(Uniform(0.0, 0.0, 2000.0, 64, kForestMaskMeadowValue), 1000.0, 1000.0, p, {}, meadow);
		size_t meadowTrees = 0, meadowGround = 0, meadowBush = 0;
		bool groundInRadius = true;
		for (const ScatterInstance& in : meadow.instances)
		{
			if (in.cullClass == static_cast<uint8_t>(ScatterCullClass::Large)) ++meadowTrees;
			else if (in.cullClass == static_cast<uint8_t>(ScatterCullClass::Small))
			{
				++meadowGround;
				groundInRadius = groundInRadius && std::hypot(in.x - 1000.0, in.z - 1000.0) <= p.groundRadiusM + 1e-3;
			}
			else ++meadowBush;
		}
		std::printf("  prairie : %zu arbres, %zu touffes, %zu buissons\n", meadowTrees, meadowGround, meadowBush);
		Check(meadowTrees == 0, "prairie : aucun arbre");
		Check(meadowGround > 500 && groundInRadius, "prairie : couvre-sol fourni dans son rayon");
		const double groundDensity = meadowGround / (3.14159265 * p.groundRadiusM * p.groundRadiusM);
		const double bushDensity = meadowBush / (3.14159265 * p.radiusM * p.radiusM);
		Check(meadowBush > 0 && bushDensity < groundDensity / 10.0, "prairie : buissons epars");

		// Seuls les points DE la carte : une carte voisine ne produit pas de doublon.
		ScatterFile left, right;
		GenerateForestFiller(Uniform(0.0, 0.0, 1000.0, 32, 255), 1000.0, 500.0, p, {}, left);
		GenerateForestFiller(Uniform(1000.0, 0.0, 1000.0, 32, 255), 1000.0, 500.0, p, {}, right);
		bool disjoint = true;
		for (const ScatterInstance& in : left.instances) disjoint = disjoint && in.x < 1000.0f;
		for (const ScatterInstance& in : right.instances) disjoint = disjoint && in.x >= 1000.0f;
		Check(disjoint && !left.instances.empty() && !right.instances.empty(), "remplissage : cartes voisines sans doublon");

		// Altitude : résineux au-dessus du seuil.
		ScatterFile high;
		GenerateForestFiller(full, 1000.0, 1000.0, p, [](double, double) { return 450.0; }, high);
		bool pines = true;
		for (const ScatterInstance& in : high.instances)
			if (in.cullClass == static_cast<uint8_t>(ScatterCullClass::Large))
				pines = pines && (high.meshPaths[in.meshIndex].find("Pine_") != std::string::npos
					|| high.meshPaths[in.meshIndex].find("DeadTree_") != std::string::npos);
		Check(!high.instances.empty() && pines, "remplissage : resineux en altitude");

		// Horizon boisé (22/11/2026) : arbres jusqu'à radiusM, sous-bois limité à underRadiusM.
		ForestFillerParams far = p;
		far.radiusM = 380.0;
		far.underRadiusM = 110.0;
		ScatterFile horizon;
		GenerateForestFiller(full, 1000.0, 1000.0, far, {}, horizon);
		double treeMax = 0.0, underMax = 0.0;
		for (const ScatterInstance& in : horizon.instances)
		{
			const double d = std::hypot(in.x - 1000.0, in.z - 1000.0);
			if (in.cullClass == static_cast<uint8_t>(ScatterCullClass::Large)) treeMax = std::max(treeMax, d);
			else underMax = std::max(underMax, d);
		}
		Check(treeMax > 360.0 && treeMax <= 380.0, "remplissage : arbres jusqu'a l'horizon des billboards");
		Check(underMax > 90.0 && underMax <= 110.0, "remplissage : sous-bois limite a son rayon");
	}
}

int main()
{
	TestFormat();
	TestDensity();
	TestFiller();
	if (g_failures != 0)
	{
		std::printf("forest_mask_tests : %d echec(s)\n", g_failures);
		return EXIT_FAILURE;
	}
	std::printf("forest_mask_tests : OK\n");
	return EXIT_SUCCESS;
}
