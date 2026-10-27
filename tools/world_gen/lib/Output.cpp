// tools/world_gen — écriture de toutes les sorties runtime.
//
// Sorties PAR CELLULE <c> = cell_nXXX_eYYY (relatives au content root, toutes
// DÉRIVÉES du master, NON versionnées : produites par lcdlln_world_gen,
// puis par la CI avant l'empaquetage à partir de l'étape 4) :
//   zones/<c>/terrain_height.r16h      heightmap 2049² (HAMP), 10 km de côté
//   zones/<c>/terrain_splat.slap       splat 2048² RGBA (herbe/terre/roche/neige)
//   zones/<c>/terrain_grass.grms       masque herbe 2048²
//   zones/<c>/zone.json                réglages client de la cellule (terrain, PNJ statiques…)
//   zones/<c>/spawners.json            spawners serveur (+ routines d'ambiance), zoneId = cellule
//   zones/<c>/respawn_points.txt       cimetières / auberges
//   zones/<c>/runtime_manifest.json    manifest (nom + id numérique de zone)
//   instances/zone_<c>/scatter.bin     décor instancié (LCSC v2, chunks de 500 m)
//   instances/zone_<c>/buildings.bin   bâtiments (LCBD)
//   instances/zone_<c>/water.bin       mer, lacs, rivières (WATR v3)
// Sorties GLOBALES :
//   buildings/templates/maison.json    maisons paramétriques (bibliothèque, versionnée)
//   world/<w>/places.json              villes, lieux, régions, cellules générées (UI client)
//   world/<w>/world_map.png            carte du continent (fenêtre Carte)
//   world/<w>/start_locations.json     points de départ par race (master)
//   world/<w>/quest_poi.json           positions des cibles de quêtes (minimap)

#include "WorldGenContext.h"
#include "HouseCatalog.h"
#include "PngWriter.h"
#include "Parallel.h"
#include "Random.h"

#include "src/client/world/instances/Buildings.h"
#include "src/client/world/instances/ScatterInstances.h"
#include "src/client/world/water/WaterSurfaces.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace tools::world_gen
{
	namespace
	{
		namespace fs = std::filesystem;
		using tools::zone_builder::FindObjectMember;
		using tools::zone_builder::JsonType;
		using tools::zone_builder::JsonValue;

		// ------------------------------------------------------------ JSON

		std::string Esc(const std::string& s)
		{
			std::string o;
			o.reserve(s.size() + 2);
			for (unsigned char c : s)
			{
				switch (c)
				{
				case '"': o += "\\\""; break;
				case '\\': o += "\\\\"; break;
				case '\n': o += "\\n"; break;
				case '\r': break;
				case '\t': o += "\\t"; break;
				default:
					if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
					else o.push_back(static_cast<char>(c));
				}
			}
			return o;
		}
		std::string Q(const std::string& s) { return "\"" + Esc(s) + "\""; }

		/// Nombre compact et stable : entier si entier, sinon ≤ 3 décimales.
		std::string Num(double v)
		{
			if (std::fabs(v) < 5e-4) return "0";
			if (std::fabs(v - std::round(v)) < 5e-4 && std::fabs(v) < 1e15)
			{
				char b[32]; std::snprintf(b, sizeof b, "%lld", static_cast<long long>(std::llround(v))); return b;
			}
			char b[48]; std::snprintf(b, sizeof b, "%.3f", v);
			std::string s = b;
			while (!s.empty() && s.back() == '0') s.pop_back();
			if (!s.empty() && s.back() == '.') s.pop_back();
			return s;
		}

		/// Sérialise un JsonValue avec clés TRIÉES (sortie déterministe).
		void DumpJson(const JsonValue& v, std::string& out, int indent)
		{
			const std::string pad(static_cast<size_t>(indent) * 2, ' ');
			const std::string pad2(static_cast<size_t>(indent + 1) * 2, ' ');
			switch (v.type)
			{
			case JsonType::Null: out += "null"; break;
			case JsonType::Bool: out += v.boolValue ? "true" : "false"; break;
			case JsonType::Number: out += Num(v.numberValue); break;
			case JsonType::String: out += Q(v.stringValue); break;
			case JsonType::Array:
			{
				out += "[";
				for (size_t i = 0; i < v.arrayValue.size(); ++i)
				{
					out += (i ? ", " : "");
					DumpJson(v.arrayValue[i], out, indent + 1);
				}
				out += "]";
				break;
			}
			case JsonType::Object:
			{
				std::vector<std::string> keys;
				for (const auto& kv : v.objectValue) keys.push_back(kv.first);
				std::sort(keys.begin(), keys.end());
				out += "{\n";
				for (size_t i = 0; i < keys.size(); ++i)
				{
					out += pad2 + Q(keys[i]) + ": ";
					DumpJson(v.objectValue.at(keys[i]), out, indent + 1);
					out += (i + 1 < keys.size()) ? ",\n" : "\n";
				}
				out += pad + "}";
				break;
			}
			}
		}

		JsonValue JStr(const std::string& s) { JsonValue v; v.type = JsonType::String; v.stringValue = s; return v; }
		JsonValue JNum(double d) { JsonValue v; v.type = JsonType::Number; v.numberValue = d; return v; }
		JsonValue JBool(bool b) { JsonValue v; v.type = JsonType::Bool; v.boolValue = b; return v; }
		JsonValue JObj() { JsonValue v; v.type = JsonType::Object; return v; }
		JsonValue& Child(JsonValue& o, const std::string& k)
		{
			auto it = o.objectValue.find(k);
			if (it == o.objectValue.end() || it->second.type != JsonType::Object)
				o.objectValue[k] = JObj();
			return o.objectValue[k];
		}
		/// Fusion profonde (b écrase a, objets fusionnés récursivement).
		void DeepMerge(JsonValue& a, const JsonValue& b)
		{
			if (b.type == JsonType::Null) return; // rien à fusionner (import absent)
			if (a.type != JsonType::Object || b.type != JsonType::Object) { a = b; return; }
			for (const auto& kv : b.objectValue)
			{
				auto it = a.objectValue.find(kv.first);
				if (it != a.objectValue.end() && it->second.type == JsonType::Object && kv.second.type == JsonType::Object)
					DeepMerge(it->second, kv.second);
				else
					a.objectValue[kv.first] = kv.second;
			}
		}

		bool WriteFile(const std::string& outputRoot, GenReport& rep, const std::string& rel, const std::string& text, std::string& err)
		{
			const fs::path p = fs::path(outputRoot) / rel;
			std::error_code ec;
			fs::create_directories(p.parent_path(), ec);
			std::ofstream f(p, std::ios::binary | std::ios::trunc);
			if (!f.good()) { err = "ecriture impossible : " + p.string(); return false; }
			f.write(text.data(), static_cast<std::streamsize>(text.size()));
			rep.writtenFiles.push_back(rel);
			return f.good();
		}
		bool WriteBytes(const std::string& outputRoot, GenReport& rep, const std::string& rel, const std::vector<uint8_t>& bytes, std::string& err)
		{
			return WriteFile(outputRoot, rep, rel, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()), err);
		}
		bool WriteFile(World& w, const std::string& rel, const std::string& text, std::string& err)
		{
			return WriteFile(w.outputRoot, w.report, rel, text, err);
		}
		bool WriteBytes(World& w, const std::string& rel, const std::vector<uint8_t>& bytes, std::string& err)
		{
			return WriteBytes(w.outputRoot, w.report, rel, bytes, err);
		}
		void PutU32(std::vector<uint8_t>& b, uint32_t v)
		{
			b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
		}

		// --------------------------------------------------- splat / herbe

		struct SplatTexel { double grass, dirt, rock, snow, grassMask; };

		/// Base de splat (herbe, terre, roche, neige, masque d'herbe) d'un code de
		/// couverture, sans les effets locaux (pente, eau, routes…).
		SplatTexel BaseSplat(char code, double h, double sea)
		{
			SplatTexel t{ 1.0, 0.0, 0.0, 0.0, 1.0 };
			switch (code)
			{
			case 'T': case 'R': case 'L': t = { 0.65, 0.35, 0.0, 0.0, 0.7 }; break;
			case 't': t = { 0.8, 0.2, 0.0, 0.0, 0.9 }; break;
			case '_': case ',':
			{
				// Sable seulement près de l'eau ; au-dessus, prairie littorale.
				const double sand = SmoothStep(sea + 6.0, sea + 1.5, h);
				t = { 0.85 - 0.75 * sand, 0.15 + 0.75 * sand, 0.0, 0.0, 0.8 * (1.0 - sand) };
				break;
			}
			case '~': t = { 0.0, 0.8, 0.2, 0.0, 0.0 }; break;
			case ':': case 'l': t = { 0.3, 0.55, 0.15, 0.0, 0.2 }; break;
			case '^': case 'M': t = { 0.45, 0.15, 0.4, 0.0, 0.5 }; break;
			case '*': t = { 0.05, 0.0, 0.35, 0.6, 0.0 }; break;
			case 'V': t = { 0.0, 0.35, 0.65, 0.0, 0.0 }; break;
			case '%': case '&': t = { 0.45, 0.55, 0.0, 0.0, 0.6 }; break;
			case ' ': case '-': t = { 0.0, 0.9, 0.1, 0.0, 0.0 }; break;
			default: break;
			}
			return t;
		}

		SplatTexel ComputeSplat(const World& w, Vec2 p)
		{
			const double h = w.HeightAt(p);
			const double slope = w.SlopeDegAt(p);
			const double sea = w.master.scale.seaLevelM;
			const double n = Fbm(p.x, p.z, 26.0, 3, w.master.seed + 501);
			// Fondu des frontières de biome sur ~120 m : moyenne de 5 échantillons.
			SplatTexel t{ 0, 0, 0, 0, 0 };
			const double off[5][2] = { { 0, 0 }, { 60, 0 }, { -60, 0 }, { 0, 60 }, { 0, -60 } };
			for (const auto& o : off)
			{
				const double sx = p.x + o[0], sz = p.z + o[1];
				// Fonction macro (et non la grille) : identique des deux côtés d'une
				// frontière de cellule → splat raccord.
				const char c = EffectiveCover(MacroBiome(w.master, sx, sz), sx, sz, w.master.seed);
				const SplatTexel b = BaseSplat(c, h, sea);
				t.grass += b.grass * 0.2; t.dirt += b.dirt * 0.2; t.rock += b.rock * 0.2; t.snow += b.snow * 0.2; t.grassMask += b.grassMask * 0.2;
			}
			// Variation de taches (terre affleurante dans l'herbe).
			const double patch = Clamp(n * 0.9, -0.25, 0.25);
			t.grass = std::max(0.0, t.grass - patch);
			t.dirt = std::max(0.0, t.dirt + patch);
			// Rivage et fonds : sable/terre mouillée.
			if (h < sea + 1.2) { const double k = SmoothStep(sea + 1.2, sea - 0.5, h); t.grass *= 1 - k; t.dirt += k; t.grassMask *= 1 - k; }
			// Neige en altitude (moins sur les pentes raides).
			const double snowK = SmoothStep(520.0, 600.0, h + n * 25.0) * (1.0 - SmoothStep(38.0, 55.0, slope));
			t.snow = std::max(t.snow, snowK);
			// Roche sur les pentes fortes.
			const double rockK = SmoothStep(26.0, 40.0, slope + n * 4.0);
			t.rock = std::max(t.rock, rockK);
			t.grassMask *= 1.0 - rockK;
			// Routes (bord légèrement diffus).
			const int ri = w.roadDist.IndexX(p.x), rj = w.roadDist.IndexZ(p.z);
			if (w.roadDist.InBounds(ri, rj))
			{
				const double rd = w.roadDist.At(ri, rj);
				const double k = SmoothStep(2.2, -0.8, rd);
				t.grass *= 1 - k; t.rock *= 1 - k; t.snow *= 1 - k; t.dirt = Lerp(t.dirt, 1.0, k);
				t.grassMask *= 1 - k;
			}
			// Rues, places, emprise urbaine.
			const int ui = w.urban.IndexX(p.x), uj = w.urban.IndexZ(p.z);
			if (w.urban.InBounds(ui, uj))
			{
				const uint8_t u = w.urban.At(ui, uj);
				if (u == 2) { t = { 0.05, 0.95, 0.0, 0.0, 0.0 }; }
				else if (u == 1) { t.grass *= 0.6; t.dirt += 0.35; t.grassMask *= 0.6; }
			}
			// Berges de rivière.
			const int vi = w.riverDist.IndexX(p.x), vj = w.riverDist.IndexZ(p.z);
			if (w.riverDist.InBounds(vi, vj) && w.riverDist.At(vi, vj) < 4.0f)
			{
				const double k = SmoothStep(4.0, 0.0, w.riverDist.At(vi, vj));
				t.dirt += 0.6 * k; t.grass *= 1 - 0.5 * k;
			}
			return t;
		}

		// ------------------------------------------------- carte du monde

		/// Couleur de terrain d'un point (altitude + biome), sans ombrage.
		void TerrainColor(char code, double h, double sea, double& r, double& g, double& b)
		{
			if (h < sea)
			{
				const double d = Clamp((sea - h) / 34.0, 0.0, 1.0);
				r = Lerp(86, 22, d); g = Lerp(150, 52, d); b = Lerp(176, 98, d);
				return;
			}
			switch (code)
			{
			case 'T': case 'R': case 'L': r = 58; g = 98; b = 52; break;
			case 't': r = 88; g = 124; b = 66; break;
			case '~': r = 214; g = 188; b = 128; break;
			case ':': case 'l': r = 176; g = 158; b = 112; break;
			case '%': case '&': r = 92; g = 104; b = 74; break;
			case 'V': r = 92; g = 70; b = 62; break;
			case '_': case ',':
				if (h < sea + 5.0) { r = 214; g = 200; b = 150; } else { r = 132; g = 160; b = 92; }
				break;
			default: r = 132; g = 160; b = 92; break;
			}
			const double alt = Clamp((h - 90.0) / 450.0, 0.0, 1.0);
			r = Lerp(r, 128, alt * 0.7); g = Lerp(g, 116, alt * 0.7); b = Lerp(b, 104, alt * 0.7);
			const double snow = SmoothStep(520.0, 600.0, h);
			r = Lerp(r, 238, snow); g = Lerp(g, 240, snow); b = Lerp(b, 245, snow);
		}

		/// Carte du CONTINENT (carrée, N² px) : relief macro ombré, rivières,
		/// lacs, routes, villes, cellules générées. Rendu parallèle par rangée.
		std::vector<uint8_t> RenderContinentMap(const GlobalWorld& g, int N, double originX, double originZ, double sizeM,
			const std::vector<World*>& cells)
		{
			std::vector<uint8_t> rgb(static_cast<size_t>(N) * N * 3);
			const double sea = g.master.scale.seaLevelM;
			const double px = sizeM / N;
			ParallelRows(N, [&](int j) {
				for (int i = 0; i < N; ++i)
				{
					const Vec2 p{ originX + (i + 0.5) * px, originZ + (j + 0.5) * px };
					// Hors de la grille du lore : haute mer (la carte est carrée).
					const bool outside = p.x < g.minX || p.x > g.maxX || p.z < g.minZ || p.z > g.maxZ;
					const double h = outside ? 6.0 : SampleBilinear(g.coarse, p.x, p.z);
					double r, gg, b;
					TerrainColor(MacroBiome(g.master, p.x, p.z), h, sea, r, gg, b);
					if (h >= sea)
					{
						const double hx = SampleBilinear(g.coarse, p.x + px, p.z) - SampleBilinear(g.coarse, p.x - px, p.z);
						const double hz = SampleBilinear(g.coarse, p.x, p.z + px) - SampleBilinear(g.coarse, p.x, p.z - px);
						const double shade = Clamp(1.0 + (-hx - hz) / (2.0 * px) * 2.5, 0.55, 1.35);
						r *= shade; gg *= shade; b *= shade;
					}
					uint8_t* o = &rgb[(static_cast<size_t>(j) * N + i) * 3];
					o[0] = static_cast<uint8_t>(Clamp(r, 0, 255));
					o[1] = static_cast<uint8_t>(Clamp(gg, 0, 255));
					o[2] = static_cast<uint8_t>(Clamp(b, 0, 255));
				}
			});
			auto plot = [&](Vec2 p, int rad, uint8_t r, uint8_t gg, uint8_t b) {
				const int ci = static_cast<int>((p.x - originX) / px), cj = static_cast<int>((p.z - originZ) / px);
				for (int dj = -rad; dj <= rad; ++dj)
					for (int di = -rad; di <= rad; ++di)
					{
						const int ii = ci + di, jj = cj + dj;
						if (ii < 0 || jj < 0 || ii >= N || jj >= N) continue;
						uint8_t* o = &rgb[(static_cast<size_t>(jj) * N + ii) * 3];
						o[0] = r; o[1] = gg; o[2] = b;
					}
			};
			for (const Lake& l : g.lakes)
				for (double a = 0.0; a < 6.2832; a += 0.3)
					for (double rr = 0.0; rr < l.radiusM; rr += px * 0.5)
						plot(l.center + Vec2{ std::cos(a), std::sin(a) } * rr, 0, 70, 132, 170);
			for (const River& rv : g.rivers)
				for (size_t k = 0; k < rv.points.size(); k += 10) plot(rv.points[k], 0, 70, 132, 170);
			for (const Road& rd : g.roads)
				for (size_t k = 0; k < rd.points.size(); k += 10) plot(rd.points[k], 0, 150, 112, 72);
			// Cellules générées : liseré clair (repère de l'avancement du mapping).
			for (const World* w : cells)
				for (double t = 0.0; t <= w->terrainSizeM; t += px)
				{
					plot({ w->terrainOriginX + t, w->terrainOriginZ }, 0, 235, 225, 190);
					plot({ w->terrainOriginX + t, w->terrainOriginZ + w->terrainSizeM }, 0, 235, 225, 190);
					plot({ w->terrainOriginX, w->terrainOriginZ + t }, 0, 235, 225, 190);
					plot({ w->terrainOriginX + w->terrainSizeM, w->terrainOriginZ + t }, 0, 235, 225, 190);
				}
			for (const CityDef& c : g.cities) plot(c.center, 2, 92, 64, 52);
			return rgb;
		}

		// --------------------------------------------- modèles de maisons

		std::string PartJson(const std::string& mesh, double x, double y, double z, double ry, bool solid)
		{
			return "{ \"mesh\": " + Q("meshes/props/" + mesh + ".gltf") + ", \"x\": " + Num(x) + ", \"y\": " + Num(y) +
				", \"z\": " + Num(z) + ", \"rx\": 0, \"ry\": " + Num(ry) + ", \"rz\": 0, \"scale\": 1, \"solid\": " +
				(solid ? "true" : "false") + ", \"collision_radius\": 0 }";
		}

		std::vector<std::string> HouseParts(const HouseSpec& s)
		{
			std::vector<std::string> parts;
			const double hw = s.widthM * 0.5, hd = s.depthM * 0.5;
			const int nx = s.widthM / 2, nz = s.depthM / 2;
			const uint64_t hs = HashString(s.id);
			const int floors = s.ruin ? 1 : s.floors;
			const int doorSeg = nx / 2;
			for (int f = 0; f < floors; ++f)
			{
				const double y = 3.0 * f;
				const bool brickLevel = s.style == WallStyle::Brick || (s.style == WallStyle::Mixed && f == 0);
				const char* straight = brickLevel ? "Wall_UnevenBrick_Straight" : (f % 2 ? "Wall_Plaster_WoodGrid" : "Wall_Plaster_Straight");
				const char* windowA = brickLevel ? "Wall_UnevenBrick_Window_Wide_Round" : "Wall_Plaster_Window_Wide_Round";
				const char* windowB = brickLevel ? "Wall_UnevenBrick_Window_Thin_Round" : "Wall_Plaster_Window_Thin_Round";
				const char* door = brickLevel ? "Wall_UnevenBrick_Door_Round" : "Wall_Plaster_Door_Round";
				const char* floorMesh = (f == 0) ? (brickLevel ? "Floor_Brick" : "Floor_WoodDark") : "Floor_WoodDark";
				for (int a = 0; a < nx; ++a)
					for (int b = 0; b < nz; ++b)
						if (f == 0 || !(a == 0 && b == 0)) // trémie d'escalier symbolique à l'étage
							parts.push_back(PartJson(floorMesh, -hw + 1 + 2 * a, y, -hd + 1 + 2 * b, 0, true));
				auto wallAt = [&](int side, int idx, double px, double pz, double ry) {
					const uint64_t r = Hash(hs, static_cast<uint64_t>(f), static_cast<uint64_t>(side), static_cast<uint64_t>(idx));
					if (s.ruin && side != 0 && ToUnit(r) < 0.33) return; // pan effondré
					if (s.ruin && side == 0 && idx != doorSeg && ToUnit(r) < 0.25) return;
					const char* mesh = straight;
					if (f == 0 && side == 0 && idx == doorSeg) mesh = door;
					else if (ToUnit(Mix64(r)) < (f == 0 ? 0.35 : 0.5)) mesh = (ToUnit(Mix64(r + 1)) < 0.6) ? windowA : windowB;
					parts.push_back(PartJson(mesh, px, y, pz, ry, true));
				};
				for (int a = 0; a < nx; ++a)
				{
					wallAt(0, a, -hw + 1 + 2 * a, -hd, 0);     // façade (porte)
					wallAt(1, a, -hw + 1 + 2 * a, hd, 180);    // arrière
				}
				for (int b = 0; b < nz; ++b)
				{
					wallAt(2, b, -hw, -hd + 1 + 2 * b, 90);    // gauche
					wallAt(3, b, hw, -hd + 1 + 2 * b, 270);    // droite
				}
				if (!s.ruin)
				{
					parts.push_back(PartJson("Corner_Exterior_Wood", -hw, y, -hd, 0, false));
					parts.push_back(PartJson("Corner_Exterior_Wood", hw, y, -hd, 270, false));
					parts.push_back(PartJson("Corner_Exterior_Wood", hw, y, hd, 180, false));
					parts.push_back(PartJson("Corner_Exterior_Wood", -hw, y, hd, 90, false));
				}
			}
			if (!s.ruin)
			{
				const double ry = 3.0 * floors;
				parts.push_back(PartJson("Roof_RoundTiles_" + std::to_string(s.widthM) + "x" + std::to_string(s.depthM), 0, ry, 0, 0, false));
				parts.push_back(PartJson("Roof_Front_Brick" + std::to_string(s.widthM), 0, ry, -hd, 180, false));
				parts.push_back(PartJson("Roof_Front_Brick" + std::to_string(s.widthM), 0, ry, hd, 0, false));
				// Mobilier du rez-de-chaussée (vie intérieure).
				parts.push_back(PartJson("Table_Large", hw - 1.2, 0, hd - 1.3, 0, true));
				parts.push_back(PartJson("Barrel", -hw + 0.6, 0, hd - 0.6, 0, true));
				if (std::string(s.id).rfind("forge", 0) == 0)
					parts.push_back(PartJson("Cauldron", -hw + 1.0, 0, 0.0, 0, true));
			}
			else
			{
				parts.push_back(PartJson("Prop_Brick1", -hw + 1.4, 0, 0.4, 25, false));
				parts.push_back(PartJson("Prop_Brick3", hw - 1.6, 0, -0.8, 70, false));
				parts.push_back(PartJson("Vase_Rubble_Medium", 0.3, 0, hd - 1.5, 10, false));
			}
			return parts;
		}

		void CollectTemplateMeshes(std::set<std::string>& out)
		{
			for (const HouseSpec& s : HouseCatalog())
				for (const std::string& p : HouseParts(s))
				{
					const size_t a = p.find("\"mesh\": \"") + 9;
					out.insert(p.substr(a, p.find('"', a) - a));
				}
		}
	}

	bool WriteHouseTemplates(const std::string& outputRoot, GenReport& report, std::string& err)
	{
		std::string j = "{\n  \"_comment\": \"GENERE par tools/world_gen (lcdlln_world_gen) depuis HouseCatalog.h — ne pas editer a la main : modifier le catalogue puis relancer le generateur. Maisons parametriques du kit modulaire (grille 2 m, etages 3 m, porte en facade z = -D/2).\",\n";
		j += "  \"type\": \"maison\",\n  \"displayName\": \"Maisons parametriques\",\n  \"variants\": {\n";
		const auto& cat = HouseCatalog();
		j += "    \"count\": " + std::to_string(cat.size()) + ",\n";
		for (size_t v = 0; v < cat.size(); ++v)
		{
			const std::vector<std::string> parts = HouseParts(cat[v]);
			j += "    \"" + std::to_string(v) + "\": {\n      \"id\": " + Q(cat[v].id) + ",\n      \"displayName\": " + Q(cat[v].displayName) + ",\n      \"parts\": {\n";
			j += "        \"count\": " + std::to_string(parts.size()) + ",\n";
			for (size_t k = 0; k < parts.size(); ++k)
				j += "        \"" + std::to_string(k) + "\": " + parts[k] + (k + 1 < parts.size() ? ",\n" : "\n");
			j += "      }\n    }" + std::string(v + 1 < cat.size() ? ",\n" : "\n");
		}
		j += "  }\n}\n";
		report.houseTemplates = cat.size();
		return WriteFile(outputRoot, report, "buildings/templates/maison.json", j, err);
	}

	bool WriteCellOutputs(World& w, std::string& err)
	{
		const std::string wid = w.master.worldId;
		const std::string zoneDir = "zones/" + w.cellName + "/";
		const std::string instDir = "instances/zone_" + w.cellName + "/";
		const WorldScale& s = w.master.scale;
		const int N = w.height.width;

		// ---- Validation : tous les meshes référencés doivent exister.
		{
			std::set<std::string> meshes;
			for (const ScatterDef& d : w.scatter) meshes.insert(d.mesh);
			CollectTemplateMeshes(meshes);
			for (const std::string& m : meshes)
				if (!fs::exists(fs::path(w.contentRoot) / m))
				{
					err = "mesh reference introuvable dans le content : " + m;
					return false;
				}
		}

		// ---- Heightmap r16h (N², noeuds aux bords : raccord exact avec les voisines).
		{
			std::vector<uint8_t> b;
			PutU32(b, 0x504D4148u); PutU32(b, static_cast<uint32_t>(N)); PutU32(b, static_cast<uint32_t>(N));
			b.reserve(b.size() + static_cast<size_t>(N) * N * 2u);
			for (int j = 0; j < N; ++j)
				for (int i = 0; i < N; ++i)
				{
					const double h = Clamp(w.height.At(i, j) / s.heightScaleM, 0.0, 1.0);
					const uint16_t v = static_cast<uint16_t>(std::lround(h * 65535.0));
					b.push_back(uint8_t(v & 0xFF)); b.push_back(uint8_t(v >> 8));
				}
			if (!WriteBytes(w, zoneDir + "terrain_height.r16h", b, err)) return false;
		}
		// ---- Splat + masque herbe (N-1)² (centres de texels), calcul parallèle.
		{
			const int T = N - 1;
			const double px = w.terrainSizeM / T;
			std::vector<uint8_t> splatPx(static_cast<size_t>(T) * T * 4), grassPx(static_cast<size_t>(T) * T);
			ParallelRows(T, [&](int j) {
				for (int i = 0; i < T; ++i)
				{
					const Vec2 p{ w.terrainOriginX + (i + 0.5) * px, w.terrainOriginZ + (j + 0.5) * px };
					const SplatTexel t = ComputeSplat(w, p);
					double sum = t.grass + t.dirt + t.rock + t.snow;
					if (sum <= 1e-6) sum = 1.0;
					// Quantification à somme exacte 255 (le reste va au canal dominant).
					int c[4] = { static_cast<int>(t.grass / sum * 255.0), static_cast<int>(t.dirt / sum * 255.0),
						static_cast<int>(t.rock / sum * 255.0), static_cast<int>(t.snow / sum * 255.0) };
					const int rest = 255 - (c[0] + c[1] + c[2] + c[3]);
					int dom = 0;
					for (int k = 1; k < 4; ++k) if (c[k] > c[dom]) dom = k;
					c[dom] += rest;
					const size_t q = static_cast<size_t>(j) * T + i;
					for (int k = 0; k < 4; ++k) splatPx[q * 4 + k] = static_cast<uint8_t>(c[k]);
					grassPx[q] = static_cast<uint8_t>(Clamp(t.grassMask * t.grass / sum, 0.0, 1.0) * 255.0);
				}
			});
			std::vector<uint8_t> splat, grass;
			PutU32(splat, 0x50414C53u); PutU32(splat, static_cast<uint32_t>(T)); PutU32(splat, static_cast<uint32_t>(T));
			PutU32(grass, 0x47524D53u); PutU32(grass, static_cast<uint32_t>(T)); PutU32(grass, static_cast<uint32_t>(T));
			splat.insert(splat.end(), splatPx.begin(), splatPx.end());
			grass.insert(grass.end(), grassPx.begin(), grassPx.end());
			if (!WriteBytes(w, zoneDir + "terrain_splat.slap", splat, err)) return false;
			if (!WriteBytes(w, zoneDir + "terrain_grass.grms", grass, err)) return false;
		}
		// ---- Décor instancié (scatter.bin v2) : trié par chunk de 500 m puis case de
		//      64 m puis mesh ; table de chunks pour le streaming client.
		{
			constexpr double kChunk = 500.0;
			const int chunks = static_cast<int>(std::lround(w.terrainSizeM / kChunk));
			engine::world::instances::ScatterFile sf;
			std::map<std::string, uint16_t> idx;
			for (const ScatterDef& d : w.scatter) idx[d.mesh] = 0;
			uint16_t k = 0;
			for (auto& kv : idx) { kv.second = k++; sf.meshPaths.push_back(kv.first); }
			auto chunkOf = [&](const ScatterDef* d) {
				const int cx = std::clamp(static_cast<int>(std::floor((d->position.x - w.terrainOriginX) / kChunk)), 0, chunks - 1);
				const int cz = std::clamp(static_cast<int>(std::floor((d->position.z - w.terrainOriginZ) / kChunk)), 0, chunks - 1);
				return cz * chunks + cx;
			};
			auto cellKey = [&](const ScatterDef* d) {
				const int cx = static_cast<int>(std::floor((d->position.x - w.terrainOriginX) / 64.0));
				const int cz = static_cast<int>(std::floor((d->position.z - w.terrainOriginZ) / 64.0));
				return std::make_pair(cz, cx);
			};
			std::vector<const ScatterDef*> order;
			for (const ScatterDef& d : w.scatter) order.push_back(&d);
			std::stable_sort(order.begin(), order.end(), [&](const ScatterDef* a, const ScatterDef* b) {
				const int ca = chunkOf(a), cb = chunkOf(b);
				if (ca != cb) return ca < cb;
				const auto ka = cellKey(a), kb = cellKey(b);
				if (ka != kb) return ka < kb;
				return idx[a->mesh] < idx[b->mesh];
			});
			sf.chunks.originX = static_cast<float>(w.terrainOriginX);
			sf.chunks.originZ = static_cast<float>(w.terrainOriginZ);
			sf.chunks.chunkSizeM = static_cast<float>(kChunk);
			sf.chunks.cols = static_cast<uint16_t>(chunks);
			sf.chunks.rows = static_cast<uint16_t>(chunks);
			sf.chunks.ranges.assign(static_cast<size_t>(chunks) * chunks, {});
			for (const ScatterDef* d : order)
			{
				const int c = chunkOf(d);
				auto& r = sf.chunks.ranges[static_cast<size_t>(c)];
				if (r.count == 0) r.first = static_cast<uint32_t>(sf.instances.size());
				++r.count;
				engine::world::instances::ScatterInstance in;
				in.meshIndex = idx[d->mesh];
				in.flags = d->flags;
				in.cullClass = d->cullClass;
				in.x = static_cast<float>(d->position.x);
				in.z = static_cast<float>(d->position.z);
				in.yOffset = static_cast<float>(d->yOffset);
				in.yawDeg = static_cast<float>(std::fmod(d->yawDeg + 720.0, 360.0));
				in.scale = static_cast<float>(d->scale);
				in.collisionRadius = static_cast<float>(d->collisionRadius);
				in.tiltDeg = static_cast<float>(d->tiltDeg);
				sf.instances.push_back(in);
			}
			w.report.scatterInstances = sf.instances.size();
			w.report.scatterMeshes = sf.meshPaths.size();
			if (!WriteBytes(w, instDir + "scatter.bin", engine::world::instances::SaveScatterBin(sf), err)) return false;
		}
		// ---- Bâtiments (buildings.bin).
		{
			std::vector<engine::world::instances::BuildingPlacement> pls;
			uint64_t n = 0;
			for (const BuildingPlacementDef& b : w.buildings)
			{
				engine::world::instances::BuildingPlacement p;
				p.guid = Hash(w.master.seed, HashString(b.cityId.c_str()), ++n) | 1ull;
				p.templateType = b.templateType;
				p.variantId = b.variantId;
				p.displayName = b.displayName;
				p.worldPosition = { static_cast<float>(b.position.x), static_cast<float>(b.y), static_cast<float>(b.position.z) };
				p.worldYawDeg = static_cast<float>(std::fmod(b.yawDeg + 720.0, 360.0));
				p.worldScale = 1.0f;
				pls.push_back(std::move(p));
			}
			w.report.buildings = pls.size();
			if (!WriteBytes(w, instDir + "buildings.bin", engine::world::instances::SaveBuildingsBin(pls), err)) return false;
		}
		// ---- Eau (water.bin v3) : mer (si la cellule en a), lacs, rivières.
		{
			engine::world::water::WaterScene scene;
			if (w.hasSea)
			{
				engine::world::water::LakeInstance ocean;
				ocean.name = "ocean";
				ocean.isOcean = true;
				ocean.waterLevelY = static_cast<float>(s.seaLevelM);
				ocean.turbidity = 0.35f;
				ocean.bottomColor = { 0.06f, 0.18f, 0.27f };
				// Légère marge : pas de couture visible entre deux cellules maritimes.
				const float x0 = static_cast<float>(w.terrainOriginX - 2.0), z0 = static_cast<float>(w.terrainOriginZ - 2.0);
				const float x1 = static_cast<float>(w.terrainOriginX + w.terrainSizeM + 2.0), z1 = static_cast<float>(w.terrainOriginZ + w.terrainSizeM + 2.0);
				const float y = ocean.waterLevelY;
				ocean.polygon = { { x0, y, z0 }, { x1, y, z0 }, { x1, y, z1 }, { x0, y, z1 } };
				scene.lakes.push_back(std::move(ocean));
			}
			for (const Lake& l : w.lakes)
			{
				if (!w.OwnsPoint(l.center)) continue;
				engine::world::water::LakeInstance li;
				li.name = l.name;
				li.waterLevelY = static_cast<float>(l.waterY);
				for (const Vec2& p : l.polygon) li.polygon.push_back({ static_cast<float>(p.x), li.waterLevelY, static_cast<float>(p.z) });
				scene.lakes.push_back(std::move(li));
			}
			for (const River& r : w.rivers)
			{
				engine::world::water::RiverInstance ri;
				ri.name = r.name;
				// Noeuds pris sur l'index GLOBAL (pair) et limités à la cellule + 12 m :
				// deux cellules voisines produisent des rubans qui se rejoignent
				// exactement au lieu de se chevaucher décalés.
				for (size_t k = (r.globalStart % 2 == 0) ? 0 : 1; k < r.points.size(); k += 2)
				{
					const Vec2 q = r.points[k];
					if (q.x < w.terrainOriginX - 12.0 || q.z < w.terrainOriginZ - 12.0
						|| q.x > w.terrainOriginX + w.terrainSizeM + 12.0 || q.z > w.terrainOriginZ + w.terrainSizeM + 12.0) continue;
					engine::world::water::RiverNode node;
					node.position = { static_cast<float>(r.points[k].x), static_cast<float>(r.waterY[k]), static_cast<float>(r.points[k].z) };
					node.widthMeters = static_cast<float>(r.widthM[k] + 2.0);
					node.depthMeters = 2.2f;
					ri.nodes.push_back(node);
				}
				if (ri.nodes.size() >= 2) scene.rivers.push_back(std::move(ri));
			}
			engine::world::water::OceanSectionData os;
			os.seaLevelMeters = static_cast<float>(s.seaLevelM);
			std::vector<uint8_t> bytes;
			std::string werr;
			if (!engine::world::water::SaveWaterBin(scene, os, bytes, werr)) { err = "water.bin : " + werr; return false; }
			if (!WriteBytes(w, instDir + "water.bin", bytes, err)) return false;
		}

		// ---- zone.json (réglages client de la cellule).
		{
			JsonValue root = JObj();
			root.objectValue["_comment"] = JStr("GENERE par tools/world_gen depuis world/" + wid + "/world_master.json — NE PAS EDITER A LA MAIN (relancer lcdlln_world_gen). Cellule " + w.cellName + " (10 km x 10 km, coordonnees GLOBALES, cf. docs/world/ROYAUMES.md).");
			JsonValue& rt = Child(Child(root, "render"), "terrain");
			rt.objectValue["heightmap"] = JStr(zoneDir + "terrain_height.r16h");
			rt.objectValue["splatmap"] = JStr(zoneDir + "terrain_splat.slap");
			rt.objectValue["grass_mask"] = JStr(zoneDir + "terrain_grass.grms");
			rt.objectValue["hole_mask"] = JStr("");
			rt.objectValue["grass_mask_visual_strength"] = JNum(0.35);
			JsonValue& tr = Child(root, "terrain");
			tr.objectValue["_comment"] = JStr("Cadrage de la cellule : world_size (m) couvert par la heightmap, origine = coin min (x, z) GLOBAL, altitude max encodee, resolution de la heightmap.");
			tr.objectValue["world_size"] = JNum(w.terrainSizeM);
			tr.objectValue["origin_x"] = JNum(w.terrainOriginX);
			tr.objectValue["origin_z"] = JNum(w.terrainOriginZ);
			tr.objectValue["height_scale"] = JNum(s.heightScaleM);
			tr.objectValue["heightmap_resolution"] = JNum(static_cast<double>(N));
			JsonValue& world = Child(root, "world");
			DeepMerge(world, w.importedWorldConfig);
			if (auto it = w.master.zoneOverrides.objectValue.find("world"); it != w.master.zoneOverrides.objectValue.end())
				DeepMerge(world, it->second);
			for (const auto& kv : w.master.zoneOverrides.objectValue)
				if (kv.first != "world") DeepMerge(Child(root, kv.first), kv.second);
			world.objectValue["places_path"] = JStr("world/" + wid + "/places.json");
			world.objectValue["scatter_path"] = JStr(instDir + "scatter.bin");
			JsonValue& cellInfo = Child(world, "cell");
			cellInfo.objectValue["name"] = JStr(w.cellName);
			cellInfo.objectValue["n"] = JNum(w.cell.n);
			cellInfo.objectValue["e"] = JNum(w.cell.e);
			cellInfo.objectValue["zone_id"] = JNum(static_cast<double>(w.zoneId));
			Child(world, "test_water").objectValue["enabled"] = JBool(false);
			Child(world, "scenery").objectValue["count"] = JNum(0);
			// Spawn par défaut : départ de la race dont la ville est dans la cellule.
			if (!w.starts.empty())
			{
				const StartLocationDef& st = w.starts.front();
				JsonValue sp = JObj();
				sp.objectValue["x"] = JNum(st.position.x);
				sp.objectValue["y"] = JNum(st.y);
				sp.objectValue["z"] = JNum(st.position.z);
				sp.objectValue["yaw_deg"] = JNum(st.yawDeg);
				sp.objectValue["pitch_deg"] = JNum(-10.0);
				world.objectValue["default_spawn"] = sp;
				Child(Child(root, "client"), "world").objectValue["default_spawn"] = sp;
			}
			// Interactibles (PNJ statiques, coffres…).
			JsonValue& its = Child(world, "interactables");
			its.objectValue["count"] = JNum(static_cast<double>(w.interactables.size()));
			for (size_t k = 0; k < w.interactables.size(); ++k)
			{
				const InteractableDef& d = w.interactables[k];
				JsonValue e = JObj();
				e.objectValue["x"] = JNum(d.x); e.objectValue["z"] = JNum(d.z);
				e.objectValue["radius"] = JNum(d.radius); e.objectValue["npc"] = JBool(d.npc);
				e.objectValue["label"] = JStr(d.label);
				if (!d.role.empty()) e.objectValue["role"] = JStr(d.role);
				if (!d.message.empty()) e.objectValue["message"] = JStr(d.message);
				if (!d.mesh.empty()) e.objectValue["mesh"] = JStr(d.mesh);
				e.objectValue["scale"] = JNum(d.scale);
				if (d.rotXDeg != 0.0) e.objectValue["rot_x_deg"] = JNum(d.rotXDeg);
				e.objectValue["yaw_deg"] = JNum(d.yawDeg);
				if (!d.dialogueId.empty()) e.objectValue["dialogue_id"] = JStr(d.dialogueId);
				if (!d.npcTargetId.empty()) e.objectValue["npc_target_id"] = JStr(d.npcTargetId);
				if (!d.dialogueLines.empty())
				{
					JsonValue dl = JObj();
					dl.objectValue["count"] = JNum(static_cast<double>(d.dialogueLines.size()));
					for (size_t q = 0; q < d.dialogueLines.size(); ++q) dl.objectValue[std::to_string(q)] = JStr(d.dialogueLines[q]);
					e.objectValue["dialogue"] = dl;
				}
				its.objectValue[std::to_string(k)] = e;
			}
			std::string text;
			DumpJson(root, text, 0);
			text += "\n";
			if (!WriteFile(w, zoneDir + "zone.json", text, err)) return false;
		}

		// ---- spawners.json (zoneId = identifiant numérique de la cellule).
		{
			std::string j = "{\n  \"_comment\": \"GENERE par tools/world_gen — cellule " + w.cellName + " : PNJ d'ambiance (routine : tour/patrol/post, schedule day/night/always) et faune. Positions GLOBALES. Les spawners sans 'routine' gardent l'IA de creature historique (errance + riposte). Voir docs/world/ROYAUMES.md.\",\n";
			j += "  \"zoneId\": " + std::to_string(w.zoneId) + ",\n  \"spawners\": [\n";
			for (size_t k = 0; k < w.spawners.size(); ++k)
			{
				const SpawnerDef& d = w.spawners[k];
				j += "    { \"id\": " + Q(w.cellName + "_" + d.id) + ", \"archetypeId\": " + std::to_string(d.archetypeId) +
					", \"position\": [" + Num(d.position.x) + ", " + Num(d.y) + ", " + Num(d.position.z) + "]" +
					", \"count\": " + std::to_string(d.count) + ", \"respawnSec\": " + std::to_string(d.respawnSec) +
					", \"leashDistanceMeters\": " + Num(d.leashM);
				if (!d.routineKind.empty())
				{
					j += ",\n      \"routine\": { \"kind\": " + Q(d.routineKind) + ", \"schedule\": " + Q(d.schedule.empty() ? "always" : d.schedule) +
						", \"pauseMinSec\": " + Num(d.pauseMinSec) + ", \"pauseMaxSec\": " + Num(d.pauseMaxSec) +
						", \"pauseChance\": " + Num(d.pauseChance);
					if (d.wanderRadiusM > 0.0) j += ", \"wanderRadiusMeters\": " + Num(d.wanderRadiusM);
					if (d.pingPong) j += ", \"pingPong\": true";
					if (d.hasFacePoint) j += ", \"face\": [" + Num(d.facePoint.x) + ", " + Num(d.facePoint.z) + "]";
					if (!d.path.empty())
					{
						j += ", \"path\": [";
						for (size_t q = 0; q < d.path.size(); ++q)
							j += (q ? ", [" : "[") + Num(d.path[q].x) + ", " + Num(d.path[q].z) + "]";
						j += "]";
					}
					j += " }";
				}
				// ETAPE4.4 — agenda de métier (cf. src/shardd/gameplay/ambient/DailyAgenda.h).
				if (!d.agendaProfession.empty() && d.agendaPlaces.count("home") != 0u)
				{
					j += ",\n      \"agenda\": { \"profession\": " + Q(d.agendaProfession) + ", \"places\": {";
					bool firstPlace = true;
					for (const auto& [place, pts] : d.agendaPlaces)
					{
						if (pts.empty()) continue;
						j += std::string(firstPlace ? " " : ", ") + Q(place) + ": [";
						firstPlace = false;
						for (size_t q = 0; q < pts.size(); ++q)
							j += (q ? ", [" : "[") + Num(pts[q].x) + ", " + Num(pts[q].z) + "]";
						j += "]";
					}
					j += " } }";
				}
				j += " }" + std::string(k + 1 < w.spawners.size() ? ",\n" : "\n");
			}
			j += "  ]\n}\n";
			if (!WriteFile(w, zoneDir + "spawners.json", j, err)) return false;
		}

		// ---- respawn_points.txt.
		{
			std::string t = "# GENERE par tools/world_gen — points de reapparition de la cellule " + w.cellName + " (positions GLOBALES).\n"
				"# Format : zone_id type x y z [faction] [neutral_radius_m] (cf. ServerApp::InitRespawnPoints).\n"
				"# zone_id = identifiant numerique de la cellule (" + std::to_string(w.zoneId) + ", WorldCells::CellZoneId).\n";
			for (const std::string& l : w.importedRespawnLines) t += l + "\n";
			for (const RespawnDef& r : w.respawns)
				t += std::to_string(w.zoneId) + " " + r.type + " " + Num(r.position.x) + " " + Num(r.y) + " " + Num(r.position.z) +
					(r.type == "graveyard" ? " " + r.faction : std::string()) + "\n";
			if (!WriteFile(w, zoneDir + "respawn_points.txt", t, err)) return false;
		}

		// ---- runtime_manifest.json.
		{
			std::string j = "{\n  \"lcdlln_runtime_manifest_version\": 3,\n  \"zone_id\": " + Q(w.cellName) +
				",\n  \"zone_numeric_id\": " + std::to_string(w.zoneId) +
				",\n  \"display_name\": " + Q(w.master.displayName + " - " + engine::world::cells::LoreCellId(w.cell)) +
				",\n  \"terrain_heightmap\": " + Q(zoneDir + "terrain_height.r16h") +
				",\n  \"terrain_splatmap\": " + Q(zoneDir + "terrain_splat.slap") +
				",\n  \"terrain_grass_mask\": " + Q(zoneDir + "terrain_grass.grms") +
				",\n  \"source_edit_format_version\": 1,\n  \"terrain_world_size_m\": " + Num(w.terrainSizeM) +
				",\n  \"texture_assets\": [],\n  \"exported_textures\": [],\n  \"texture_assets_source_missing\": [],\n  \"object_prefab_ids\": [],\n" +
				"  \"note\": \"GENERE par tools/world_gen depuis world/" + wid + "/world_master.json.\"\n}\n";
			if (!WriteFile(w, zoneDir + "runtime_manifest.json", j, err)) return false;
		}
		return true;
	}

	bool WriteGlobalOutputs(GlobalWorld& g, const std::vector<World*>& cells, std::string& err)
	{
		const std::string wid = g.master.worldId;
		const std::string dir = "world/" + wid + "/";
		// Carte carrée couvrant tout le continent.
		const double sizeM = std::max(g.maxX - g.minX, g.maxZ - g.minZ);
		const double originX = g.minX, originZ = g.minZ;

		// ---- places.json (UI client : carte, bannières, radar) — coordonnées GLOBALES.
		{
			std::vector<const CityDef*> all;
			for (const CityDef& c : g.cities) all.push_back(&c);
			for (const World* w : cells)
				for (const CityDef& c : w->cities)
					if (!c.procedural.empty()) all.push_back(&c);
			std::string j = "{\n  \"_comment\": \"GENERE par tools/world_gen — lieux du monde pour l'interface (carte du monde, bannieres d'entree, radar). Coordonnees GLOBALES (cellules de 10 km, origine = centre de cell_n000_e000).\",\n";
			j += "  \"schema_version\": 1,\n  \"world\": " + Q(wid) + ",\n  \"display_name\": " + Q(g.master.displayName) + ",\n";
			j += "  \"map_image\": " + Q(dir + "world_map.png") + ",\n";
			j += "  \"terrain\": { \"origin_x\": " + Num(originX) + ", \"origin_z\": " + Num(originZ) + ", \"size_m\": " + Num(sizeM) + " },\n";
			j += "  \"cell_size_m\": " + Num(engine::world::cells::kCellSizeMeters) + ",\n";
			j += "  \"cell_count\": " + std::to_string(cells.size()) + ",\n  \"cells\": [\n";
			for (size_t k = 0; k < cells.size(); ++k)
			{
				const World* w = cells[k];
				j += "    { \"name\": " + Q(w->cellName) + ", \"lore\": " + Q(engine::world::cells::LoreCellId(w->cell)) +
					", \"n\": " + std::to_string(w->cell.n) + ", \"e\": " + std::to_string(w->cell.e) +
					", \"zone_id\": " + std::to_string(w->zoneId) + ", \"x_min\": " + Num(w->terrainOriginX) + ", \"z_min\": " + Num(w->terrainOriginZ) +
					" }" + (k + 1 < cells.size() ? ",\n" : "\n");
			}
			j += "  ],\n  \"city_count\": " + std::to_string(all.size()) + ",\n  \"cities\": [\n";
			for (size_t k = 0; k < all.size(); ++k)
			{
				const CityDef& c = *all[k];
				std::string starts;
				for (size_t q = 0; q < c.startFor.size(); ++q) starts += (q ? ", " : "") + Q(c.startFor[q]);
				j += "    { \"id\": " + Q(c.id) + ", \"name\": " + Q(c.name) + ", \"kind\": " + Q(c.procedural.empty() ? c.kind : c.procedural) +
					", \"race\": " + Q(c.race) + ", \"state\": " + Q(c.state) + ", \"faction\": " + Q(c.faction) +
					", \"x\": " + Num(c.center.x) + ", \"z\": " + Num(c.center.z) +
					", \"radius_m\": " + Num(c.radiusM) + ", \"start_for\": [" + starts + "], \"source\": " + Q(c.source) +
					", \"description\": " + Q(c.description) + " }" + (k + 1 < all.size() ? ",\n" : "\n");
			}
			j += "  ],\n  \"poi_count\": " + std::to_string(g.pois.size()) + ",\n  \"pois\": [\n";
			for (size_t k = 0; k < g.pois.size(); ++k)
			{
				const PoiDef& p = g.pois[k];
				j += "    { \"id\": " + Q(p.id) + ", \"name\": " + Q(p.name) + ", \"kind\": " + Q(p.kind) + ", \"x\": " + Num(p.center.x) +
					", \"z\": " + Num(p.center.z) + ", \"description\": " + Q(p.description) + " }" + (k + 1 < g.pois.size() ? ",\n" : "\n");
			}
			j += "  ],\n  \"region_count\": " + std::to_string(g.master.regions.size()) + ",\n  \"regions\": [\n";
			const double C = engine::world::cells::kCellSizeMeters;
			for (size_t k = 0; k < g.master.regions.size(); ++k)
			{
				const RegionDef& r = g.master.regions[k];
				// Rectangle de cases : x de e_min à e_max, z de -n_max à -n_min (nord = -Z).
				j += "    { \"id\": " + Q(r.id) + ", \"name\": " + Q(r.name) + ", \"x_min\": " + Num((r.eMin - 0.5) * C) + ", \"x_max\": " + Num((r.eMax + 0.5) * C) +
					", \"z_min\": " + Num(-(r.nMax + 0.5) * C) + ", \"z_max\": " + Num(-(r.nMin - 0.5) * C) + " }" + (k + 1 < g.master.regions.size() ? ",\n" : "\n");
			}
			j += "  ]\n}\n";
			if (!WriteFile(g.outputRoot, g.report, dir + "places.json", j, err)) return false;
		}

		// ---- Carte du continent (PNG 2048²).
		{
			const int N = 2048;
			const std::vector<uint8_t> rgb = RenderContinentMap(g, N, originX, originZ, sizeM, cells);
			if (!WriteBytes(g.outputRoot, g.report, dir + "world_map.png", EncodePngRgb(rgb, N, N), err)) return false;
		}

		// ---- Points de départ par race (lus par le master à la création).
		{
			std::vector<const StartLocationDef*> starts;
			for (const World* w : cells) for (const StartLocationDef& s : w->starts) starts.push_back(&s);
			std::string j = "{\n  \"_comment\": \"GENERE par tools/world_gen — point d'apparition d'un NOUVEAU personnage selon sa race (positions GLOBALES). Repli : character_creation.default_spawn.* puis (0,100,0).\",\n";
			// Cellules générées (jouables) : lues par le shard (mur invisible côté serveur,
			// relocalisation d'une position hors monde). Identifiants du lore séparés par des virgules.
			std::string cellList;
			for (const World* w : cells) cellList += (cellList.empty() ? "" : ",") + engine::world::cells::LoreCellId(w->cell);
			j += "  \"schema_version\": 1,\n  \"world\": " + Q(wid) + ",\n  \"default_race\": \"humains\",\n";
			j += "  \"generated_cells\": " + Q(cellList) + ",\n  \"races\": {\n";
			for (size_t k = 0; k < starts.size(); ++k)
			{
				const StartLocationDef& st = *starts[k];
				const CityDef* c = g.FindCity(st.cityId);
				j += "    " + Q(st.race) + ": { \"city\": " + Q(st.cityId) + ", \"city_name\": " + Q(c ? c->name : st.cityId) +
					", \"cell\": " + Q(engine::world::cells::CellDirName(engine::world::cells::CellFromPosition(st.position.x, st.position.z))) +
					", \"x\": " + Num(st.position.x) + ", \"y\": " + Num(st.y) + ", \"z\": " + Num(st.position.z) +
					", \"yaw_deg\": " + Num(st.yawDeg) + " }" + (k + 1 < starts.size() ? ",\n" : "\n");
			}
			j += "  }\n}\n";
			if (!WriteFile(g.outputRoot, g.report, dir + "start_locations.json", j, err)) return false;
		}

		// ---- quest_poi.json (positions GLOBALES) : POI historiques translatés sur la
		//      ville d'ancrage + cibles des quêtes générées (créatures, PNJ).
		{
			std::map<std::string, std::vector<Vec2>> pois;
			if (!g.master.importQuestPoi.empty())
			{
				std::ifstream f(fs::path(g.contentRoot) / g.master.importQuestPoi, std::ios::binary);
				if (!f.good()) { err = "import quest_poi introuvable : " + g.master.importQuestPoi; return false; }
				std::stringstream ss; ss << f.rdbuf();
				JsonValue root; std::string perr;
				if (!tools::zone_builder::ParseJsonDocument(ss.str(), root, perr)) { err = "import quest_poi JSON : " + perr; return false; }
				Vec2 off{};
				if (const CityDef* a = g.FindCity(g.master.importAnchorCity)) off = a->center;
				for (const auto& kv : root.objectValue)
				{
					if (kv.second.type != JsonType::Array) continue;
					for (const JsonValue& pt : kv.second.arrayValue)
						if (pt.type == JsonType::Array && pt.arrayValue.size() >= 2)
							pois[kv.first].push_back({ pt.arrayValue[0].numberValue + off.x, pt.arrayValue[1].numberValue + off.z });
				}
			}
			for (const World* w : cells)
			{
				for (const SpawnerDef& d : w->spawners)
					if (d.id.rfind("quete_", 0) == 0) pois["mob:" + std::to_string(d.archetypeId)].push_back(d.position);
				for (const InteractableDef& d : w->interactables)
					if (!d.npcTargetId.empty() && d.npcTargetId.rfind("npc:", 0) == 0 && !pois.count(d.npcTargetId))
						pois[d.npcTargetId].push_back({ d.x, d.z });
			}
			std::string j = "{\n";
			size_t k = 0;
			for (const auto& kv : pois)
			{
				j += "  " + Q(kv.first) + ": [";
				for (size_t q = 0; q < kv.second.size(); ++q)
					j += (q ? ", [" : "[") + Num(std::round(kv.second[q].x)) + ", " + Num(std::round(kv.second[q].z)) + "]";
				j += "]" + std::string(++k < pois.size() ? ",\n" : "\n");
			}
			j += "}\n";
			if (!WriteFile(g.outputRoot, g.report, dir + "quest_poi.json", j, err)) return false;
		}
		return true;
	}
}
