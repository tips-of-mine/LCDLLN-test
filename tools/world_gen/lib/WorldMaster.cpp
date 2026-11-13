// tools/world_gen — chargement et validation du master du monde.
//
// Politique STRICTE : toute incohérence (cellule invalide, id dupliqué,
// rangée de grille de mauvaise longueur, ville dans l'eau…) fait échouer la
// génération avec un message explicite. Mieux vaut un CI rouge qu'un monde
// silencieusement faux.

#include "WorldGenContext.h"

#include <zone_builder/JsonDocument.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

		/// Écart maximal (m, par axe) d'une position manuelle depuis le centre de la
		/// case (v1.0.95) : comme les POI décalés (v1.0.72), le plateau aplani reste à
		/// distance du bord (le plus grand, celui d'une capitale, fait ~300 m de rayon).
		constexpr double kMaxPositionM = 4300.0;

		/// Lit la position posée à la main (v1.0.95) d'une ville ou d'un POI : clé
		/// `position` = `{ "x_m": <est>, "z_m": <sud> }`, en mètres depuis le centre
		/// de la case. Absente ou null : \p has = false (comportement historique).
		/// \param what  libellé de l'entrée pour le message d'erreur (« ville 'id' »).
		/// \return false + \p err si la clé est mal formée ou hors de la case.
		bool ReadManualPosition(const JsonValue& o, const std::string& what, bool& has, double& x, double& z, std::string& err)
		{
			has = false; x = 0.0; z = 0.0;
			const JsonValue* v = FindObjectMember(o, "position");
			if (!v || v->type == JsonType::Null) return true;
			const JsonValue* jx = v->type == JsonType::Object ? FindObjectMember(*v, "x_m") : nullptr;
			const JsonValue* jz = v->type == JsonType::Object ? FindObjectMember(*v, "z_m") : nullptr;
			if (!jx || !jz || jx->type != JsonType::Number || jz->type != JsonType::Number)
			{
				err = "master: position invalide pour " + what + " (attendu { \"x_m\": nombre, \"z_m\": nombre })"; return false;
			}
			x = jx->numberValue; z = jz->numberValue;
			if (!std::isfinite(x) || !std::isfinite(z) || std::fabs(x) > kMaxPositionM || std::fabs(z) > kMaxPositionM)
			{
				err = "master: position hors de la case pour " + what + " (|x_m|, |z_m| <= 4300 m depuis le centre)"; return false;
			}
			has = true;
			return true;
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
		out.cellMap = engine::world::cells::CellMapFromWorldId(out.worldId).value_or(engine::world::cells::CellMap::Royaumes);

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
		static const std::set<std::string> kStates = { "intact", "ruine", "reconstruction", "camp" };
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
			if (!ReadManualPosition(c, "ville '" + d.id + "'", d.hasPosition, d.positionXM, d.positionZM, err)) return false;
			// Ville secrète (v1.0.107) : hors carte publique, sans route ; une ville de
			// départ ne peut pas l'être (les nouveaux personnages doivent la voir).
			d.secret = GetBool(c, "secret", false);
			if (d.secret && !d.startFor.empty()) { err = "master: la ville de depart '" + d.id + "' ne peut pas etre secrete"; return false; }
			out.cities.push_back(std::move(d));
		}
		// Une seule ville de départ par faction (v1.0.61 : start_for porte des
		// identifiants de faction de jeu, game/data/races/factions.json). Identifiant
		// non vide, [a-z0-9_] (clé de start_locations.json et de characters.faction_str).
		std::set<std::string> startFactions;
		for (const CityDef& c : out.cities)
			for (const std::string& f : c.startFor)
			{
				const bool valid = !f.empty() && std::all_of(f.begin(), f.end(), [](char ch) {
					return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'; });
				if (!valid) { err = "master: faction de depart invalide pour '" + c.id + "' : '" + f + "'"; return false; }
				if (!startFactions.insert(f).second) { err = "master: plusieurs villes de depart pour la faction '" + f + "'"; return false; }
			}

		// --- POI ---
		if (const JsonValue* ps = FindObjectMember(root, "pois"); ps && ps->type == JsonType::Array)
		{
			for (const JsonValue& p : ps->arrayValue)
			{
				PoiDef d;
				d.id = GetStr(p, "id"); d.name = GetStr(p, "name", d.id); d.cell = GetStr(p, "cell");
				d.kind = GetStr(p, "kind", "monolithe"); d.description = GetStr(p, "description");
				d.link = GetStr(p, "link");
				// Décalage depuis le centre de la case (v1.0.72) : le lieu et son
				// aplanissement (100 m) restent à plus de 700 m des bords de la case.
				d.offsetX = GetNum(p, "offset_x_m", 0.0); d.offsetZ = GetNum(p, "offset_z_m", 0.0);
				if (std::fabs(d.offsetX) > 4300.0 || std::fabs(d.offsetZ) > 4300.0)
				{
					err = "master: offset hors de la case pour POI '" + d.id + "' (|offset| <= 4300 m)"; return false;
				}
				if (d.id.empty() || !ids.insert(d.id).second) { err = "master: id de POI vide ou duplique '" + d.id + "'"; return false; }
				if (!ParseCellId(d.cell, d.n, d.e)) { err = "master: cellule invalide pour POI '" + d.id + "'"; return false; }
				if (!ReadManualPosition(p, "POI '" + d.id + "'", d.hasPosition, d.positionXM, d.positionZM, err)) return false;
				// Lien de portail « <monde>:<id> » : monde connu, différent du monde courant.
				if (!d.link.empty())
				{
					const size_t colon = d.link.find(':');
					const std::string linkWorld = colon == std::string::npos ? std::string() : d.link.substr(0, colon);
					if (d.kind != "portail" || colon == std::string::npos || colon + 1 >= d.link.size()
						|| !engine::world::cells::CellMapFromWorldId(linkWorld) || linkWorld == out.worldId)
					{
						err = "master: link invalide pour POI '" + d.id + "' (attendu : portail, \"<autre monde>:<id>\")"; return false;
					}
				}
				// Lieu secret (v1.0.110) : caché de places.json ; un portail relié ne peut
				// pas l'être (le shard et le client doivent connaître ses deux extrémités).
				d.secret = GetBool(p, "secret", false);
				if (d.secret && !d.link.empty()) { err = "master: le portail '" + d.id + "' ne peut pas etre secret"; return false; }
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

		// --- cours d'eau tracés à la main ---
		if (const JsonValue* rc = FindObjectMember(root, "river_courses"); rc && rc->type == JsonType::Array)
		{
			std::set<std::string> courseIds;
			for (const JsonValue& v : rc->arrayValue)
			{
				RiverCourseDef d;
				d.id = GetStr(v, "id"); d.name = GetStr(v, "name", d.id);
				d.cells = GetStrArray(v, "cells");
				d.widthM = GetNum(v, "width_m", 32.0);
				d.islandCity = GetStr(v, "island_city");
				if (d.id.empty() || !courseIds.insert(d.id).second || d.cells.size() < 2 || d.widthM < 6.0 || d.widthM > 200.0)
				{
					err = "master: river_courses invalide '" + d.id + "'"; return false;
				}
				for (const std::string& cell : d.cells)
				{
					int n = 0, e = 0;
					if (!ParseCellId(cell, n, e)) { err = "master: river_courses '" + d.id + "' : cellule invalide " + cell; return false; }
					const int r = s.northN - n, c = e - s.westE;
					if (r < 0 || c < 0 || r >= static_cast<int>(out.gridRows.size()) || c >= s.gridCols) { err = "master: river_courses '" + d.id + "' : " + cell + " hors grille"; return false; }
					const char code = out.gridRows[static_cast<size_t>(r)][static_cast<size_t>(c)];
					// Toutes les cases ont un code « rivière », sauf la dernière qui peut être
					// l'embouchure (côte, plage ou mer).
					const bool mouth = &cell == &d.cells.back() && (code == ',' || code == '_' || code == '-' || code == ' ');
					if (code != 'r' && code != 'R' && code != '&' && !mouth) { err = "master: river_courses '" + d.id + "' : " + cell + " sans code riviere dans la grille"; return false; }
					if (!d.cellNE.empty() && (std::abs(n - d.cellNE.back().first) > 1 || std::abs(e - d.cellNE.back().second) > 1 || (n == d.cellNE.back().first && e == d.cellNE.back().second)))
					{
						err = "master: river_courses '" + d.id + "' : " + cell + " non voisine de la case precedente"; return false;
					}
					d.cellNE.push_back({ n, e });
				}
				if (!d.islandCity.empty())
				{
					const auto it = std::find_if(out.cities.begin(), out.cities.end(), [&](const CityDef& c) { return c.id == d.islandCity; });
					if (it == out.cities.end() || std::find(d.cells.begin(), d.cells.end(), it->cell) == d.cells.end())
					{
						err = "master: river_courses '" + d.id + "' : island_city inconnue ou hors du cours '" + d.islandCity + "'"; return false;
					}
				}
				out.riverCourses.push_back(std::move(d));
			}
		}

		// --- reliefs locaux (v1.0.72) : colline sous une ville perchée, crevasse en cul-de-sac ;
		//     massif sans ville (v1.0.77) ancré sur une case ---
		if (const JsonValue* rl = FindObjectMember(root, "reliefs"); rl && rl->type == JsonType::Array)
		{
			std::set<std::string> reliefIds, anchored;
			for (const JsonValue& v : rl->arrayValue)
			{
				ReliefDef d;
				d.id = GetStr(v, "id"); d.type = GetStr(v, "type"); d.cityId = GetStr(v, "city");
				d.heightM = GetNum(v, "height_m", 0.0);
				d.topRadiusM = GetNum(v, "top_radius_m", 0.0);
				d.footRadiusM = GetNum(v, "foot_radius_m", 0.0);
				d.halfWidthM = GetNum(v, "half_width_m", 0.0);
				d.wallM = GetNum(v, "wall_m", 0.0);
				d.backM = GetNum(v, "back_m", 0.0);
				d.openToward = GetStr(v, "open_toward");
				d.mine = GetStr(v, "mine");
				d.depthM = GetNum(v, "depth_m", 0.0);
				d.floorRadiusM = GetNum(v, "floor_radius_m", 0.0);
				d.galleryHalfWidthM = GetNum(v, "gallery_half_width_m", 0.0);
				d.galleryWallM = GetNum(v, "gallery_wall_m", 0.0);
				// Massif (v1.0.77) : ancré sur une case (`cell`), sans ville ; jusqu'à
				// 600 m de haut et 7 km de rayon (il déborde sur les cases voisines :
				// MacroHeight est une fonction de la position, le raccord reste exact).
				if (d.type == "massif")
				{
					d.cell = GetStr(v, "cell");
					d.offsetXM = GetNum(v, "offset_x_m", 0.0);
					d.offsetZM = GetNum(v, "offset_z_m", 0.0);
					if (d.id.empty() || !reliefIds.insert(d.id).second || !d.cityId.empty() || !ParseCellId(d.cell, d.n, d.e))
					{
						err = "master: relief invalide '" + d.id + "' (id unique, massif : case valide, pas de ville)"; return false;
					}
					const engine::world::cells::CellPoint c0 = engine::world::cells::CellCenter({ d.n, d.e });
					d.center = { c0.x + d.offsetXM, c0.z + d.offsetZM };
					d.noiseSeed = HashString(d.id.c_str());
					d.phase = static_cast<double>(d.noiseSeed % 6283ull) / 1000.0;
					if (!(d.heightM > 0.0 && d.heightM <= 600.0 && d.footRadiusM >= 1500.0 && d.footRadiusM <= 7000.0
						&& std::fabs(d.offsetXM) <= 4000.0 && std::fabs(d.offsetZM) <= 4000.0))
					{
						err = "master: relief '" + d.id + "' : dimensions invalides"; return false;
					}
					out.reliefs.push_back(std::move(d));
					continue;
				}
				const auto city = std::find_if(out.cities.begin(), out.cities.end(), [&](const CityDef& c) { return c.id == d.cityId; });
				if (d.id.empty() || !reliefIds.insert(d.id).second || city == out.cities.end() || !anchored.insert(d.cityId).second)
				{
					err = "master: relief invalide '" + d.id + "' (id unique, ville connue, un relief par ville)"; return false;
				}
				// Le relief est dessiné au centre de la case de sa ville : une ville perchée
				// ou au fond d'une crevasse ne peut pas être déplacée à la main (v1.0.95).
				// Gouffre (v1.0.107) : il SUIT la ville (position manuelle admise).
				if (city->hasPosition && d.type != "gouffre")
				{
					err = "master: ville '" + d.cityId + "' ancree sur le relief '" + d.id + "' : position manuelle interdite"; return false;
				}
				d.n = city->n; d.e = city->e;
				const engine::world::cells::CellPoint ctr = engine::world::cells::CellCenter({ d.n, d.e });
				d.center = { ctr.x, ctr.z };
				if (d.type == "gouffre")
				{
					// Fosse creusée sous la ville ; la galerie est relue en positions globales.
					if (city->hasPosition) d.center = { ctr.x + city->positionXM, ctr.z + city->positionZM };
					d.noiseSeed = HashString(d.id.c_str());
					d.phase = static_cast<double>(d.noiseSeed % 6283ull) / 1000.0;
					const JsonValue* gal = FindObjectMember(v, "gallery");
					bool galOk = gal && gal->type == JsonType::Array && gal->arrayValue.size() >= 2 && gal->arrayValue.size() <= 32;
					if (galOk)
						for (const JsonValue& q : gal->arrayValue)
						{
							const JsonValue* qx = FindObjectMember(q, "x_m");
							const JsonValue* qz = FindObjectMember(q, "z_m");
							if (!qx || !qz || qx->type != JsonType::Number || qz->type != JsonType::Number) { galOk = false; break; }
							d.gallery.push_back({ d.center.x + qx->numberValue, d.center.z + qz->numberValue });
						}
					double reach = d.floorRadiusM * 1.13 + d.wallM;
					double total = 0.0;
					if (galOk)
					{
						d.galleryAlong.push_back(0.0);
						for (size_t k = 1; k < d.gallery.size(); ++k)
						{
							const double seg = std::hypot(d.gallery[k].x - d.gallery[k - 1].x, d.gallery[k].z - d.gallery[k - 1].z);
							galOk = galOk && seg >= 20.0;
							total += seg;
							d.galleryAlong.push_back(total);
						}
						for (const Vec2& q : d.gallery)
							reach = std::max(reach, std::hypot(q.x - d.center.x, q.z - d.center.z) + d.galleryHalfWidthM + d.galleryWallM);
						// Le fond de la galerie part du fond plat du gouffre.
						galOk = galOk && std::hypot(d.gallery.front().x - d.center.x, d.gallery.front().z - d.center.z) < d.floorRadiusM * 0.85;
					}
					d.footRadiusM = reach + 5.0; // boîte englobante (m) autour du centre
					const bool inCell = std::fabs(d.center.x - ctr.x) + d.footRadiusM <= 4700.0 && std::fabs(d.center.z - ctr.z) + d.footRadiusM <= 4700.0;
					if (!(galOk && inCell && d.depthM >= 40.0 && d.depthM <= 200.0 && d.floorRadiusM >= 150.0 && d.floorRadiusM <= 600.0
						&& d.wallM >= 15.0 && d.wallM <= 150.0 && d.galleryHalfWidthM >= 3.0 && d.galleryHalfWidthM <= 12.0
						&& d.galleryWallM >= 3.0 && d.galleryWallM <= 30.0))
					{
						err = "master: relief '" + d.id + "' : gouffre invalide (dimensions, galerie d'au moins 2 points partant du fond, tout dans la case)"; return false;
					}
					out.reliefs.push_back(std::move(d));
					continue;
				}
				// Tout le relief (pied, parois) reste dans la case : raccord inutile.
				bool shapeOk = d.heightM > 0.0 && d.heightM <= 400.0;
				if (d.type == "colline")
					shapeOk = shapeOk && d.topRadiusM > 0.0 && d.footRadiusM > d.topRadiusM + 100.0 && d.footRadiusM <= 4000.0;
				else if (d.type == "crevasse")
				{
					int on = 0, oe = 0;
					shapeOk = shapeOk && d.halfWidthM >= 60.0 && d.wallM >= 10.0 && d.backM >= 0.0
						&& d.footRadiusM > d.backM + d.halfWidthM + d.wallM
						&& d.footRadiusM * 1.16 + std::max(d.wallM, 0.4 * d.footRadiusM) <= 4000.0
						&& ParseCellId(d.openToward, on, oe) && (on != d.n || oe != d.e);
					if (shapeOk)
					{
						const engine::world::cells::CellPoint o = engine::world::cells::CellCenter({ on, oe });
						const double l = std::hypot(o.x - ctr.x, o.z - ctr.z);
						d.dir = { (o.x - ctr.x) / l, (o.z - ctr.z) / l };
					}
				}
				else
					shapeOk = false;
				if (!shapeOk) { err = "master: relief '" + d.id + "' : type ou dimensions invalides"; return false; }
				out.reliefs.push_back(std::move(d));
			}
			// Gouffres (v1.0.107) : altitude ABSOLUE du fond et de l'entrée, relevées sur
			// le relief SANS les gouffres (fonction pure du master : même valeur pour
			// toutes les cellules, raccord exact). Fond = point le plus bas du pourtour
			// (64 directions, à floor_radius_m × 1,13 + wall_m + 100 m) − depth_m : les
			// parois ont au moins depth_m partout.
			WorldMaster plain;
			bool plainReady = false;
			for (ReliefDef& d : out.reliefs)
			{
				if (d.type != "gouffre") continue;
				if (!plainReady)
				{
					plain.worldId = out.worldId; plain.cellMap = out.cellMap; plain.seed = out.seed;
					plain.scale = out.scale; plain.gridRows = out.gridRows; plain.legend = out.legend;
					for (const ReliefDef& r : out.reliefs)
						if (r.type != "gouffre") plain.reliefs.push_back(r);
					plainReady = true;
				}
				const double rim = d.floorRadiusM * 1.13 + d.wallM + 100.0;
				double lowest = 1e18;
				for (int k = 0; k < 64; ++k)
				{
					const double a = k * 6.283185307179586 / 64.0;
					lowest = std::min(lowest, MacroHeight(plain, d.center.x + std::cos(a) * rim, d.center.z + std::sin(a) * rim));
				}
				d.floorY = lowest - d.depthM;
				d.entranceY = MacroHeight(plain, d.gallery.back().x, d.gallery.back().z);
				// Sortie du fond : premier point (pas de 2 m) de la galerie au-delà du fond
				// lobé et de ses parois ; avant, le sol de la galerie reste au niveau du fond
				// (pas de marche à l'embouchure).
				d.galleryExitM = d.galleryAlong.back();
				for (double a = 0.0; a <= d.galleryAlong.back(); a += 2.0)
				{
					size_t k = 0;
					while (k + 2 < d.gallery.size() && d.galleryAlong[k + 1] < a) ++k;
					const double seg = d.galleryAlong[k + 1] - d.galleryAlong[k];
					const Vec2 p = d.gallery[k] + (d.gallery[k + 1] - d.gallery[k]) * ((a - d.galleryAlong[k]) / seg);
					const double dx = p.x - d.center.x, dz = p.z - d.center.z;
					if (std::hypot(dx, dz) > GouffreFloorRadiusAt(d, std::atan2(dz, dx)) + d.wallM) { d.galleryExitM = a; break; }
				}
				// Galerie praticable : elle monte (au moins 10 m) avec une pente moyenne
				// d'escalier taillé (≤ 30 %) ; le fond reste au-dessus de la mer.
				const double rise = d.entranceY - d.floorY, run = d.galleryAlong.back() - d.galleryExitM;
				if (rise < 10.0 || run < 50.0 || rise > 0.30 * run || d.floorY < out.scale.seaLevelM + 10.0)
				{
					err = "master: relief '" + d.id + "' : galerie du gouffre trop raide ou fond trop bas (denivele " + std::to_string(rise) + " m sur "
						+ std::to_string(run) + " m)"; return false;
				}
			}
		}

		// --- routes du lore (v1.0.105, tools/lore/sync_routes.py) ---
		if (const JsonValue* lr = FindObjectMember(root, "lore_roads"); lr && lr->type == JsonType::Array)
		{
			std::set<std::string> roadIds;
			for (const JsonValue& v : lr->arrayValue)
			{
				LoreRoadDef d;
				d.id = GetStr(v, "id"); d.type = GetStr(v, "type");
				d.fromId = GetStr(v, "from"); d.toId = GetStr(v, "to");
				d.waypoints = GetStrArray(v, "waypoints");
				if (d.id.empty() || !roadIds.insert(d.id).second || d.waypoints.size() < 2
					|| (d.type != "road" && d.type != "imperial_road" && d.type != "path"))
				{
					err = "master: lore_roads invalide '" + d.id + "' (id unique, type road|imperial_road|path, 2 cases au moins)"; return false;
				}
				// Extrémité : ville ou POI connus (vide = jonction ou case seule). Un lieu
				// secret (v1.0.110) n'est jamais extrémité : la route le révélerait sur la
				// carte (map_features.bin).
				for (const std::string* end : { &d.fromId, &d.toId })
				{
					if (!end->empty() && !ids.count(*end))
					{
						err = "master: lore_roads '" + d.id + "' : extremite inconnue '" + *end + "'"; return false;
					}
					bool secretEnd = false;
					for (const PoiDef& poi : out.pois) secretEnd = secretEnd || (poi.secret && poi.id == *end);
					for (const CityDef& c : out.cities) secretEnd = secretEnd || (c.secret && c.id == *end);
					if (secretEnd)
					{
						err = "master: lore_roads '" + d.id + "' : extremite secrete '" + *end + "'"; return false;
					}
				}
				for (const std::string& cell : d.waypoints)
				{
					int n = 0, e = 0;
					if (!ParseCellId(cell, n, e)) { err = "master: lore_roads '" + d.id + "' : cellule invalide " + cell; return false; }
					const int r = s.northN - n, c = e - s.westE;
					if (r < 0 || c < 0 || r >= static_cast<int>(out.gridRows.size()) || c >= s.gridCols)
					{
						err = "master: lore_roads '" + d.id + "' : " + cell + " hors grille"; return false;
					}
					d.cellNE.push_back({ n, e });
				}
				out.loreRoads.push_back(std::move(d));
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
		// --- cimetières (v1.0.111) : nombre fixe, cellules sans cimetière remplies ---
		if (const JsonValue* gv = FindObjectMember(root, "graveyards"); gv && gv->type == JsonType::Object)
		{
			const double target = GetNum(*gv, "target", 0.0);
			if (target < 0.0 || target > 100000.0 || target != std::floor(target)) { err = "master: graveyards.target invalide"; return false; }
			out.graveyardTarget = static_cast<int>(target);
			out.graveyardFillEmptyCells = GetBool(*gv, "fill_empty_cells", false);
			for (const std::string& id : GetStrArray(*gv, "without_graveyard"))
				if (id.empty() || !out.graveyardExcluded.insert(id).second)
				{
					err = "master: graveyards.without_graveyard vide ou duplique '" + id + "'"; return false;
				}
		}
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
