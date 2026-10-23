// tools/world_gen — chargement et validation du master du monde.
//
// Politique STRICTE : toute incohérence (cellule invalide, id dupliqué,
// rangée de grille de mauvaise longueur, ville dans l'eau…) fait échouer la
// génération avec un message explicite. Mieux vaut un CI rouge qu'un monde
// silencieusement faux.

#include "WorldGenContext.h"

#include <zone_builder/JsonDocument.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace tools::world_gen
{
	namespace
	{
		using tools::zone_builder::FindObjectMember;
		using tools::zone_builder::JsonType;
		using tools::zone_builder::JsonValue;

		std::string GetStr(const JsonValue& o, const char* key, const std::string& def = {})
		{
			const JsonValue* v = FindObjectMember(o, key);
			return (v && v->type == JsonType::String) ? v->stringValue : def;
		}
		double GetNum(const JsonValue& o, const char* key, double def)
		{
			const JsonValue* v = FindObjectMember(o, key);
			return (v && v->type == JsonType::Number) ? v->numberValue : def;
		}
		bool GetBool(const JsonValue& o, const char* key, bool def)
		{
			const JsonValue* v = FindObjectMember(o, key);
			return (v && v->type == JsonType::Bool) ? v->boolValue : def;
		}
		std::vector<std::string> GetStrArray(const JsonValue& o, const char* key)
		{
			std::vector<std::string> out;
			const JsonValue* v = FindObjectMember(o, key);
			if (v && v->type == JsonType::Array)
				for (const JsonValue& e : v->arrayValue)
					if (e.type == JsonType::String) out.push_back(e.stringValue);
			return out;
		}
	}

	/// Décode "N003_W009" → (n=3, e=-9). Renvoie false si le format est invalide.
	bool ParseCellId(const std::string& id, int& n, int& e)
	{
		if (id.size() != 9 || id[4] != '_') return false;
		const char ns = id[0], ew = id[5];
		if ((ns != 'N' && ns != 'S') || (ew != 'E' && ew != 'W')) return false;
		for (int i : { 1, 2, 3, 6, 7, 8 })
			if (id[static_cast<size_t>(i)] < '0' || id[static_cast<size_t>(i)] > '9') return false;
		n = std::stoi(id.substr(1, 3)) * (ns == 'N' ? 1 : -1);
		e = std::stoi(id.substr(6, 3)) * (ew == 'E' ? 1 : -1);
		return true;
	}

	bool LoadWorldMaster(const std::string& contentRoot, const std::string& worldId,
		WorldMaster& out, std::string& err)
	{
		const std::string path = contentRoot + "/world/" + worldId + "/world_master.json";
		std::ifstream f(path, std::ios::binary);
		if (!f.good()) { err = "master introuvable : " + path; return false; }
		std::stringstream ss; ss << f.rdbuf();
		JsonValue root;
		std::string perr;
		if (!tools::zone_builder::ParseJsonDocument(ss.str(), root, perr))
		{
			err = "master JSON invalide (" + path + ") : " + perr; return false;
		}
		if (GetNum(root, "schema_version", 0) != 1.0) { err = "master: schema_version 1 attendu"; return false; }
		out = WorldMaster{};
		out.worldId = GetStr(root, "world_id");
		out.displayName = GetStr(root, "display_name", out.worldId);
		out.seed = static_cast<uint64_t>(GetNum(root, "seed", 1.0));
		if (out.worldId != worldId) { err = "master: world_id '" + out.worldId + "' != '" + worldId + "'"; return false; }

		// --- échelle ---
		const JsonValue* sc = FindObjectMember(root, "scale");
		if (!sc || sc->type != JsonType::Object) { err = "master: section 'scale' manquante"; return false; }
		WorldScale& s = out.scale;
		s.cellSizeM = GetNum(*sc, "cell_size_m", 10000.0);
		s.gridCols = static_cast<int>(GetNum(*sc, "grid_cols", 40.0));
		s.westE = static_cast<int>(GetNum(*sc, "west_e", -19.0));
		s.northN = static_cast<int>(GetNum(*sc, "north_n", 18.0));
		s.heightScaleM = GetNum(*sc, "height_scale_m", 1200.0);
		s.seaLevelM = GetNum(*sc, "sea_level_m", 40.0);
		s.heightmapRes = static_cast<int>(GetNum(*sc, "heightmap_resolution", 2049.0));
		// La cellule du lore EST la cellule moteur de 10 km (WorldCells.h) : pas
		// d'autre échelle possible sans casser client et serveur.
		if (s.cellSizeM != engine::world::cells::kCellSizeMeters)
		{
			err = "master: scale.cell_size_m doit valoir 10000 (1 case du lore = 1 cellule de 10 km)"; return false;
		}
		if (s.gridCols < 4 || s.gridCols > 128)
		{
			err = "master: grid_cols hors bornes (4..128)"; return false;
		}
		if (s.heightmapRes != 1025 && s.heightmapRes != 2049 && s.heightmapRes != 4097)
		{
			err = "master: heightmap_resolution doit valoir 1025, 2049 ou 4097"; return false;
		}
		if (s.heightScaleM <= s.seaLevelM + 20.0) { err = "master: height_scale_m trop faible"; return false; }

		// --- grille ---
		const JsonValue* gr = FindObjectMember(root, "grid");
		if (!gr) { err = "master: section 'grid' manquante"; return false; }
		out.gridRows = GetStrArray(*gr, "rows");
		if (out.gridRows.empty()) { err = "master: grid.rows vide"; return false; }
		const JsonValue* lg = FindObjectMember(*gr, "legend");
		std::set<char> known;
		if (lg && lg->type == JsonType::Object)
		{
			for (const auto& kv : lg->objectValue)
			{
				if (kv.first.size() == 1 && kv.second.type == JsonType::String)
				{
					out.legend.emplace_back(kv.first[0], kv.second.stringValue);
					known.insert(kv.first[0]);
				}
			}
			std::sort(out.legend.begin(), out.legend.end());
		}
		for (size_t r = 0; r < out.gridRows.size(); ++r)
		{
			if (static_cast<int>(out.gridRows[r].size()) != s.gridCols)
			{
				err = "master: grid.rows[" + std::to_string(r) + "] doit faire " + std::to_string(s.gridCols) + " caracteres";
				return false;
			}
			for (char c : out.gridRows[r])
				if (!known.count(c)) { err = std::string("master: code de terrain inconnu '") + c + "'"; return false; }
		}

		// --- régions ---
		if (const JsonValue* rg = FindObjectMember(root, "regions"); rg && rg->type == JsonType::Array)
		{
			for (const JsonValue& r : rg->arrayValue)
			{
				RegionDef d;
				d.id = GetStr(r, "id"); d.name = GetStr(r, "name", d.id);
				d.eMin = static_cast<int>(GetNum(r, "e_min", 0)); d.eMax = static_cast<int>(GetNum(r, "e_max", 0));
				d.nMin = static_cast<int>(GetNum(r, "n_min", 0)); d.nMax = static_cast<int>(GetNum(r, "n_max", 0));
				if (d.id.empty() || d.eMin > d.eMax || d.nMin > d.nMax) { err = "master: region invalide '" + d.id + "'"; return false; }
				out.regions.push_back(std::move(d));
			}
		}

		// --- villes ---
		std::set<std::string> ids;
		static const std::set<std::string> kKinds = { "capitale", "grande_ville", "ville", "forteresse", "village", "hameau", "camp" };
		static const std::set<std::string> kRaces = { "humains", "elfes", "orcs", "nains", "demons", "neutre" };
		static const std::set<std::string> kStates = { "intact", "ruine", "camp" };
		const JsonValue* cs = FindObjectMember(root, "cities");
		if (!cs || cs->type != JsonType::Array || cs->arrayValue.empty()) { err = "master: 'cities' vide"; return false; }
		for (const JsonValue& c : cs->arrayValue)
		{
			CityDef d;
			d.id = GetStr(c, "id"); d.name = GetStr(c, "name", d.id); d.cell = GetStr(c, "cell");
			d.kind = GetStr(c, "kind"); d.race = GetStr(c, "race"); d.state = GetStr(c, "state", "intact");
			d.faction = GetStr(c, "faction", "neutre"); d.startFor = GetStrArray(c, "start_for");
			d.source = GetStr(c, "source"); d.description = GetStr(c, "description", d.name);
			if (d.id.empty() || !ids.insert(d.id).second) { err = "master: id de ville vide ou duplique '" + d.id + "'"; return false; }
			if (!ParseCellId(d.cell, d.n, d.e)) { err = "master: cellule invalide pour '" + d.id + "'"; return false; }
			if (!kKinds.count(d.kind)) { err = "master: kind inconnu pour '" + d.id + "' : " + d.kind; return false; }
			if (!kRaces.count(d.race)) { err = "master: race inconnue pour '" + d.id + "' : " + d.race; return false; }
			if (!kStates.count(d.state)) { err = "master: state inconnu pour '" + d.id + "' : " + d.state; return false; }
			out.cities.push_back(std::move(d));
		}
		// Une seule ville de départ par race.
		std::set<std::string> startRaces;
		for (const CityDef& c : out.cities)
			for (const std::string& r : c.startFor)
				if (!startRaces.insert(r).second) { err = "master: plusieurs villes de depart pour la race '" + r + "'"; return false; }

		// --- POI ---
		if (const JsonValue* ps = FindObjectMember(root, "pois"); ps && ps->type == JsonType::Array)
		{
			for (const JsonValue& p : ps->arrayValue)
			{
				PoiDef d;
				d.id = GetStr(p, "id"); d.name = GetStr(p, "name", d.id); d.cell = GetStr(p, "cell");
				d.kind = GetStr(p, "kind", "monolithe"); d.description = GetStr(p, "description");
				if (d.id.empty() || !ids.insert(d.id).second) { err = "master: id de POI vide ou duplique '" + d.id + "'"; return false; }
				if (!ParseCellId(d.cell, d.n, d.e)) { err = "master: cellule invalide pour POI '" + d.id + "'"; return false; }
				out.pois.push_back(std::move(d));
			}
		}

		// --- PNJ statiques déclarés à la main ---
		if (const JsonValue* is = FindObjectMember(root, "interactables"); is && is->type == JsonType::Array)
		{
			for (const JsonValue& i : is->arrayValue)
			{
				InteractableDef d;
				d.cityId = GetStr(i, "city");
				d.x = GetNum(i, "x", 0.0); d.z = GetNum(i, "z", 0.0);
				d.radius = GetNum(i, "radius", 1.5); d.npc = GetBool(i, "npc", true);
				d.label = GetStr(i, "label"); d.role = GetStr(i, "role"); d.message = GetStr(i, "message");
				d.mesh = GetStr(i, "mesh"); d.scale = GetNum(i, "scale", 1.0);
				d.rotXDeg = GetNum(i, "rot_x_deg", 0.0); d.yawDeg = GetNum(i, "yaw_deg", 0.0);
				d.dialogueId = GetStr(i, "dialogue_id"); d.npcTargetId = GetStr(i, "npc_target_id");
				d.dialogueLines = GetStrArray(i, "dialogue");
				if (d.label.empty()) { err = "master: interactable sans label"; return false; }
				if (!d.cityId.empty() && !ids.count(d.cityId)) { err = "master: interactable ancre sur ville inconnue '" + d.cityId + "'"; return false; }
				out.interactables.push_back(std::move(d));
			}
		}

		// --- lacs manuels ---
		if (const JsonValue* el = FindObjectMember(root, "extra_lakes"); el && el->type == JsonType::Array)
		{
			for (const JsonValue& l : el->arrayValue)
			{
				ExtraLakeDef d;
				d.id = GetStr(l, "id"); d.cityId = GetStr(l, "city");
				d.x = GetNum(l, "x", 0.0); d.z = GetNum(l, "z", 0.0);
				d.radiusM = GetNum(l, "radius_m", 20.0);
				if (d.id.empty() || d.radiusM < 4.0 || d.radiusM > 600.0) { err = "master: extra_lakes invalide '" + d.id + "'"; return false; }
				if (!d.cityId.empty() && !ids.count(d.cityId)) { err = "master: extra_lakes ancre sur ville inconnue '" + d.cityId + "'"; return false; }
				out.extraLakes.push_back(std::move(d));
			}
		}

		// --- imports ---
		if (const JsonValue* im = FindObjectMember(root, "imports"); im && im->type == JsonType::Object)
		{
			out.importZoneConfig = GetStr(*im, "zone_config");
			out.importScenery = GetStr(*im, "scenery");
			out.importBuildings = GetStr(*im, "buildings");
			out.importRespawn = GetStr(*im, "respawn_points");
			out.importSpawners = GetStr(*im, "spawners");
			out.importQuestPoi = GetStr(*im, "quest_poi");
			out.importAnchorCity = GetStr(*im, "anchor_city");
			if (!out.importAnchorCity.empty() && !ids.count(out.importAnchorCity))
			{
				err = "master: imports.anchor_city inconnue '" + out.importAnchorCity + "'"; return false;
			}
		}
		if (const JsonValue* zo = FindObjectMember(root, "zone_overrides"); zo && zo->type == JsonType::Object)
			out.zoneOverrides = *zo;
		else
			out.zoneOverrides.type = JsonType::Object;
		// --- cellules générées par défaut ---
		out.generatedCells = GetStrArray(root, "generated_cells");
		out.connectStartCities = GetBool(root, "connect_start_cities", false);
		out.corridorMarginM = std::clamp(GetNum(root, "corridor_margin_m", 300.0), 0.0, 4000.0);
		{
			std::set<std::string> seen;
			for (const std::string& c : out.generatedCells)
			{
				int n = 0, e = 0;
				if (!ParseCellId(c, n, e)) { err = "master: generated_cells invalide '" + c + "'"; return false; }
				if (!seen.insert(c).second) { err = "master: generated_cells duplique '" + c + "'"; return false; }
			}
		}
		// Chaque ville de départ doit être dans une cellule générée (sinon un
		// nouveau personnage apparaîtrait dans le vide).
		for (const CityDef& c : out.cities)
			if (!c.startFor.empty() && std::find(out.generatedCells.begin(), out.generatedCells.end(), c.cell) == out.generatedCells.end())
			{
				err = "master: la ville de depart '" + c.id + "' (" + c.cell + ") n'est pas dans generated_cells"; return false;
			}
		return true;
	}
}
