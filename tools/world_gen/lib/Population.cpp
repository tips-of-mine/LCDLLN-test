// tools/world_gen — population du monde et import du contenu existant.
//
// PNJ d'ambiance (serveur-autoritaires, cf. routines dans spawners.json) :
//   - villageois : tournées sur le graphe de rues, porte → rues → porte,
//     rentrent chez eux la nuit (schedule "day") ;
//   - gardes : rondes autour de la place et sur la rue circulaire (toujours),
//     sentinelles aux portes, veilleur de nuit ;
//   - marchands : postés devant leur étal (jour), aubergiste devant l'auberge ;
//   - voyageurs et patrouilles : aller-retour sur les routes entre villes.
// Créatures : familles par biome, niveau croissant avec la distance aux villes
// de départ (progression), groupes de quête autour des villes de départ.

#include "WorldGenContext.h"
#include "Random.h"

#include "src/client/world/instances/Buildings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace tools::world_gen
{
	namespace
	{
		using tools::zone_builder::FindObjectMember;
		using tools::zone_builder::JsonType;
		using tools::zone_builder::JsonValue;

		/// Rôles d'ambiance → archetypeId (cf. game/data/creatures/archetypes.json, plage 200-259).
		struct RaceNpcIds { uint32_t villagerM, villagerF, guard, merchant, innkeeper; };
		RaceNpcIds NpcIdsFor(const std::string& race)
		{
			if (race == "elfes")  return { 210, 211, 212, 213, 211 };
			if (race == "orcs")   return { 220, 220, 221, 222, 222 };
			if (race == "nains")  return { 230, 230, 231, 232, 232 };
			if (race == "demons") return { 240, 240, 241, 242, 242 };
			if (race == "neutre") return { 250, 250, 251, 203, 204 };
			return { 200, 201, 202, 203, 204 };
		}
		constexpr uint32_t kTraveller = 205;
		constexpr uint32_t kImperialPatrol = 206;

		int VillagersFor(const CityDef& c)
		{
			if (c.procedural == "ferme") return 2;
			if (c.procedural == "bucherons" || c.procedural == "mine") return 4;
			if (c.kind == "capitale") return 30;
			if (c.kind == "grande_ville") return 22;
			if (c.kind == "ville") return 16;
			if (c.kind == "forteresse") return 10;
			if (c.kind == "village") return 10;
			if (c.kind == "camp") return 6;
			return 5;
		}
		int GuardsFor(const CityDef& c)
		{
			if (c.procedural == "ferme" || c.procedural == "bucherons") return 0;
			if (c.procedural == "mine") return 1;
			if (c.kind == "capitale") return 10;
			if (c.kind == "grande_ville" || c.kind == "forteresse") return 6;
			if (c.kind == "ville") return 4;
			if (c.kind == "village" || c.kind == "camp") return 2;
			return 1;
		}

		/// Tournée aléatoire (marche sans demi-tour immédiat) sur le graphe de rues.
		std::vector<Vec2> RandomTour(const SettlementLayout& L, int start, int steps, Rng& rng)
		{
			std::vector<Vec2> out;
			if (L.nodes.empty()) return out;
			int cur = std::clamp(start, 0, static_cast<int>(L.nodes.size()) - 1);
			int prev = -1;
			out.push_back(L.nodes[static_cast<size_t>(cur)].p);
			for (int s = 0; s < steps; ++s)
			{
				const auto& links = L.nodes[static_cast<size_t>(cur)].links;
				if (links.empty()) break;
				std::vector<int> options;
				for (int l : links) if (l != prev) options.push_back(l);
				if (options.empty()) options = links;
				const int nxt = options[rng.Below(static_cast<uint32_t>(options.size()))];
				prev = cur; cur = nxt;
				out.push_back(L.nodes[static_cast<size_t>(cur)].p);
			}
			return out;
		}

		int NearestNode(const SettlementLayout& L, Vec2 p)
		{
			int best = 0; double bd = 1e18;
			for (size_t i = 0; i < L.nodes.size(); ++i)
			{
				const double d = std::hypot(L.nodes[i].p.x - p.x, L.nodes[i].p.z - p.z);
				if (d < bd) { bd = d; best = static_cast<int>(i); }
			}
			return best;
		}

		/// Famille de créatures selon le biome et la distance à la ville de départ
		/// la plus proche (≈ niveau de zone). Renvoie 0 si le biome est vide.
		uint32_t CreatureFor(char code, double distToStart, Rng& rng)
		{
			// Paliers de progression (échelle 10 km) : ≈ niveaux 1-3 / 4-7 / 8+.
			const bool isNear = distToStart < 1500.0;
			const bool mid = distToStart < 4500.0;
			switch (code)
			{
			case 'T': case 'R': case 'L': case 't':
				if (isNear) return rng.Chance(0.5) ? 107u : (rng.Chance(0.5) ? 102u : 105u);
				if (mid) return rng.Chance(0.5) ? 101u : 105u;
				return rng.Chance(0.6) ? 160u : 101u;           // ours bruns, loups
			case '.': case 'r': case 'o':
				if (isNear) return rng.Chance(0.6) ? 102u : 100u;
				if (mid) return rng.Chance(0.5) ? 100u : 107u;
				return rng.Chance(0.6) ? 162u : 104u;           // gobelins pillards, sangliers alpha
			case '%': case '&':
				return mid ? 105u : 163u;                        // spectres du marais
			case '~': case ':': case 'l':
				return mid ? 101u : 165u;                        // scorpions géants
			case '^': case 'M':
				return mid ? 101u : 161u;                        // ogres des collines
			case 'V':
				return 164u;                                     // élémentaires de braise
			default:
				return 0u;
			}
		}

		uint8_t CullClassForMesh(const std::string& mesh)
		{
			auto has = [&](const char* s) { return mesh.find(s) != std::string::npos; };
			if (has("Tree") || has("Pine")) return 0;
			if (has("Rock") || has("Bush") || has("Fern") || has("Plant_1_Big") || has("Barrel") || has("Crate")) return 1;
			return 2;
		}
	}

	// ------------------------------------------------------------------ imports

	bool ImportLegacyContent(World& w, std::string& err)
	{
		const std::string& root = w.contentRoot;
		// Interactibles déclarés dans le master, ancrés sur une ville DE la cellule.
		for (InteractableDef d : w.master.interactables)
		{
			if (d.cityId.empty()) continue;
			const CityDef* c = nullptr;
			for (const CityDef& cc : w.cities) if (cc.id == d.cityId) c = &cc;
			if (!c) continue;
			d.x += c->center.x; d.z += c->center.z;
			d.cityId.clear();
			w.interactables.push_back(std::move(d));
		}

		auto readText = [&](const std::string& rel, std::string& out) {
			std::ifstream f(root + "/" + rel, std::ios::binary);
			if (!f.good()) return false;
			std::stringstream ss; ss << f.rdbuf(); out = ss.str();
			return true;
		};
		// Réglages client world.* (brouillard, DOF, impostors…) de la zone importée :
		// recopiés dans le zone.json de CHAQUE cellule (pas seulement celle du hameau).
		JsonValue importedZone;
		bool hasImportedZone = false;
		if (!w.master.importZoneConfig.empty())
		{
			std::string txt;
			if (!readText(w.master.importZoneConfig, txt)) { err = "import zone_config introuvable : " + w.master.importZoneConfig; return false; }
			std::string perr;
			if (!tools::zone_builder::ParseJsonDocument(txt, importedZone, perr)) { err = "import zone_config JSON : " + perr; return false; }
			hasImportedZone = true;
			if (const JsonValue* world = FindObjectMember(importedZone, "world"); world && world->type == JsonType::Object)
			{
				w.importedWorldConfig = *world;
				// Interactibles, spawn et eau-test sont regénérés par cellule.
				for (const char* k : { "interactables", "_comment_interactables", "default_spawn", "test_water", "_comment_test_water" })
					w.importedWorldConfig.objectValue.erase(k);
			}
		}

		// Contenu historique (ex-zone feyhin) : seulement dans la cellule de la
		// ville d'ancrage, translaté sur son centre (l'ancienne origine (0,0)).
		const CityDef* anchor = nullptr;
		for (const CityDef& c : w.cities) if (c.id == w.master.importAnchorCity) anchor = &c;
		if (!anchor)
		{
			for (const InteractableDef& d : w.interactables) w.Occupy({ d.x, d.z }, std::max(1.2, d.radius));
			w.report.interactables = w.interactables.size();
			return true;
		}
		const Vec2 off = anchor->center;
		// --- zone.json : interactibles historiques.
		if (hasImportedZone)
		{
			const JsonValue& zroot = importedZone;
			const JsonValue* world = FindObjectMember(zroot, "world");
			if (world && world->type == JsonType::Object)
			{
				if (const JsonValue* it = FindObjectMember(*world, "interactables"); it && it->type == JsonType::Object)
				{
					const JsonValue* cnt = FindObjectMember(*it, "count");
					const int n = cnt ? static_cast<int>(cnt->numberValue) : 0;
					for (int k = 0; k < n; ++k)
					{
						const JsonValue* e = FindObjectMember(*it, std::to_string(k));
						if (!e || e->type != JsonType::Object) continue;
						auto str = [&](const char* key) { const JsonValue* v = FindObjectMember(*e, key); return (v && v->type == JsonType::String) ? v->stringValue : std::string(); };
						auto num = [&](const char* key, double d) { const JsonValue* v = FindObjectMember(*e, key); return (v && v->type == JsonType::Number) ? v->numberValue : d; };
						InteractableDef d;
						d.x = num("x", 0.0) + off.x; d.z = num("z", 0.0) + off.z; d.radius = num("radius", 1.5);
						const JsonValue* npc = FindObjectMember(*e, "npc");
						d.npc = npc && npc->type == JsonType::Bool && npc->boolValue;
						d.label = str("label"); d.role = str("role"); d.message = str("message"); d.mesh = str("mesh");
						d.scale = num("scale", 1.0); d.rotXDeg = num("rot_x_deg", 0.0); d.yawDeg = num("yaw_deg", 0.0);
						d.dialogueId = str("dialogue_id"); d.npcTargetId = str("npc_target_id");
						if (const JsonValue* dl = FindObjectMember(*e, "dialogue"); dl && dl->type == JsonType::Object)
						{
							const JsonValue* dc = FindObjectMember(*dl, "count");
							for (int q = 0; dc && q < static_cast<int>(dc->numberValue); ++q)
								if (const JsonValue* line = FindObjectMember(*dl, std::to_string(q)); line && line->type == JsonType::String)
									d.dialogueLines.push_back(line->stringValue);
						}
						w.interactables.push_back(std::move(d));
					}
				}
			}
		}
		// --- scenery.json : props historiques → scatter.
		if (!w.master.importScenery.empty())
		{
			std::string txt;
			if (!readText(w.master.importScenery, txt)) { err = "import scenery introuvable : " + w.master.importScenery; return false; }
			JsonValue sroot; std::string perr;
			if (!tools::zone_builder::ParseJsonDocument(txt, sroot, perr)) { err = "import scenery JSON : " + perr; return false; }
			const JsonValue* world = FindObjectMember(sroot, "world");
			const JsonValue* sc = world ? FindObjectMember(*world, "scenery") : nullptr;
			const JsonValue* cnt = sc ? FindObjectMember(*sc, "count") : nullptr;
			const int n = cnt ? static_cast<int>(cnt->numberValue) : 0;
			for (int k = 0; k < n; ++k)
			{
				const JsonValue* e = FindObjectMember(*sc, std::to_string(k));
				if (!e || e->type != JsonType::Object) continue; // indices vacants (auberge migrée)
				auto num = [&](const char* key, double d) { const JsonValue* v = FindObjectMember(*e, key); return (v && v->type == JsonType::Number) ? v->numberValue : d; };
				const JsonValue* mesh = FindObjectMember(*e, "mesh");
				if (!mesh || mesh->type != JsonType::String) continue;
				ScatterDef s;
				s.mesh = mesh->stringValue;
				s.position = { num("x", 0.0) + off.x, num("z", 0.0) + off.z };
				s.yawDeg = num("yaw_deg", 0.0);
				s.scale = num("scale", 1.0);
				s.collisionRadius = num("collision_radius", 0.0);
				const JsonValue* solid = FindObjectMember(*e, "solid");
				if (solid && solid->type == JsonType::Bool && solid->boolValue) s.flags |= 1u;
				s.cullClass = CullClassForMesh(s.mesh);
				if (s.cullClass == 0) s.flags |= 4u;
				w.Occupy(s.position, s.cullClass == 0 ? 1.6 : 0.6);
				w.scatter.push_back(std::move(s));
			}
		}
		// --- buildings.bin (LCBD) : recopié dans la liste des placements.
		if (!w.master.importBuildings.empty())
		{
			std::ifstream f(root + "/" + w.master.importBuildings, std::ios::binary);
			if (!f.good()) { err = "import buildings introuvable : " + w.master.importBuildings; return false; }
			std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			std::vector<engine::world::instances::BuildingPlacement> placements;
			std::string berr;
			if (!engine::world::instances::LoadBuildingsBin(bytes, placements, berr)) { err = "import buildings : " + berr; return false; }
			for (const auto& p : placements)
			{
				BuildingPlacementDef b;
				b.templateType = p.templateType; b.variantId = p.variantId; b.displayName = p.displayName;
				b.position = { p.worldPosition.x + off.x, p.worldPosition.z + off.z };
				b.y = p.worldPosition.y; b.yawDeg = p.worldYawDeg; b.footprintRadius = 7.0;
				b.cityId = anchor->id;
				b.doorPoint = b.position;
				b.isInn = p.templateType == "tavern";
				w.Occupy(b.position, b.footprintRadius);
				w.buildings.push_back(std::move(b));
			}
		}
		// --- respawn_points.txt : zone id de la cellule, positions translatées,
		//     altitude recalée sur le terrain généré.
		if (!w.master.importRespawn.empty())
		{
			std::ifstream f(root + "/" + w.master.importRespawn);
			if (!f.good()) { err = "import respawn introuvable : " + w.master.importRespawn; return false; }
			std::string line;
			while (std::getline(f, line))
			{
				if (line.empty() || line[0] == '#') continue;
				std::istringstream ls(line);
				std::string zid, type, rest;
				double x = 0, y = 0, z = 0;
				if (!(ls >> zid >> type >> x >> y >> z)) continue;
				std::getline(ls, rest);
				const Vec2 p{ x + off.x, z + off.z };
				char buf[160];
				std::snprintf(buf, sizeof buf, "%u %s %.3f %.3f %.3f", w.zoneId, type.c_str(), p.x, w.HeightAt(p) + 1.0, p.z);
				w.importedRespawnLines.push_back(buf + rest);
			}
		}
		// --- spawners.json historique : créatures du hameau (quêtes de l'ancien Marn
		//     et du capitaine Rendal), ids préfixés pour éviter tout doublon.
		if (!w.master.importSpawners.empty())
		{
			std::string txt;
			if (!readText(w.master.importSpawners, txt)) { err = "import spawners introuvable : " + w.master.importSpawners; return false; }
			JsonValue sroot; std::string perr;
			if (!tools::zone_builder::ParseJsonDocument(txt, sroot, perr)) { err = "import spawners JSON : " + perr; return false; }
			const JsonValue* arr = FindObjectMember(sroot, "spawners");
			if (arr && arr->type == JsonType::Array)
				for (const JsonValue& e : arr->arrayValue)
				{
					auto num = [&](const char* key, double d) { const JsonValue* v = FindObjectMember(e, key); return (v && v->type == JsonType::Number) ? v->numberValue : d; };
					const JsonValue* id = FindObjectMember(e, "id");
					const JsonValue* pos = FindObjectMember(e, "position");
					if (!id || id->type != JsonType::String || !pos || pos->type != JsonType::Array || pos->arrayValue.size() < 3) continue;
					SpawnerDef s;
					s.id = "import_" + id->stringValue;
					s.archetypeId = static_cast<uint32_t>(num("archetypeId", 0));
					s.position = { pos->arrayValue[0].numberValue + off.x, pos->arrayValue[2].numberValue + off.z };
					s.count = static_cast<uint32_t>(num("count", 1));
					s.respawnSec = static_cast<uint32_t>(num("respawnSec", 60));
					s.leashM = num("leashDistanceMeters", 24.0);
					s.y = w.HeightAt(s.position) + 0.05;
					w.spawners.push_back(std::move(s));
					++w.report.creatures;
				}
		}
		for (const InteractableDef& d : w.interactables) w.Occupy({ d.x, d.z }, std::max(1.2, d.radius));
		w.report.interactables = w.interactables.size();
		return true;
	}

	// --------------------------------------------------------------- population

	void BuildPopulation(World& w)
	{
		const uint64_t seed = w.master.seed;
		std::map<std::string, const SettlementLayout*> layoutOf;
		for (const SettlementLayout& L : w.layouts) layoutOf[L.cityId] = &L;

		auto addSpawner = [&](SpawnerDef s) {
			s.y = w.HeightAt(s.position) + 0.05;
			w.spawners.push_back(std::move(s));
		};

		// ---- PNJ d'ambiance par agglomération.
		for (const CityDef& c : w.cities)
		{
			const SettlementLayout* L = layoutOf[c.id];
			if (!L || L->nodes.size() < 3) continue;
			const RaceNpcIds ids = NpcIdsFor(c.race);
			const bool ruin = c.state == "ruine";
			Rng cr(Hash(seed, HashString(c.id.c_str()), 77));
			// Portes des maisons de la ville (départ / retour des tournées).
			std::vector<Vec2> doors;
			for (const BuildingPlacementDef& b : w.buildings)
				if (b.cityId == c.id && !b.isInn) doors.push_back(b.doorPoint);
			if (doors.empty()) doors.push_back(L->nodes[0].p);

			if (ruin)
			{
				// Ruines : repaire de pillards (combat, IA historique).
				for (int g = 0; g < 2; ++g)
				{
					SpawnerDef s;
					s.id = "amb_" + c.id + "_pillards_" + std::to_string(g);
					s.archetypeId = 103;
					s.position = L->nodes[static_cast<size_t>(cr.Below(static_cast<uint32_t>(L->nodes.size())))].p;
					s.count = 3; s.respawnSec = 150; s.leashM = 26.0;
					addSpawner(s);
					++w.report.creatures;
				}
				continue;
			}

			// Repaire de bandits : groupes de pillards (combat), pas de vie civile.
			if (c.procedural == "bandits")
			{
				for (int g = 0; g < 3; ++g)
				{
					SpawnerDef s;
					s.id = "amb_" + c.id + "_bandits_" + std::to_string(g);
					s.archetypeId = 103;
					s.position = L->nodes[static_cast<size_t>(cr.Below(static_cast<uint32_t>(L->nodes.size())))].p;
					s.count = 3; s.respawnSec = 180; s.leashM = 30.0;
					addSpawner(s);
					++w.report.creatures;
				}
				continue;
			}

			// ETAPE4.4 (chantier 2.7) — lieux de la journée des PNJ : auberge, place, étals.
			// Tirages sur un générateur À PART (cr reste inchangé : même ville, mêmes
			// tournées qu'avant). Le shard suit l'agenda ; un shard plus ancien, qui ignore
			// `agenda`, garde la routine.
			Rng ar(Hash(seed, HashString(c.id.c_str()), 4404));
			std::vector<Vec2> squarePts;
			for (int r : L->plazaRing) squarePts.push_back(L->nodes[static_cast<size_t>(r)].p);
			if (squarePts.empty()) squarePts.push_back(c.center);
			const bool hasInn = std::hypot(L->innDoor.x - c.center.x, L->innDoor.z - c.center.z) > 1.0;
			const std::vector<Vec2> tavernPts{ hasInn ? L->innDoor : squarePts.front() };
			const std::vector<Vec2> marketPts = L->stallFronts.empty() ? squarePts : L->stallFronts;
			auto commonPlaces = [&](SpawnerDef& s, Vec2 home) {
				s.agendaPlaces["home"] = { home };
				s.agendaPlaces["tavern"] = tavernPts;
				s.agendaPlaces["square"] = squarePts;
				s.agendaPlaces["market"] = marketPts;
			};

			const int villagers = VillagersFor(c);
			for (int k = 0; k < villagers; ++k)
			{
				const Vec2 door = doors[cr.Below(static_cast<uint32_t>(doors.size()))];
				std::vector<Vec2> tour{ door };
				const std::vector<Vec2> walk = RandomTour(*L, NearestNode(*L, door), 5 + static_cast<int>(cr.Below(6)), cr);
				tour.insert(tour.end(), walk.begin(), walk.end());
				tour.push_back(door);
				SpawnerDef s;
				s.id = "amb_" + c.id + "_villageois_" + std::to_string(k);
				s.archetypeId = (k % 2) ? ids.villagerF : ids.villagerM;
				s.position = door;
				s.count = 1; s.respawnSec = 30; s.leashM = 200.0;
				s.routineKind = "tour"; s.path = tour;
				s.pauseMinSec = 3.0; s.pauseMaxSec = 14.0; s.pauseChance = 0.4;
				s.schedule = (k % 5 == 4) ? "always" : "day";
				// ETAPE4.4 — villageois de jour : une journée de métier (fermier, artisan,
				// flâneur) ; les noctambules (« always ») gardent leur tournée.
				if (s.schedule == "day")
				{
					static const char* kTrades[] = { "fermier", "artisan", "flaneur" };
					s.agendaProfession = kTrades[k % 3];
					commonPlaces(s, door);
					// Lieu de travail : un nœud de la ville (champ, atelier, chantier).
					s.agendaPlaces["work"] = { L->nodes[static_cast<size_t>(ar.Below(static_cast<uint32_t>(L->nodes.size())))].p };
				}
				addSpawner(s);
				++w.report.ambientNpcs;
			}
			// Gardes : ronde sur la place, sentinelles aux portes, veilleur de nuit.
			const int guards = GuardsFor(c);
			for (int k = 0; k < guards; ++k)
			{
				SpawnerDef s;
				s.archetypeId = ids.guard;
				s.count = 1; s.respawnSec = 45; s.leashM = 200.0;
				if (k < static_cast<int>(L->gatePoints.size()) && k < 3 && c.kind != "hameau")
				{
					// Sentinelle : à l'intérieur de la porte, regard vers l'extérieur.
					const Vec2 g = L->gatePoints[static_cast<size_t>(k)];
					const Vec2 dir{ g.x - c.center.x, g.z - c.center.z };
					const double dl = std::hypot(dir.x, dir.z);
					s.id = "amb_" + c.id + "_sentinelle_" + std::to_string(k);
					s.position = c.center + dir * ((dl - 6.0) / std::max(1.0, dl)) + Vec2{ -dir.z / std::max(1.0, dl), dir.x / std::max(1.0, dl) } * 2.6;
					s.routineKind = "post"; s.wanderRadiusM = 1.5;
					s.pauseMinSec = 6.0; s.pauseMaxSec = 20.0;
					s.schedule = "always";
					s.hasFacePoint = true;
					s.facePoint = g + dir * (8.0 / std::max(1.0, dl));
				}
				else
				{
					s.id = "amb_" + c.id + "_ronde_" + std::to_string(k);
					std::vector<Vec2> loop;
					for (int r : L->plazaRing) loop.push_back(L->nodes[static_cast<size_t>(r)].p);
					// Rondes alternées : place, puis grande boucle par une rue.
					if (k % 2 == 1 && L->nodes.size() > 12)
					{
						const std::vector<Vec2> ext = RandomTour(*L, L->plazaRing[0], 10, cr);
						loop.insert(loop.end(), ext.begin(), ext.end());
					}
					if (k % 2 == 1) std::reverse(loop.begin(), loop.end());
					s.position = loop.front();
					s.routineKind = "patrol"; s.path = loop;
					s.pauseMinSec = 2.0; s.pauseMaxSec = 6.0; s.pauseChance = 0.15;
					s.schedule = (k == guards - 1 && guards > 1) ? "night" : "always";
				}
				addSpawner(s);
				++w.report.ambientNpcs;
			}
			// Marchands (jour) devant les étals, aubergiste devant l'auberge.
			for (size_t k = 0; k < L->stallFronts.size(); ++k)
			{
				SpawnerDef s;
				s.id = "amb_" + c.id + "_marchand_" + std::to_string(k);
				s.archetypeId = ids.merchant;
				s.position = L->stallFronts[k];
				s.count = 1; s.respawnSec = 30; s.leashM = 200.0;
				s.routineKind = "post"; s.wanderRadiusM = 1.2;
				s.pauseMinSec = 5.0; s.pauseMaxSec = 16.0; s.schedule = "day";
				s.hasFacePoint = true;
				s.facePoint = c.center; // tourné vers la place (les clients)
				// ETAPE4.4 — journée de marchand : son étal, l'auberge à midi, la place le soir.
				s.agendaProfession = "marchand";
				commonPlaces(s, doors[ar.Below(static_cast<uint32_t>(doors.size()))]);
				s.agendaPlaces["market"] = { L->stallFronts[k] };
				addSpawner(s);
				++w.report.ambientNpcs;
			}
			if (std::hypot(L->innDoor.x - c.center.x, L->innDoor.z - c.center.z) > 1.0)
			{
				SpawnerDef s;
				s.id = "amb_" + c.id + "_aubergiste";
				s.archetypeId = ids.innkeeper;
				s.position = L->innDoor;
				s.count = 1; s.respawnSec = 30; s.leashM = 200.0;
				s.routineKind = "post"; s.wanderRadiusM = 2.0;
				s.pauseMinSec = 4.0; s.pauseMaxSec = 12.0; s.schedule = "always";
				// ETAPE4.4 — l'aubergiste dort à l'auberge et y travaille.
				s.agendaProfession = "aubergiste";
				commonPlaces(s, L->innDoor);
				addSpawner(s);
				++w.report.ambientNpcs;
			}
		}

		// ---- Voyageurs et patrouilles sur les routes.
		int roadIdx = 0;
		for (const Road& r : w.roads)
		{
			if (r.path) { ++roadIdx; continue; } // chemins locaux : pas de voyageurs
			std::vector<Vec2> path;
			for (size_t k = 0; k < r.points.size(); k += 25)
				if (w.InPlayableBounds(r.points[k], 10.0)) path.push_back(r.points[k]);
			if (path.size() < 4) { ++roadIdx; continue; }
			const int n = r.major ? 2 : 1;
			for (int k = 0; k < n; ++k)
			{
				SpawnerDef s;
				s.id = "amb_route_" + r.fromId + "_" + r.toId + "_" + std::to_string(roadIdx) + "_" + std::to_string(k);
				s.archetypeId = (r.major && k == 1) ? kImperialPatrol : kTraveller;
				std::vector<Vec2> p = path;
				if (k == 1) std::reverse(p.begin(), p.end());
				s.position = p.front();
				s.count = 1; s.respawnSec = 120; s.leashM = 4000.0;
				s.routineKind = "patrol"; s.path = p; s.pingPong = true;
				s.pauseMinSec = 4.0; s.pauseMaxSec = 10.0; s.pauseChance = 0.05;
				s.schedule = s.archetypeId == kImperialPatrol ? "always" : "day";
				addSpawner(s);
				++w.report.ambientNpcs;
			}
			++roadIdx;
		}

		// ---- Créatures par biome (treillis GLOBAL jitterisé de 380 m).
		const double cell = 380.0;
		const int gi0 = static_cast<int>(std::floor(w.terrainOriginX / cell)) - 1;
		const int gj0 = static_cast<int>(std::floor(w.terrainOriginZ / cell)) - 1;
		const int nC = static_cast<int>(w.terrainSizeM / cell) + 3;
		for (int jj = 0; jj < nC; ++jj)
			for (int ii = 0; ii < nC; ++ii)
			{
				const int i = gi0 + ii, j = gj0 + jj;
				const uint64_t h = Hash(seed, 5150, static_cast<uint64_t>(static_cast<int64_t>(i)), static_cast<uint64_t>(static_cast<int64_t>(j)));
				const Vec2 p{ (i + 0.2 + 0.6 * ToUnit(h)) * cell, (j + 0.2 + 0.6 * ToUnit(Mix64(h))) * cell };
				if (!w.OwnsPoint(p) || !w.InPlayableBounds(p, 40.0) || w.IsWater(p) || w.SlopeDegAt(p) > 28.0) continue;
				bool nearCity = false;
				for (const CityDef& c : w.cities)
					if (std::hypot(p.x - c.center.x, p.z - c.center.z) < c.radiusM + 120.0) nearCity = true;
				if (nearCity) continue;
				const int ri = w.roadDist.IndexX(p.x), rj = w.roadDist.IndexZ(p.z);
				if (w.roadDist.InBounds(ri, rj) && w.roadDist.At(ri, rj) < 25.0f) continue;
				double dStart = 1e18;
				for (const Vec2& sc : w.g->startCenters) dStart = std::min(dStart, std::hypot(p.x - sc.x, p.z - sc.z));
				Rng lr(h);
				if (lr.Chance(0.3)) continue; // respirations : pas un monstre à chaque case
				const char code = w.biome.At(w.biome.IndexX(p.x), w.biome.IndexZ(p.z));
				const uint32_t arch = CreatureFor(code, dStart, lr);
				if (arch == 0) continue;
				SpawnerDef s;
				s.id = "faune_" + std::to_string(i) + "_" + std::to_string(j);
				s.archetypeId = arch;
				s.position = p;
				s.count = 2 + lr.Below(3);
				s.respawnSec = 90 + lr.Below(90);
				s.leashM = 28.0;
				addSpawner(s);
				++w.report.creatures;
			}

		// ---- Créatures des quêtes d'accueil autour des villes de départ (hors humains,
		//      dont le hameau importé a déjà ses spawners historiques).
		struct StartFauna { const char* race; uint32_t a, b; };
		const StartFauna fauna[] = { { "elfes", 120, 121 }, { "orcs", 130, 131 }, { "nains", 140, 141 }, { "demons", 150, 151 } };
		for (const CityDef& c : w.cities)
			for (const std::string& race : c.startFor)
				for (const StartFauna& f : fauna)
				{
					if (race != f.race) continue;
					Rng qr(Hash(seed, HashString(c.id.c_str()), 31337));
					int made = 0;
					for (int attempt = 0; attempt < 80 && made < 6; ++attempt)
					{
						const double a = qr.Range(0.0, 6.283185307179586);
						const double d = c.radiusM + qr.Range(120.0, 420.0);
						const Vec2 p = c.center + Vec2{ std::cos(a), std::sin(a) } * d;
						if (!w.InPlayableBounds(p, 60.0) || w.IsWater(p) || w.SlopeDegAt(p) > 26.0 || w.IsOccupied(p, 3.0)) continue;
						const int ri = w.roadDist.IndexX(p.x), rj = w.roadDist.IndexZ(p.z);
						if (w.roadDist.InBounds(ri, rj) && w.roadDist.At(ri, rj) < 10.0f) continue;
						SpawnerDef s;
						s.id = "quete_" + c.id + "_" + std::to_string(made);
						s.archetypeId = (made < 4) ? f.a : f.b;
						s.position = p;
						s.count = (made < 4) ? 3u : 2u;
						s.respawnSec = 60; s.leashM = 24.0;
						addSpawner(s);
						++made;
						++w.report.creatures;
					}
				}

		// ---- Respawns : importés + cimetière et auberge de chaque agglomération.
		for (const SettlementLayout& L : w.layouts)
		{
			const CityDef* c = w.FindCity(L.cityId);
			if (!c || c->id == w.master.importAnchorCity) continue; // le hameau importé a les siens
			if (!c->procedural.empty() && c->procedural != "hameau") continue; // fermes, camps, ruines : pas de respawn
			RespawnDef g; g.type = "graveyard"; g.position = L.graveyard; g.y = w.HeightAt(L.graveyard) + 1.0; g.faction = "-";
			w.respawns.push_back(g);
			if (c->state != "ruine" && c->kind != "camp")
			{
				RespawnDef inn; inn.type = "inn"; inn.position = L.innDoor; inn.y = w.HeightAt(L.innDoor) + 1.0; inn.faction = "-";
				w.respawns.push_back(inn);
			}
		}
		w.report.respawnPoints = w.respawns.size() + w.importedRespawnLines.size();

		// ---- Points de départ par race.
		for (const SettlementLayout& L : w.layouts)
		{
			const CityDef* c = w.FindCity(L.cityId);
			if (!c) continue;
			for (const std::string& race : c->startFor)
			{
				StartLocationDef s;
				s.race = race; s.cityId = c->id;
				// Ville d'ancrage importée : on garde le point d'entrée historique
				// (ancienne origine (0,0) de feyhin = centre de la ville).
				s.position = (c->id == w.master.importAnchorCity) ? c->center : L.startSpot;
				s.yawDeg = (c->id == w.master.importAnchorCity) ? 0.0 : L.startYawDeg;
				s.y = w.HeightAt(s.position) + 1.0;
				w.starts.push_back(s);
			}
		}
		w.report.spawners = w.spawners.size();
	}
}
