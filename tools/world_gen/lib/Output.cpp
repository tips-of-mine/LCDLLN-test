// tools/world_gen — écriture de toutes les sorties runtime.
//
// Sorties PAR CELLULE <c> = cell_nXXX_eYYY (relatives au content root, toutes
// DÉRIVÉES du master, NON versionnées : produites par lcdlln_world_gen,
// puis par la CI avant l'empaquetage à partir de l'étape 4) :
//   zones/<c>/terrain_height.r16h      heightmap 2049² (HAMP), 10 km de côté
//   zones/<c>/terrain_splat.slap       splat 2048² RGBA (herbe/terre/roche/neige)
//   zones/<c>/terrain_grass.grms       masque herbe 2048²
//   zones/<c>/terrain_forest.frms      densité forestière 1024² (FORET.1 : arbres posés par le client)
//   zones/<c>/terrain_ground.gndm      nature du sol 2048² (SND.2b : sons de pas, client seul)
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
//   world/<w>/secret_places.json       villes et lieux secrets (annuaire MJ du shard, JAMAIS
//                                      livré au client — v1.0.110)
//   world/<w>/world_map.png            carte du continent (fenêtre Carte)
//   world/<w>/map_features.bin         routes, rues, bâtiments en vectoriel (FIX.12)
//   world/<w>/start_locations.json     points de départ par faction, repli par race (master, shard)
//   world/<w>/quest_poi.json           positions des cibles de quêtes (minimap)
//   world/<w>/portals.json             portails reliés à une autre carte (shard)
//   world/<w>/sea_lines.json           lignes maritimes : pontons, quais, trajets des bateaux (shard, v1.0.125)
// Sorties du PORTAIL WEB (PORTAL.1, seulement avec --portal-out <dossier>, HORS de game/data) :
//   <dossier>/portal_map.png           affiche 4096² de la zone jouable (sans secrets)
//   <dossier>/portal_map.json          étiquettes : villes importantes, régions (u, v normalisés)
//   <dossier>/places/<id>.png          PORTAL.2 : vue de dessus de chaque ville du lore / lieu remarquable
//   <dossier>/places/index.json        PORTAL.2 : index de la galerie (nom, genre, région, description)

#include "WorldGenContext.h"
#include "HouseCatalog.h"
#include "PngWriter.h"
#include "Parallel.h"
#include "Random.h"

#include "src/shared/world/instances/Buildings.h"
#include "src/shared/world/instances/ForestMask.h"
#include "src/shared/world/instances/ScatterInstances.h"
#include "src/shared/world/water/WaterSurfaces.h"
#include "src/shared/world/GroundMap.h"
#include "src/shared/world/LavaMask.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_set>

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

		/// SND.2b — ce qui a façonné un texel du splat, relevé au passage par
		/// ComputeSplat / InfernalSplat (lecture seule, le splat reste identique) pour
		/// en déduire la NATURE du sol (ClassifyGround) : route, rue, plage, marais…
		struct SplatTrace
		{
			bool infernal = false;
			char cover = 0;          ///< code de couverture au point même (Royaumes)
			double coastSand = 0.0;  ///< part de sable d'une côte (BaseSplat)
			double shoreK = 0.0;     ///< rivage / fond (0..1)
			double chalk = 0.0;      ///< paroi de craie (0..1)
			double roadK = 0.0;      ///< route (0..1)
			double riverK = 0.0;     ///< berge de rivière (0..1)
			uint8_t urban = 0;       ///< 1 = emprise urbaine, 2 = rue / place
			uint8_t paint = 0;       ///< 1 = roche de l'Oracle, 2 = pierre blanche (marbre)
		};

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
			case ' ': case '-':
				// v1.0.74 : terre émergée sous un code marin (une île de forêt entourée de
				// haut-fond n'a pas de case de côte) : traitée comme une côte (plage près
				// de l'eau, prairie littorale au-dessus) au lieu d'un fond nu.
				if (h >= sea)
				{
					const double sand = SmoothStep(sea + 6.0, sea + 1.5, h);
					t = { 0.85 - 0.75 * sand, 0.15 + 0.75 * sand, 0.0, 0.0, 0.8 * (1.0 - sand) };
				}
				else
					t = { 0.0, 0.9, 0.1, 0.0, 0.0 };
				break;
			default: break;
			}
			return t;
		}

		/// v1.0.83 — splat des Enfers (T15-22 : falaises rouges striées, basalte, cendre,
		/// lave). Le client n'a que 4 couches à couleur fixe (herbe, terre rouge-brun,
		/// roche gris-brun, neige) : aucune herbe, aucune neige, aucun masque d'herbe ;
		/// sol de terre rouge-brun (plaines, cendres, lave, chemins) et roche (pentes,
		/// crêtes, coins montagneux, hauts sommets cendreux).
		SplatTexel InfernalSplat(const World& w, Vec2 p, SplatTrace* trace = nullptr)
		{
			const double h = w.HeightAt(p);
			const double slope = w.SlopeDegAt(p);
			const double n = Fbm(p.x, p.z, 26.0, 3, w.master.seed + 501);
			// Fondu des frontières de biome sur ~120 m (même échantillonnage que ComputeSplat).
			double dirt = 0.0, rock = 0.0;
			const double off[5][2] = { { 0, 0 }, { 60, 0 }, { -60, 0 }, { 0, 60 }, { 0, -60 } };
			for (const auto& o : off)
			{
				const char c = MacroBiome(w.master, p.x + o[0], p.z + o[1]);
				// Croix volcanique : plaines de terre rouge et dalles rocheuses ; coins `M` : roche.
				if (c == 'V') { dirt += 0.6 * 0.2; rock += 0.4 * 0.2; }
				else { dirt += 0.3 * 0.2; rock += 0.7 * 0.2; }
			}
			// Taches (affleurements rocheux dans la terre, et inversement).
			const double patch = Clamp(n * 0.9, -0.25, 0.25);
			dirt = std::max(0.0, dirt + patch);
			rock = std::max(0.0, rock - patch);
			// Pentes fortes et hauts sommets (cendre grise au lieu de neige) : roche.
			const double rockK = std::max(SmoothStep(26.0, 40.0, slope + n * 4.0), SmoothStep(480.0, 580.0, h + n * 25.0));
			rock = std::max(rock, rockK);
			// v1.0.123 — lave (mers et fleuves) dans le canal A (« neige », toujours vide aux
			// Enfers) : le client la rend lumineuse (zone.json `render.terrain.lava_layer`) et
			// le serveur y tue en 3 s (lava_mask.lvmk, même valeur).
			double lava = InfernalLavaAt(w.master, p.x, p.z);
			// Chemins et emprise urbaine (Citadelle, mines, camps) : terre battue ; un chemin
			// passe la lave sur une croûte refroidie (pas de lave sous la route).
			const int ri = w.roadDist.IndexX(p.x), rj = w.roadDist.IndexZ(p.z);
			if (w.roadDist.InBounds(ri, rj))
			{
				const double k = SmoothStep(2.2, -0.8, w.roadDist.At(ri, rj));
				if (trace) trace->roadK = k;
				rock *= 1 - k; dirt = Lerp(dirt, 1.0, k);
				lava *= 1.0 - SmoothStep(6.0, 2.0, w.roadDist.At(ri, rj));
			}
			const int ui = w.urban.IndexX(p.x), uj = w.urban.IndexZ(p.z);
			if (w.urban.InBounds(ui, uj))
			{
				const uint8_t u = w.urban.At(ui, uj);
				if (trace) trace->urban = u;
				if (u == 2) { dirt = 0.95; rock = 0.05; lava = 0.0; }
				else if (u == 1) dirt += 0.35;
			}
			const double sum = std::max(1e-6, dirt + rock);
			return { 0.0, dirt / sum * (1.0 - lava), rock / sum * (1.0 - lava), lava, 0.0 };
		}

		/// Biome effectif (EffectiveCover(MacroBiome)) des échantillons du splat, calculé
		/// une seule fois par nœud d'une grille calée sur les centres de texels de la
		/// cellule (marge de 14 texels pour les échantillons à ±60 m), puis RÉUTILISÉ
		/// pour les points voisins SANS changer le résultat : la position déformée d'un
		/// point à distance d d'un nœud est connue à kMacroBiomeWarpLipschitz × d près ;
		/// si tout l'intervalle tombe dans la même case du lore (et si le bruit de
		/// EffectiveCover reste du même côté de son seuil), le code est celui du calcul
		/// direct ; sinon (bord de case, ≈ 3 % des points) on le calcule directement.
		/// Résultat identique au bit près à EffectiveCover(MacroBiome(...)), ≈ 4 fois
		/// moins de bruits évalués (v1.0.112, accélération de la génération).
		struct SplatBiomeGrid
		{
			/// Côté d'une case du lore (même constante que MacroBiomeFromWarp).
			static constexpr double kCellSizeM = engine::world::cells::kCellSizeMeters;
			const World* w = nullptr;
			double x0 = 0.0, z0 = 0.0, step = 1.0; ///< centre du nœud (0, 0), pas (m)
			int margin = 0, n = 0;                  ///< nœuds de marge, nœuds par côté
			std::vector<double> bx, bz;             ///< position déformée exacte du nœud
			std::vector<float> fA, fB;              ///< bruits de EffectiveCover au nœud

			/// Remplit la grille de la cellule \p cell (texels de \p px m). Effet de
			/// bord : alloue ≈ 100 Mo ; calcul parallèle par rangée.
			void Build(const World& cell, int texels, double px)
			{
				w = &cell;
				margin = 14;
				n = texels + 2 * margin;
				step = px;
				x0 = cell.terrainOriginX + (0.5 - margin) * px;
				z0 = cell.terrainOriginZ + (0.5 - margin) * px;
				const size_t count = static_cast<size_t>(n) * n;
				bx.assign(count, 0.0); bz.assign(count, 0.0); fA.assign(count, 0.0f); fB.assign(count, 0.0f);
				const uint64_t seed = cell.master.seed;
				ParallelRows(n, [&](int j) {
					for (int i = 0; i < n; ++i)
					{
						const double x = NodeX(i), z = NodeZ(j);
						const size_t q = static_cast<size_t>(j) * n + i;
						MacroBiomeWarp(cell.master, x, z, bx[q], bz[q]);
						fA[q] = static_cast<float>(Fbm(x, z, 3500.0, 2, seed + 77));
						fB[q] = static_cast<float>(Fbm(x, z, 2600.0, 2, seed + 78));
					}
				});
			}
			/// Abscisse du nœud i (même formule que les centres de texels du splat).
			double NodeX(int i) const { return w->terrainOriginX + ((i - margin) + 0.5) * step; }
			double NodeZ(int j) const { return w->terrainOriginZ + ((j - margin) + 0.5) * step; }

			/// EffectiveCover(MacroBiome(m, x, z), x, z, seed), au bit près.
			char Cover(double x, double z) const
			{
				const WorldMaster& m = w->master;
				const int i = std::clamp(static_cast<int>(std::lround((x - x0) / step)), 0, n - 1);
				const int j = std::clamp(static_cast<int>(std::lround((z - z0) / step)), 0, n - 1);
				const size_t q = static_cast<size_t>(j) * n + i;
				const double dx = x - NodeX(i), dz = z - NodeZ(j);
				const double d = std::fabs(dx) + std::fabs(dz); // ≥ distance euclidienne
				// Position déformée du point : celle du nœud + déplacement, ± pente × d
				// (+ 1 m de marge pour les arrondis du calcul flottant).
				const double e = kMacroBiomeWarpLipschitz * d + 1.0;
				const double cx = bx[q] + dx, cz = bz[q] + dz;
				const long e0 = std::lround((cx - e) / kCellSizeM), e1 = std::lround((cx + e) / kCellSizeM);
				const long n0 = std::lround(-(cz + e) / kCellSizeM), n1 = std::lround(-(cz - e) / kCellSizeM);
				const char code = (e0 == e1 && n0 == n1) ? LoreCode(m, static_cast<int>(n0), static_cast<int>(e0))
					: MacroBiome(m, x, z);
				// EffectiveCover : seuil du bruit tranché sur l'intervalle, sinon calcul direct.
				const double ef = kEffectiveCoverNoiseLipschitz * d + 1e-5;
				if (code == '.' || code == 'r' || code == 'p')
				{
					if (fA[q] - ef > 0.22) return 't';
					if (fA[q] + ef <= 0.22) return code;
				}
				else if (code == 'T' || code == 'R' || code == 'L')
				{
					if (fB[q] + ef < -0.45) return '.';
					if (fB[q] - ef >= -0.45) return code;
				}
				else
					return code;
				return EffectiveCover(code, x, z, m.seed);
			}
		};

		/// Mélange des 4 couches du sol (herbe, terre, roche, neige) au point global \p p
		/// de la cellule : biome fondu aux frontières, pente, altitude, eau, routes et
		/// emprise urbaine. Aux Enfers (v1.0.83), délègue à InfernalSplat (ni herbe ni
		/// neige). Fonction pure (lecture seule de \p w), appelée en parallèle par rangée.
		/// \param grid biomes précalculés de la cellule (résultat identique), ou nullptr.
		/// \param trace SND.2b : si non nul, reçoit les facteurs locaux (route, rue,
		///        rivage…) qui servent à ClassifyGround ; sans effet sur le résultat.
		SplatTexel ComputeSplat(const World& w, Vec2 p, const SplatBiomeGrid* grid = nullptr, SplatTrace* trace = nullptr)
		{
			// v1.0.83 : les Enfers ont leur propre palette (les Royaumes ne passent pas ici).
			if (IsInfernalMap(w.master))
			{
				if (trace) trace->infernal = true;
				return InfernalSplat(w, p, trace);
			}
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
				const char c = grid ? grid->Cover(sx, sz) : EffectiveCover(MacroBiome(w.master, sx, sz), sx, sz, w.master.seed);
				if (trace && o[0] == 0 && o[1] == 0)
				{
					trace->cover = c;
					// Même part de sable que BaseSplat (côte, plage, terre émergée sous code marin).
					if (c == '_' || c == ',' || ((c == ' ' || c == '-') && h >= sea))
						trace->coastSand = SmoothStep(sea + 6.0, sea + 1.5, h);
				}
				const SplatTexel b = BaseSplat(c, h, sea);
				t.grass += b.grass * 0.2; t.dirt += b.dirt * 0.2; t.rock += b.rock * 0.2; t.snow += b.snow * 0.2; t.grassMask += b.grassMask * 0.2;
			}
			// Variation de taches (terre affleurante dans l'herbe).
			const double patch = Clamp(n * 0.9, -0.25, 0.25);
			t.grass = std::max(0.0, t.grass - patch);
			t.dirt = std::max(0.0, t.dirt + patch);
			// Rivage et fonds : sable/terre mouillée.
			if (h < sea + 1.2) { const double k = SmoothStep(sea + 1.2, sea - 0.5, h); t.grass *= 1 - k; t.dirt += k; t.grassMask *= 1 - k; if (trace) trace->shoreK = k; }
			// Neige en altitude (moins sur les pentes raides).
			// v1.0.130 : limite des neiges des Royaumes vers 2 500 m (Pyrénées), avec ±60 m de bruit.
			const double snowK = SmoothStep(2450.0, 2650.0, h + n * 60.0) * (1.0 - SmoothStep(38.0, 55.0, slope));
			t.snow = std::max(t.snow, snowK);
			// Roche sur les pentes fortes.
			const double rockK = SmoothStep(26.0, 40.0, slope + n * 4.0);
			t.rock = std::max(t.rock, rockK);
			t.grassMask *= 1.0 - rockK;
			// v1.0.120 — paroi des falaises de craie : blanche (couche « neige », la seule
			// couche claire du client) au lieu de la roche, sur la bande de la paroi.
			if (const double wall = FalaiseWallAt(w.master, p.x, p.z); wall > 0.0)
			{
				const double chalk = wall * SmoothStep(22.0, 38.0, slope + n * 4.0);
				if (trace) trace->chalk = chalk;
				t.snow = std::max(t.snow, chalk);
				t.rock *= 1.0 - chalk; t.grass *= 1.0 - chalk; t.dirt *= 1.0 - chalk;
				t.grassMask *= 1.0 - chalk;
			}
			// Routes (bord légèrement diffus).
			const int ri = w.roadDist.IndexX(p.x), rj = w.roadDist.IndexZ(p.z);
			if (w.roadDist.InBounds(ri, rj))
			{
				const double rd = w.roadDist.At(ri, rj);
				const double k = SmoothStep(2.2, -0.8, rd);
				if (trace) trace->roadK = k;
				t.grass *= 1 - k; t.rock *= 1 - k; t.snow *= 1 - k; t.dirt = Lerp(t.dirt, 1.0, k);
				t.grassMask *= 1 - k;
			}
			// Rues, places, emprise urbaine.
			const int ui = w.urban.IndexX(p.x), uj = w.urban.IndexZ(p.z);
			if (w.urban.InBounds(ui, uj))
			{
				const uint8_t u = w.urban.At(ui, uj);
				if (trace) trace->urban = u;
				if (u == 2) { t = { 0.05, 0.95, 0.0, 0.0, 0.0 }; }
				else if (u == 1) { t.grass *= 0.6; t.dirt += 0.35; t.grassMask *= 0.6; }
			}
			// v1.0.136 — peinture (Camp de l'Oracle) : roche pâle du rocher, pierre blanche des escaliers.
			const int pi = w.paint.IndexX(p.x), pj = w.paint.IndexZ(p.z);
			if (w.paint.InBounds(pi, pj) && w.paint.At(pi, pj) != 0)
			{
				const uint8_t pv = w.paint.At(pi, pj);
				if (trace) trace->paint = pv;
				return pv == 2 ? SplatTexel{ 0.0, 0.0, 0.05, 0.95, 0.0 } : SplatTexel{ 0.0, 0.0, 0.45, 0.55, 0.0 };
			}
			// Berges de rivière.
			const int vi = w.riverDist.IndexX(p.x), vj = w.riverDist.IndexZ(p.z);
			if (w.riverDist.InBounds(vi, vj) && w.riverDist.At(vi, vj) < 4.0f)
			{
				const double k = SmoothStep(4.0, 0.0, w.riverDist.At(vi, vj));
				if (trace) trace->riverK = k;
				t.dirt += 0.6 * k; t.grass *= 1 - 0.5 * k;
			}
			return t;
		}

		/// SND.2b — nature du sol d'un texel (sons de pas) à partir de son splat \p t
		/// et des facteurs relevés par ComputeSplat (\p tr). Ordre de priorité : marbre /
		/// roche peints, rues et routes (terre battue), couche dominante neige / roche,
		/// berges et rivages (boue, sable), puis la couverture (désert, marais, terres
		/// arides, volcans, montagnes) et enfin herbe ou terre. Fonction pure.
		engine::world::GroundClass ClassifyGround(const SplatTexel& t, const SplatTrace& tr)
		{
			using engine::world::GroundClass;
			int dom = 0; // 0 herbe, 1 terre, 2 roche, 3 neige (même règle que le client : premier maximum)
			const double ch[4] = { t.grass, t.dirt, t.rock, t.snow };
			for (int k = 1; k < 4; ++k) if (ch[k] > ch[dom]) dom = k;
			if (tr.infernal)
			{
				// Canal A = lave : on n'y marche que sur la croûte refroidie (pierre).
				if (tr.urban == 2 || tr.roadK >= 0.5) return GroundClass::PackedEarth;
				return dom == 1 ? GroundClass::Dirt : GroundClass::Rock;
			}
			if (tr.paint == 2) return GroundClass::Marble;
			if (tr.paint == 1) return GroundClass::Rock;
			if (tr.urban == 2 || tr.roadK >= 0.5) return GroundClass::PackedEarth;
			if (dom == 3) return tr.chalk >= 0.5 ? GroundClass::Rock : GroundClass::Snow;
			if (dom == 2) return GroundClass::Rock;
			const char c = tr.cover;
			const bool sandy = c == '~' || c == '_' || c == ',' || c == ' ' || c == '-';
			if (tr.shoreK >= 0.5) return sandy ? GroundClass::Sand : GroundClass::Mud;
			if (tr.riverK >= 0.5) return GroundClass::Mud;
			if (c == '~' || tr.coastSand >= 0.5) return GroundClass::Sand;
			if (dom == 1)
			{
				if (c == '%' || c == '&') return GroundClass::Mud;
				if (c == ':' || c == 'l' || c == 'V' || c == '^' || c == 'M') return GroundClass::Gravel;
				return GroundClass::Dirt;
			}
			return GroundClass::Grass;
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
			// v1.0.74 : terre émergée sous un code marin (île entourée de haut-fond,
			// sans case de côte) : couleur de côte, comme dans BaseSplat.
			case '_': case ',': case ' ': case '-':
				if (h < sea + 5.0) { r = 214; g = 200; b = 150; } else { r = 132; g = 160; b = 92; }
				break;
			default: r = 132; g = 160; b = 92; break;
			}
			// v1.0.130 : teinte de roche jusqu'à 1 800 m, neige au-dessus de ≈ 2 500 m (Pyrénées).
			const double alt = Clamp((h - 120.0) / 1700.0, 0.0, 1.0);
			r = Lerp(r, 128, alt * 0.75); g = Lerp(g, 116, alt * 0.75); b = Lerp(b, 104, alt * 0.75);
			const double snow = SmoothStep(2450.0, 2650.0, h);
			r = Lerp(r, 238, snow); g = Lerp(g, 240, snow); b = Lerp(b, 245, snow);
		}

		/// v1.0.83 — couleur de terrain des Enfers (carte du monde), sans ombrage ni
		/// lave : roche rouge striée par l'altitude (T16 p16), coulées et dalles de
		/// basalte noir (T17 p16), cendre grise sur les hauts sommets (au lieu de la
		/// neige), coins montagneux plus sombres.
		/// \param code code de biome du lore (MacroBiome), \param h altitude (m)
		void InfernalTerrainColor(char code, double h, double x, double z, uint64_t seed, double& r, double& g, double& b)
		{
			if (code == 'V') { r = 150; g = 62; b = 40; }   // roche rouge
			else { r = 112; g = 48; b = 38; }               // chaîne : rouge sombre
			// Strates (falaises rouges striées) : bandes d'altitude de ~45 m.
			const double strata = 0.9 + 0.1 * std::sin(h / 7.0 + 2.0 * Fbm(x, z, 1200.0, 2, seed + 9103));
			r *= strata; g *= strata; b *= strata;
			// Basalte noir : coulées figées à l'échelle du kilomètre, plus en altitude.
			const double basalt = SmoothStep(0.05, 0.3, Fbm(x, z, 1400.0, 3, seed + 9104) + (code == 'V' ? 0.0 : 0.12)
				+ SmoothStep(300.0, 500.0, h) * 0.15);
			r = Lerp(r, 44, basalt); g = Lerp(g, 36, basalt); b = Lerp(b, 38, basalt);
			// Cendre grise sur les sommets.
			const double ash = SmoothStep(470.0, 590.0, h);
			r = Lerp(r, 128, ash); g = Lerp(g, 122, ash); b = Lerp(b, 118, ash);
		}

		/// Style de rendu de la carte du continent. Les valeurs par défaut donnent
		/// EXACTEMENT la carte du jeu (world_map.png) ; l'affiche du portail
		/// (PORTAL.1) et l'aperçu (--preview-map) les ajustent.
		/// v1.0.133 : la carte du jeu n'a plus ni liseré des cellules générées (il
		/// dessinait des traits droits sans rien de visible en jeu) ni carré brun
		/// sous les villes (le client pose déjà leurs noms et leurs bâtiments).
		struct ContinentMapStyle
		{
			bool cellOutlines = false;   ///< liseré des cellules générées (repère de mapping, outils seulement)
			bool continuousLines = false;///< rivières et routes en traits continus (sinon : 1 point sur 10)
			int riverRadius = 0;         ///< demi-épaisseur des rivières (px, traits continus)
			int roadRadius = 0;          ///< demi-épaisseur des routes (px, traits continus)
			int majorRoadRadius = 0;     ///< demi-épaisseur des grandes routes (px, traits continus)
			int cityRadius = -1;         ///< rayon du repère rond de ville (px) ; < 0 = aucun repère
		};

		/// Carte du CONTINENT (carrée, N² px) : relief macro ombré, rivières,
		/// lacs, routes, villes, cellules générées. Rendu parallèle par rangée.
		/// \param originX, originZ coin nord-ouest de l'emprise (m, repère global)
		/// \param sizeM côté de l'emprise (m)
		/// \param style options de rendu (défaut = carte du jeu)
		std::vector<uint8_t> RenderContinentMap(const GlobalWorld& g, int N, double originX, double originZ, double sizeM,
			const std::vector<World*>& cells, const ContinentMapStyle& style = {})
		{
			std::vector<uint8_t> rgb(static_cast<size_t>(N) * N * 3);
			const double sea = g.master.scale.seaLevelM;
			const double px = sizeM / N;
			const bool infernal = IsInfernalMap(g.master); // v1.0.83 : palette des Enfers
			ParallelRows(N, [&](int j) {
				for (int i = 0; i < N; ++i)
				{
					const Vec2 p{ originX + (i + 0.5) * px, originZ + (j + 0.5) * px };
					// Hors de la grille du lore : haute mer (la carte est carrée).
					const bool outside = p.x < g.minX || p.x > g.maxX || p.z < g.minZ || p.z > g.maxZ;
					const double h = outside ? 6.0 : SampleBilinear(g.coarse, p.x, p.z);
					double r, gg, b;
					const char code = MacroBiome(g.master, p.x, p.z);
					if (!infernal) TerrainColor(code, h, sea, r, gg, b);
					else if (outside || h < sea) { r = 30; gg = 18; b = 18; } // v1.0.83 : abîme hors de la carte des Enfers
					else InfernalTerrainColor(code, h, p.x, p.z, g.master.seed, r, gg, b);
					if (h >= sea)
					{
						const double hx = SampleBilinear(g.coarse, p.x + px, p.z) - SampleBilinear(g.coarse, p.x - px, p.z);
						const double hz = SampleBilinear(g.coarse, p.x, p.z + px) - SampleBilinear(g.coarse, p.x, p.z - px);
						const double shade = Clamp(1.0 + (-hx - hz) / (2.0 * px) * 2.5, 0.55, 1.35);
						r *= shade; gg *= shade; b *= shade;
					}
					// v1.0.120 : liseré blanc des falaises de craie (non ombré).
					if (!infernal && !outside && h >= sea)
						if (const double wall = FalaiseWallAt(g.master, p.x, p.z, 1.5 * px); wall > 0.0)
						{
							r = Lerp(r, 236, wall); gg = Lerp(gg, 234, wall); b = Lerp(b, 222, wall);
						}
					// v1.0.83 : lave jaune-orange NON ombrée (lumineuse), cœur jaune, bord orange-rouge.
					if (infernal && !outside)
					{
						const double lava = InfernalLavaAt(g.master, p.x, p.z);
						if (lava > 0.0)
						{
							const double core = SmoothStep(0.35, 1.0, lava);
							const double lr = Lerp(222, 255, core), lg = Lerp(84, 186, core), lb = Lerp(22, 48, core);
							r = Lerp(r, lr, lava); gg = Lerp(gg, lg, lava); b = Lerp(b, lb, lava);
						}
					}
					uint8_t* o = &rgb[(static_cast<size_t>(j) * N + i) * 3];
					o[0] = static_cast<uint8_t>(Clamp(r, 0, 255));
					o[1] = static_cast<uint8_t>(Clamp(gg, 0, 255));
					o[2] = static_cast<uint8_t>(Clamp(b, 0, 255));
				}
			});
			// v1.0.134 : bornes vérifiées AVANT la conversion en entier (un point non fini ou très
			// loin de l'image rendait la conversion indéfinie).
			auto plot = [&](Vec2 p, int rad, uint8_t r, uint8_t gg, uint8_t b) {
				const double fi = (p.x - originX) / px, fj = (p.z - originZ) / px;
				if (!(fi > -rad - 1.0 && fi < N + rad + 1.0 && fj > -rad - 1.0 && fj < N + rad + 1.0)) return;
				const int ci = static_cast<int>(fi), cj = static_cast<int>(fj);
				for (int dj = -rad; dj <= rad; ++dj)
					for (int di = -rad; di <= rad; ++di)
					{
						const int ii = ci + di, jj = cj + dj;
						if (ii < 0 || jj < 0 || ii >= N || jj >= N) continue;
						uint8_t* o = &rgb[(static_cast<size_t>(jj) * N + ii) * 3];
						o[0] = r; o[1] = gg; o[2] = b;
					}
			};
			// PORTAL.1 — segment continu (pas d'un demi-pixel) pour l'affiche du portail.
			auto line = [&](Vec2 a, Vec2 b, int rad, uint8_t r, uint8_t gg, uint8_t bl) {
				const double len = std::hypot(b.x - a.x, b.z - a.z);
				if (!std::isfinite(len) || len > 1e7) return; // v1.0.134 : segment aberrant ignoré
				const int steps = std::max(1, static_cast<int>(len / (px * 0.5)));
				for (int s = 0; s <= steps; ++s)
				{
					const double t = static_cast<double>(s) / steps;
					plot({ a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t }, rad, r, gg, bl);
				}
			};
			// Polyligne : traits continus (affiche) ou un point sur 10 (carte du jeu).
			auto polyline = [&](const std::vector<Vec2>& pts, int rad, uint8_t r, uint8_t gg, uint8_t bl) {
				if (!style.continuousLines)
				{
					for (size_t k = 0; k < pts.size(); k += 10) plot(pts[k], 0, r, gg, bl);
					return;
				}
				// Points espacés d'environ 4 à 6 m : un point sur 4 suffit à l'échelle de l'affiche.
				size_t prev = 0;
				for (size_t k = 4; k < pts.size(); k += 4) { line(pts[prev], pts[k], rad, r, gg, bl); prev = k; }
				if (!pts.empty() && prev + 1 < pts.size()) line(pts[prev], pts.back(), rad, r, gg, bl);
			};
			for (const Lake& l : g.lakes)
				for (double a = 0.0; a < 6.2832; a += 0.3)
					for (double rr = 0.0; rr < l.radiusM; rr += px * 0.5)
						plot(l.center + Vec2{ std::cos(a), std::sin(a) } * rr, 0, 70, 132, 170);
			for (const River& rv : g.rivers)
				polyline(rv.points, style.riverRadius, 70, 132, 170);
			for (const Road& rd : g.roads)
				polyline(rd.points, rd.major ? style.majorRoadRadius : style.roadRadius, 150, 112, 72);
			// Cellules générées : liseré clair (repère de l'avancement du mapping).
			if (style.cellOutlines)
			for (const World* w : cells)
				for (double t = 0.0; t <= w->terrainSizeM; t += px)
				{
					plot({ w->terrainOriginX + t, w->terrainOriginZ }, 0, 235, 225, 190);
					plot({ w->terrainOriginX + t, w->terrainOriginZ + w->terrainSizeM }, 0, 235, 225, 190);
					plot({ w->terrainOriginX, w->terrainOriginZ + t }, 0, 235, 225, 190);
					plot({ w->terrainOriginX + w->terrainSizeM, w->terrainOriginZ + t }, 0, 235, 225, 190);
				}
			// Repère de ville : disque (v1.0.133, plus de carré), jamais les villes secrètes (v1.0.107).
			if (style.cityRadius >= 0)
				for (const CityDef& c : g.cities)
				{
					if (c.secret) continue;
					const int ci = static_cast<int>((c.center.x - originX) / px), cj = static_cast<int>((c.center.z - originZ) / px);
					const int rad = style.cityRadius;
					for (int dj = -rad; dj <= rad; ++dj)
						for (int di = -rad; di <= rad; ++di)
						{
							if (di * di + dj * dj > rad * rad + rad) continue;
							const int ii = ci + di, jj = cj + dj;
							if (ii < 0 || jj < 0 || ii >= N || jj >= N) continue;
							uint8_t* o = &rgb[(static_cast<size_t>(jj) * N + ii) * 3];
							o[0] = 92; o[1] = 64; o[2] = 52;
						}
				}
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
				// Atelier décors : styles « planches » et « pierre sèche » (pièces dérivées du kit,
				// même géométrie, voir tools/asset_pipeline/decor/decor_specs.json).
				if (s.style == WallStyle::RedBrick)
				{
					straight = "Wall_RedBrick_Straight";
					windowA = "Wall_RedBrick_Window_Wide_Round";
					windowB = "Wall_RedBrick_Window_Thin_Round";
					door = "Wall_RedBrick_Door_Round";
				}
				if (s.style == WallStyle::Plank || s.style == WallStyle::DryStone)
				{
					const bool pl = s.style == WallStyle::Plank;
					straight = pl ? "Wall_Plank_Straight" : "Wall_DryStone_Straight";
					windowA = pl ? "Wall_Plank_Window_Wide_Round" : "Wall_DryStone_Window_Wide_Round";
					windowB = pl ? "Wall_Plank_Window_Thin_Round" : "Wall_DryStone_Window_Thin_Round";
					door = pl ? "Wall_Plank_Door_Round" : "Wall_DryStone_Door_Round";
				}
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
				const std::string roof = s.slateRoof ? "Roof_Slate_" : (s.style == WallStyle::Plank ? "Roof_Shingle_" : (s.style == WallStyle::DryStone ? "Roof_Thatch_" : "Roof_RoundTiles_"));
				const std::string gable = s.style == WallStyle::Plank ? "Roof_Front_Plank" : (s.style == WallStyle::DryStone ? "Roof_Front_DryStone" : "Roof_Front_Brick");
				parts.push_back(PartJson(roof + std::to_string(s.widthM) + "x" + std::to_string(s.depthM), 0, ry, 0, 0, false));
				parts.push_back(PartJson(gable + std::to_string(s.widthM), 0, ry, -hd, 180, false));
				parts.push_back(PartJson(gable + std::to_string(s.widthM), 0, ry, hd, 0, false));
				// Tourelle d'angle à flèche d'ardoise (atelier décors T2), maisons hautes.
				if (s.slateRoof && floors >= 3)
					parts.push_back(PartJson("Turret_Corner_1", -hw, 0, -hd, 0, true));
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

	std::string BuildBiomeGrid(const World& w, int size)
	{
		std::string g(static_cast<size_t>(size) * size, '.');
		const double step = w.terrainSizeM / size;
		for (int j = 0; j < size; ++j)
			for (int i = 0; i < size; ++i)
			{
				const double x = w.terrainOriginX + (i + 0.5) * step;
				const double z = w.terrainOriginZ + (j + 0.5) * step;
				const int bi = w.biome.IndexX(x), bj = w.biome.IndexZ(z);
				if (w.biome.InBounds(bi, bj))
					g[static_cast<size_t>(j) * size + i] = w.biome.At(bi, bj);
			}
		return g;
	}

	size_t CountSplatBiomeGridMismatches(const World& w, int stride)
	{
		const int T = w.master.scale.heightmapRes - 1;
		const double px = w.terrainSizeM / T;
		SplatBiomeGrid grid;
		grid.Build(w, T, px);
		const double off[5][2] = { { 0, 0 }, { 60, 0 }, { -60, 0 }, { 0, 60 }, { 0, -60 } };
		size_t bad = 0;
		for (int j = 0; j < T; j += std::max(1, stride))
			for (int i = 0; i < T; i += std::max(1, stride))
				for (const auto& o : off)
				{
					const double sx = w.terrainOriginX + (i + 0.5) * px + o[0], sz = w.terrainOriginZ + (j + 0.5) * px + o[1];
					if (grid.Cover(sx, sz) != EffectiveCover(MacroBiome(w.master, sx, sz), sx, sz, w.master.seed)) ++bad;
				}
		return bad;
	}

	std::string RespawnPointsText(const World& w)
	{
		std::string t = "# GENERE par tools/world_gen — points de reapparition de la cellule " + w.cellName + " (positions GLOBALES).\n"
			"# Format : zone_id type x y z [faction] [neutral_radius_m] [gy=<id>] (cf. ServerApp::InitRespawnPoints).\n"
			"# gy=<id> (cimetieres, v1.0.114) : identifiant stable du cimetiere (exploit des 666 cimetieres).\n"
			"# zone_id = identifiant numerique de la cellule (" + std::to_string(w.zoneId) + ", WorldCells::CellZoneId).\n";
		for (const std::string& l : w.importedRespawnLines) t += l + "\n";
		for (const RespawnDef& r : w.respawns)
		{
			t += std::to_string(w.zoneId) + " " + r.type + " " + Num(r.position.x) + " " + Num(r.y) + " " + Num(r.position.z);
			if (r.type == "graveyard")
			{
				// Faction toujours écrite avant gy= : l'ancien parseur (faction puis rayon
				// flottant) ignore alors le jeton final sans erreur.
				t += " " + (r.faction.empty() ? std::string("-") : r.faction);
				if (!r.graveyardId.empty()) t += " gy=" + r.graveyardId;
			}
			t += "\n";
		}
		return t;
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
			std::vector<uint8_t> groundPx(static_cast<size_t>(T) * T); // SND.2b — nature du sol
			// Biomes précalculés (résultat identique, ≈ 4 fois moins de bruits) ; les
			// Enfers ont leur propre palette (InfernalSplat), sans cette grille.
			SplatBiomeGrid grid;
			if (!IsInfernalMap(w.master)) grid.Build(w, T, px);
			const SplatBiomeGrid* gridPtr = IsInfernalMap(w.master) ? nullptr : &grid;
			ParallelRows(T, [&](int j) {
				for (int i = 0; i < T; ++i)
				{
					const Vec2 p{ w.terrainOriginX + (i + 0.5) * px, w.terrainOriginZ + (j + 0.5) * px };
					SplatTrace trace;
					const SplatTexel t = ComputeSplat(w, p, gridPtr, &trace);
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
					groundPx[q] = static_cast<uint8_t>(ClassifyGround(t, trace));
				}
			});
			std::vector<uint8_t> splat, grass;
			PutU32(splat, 0x50414C53u); PutU32(splat, static_cast<uint32_t>(T)); PutU32(splat, static_cast<uint32_t>(T));
			PutU32(grass, 0x47524D53u); PutU32(grass, static_cast<uint32_t>(T)); PutU32(grass, static_cast<uint32_t>(T));
			splat.insert(splat.end(), splatPx.begin(), splatPx.end());
			grass.insert(grass.end(), grassPx.begin(), grassPx.end());
			if (!WriteBytes(w, zoneDir + "terrain_splat.slap", splat, err)) return false;
			if (!WriteBytes(w, zoneDir + "terrain_grass.grms", grass, err)) return false;
			// SND.2b — nature du sol (même grille que le splat) pour les sons de pas.
			{
				engine::world::GroundMap gm;
				gm.size = static_cast<uint32_t>(T);
				gm.originX = w.terrainOriginX;
				gm.originZ = w.terrainOriginZ;
				gm.texelM = px;
				gm.classes = std::move(groundPx);
				if (!WriteBytes(w, zoneDir + "terrain_ground.gndm", engine::world::SaveGroundMap(gm), err)) return false;
			}
			// v1.0.123 — masque de lave des Enfers (serveur : mort en 3 s ; LORE_BD §16.49) :
			// canal A du splat réduit en blocs de F×F texels (moyenne, ≈ 20 m, 512² au plus :
			// ≈ 1,3 Mo par cellule au lieu de 20 Mo), plus l'altitude la plus haute du sol du bloc
			// (un joueur debout n'importe où dans le bloc est à moins de 1,5 m au-dessus).
			if (IsInfernalMap(w.master))
			{
				const int F = std::max(1, T / 512), S = T / F;
				std::vector<uint8_t> lavaPx(static_cast<size_t>(S) * S);
				std::vector<float> ground(lavaPx.size());
				bool any = false;
				ParallelRows(S, [&](int bj) {
					for (int bi = 0; bi < S; ++bi)
					{
						double sumL = 0.0, maxH = -1e30;
						for (int dj = 0; dj < F; ++dj)
							for (int di = 0; di < F; ++di)
							{
								const int i = bi * F + di, j = bj * F + dj;
								sumL += splatPx[(static_cast<size_t>(j) * T + i) * 4 + 3];
								maxH = std::max(maxH, w.HeightAt({ w.terrainOriginX + (i + 0.5) * px, w.terrainOriginZ + (j + 0.5) * px }));
							}
						const size_t q = static_cast<size_t>(bj) * S + bi;
						lavaPx[q] = static_cast<uint8_t>(std::lround(sumL / (F * F)));
						ground[q] = static_cast<float>(maxH);
					}
				});
				for (uint8_t v : lavaPx) any = any || v != 0;
				if (any)
				{
					engine::world::LavaMask lm;
					lm.size = static_cast<uint32_t>(S);
					lm.originX = w.terrainOriginX;
					lm.originZ = w.terrainOriginZ;
					lm.texelM = px * F;
					lm.lava = std::move(lavaPx);
					lm.groundY = std::move(ground);
					if (!WriteBytes(w, zoneDir + "lava_mask.lvmk", engine::world::SaveLavaMask(lm), err)) return false;
				}
			}
		}
		// ---- FORET.1 : densité forestière (le client y pose arbres et sous-bois).
		{
			engine::world::instances::ForestMask fm;
			fm.width = fm.height = static_cast<uint32_t>(w.forestMaskRes);
			fm.originX = w.terrainOriginX;
			fm.originZ = w.terrainOriginZ;
			fm.sizeM = w.terrainSizeM;
			fm.density = w.forestMask;
			if (fm.Empty()) { err = "carte de densite forestiere absente (BuildForestMask non appele)"; return false; }
			if (!WriteBytes(w, zoneDir + "terrain_forest.frms", engine::world::instances::SaveForestMask(fm), err)) return false;
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
			rt.objectValue["forest_mask"] = JStr(zoneDir + "terrain_forest.frms");
			rt.objectValue["ground_map"] = JStr(zoneDir + "terrain_ground.gndm"); // SND.2b — sons de pas
			rt.objectValue["hole_mask"] = JStr("");
			rt.objectValue["grass_mask_visual_strength"] = JNum(0.35);
			// v1.0.123 — Enfers : le canal A du splat est la lave (rendu lumineux, client).
			if (IsInfernalMap(w.master)) rt.objectValue["lava_layer"] = JBool(true);
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
			// SND.4 — biomes du lore par blocs de 250 m (ambiances sonores côté client).
			{
				JsonValue& au = Child(root, "audio");
				au.objectValue["_comment"] = JStr("SND.4 — biome_grid : codes de biome du lore (legende de grid.legend du master), biome_grid_size x biome_grid_size blocs, ligne 0 = Z minimal, colonne 0 = X minimal.");
				au.objectValue["biome_grid_size"] = JNum(static_cast<double>(kBiomeGridSize));
				au.objectValue["biome_grid"] = JStr(BuildBiomeGrid(w, kBiomeGridSize));
			}
			Child(world, "test_water").objectValue["enabled"] = JBool(false);
			Child(world, "scenery").objectValue["count"] = JNum(0);
			// Spawn par défaut : premier départ (faction) dont la ville est dans la cellule.
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
			// v1.0.107 — torches et ambiance du gouffre : DONNÉES pour le client, lues depuis
			// v1.0.118 (Engine::AppendCellLightsAndAmbiance : lumières de cellule de
			// DynamicLightSystem, assombrissement local de la passe Lighting). Clés absentes
			// des autres cellules.
			if (!w.pointLights.empty())
			{
				JsonValue& pl = Child(world, "point_lights");
				pl.objectValue["_comment"] = JStr("v1.0.107 — lumieres ponctuelles posees par le generateur (torches du gouffre), positions GLOBALES, couleur RGB 0-1. Lue par le client depuis v1.0.118 (lumieres de cellule de DynamicLightSystem, 64 plus proches de la camera).");
				pl.objectValue["count"] = JNum(static_cast<double>(w.pointLights.size()));
				for (size_t k = 0; k < w.pointLights.size(); ++k)
				{
					const PointLightDef& d = w.pointLights[k];
					JsonValue e = JObj();
					e.objectValue["id"] = JStr(d.id);
					e.objectValue["type"] = JStr(d.type);
					e.objectValue["x"] = JNum(d.position.x); e.objectValue["y"] = JNum(d.y); e.objectValue["z"] = JNum(d.position.z);
					e.objectValue["radius"] = JNum(d.radiusM);
					e.objectValue["intensity"] = JNum(d.intensity);
					e.objectValue["color_r"] = JNum(d.r); e.objectValue["color_g"] = JNum(d.g); e.objectValue["color_b"] = JNum(d.b);
					e.objectValue["auto_night_light"] = JBool(false); // allumees jour et nuit (fond sans soleil)
					pl.objectValue[std::to_string(k)] = e;
				}
			}
			if (!w.ambianceZones.empty())
			{
				JsonValue& az = Child(world, "ambiance_zones");
				az.objectValue["_comment"] = JStr("v1.0.107 — zones d'ambiance locales (fond du gouffre : sombre). darkness 0 = jour, 1 = nuit noire ; la zone couvre le disque (x, z, radius_m) entre floor_y et top_y. Lue par le client depuis v1.0.118 (soleil, ambiant et plafond d'exposition attenues).");
				az.objectValue["count"] = JNum(static_cast<double>(w.ambianceZones.size()));
				for (size_t k = 0; k < w.ambianceZones.size(); ++k)
				{
					const AmbianceZoneDef& d = w.ambianceZones[k];
					JsonValue e = JObj();
					e.objectValue["id"] = JStr(d.id);
					e.objectValue["label"] = JStr(d.label);
					e.objectValue["x"] = JNum(d.center.x); e.objectValue["z"] = JNum(d.center.z);
					e.objectValue["radius_m"] = JNum(d.radiusM);
					e.objectValue["floor_y"] = JNum(d.floorY); e.objectValue["top_y"] = JNum(d.topY);
					e.objectValue["darkness"] = JNum(d.darkness);
					az.objectValue[std::to_string(k)] = e;
				}
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
					// v1.0.142 — altitude réelle des points (graphe 3D de la ville-tour) : champs
					// ADDITIFS (un shard plus ancien les ignore et garde l'altitude du spawner).
					if (d.realHeight && !d.path.empty() && d.pathY.size() == d.path.size())
					{
						j += ", \"realHeight\": true, \"pathY\": [";
						for (size_t q = 0; q < d.pathY.size(); ++q)
							j += (q ? ", " : "") + Num(d.pathY[q]);
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

		// ---- respawn_points.txt (v1.0.114 : identifiant stable gy=<id> des cimetières).
		if (!WriteFile(w, zoneDir + "respawn_points.txt", RespawnPointsText(w), err)) return false;

		// ---- runtime_manifest.json.
		{
			std::string j = "{\n  \"lcdlln_runtime_manifest_version\": 3,\n  \"zone_id\": " + Q(w.cellName) +
				",\n  \"zone_numeric_id\": " + std::to_string(w.zoneId) +
				",\n  \"display_name\": " + Q(w.master.displayName + " - " + engine::world::cells::LoreCellId(w.cell)) +
				",\n  \"terrain_heightmap\": " + Q(zoneDir + "terrain_height.r16h") +
				",\n  \"terrain_splatmap\": " + Q(zoneDir + "terrain_splat.slap") +
				",\n  \"terrain_grass_mask\": " + Q(zoneDir + "terrain_grass.grms") +
				",\n  \"terrain_forest_mask\": " + Q(zoneDir + "terrain_forest.frms") +
				",\n  \"terrain_ground_map\": " + Q(zoneDir + "terrain_ground.gndm") +
				",\n  \"source_edit_format_version\": 1,\n  \"terrain_world_size_m\": " + Num(w.terrainSizeM) +
				",\n  \"texture_assets\": [],\n  \"exported_textures\": [],\n  \"texture_assets_source_missing\": [],\n  \"object_prefab_ids\": [],\n" +
				"  \"note\": \"GENERE par tools/world_gen depuis world/" + wid + "/world_master.json.\"\n}\n";
			if (!WriteFile(w, zoneDir + "runtime_manifest.json", j, err)) return false;
		}
		return true;
	}

	engine::world::mapfeatures::MapFeatures BuildMapFeatures(const GlobalWorld& g, const std::vector<World*>& cells)
	{
		namespace mf = engine::world::mapfeatures;
		// Pas minimal entre deux points gardés : à 250 m visibles (zoom maximal)
		// un segment de 10 m fait ~35 px, la courbure reste fluide.
		constexpr float kStepM = 10.0f;
		mf::MapFeatures f;
		auto addRoad = [&](const std::vector<Vec2>& pts, mf::MapRoadKind kind, double widthM)
		{
			if (pts.size() < 2u)
				return;
			std::vector<float> xz;
			xz.reserve(pts.size() * 2u);
			for (const Vec2& p : pts)
			{
				xz.push_back(static_cast<float>(p.x));
				xz.push_back(static_cast<float>(p.z));
			}
			mf::MapRoad r;
			r.kind = kind;
			r.widthM = static_cast<float>(widthM);
			r.xz = mf::DecimatePolyline(xz, kStepM);
			mf::ComputeBounds(r);
			f.roads.push_back(std::move(r));
		};
		// v1.0.136 — centres des camps de l'Oracle : les routes s'arrêtent au chemin de ronde
		// (comme sur le terrain, cf. BuildOracleCamp), pas de tracé sur le rocher.
		std::vector<Vec2> oracleCenters;
		for (const CityDef& c : g.cities)
			if (c.layout == "oracle") oracleCenters.push_back(c.center);
		auto insideOracle = [&](Vec2 p) {
			for (const Vec2& o : oracleCenters)
				if (std::hypot(p.x - o.x, p.z - o.z) < kOracleRoadStopM) return true;
			return false;
		};
		// Réseau inter-villes (polylignes globales complètes, coupées aux camps de l'Oracle).
		for (const Road& rd : g.roads)
		{
			const mf::MapRoadKind kind = rd.major ? mf::MapRoadKind::Major : (rd.path ? mf::MapRoadKind::Path : mf::MapRoadKind::Road);
			if (oracleCenters.empty()) { addRoad(rd.points, kind, rd.widthM); continue; }
			std::vector<Vec2> piece;
			for (const Vec2& p : rd.points)
			{
				if (insideOracle(p)) { addRoad(piece, kind, rd.widthM); piece.clear(); continue; }
				piece.push_back(p);
			}
			addRoad(piece, kind, rd.widthM);
		}
		for (const World* w : cells)
		{
			// Chemins locaux de la cellule (les tronçons de routes globales y sont
			// des découpes de g.roads, déjà exportées ci-dessus ; v1.0.105 : les
			// sentiers du lore, Road::loreId, aussi).
			for (const Road& rd : w->roads)
				if (rd.path && rd.loreId.empty())
					addRoad(rd.points, mf::MapRoadKind::Path, rd.widthM);
			// Rues : chaque arête du graphe (exportée une seule fois, a < b).
			// Ville secrète (v1.0.107) : ni ses rues ni ses bâtiments sur la carte zoomée.
			auto secretAt = [&](Vec2 p) {
				for (const CityDef& c : w->cities)
					if (c.secret && std::hypot(p.x - c.center.x, p.z - c.center.z) < c.radiusM + 60.0) return true;
				return false;
			};
			for (const SettlementLayout& L : w->layouts)
				for (size_t a = 0; a < L.nodes.size(); ++a)
					if (const CityDef* lc = w->FindCity(L.cityId); !(lc && lc->secret))
					for (int b : L.nodes[a].links)
						if (b > static_cast<int>(a) && static_cast<size_t>(b) < L.nodes.size())
							addRoad({ L.nodes[a].p, L.nodes[static_cast<size_t>(b)].p }, mf::MapRoadKind::Street, 3.0);
			for (const BuildingPlacementDef& b : w->buildings)
			{
				if (secretAt(b.position)) continue;
				mf::MapBuilding m;
				m.x = static_cast<float>(b.position.x);
				m.z = static_cast<float>(b.position.z);
				m.yawRad = static_cast<float>(b.yawDeg * 3.14159265358979323846 / 180.0);
				// Emprise inconnue (bâtiments historiques importés) : carré inscrit
				// dans le rayon d'encombrement.
				const double fallback = b.footprintRadius * 0.7071;
				m.halfW = static_cast<float>(b.halfW > 0.0 ? b.halfW : fallback);
				m.halfD = static_cast<float>(b.halfD > 0.0 ? b.halfD : fallback);
				f.buildings.push_back(m);
			}
		}
		// FIX.15 — rivières (polylignes globales, largeur médiane) et lacs (contours).
		for (const River& rv : g.rivers)
		{
			if (rv.points.size() < 2u)
				continue;
			std::vector<float> xz;
			xz.reserve(rv.points.size() * 2u);
			for (const Vec2& p : rv.points)
			{
				xz.push_back(static_cast<float>(p.x));
				xz.push_back(static_cast<float>(p.z));
			}
			std::vector<double> widths = rv.widthM;
			double width = 6.0;
			if (!widths.empty())
			{
				std::nth_element(widths.begin(), widths.begin() + static_cast<std::ptrdiff_t>(widths.size() / 2u), widths.end());
				width = widths[widths.size() / 2u];
			}
			mf::MapWater w;
			w.kind = mf::MapWaterKind::River;
			w.widthM = static_cast<float>(width);
			w.xz = mf::DecimatePolyline(xz, kStepM);
			mf::ComputeBounds(w);
			f.waters.push_back(std::move(w));
		}
		// Lacs du lore (g.lakes, recopiés dans chaque cellule qu'ils recoupent) et
		// étangs procéduraux des cellules (w->lakes) : chaque lac une seule fois
		// (noms uniques).
		std::vector<const Lake*> allLakes;
		std::unordered_set<std::string> seenLakes;
		// Clé : nom + centre au mètre près (un lac sans nom reste distinct).
		auto lakeKey = [](const Lake& lk)
		{
			return lk.name + "@" + std::to_string(std::llround(lk.center.x)) + "," + std::to_string(std::llround(lk.center.z));
		};
		for (const Lake& lk : g.lakes)
			if (seenLakes.insert(lakeKey(lk)).second)
				allLakes.push_back(&lk);
		for (const World* cw : cells)
			for (const Lake& lk : cw->lakes)
				if (seenLakes.insert(lakeKey(lk)).second)
					allLakes.push_back(&lk);
		for (const Lake* lkp : allLakes)
		{
			const Lake& lk = *lkp;
			mf::MapWater w;
			w.kind = mf::MapWaterKind::Lake;
			if (lk.polygon.size() >= 3u)
			{
				for (const Vec2& p : lk.polygon)
				{
					w.xz.push_back(static_cast<float>(p.x));
					w.xz.push_back(static_cast<float>(p.z));
				}
			}
			else
			{
				// Lac sans contour : cercle de 24 côtés.
				for (int k = 0; k < 24; ++k)
				{
					const double a = k * (2.0 * 3.14159265358979323846 / 24.0);
					w.xz.push_back(static_cast<float>(lk.center.x + lk.radiusM * std::cos(a)));
					w.xz.push_back(static_cast<float>(lk.center.z + lk.radiusM * std::sin(a)));
				}
			}
			mf::ComputeBounds(w);
			f.waters.push_back(std::move(w));
		}
		return f;
	}

	namespace
	{
		/// PORTAL.1 — affiche du monde pour le portail web (vitrine du jeu).
		///
		/// Écrit dans \p dir (hors de game/data : jamais livrée au client ni au shard) :
		///   - `portal_map.png` : carte N² px de l'emprise des cellules générées
		///     (+ 4 % de marge), relief, eau, lave, rivières et routes en traits
		///     continus, SANS liseré de cellules ni repère de ville ;
		///   - `portal_map.json` : étiquettes posées par le portail par-dessus l'image
		///     (villes importantes, régions, quelques lieux remarquables ; coordonnées
		///     normalisées u, v ∈ [0, 1], u vers l'est, v vers le sud).
		/// Les villes et lieux SECRETS (v1.0.107, v1.0.110) en sont exclus, comme de la
		/// carte du jeu ; les cimetières n'y figurent pas.
		/// \param dir dossier de sortie (absolu ou relatif au répertoire courant), créé au besoin
		/// \param cells cellules générées (rendu du terrain)
		/// \param N côté maximal de l'image en pixels (2048 au plus pour une carte de moins de 200 km)
		/// Effet de bord : écrit deux fichiers ; ajoute leurs chemins au bilan.
		bool WritePortalPoster(GlobalWorld& g, const std::vector<World*>& cells, const std::string& dir, int N, std::string& err)
		{
			// Emprise : cellules jouables du master (`generated_cells`, sans les couloirs
			// ni les cellules de mer ajoutés à la génération), carrée, + 4 % de marge.
			// Repli : toute la grille du lore.
			double x0 = 1e300, z0 = 1e300, x1 = -1e300, z1 = -1e300;
			for (const std::string& id : g.master.generatedCells)
				if (const std::optional<engine::world::cells::CellCoord> c = engine::world::cells::ParseLoreCellId(id))
				{
					const engine::world::cells::CellPoint mn = engine::world::cells::CellMinCorner(*c);
					x0 = std::min(x0, mn.x); z0 = std::min(z0, mn.z);
					x1 = std::max(x1, mn.x + engine::world::cells::kCellSizeMeters);
					z1 = std::max(z1, mn.z + engine::world::cells::kCellSizeMeters);
				}
			if (x0 > x1) { x0 = g.minX; z0 = g.minZ; x1 = g.maxX; z1 = g.maxZ; }
			const double side = std::max(x1 - x0, z1 - z0) * 1.08;
			const double originX = (x0 + x1) * 0.5 - side * 0.5, originZ = (z0 + z1) * 0.5 - side * 0.5;
			// Petite carte (Enfers, ~30 km) : le relief vient d'une grille de 250 m ;
			// au-delà de 2048 px on ne verrait que des pixels agrandis.
			if (side < 200000.0) N = std::min(N, 2048);
			// Épaisseurs : lisibles sur l'image entière (≈ 20 m/px pour 9 cellules,
			// ≈ 80 m/px pour le continent).
			ContinentMapStyle style;
			style.cellOutlines = false;
			style.continuousLines = true;
			style.riverRadius = 1;
			style.roadRadius = 1;
			style.majorRoadRadius = 2;
			style.cityRadius = -1; // repères posés par le portail avec les noms
			const std::vector<uint8_t> rgb = RenderContinentMap(g, N, originX, originZ, side, cells, style);
			if (!WriteBytes(dir, g.report, "portal_map.png", EncodePngRgb(rgb, static_cast<uint32_t>(N), static_cast<uint32_t>(N)), err)) return false;

			auto inside = [](double u, double v) { return u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0; };
			std::string j = "{\n  \"_comment\": \"GENERE par tools/world_gen --portal-out (PORTAL.1) — etiquettes de l'affiche du portail. Sans secrets.\",\n";
			j += "  \"schema_version\": 1,\n  \"world\": " + Q(g.master.worldId) + ",\n  \"display_name\": " + Q(g.master.displayName) + ",\n";
			j += "  \"image\": \"portal_map.png\",\n  \"size_px\": " + std::to_string(N) + ",\n";
			j += "  \"area\": { \"x_min\": " + Num(originX) + ", \"z_min\": " + Num(originZ) + ", \"size_m\": " + Num(side) + " },\n";
			// Villes : seules les importantes (les hameaux et villages chargeraient l'affiche).
			std::vector<std::string> cityLines;
			for (const CityDef& c : g.cities)
			{
				if (c.secret) continue;
				if (c.kind != "capitale" && c.kind != "grande_ville" && c.kind != "ville" && c.kind != "forteresse" && c.kind != "camp") continue;
				const double u = (c.center.x - originX) / side, v = (c.center.z - originZ) / side;
				if (!inside(u, v)) continue;
				cityLines.push_back("    { \"name\": " + Q(c.name) + ", \"kind\": " + Q(c.kind) + ", \"u\": " + Num(u) + ", \"v\": " + Num(v) + " }");
			}
			j += "  \"cities\": [\n";
			for (size_t k = 0; k < cityLines.size(); ++k) j += cityLines[k] + (k + 1 < cityLines.size() ? ",\n" : "\n");
			j += "  ],\n";
			// Régions : centre du rectangle de cases (nord = -Z, cf. places.json).
			const double C = engine::world::cells::kCellSizeMeters;
			std::vector<std::string> regionLines;
			for (const RegionDef& r : g.master.regions)
			{
				const double cx = (r.eMin + r.eMax) * 0.5 * C, cz = -(r.nMin + r.nMax) * 0.5 * C;
				const double u = (cx - originX) / side, v = (cz - originZ) / side;
				if (!inside(u, v)) continue;
				regionLines.push_back("    { \"name\": " + Q(r.name) + ", \"u\": " + Num(u) + ", \"v\": " + Num(v) + " }");
			}
			j += "  \"regions\": [\n";
			for (size_t k = 0; k < regionLines.size(); ++k) j += regionLines[k] + (k + 1 < regionLines.size() ? ",\n" : "\n");
			j += "  ],\n";
			// Lieux remarquables : volcans et cités ; temples aussi dans les Enfers (Palais
			// de Lucifer) mais pas dans les Royaumes (9 temples serrés sur l'île de la
			// Trêve : illisibles). Jamais les lieux secrets.
			const bool infernal = IsInfernalMap(g.master);
			std::vector<std::string> poiLines;
			for (const PoiDef& p : g.pois)
			{
				if (p.secret) continue;
				// v1.0.132 : les châteaux (Palais des Maîtres des Dragons, Garundel…) ont aussi leur étiquette ;
				// v1.0.140 : le palais impérial de Lhynn aussi.
				if (p.kind != "volcan" && p.kind != "cite" && p.kind != "chateau" && p.kind != "palais" && !(infernal && p.kind == "temple")) continue;
				const double u = (p.center.x - originX) / side, v = (p.center.z - originZ) / side;
				if (!inside(u, v)) continue;
				poiLines.push_back("    { \"name\": " + Q(p.name) + ", \"kind\": " + Q(p.kind) + ", \"u\": " + Num(u) + ", \"v\": " + Num(v) + " }");
			}
			j += "  \"pois\": [\n";
			for (size_t k = 0; k < poiLines.size(); ++k) j += poiLines[k] + (k + 1 < poiLines.size() ? ",\n" : "\n");
			j += "  ]\n}\n";
			return WriteFile(dir, g.report, "portal_map.json", j, err);
		}

		/// PORTAL.2 — index de la galerie : `<dir>/index.json`, une entrée par vignette
		/// écrite par WritePortalVignettes (nom, genre, région, description du lore,
		/// fichier), triée par région, ville avant lieu, puis par nom.
		bool WritePortalPlacesIndex(GlobalWorld& g, const std::vector<World*>& cells, const std::string& dir, std::string& err)
		{
			struct Entry { std::string region; const World::PortalVignette* v; };
			std::vector<Entry> entries;
			const double C = engine::world::cells::kCellSizeMeters;
			for (const World* w : cells)
				for (const World::PortalVignette& v : w->portalVignettes)
				{
					std::string region;
					for (const RegionDef& r : g.master.regions)
						if (v.center.x >= (r.eMin - 0.5) * C && v.center.x <= (r.eMax + 0.5) * C &&
							v.center.z >= -(r.nMax + 0.5) * C && v.center.z <= -(r.nMin - 0.5) * C) { region = r.name; break; }
					entries.push_back({ region, &v });
				}
			std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
				if (a.region != b.region) return a.region < b.region;
				if (a.v->city != b.v->city) return a.v->city;
				return a.v->name < b.v->name;
			});
			std::string j = "{\n  \"_comment\": \"GENERE par tools/world_gen --portal-out (PORTAL.2) — galerie des villes et lieux du portail. Sans secrets.\",\n";
			j += "  \"schema_version\": 1,\n  \"world\": " + Q(g.master.worldId) + ",\n  \"display_name\": " + Q(g.master.displayName) + ",\n";
			j += "  \"places\": [\n";
			for (size_t k = 0; k < entries.size(); ++k)
			{
				const World::PortalVignette& v = *entries[k].v;
				j += "    { \"id\": " + Q(v.id) + ", \"name\": " + Q(v.name) + ", \"kind\": " + Q(v.kind) + ", \"city\": " + (v.city ? "true" : "false") +
					", \"region\": " + Q(entries[k].region) + ", \"file\": " + Q(v.file) + ", \"description\": " + Q(v.description) + " }" +
					(k + 1 < entries.size() ? ",\n" : "\n");
			}
			j += "  ]\n}\n";
			return WriteFile(dir, g.report, "index.json", j, err);
		}
	}

	namespace
	{
		/// PORTAL.2 — vrai pour un décor végétal (arbres, buissons, fleurs, herbes…).
		bool IsVegetationMesh(const std::string& mesh)
		{
			// Atelier décors : parcelles de cultures, plantes d'eau, haies, troncs et souches.
			for (const char* t : { "Tree", "Pine", "Bush", "Flower", "Clover", "Fern", "Grass", "Plant", "Mushroom", "Petal", "Reed",
				"Crop_", "WaterLily", "Duckweed", "Hedge", "Log_Fallen", "Stump" })
				if (mesh.find(t) != std::string::npos) return true;
			return false;
		}

		/// PORTAL.2 — vrai pour un minéral posé (rochers, galets, dalles de chemin).
		bool IsRockMesh(const std::string& mesh)
		{
			return mesh.find("Rock") != std::string::npos || mesh.find("Pebble") != std::string::npos;
		}

		/// PORTAL.2 — genres de lieux remarquables illustrés dans la galerie du portail.
		bool IsPortalPoiKind(const std::string& k)
		{
			return k == "temple" || k == "cite" || k == "volcan" || k == "champ_bataille" || k == "port" ||
				k == "foret" || k == "faille" || k == "sanctuaire" || k == "ruine" || k == "defile";
		}

		/// PORTAL.2 — rend une vue de dessus détaillée N² px d'un carré de côté
		/// \p sideM centré sur \p center, à partir des grilles fines de la cellule.
		/// Couches, de bas en haut : relief ombré (soleil au nord-ouest), forêt
		/// (densité du masque + canopée bruitée), eau (mer selon la profondeur,
		/// rivières, lacs), routes et rues, arbres posés, bâtiments avec ombre portée.
		/// \param sideM côté de la zone rendue (m) ; \param N côté de l'image (px)
		/// Échantillon bilinéaire d'une grille quelconque (float, uint8_t…) ; \p f
		/// convertit une valeur de la grille en double (ex. « rue » → 1.0). Hors de
		/// la grille : \p outside.
		template <typename T, typename F>
		double SampleGridBilinear(const Grid<T>& g, double x, double z, double outside, F f)
		{
			if (g.data.empty()) return outside;
			const double fx = (x - g.originX) / g.step, fz = (z - g.originZ) / g.step;
			const int i = static_cast<int>(std::floor(fx)), j = static_cast<int>(std::floor(fz));
			if (i < 0 || j < 0 || i + 1 >= g.width || j + 1 >= g.height) return outside;
			const double tx = fx - i, tz = fz - j;
			const double a = f(g.At(i, j)) + (f(g.At(i + 1, j)) - f(g.At(i, j))) * tx;
			const double b = f(g.At(i, j + 1)) + (f(g.At(i + 1, j + 1)) - f(g.At(i, j + 1))) * tx;
			return a + (b - a) * tz;
		}

		std::vector<uint8_t> RenderPlaceVignette(const World& w, Vec2 center, double sideM, int N)
		{
			std::vector<uint8_t> rgb(static_cast<size_t>(N) * N * 3);
			const double sea = w.master.scale.seaLevelM;
			const double px = sideM / N;
			const double x0 = center.x - sideM * 0.5, z0 = center.z - sideM * 0.5;
			const bool infernal = IsInfernalMap(w.master);
			const double d = std::max(px, 3.0); // pas des dérivées (heightmap ≈ 4,9 m)
			const int fN = w.forestMaskRes;
			const double fpx = fN > 0 ? w.terrainSizeM / fN : 1.0;
			ParallelRows(N, [&](int j) {
				for (int i = 0; i < N; ++i)
				{
					const Vec2 p{ x0 + (i + 0.5) * px, z0 + (j + 0.5) * px };
					const double h = w.HeightAt(p);
					char code = 'P';
					{
						const int bi = std::clamp(w.biome.IndexX(p.x), 0, std::max(0, w.biome.width - 1));
						const int bj = std::clamp(w.biome.IndexZ(p.z), 0, std::max(0, w.biome.height - 1));
						if (!w.biome.data.empty()) code = w.biome.At(bi, bj);
					}
					double r, g, b;
					if (!infernal) TerrainColor(code, h, sea, r, g, b);
					else InfernalTerrainColor(code, h, p.x, p.z, w.master.seed, r, g, b);
					// Grain fin du sol (évite les aplats).
					const double grain = 1.0 + 0.06 * ValueNoise2(p.x / 3.0, p.z / 3.0, w.master.seed ^ 0x51ull);
					r *= grain; g *= grain; b *= grain;
					// Forêt : densité du masque, canopée bruitée (touffes de 6 à 12 m).
					if (fN > 0)
					{
						const int fi = std::clamp(static_cast<int>((p.x - w.terrainOriginX) / fpx), 0, fN - 1);
						const int fj = std::clamp(static_cast<int>((p.z - w.terrainOriginZ) / fpx), 0, fN - 1);
						const double f = w.forestMask[static_cast<size_t>(fj) * fN + fi] / 255.0;
						if (f > 0.0)
						{
							const double tuft = 0.5 + 0.5 * ValueNoise2(p.x / 7.0, p.z / 7.0, w.master.seed ^ 0xF0ull);
							const double canopy = std::clamp(f * (0.55 + 0.6 * tuft), 0.0, 1.0);
							const double cr = infernal ? 46 : 38, cg = infernal ? 30 : 70, cb = infernal ? 26 : 34;
							r = Lerp(r, cr * (0.75 + 0.5 * tuft), canopy * 0.85);
							g = Lerp(g, cg * (0.75 + 0.5 * tuft), canopy * 0.85);
							b = Lerp(b, cb * (0.75 + 0.5 * tuft), canopy * 0.85);
						}
					}
					// Relief ombré (soleil au nord-ouest), plus marqué que la carte du monde.
					const double hx = w.HeightAt({ p.x + d, p.z }) - w.HeightAt({ p.x - d, p.z });
					const double hz = w.HeightAt({ p.x, p.z + d }) - w.HeightAt({ p.x, p.z - d });
					const double shade = Clamp(1.0 + (-hx - hz) / (2.0 * d) * 1.4, 0.5, 1.4);
					// Routes et rues (sous l'eau : non).
					bool water = w.IsWater(p);
					if (!water)
					{
						// Grilles à ≈ 4,9 m : échantillonnées en bilinéaire puis seuillées en
						// douceur, pour des bords de rue et de route sans escalier.
						const double street = SampleGridBilinear(w.urban, p.x, p.z, 0.0, [](uint8_t u) { return u == 2 ? 1.0 : 0.0; });
						const double town = SampleGridBilinear(w.urban, p.x, p.z, 0.0, [](uint8_t u) { return u >= 1 ? 1.0 : 0.0; });
						const double rd = SampleGridBilinear(w.roadDist, p.x, p.z, 1e9, [](float v) { return static_cast<double>(v); });
						const double road = 1.0 - SmoothStep(0.6, 2.2, rd);
						const double paved = SmoothStep(0.35, 0.65, street);
						r = Lerp(r, 128, 0.22 * town); g = Lerp(g, 118, 0.22 * town); b = Lerp(b, 96, 0.22 * town);
						r = Lerp(r, 146, 0.8 * road); g = Lerp(g, 118, 0.8 * road); b = Lerp(b, 84, 0.8 * road);
						r = Lerp(r, 152, 0.85 * paved); g = Lerp(g, 140, 0.85 * paved); b = Lerp(b, 120, 0.85 * paved);
						if (!infernal)
							if (const double wall = FalaiseWallAt(w.master, p.x, p.z, 1.5 * px); wall > 0.0)
							{ r = Lerp(r, 236, wall); g = Lerp(g, 234, wall); b = Lerp(b, 222, wall); }
						r *= shade; g *= shade; b *= shade;
						if (infernal)
							if (const double lava = InfernalLavaAt(w.master, p.x, p.z); lava > 0.0)
							{
								const double core = SmoothStep(0.35, 1.0, lava);
								r = Lerp(r, Lerp(222, 255, core), lava); g = Lerp(g, Lerp(84, 186, core), lava); b = Lerp(b, Lerp(22, 48, core), lava);
							}
					}
					else
					{
						// Mer : plus sombre avec la profondeur ; eau douce : bleu-vert clair.
						const double depth = sea - h;
						if (depth > 0.0)
						{
							const double t = Clamp(depth / 25.0, 0.0, 1.0);
							r = Lerp(78, 22, t); g = Lerp(150, 62, t); b = Lerp(168, 112, t);
						}
						else { r = 64; g = 128; b = 150; }
						const double ripple = 1.0 + 0.04 * ValueNoise2(p.x / 4.0, p.z / 9.0, w.master.seed ^ 0xA7ull);
						r *= ripple; g *= ripple; b *= ripple;
					}
					uint8_t* o = &rgb[(static_cast<size_t>(j) * N + i) * 3];
					o[0] = static_cast<uint8_t>(Clamp(r, 0, 255));
					o[1] = static_cast<uint8_t>(Clamp(g, 0, 255));
					o[2] = static_cast<uint8_t>(Clamp(b, 0, 255));
				}
			});
			// Disque plein (arbres, ombres) ; alpha = opacité.
			auto disc = [&](Vec2 c, double radM, double rr, double gg, double bb, double alpha) {
				const double ci = (c.x - x0) / px, cj = (c.z - z0) / px, rp = radM / px;
				const int i0 = std::max(0, static_cast<int>(ci - rp - 1)), i1 = std::min(N - 1, static_cast<int>(ci + rp + 1));
				const int j0 = std::max(0, static_cast<int>(cj - rp - 1)), j1 = std::min(N - 1, static_cast<int>(cj + rp + 1));
				for (int jj = j0; jj <= j1; ++jj)
					for (int ii = i0; ii <= i1; ++ii)
					{
						const double dx = ii + 0.5 - ci, dz = jj + 0.5 - cj;
						const double dist = std::sqrt(dx * dx + dz * dz);
						if (dist > rp + 0.5) continue;
						const double a = alpha * Clamp(rp + 0.5 - dist, 0.0, 1.0);
						// Éclairage de la canopée : plus clair au nord-ouest.
						const double light = rp > 1.5 ? 1.0 + 0.25 * Clamp((-dx - dz) / (2.0 * rp), -1.0, 1.0) : 1.0;
						uint8_t* o = &rgb[(static_cast<size_t>(jj) * N + ii) * 3];
						o[0] = static_cast<uint8_t>(Clamp(Lerp(o[0], rr * light, a), 0, 255));
						o[1] = static_cast<uint8_t>(Clamp(Lerp(o[1], gg * light, a), 0, 255));
						o[2] = static_cast<uint8_t>(Clamp(Lerp(o[2], bb * light, a), 0, 255));
					}
			};
			// Rectangle orienté (bâtiments) : (hw, hd) demi-dimensions, yaw en degrés.
			// roof : deux pans (ombre/lumière) séparés par le faîtage le long du grand axe.
			auto rect = [&](Vec2 c, double hw, double hd, double yawDeg, double rr, double gg, double bb, double alpha, bool roof) {
				const double a = yawDeg * 3.14159265358979323846 / 180.0;
				const double ca = std::cos(a), sa = std::sin(a);
				const double ext = std::sqrt(hw * hw + hd * hd);
				const int i0 = std::max(0, static_cast<int>((c.x - ext - x0) / px) - 1), i1 = std::min(N - 1, static_cast<int>((c.x + ext - x0) / px) + 1);
				const int j0 = std::max(0, static_cast<int>((c.z - ext - z0) / px) - 1), j1 = std::min(N - 1, static_cast<int>((c.z + ext - z0) / px) + 1);
				for (int jj = j0; jj <= j1; ++jj)
					for (int ii = i0; ii <= i1; ++ii)
					{
						const double wx = x0 + (ii + 0.5) * px - c.x, wz = z0 + (jj + 0.5) * px - c.z;
						const double lx = wx * ca + wz * sa, lz = -wx * sa + wz * ca;
						if (std::abs(lx) > hw || std::abs(lz) > hd) continue;
						double k = 1.0;
						if (roof)
						{
							const bool alongX = hw >= hd;
							const double side = alongX ? lz : lx;
							k = side < 0.0 ? 1.12 : 0.82;
							const double edge = alongX ? hd - std::abs(lz) : hw - std::abs(lx);
							if (edge < px * 0.8) k *= 0.7; // bord du toit
						}
						uint8_t* o = &rgb[(static_cast<size_t>(jj) * N + ii) * 3];
						o[0] = static_cast<uint8_t>(Clamp(Lerp(o[0], rr * k, alpha), 0, 255));
						o[1] = static_cast<uint8_t>(Clamp(Lerp(o[1], gg * k, alpha), 0, 255));
						o[2] = static_cast<uint8_t>(Clamp(Lerp(o[2], bb * k, alpha), 0, 255));
					}
			};
			auto inView = [&](Vec2 q, double margin) {
				return q.x > x0 - margin && q.x < x0 + sideM + margin && q.z > z0 - margin && q.z < z0 + sideM + margin;
			};
			// Arbres et buissons posés (ombre portée au sud-est, puis canopée).
			for (const ScatterDef& sc : w.scatter)
			{
				const bool tree = sc.mesh.find("Tree") != std::string::npos || sc.mesh.find("Pine") != std::string::npos;
				const bool bush = sc.mesh.find("Bush") != std::string::npos;
				if ((!tree && !bush) || !inView(sc.position, 10.0)) continue;
				const bool dead = sc.mesh.find("Dead") != std::string::npos;
				const double rad = (tree ? 2.6 : 1.1) * std::max(0.5, sc.scale);
				disc({ sc.position.x + rad * 0.5, sc.position.z + rad * 0.5 }, rad, 10, 14, 8, 0.35);
				if (dead) disc(sc.position, rad * 0.7, 92, 76, 58, 0.9);
				else if (infernal) disc(sc.position, rad, 58, 34, 28, 0.95);
				else disc(sc.position, rad, tree ? 46 : 70, tree ? 86 : 104, tree ? 40 : 48, 0.95);
			}
			// Décor construit (murs, ruines, étals, mobilier) : petits blocs gris ; rochers
			// moyens en petits points, jamais sur l'eau ni la lave ; galets et dalles omis.
			for (const ScatterDef& sc : w.scatter)
			{
				if (IsVegetationMesh(sc.mesh) || !inView(sc.position, 10.0)) continue;
				const bool rock = IsRockMesh(sc.mesh);
				if (rock && (sc.mesh.find("Rock_") == std::string::npos || w.IsWater(sc.position) ||
					(infernal && InfernalLavaAt(w.master, sc.position.x, sc.position.z) > 0.0))) continue;
				const double rad = rock ? 0.9 : std::clamp(sc.collisionRadius > 0.0 ? sc.collisionRadius : 0.8 * sc.scale, 0.5, 6.0);
				disc({ sc.position.x + rad * 0.4, sc.position.z + rad * 0.4 }, rad, 10, 10, 12, 0.35);
				disc(sc.position, rad, infernal ? 88 : 132, infernal ? 72 : 126, infernal ? 70 : 116, 0.95);
			}
			// Bâtiments : ombre portée, puis toit (teinte stable par bâtiment).
			for (const BuildingPlacementDef& bd : w.buildings)
			{
				if (!inView(bd.position, 40.0)) continue;
				const double fallback = bd.footprintRadius * 0.7071;
				const double hw = bd.halfW > 0.0 ? bd.halfW : fallback, hd = bd.halfD > 0.0 ? bd.halfD : fallback;
				const double sh = std::max(1.5, std::min(hw, hd) * 0.5);
				rect({ bd.position.x + sh, bd.position.z + sh }, hw, hd, bd.yawDeg, 12, 12, 14, 0.45, false);
				static const double kRoofs[][3] = { { 142, 70, 52 }, { 96, 98, 108 }, { 128, 98, 64 }, { 156, 88, 60 }, { 84, 78, 74 }, { 170, 140, 92 } };
				const uint64_t hsh = Hash(w.master.seed, static_cast<uint64_t>(std::llround(bd.position.x)), static_cast<uint64_t>(std::llround(bd.position.z)));
				const double* rc = kRoofs[hsh % (sizeof(kRoofs) / sizeof(kRoofs[0]))];
				if (infernal) rect(bd.position, hw, hd, bd.yawDeg, 70, 46, 44, 1.0, true);
				else rect(bd.position, hw, hd, bd.yawDeg, rc[0], rc[1], rc[2], 1.0, true);
			}
			return rgb;
		}
	}

	bool WritePortalVignettes(World& w, const std::string& dir, std::string& err)
	{
		const int N = 768;
		auto emit = [&](const std::string& id, const std::string& name, const std::string& kind, const std::string& description,
			Vec2 center, double sideM, bool city) -> bool {
			// Identifiant sûr pour un nom de fichier.
			std::string file;
			for (char ch : id) file += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') ? ch : '_';
			file += ".png";
			// Cadre gardé dans la cellule (les grilles fines s'arrêtent à son bord).
			const double half = sideM * 0.5;
			const Vec2 view{ std::clamp(center.x, w.terrainOriginX + half, w.terrainOriginX + w.terrainSizeM - half),
				std::clamp(center.z, w.terrainOriginZ + half, w.terrainOriginZ + w.terrainSizeM - half) };
			const std::vector<uint8_t> rgb = RenderPlaceVignette(w, view, sideM, N);
			if (!WriteBytes(dir, w.report, file, EncodePngRgb(rgb, static_cast<uint32_t>(N), static_cast<uint32_t>(N)), err)) return false;
			w.portalVignettes.push_back({ id, name, kind, description, file, center, city });
			return true;
		};
		// Villes du lore (pas les agglomérations procédurales, jamais les secrètes) :
		// cadre de 2,6 rayons urbains, entre 500 m et 3 km.
		for (const CityDef& c : w.cities)
		{
			if (c.secret || !c.procedural.empty() || !w.OwnsPoint(c.center)) continue;
			if (!emit(c.id, c.name, c.kind, c.description, c.center, std::clamp(c.radiusM * 2.6, 500.0, 3000.0), true)) return false;
		}
		// Lieux remarquables de la cellule (quelques genres, jamais les secrets) : 800 m,
		// seulement s'il y a quelque chose à voir (au moins 3 bâtiments ou éléments de
		// décor construit, hors rochers, à moins de 150 m) — sinon la vue ne montrerait
		// qu'un relief nu.
		for (const PoiDef& p : w.pois)
		{
			if (p.secret || !IsPortalPoiKind(p.kind) || !w.OwnsPoint(p.center)) continue;
			int features = 0;
			auto near = [&](Vec2 q) { return std::hypot(q.x - p.center.x, q.z - p.center.z) < 150.0; };
			for (const BuildingPlacementDef& bd : w.buildings) features += near(bd.position) ? 1 : 0;
			for (const ScatterDef& sc : w.scatter) features += (!IsVegetationMesh(sc.mesh) && !IsRockMesh(sc.mesh) && near(sc.position)) ? 1 : 0;
			if (features < 3) continue;
			if (!emit(p.id, p.name, p.kind, p.description, p.center, 800.0, false)) return false;
		}
		return true;
	}

	// Aperçu du continent (--preview-map) : couche macro seule, traits continus, sans
	// liseré de cellules ; même rendu que l'affiche du portail, villes marquées.
	bool WriteMapPreview(const GlobalWorld& g, const std::string& path, int sizePx, std::string& err)
	{
		const int N = std::clamp(sizePx, 512, 8192);
		const double sizeM = std::max(g.maxX - g.minX, g.maxZ - g.minZ);
		ContinentMapStyle style;
		style.cellOutlines = false;
		style.continuousLines = true;
		style.riverRadius = 1;
		style.roadRadius = 0;
		style.majorRoadRadius = 1;
		style.cityRadius = 2;
		const std::vector<uint8_t> rgb = RenderContinentMap(g, N, g.minX, g.minZ, sizeM, {}, style);
		const std::vector<uint8_t> png = EncodePngRgb(rgb, static_cast<uint32_t>(N), static_cast<uint32_t>(N));
		std::FILE* f = std::fopen(path.c_str(), "wb");
		if (!f) { err = "apercu impossible a ecrire : " + path; return false; }
		const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
		std::fclose(f);
		if (!ok) err = "apercu incomplet : " + path;
		return ok;
	}

	bool WriteGlobalOutputs(GlobalWorld& g, const std::vector<World*>& cells, std::string& err)
	{
		const std::string wid = g.master.worldId;
		const std::string dir = "world/" + wid + "/";
		// Carte carrée couvrant tout le continent.
		const double sizeM = std::max(g.maxX - g.minX, g.maxZ - g.minZ);
		const double originX = g.minX, originZ = g.minZ;

		// ---- places.json (UI client : carte, bannières, radar) — coordonnées GLOBALES.
		//      v1.0.110 : villes et lieux SECRETS (clé `secret` du master) absents de ce
		//      fichier, écrits à part dans secret_places.json (serveur seulement).
		{
			std::vector<const CityDef*> all, secrets;
			for (const CityDef& c : g.cities) (c.secret ? secrets : all).push_back(&c);
			for (const World* w : cells)
				for (const CityDef& c : w->cities)
					if (!c.procedural.empty()) all.push_back(&c);
			std::vector<const PoiDef*> pois, secretPois;
			for (const PoiDef& p : g.pois) (p.secret ? secretPois : pois).push_back(&p);
			// Une ville (forme commune à places.json et secret_places.json).
			auto cityJson = [&](const CityDef& c, bool last) {
				std::string starts;
				for (size_t q = 0; q < c.startFor.size(); ++q) starts += (q ? ", " : "") + Q(c.startFor[q]);
				return "    { \"id\": " + Q(c.id) + ", \"name\": " + Q(c.name) + ", \"kind\": " + Q(c.procedural.empty() ? c.kind : c.procedural) +
					", \"race\": " + Q(c.race) + ", \"state\": " + Q(c.state) + ", \"faction\": " + Q(c.faction) +
					", \"x\": " + Num(c.center.x) + ", \"z\": " + Num(c.center.z) +
					", \"radius_m\": " + Num(c.radiusM) + ", \"start_for\": [" + starts + "], \"source\": " + Q(c.source) +
					", \"description\": " + Q(c.description) + " }" + (last ? "\n" : ",\n");
			};
			// Un lieu (forme commune aux deux fichiers).
			auto poiJson = [&](const PoiDef& p, bool last) {
				return "    { \"id\": " + Q(p.id) + ", \"name\": " + Q(p.name) + ", \"kind\": " + Q(p.kind) + ", \"x\": " + Num(p.center.x) +
					", \"z\": " + Num(p.center.z) + ", \"description\": " + Q(p.description) + " }" + (last ? "\n" : ",\n");
			};

			// secret_places.json : même schéma que places.json (villes, lieux), lu par
			// l'annuaire des MJ du shard ; retiré du paquet client (package-windows.ps1).
			// Supprimé s'il n'y a aucun secret (pas de fichier périmé).
			if (!secrets.empty() || !secretPois.empty())
			{
				std::string sj = "{\n  \"_comment\": \"GENERE par tools/world_gen — villes et lieux SECRETS (gouffre, grottes des dragons, entrees ophidiennes). RESERVE AU SERVEUR : jamais livre au client (retire par .github/scripts/package-windows.ps1). Coordonnees GLOBALES.\",\n";
				sj += "  \"schema_version\": 1,\n  \"world\": " + Q(wid) + ",\n  \"secret\": true,\n";
				sj += "  \"city_count\": " + std::to_string(secrets.size()) + ",\n  \"cities\": [\n";
				for (size_t k = 0; k < secrets.size(); ++k) sj += cityJson(*secrets[k], k + 1 == secrets.size());
				sj += "  ],\n  \"poi_count\": " + std::to_string(secretPois.size()) + ",\n  \"pois\": [\n";
				for (size_t k = 0; k < secretPois.size(); ++k) sj += poiJson(*secretPois[k], k + 1 == secretPois.size());
				sj += "  ]\n}\n";
				if (!WriteFile(g.outputRoot, g.report, dir + "secret_places.json", sj, err)) return false;
			}
			else
			{
				std::error_code ec;
				fs::remove(fs::path(g.outputRoot) / (dir + "secret_places.json"), ec);
			}

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
			for (size_t k = 0; k < all.size(); ++k) j += cityJson(*all[k], k + 1 == all.size());
			j += "  ],\n";
			j += "  \"poi_count\": " + std::to_string(pois.size()) + ",\n  \"pois\": [\n";
			for (size_t k = 0; k < pois.size(); ++k) j += poiJson(*pois[k], k + 1 == pois.size());
			j += "  ],\n  \"region_count\": " + std::to_string(g.master.regions.size()) + ",\n  \"regions\": [\n";
			const double C = engine::world::cells::kCellSizeMeters;
			for (size_t k = 0; k < g.master.regions.size(); ++k)
			{
				const RegionDef& r = g.master.regions[k];
				// Rectangle de cases : x de e_min à e_max, z de -n_max à -n_min (nord = -Z).
				// v1.0.118 — niveaux de référence par faction de jeu (couleur du nom de zone) :
				// tableau d'objets (lisible par l'API indexée du client), absent si inconnu.
				std::string levels;
				if (!r.levels.empty())
				{
					levels = ", \"levels\": [";
					for (size_t b = 0; b < r.levels.size(); ++b)
						levels += std::string(b ? ", " : "") + "{ \"faction\": " + Q(r.levels[b].faction) + ", \"min\": " +
							std::to_string(r.levels[b].minLevel) + ", \"max\": " + std::to_string(r.levels[b].maxLevel) + " }";
					levels += "]";
				}
				j += "    { \"id\": " + Q(r.id) + ", \"name\": " + Q(r.name) + ", \"x_min\": " + Num((r.eMin - 0.5) * C) + ", \"x_max\": " + Num((r.eMax + 0.5) * C) +
					", \"z_min\": " + Num(-(r.nMax + 0.5) * C) + ", \"z_max\": " + Num(-(r.nMin - 0.5) * C) + levels + " }" + (k + 1 < g.master.regions.size() ? ",\n" : "\n");
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

		// ---- FIX.12 — routes, rues et bâtiments en vectoriel (fenêtre Carte zoomée).
		{
			const engine::world::mapfeatures::MapFeatures features = BuildMapFeatures(g, cells);
			if (!WriteBytes(g.outputRoot, g.report, dir + "map_features.bin", engine::world::mapfeatures::Serialize(features), err)) return false;
		}

		// ---- Points de départ par faction (v1.0.61), repli par race (lus par le master
		//      à la création et par le shard).
		{
			std::vector<const StartLocationDef*> starts;
			for (const World* w : cells) for (const StartLocationDef& s : w->starts) starts.push_back(&s);
			// Ordre stable : celui des villes dans le master (puis des factions de start_for).
			auto cityRank = [&](const std::string& id) {
				for (size_t i = 0; i < g.cities.size(); ++i) if (g.cities[i].id == id) return i;
				return g.cities.size();
			};
			std::stable_sort(starts.begin(), starts.end(), [&](const StartLocationDef* a, const StartLocationDef* b) {
				return cityRank(a->cityId) < cityRank(b->cityId); });
			// Repli par race : la PREMIÈRE ville de départ du master dont la ville est de
			// cette race (personnage sans faction, ou faction sans départ).
			std::vector<const StartLocationDef*> raceStarts;
			for (const StartLocationDef* st : starts)
			{
				if (st->race.empty()) continue;
				const bool known = std::any_of(raceStarts.begin(), raceStarts.end(),
					[&](const StartLocationDef* r) { return r->race == st->race; });
				if (!known) raceStarts.push_back(st);
			}
			// Une entrée « { "city": …, "x": … } » d'un départ (sections factions et races).
			auto entry = [&](const StartLocationDef& st, bool withRace) {
				const CityDef* c = g.FindCity(st.cityId);
				return std::string("{ \"city\": ") + Q(st.cityId) + ", \"city_name\": " + Q(c ? c->name : st.cityId) +
					(withRace ? ", \"race\": " + Q(st.race) : std::string()) +
					", \"cell\": " + Q(engine::world::cells::CellDirName(engine::world::cells::CellFromPosition(st.position.x, st.position.z), g.master.cellMap)) +
					", \"x\": " + Num(st.position.x) + ", \"y\": " + Num(st.y) + ", \"z\": " + Num(st.position.z) +
					", \"yaw_deg\": " + Num(st.yawDeg) + " }";
			};
			std::string j = "{\n  \"_comment\": \"GENERE par tools/world_gen — point d'apparition d'un NOUVEAU personnage selon sa faction (factions, cles de game/data/races/factions.json), a defaut selon sa race (races : premiere ville de depart de la race dans le master) (positions GLOBALES). Repli : character_creation.default_spawn.* puis (0,100,0).\",\n";
			// Cellules générées (jouables) : lues par le shard (mur invisible côté serveur,
			// relocalisation d'une position hors monde). Identifiants du lore séparés par des virgules.
			std::string cellList;
			for (const World* w : cells) cellList += (cellList.empty() ? "" : ",") + engine::world::cells::LoreCellId(w->cell);
			// Liste des factions : le Config du serveur n'énumère pas les clés.
			std::string factionList;
			for (const StartLocationDef* st : starts) factionList += (factionList.empty() ? "" : ",") + st->faction;
			j += "  \"schema_version\": 1,\n  \"world\": " + Q(wid) + ",\n  \"default_race\": \"humains\",\n";
			j += "  \"generated_cells\": " + Q(cellList) + ",\n";
			j += "  \"start_factions\": " + Q(factionList) + ",\n  \"factions\": {\n";
			for (size_t k = 0; k < starts.size(); ++k)
				j += "    " + Q(starts[k]->faction) + ": " + entry(*starts[k], true) + (k + 1 < starts.size() ? ",\n" : "\n");
			j += "  },\n  \"races\": {\n";
			for (size_t k = 0; k < raceStarts.size(); ++k)
				j += "    " + Q(raceStarts[k]->race) + ": " + entry(*raceStarts[k], false) + (k + 1 < raceStarts.size() ? ",\n" : "\n");
			j += "  }\n}\n";
			if (!WriteFile(g.outputRoot, g.report, dir + "start_locations.json", j, err)) return false;
		}

		// ---- sea_lines.json (v1.0.124, v1.0.125 schéma 2) : lignes maritimes servies par des
		//      bateaux (LORE_BD 16.51) ; pour chaque escale, le ponton (point de débarquement,
		//      terre au bord de l'eau, regard vers la mer) et le quai (bateau à l'arrêt, cap) ;
		//      pour chaque étape, le trajet du bateau de quai à quai (polyligne, m). Lu par le
		//      shard. Écrit si le master a des lignes.
		if (!g.master.seaLines.empty())
		{
			std::string j = "{\n  \"_comment\": \"GENERE par tools/world_gen — lignes maritimes servies par des bateaux (LORE_BD 16.51) : escales (ponton = debarquement, quai = bateau a l'arret, positions GLOBALES, m), trajets de quai a quai (legs, sens aller), prix en or par etape. Lu par le shard.\",\n";
			j += "  \"schema_version\": 2,\n  \"world\": " + Q(wid) + ",\n  \"sea_level_m\": " + Num(g.master.scale.seaLevelM) + ",\n  \"lines\": [\n";
			auto r1 = [&](double v) { return Num(std::round(v * 10.0) / 10.0); };
			for (size_t li = 0; li < g.master.seaLines.size(); ++li)
			{
				const SeaLineDef& line = g.master.seaLines[li];
				j += "    { \"id\": " + Q(line.id) + ", \"name\": " + Q(line.name) + ", \"loop\": " + (line.loop ? "true" : "false")
					+ ", \"price_gold_per_leg\": " + std::to_string(line.priceGoldPerLeg) + ", \"boats\": " + std::to_string(line.boats)
					+ ", \"stop_seconds\": " + std::to_string(line.stopSeconds) + ", \"stops\": [\n";
				for (size_t k = 0; k < line.stops.size(); ++k)
				{
					const World::SeaPonton* sp = nullptr;
					for (const World* w : cells)
						for (const World::SeaPonton& s : w->seaPontons)
							if (s.lineId == line.id && s.stopIndex == k) sp = &s;
					const GlobalWorld::SeaDock* dock = nullptr;
					for (const GlobalWorld::SeaDock& d : g.seaDocks)
						if (d.lineId == line.id && d.stop == k) dock = &d;
					const CityDef* c = g.FindCity(line.stops[k].cityId);
					j += "      { \"city\": " + Q(line.stops[k].cityId) + ", \"name\": " + Q(c ? c->name : line.stops[k].cityId)
						+ ", \"cell\": " + Q(line.stops[k].cell);
					if (sp)
						j += ", \"x\": " + r1(sp->position.x) + ", \"y\": " + r1(sp->y)
							+ ", \"z\": " + r1(sp->position.z) + ", \"yaw_deg\": " + Num(std::round(sp->yawDeg));
					if (dock)
						j += ", \"dock_x\": " + r1(dock->position.x) + ", \"dock_z\": " + r1(dock->position.z)
							+ ", \"dock_yaw_deg\": " + Num(std::round(dock->yawDeg));
					j += std::string(" }") + (k + 1 < line.stops.size() ? ",\n" : "\n");
				}
				j += "    ], \"legs\": [\n";
				std::vector<const GlobalWorld::SeaRoute*> legs;
				for (const GlobalWorld::SeaRoute& r : g.seaLegs)
					if (r.lineId == line.id) legs.push_back(&r);
				for (size_t k = 0; k < legs.size(); ++k)
				{
					j += "      { \"from\": " + std::to_string(legs[k]->from) + ", \"to\": " + std::to_string(legs[k]->to) + ", \"points\": [";
					for (size_t p = 0; p < legs[k]->points.size(); ++p)
						j += (p ? ", [" : "[") + r1(legs[k]->points[p].x) + ", " + r1(legs[k]->points[p].z) + "]";
					j += std::string("] }") + (k + 1 < legs.size() ? ",\n" : "\n");
				}
				j += std::string("    ] }") + (li + 1 < g.master.seaLines.size() ? ",\n" : "\n");
			}
			j += "  ]\n}\n";
			if (!WriteFile(g.outputRoot, g.report, dir + "sea_lines.json", j, err)) return false;
		}

		// ---- portals.json : portails reliés à une AUTRE carte (Enfers ↔ Royaumes), lus
		//      par le shard (src/shardd/world/PortalNetwork.h). Seuls les portails des
		//      cellules générées y figurent (altitude connue, cellule jouable).
		{
			std::vector<const PoiDef*> portals;
			for (const World* w : cells)
				for (const PoiDef& p : w->pois)
					if (p.kind == "portail" && !p.link.empty()) portals.push_back(&p);
			std::string j = "{\n  \"_comment\": \"GENERE par tools/world_gen — portails entre cartes (positions GLOBALES de la carte '" + wid + "'). link = <monde>:<id du portail apparie>. Un joueur vivant dans radius_m est transfere a arrival_offset_m du portail apparie (docs/world/ENFERS.md).\",\n";
			j += "  \"schema_version\": 1,\n  \"world\": " + Q(wid) + ",\n  \"portal_count\": " + std::to_string(portals.size()) + ",\n  \"portals\": [\n";
			for (size_t k = 0; k < portals.size(); ++k)
			{
				const PoiDef& p = *portals[k];
				j += "    { \"id\": " + Q(p.id) + ", \"name\": " + Q(p.name) + ", \"link\": " + Q(p.link) +
					", \"cell\": " + Q(p.cell) + ", \"x\": " + Num(p.center.x) + ", \"y\": " + Num(p.y) + ", \"z\": " + Num(p.center.z) +
					", \"radius_m\": 6, \"arrival_offset_m\": 8 }" + (k + 1 < portals.size() ? ",\n" : "\n");
			}
			j += "  ]\n}\n";
			if (!WriteFile(g.outputRoot, g.report, dir + "portals.json", j, err)) return false;
			g.report.portals = portals.size();
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

		// ---- PORTAL.1 — affiche du monde pour le portail web (option --portal-out).
		if (!g.portalOutDir.empty() && !WritePortalPoster(g, cells, g.portalOutDir, 4096, err)) return false;
		// ---- PORTAL.2 — index de la galerie des villes et lieux (vignettes écrites par cellule).
		if (!g.portalOutDir.empty() && !WritePortalPlacesIndex(g, cells, g.portalOutDir + "/places", err)) return false;
		return true;
	}
}
