// Lore v1.0.114 — tests de DiscoveryProgress (exploit des 666 cimetières, lieux
// secrets découverts par personnage). Pattern repo : int main() + vérifications.

#include "src/shardd/world/DiscoveryProgress.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int s_failCount = 0;

	/// Compte un échec et l'affiche si \p cond est faux.
	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::fprintf(stderr, "[FAIL] %s\n", msg);
		}
	}

	/// \p n cimetières alignés sur l'axe X, tous les 100 m (ids gy_0, gy_1…).
	std::vector<engine::server::GraveyardSite> Line(int n)
	{
		std::vector<engine::server::GraveyardSite> out;
		for (int i = 0; i < n; ++i)
			out.push_back({ "gy_" + std::to_string(i), 100.0f * static_cast<float>(i), 0.0f });
		return out;
	}

	const char* kSecretJson = R"({
  "world": "royaumes", "secret": true,
  "city_count": 1,
  "cities": [ { "id": "ville_thurvhal", "name": "Thurvhal", "kind": "ville", "x": 5000, "z": 5000, "radius_m": 120 } ],
  "poi_count": 2,
  "pois": [ { "id": "poi_grotte_xygga", "name": "Grotte de Xygga", "kind": "grotte", "x": -3000, "z": 200 },
            { "id": "poi_sans_rayon", "name": "Sans rayon", "kind": "lieu", "x": 255.0, "z": 0.0 } ]
})";
}

int main()
{
	using namespace engine::server;
	namespace cells = engine::world::cells;

	// 1. Jetons optionnels de respawn_points.txt (ancien et nouveau format).
	{
		const RespawnTrailingTokens a = ParseRespawnTrailingTokens(" -");
		Check(a.faction.empty() && !a.hasNeutralRadius && a.graveyardId.empty(), "ancien format : neutre, sans id");
		const RespawnTrailingTokens b = ParseRespawnTrailingTokens(" - gy=hameau_marn");
		Check(b.faction.empty() && !b.hasNeutralRadius && b.graveyardId == "hameau_marn", "nouveau format : id lu");
		const RespawnTrailingTokens c = ParseRespawnTrailingTokens(" empire 300 gy=isole_cell_n011_e018");
		Check(c.faction == "empire" && c.hasNeutralRadius && c.neutralRadiusM == 300.0f && c.graveyardId == "isole_cell_n011_e018",
			"faction + rayon + id");
		const RespawnTrailingTokens d = ParseRespawnTrailingTokens("");
		Check(d.faction.empty() && d.graveyardId.empty(), "ligne sans jeton optionnel");
		const RespawnTrailingTokens e = ParseRespawnTrailingTokens(" empire abc");
		Check(e.faction == "empire" && !e.hasNeutralRadius, "rayon illisible ignoré");
	}

	// 2. Catalogue : doublons écartés, index spatial (rayon 15 m).
	DiscoveryCatalog catalog;
	{
		std::vector<GraveyardSite> sites = Line(3);
		sites.push_back({ "gy_1", 9999.0f, 9999.0f }); // doublon
		sites.push_back({ "", 50.0f, 50.0f });         // sans id
		Check(catalog.SetGraveyards(sites) == 1u && catalog.GraveyardCount() == 3u, "doublon et id vide écartés");
		std::vector<uint32_t> hit;
		catalog.GraveyardsAt(100.0f + 14.0f, 0.0f, hit);
		Check(hit.size() == 1u && catalog.Graveyard(hit[0]).id == "gy_1", "dans le rayon de 15 m");
		catalog.GraveyardsAt(100.0f + 16.0f, 0.0f, hit);
		Check(hit.empty(), "hors du rayon de 15 m");
		catalog.GraveyardsAt(-10.0f, 0.0f, hit); // seau voisin (x < 0) : indexé par le disque
		Check(hit.size() == 1u && catalog.Graveyard(hit[0]).id == "gy_0", "disque à cheval sur deux seaux");
		Check(!catalog.PilgrimExploitPossible(), "3 cimetières : pas d'exploit");
	}

	// 3. Lieux secrets : rayon de ville, 40 m par défaut, carte.
	{
		std::string err;
		Check(catalog.AppendSecretPlacesFromText(kSecretJson, err) && catalog.SecretPlaceCount() == 3u, "secret_places lu");
		Check(!catalog.AppendSecretPlacesFromText("{ pas du json", err), "JSON invalide refusé");
		const int64_t t = catalog.FindSecretPlace("ville_thurvhal");
		const int64_t g = catalog.FindSecretPlace("poi_grotte_xygga");
		Check(t >= 0 && catalog.SecretPlace(static_cast<uint32_t>(t)).radiusM == 120.0f, "rayon de la ville");
		Check(g >= 0 && catalog.SecretPlace(static_cast<uint32_t>(g)).radiusM == kSecretPlaceDefaultRadiusM, "40 m sans rayon");
		std::vector<uint32_t> hit;
		catalog.SecretPlacesAt(cells::CellMap::Royaumes, 5000.0f + 110.0f, 5000.0f, hit);
		Check(hit.size() == 1u, "dans le rayon de Thurvhal");
		catalog.SecretPlacesAt(cells::CellMap::Enfers, 5000.0f, 5000.0f, hit);
		Check(hit.empty(), "autre carte : rien");
		catalog.SecretPlacesAt(cells::CellMap::Royaumes, -3000.0f + 41.0f, 200.0f, hit);
		Check(hit.empty(), "grotte : hors des 40 m");
	}

	// 4. Personnage : visites par position (vivant), par réapparition, découvertes.
	{
		CharacterDiscoveryProgress p;
		DiscoveryEvents ev;
		p.ObservePosition(catalog, cells::CellMap::Royaumes, 0.0f, 0.0f, ev);
		Check(ev.Empty(), "sans Load : rien");
		Check(!p.Load(catalog, { "gy_0", "ancien_id" }, { "poi_grotte_xygga" }, false), "chargement sans rattrapage");
		Check(p.VisitedCount() == 1u && p.HasVisited("ancien_id"), "id inconnu gardé mais non compté");
		p.ObservePosition(catalog, cells::CellMap::Royaumes, 2.0f, 3.0f, ev);
		Check(ev.Empty(), "cimetière déjà visité : rien de neuf");
		p.ObservePosition(catalog, cells::CellMap::Royaumes, 200.0f, 5.0f, ev);
		Check(ev.graveyards.size() == 1u && ev.graveyards[0] == "gy_2" && p.VisitedCount() == 2u, "nouvelle visite par position");
		p.ObservePosition(catalog, cells::CellMap::Enfers, 100.0f, 0.0f, ev);
		Check(ev.graveyards.size() == 1u, "aux Enfers : aucun cimetière des Royaumes");
		Check(p.RecordGraveyardVisit(catalog, "gy_1", ev) && p.VisitedCount() == 3u, "visite par réapparition");
		Check(!p.RecordGraveyardVisit(catalog, "gy_1", ev) && !p.RecordGraveyardVisit(catalog, "inconnu", ev),
			"réapparition déjà comptée / cimetière inconnu");
		Check(!ev.exploitUnlocked && !p.ExploitGrantPending(), "monde de 3 cimetières : pas d'exploit");
		p.ObservePosition(catalog, cells::CellMap::Royaumes, 5050.0f, 5000.0f, ev);
		p.ObservePosition(catalog, cells::CellMap::Royaumes, 5060.0f, 5000.0f, ev);
		Check(ev.secretPlaces.size() == 1u && p.HasDiscovered("ville_thurvhal"), "lieu secret découvert une seule fois");
		Check(p.SessionGraveyardVisits().size() == 2u && p.SessionSecretDiscoveries().size() == 1u, "nouveautés de la session");
		const std::vector<uint32_t> found = p.DiscoveredPlaceIndices(catalog);
		Check(found.size() == 2u, "lieux découverts : chargé + session");
	}

	// 5. Exploit : seulement avec 666 cimetières, au 666e, une seule fois ; rattrapage au chargement.
	{
		DiscoveryCatalog full;
		(void)full.SetGraveyards(Line(static_cast<int>(kPilgrimGraveyardCount)));
		Check(full.PilgrimExploitPossible(), "666 cimetières : exploit possible");
		std::vector<std::string> all;
		for (uint32_t i = 0; i + 1 < kPilgrimGraveyardCount; ++i)
			all.push_back("gy_" + std::to_string(i));
		CharacterDiscoveryProgress p;
		Check(!p.Load(full, all, {}, false), "665 visités : pas encore");
		DiscoveryEvents ev;
		Check(p.RecordGraveyardVisit(full, "gy_665", ev) && ev.exploitUnlocked && p.ExploitGrantPending(),
			"666e cimetière : exploit débloqué");
		DiscoveryEvents ev2;
		p.ObservePosition(full, cells::CellMap::Royaumes, 0.0f, 0.0f, ev2);
		Check(!ev2.exploitUnlocked, "pas de second déblocage");

		all.push_back("gy_665");
		CharacterDiscoveryProgress q;
		Check(q.Load(full, all, {}, false) && q.ExploitGrantPending(), "rattrapage au chargement");
		CharacterDiscoveryProgress r;
		Check(!r.Load(full, all, {}, true) && !r.ExploitGrantPending(), "déjà débloqué : rien");
	}

	if (s_failCount == 0)
		std::puts("[OK] discovery_progress_tests");
	return s_failCount == 0 ? 0 : 1;
}
