// ADM.1 — Tests : annuaire des lieux (/tp <lieu>, /places), état MJ d'un joueur
// (vitesse, vol, gel) et message GmStateUpdate (kind 104).

#include "src/shardd/gameplay/gm/GmPlaceDirectory.h"
#include "src/shardd/gameplay/gm/GmPlayerState.h"
#include "src/shared/network/ServerProtocol.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const std::string& msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	constexpr const char* kPlacesJson = R"({
  "schema_version": 1,
  "terrain": { "size_m": 20000, "origin_x": -10000, "origin_z": -10000 },
  "cities": [
    { "id": "lhynn", "name": "L'hynn", "kind": "capitale", "x": 1250.5, "z": -820.25 },
    { "id": "marn_village", "name": "Marn", "kind": "village", "x": 300, "z": 400 }
  ],
  "pois": [
    { "id": "lac_marn", "name": "Lac de Marn", "kind": "lac", "x": 320, "z": 460 },
    { "id": "foret_noire", "name": "Forêt Noire", "kind": "forêt", "x": -2000, "z": 50 }
  ],
  "regions": [
    { "id": "empire", "name": "Terres de l'Empereur", "x_min": 0, "x_max": 4000, "z_min": -2000, "z_max": 0 }
  ]
})";
}

int main()
{
	using namespace engine::server::gm;

	// --- Repli du texte ---
	Check(FoldPlaceText("L'Hynn") == "lhynn", "repli : apostrophe et majuscule");
	Check(FoldPlaceText("Forêt Noire") == "foretnoire", "repli : accent et espace");
	Check(FoldPlaceText("ÉCHO-Sud") == "echosud", "repli : majuscule accentuée et tiret");

	// --- Chargement ---
	GmPlaceDirectory dir;
	GmPlace start;
	start.id = "lhynn";
	start.name = "L'hynn";
	start.detail = "départ des humains";
	start.kind = GmPlaceKind::Start;
	start.x = 1250.0f;
	start.z = -820.0f;
	dir.Add(start);
	std::string err;
	Check(dir.LoadFromText(kPlacesJson, err), "chargement places.json : " + err);
	Check(dir.Size() == 6u, "2 villes + 2 lieux + 1 région + 1 départ conservé (" + std::to_string(dir.Size()) + ")");
	Check(!dir.LoadFromText("{ pas du json", err), "JSON invalide refusé");
	Check(dir.Size() == 1u, "après échec : seuls les départs restent");
	Check(dir.LoadFromText(kPlacesJson, err), "rechargement");

	// --- Recherche ---
	auto r = dir.Search("lhynn");
	Check(!r.empty() && r[0]->kind == GmPlaceKind::City && std::fabs(r[0]->x - 1250.5f) < 0.01f,
		"recherche exacte : la ville passe avant le départ du même nom");
	r = dir.Search("lac de marn");
	Check(!r.empty() && r[0]->id == "lac_marn", "recherche multi-mots sans majuscules");
	r = dir.Search("foret");
	Check(!r.empty() && r[0]->name == "Forêt Noire", "recherche sans accent");
	r = dir.Search("marn");
	Check(r.size() >= 2u && r[0]->name == "Marn", "nom exact avant « contient »");
	r = dir.Search("empereur");
	Check(!r.empty() && r[0]->kind == GmPlaceKind::Region && std::fabs(r[0]->x - 2000.0f) < 0.01f
		&& std::fabs(r[0]->z + 1000.0f) < 0.01f, "région : centre du rectangle");
	Check(dir.Search("zzz").empty() && dir.Search("").empty(), "recherche sans résultat");

	// --- Liste ---
	const auto all = dir.List("");
	Check(all.size() == 6u, "liste complète");
	bool sorted = true;
	for (size_t i = 1; i < all.size(); ++i)
		sorted = sorted && FoldPlaceText(all[i - 1]->name) <= FoldPlaceText(all[i]->name);
	Check(sorted, "liste triée par nom");
	Check(dir.List("marn").size() == 2u, "liste filtrée");

	// --- État MJ ---
	GmPlayerState st;
	Check(!st.IsActive() && st.Flags() == 0u && st.EffectiveSpeedMultiplier() == 1.0f, "état neutre");
	st.speedMultiplier = 5.0f;
	Check(st.IsActive() && std::fabs(st.EffectiveSpeedMultiplier() - 5.0f) < 1e-5f, "vitesse ×5");
	st.flying = true;
	Check((st.Flags() & kGmFlagFly) != 0u, "drapeau vol");
	st.frozen = true;
	Check(st.EffectiveSpeedMultiplier() == 0.0f && (st.Flags() & kGmFlagFrozen) != 0u, "gel : vitesse nulle");
	st.frozen = false;
	st.mounted = true;
	Check(std::fabs(st.EffectiveSpeedMultiplier() - 5.0f * kGmDefaultMountMultiplier) < 1e-4f, "monture multipliée");
	Check(ClampSpeedMultiplier(50.0) == kGmMaxSpeedMultiplier && ClampSpeedMultiplier(0.01) == kGmMinSpeedMultiplier
		&& ClampSpeedMultiplier(0.0) < 0.0f && ClampSpeedMultiplier(-2.0) < 0.0f, "bornes de /speed");
	float dx = 0.0f;
	float dz = 0.0f;
	ForwardOffset(0.0f, 10.0f, dx, dz);
	Check(std::fabs(dx) < 1e-4f && std::fabs(dz - 10.0f) < 1e-4f, "avant à yaw 0 = +Z");
	ForwardOffset(1.5707963f, 10.0f, dx, dz);
	Check(std::fabs(dx - 10.0f) < 1e-3f && std::fabs(dz) < 1e-3f, "avant à yaw π/2 = +X");

	// --- GmStateUpdate (kind 104) ---
	engine::server::GmStateUpdateMessage msg{};
	msg.clientId = 42;
	msg.flags = kGmFlagFly | kGmFlagFrozen;
	msg.speedMultiplier = 3.5f;
	const auto packet = engine::server::EncodeGmStateUpdate(msg);
	engine::server::GmStateUpdateMessage back{};
	Check(engine::server::DecodeGmStateUpdate(packet, back) && back.clientId == 42 && back.flags == msg.flags
		&& std::fabs(back.speedMultiplier - 3.5f) < 1e-6f, "GmStateUpdate aller-retour");
	msg.speedMultiplier = NAN;
	engine::server::GmStateUpdateMessage bad{};
	Check(!engine::server::DecodeGmStateUpdate(engine::server::EncodeGmStateUpdate(msg), bad), "GmStateUpdate NaN refusé");

	if (s_failCount == 0)
		std::cout << "gm_place_directory_tests : tout est vert" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
