// tools/world_gen — agglomérations : rues, lots, bâtiments, remparts,
// mobilier urbain, champs, cimetière et graphe de déplacement des PNJ.
//
// Disposition (pour chaque ville) :
//   - une PLACE centrale (rayon ~30 % du rayon urbain) avec étals de marché ;
//   - des RUES rayonnantes : d'abord les routes qui arrivent à la ville, puis
//     des rues complémentaires pour atteindre un nombre minimal selon la taille ;
//   - une RUE CIRCULAIRE pour les villes moyennes et grandes ;
//   - des LOTS de part et d'autre des rues, façade (porte) tournée vers la rue ;
//   - l'AUBERGE face à la place ; une FORGE en périphérie ;
//   - un REMPART (capitale, grande ville, forteresse) ou une PALISSADE (orcs,
//     camps), percé de portes là où passent les routes ;
//   - des CHAMPS clôturés hors les murs (toutes les agglomérations habitées,
//     parcelles plus grandes pour les villes), un CIMETIÈRE.
// État « reconstruction » (ville relevée après un sac, ex. Kendrhir après le
// tome 1) : ruines et CHANTIERS (ruine entourée d'étais et de matériaux) mêlés
// aux maisons neuves, BRÈCHES avec gravats dans le rempart ou la palissade ;
// auberge, marché, forge, pavage et PNJ restent ceux d'une ville habitée.
// Le graphe de rues (noeuds tous les ~9 m) sert aux tournées des villageois et
// aux rondes des gardes (Population.cpp).

#include "WorldGenContext.h"
#include "HouseCatalog.h"
#include "Random.h"

#include <algorithm>
#include <cmath>

namespace tools::world_gen
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
		constexpr double kStreetHalf = 2.5;
		/// Bits ScatterInstance::flags (cf. src/shared/world/instances/ScatterInstances.h).
		constexpr uint8_t kSolid = 1u, kPiece = 2u, kOccluder = 4u;

		/// v1.0.121 — la cellule est-elle dans `graveyards.village_only_cells` du master ?
		/// (seuls les villes et villages du lore y ont un cimetière). Sans effet de bord.
		bool VillageOnlyCell(const World& w)
		{
			return w.master.graveyardVillageOnlyCells.count({ w.cell.n, w.cell.e }) != 0;
		}

		/// Modèle de bâtiment candidat (référence de bibliothèque + encombrement).
		struct TemplateRef
		{
			std::string type;
			std::string variant;
			double halfW = 2.0;   ///< demi-largeur X (m)
			double halfD = 3.0;   ///< demi-profondeur Z hors perron (m)
			double weight = 1.0;
			bool site = false;    ///< chantier de reconstruction : étais et matériaux posés autour
		};

		TemplateRef House(const char* id, double weight)
		{
			const HouseSpec* s = FindHouse(id);
			TemplateRef t;
			t.type = "maison"; t.variant = id; t.weight = weight;
			t.halfW = s ? s->widthM * 0.5 : 2.0;
			t.halfD = s ? s->depthM * 0.5 : 3.0;
			return t;
		}
		/// Chantier de reconstruction : la ruine `ruinId` du catalogue, marquée pour
		/// recevoir étais, briques et outils (cf. « Chantiers » dans BuildSettlements).
		TemplateRef Site(const char* ruinId, double weight)
		{
			TemplateRef t = House(ruinId, weight);
			t.site = true;
			return t;
		}
		TemplateRef Hand(const char* type, const char* variant, double halfW, double halfD, double weight)
		{
			TemplateRef t; t.type = type; t.variant = variant; t.halfW = halfW; t.halfD = halfD; t.weight = weight;
			return t;
		}

		/// Palette d'habitations selon la taille, la race et l'état.
		std::vector<TemplateRef> Palette(const CityDef& c)
		{
			// Atelier décors (BD T1/T3) : pierre sèche et chaume dans les campagnes,
			// planches et bardeaux dans les villes des Marches (Kendrhir).
			if (c.procedural == "ferme")
				return { House("longere_6x10", 1.2), House("chaumiere_4x4", 1.2), House("maison_4x6", 1),
					House("longere_pierre_6x10", 1.2), House("chaumiere_pierre_4x4", 1.2) };
			if (c.procedural == "bucherons")
				return { House("chaumiere_4x4", 1.5), House("longere_6x10", 1), House("chaumiere_pierre_4x4", 1.5) };
			if (c.procedural == "mine")
				return { House("chaumiere_4x4", 2), House("forge_6x6", 1) };
			if (c.procedural == "bandits")
				return { House("ruine_6x6", 2), House("chaumiere_4x4", 2) };
			if (c.state == "ruine")
				return { House("ruine_6x6", 3), House("ruine_6x10", 2), House("ruine_8x10", 2), House("chaumiere_4x4", 0.5) };
			if (c.state == "reconstruction")
			{
				// Palette de la ville intacte (≈ 51 % des lots), plus des ruines (≈ 31 %)
				// et des chantiers (≈ 18 %), en proportion du poids total de la palette.
				CityDef intact = c;
				intact.state = "intact";
				std::vector<TemplateRef> pal = Palette(intact);
				double total = 0.0;
				for (const TemplateRef& t : pal) total += t.weight;
				pal.push_back(House("ruine_6x6", total * 0.25));
				pal.push_back(House("ruine_6x10", total * 0.20));
				pal.push_back(House("ruine_8x10", total * 0.15));
				pal.push_back(Site("ruine_6x6", total * 0.15));
				pal.push_back(Site("ruine_6x10", total * 0.12));
				pal.push_back(Site("ruine_8x10", total * 0.08));
				return pal;
			}
			if (c.race == "nains")
				return { House("maison_6x6", 3), House("manoir_8x12", 1), House("caserne_8x14", 0.6), House("forge_6x6", 0.8), House("echoppe_4x8", 1) };
			if (c.race == "orcs")
				return { House("chaumiere_4x4", 3), House("longere_6x10", 2), House("forge_6x6", 0.6) };
			if (c.race == "elfes")
				return { House("chaumiere_4x4", 2), House("maison_4x6", 2), House("longere_6x10", 1) };
			if (c.race == "demons" || c.kind == "camp")
				return { House("chaumiere_4x4", 2), House("ruine_6x6", 1), House("forge_6x6", 0.5) };
			// Atelier décors T2 (n° 92) : toits d'ardoise et tourelles dans les villes de l'Empire.
			const bool empire = c.faction == "empire_hynn";
			if (c.kind == "capitale")
			{
				std::vector<TemplateRef> pal = { House("maison_haute_6x8", 3), House("grande_maison_8x10", 2), House("manoir_8x12", 1.5),
					Hand("house", "house_001", 4.0, 7.4, 1.2), House("echoppe_4x8", 1.2), House("maison_6x6", 1) };
				if (empire) { pal.push_back(House("maison_haute_ardoise_6x8", 2)); pal.push_back(House("grande_maison_ardoise_8x10", 1.5)); }
				else { pal.push_back(House("maison_brique_6x8", 1.5)); pal.push_back(House("maison_brique_4x6", 1.2)); }   // T4 (n° 156)
				return pal;
			}
			if (c.kind == "grande_ville")
			{
				std::vector<TemplateRef> pal = { House("maison_6x6", 2.5), House("maison_haute_6x8", 2), House("grande_maison_8x10", 1.5),
					House("echoppe_4x8", 1.5), House("manoir_8x12", 0.6), Hand("house", "house_001", 4.0, 7.4, 0.8) };
				if (empire) { pal.push_back(House("maison_ardoise_6x6", 2)); pal.push_back(House("maison_haute_ardoise_6x8", 1.5)); }
				else { pal.push_back(House("maison_brique_6x8", 1.2)); pal.push_back(House("maison_brique_4x6", 1.2)); }   // T4 (n° 156)
				return pal;
			}
			if (c.kind == "forteresse")
				return { House("caserne_8x14", 1.5), House("maison_6x6", 2), House("manoir_8x12", 0.8), House("forge_6x6", 0.6) };
			if (c.kind == "ville")
			{
				std::vector<TemplateRef> pal = { House("maison_4x6", 1.5), House("maison_6x6", 2), House("echoppe_4x8", 1), House("maison_haute_6x8", 1),
					House("grande_maison_8x10", 0.8), House("maison_planches_4x6", 1.5), House("maison_planches_6x8", 1.5),
					House("echoppe_planches_4x8", 0.8) };
				if (empire) { pal.push_back(House("maison_ardoise_6x6", 1.5)); pal.push_back(House("maison_haute_ardoise_6x8", 0.8)); }
				return pal;
			}
			if (c.kind == "village")
				return { House("chaumiere_4x4", 1.2), House("maison_4x6", 1.5), House("longere_6x10", 1), House("maison_6x6", 1),
					House("echoppe_4x8", 0.7), House("chaumiere_pierre_4x4", 1.2), House("longere_pierre_6x10", 0.8),
					House("maison_planches_4x6", 1) };
			return { House("chaumiere_4x4", 1.5), House("longere_6x10", 1), House("maison_4x6", 1.5),
				House("chaumiere_pierre_4x4", 1.5), House("longere_pierre_6x10", 1) }; // hameau
		}

		TemplateRef InnFor(const CityDef& c)
		{
			if (c.kind == "capitale") return Hand("tavern", "auberge_cite", 4.0, 7.4, 1);
			if (c.kind == "grande_ville") return Hand("tavern", "auberge_grande_ville", 4.0, 5.4, 1);
			if (c.kind == "forteresse") return Hand("tavern", "auberge_forteresse", 4.0, 6.4, 1);
			if (c.kind == "ville") return Hand("tavern", "auberge_terrasse", 3.0, 4.4, 1);
			return Hand("tavern", "auberge_hameau", 2.0, 4.4, 1);
		}

		int MinSpokes(const std::string& kind, const std::string& procedural)
		{
			if (procedural == "ferme" || procedural == "mine") return 1;
			if (kind == "capitale") return 6;
			if (kind == "grande_ville") return 5;
			if (kind == "ville" || kind == "forteresse") return 4;
			if (kind == "village" || kind == "camp") return 3;
			return 2;
		}

		bool HasWalls(const CityDef& c)
		{
			return c.state != "ruine" && (c.kind == "capitale" || c.kind == "grande_ville" || c.kind == "forteresse");
		}
		bool HasPalisade(const CityDef& c)
		{
			return c.state != "ruine" && !HasWalls(c) && (c.race == "orcs" || c.kind == "camp" || c.kind == "ville");
		}

		/// Parcelles cultivées d'une agglomération (champs clôturés hors les murs).
		struct FieldPlan
		{
			int count = 0;        ///< nombre de champs visés
			int attempts = 0;     ///< tirages d'emplacement (les grands champs exigent un sol plus plat)
			double halfW = 8.0;   ///< demi-largeur (m), le long de la clôture d'entrée
			double halfD = 5.0;   ///< demi-profondeur (m), dans l'axe de la ville
		};

		/// Plan des champs selon le type d'agglomération : rien pour les camps, les
		/// ruines et les villes démoniaques ; plus de champs, et plus grands, pour
		/// les villes.
		FieldPlan FieldsFor(const CityDef& c)
		{
			FieldPlan p;
			if (c.state == "ruine" || c.race == "demons" || c.kind == "camp") return p;
			if (c.kind == "hameau") { p.count = c.procedural == "ferme" ? 4 : 2; p.attempts = 60; }
			else if (c.kind == "village") { p.count = c.procedural == "ferme" ? 4 : 3; p.attempts = 60; }
			else if (c.kind == "ville") { p = { 4, 100, 12.0, 7.0 }; }
			else if (c.kind == "grande_ville" || c.kind == "forteresse") { p = { 5, 120, 14.0, 8.0 }; }
			else if (c.kind == "capitale") { p = { 6, 140, 16.0, 9.0 }; }
			// v1.0.140 (retour du propriétaire : « rajoute plus de champs, il y a beaucoup de gens à
			// nourrir ») : la capitale en gradins (Lhynn) a trois fois plus de champs.
			if (c.kind == "capitale" && c.acropole.enabled) { p.count = 18; p.attempts = 600; }
			// Atelier décors (décision du propriétaire, 21/11/2026) : très grands champs
			// pour remplir les abords des villes et hameaux — largeur ×5, longueur ×10.
			// Les cultures sont des parcelles modulaires de 4 m posées en pavage.
			p.halfW *= 5.0;
			p.halfD *= 10.0;
			return p;
		}

		/// Champ (atelier décors) : pas d'eau ni de rivière sur l'emprise, pas de route
		/// qui la traverse, pente moyenne ≤ ~8 % (les cultures suivent le relief, à la
		/// différence d'un bâtiment). 16 points de contrôle sur le rectangle.
		bool FieldGroundOk(const World& w, Vec2 c, Vec2 ax, Vec2 az, double hw, double hd)
		{
			if (!w.InPlayableBounds(c, 48.0)) return false;
			double lo = 1e9, hi = -1e9;
			for (int a = -2; a <= 2; ++a)
				for (int b = -2; b <= 2; ++b)
				{
					const Vec2 p = c + ax * (hw * a / 2.0) + az * (hd * b / 2.0);
					if (w.IsWater(p)) return false;
					const int ri = w.riverDist.IndexX(p.x), rj = w.riverDist.IndexZ(p.z);
					if (w.riverDist.InBounds(ri, rj) && w.riverDist.At(ri, rj) < 6.0f) return false;
					const int qi = w.roadDist.IndexX(p.x), qj = w.roadDist.IndexZ(p.z);
					if (w.roadDist.InBounds(qi, qj) && w.roadDist.At(qi, qj) < 4.0) return false;
					const double h = w.HeightAt(p);
					lo = std::min(lo, h); hi = std::max(hi, h);
				}
			return hi - lo < 1.6 + 0.08 * std::hypot(hw, hd);
		}

		/// Culture d'un champ (parcelle modulaire du registre des décors), tirée au sort.
		const char* PickCrop(double u)
		{
			struct C { const char* mesh; double w; };
			static const C crops[] = { { "Crop_Wheat_1", 3.0 }, { "Crop_Barley_1", 2.0 }, { "Crop_Corn_1", 2.0 },
				{ "Crop_Sunflower_1", 1.5 }, { "Crop_Rapeseed_1", 1.5 }, { "Crop_Vine_1", 1.0 } };
			double total = 0.0;
			for (const C& c : crops) total += c.w;
			double acc = 0.0;
			for (const C& c : crops) { acc += c.w / total; if (u <= acc) return c.mesh; }
			return crops[0].mesh;
		}

		double YawFacing(Vec2 f) { return std::atan2(-f.x, -f.z) * 180.0 / kPi; }
		double YawAlong(Vec2 d) { return std::atan2(-d.z, d.x) * 180.0 / kPi; }     ///< axe X local vers d
		Vec2 Dir(double angleRad) { return { std::cos(angleRad), std::sin(angleRad) }; }

		void AddProp(World& w, const std::string& mesh, Vec2 p, double yaw, double scale, uint8_t flags,
			uint8_t cull, double collisionRadius = 0.0, double yOffset = 0.0)
		{
			ScatterDef s;
			s.mesh = "meshes/props/" + mesh + ".gltf";
			s.position = p; s.yawDeg = yaw; s.scale = scale; s.flags = flags; s.cullClass = cull;
			s.collisionRadius = collisionRadius; s.yOffset = yOffset;
			w.scatter.push_back(std::move(s));
		}

		/// Services qui ont un panneau Sign_Service_<id> (atelier décors ; ordre et liste
		/// de tools/asset_pipeline/decor/decor_textures.py SIGN_SERVICES). Le cimetière
		/// (enclos à part) et le port (quai) n'ont pas de panneau de façade.
		bool HasFacadeSign(const std::string& svc)
		{
			static const char* kSigns[] = { "auberge", "ecurie", "forgeron", "potions", "armes", "armures", "temple",
				"guilde", "infirmerie", "marche", "banque", "prison", "ecole_magie", "bibliotheque", "arene" };
			for (const char* k : kSigns)
				if (svc == k) return true;
			return false;
		}

		/// Affinité d'un service pour un modèle de bâtiment (plus petit = préféré) :
		/// l'auberge va à la taverne, la forge à la forge, la prison à la caserne, les
		/// commerces aux échoppes ; les autres au bâtiment libre le plus proche de la place.
		int ServiceAffinity(const std::string& svc, const BuildingPlacementDef& b)
		{
			const bool tavern = b.templateType == "tavern";
			if (svc == "auberge") return tavern ? 0 : 99;
			if (tavern) return 99;                     // la taverne ne porte que l'auberge
			const std::string& v = b.variantId;
			auto has = [&](const char* k) { return v.find(k) != std::string::npos; };
			if (svc == "forgeron") return has("forge") ? 0 : 2;
			if (has("forge")) return 50;               // une forge ne sert qu'au forgeron
			if (svc == "prison") return has("caserne") ? 0 : 2;
			if (svc == "armes" || svc == "armures" || svc == "potions" || svc == "marche")
				return has("echoppe") ? 0 : 1;
			if (svc == "banque" || svc == "guilde" || svc == "ecole_magie" || svc == "bibliotheque" || svc == "temple")
				return (has("manoir") || has("grande_maison") || has("maison_haute")) ? 0 : 1;
			return 1;
		}

		/// Enseignes de service (atelier décors, décision du 22/11/2026) : un panneau à
		/// pictogramme, sans texte, devant le bâtiment de chaque service de la ville ; le
		/// client affiche le nom du service dans la langue du joueur au survol ou au clic.
		/// Le panneau est sur potence, perpendiculaire à la façade, à côté de la porte.
		/// Les bâtiments importés de la ville d'ancrage (ImportLegacyContent, posés avant
		/// la génération) sont aussi candidats : leur façade est inconnue (doorPoint =
		/// position), le panneau est alors posé du côté du centre de la ville, au bord de
		/// leur emprise (footprintRadius).
		/// \param firstBuilding indice du premier bâtiment posé pour cette ville.
		void PlaceServiceSigns(World& w, const CityDef& c, size_t firstBuilding)
		{
			std::vector<std::string> todo;
			for (const std::string& s : c.services)
				if (HasFacadeSign(s) && std::find(todo.begin(), todo.end(), s) == todo.end()) todo.push_back(s);
			if (todo.empty()) return;
			std::vector<size_t> cand;
			const size_t first = (c.id == w.master.importAnchorCity) ? 0 : firstBuilding;
			for (size_t i = first; i < w.buildings.size(); ++i)
				if (w.buildings[i].cityId == c.id && w.buildings[i].templateType != "ruine") cand.push_back(i);
			auto dist = [&](size_t i) {
				const Vec2 p = w.buildings[i].position;
				return std::hypot(p.x - c.center.x, p.z - c.center.z);
			};
			std::sort(cand.begin(), cand.end(), [&](size_t a, size_t b) { return dist(a) < dist(b); });
			std::vector<bool> used(cand.size(), false);
			// L'auberge et la forge d'abord (bâtiments dédiés), puis l'ordre du lore.
			std::stable_sort(todo.begin(), todo.end(), [](const std::string& a, const std::string& b) {
				auto rank = [](const std::string& s) { return s == "auberge" ? 0 : (s == "forgeron" ? 1 : 2); };
				return rank(a) < rank(b);
			});
			for (const std::string& svc : todo)
			{
				int best = -1, bestAff = 99;
				for (size_t k = 0; k < cand.size(); ++k)
				{
					if (used[k]) continue;
					const int aff = ServiceAffinity(svc, w.buildings[cand[k]]);
					if (aff < bestAff) { bestAff = aff; best = static_cast<int>(k); }
				}
				if (best < 0 || bestAff >= 50) continue;   // plus de bâtiment disponible
				used[best] = true;
				const BuildingPlacementDef& b = w.buildings[cand[best]];
				Vec2 f{ b.doorPoint.x - b.position.x, b.doorPoint.z - b.position.z };
				const bool imported = std::hypot(f.x, f.z) < 1e-6;
				// Bâtiment importé : façade inconnue, panneau tourné vers le centre de la ville.
				if (imported) f = Vec2{ c.center.x - b.position.x, c.center.z - b.position.z };
				const double l = std::hypot(f.x, f.z);
				if (l < 1e-6) continue;
				f = f * (1.0 / l);
				const Vec2 side{ -f.z, f.x };
				const double off = std::min(std::max(b.halfW * 0.55, 1.2), std::max(0.6, b.halfW - 0.2));
				// Origine du mesh au milieu de la potence : poteau à 0,15 m de la façade.
				const double depth = imported ? b.footprintRadius : b.halfD;
				const Vec2 p = b.position + f * (depth + 0.6) + side * off;
				if (w.IsWater(p)) continue;
				AddProp(w, "Sign_Service_" + svc, p, YawAlong(f), 1.0, kSolid, 1, 0.5);
				w.Occupy(p, 0.7);
				// Armurier (atelier décors T3, pl.17) : présentoir d'armure de l'autre côté de la porte.
				if (svc == "armures")
				{
					const Vec2 q = b.position + f * (b.halfD + 1.3) - side * off;
					if (!w.IsWater(q) && !w.IsOccupied(q, 0.5)) { AddProp(w, "ArmorStand_1", q, YawFacing(f * -1.0), 1.0, kSolid, 1, 0.4); w.Occupy(q, 0.5); }
				}
			}
		}

		/// Vrai si un disque reste exploitable (hors eau, hors rivière, pente faible).
		bool GroundOk(const World& w, Vec2 c, double r)
		{
			if (!w.InPlayableBounds(c, 48.0)) return false;
			for (int k = 0; k < 8; ++k)
			{
				const Vec2 p = c + Dir(k * kPi / 4.0) * r;
				if (w.IsWater(p)) return false;
				const int ri = w.riverDist.IndexX(p.x), rj = w.riverDist.IndexZ(p.z);
				if (w.riverDist.InBounds(ri, rj) && w.riverDist.At(ri, rj) < 4.0f) return false;
			}
			if (w.IsWater(c)) return false;
			// Dénivelé sur l'emprise : un bâtiment ne se pose pas à flanc de colline.
			double lo = 1e9, hi = -1e9;
			for (int k = 0; k < 9; ++k)
			{
				const Vec2 p = (k == 8) ? c : c + Dir(k * kPi / 4.0) * r;
				const double h = w.HeightAt(p);
				lo = std::min(lo, h); hi = std::max(hi, h);
			}
			return hi - lo < 1.6;
		}

		/// Gouffre (v1.0.107) : torches au pied des parois du fond et le long de la
		/// galerie (mesh Torch_Metal, vrai décor de scatter.bin, flamme orientée vers
		/// l'espace libre), éboulis qui dissimulent l'entrée de la galerie (rochers,
		/// passage laissé libre dans l'axe), lumière de chaque torche et zone
		/// d'ambiance « sombre » du gouffre en DONNÉES (w.pointLights, w.ambianceZones,
		/// écrites dans zone.json ; le client ne les lit pas encore). Déterministe.
		/// Effet de bord : w.scatter, w.pointLights, w.ambianceZones, occupation fine.
		/// À appeler après les agglomérations (les torches évitent le bâti).
		/// Tombes ANCIENNES d'un cimetière (demande du propriétaire, 22/11/2026) : rangées
		/// lâches de tas de cailloux moussus (Grave_Cairn_1-3), chacun avec à la tête une
		/// croix de bois plus ou moins bien faite (Cross_Wood_1 équarrie, _2 branches liées,
		/// _3 penchée au bras cassé), parfois une vieille stèle, parfois rien. Le centre
		/// (point de résurrection, chandelier) reste dégagé sur 2,5 m.
		/// \param toward direction des têtes de tombe (vers la ville) ; \param count tombes visées.
		/// Effet de bord : w.scatter, occupation fine (1 m par tombe). Déterministe (rng).
		void PlaceOldGraves(World& w, Vec2 center, Vec2 toward, int count, Rng& rng)
		{
			const double l = std::hypot(toward.x, toward.z);
			const Vec2 ax = l > 1e-6 ? toward * (1.0 / l) : Vec2{ 1.0, 0.0 };
			const Vec2 side{ -ax.z, ax.x };
			const int cols = std::max(2, static_cast<int>(std::ceil(std::sqrt(count * 1.4))));
			const int rows = (count + cols - 1) / cols + 1;
			int placed = 0;
			for (int r = 0; r < rows && placed < count; ++r)
				for (int c = 0; c < cols && placed < count; ++c)
				{
					// Grille : tombes de 2 m alignées selon `ax`, rangées espacées de 3,2 m.
					const double u = (r - (rows - 1) * 0.5) * 3.2 + rng.Range(-0.3, 0.3);
					const double v = (c - (cols - 1) * 0.5) * 1.9 + rng.Range(-0.25, 0.25);
					const Vec2 p = center + ax * u + side * v;
					if (std::hypot(p.x - center.x, p.z - center.z) < 2.5) continue;
					if (w.IsWater(p) || w.IsOccupied(p, 1.0) || !w.InPlayableBounds(p, 40.0)) continue;
					const int ri = w.roadDist.IndexX(p.x), rj = w.roadDist.IndexZ(p.z);
					if (w.roadDist.InBounds(ri, rj) && w.roadDist.At(ri, rj) < 2.5) continue;   // jamais sur la route
					const double yaw = YawAlong(ax) + rng.Range(-8.0, 8.0);
					static const char* kCairns[] = { "Grave_Cairn_1", "Grave_Cairn_2", "Grave_Cairn_3" };
					AddProp(w, kCairns[rng.Below(3)], p, yaw, rng.Range(0.85, 1.1), 0, 2, 0.0, -0.03);
					// Tête de tombe : croix (bonne, rustique, abîmée), vieille stèle ou rien.
					const Vec2 head = p + ax * 1.15;
					const double pick = rng.Next();
					const double hyaw = YawAlong(side) + rng.Range(-10.0, 10.0);
					if (pick < 0.30) AddProp(w, "Cross_Wood_1", head, hyaw, rng.Range(0.9, 1.05), 0, 1, 0.0, -0.25);
					else if (pick < 0.58) AddProp(w, "Cross_Wood_2", head, hyaw, rng.Range(0.9, 1.1), 0, 1, 0.0, -0.25);
					else if (pick < 0.78) AddProp(w, "Cross_Wood_3", head, hyaw, rng.Range(0.9, 1.05), 0, 1, 0.0, -0.25);
					else if (pick < 0.86)   // stèles brutes (atelier décors T4, pl.11, 30)
					{
						static const char* kStones[] = { "Gravestone_1", "Gravestone_2", "Gravestone_3" };
						AddProp(w, kStones[rng.Below(3)], head, hyaw + 90.0, rng.Range(0.85, 1.05), kSolid, 1, 0.35, -0.3);
					}
					else if (pick < 0.92) AddProp(w, "Grave_Stone_Old_1", head, hyaw + 90.0, rng.Range(0.85, 1.05), kSolid, 1, 0.35, -0.3);
					w.Occupy(p, 1.0);
					++placed;
				}
		}

		/// Nombre de tombes d'un cimetière selon la taille de l'agglomération.
		int GraveCountFor(const CityDef& c)
		{
			if (c.kind == "capitale") return 30;
			if (c.kind == "grande_ville") return 24;
			if (c.kind == "ville" || c.kind == "forteresse") return 18;
			if (c.kind == "village") return 12;
			return c.procedural.empty() ? 8 : 6;
		}

		/// Cimetière ISOLÉ (v1.0.111) : si `graveyards.fill_empty_cells` est vrai et que la
		/// cellule n'a aucun cimetière (ni agglomération qui en a un, ni cimetière importé),
		/// en pose un sans ville : au bord de la route la plus proche du centre de la case
		/// (14 m de l'axe, côté sec et libre), sinon sur le premier point praticable d'une
		/// spirale depuis le centre (pas de 150 m, jusqu'à 4300 m). Décor : cercle de
		/// pierres et chandelier, comme les cimetières des villes. Déterministe.
		/// Effet de bord : w.isolatedGraveyard, w.scatter, occupation fine ; une cellule
		/// sans terre (pleine mer) n'en reçoit pas : avertissement seulement (le choix de
		/// `without_graveyard` en tient compte pour garder `graveyards.target`).
		void BuildIsolatedGraveyard(World& w)
		{
			if (!w.master.graveyardFillEmptyCells || VillageOnlyCell(w)) return;
			for (const SettlementLayout& L : w.layouts)
				if (L.hasGraveyard && L.cityId != w.master.importAnchorCity) return;
			for (const std::string& l : w.importedRespawnLines)
				if (l.find(" graveyard ") != std::string::npos) return;
			const double sea = w.master.scale.seaLevelM;
			// Site valable : terre ferme, dans la zone jouable, libre.
			auto ok = [&](Vec2 p) {
				return w.InPlayableBounds(p, 80.0) && !w.IsWater(p) && w.HeightAt(p) > sea + 2.0 && !w.IsOccupied(p, 6.0);
			};
			const Vec2 ctr{ w.terrainOriginX + w.terrainSizeM * 0.5, w.terrainOriginZ + w.terrainSizeM * 0.5 };
			bool found = false;
			Vec2 site;
			double best = 1e18;
			for (const Road& r : w.roads)
				for (size_t i = 1; i + 1 < r.points.size(); i += 4)
				{
					const Vec2 p = r.points[i];
					const double d = std::hypot(p.x - ctr.x, p.z - ctr.z);
					if (d >= best) continue;
					const Vec2 dir = r.points[i + 1] - r.points[i - 1];
					const double len = std::hypot(dir.x, dir.z);
					if (len < 1e-6) continue;
					const Vec2 nrm{ -dir.z / len, dir.x / len };
					const double off = r.widthM * 0.5 + 12.0;
					for (const double side : { 1.0, -1.0 })
					{
						const Vec2 q = p + nrm * (off * side);
						if (ok(q)) { best = d; site = q; found = true; break; }
					}
				}
			for (double rad = 0.0; !found && rad <= 4300.0; rad += 150.0)
			{
				const int steps = rad < 1.0 ? 1 : static_cast<int>(rad / 150.0) * 6;
				for (int k = 0; k < steps && !found; ++k)
				{
					const double a = k * 2.0 * kPi / steps;
					const Vec2 q = ctr + Vec2{ std::cos(a), std::sin(a) } * rad;
					if (ok(q)) { site = q; found = true; }
				}
			}
			// Cellule de pleine mer (ex. mer autour de l'Île de la Trêve) : pas de cimetière.
			if (!found) { w.Warn("aucun site a terre pour le cimetiere isole (cellule de mer)"); return; }
			w.hasIsolatedGraveyard = true;
			w.isolatedGraveyard = site;
			Rng rng(Hash(w.master.seed, HashString(w.cellName.c_str()), 6666));
			AddProp(w, "CandleStick_Stand", site, 0.0, 1.0, 0, 1);
			PlaceOldGraves(w, site, ctr - site, 10, rng);
			w.Occupy(site, 6.0);
		}

		void BuildGouffreProps(World& w)
		{
			for (const ReliefDef& r : w.master.reliefs)
			{
				if (r.type != "gouffre" || r.n != w.cell.n || r.e != w.cell.e || r.gallery.size() < 2) continue;
				const CityDef* host = w.FindCity(r.cityId);
				int lightIndex = 0;
				// Torche plantée en \p p, flamme tournée vers \p open (direction unitaire
				// de l'espace libre) ; lumière enregistrée à 2 m du sol.
				auto torch = [&](Vec2 p, Vec2 open) {
					if (!w.OwnsPoint(p) || w.IsOccupied(p, 0.6)) return;
					AddProp(w, "Torch_Metal", p, YawFacing(open * -1.0), 1.0, 0, 1, 0.0, 1.4);
					w.Occupy(p, 0.6);
					PointLightDef l;
					l.id = r.id + "_torche_" + std::to_string(lightIndex++);
					l.position = p + open * 0.3;
					l.y = w.HeightAt(p) + 1.8;
					w.pointLights.push_back(std::move(l));
				};
				// 1) Pied des parois du fond : une torche tous les ~140 m de pourtour, sauf à
				//    l'embouchure de la galerie (elle a les siennes).
				const double perim = 2.0 * kPi * r.floorRadiusM;
				const int ring = std::max(8, static_cast<int>(std::lround(perim / 140.0)));
				for (int k = 0; k < ring; ++k)
				{
					const double a = r.phase + k * 2.0 * kPi / ring;
					const Vec2 p = r.center + Dir(a) * (GouffreFloorRadiusAt(r, a) + 2.0);
					if (GouffreGalleryDistance(r, p, nullptr) < r.galleryHalfWidthM + r.galleryWallM + 20.0) continue;
					torch(p, Dir(a) * -1.0);
				}
				// 2) Galerie : une torche tous les 70 m, en quinconce sur les deux parois,
				//    depuis la sortie du fond jusqu'à 60 m de l'entrée (pas de lueur dehors).
				const double total = r.galleryAlong.back();
				int side = 1;
				for (double along = 35.0; along < total - 60.0; along += 70.0)
				{
					size_t k = 0;
					while (k + 2 < r.gallery.size() && r.galleryAlong[k + 1] < along) ++k;
					const double seg = r.galleryAlong[k + 1] - r.galleryAlong[k];
					const double t = seg > 0.0 ? (along - r.galleryAlong[k]) / seg : 0.0;
					const Vec2 a = r.gallery[k], b = r.gallery[k + 1];
					const Vec2 axis = a + (b - a) * t;
					if (std::hypot(axis.x - r.center.x, axis.z - r.center.z) < GouffreFloorRadiusAt(r, std::atan2(axis.z - r.center.z, axis.x - r.center.x)) + r.wallM)
						continue; // encore dans le fond
					const double len = std::max(1e-6, std::hypot(b.x - a.x, b.z - a.z));
					const Vec2 dir{ (b.x - a.x) / len, (b.z - a.z) / len };
					const Vec2 nrm{ -dir.z, dir.x };
					torch(axis + nrm * (side * (r.galleryHalfWidthM + 1.0)), nrm * static_cast<double>(-side));
					side = -side;
				}
				// 3) Éboulis de l'entrée cachée : blocs de part et d'autre de l'axe et en
				//    travers, qui laissent un passage (≥ 2 m) ; aucune torche dehors.
				{
					const Vec2 e = r.gallery.back(), q = r.gallery[r.gallery.size() - 2];
					const double len = std::max(1e-6, std::hypot(e.x - q.x, e.z - q.z));
					const Vec2 dir{ (e.x - q.x) / len, (e.z - q.z) / len };
					const Vec2 nrm{ -dir.z, dir.x };
					Rng rng(Hash(w.master.seed, HashString(r.id.c_str()), 1107));
					const char* rocks[] = { "Rock_Medium_1", "Rock_Medium_2", "Rock_Medium_3" };
					for (int k = 0; k < 9; ++k)
					{
						const double lateral = (k % 2 ? 1.0 : -1.0) * rng.Range(r.galleryHalfWidthM * 0.45, r.galleryHalfWidthM + 6.0);
						const Vec2 p = e + dir * rng.Range(-18.0, 10.0) + nrm * lateral;
						if (!w.OwnsPoint(p) || w.IsOccupied(p, 1.0)) continue;
						AddProp(w, rocks[rng.Below(3)], p, rng.Range(0.0, 360.0), rng.Range(1.8, 3.2), kSolid, 0, 1.2);
						w.Occupy(p, 1.0);
					}
				}
				// 4) Ambiance « sombre » du fond (donnée) : jusqu'au bord le plus bas.
				AmbianceZoneDef z;
				z.id = r.id;
				z.label = host ? host->name : r.id;
				z.center = r.center;
				z.radiusM = r.floorRadiusM * 1.13 + r.wallM;
				z.floorY = r.floorY;
				z.topY = r.floorY + r.depthM;
				z.darkness = 0.65;
				w.ambianceZones.push_back(std::move(z));
			}
		}

		int AddNode(SettlementLayout& L, Vec2 p)
		{
			L.nodes.push_back({ p, {} });
			return static_cast<int>(L.nodes.size()) - 1;
		}
		void Link(SettlementLayout& L, int a, int b)
		{
			if (a == b || a < 0 || b < 0) return;
			auto& la = L.nodes[static_cast<size_t>(a)].links;
			if (std::find(la.begin(), la.end(), b) == la.end()) la.push_back(b);
			auto& lb = L.nodes[static_cast<size_t>(b)].links;
			if (std::find(lb.begin(), lb.end(), a) == lb.end()) lb.push_back(a);
		}
		int Nearest(const SettlementLayout& L, Vec2 p, int skip = -1)
		{
			int best = -1; double bd = 1e18;
			for (size_t i = 0; i < L.nodes.size(); ++i)
			{
				if (static_cast<int>(i) == skip) continue;
				const double d = std::hypot(L.nodes[i].p.x - p.x, L.nodes[i].p.z - p.z);
				if (d < bd) { bd = d; best = static_cast<int>(i); }
			}
			return best;
		}

		/// Pose un bâtiment s'il tient ; renvoie l'indice ou -1. L'emprise testée
		/// est le RECTANGLE orienté du bâtiment (+0,6 m de marge), pas un disque :
		/// les maisons s'alignent en front de rue sans gaspiller les lots.
		int TryPlaceBuilding(World& w, const CityDef& c, const TemplateRef& t, Vec2 center, Vec2 facing,
			const char* label, bool requireInside = true)
		{
			const double yaw = YawFacing(facing);
			const double hx = t.halfW + 0.6, hz = t.halfD + 0.6;
			const double footR = std::sqrt(hx * hx + hz * hz);
			if (requireInside && std::hypot(center.x - c.center.x, center.z - c.center.z) + footR * 0.5 > c.radiusM + 4.0)
				return -1;
			if (w.IsOccupiedRect(center, hx, hz, yaw) || !GroundOk(w, center, footR)) return -1;
			// Hors des routes inter-villes (la chaussée reste dégagée).
			for (int k = 0; k < 5; ++k)
			{
				const Vec2 corner = (k == 4) ? center : center + Vec2{ (k & 1 ? 1 : -1) * t.halfW, (k & 2 ? 1 : -1) * t.halfD };
				const int ri = w.roadDist.IndexX(corner.x), rj = w.roadDist.IndexZ(corner.z);
				if (w.roadDist.InBounds(ri, rj) && w.roadDist.At(ri, rj) < 1.0f) return -1;
			}
			w.OccupyRect(center, hx, hz, yaw);
			BuildingPlacementDef b;
			b.templateType = t.type;
			b.variantId = t.variant;
			b.displayName = std::string(label) + " (" + c.name + ")";
			b.position = center;
			b.yawDeg = yaw;
			b.footprintRadius = footR;
			b.halfW = t.halfW;
			b.halfD = t.halfD;
			b.cityId = c.id;
			b.doorPoint = center + facing * (t.halfD + 2.2);
			w.buildings.push_back(std::move(b));
			return static_cast<int>(w.buildings.size()) - 1;
		}

		/// Pave une polyligne de dalles (2 m) : `across` dalles de front, suivant
		/// la pente (inclinaison autour de l'axe X local).
		void PaveAlong(World& w, const std::vector<Vec2>& pts, int across, const char* mesh, double clipRadius, Vec2 clipCenter)
		{
			const std::vector<Vec2> rs = ResamplePolyline(pts, 2.0);
			for (size_t k = 0; k + 1 < rs.size(); ++k)
			{
				const Vec2 a = rs[k], b = rs[k + 1];
				if (clipRadius > 0.0 && std::hypot(a.x - clipCenter.x, a.z - clipCenter.z) > clipRadius) continue;
				Vec2 d{ b.x - a.x, b.z - a.z };
				const double l = std::hypot(d.x, d.z);
				if (l < 1e-6) continue;
				d = d * (1.0 / l);
				const Vec2 nrm{ -d.z, d.x };
				const Vec2 mid = (a + b) * 0.5;
				const double dh = w.HeightAt(b) - w.HeightAt(a);
				const double tilt = std::atan2(dh, l) * 180.0 / kPi;
				for (int t = 0; t < across; ++t)
				{
					const double off = (t - (across - 1) * 0.5) * 2.0;
					const Vec2 p = mid + nrm * off;
					if (w.IsWater(p)) continue;
					ScatterDef s;
					s.mesh = std::string("meshes/props/") + mesh + ".gltf";
					s.position = p;
					s.yawDeg = YawFacing(d);
					s.tiltDeg = tilt;
					s.yOffset = -0.06;
					s.cullClass = 1;
					w.scatter.push_back(std::move(s));
				}
			}
		}

		const TemplateRef& PickWeighted(const std::vector<TemplateRef>& pal, double u)
		{
			double total = 0.0;
			for (const auto& t : pal) total += t.weight;
			double acc = 0.0;
			for (const auto& t : pal) { acc += t.weight / total; if (u <= acc) return t; }
			return pal.back();
		}
	}

	namespace
	{
		/// v1.0.140 — ville en gradins (acropole) : réserve la plateforme du sommet (palais,
		/// Landmarks.cpp) et le couloir de l'escalier, pose l'escalier monumental direct (volées
		/// Stairs_Monumental_1 de 3 m × 1,8 m, bout à bout sur la rampe de AcropoleOffset, du
		/// bord de la plateforme au niveau de la grand-place) et prolonge le graphe des PNJ de la
		/// place jusqu'au sommet. Les volées sont du décor (sans collision) : le sol marchable
		/// est la rampe de la heightmap (pente 0,6 ≈ 31°, sous les 45° du contrôleur).
		/// Effet de bord : w.scatter, occupation de w, nœuds et liens de \p L.
		void BuildAcropoleStairs(World& w, const CityDef& c, SettlementLayout& L)
		{
			const AcropoleShape& a = c.acropole;
			const Vec2 dir = a.stairDir;              // sommet -> grand-place
			const Vec2 climb = dir * -1.0;            // sens de la montée
			w.Occupy(a.summit, a.topRadiusM + 2.0);
			for (double along = a.topRadiusM - 2.0; along <= a.stairBottomM; along += 2.0)
				w.Occupy(a.summit + dir * along, a.stairHalfWidthM + 1.5);
			const double plazaY = AcropoleTerrace(a, a.stairBottomM);
			const int flights = static_cast<int>(std::floor((a.heightM - plazaY) / 1.8 + 1e-6));
			for (int k = 0; k < flights; ++k)
			{
				// Volée k (0 = la plus haute) : de along = top + 3k (haut) à top + 3(k + 1) (bas) ;
				// origine du mesh au milieu de la volée, pied de la première marche à baseY.
				const Vec2 p = a.summit + dir * (a.topRadiusM + 3.0 * k + 1.5);
				const double baseY = c.plateauY + a.heightM - 1.8 * (k + 1);
				AddProp(w, "Stairs_Monumental_1", p, YawFacing(climb), 1.0, 0, 0, 0.0, baseY - w.HeightAt(p));
			}
			// Graphe : de la grand-place au bord de la plateforme (rondes des gardes).
			int prev = Nearest(L, a.summit + dir * a.stairBottomM);
			for (double along = a.stairBottomM - 6.0; along > a.topRadiusM - 6.0; along -= 6.0)
			{
				const int id = AddNode(L, a.summit + dir * along);
				Link(L, prev, id);
				prev = id;
			}
		}
	}

	namespace
	{
		/// v1.0.140 (retour du propriétaire : « on ne voit pas le port ! ») — PORT d'une capitale
		/// en gradins qui a le service `port` (Lhynn, port de guerre de la BD, T7 pl.4) : site =
		/// rivage le plus proche de la ville (sol de mer + 0,8 à + 8 m, hors les murs, chemin direct à sec à 90 %) devant une
		/// eau franche (60 m de large, 50 m de profondeur) reliée à la pleine mer. Construit :
		/// esplanade aplanie à quai + 1,1 m (30 m de profondeur), quai de pierre sur l'eau, quatre
		/// jetées de bois de 40 m, cinq navires amarrés (Ship_Cog_1, flottaison au niveau de la
		/// mer), deux grues, piles de caisses et de tonneaux, entrepôts (maisons) au fond de
		/// l'esplanade, chemin pavé jusqu'à la ville. Rend false (avertissement) sans site.
		/// Effet de bord : w.height (esplanade), w.scatter, w.buildings, occupation, graphe \p L.
		bool BuildHarbour(World& w, const CityDef& c, SettlementLayout& L, const std::vector<TemplateRef>& pal, Rng& rng)
		{
			const double sea = w.master.scale.seaLevelM;
			const double R = c.radiusM;
			auto deep = [&](Vec2 q) { return w.HeightAt(q) < sea - 0.8; };
			const double x0 = w.terrainOriginX + 60.0, z0 = w.terrainOriginZ + 60.0;
			const double x1 = w.terrainOriginX + w.terrainSizeM - 60.0, z1 = w.terrainOriginZ + w.terrainSizeM - 60.0;
			double best = 1e30;
			Vec2 site{}, sdir{};
			for (double dz = -900.0; dz <= 900.0; dz += 8.0)
				for (double dx = -900.0; dx <= 900.0; dx += 8.0)
				{
					const Vec2 q{ c.center.x + dx, c.center.z + dz };
					const double d = std::hypot(dx, dz);
					if (d < R + 30.0 || d > 900.0 || d >= best || q.x < x0 || q.x > x1 || q.z < z0 || q.z > z1) continue;
					const double h = w.HeightAt(q);
					if (h < sea + 0.8 || h > sea + 8.0 || w.IsWater(q) || w.SlopeDegAt(q) > 20.0) continue;
					// Le chemin direct depuis la ville reste à sec (pas de port de l'autre côté d'une lagune).
					// (au plus 10 % des points mouillés : mares et chenaux franchis à gué).
					int wet = 0, samples = 0;
					for (double r = R; r < d - 20.0; r += 10.0, ++samples)
						wet += w.HeightAt(c.center + Vec2{ dx / d, dz / d } * r) > sea + 0.3 ? 0 : 1;
					if (wet * 10 > samples) continue;
					for (int a = 0; a < 16; ++a)
					{
						const Vec2 dir = Dir(a * kPi / 8.0), tan{ -dir.z, dir.x };
						bool ok = true;
						for (double r : { 14.0, 32.0, 50.0 })
							for (double t : { -30.0, 0.0, 30.0 })
								ok = ok && deep(q + dir * r + tan * t);
						Vec2 open{};
						if (!ok || !NearestOpenSea(*w.g, q + dir * 50.0, 2500.0, open)) continue;
						best = d; site = q; sdir = dir;
						break;
					}
				}
			if (best >= 1e29) { w.Warn("port de " + c.id + " : aucun rivage praticable"); return false; }
			const Vec2 dir = sdir, tan{ -sdir.z, sdir.x };
			const double quayY = sea + 1.1;
			// Esplanade : 110 m le long du rivage, 30 m vers la terre, aplanie à la hauteur du quai.
			for (int j = 0; j < w.height.height; ++j)
				for (int i = 0; i < w.height.width; ++i)
				{
					const Vec2 q{ w.height.WorldX(i), w.height.WorldZ(j) };
					const Vec2 d = q - site;
					const double along = d.x * tan.x + d.z * tan.z, depth = -(d.x * dir.x + d.z * dir.z);
					if (std::fabs(along) > 70.0 || depth < -2.0 || depth > 48.0) continue;
					const double t = std::max(SmoothStep(55.0, 70.0, std::fabs(along)), SmoothStep(30.0, 48.0, depth));
					double& h = w.height.At(i, j);
					if (h < sea - 0.2 && depth < 2.0) continue; // l'eau reste de l'eau au bord du quai
					h = Lerp(quayY - 0.02, h, t);
				}
			// Quai et marchandises (de 2 m sur l'eau à 20 m vers la terre) ; les entrepôts, posés plus
			// loin (au-delà de 24 m), restent libres.
			w.OccupyRect(site - dir * 9.0, 56.0, 11.0, YawAlong(tan));
			// Quai de pierre sur l'eau (3 rangées de dalles), jetées de bois, navires.
			for (double a = -54.0; a <= 54.0; a += 2.0)
				for (int k = 0; k < 3; ++k)
				{
					const Vec2 q = site + tan * a + dir * (1.0 + 2.0 * k);
					AddProp(w, "Bridge_Stone_Deck_1", q, YawAlong(tan), 1.0, kSolid | kPiece, 0, 0.0, quayY - w.HeightAt(q));
				}
			int ship = 0;
			for (double a : { -36.0, -12.0, 12.0, 36.0 })
			{
				for (double r = 8.0; r <= 46.0; r += 2.0)
					for (int k = -1; k <= 1; k += 2)
					{
						const Vec2 q = site + tan * (a + k * 1.0) + dir * r;
						AddProp(w, "Boardwalk_Deck_1", q, YawAlong(dir), 1.0, kSolid | kPiece, 0, 0.0, quayY - w.HeightAt(q));
					}
				const Vec2 sp = site + tan * (a + 8.0) + dir * 26.0;
				AddProp(w, "Ship_Cog_1", sp, YawFacing(dir) + rng.Range(-3.0, 3.0), 0.9 + 0.2 * rng.Next(), 0, 0, 0.0, (sea - 1.6) - w.HeightAt(sp));
				++ship;
			}
			{
				const Vec2 sp = site + tan * -60.0 + dir * 85.0; // navire au mouillage
				AddProp(w, "Ship_Cog_1", sp, YawFacing(tan) + 20.0, 1.1, 0, 0, 0.0, (sea - 1.6) - w.HeightAt(sp));
			}
			w.Occupy(site + dir * 40.0, 60.0);
			// Grues et marchandises sur le quai.
			for (int sg = -1; sg <= 1; sg += 2)
				AddProp(w, "Crane_Wood_1", site + tan * (sg * 24.0) - dir * 3.0, YawAlong(dir), 1.0, kSolid, 0, 1.6);
			static const char* kGoods[] = { "Crate_Wooden", "Barrel", "Crate_Metal", "Barrel_Holder", "Rope_1", "FarmCrate_Empty", "Prop_Crate" };
			for (double a = -50.0; a <= 50.0; a += 6.5)
			{
				if (std::fabs(std::fabs(a) - 24.0) < 4.0) continue; // pied des grues
				const Vec2 g = site + tan * (a + rng.Range(-1.0, 1.0)) - dir * rng.Range(5.0, 9.0);
				AddProp(w, kGoods[rng.Below(7)], g, rng.Range(0.0, 360.0), 1.0, kSolid, 1, 0.6);
				if (rng.Chance(0.6)) AddProp(w, kGoods[rng.Below(7)], g + tan * 1.3, rng.Range(0.0, 360.0), 1.0, kSolid, 1, 0.6);
			}
			// Entrepôts au fond de l'esplanade, porte vers le quai.
			for (double a = -48.0; a <= 48.0; a += 1.0)
			{
				const TemplateRef& t = PickWeighted(pal, rng.Next());
				const Vec2 q = site + tan * a - dir * (24.0 + t.halfD);
				const int b = TryPlaceBuilding(w, c, t, q, dir, "Entrepot", false);
				if (b >= 0) { L.buildingIndices.push_back(b); a += 2.0 * t.halfW; }
			}
			// Chemin pavé de la ville au port, nœuds du graphe (dockers, gardes).
			Vec2 toPort = site - c.center;
			const double lp = std::hypot(toPort.x, toPort.z);
			toPort = toPort * (1.0 / lp);
			std::vector<Vec2> path;
			for (double r = R - 4.0; r <= lp - 10.0; r += 3.0) path.push_back(c.center + toPort * r);
			if (path.size() >= 2)
			{
				PaveAlong(w, path, 3, "RockPath_Irregular_Wide", 0.0, c.center);
				int prev = Nearest(L, path.front());
				for (size_t k = 3; k < path.size(); k += 3)
				{
					w.Occupy(path[k], kStreetHalf + 1.0);
					const int id = AddNode(L, path[k]);
					Link(L, prev, id);
					prev = id;
				}
				const int q = AddNode(L, site - dir * 8.0);
				Link(L, prev, q);
			}
			return ship > 0;
		}
	}

	// ---------------------------------------------------------------------------------
	// v1.0.141 — MAGISTRYA, VILLE-TOUR (mise en page « ville_tour », relief `pic` ;
	// BD-T2 p.19-27, LORE_BD §16.67). Pic rocheux au milieu d'un lac, rampe en spirale du
	// socle au sommet (TowerShape, Terrain.cpp) ; les parois entre deux spires sont couvertes
	// de façades (murs à fenêtres, toits d'ardoise, tourelles) comme « un écheveau de
	// dentelle » ; maisons le long de la rampe (les plus belles vers les hauteurs), volées
	// d'escalier, arches au-dessus de la chaussée ; long ponton de bois depuis la rive (gibets
	// et poteau indicateur, p.19) ; geôles voûtées au socle (p.22). Le palais du Magister, au
	// sommet, est bâti par BuildLandmarks (POI de genre « palais »).
	// ---------------------------------------------------------------------------------
	namespace
	{
		/// Pose un décor de meshes/props/ dont la base est à l'altitude ABSOLUE \p yAbs (m) ;
		/// \p tilt : rotation autour de l'axe X local (degrés). Effet de bord : w.scatter.
		void TowerProp(World& w, const std::string& mesh, Vec2 p, double yaw, double scale, uint8_t flags, uint8_t cull,
			double yAbs, double collisionR = 0.0, double tilt = 0.0)
		{
			ScatterDef s;
			s.mesh = "meshes/props/" + mesh + ".gltf";
			s.position = p; s.yawDeg = yaw; s.scale = scale; s.flags = flags; s.cullClass = cull;
			s.collisionRadius = collisionR; s.yOffset = yAbs - w.HeightAt(p); s.tiltDeg = tilt;
			w.scatter.push_back(std::move(s));
		}

		/// Applique \p fn(i, j, d) à chaque échantillon de la heightmap à moins de \p r (m) de
		/// \p c (d : distance au centre). Copie locale de l'aide de Terrain.cpp.
		template <typename F>
		void TowerForDisc(Grid<double>& g, Vec2 c, double r, F&& fn)
		{
			const int i0 = std::max(0, g.IndexX(c.x - r) - 1), i1 = std::min(g.width - 1, g.IndexX(c.x + r) + 1);
			const int j0 = std::max(0, g.IndexZ(c.z - r) - 1), j1 = std::min(g.height - 1, g.IndexZ(c.z + r) + 1);
			for (int j = j0; j <= j1; ++j)
				for (int i = i0; i <= i1; ++i)
				{
					const double d = std::hypot(g.WorldX(i) - c.x, g.WorldZ(j) - c.z);
					if (d <= r) fn(i, j, d);
				}
		}

		/// Lumière de flamme (torche, brasero) à l'altitude absolue \p y. Effet de bord : w.pointLights.
		void TowerLight(World& w, const std::string& id, Vec2 p, double y, double radius, double intensity)
		{
			PointLightDef L;
			L.id = id; L.position = p; L.y = y; L.radiusM = radius; L.intensity = intensity;
			w.pointLights.push_back(std::move(L));
		}

		/// Inscrit une chaussée locale dans w.roadDist (distance au bord), sans toucher au
		/// relief (même règle que FlattenRoads). Effet de bord : w.roadDist.
		void TowerStampRoad(World& w, const Road& r)
		{
			const double half = r.widthM * 0.5, reach = half + 9.0;
			for (size_t k = 0; k + 1 < r.points.size(); ++k)
			{
				const Vec2 a = r.points[k], b = r.points[k + 1];
				const int i0 = std::max(0, w.roadDist.IndexX(std::min(a.x, b.x) - reach)), i1 = std::min(w.roadDist.width - 1, w.roadDist.IndexX(std::max(a.x, b.x) + reach));
				const int j0 = std::max(0, w.roadDist.IndexZ(std::min(a.z, b.z) - reach)), j1 = std::min(w.roadDist.height - 1, w.roadDist.IndexZ(std::max(a.z, b.z) + reach));
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i)
					{
						const float d = static_cast<float>(DistPointSegment({ w.roadDist.WorldX(i), w.roadDist.WorldZ(j) }, a, b) - half);
						w.roadDist.At(i, j) = std::min(w.roadDist.At(i, j), d);
					}
			}
		}

		/// Repère local de la rampe à l'abscisse \p s : axe du replat \p axis, normale vers le
		/// vide \p out (unitaire), tangente de montée \p up (unitaire, horizontale).
		void TowerFrame(const TowerShape& t, double s, Vec2& axis, Vec2& out, Vec2& up)
		{
			axis = TowerRampPoint(t, s);
			const Vec2 d = axis - t.center;
			const double l = std::max(1e-6, std::hypot(d.x, d.z));
			out = d * (1.0 / l);
			const double ds = 1e-4;
			Vec2 a = TowerRampPoint(t, std::min(1.0, s + ds)) - TowerRampPoint(t, std::max(0.0, s - ds));
			const double la = std::max(1e-9, std::hypot(a.x, a.z));
			up = a * (1.0 / la);
		}

		/// Nœud le plus proche de \p p parmi \p ids (-1 si vide).
		int TowerNearestOf(const SettlementLayout& L, const std::vector<int>& ids, Vec2 p)
		{
			int best = -1; double bd = 1e18;
			for (int id : ids)
			{
				const Vec2 q = L.nodes[static_cast<size_t>(id)].p;
				const double d = std::hypot(q.x - p.x, q.z - p.z);
				if (d < bd) { bd = d; best = id; }
			}
			return best;
		}

		/// Distance (m) au bord de la chaussée la plus proche en \p p (1e9 hors grille).
		double TowerRoadGap(const World& w, Vec2 p)
		{
			const int i = w.roadDist.IndexX(p.x), j = w.roadDist.IndexZ(p.z);
			return w.roadDist.InBounds(i, j) ? w.roadDist.At(i, j) : 1e9;
		}

		/// Palette des maisons de la ville-tour : maisons hautes de pierre à toit d'ardoise,
		/// échoppes ; vers les hauteurs (\p high), grandes maisons et manoirs (« ses plus belles
		/// demeures se déployaient vers les hauteurs », p.20).
		std::vector<TemplateRef> TowerPalette(bool high)
		{
			std::vector<TemplateRef> pal = { House("maison_haute_ardoise_6x8", 3), House("maison_ardoise_6x6", 2.5),
				House("maison_haute_6x8", 1.5), House("echoppe_4x8", 1.2), House("maison_6x6", 1) };
			if (high) { pal.push_back(House("grande_maison_ardoise_8x10", 2.5)); pal.push_back(House("manoir_8x12", 1.5)); }
			else pal.push_back(House("grande_maison_ardoise_8x10", 0.6));
			return pal;
		}
	}

	void BuildTowerCity(World& w, CityDef& c, SettlementLayout& L, size_t firstBuilding)
	{
		const TowerShape& t = c.tower;
		Rng rng(Hash(w.master.seed, HashString(c.id.c_str()), 1141));
		const Vec2 o = t.center;
		const double socleY = c.plateauY;
		const double half = t.ledgeM * 0.5;
		const double H = t.heightM;
		const double R = t.SocleR();
		const double turns = t.Turns();
		const double rampL = TowerLengthAt(t, 1.0);
		const Vec2 entry = Dir(t.entryRad);
		const Vec2 entrySide{ -entry.z, entry.x };
		const Lake* lake = nullptr;
		for (const Lake& l : w.lakes)
			if (l.name == "lac_" + c.id) lake = &l;
		const double waterY = lake ? lake->waterY : socleY - kTowerSocleAboveWaterM;
		const double bedY = waterY - 4.0;
		c.radiusM = R;

		// --- 1. Pic, socle et pied du socle sculptés dans la heightmap ; emprise urbaine (rues :
		//     chaussées et socle ; maisons : 1) ; parois en roche ocre (peinture 3).
		if (w.paint.data.empty())
			w.paint.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, uint8_t(0));
		TowerForDisc(w.height, o, R + 4.0, [&](int i, int j, double d) {
			const Vec2 q{ w.height.WorldX(i), w.height.WorldZ(j) };
			double& h = w.height.At(i, j);
			if (d > R)
			{
				// Quai du socle : chute jusqu'au lit du lac (le lit au-delà est creusé par CarveLake).
				h = Lerp(socleY, bedY, SmoothStep(R, R + 4.0, d));
				w.paint.At(i, j) = 3;
				return;
			}
			double s = -1.0, across = 0.0;
			const double off = TowerOffset(t, q, &s, &across);
			h = socleY + off;
			if (d <= t.topRadiusM) { w.urban.At(i, j) = 2; return; }       // plateforme du palais
			if (s >= 0.0) { w.urban.At(i, j) = across >= half - t.roadM - 0.5 ? 2 : 1; return; } // replat
			if (off <= 0.05) { w.urban.At(i, j) = 2; return; }              // socle (quai)
			w.paint.At(i, j) = 3;                                            // paroi rocheuse
			w.urban.At(i, j) = 1;
		});

		// --- 2. Réserve de la chaussée (tout le long de la rampe) et de la plateforme.
		for (double l = 0.0; l <= rampL; l += 2.0)
		{
			Vec2 axis, out, up;
			TowerFrame(t, TowerSAtLength(t, l), axis, out, up);
			w.Occupy(axis + out * (half - t.roadM * 0.5), t.roadM * 0.5 + 0.3);
		}
		w.Occupy(o, t.topRadiusM + 2.0);

		// --- 3. Façades des parois : au pied de chaque paroi, des murs de pierre (échelle 3 :
		//     6 × 9,4 m) jusqu'au replat du dessus, fenêtres en plein cintre au hasard ; toits
		//     d'ardoise et tourelles sur la crête (entre la façade et le parapet du dessus).
		//     Paroi intérieure d'un replat (vers le pic) : de son altitude à celle du replat
		//     suivant (un tour plus haut) ou de la plateforme ; paroi extérieure du premier tour
		//     (face au lac) : du socle au replat.
		constexpr double kWallS = 3.0, kWallW = 2.0 * kWallS, kWallHt = 3.12 * kWallS;
		int facadePanels = 0;
		auto facade = [&](Vec2 foot, Vec2 tan, double y0, double y1, bool crest, Vec2 inward) {
			if (y1 - y0 < 1.0) return;
			const int rows = static_cast<int>(std::ceil((y1 - y0) / kWallHt));
			const double yaw = YawAlong(tan); // +Z local vers le pic (face décorée vers le vide)
			// Rangées alignées sur la crête (y1) : la rangée du bas s'enfonce sous le replat ou le
			// socle (cachée), aucune ne dépasse au-dessus du replat du dessus.
			const double yBase = y1 - rows * kWallHt;
			for (int row = 0; row < rows; ++row)
			{
				const double u = rng.Next();
				const char* mesh = (row == 0 || u < 0.55) ? "Wall_UnevenBrick_Straight"
					: (u < 0.85 ? "Wall_UnevenBrick_Window_Thin_Round" : "Wall_UnevenBrick_Window_Wide_Round");
				TowerProp(w, mesh, foot, yaw, kWallS, static_cast<uint8_t>(kSolid | kPiece), 0, yBase + row * kWallHt);
				++facadePanels;
			}
			// Toit d'ardoise sur l'interstice entre la crête du mur et la marche de la paroi (une
			// maille de heightmap, au quart de la paroi depuis le mur).
			if (crest && rng.Chance(0.6))
				TowerProp(w, "Roof_Slate_4x4", foot + inward * (t.bandM * 0.25), yaw, 1.0, 0, 0, y1 - 0.4);
		};
		{
			// Le long de chaque replat, un panneau tous les 6 m (mesurés sur la ligne de pied).
			double l = 0.0;
			int turret = 0;
			while (l < rampL)
			{
				const double s = TowerSAtLength(t, l);
				Vec2 axis, out, up;
				TowerFrame(t, s, axis, out, up);
				const double r = std::hypot(axis.x - o.x, axis.z - o.z);
				const double hHere = TowerRampHeight(t, s);
				const double sUp = s + 1.0 / turns;
				// Mur : bord X local le long de la tangente, sens trigonométrique (le +Z local regarde le pic).
				const Vec2 tan{ -out.z, out.x };
				// Paroi intérieure (au pied, côté pic).
				const double footR = r - half - 0.1;
				const Vec2 footIn = o + out * footR;
				const double yLow = socleY + std::min(hHere, TowerRampHeight(t, TowerSAtLength(t, l + kWallW))) - 0.3;
				// Crête : le plus bas du replat du dessus sur la largeur du panneau (une volée peut y
				// commencer) — le mur ne dépasse jamais du replat qu'il soutient.
				const double sUp2 = TowerSAtLength(t, TowerLengthAt(t, std::min(1.0, sUp)) + kWallW * r / std::max(10.0, footR));
				const double yTop = socleY + (sUp <= 1.0 ? std::min(TowerRampHeight(t, sUp), TowerRampHeight(t, sUp2)) : H) + 0.4;
				facade(footIn, tan, yLow, yTop, true, out * -1.0);
				// Tourelle sur la crête, tous les ~70 m (« des tours dans les angles », p.20).
				if (sUp <= 1.0 && l > 40.0 && (++turret % 12) == 0)
				{
					const Vec2 tp = o + out * (footR - t.bandM * 0.75); // replat haut de la paroi, devant le parapet
					TowerProp(w, "Turret_Corner_1", tp, YawAlong(tan), 1.2, kSolid, 0, socleY + TowerRampHeight(t, sUp) - 0.5, 2.0);
				}
				// Pas de 6 m le long du pied de la paroi (rayon footR) : dl sur l'axe = 6·r/footR.
				l += kWallW * r / std::max(10.0, footR);
			}
			// Paroi extérieure du premier tour (face au lac), du socle au replat ; arcs-boutants
			// contre elle là où elle est haute (tous les ~48 m).
			const double lFirst = TowerLengthAt(t, 1.0 / turns);
			int k = 0;
			for (double lo = 0.0; lo < lFirst; ++k)
			{
				const double s = TowerSAtLength(t, lo);
				Vec2 axis, out, up;
				TowerFrame(t, s, axis, out, up);
				const double r = std::hypot(axis.x - o.x, axis.z - o.z);
				const double hHere = TowerRampHeight(t, s);
				const Vec2 tan{ -out.z, out.x };
				const double footR = r + half + t.bandM + 0.1;
				const Vec2 footOut = o + out * footR;
				const double hTop = std::min(hHere, TowerRampHeight(t, TowerSAtLength(t, lo + kWallW * r / footR)));
				facade(footOut, tan, socleY - 0.3, socleY + hTop + 0.4, true, out * -1.0);
				if (hHere > 24.0 && k % 8 == 0)
					TowerProp(w, "Flyingbuttress_1", footOut + out * 6.2, YawAlong(out * -1.0), 2.0, 0, 0, socleY - 0.2);
				lo += kWallW * r / footR;
			}
		}

		// --- 4. Chaussée : parapet au bord du vide (ouvert au pied de la rampe, sur le socle),
		//     volées d'escalier (Stairs_Monumental_1 à l'échelle 0,85 : 2,55 × 1,53 m), arches
		//     au-dessus de la chaussée sur les paliers, torches.
		const double P = TowerStairPeriodM(t), run = TowerStairRunM(t);
		int flights = 0, arches = 0, lights = 0;
		for (double l = 25.0; l <= rampL; l += 2.0)
		{
			Vec2 axis, out, up;
			TowerFrame(t, TowerSAtLength(t, l), axis, out, up);
			const Vec2 tan{ -out.z, out.x };
			const Vec2 p = axis + out * (half - 0.3);
			TowerProp(w, "Bridge_Stone_Parapet_1", p, YawAlong(tan), 1.0, static_cast<uint8_t>(kSolid | kPiece), 1,
				socleY + TowerRampHeight(t, TowerSAtLength(t, l)) - 0.05);
		}
		const int periods = static_cast<int>(std::lround(rampL / P));
		for (int k = 0; k < periods; ++k)
		{
			const double l0 = k * P + (P - run); // bas de la volée
			const int nf = static_cast<int>(std::lround(run / 2.55));
			for (int f = 0; f < nf; ++f)
			{
				const double lm = l0 + 2.55 * (f + 0.5);
				Vec2 axis, out, up;
				TowerFrame(t, TowerSAtLength(t, lm), axis, out, up);
				const Vec2 p = axis + out * (half - t.roadM * 0.5);
				const double baseY = socleY + TowerRampHeight(t, TowerSAtLength(t, l0)) + 1.53 * f;
				TowerProp(w, "Stairs_Monumental_1", p, YawFacing(up), 0.85, 0, 0, baseY);
				++flights;
			}
			// Arche au-dessus de la chaussée au milieu du palier, une période sur deux.
			if (k % 2 == 1)
			{
				const double la = k * P + (P - run) * 0.5;
				Vec2 axis, out, up;
				TowerFrame(t, TowerSAtLength(t, la), axis, out, up);
				const double sc = (t.roadM + 1.2) / 4.9;
				const Vec2 p = axis + out * (half - t.roadM * 0.5);
				TowerProp(w, "Arch_Stone_1", p, YawAlong(out), sc, 0, 0, socleY + TowerRampHeight(t, TowerSAtLength(t, la)) - 0.1);
				++arches;
			}
			// Torche au parapet, une période sur deux (lumière : flamme à 1,9 m).
			if (k % 2 == 0)
			{
				const double lt = k * P + 10.0;
				Vec2 axis, out, up;
				TowerFrame(t, TowerSAtLength(t, lt), axis, out, up);
				const Vec2 p = axis + out * (half - 0.9);
				const double y = socleY + TowerRampHeight(t, TowerSAtLength(t, lt));
				TowerProp(w, "Torch_Pole_1", p, 0.0, 1.0, kSolid, 1, y, 0.2);
				TowerLight(w, c.id + "_rampe_" + std::to_string(lights++), p, y + 3.1, 10.0, 1.6);
			}
		}

		// --- 5. Ponton de bois (un seul, BD p.19) : du bord du socle à la rive, dans la direction
		//     d'entrée ; tablier de 6 m au niveau du socle (pieux jusqu'à l'eau), garde-corps,
		//     poteaux à torche ; levée de terre sur la rive ; gibets et poteau indicateur à la
		//     tête du ponton ; chemin pavé jusqu'à l'ancre des routes.
		double shoreR = (lake ? LakeRadiusAt(*lake, t.entryRad) : t.lakeRadiusM) + 2.0;
		while (shoreR < t.lakeRadiusM * 1.3 && w.HeightAt(o + entry * shoreR) < waterY + 0.25) shoreR += 2.0;
		const double deckEnd = shoreR + 6.0;
		// Levée de terre (7 m de large, 30 m) du niveau du tablier au terrain naturel.
		TowerForDisc(w.height, o + entry * (deckEnd + 15.0), 26.0, [&](int i, int j, double) {
			const Vec2 q{ w.height.WorldX(i) - o.x, w.height.WorldZ(j) - o.z };
			const double along = q.x * entry.x + q.z * entry.z, side = std::fabs(q.x * entrySide.x + q.z * entrySide.z);
			if (along < shoreR - 4.0 || along > deckEnd + 30.0 || side > 6.0) return;
			const double k = SmoothStep(deckEnd, deckEnd + 30.0, along) * 0.7 + SmoothStep(3.5, 6.0, side) * 0.3;
			double& h = w.height.At(i, j);
			h = std::max(h, Lerp(socleY, h, Clamp(k, 0.0, 1.0)));
			w.urban.At(i, j) = 2;
		});
		int deckTiles = 0;
		for (double r = R - 1.0; r <= deckEnd; r += 2.0)
		{
			for (int col = -1; col <= 1; ++col)
			{
				const Vec2 p = o + entry * r + entrySide * (col * 2.0);
				TowerProp(w, "Boardwalk_Deck_1", p, YawAlong(entry), 1.0, static_cast<uint8_t>(kSolid | kPiece), 0, socleY);
				++deckTiles;
			}
			for (int sd = -1; sd <= 1; sd += 2)
				TowerProp(w, "Prop_WoodenFence_Single", o + entry * r + entrySide * (sd * 3.1), YawAlong(entry), 1.0, 0, 1, socleY);
			w.Occupy(o + entry * r, 3.6);
		}
		for (double r = R + 30.0; r < shoreR - 10.0; r += 40.0)
		{
			const int sd = (static_cast<int>(r / 40.0) % 2) ? 1 : -1;
			const Vec2 p = o + entry * r + entrySide * (sd * 3.4);
			TowerProp(w, "Torch_Pole_1", p, 0.0, 1.0, 0, 1, socleY);
			if (static_cast<int>(r / 40.0) % 2 == 0) TowerLight(w, c.id + "_ponton_" + std::to_string(lights++), p, socleY + 3.1, 10.0, 1.5);
		}
		{
			const Vec2 head = o + entry * (deckEnd + 8.0);
			for (int sd = -1; sd <= 1; sd += 2)
			{
				const Vec2 gp = head + entrySide * (sd * 9.0) + entry * 6.0;
				AddProp(w, "Gallows_1", gp, YawAlong(entry), 1.0, kSolid, 0, 1.6);
				w.Occupy(gp, 2.4);
			}
			const Vec2 sp = head + entrySide * 4.6 - entry * 2.0;
			AddProp(w, "Signpost_1", sp, YawFacing(entry), 1.0, kSolid, 1, 0.3);
			w.Occupy(sp, 1.0);
		}
		// Chemin pavé de la tête du ponton à l'ancre des routes (rive).
		{
			Road path;
			path.fromId = c.id; path.widthM = 5.0; path.path = true;
			const Vec2 anchor = CityRoadAnchor(c);
			const Vec2 a = o + entry * (deckEnd + 2.0);
			const double len = std::hypot(anchor.x - a.x, anchor.z - a.z);
			for (double d = 0.0; d <= len; d += 4.0)
			{
				const Vec2 q = a + (anchor - a) * (d / std::max(1.0, len));
				path.points.push_back(q);
				path.profileY.push_back(w.HeightAt(q));
			}
			if (path.points.size() >= 2)
			{
				TowerStampRoad(w, path);
				PaveAlong(w, path.points, 2, "RockPath_Irregular_Wide", 0.0, o);
				for (const Vec2& q : path.points) w.Occupy(q, 3.0);
				w.roads.push_back(std::move(path));
			}
		}

		// --- 6. Place du socle (nœud 0 du graphe), juste avant le pied de la rampe (sens inverse) :
		//     auberge, étals du marché ; geôles plus loin sur le socle.
		auto socleWidthAt = [&](double a, double& rIn) {
			// Bord intérieur du socle (pied de la paroi extérieure du premier tour) à l'angle a.
			double a0 = a - t.entryRad;
			a0 -= 2.0 * 3.14159265358979323846 * std::floor(a0 / (2.0 * 3.14159265358979323846));
			const double s0 = a0 / (2.0 * 3.14159265358979323846 * turns);
			rIn = t.OuterR() - (t.OuterR() - t.InnerR()) * s0 + half + t.bandM + 0.6;
			return R - rIn;
		};
		const double plazaA = t.entryRad - 0.30;
		double plazaIn = 0.0;
		socleWidthAt(plazaA, plazaIn);
		const Vec2 plaza = o + Dir(plazaA) * (0.5 * (plazaIn + R));
		AddNode(L, plaza);
		for (int k = 0; k < 8; ++k)
		{
			const int id = AddNode(L, plaza + Dir(k * kPi / 4.0 + 0.2) * 5.0);
			L.plazaRing.push_back(id);
			Link(L, 0, id);
		}
		for (size_t k = 0; k < L.plazaRing.size(); ++k) Link(L, L.plazaRing[k], L.plazaRing[(k + 1) % L.plazaRing.size()]);
		w.Occupy(plaza, 6.0);
		// Auberge (lore : « au pied », fréquentée par les nains) : sur le socle, porte vers la place.
		{
			L.innDoor = plaza;
			const TemplateRef inn = InnFor(c);
			for (int attempt = 0; attempt < 16; ++attempt)
			{
				const double a = plazaA - 0.10 - 0.025 * attempt;
				double rIn = 0.0;
				const double wdt = socleWidthAt(a, rIn);
				if (wdt < 2.0 * inn.halfD + 4.0) continue;
				const Vec2 out = Dir(a);
				const Vec2 ctr = o + out * (rIn + inn.halfD + 1.0);
				const int b = TryPlaceBuilding(w, c, inn, ctr, out, "Auberge");
				if (b >= 0)
				{
					w.buildings[static_cast<size_t>(b)].isInn = true;
					L.innDoor = w.buildings[static_cast<size_t>(b)].doorPoint;
					L.buildingIndices.push_back(b);
					break;
				}
			}
		}
		// Étals du marché sur la place (herbes et réactifs des marais, lore v1.0.58).
		{
			static const char* kGoods[] = { "Shelf_Small_Bottles", "Basket_1", "Crate_Wooden", "Barrel" };
			for (int k = 0; k < 4; ++k)
			{
				const double a = plazaA + 0.06 + 0.035 * k;
				double rIn = 0.0;
				socleWidthAt(a, rIn);
				const Vec2 out = Dir(a), p = o + out * (R - 6.0);
				if (w.IsOccupied(p, 1.4)) continue;
				AddProp(w, k % 2 ? "Awning_Striped_1" : "Stall_Empty", p, YawFacing(out), 1.0, kSolid, 0, 1.0);
				AddProp(w, kGoods[k], p - out * 1.4 + Vec2{ -out.z, out.x } * 0.8, rng.Range(0.0, 360.0), 1.0, 0, 2);
				w.OccupyRect(p, 1.6, 1.0, YawFacing(out));
				L.stallFronts.push_back(p - out * 1.6);
			}
		}

		// --- 7. Geôles (BD p.22 : voûtes, gros piliers, banquettes, anneaux, soupiraux et rais de
		//     lumière) : salle adossée à la face au lac du premier tour, sur le socle (24 × 12 m) ;
		//     au fond, cinq cellules voûtées (Dungeon_Vault_1, 4,8 × 4 m, ouverture vers la salle)
		//     fermées de grilles ; salle à piliers massifs sous un plafond de dalles (4,4 m) ;
		//     façade sur le quai avec porte, soupiraux et enseigne de la prison.
		Vec2 prisonDoor = plaza;
		{
			const double a = plazaA - 0.75;
			double rIn = 0.0;
			const double wdt = socleWidthAt(a, rIn);
			const Vec2 out = Dir(a), tan{ -out.z, out.x };
			const double depth = 12.0, width = 24.0, ceilY = 4.4;
			if (wdt >= depth + 5.0)
			{
				const Vec2 back = o + out * rIn; // pied de la façade du premier tour
				auto at = [&](double u, double v) { return back + out * u + tan * v; };
				// Cellules : axe de la voûte (Z local) vers la salle, murets latéraux (X local).
				for (int i = 0; i < 5; ++i)
				{
					const double v = -9.6 + 4.8 * i;
					TowerProp(w, "Dungeon_Vault_1", at(2.0, v), YawAlong(tan), 1.0, 0, 0, socleY);
					for (int g = -1; g <= 1; ++g)
						TowerProp(w, "Dungeon_Grate_1", at(4.05, v + 1.2 * g), YawAlong(tan), 1.0, kSolid, 1, socleY, 0.6);
					TowerProp(w, "Bench", at(0.9, v), YawFacing(out * -1.0), 1.0, kSolid, 1, socleY, 0.5);
					TowerProp(w, "Chain_Hook_1", at(0.5, v + 1.3), 0.0, 1.0, 0, 2, socleY + 0.6);
				}
				// Salle : deux rangées de piliers massifs, plafond de dalles.
				for (int i = 0; i < 4; ++i)
					for (int j = 0; j < 2; ++j)
						TowerProp(w, "Dungeon_Pillar_1", at(6.5 + 3.5 * j, -7.2 + 4.8 * i), 0.0, 1.0, kSolid, 0, socleY, 0.6);
				for (double u = 5.0; u < depth; u += 2.0)
					for (double v = -width * 0.5 + 1.0; v < width * 0.5; v += 2.0)
						TowerProp(w, "Floor_Brick", at(u, v), YawAlong(tan), 1.0, static_cast<uint8_t>(kSolid | kPiece), 0, socleY + ceilY);
				// Murs (échelle 1,5 : 3 × 4,7 m) : façade sur le quai (porte au milieu, soupiraux), flancs.
				const double ws = 1.5;
				for (int k = 0; k < 8; ++k)
				{
					const double v = -width * 0.5 + 1.5 + 3.0 * k;
					const bool door = k == 4;
					TowerProp(w, door ? "Wall_UnevenBrick_Door_Round" : "Wall_UnevenBrick_Straight", at(depth + 0.2, v), YawAlong(tan * -1.0), ws,
						static_cast<uint8_t>(kSolid | kPiece), 0, socleY);
					if (!door && k % 2 == 1) TowerProp(w, "Dungeon_Window_1", at(depth + 0.35, v), YawAlong(tan * -1.0), 1.0, 0, 1, socleY + 3.4);
				}
				for (int sd = -1; sd <= 1; sd += 2)
					for (int k = 0; k < 4; ++k)
						TowerProp(w, "Wall_UnevenBrick_Straight", at(1.5 + 3.0 * k, sd * (width * 0.5 + 0.2)), YawAlong(out * static_cast<double>(sd)), ws,
							static_cast<uint8_t>(kSolid | kPiece), 0, socleY);
				const double doorV = -width * 0.5 + 1.5 + 12.0;
				prisonDoor = at(depth + 2.4, doorV);
				TowerProp(w, "Door_2_Round", at(depth + 0.3, doorV - 0.8), YawAlong(tan * -1.0) + 70.0, ws, 0, 1, socleY);
				for (int k = 0; k < 3; ++k)
				{
					const Vec2 tp = at(8.0, -8.0 + 8.0 * k);
					TowerProp(w, "Torch_Metal", tp, YawFacing(out), 1.0, 0, 1, socleY + 2.0);
					TowerLight(w, c.id + "_geoles_" + std::to_string(k), tp, socleY + 2.4, 9.0, 1.4);
				}
				AddProp(w, "Sign_Service_prison", at(depth + 1.1, doorV + 2.6), YawAlong(out), 1.0, kSolid, 1, 0.5);
				w.OccupyRect(at(depth * 0.5 + 0.5, 0.0), width * 0.5 + 0.6, depth * 0.5 + 1.0, YawFacing(out));
			}
			else w.Warn("ville-tour " + c.id + " : socle trop etroit pour les geoles");
		}

		// --- 8. Maisons le long de la rampe : rangée côté pic (dos à la paroi, porte vers la
		//     chaussée), sur les paliers (GroundOk écarte les volées). Les plus belles vers le haut.
		int houses = 0;
		{
			double l = 30.0;
			while (l < rampL - 25.0)
			{
				const double s = TowerSAtLength(t, l);
				const std::vector<TemplateRef> pal = TowerPalette(s > 0.5);
				const TemplateRef& tm = PickWeighted(pal, rng.Next());
				const double hx = tm.halfW + 0.6, hz = tm.halfD + 0.6;
				const double footR = std::sqrt(hx * hx + hz * hz);
				const double u = -half + footR + 0.4;
				if (u + tm.halfD > half - t.roadM - 0.6) { l += 2.0; continue; }
				Vec2 axis, out, up;
				TowerFrame(t, s, axis, out, up);
				const int b = TryPlaceBuilding(w, c, tm, axis + out * u, out, "Maison");
				if (b >= 0) { L.buildingIndices.push_back(b); ++houses; l += 2.0 * tm.halfW + 1.2 + rng.Range(0.0, 1.0); }
				else l += 2.0;
			}
		}

		// --- 9. Cimetière (point de résurrection) : sur la rive, à côté du chemin pavé.
		if (!w.master.graveyardExcluded.count(c.id))
		{
			L.hasGraveyard = true;
			const Vec2 anchor = CityRoadAnchor(c);
			Vec2 gy = anchor - entry * 90.0 + entrySide * 28.0;
			for (int attempt = 0; attempt < 16; ++attempt)
			{
				if (!w.IsWater(gy) && !w.IsOccupied(gy, 4.0) && w.InPlayableBounds(gy, 60.0) && TowerRoadGap(w, gy) > 8.0) break;
				gy = anchor - entry * (90.0 - 8.0 * attempt) + entrySide * ((attempt % 2 ? -1.0 : 1.0) * (28.0 + 4.0 * attempt));
			}
			L.graveyard = gy;
			AddProp(w, "CandleStick_Stand", gy, 0.0, 1.0, 0, 1);
			PlaceOldGraves(w, gy, o - gy, GraveCountFor(c), rng);
			w.Occupy(gy, 2.5);
		}
		else L.graveyard = plaza;

		// --- 10. Graphe des PNJ : place → pied de la rampe → chaussée jusqu'au sommet (nœuds tous
		//     les 9 m) → parvis du palais ; place → ponton → rive ; place → geôles et auberge.
		{
			int prev = TowerNearestOf(L, L.plazaRing, o + entry * (t.OuterR() + half));
			for (double l = 0.0; l <= rampL; l += 9.0)
			{
				Vec2 axis, out, up;
				TowerFrame(t, TowerSAtLength(t, l), axis, out, up);
				const int id = AddNode(L, axis + out * (half - t.roadM * 0.5));
				Link(L, prev, id);
				prev = id;
			}
			const Vec2 topEdge = TowerRampPoint(t, 1.0);
			const int parvis = AddNode(L, o + (topEdge - o) * ((t.topRadiusM - 6.0) / std::max(1.0, std::hypot(topEdge.x - o.x, topEdge.z - o.z))));
			Link(L, prev, parvis);
			prev = TowerNearestOf(L, L.plazaRing, o + entry * R);
			for (double r = R - 2.0; r <= deckEnd + 20.0; r += 9.0)
			{
				const int id = AddNode(L, o + entry * r);
				Link(L, prev, id);
				prev = id;
			}
			const int pd = AddNode(L, prisonDoor);
			Link(L, TowerNearestOf(L, L.plazaRing, prisonDoor), pd);
			const int id = AddNode(L, L.innDoor);
			Link(L, TowerNearestOf(L, L.plazaRing, L.innDoor), id);
		}

		// --- 11. Enseignes de service (la prison a la sienne, à la porte des geôles).
		{
			CityDef signs = c;
			signs.services.erase(std::remove(signs.services.begin(), signs.services.end(), "prison"), signs.services.end());
			PlaceServiceSigns(w, signs, firstBuilding);
		}
		L.startSpot = plaza - entry * 2.0;
		L.startYawDeg = YawFacing(entry * -1.0);
		if (houses < 120) w.Warn("ville-tour " + c.id + " : seulement " + std::to_string(houses) + " maisons");
		(void)facadePanels; (void)flights; (void)arches; (void)deckTiles;
	}

	void BuildSettlements(World& w)
	{
		w.urban.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, w.urban.data.empty() ? uint8_t(0) : uint8_t(0));
		for (CityDef& c : w.cities)
		{
			SettlementLayout L;
			L.cityId = c.id;
			const size_t firstBuilding = w.buildings.size();
			// v1.0.136 : Camp de l'Oracle bâti selon le plan (Oracle.cpp), pas de village générique.
			if (c.layout == "oracle")
			{
				BuildOracleCamp(w, c, L);
				w.layouts.push_back(std::move(L));
				continue;
			}
			// v1.0.141 : Magistrya, ville-tour en spirale sur son pic au milieu du lac.
			if (c.layout == "ville_tour" && c.tower.enabled)
			{
				BuildTowerCity(w, c, L, firstBuilding);
				w.layouts.push_back(std::move(L));
				continue;
			}
			const double R = c.radiusM;
			const double Rp = Clamp(R * 0.3, 7.0, 16.0);
			Rng rng(Hash(w.master.seed, HashString(c.id.c_str()), 4242));

			// --- Rues rayonnantes : directions des routes qui arrivent ici.
			std::vector<double> angles;
			for (const Road& r : w.roads)
			{
				if (r.points.size() < 4 || (r.fromId != c.id && r.toId != c.id)) continue;
				// Premier point de la route à plus de R du centre (sortie de ville).
				const bool fromHere = r.fromId == c.id;
				Vec2 exitP = fromHere ? r.points.back() : r.points.front();
				for (size_t k = 0; k < r.points.size(); ++k)
				{
					const Vec2 p = fromHere ? r.points[k] : r.points[r.points.size() - 1 - k];
					if (std::hypot(p.x - c.center.x, p.z - c.center.z) > R * 0.85) { exitP = p; break; }
				}
				const double a = std::atan2(exitP.z - c.center.z, exitP.x - c.center.x);
				bool dup = false;
				for (double b : angles) if (std::fabs(std::remainder(a - b, 2.0 * kPi)) < 0.45) dup = true;
				if (!dup) angles.push_back(a);
			}
			const size_t roadSpokes = angles.size();
			// Complète jusqu'au minimum avec des rues aux angles les plus libres.
			while (static_cast<int>(angles.size()) < MinSpokes(c.kind, c.procedural))
			{
				double bestA = rng.Range(0.0, 2.0 * kPi), bestGap = -1.0;
				for (int k = 0; k < 24; ++k)
				{
					const double a = k * kPi / 12.0 + rng.Range(-0.08, 0.08);
					double gap = 1e9;
					for (double b : angles) gap = std::min(gap, std::fabs(std::remainder(a - b, 2.0 * kPi)));
					// La rue ne doit pas plonger dans l'eau.
					const Vec2 tip = c.center + Dir(a) * (R - 2.0);
					if (w.IsWater(tip) || w.IsWater(c.center + Dir(a) * (R * 0.6))) gap *= 0.1;
					if (angles.empty()) gap = 1.0 + rng.Next();
					if (gap > bestGap) { bestGap = gap; bestA = a; }
				}
				angles.push_back(bestA);
			}

			// --- Graphe : place (noeud 0) + anneau de ronde.
			AddNode(L, c.center);
			for (int k = 0; k < 8; ++k)
			{
				const int id = AddNode(L, c.center + Dir(k * kPi / 4.0 + 0.2) * (Rp * 0.8));
				L.plazaRing.push_back(id);
				Link(L, 0, id);
			}
			for (size_t k = 0; k < L.plazaRing.size(); ++k)
				Link(L, L.plazaRing[k], L.plazaRing[(k + 1) % L.plazaRing.size()]);

			// Place : marque urbaine + occupation (dégagée).
			w.Occupy(c.center, Rp + 1.0);

			// v1.0.140 — ville en gradins : plateforme du sommet (palais) hors des rues et des lots.
			auto onSummit = [&](Vec2 q) {
				return c.acropole.enabled && std::hypot(q.x - c.acropole.summit.x, q.z - c.acropole.summit.z) < c.acropole.topRadiusM + 6.0;
			};
			// ... et pied de l'escalier monumental (étals et statues de la place s'en écartent).
			auto inStair = [&](Vec2 q) {
				if (!c.acropole.enabled) return false;
				const Vec2 d = q - c.acropole.summit;
				const double along = d.x * c.acropole.stairDir.x + d.z * c.acropole.stairDir.z;
				const double across = std::fabs(-d.x * c.acropole.stairDir.z + d.z * c.acropole.stairDir.x);
				return along > c.acropole.topRadiusM - 2.0 && along < c.acropole.stairBottomM + 6.0 && across < c.acropole.stairHalfWidthM + 2.5;
			};

			// --- Rues (polylignes) et noeuds.
			std::vector<std::vector<Vec2>> streets;
			for (size_t si = 0; si < angles.size(); ++si)
			{
				const double a = angles[si];
				// Légère courbure pour les rues complémentaires (aspect organique).
				const double bend = (si >= roadSpokes) ? rng.Range(-0.18, 0.18) : 0.0;
				std::vector<Vec2> st;
				const double len = R + (si < roadSpokes ? 6.0 : -1.0);
				for (double d = Rp; d <= len; d += 3.0)
				{
					const double t = (d - Rp) / std::max(1.0, len - Rp);
					const Vec2 q = c.center + Dir(a + bend * t) * d;
					// v1.0.140 : une rue s'arrête au pied de la plateforme du palais (acropole).
					if (onSummit(q)) break;
					st.push_back(q);
				}
				if (st.size() < 2) continue;
				int prev = Nearest(L, st.front());
				for (size_t k = 0; k < st.size(); k += 3)
				{
					const int id = AddNode(L, st[k]);
					Link(L, prev, id);
					prev = id;
				}
				if (si < roadSpokes) L.gatePoints.push_back(st.back());
				streets.push_back(st);
			}
			// Rue circulaire (villes moyennes+).
			const bool ring = (c.kind == "ville" || c.kind == "grande_ville" || c.kind == "capitale" || c.kind == "forteresse");
			if (ring)
			{
				const double rr = R * 0.62;
				std::vector<Vec2> st;
				std::vector<int> ringIds;
				for (int k = 0; k <= 48; ++k) st.push_back(c.center + Dir(k * 2.0 * kPi / 48.0) * rr);
				if (c.acropole.enabled)
				{
					// v1.0.140 : l'anneau est interrompu par la plateforme du palais (tronçons
					// hors du sommet, nœuds reliés seulement entre voisins du même tronçon).
					std::vector<Vec2> run;
					int prevId = -1;
					for (int k = 0; k <= 48; ++k)
					{
						const Vec2 q = st[static_cast<size_t>(k)];
						if (onSummit(q))
						{
							if (run.size() >= 2) streets.push_back(run);
							run.clear();
							prevId = -1;
							continue;
						}
						run.push_back(q);
						if (k % 3 == 0 && k < 48)
						{
							const int id = AddNode(L, q);
							ringIds.push_back(id);
							if (prevId >= 0) Link(L, prevId, id);
							prevId = id;
						}
					}
					if (run.size() >= 2) streets.push_back(run);
					// Raccord au point de départ (k = 48 = k = 0) si l'anneau n'y est pas coupé.
					if (!ringIds.empty() && prevId >= 0 && prevId != ringIds.front() && !onSummit(st.front()))
						Link(L, prevId, ringIds.front());
					st.clear();
				}
				else
				{
					for (int k = 0; k < 48; k += 3) ringIds.push_back(AddNode(L, st[static_cast<size_t>(k)]));
					for (size_t k = 0; k < ringIds.size(); ++k) Link(L, ringIds[k], ringIds[(k + 1) % ringIds.size()]);
				}
				// Raccorde l'anneau aux rues rayonnantes les plus proches.
				for (int id : ringIds)
				{
					const int nb = Nearest(L, L.nodes[static_cast<size_t>(id)].p, id);
					if (nb >= 0 && std::find(ringIds.begin(), ringIds.end(), nb) == ringIds.end()
						&& std::hypot(L.nodes[static_cast<size_t>(nb)].p.x - L.nodes[static_cast<size_t>(id)].p.x,
							L.nodes[static_cast<size_t>(nb)].p.z - L.nodes[static_cast<size_t>(id)].p.z) < 9.0)
						Link(L, id, nb);
				}
				if (!st.empty()) streets.push_back(st);
			}
			// v1.0.140 — escalier monumental (avant les lots : son couloir est réservé).
			if (c.acropole.enabled) BuildAcropoleStairs(w, c, L);
			// v1.0.140 — port de la capitale en gradins (avant les lots et les faubourgs).
			if (c.acropole.enabled && std::find(c.services.begin(), c.services.end(), "port") != c.services.end())
				BuildHarbour(w, c, L, Palette(c), rng);
			// v1.0.140 (retour du propriétaire : « la quantité de maisons est trop faible, c'est la
			// capitale ») — ville en gradins : rues en arc sur chaque replat (deux si le replat est large), autour du
			// sommet, dans la ville et hors du couloir de l'escalier ; les lots la bordent des deux
			// côtés (boucle des lots ci-dessous) : les terrasses se couvrent de maisons comme dans
			// la BD (T10 p.37). Nœuds tous les 9 m, reliés en chaîne et à la rue la plus proche.
			if (c.acropole.enabled)
			{
				const AcropoleShape& a = c.acropole;
				const double period = a.bankM + a.terraceM;
				// Replats larges (≥ 40 m) : deux rues (au tiers et aux deux tiers), quatre rangées de maisons.
				const int rows = a.terraceM >= 40.0 ? 2 : 1;
				for (int k = 0; k < a.terraces * rows; ++k)
				{
					const double rr = a.topRadiusM + (k / rows) * period + a.bankM + a.terraceM * ((k % rows) + 1.0) / (rows + 1.0);
					const int n = std::max(24, static_cast<int>(std::ceil(2.0 * kPi * rr / 3.0)));
					std::vector<Vec2> run;
					int prevId = -1;
					auto flush = [&]() { if (run.size() >= 4) streets.push_back(run); run.clear(); prevId = -1; };
					for (int q = 0; q <= n; ++q)
					{
						const Vec2 pt = a.summit + Dir(q * 2.0 * kPi / n) * rr;
						if (std::hypot(pt.x - c.center.x, pt.z - c.center.z) > R - 4.0 || inStair(pt) || w.IsWater(pt)) { flush(); continue; }
						run.push_back(pt);
						if (q % 3 == 0)
						{
							const int id = AddNode(L, pt);
							if (prevId >= 0) Link(L, prevId, id);
							else
							{
								const int nb = Nearest(L, pt, id);
								if (nb >= 0) Link(L, id, nb);
							}
							prevId = id;
						}
					}
					flush();
				}
			}
			// Dégage les rues (occupation) et marque les rues dans la carte urbaine.
			for (const auto& st : streets)
				for (const Vec2& p : st)
				{
					w.Occupy(p, kStreetHalf + 0.4);
					const int i = w.urban.IndexX(p.x), j = w.urban.IndexZ(p.z);
					for (int dj = -1; dj <= 1; ++dj)
						for (int di = -1; di <= 1; ++di)
							if (w.urban.InBounds(i + di, j + dj)) w.urban.At(i + di, j + dj) = 2;
				}
			// Place pavée : marque 2 (terre battue au splat).
			{
				const int i0 = w.urban.IndexX(c.center.x - Rp), i1 = w.urban.IndexX(c.center.x + Rp);
				const int j0 = w.urban.IndexZ(c.center.z - Rp), j1 = w.urban.IndexZ(c.center.z + Rp);
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i)
						if (w.urban.InBounds(i, j) && std::hypot(w.urban.WorldX(i) - c.center.x, w.urban.WorldZ(j) - c.center.z) <= Rp)
							w.urban.At(i, j) = 2;
				// Emprise urbaine (1) jusqu'au rayon.
				const int k0 = w.urban.IndexX(c.center.x - R), k1 = w.urban.IndexX(c.center.x + R);
				const int l0 = w.urban.IndexZ(c.center.z - R), l1 = w.urban.IndexZ(c.center.z + R);
				for (int j = l0; j <= l1; ++j)
					for (int i = k0; i <= k1; ++i)
						if (w.urban.InBounds(i, j) && w.urban.At(i, j) == 0
							&& std::hypot(w.urban.WorldX(i) - c.center.x, w.urban.WorldZ(j) - c.center.z) <= R)
							w.urban.At(i, j) = 1;
			}

			// --- Auberge face à la place (entre deux rues).
			{
				std::vector<double> sorted = angles;
				std::sort(sorted.begin(), sorted.end());
				double bestMid = 0.0, bestGap = -1.0;
				for (size_t k = 0; k < sorted.size(); ++k)
				{
					const double a0 = sorted[k], a1 = (k + 1 < sorted.size()) ? sorted[k + 1] : sorted[0] + 2.0 * kPi;
					if (a1 - a0 > bestGap) { bestGap = a1 - a0; bestMid = 0.5 * (a0 + a1); }
				}
				// Ville d'ancrage du contenu importé : l'auberge historique est déjà posée.
				const bool wantsInn = c.procedural.empty() || c.procedural == "hameau";
				if (wantsInn && c.state != "ruine" && c.kind != "camp" && c.id != w.master.importAnchorCity)
				{
					const TemplateRef inn = InnFor(c);
					int placed = -1;
					for (int attempt = 0; attempt < 10 && placed < 0; ++attempt)
					{
						const double a = bestMid + (attempt % 2 ? 1 : -1) * 0.12 * (attempt / 2);
						const Vec2 dir = Dir(a);
						placed = TryPlaceBuilding(w, c, inn, c.center + dir * (Rp + 2.2 + inn.halfD), dir * -1.0, "Auberge");
					}
					if (placed >= 0)
					{
						w.buildings[static_cast<size_t>(placed)].isInn = true;
						L.innDoor = w.buildings[static_cast<size_t>(placed)].doorPoint;
						L.buildingIndices.push_back(placed);
					}
					else L.innDoor = c.center;
				}
				else L.innDoor = c.center;
			}

			// --- Lots le long des rues.
			const std::vector<TemplateRef> pal = Palette(c);
			int lotCount = 0;
			std::vector<int> sites; // bâtiments posés depuis un modèle « chantier »
			for (const auto& st : streets)
			{
				const std::vector<Vec2> pts = ResamplePolyline(st, 1.0);
				double along = rng.Range(0.0, 4.0);
				while (along < static_cast<double>(pts.size()) - 2.0)
				{
					const size_t k = static_cast<size_t>(along);
					const Vec2 p = pts[k], q = pts[std::min(pts.size() - 1, k + 1)];
					Vec2 tdir{ q.x - p.x, q.z - p.z };
					const double tl = std::hypot(tdir.x, tdir.z);
					if (tl < 1e-6) { along += 1.0; continue; }
					tdir = tdir * (1.0 / tl);
					const TemplateRef& t = PickWeighted(pal, rng.Next());
					bool any = false;
					for (int side = -1; side <= 1; side += 2)
					{
						// jardins / vides (v1.0.140 : capitale en gradins plus serrée)
						if (rng.Chance(c.kind == "hameau" ? 0.35 : (c.acropole.enabled ? 0.04 : 0.12))) continue;
						const Vec2 nrm{ -tdir.z * side, tdir.x * side };
						const Vec2 center = p + nrm * (kStreetHalf + 1.2 + t.halfD);
						const int b = TryPlaceBuilding(w, c, t, center, nrm * -1.0, t.site ? "Chantier" : "Maison");
						if (b >= 0) { L.buildingIndices.push_back(b); any = true; ++lotCount; if (t.site) sites.push_back(b); }
					}
					along += any ? (2.0 * t.halfW + 1.6 + rng.Range(0.0, 1.8)) : 2.0;
				}
			}
			// --- Remplissage des îlots : maisons supplémentaires dans l'espace resté
			//     libre, porte tournée vers le noeud de rue le plus proche.
			{
				int attempts = 30;
				if (c.kind == "capitale") attempts = c.acropole.enabled ? 2000 : 260; // v1.0.140 : capitale en gradins dense
				else if (c.kind == "grande_ville") attempts = 160;
				else if (c.kind == "ville" || c.kind == "forteresse") attempts = 90;
				else if (c.kind == "village") attempts = 50;
				for (int a = 0; a < attempts; ++a)
				{
					const double ang = rng.Range(0.0, 2.0 * kPi);
					const double rad = std::sqrt(rng.Range(0.08, 1.0)) * (R - 3.0);
					const Vec2 center = c.center + Dir(ang) * rad;
					const int nn = Nearest(L, center);
					if (nn < 0) break;
					Vec2 face{ L.nodes[static_cast<size_t>(nn)].p.x - center.x, L.nodes[static_cast<size_t>(nn)].p.z - center.z };
					const double fl = std::hypot(face.x, face.z);
					if (fl < 4.0 || fl > 22.0) continue; // trop collé à la rue ou sans accès
					face = face * (1.0 / fl);
					const TemplateRef& t = PickWeighted(pal, rng.Next());
					const int b = TryPlaceBuilding(w, c, t, center, face, t.site ? "Chantier" : "Maison");
					if (b >= 0) { L.buildingIndices.push_back(b); ++lotCount; if (t.site) sites.push_back(b); }
				}
			}

			// Forge en périphérie (villages et plus, hors ruines) et à la mine.
			if (c.state != "ruine" && ((c.kind != "hameau" && c.procedural.empty()) || c.procedural == "mine"))
			{
				const TemplateRef forge = House("forge_6x6", 1);
				for (int attempt = 0; attempt < 24; ++attempt)
				{
					const double a = rng.Range(0.0, 2.0 * kPi);
					const Vec2 dir = Dir(a);
					const Vec2 center = c.center + dir * (R * rng.Range(0.55, 0.85));
					const int b = TryPlaceBuilding(w, c, forge, center, dir * -1.0, "Forge");
					if (b >= 0)
					{
						L.buildingIndices.push_back(b);
						const BuildingPlacementDef& bp = w.buildings[static_cast<size_t>(b)];
						const Vec2 side{ -dir.z, dir.x };
						AddProp(w, "Anvil", bp.doorPoint + side * 2.2, YawFacing(dir), 1.0, kSolid, 1, 0.6);
						// Forges naines (atelier décors T3, pl.17) : creuset suspendu à sa potence.
						if (c.race == "nains")
						{
							const Vec2 cp = bp.doorPoint + side * 5.0 - dir * 0.5;
							if (!w.IsOccupied(cp, 1.8)) { AddProp(w, "Crucible_Hanging_1", cp, YawFacing(side), 1.0, kSolid, 1, 1.2); w.Occupy(cp, 1.8); }
							// Grand engrenage sur chevalet (atelier décors T9, pl.8-9, 15).
							const Vec2 gp = bp.doorPoint - side * 5.2 - dir * 0.5;
							if (!w.IsOccupied(gp, 1.6)) { AddProp(w, "Gear_Large_1", gp, YawFacing(side), 1.0, kSolid, 1, 1.3); w.Occupy(gp, 1.6); }
						}
						AddProp(w, "Workbench", bp.doorPoint - side * 2.4, YawFacing(side), 1.0, kSolid, 1, 0.9);
						AddProp(w, "Whetstone", bp.doorPoint + side * 3.6 - dir * 0.8, rng.Range(0, 360), 1.0, 0, 2);
						break;
					}
				}
			}

			// --- Marché : étals autour de la place (sauf ville d'ancrage : sa place
			//     accueille déjà les PNJ et objets historiques importés).
			if (c.state != "ruine" && c.id != w.master.importAnchorCity && c.procedural.empty())
			{
				const int stalls = (c.kind == "capitale") ? 8 : (c.kind == "grande_ville" ? 6 : (c.kind == "hameau" ? 1 : 3));
				for (int k = 0; k < stalls; ++k)
				{
					const double a = 2.0 * kPi * (k + 0.5) / stalls + 0.31;
					const Vec2 dir = Dir(a);
					const Vec2 p = c.center + dir * (Rp - 1.8);
					if (inStair(p)) continue; // v1.0.140 : pied de l'escalier du palais dégagé
					const bool cart = (k % 3) == 2;
					// Atelier décors T2 (pl.4) : un étal sur trois est un auvent rayé dans les villes.
					const bool townMarket = c.kind != "hameau" && c.kind != "village";
					const bool awning = (k % 3) == 1 && townMarket;
					// Atelier décors T4 (pl.24-25) : tentes de marché frangées sur mâts rayés.
					const bool tent = (k % 3) == 0 && townMarket && k > 0;
					static const char* kTents[] = { "Stall_Tent_Market_1", "Stall_Tent_Market_2", "Stall_Tent_Market_3" };
					AddProp(w, cart ? "Stall_Cart_Empty" : (awning ? "Awning_Striped_1" : (tent ? kTents[k % 3 == 0 ? (k / 3) % 3 : 0] : "Stall_Empty")), p,
						YawFacing(dir * -1.0) + 180.0, 1.0, kSolid, 0, 1.0);
					L.stallFronts.push_back(c.center + dir * (Rp - 3.4));
					// Marchandises au pied de l'étal.
					const char* goods[] = { "Barrel_Apples", "FarmCrate_Apple", "FarmCrate_Carrot", "Crate_Wooden", "Bag", "Barrel", "Basket_1",
						"FoodPlatter_1", "Cage_Poultry_1", "Cloth_Rolls_1" };
					const Vec2 side{ -dir.z, dir.x };
					AddProp(w, goods[rng.Below(10)], p + side * 1.4, rng.Range(0, 360), 1.0, kSolid, 1, 0.4);
				}
				// Centre de la place : fontaine à vasque dans les villes (atelier décors T2,
				// pl.7-8), chaudron dans les hameaux et villages.
				const bool town = c.kind == "ville" || c.kind == "grande_ville" || c.kind == "capitale" || c.kind == "forteresse";
				if (town) AddProp(w, "Fountain_Basin_1", c.center, rng.Range(0, 45), 1.0, kSolid, 0, 1.35);
				else AddProp(w, "Cauldron", c.center, 0.0, 1.6, kSolid, 0, 0.9);
				// Jardins de capitale (atelier décors T4, pl.3-4) : statues drapées sur la place,
				// buis taillés autour de la fontaine.
				if (c.kind == "capitale" || c.kind == "grande_ville")
				{
					for (int k = 0; k < 2; ++k)
					{
						const Vec2 sp = c.center + Dir(k * kPi + 0.2) * (Rp * 0.85);
						if (!inStair(sp) && !w.IsOccupied(sp, 0.6)) AddProp(w, k ? "Statue_Garden_2" : "Statue_Garden_1", sp, YawFacing(c.center - sp), 1.0, kSolid, 0, 0.5);
					}
					if (c.kind == "capitale")
						for (int k = 0; k < 4; ++k)
						{
							const Vec2 hp = c.center + Dir(k * kPi / 2.0) * 2.6;
							if (!w.IsOccupied(hp, 0.5)) AddProp(w, "Hedge_Trimmed_1", hp, 0.0, 1.0, kSolid, 1, 0.7);
						}
				}
				// Guirlandes de fanions sur mâts (atelier décors T6, pl.37-41) : deux en travers
				// de la place des villes, mâts posés hors des étals et des statues.
				if (town)
					for (int k = 0; k < 2; ++k)
					{
						const Vec2 radial = Dir(k * kPi + 0.9);
						const Vec2 gp = c.center + radial * (Rp * 0.5);
						const Vec2 along{ -radial.z, radial.x };
						if (!w.IsOccupied(gp + along * 4.0, 0.4) && !w.IsOccupied(gp - along * 4.0, 0.4))
							AddProp(w, "Bunting_Garland_1", gp, YawAlong(along), 1.0, 0, 0);
					}
				// Braseros-vasques sur fût (atelier décors T3, pl.28) aux grandes places.
				if (c.kind == "capitale" || c.kind == "grande_ville")
					for (int k = 0; k < 4; ++k)
					{
						const Vec2 bp = c.center + Dir(k * kPi / 2.0 + kPi / 4.0) * (Rp * 0.7);
						if (!w.IsOccupied(bp, 0.6)) AddProp(w, "Brazier_Column_1", bp, 0.0, 1.0, kSolid, 1, 0.35);
					}
				for (int k = 0; k < 3; ++k)
				{
					const Vec2 dir = Dir(k * 2.0 * kPi / 3.0 + 1.0);
					AddProp(w, "Bench", c.center + dir * (Rp * 0.5), YawFacing(dir) + 90.0, 1.0, kSolid, 1, 0.5);
				}
			}

			// --- Mobilier devant les maisons (tonneaux, caisses, seaux).
			for (int bi : L.buildingIndices)
			{
				const BuildingPlacementDef& b = w.buildings[static_cast<size_t>(bi)];
				if (rng.Chance(0.55))
				{
					const double yawRad = b.yawDeg * kPi / 180.0;
					const Vec2 front{ -std::sin(yawRad), -std::cos(yawRad) };
					const Vec2 side{ -front.z, front.x };
					const char* items[] = { "Barrel", "Crate_Wooden", "Bucket_Wooden_1", "Barrel_Holder", "FarmCrate_Empty", "Bag" };
					const Vec2 p = b.doorPoint - front * 1.0 + side * (rng.Chance(0.5) ? 2.4 : -2.4);
					if (!w.IsOccupied(p, 0.5)) AddProp(w, items[rng.Below(6)], p, rng.Range(0, 360), 1.0, kSolid, 1, 0.4);
				}
			}

			// --- Chantiers de reconstruction : étais le long des murs latéraux,
			//     tas de briques, outils et matériaux devant la façade.
			for (int bi : sites)
			{
				const BuildingPlacementDef& b = w.buildings[static_cast<size_t>(bi)];
				const double yawRad = b.yawDeg * kPi / 180.0;
				const Vec2 front{ -std::sin(yawRad), -std::cos(yawRad) };
				const Vec2 side{ -front.z, front.x };
				for (int s = -1; s <= 1; s += 2)
				{
					const Vec2 p = b.position + side * (s * (b.halfW + 0.7));
					if (!w.IsOccupied(p, 0.4)) AddProp(w, "Prop_Support", p, b.yawDeg, 1.0, kSolid, 1, 0.4);
				}
				const char* bricks[] = { "Prop_Brick1", "Prop_Brick2", "Prop_Brick3", "Prop_Brick4" };
				const char* kit[] = { "Pickaxe_Bronze", "Axe_Bronze", "Rope_1", "Crate_Wooden", "Bucket_Wooden_1", "Anvil_Log", "Ladder_Wood_1" };
				// Bâti d'échafaudage (atelier décors T3, pl.17) contre un mur latéral.
				{
					const Vec2 sp = b.position + side * (b.halfW + 1.6) + front * (b.halfD * 0.3);
					if (!w.IsOccupied(sp, 1.0) && rng.Chance(0.6)) { AddProp(w, "Frame_Scaffold_1", sp, b.yawDeg + 90.0, 1.0, kSolid, 1, 1.0); w.Occupy(sp, 1.0); }
				}
				// Grue de chantier en bois (atelier décors T9, pl.9, 15) de l'autre côté, bras vers le mur.
				{
					const Vec2 cp = b.position - side * (b.halfW + 2.2);
					if (!w.IsOccupied(cp, 1.4) && rng.Chance(0.5)) { AddProp(w, "Crane_Wood_1", cp, YawAlong(side), 1.0, kSolid, 0, 1.3); w.Occupy(cp, 1.4); }
				}
				const Vec2 pile = b.doorPoint + side * (rng.Chance(0.5) ? 2.6 : -2.6);
				if (!w.IsOccupied(pile, 0.6)) AddProp(w, bricks[rng.Below(4)], pile, rng.Range(0, 360), 1.0, kSolid, 1, 0.5);
				const Vec2 tool = b.doorPoint - front * 0.8 + side * (rng.Range(-1.2, 1.2));
				if (!w.IsOccupied(tool, 0.4)) AddProp(w, kit[rng.Below(7)], tool, rng.Range(0, 360), 1.0, 0, 2);
			}

			// --- Remparts / palissade, percés aux portes. Une ville en
			//     reconstruction garde des BRÈCHES (gravats, pas de mur).
			const bool walls = HasWalls(c), palisade = HasPalisade(c);
			if (walls || palisade)
			{
				const double rw = R + 3.0;
				const double segLen = 2.0;
				const int segs = static_cast<int>(std::floor(2.0 * kPi * rw / segLen));
				// Brèches : (angle central, demi-ouverture angulaire), loin des portes.
				std::vector<std::pair<double, double>> breaches;
				if (c.state == "reconstruction")
				{
					for (int attempt = 0; attempt < 24 && breaches.size() < 3; ++attempt)
					{
						const double a = rng.Range(0.0, 2.0 * kPi);
						const double half = rng.Range(4.0, 7.0) / rw;
						bool clear = true;
						for (double ga : angles)
							if (std::fabs(std::remainder(a - ga, 2.0 * kPi)) * rw < 12.0 + half * rw) clear = false;
						for (const auto& br : breaches)
							if (std::fabs(std::remainder(a - br.first, 2.0 * kPi)) < br.second + half + 8.0 / rw) clear = false;
						if (clear) breaches.emplace_back(a, half);
					}
				}
				for (int k = 0; k < segs; ++k)
				{
					const double a = 2.0 * kPi * (k + 0.5) / segs;
					const Vec2 p = c.center + Dir(a) * rw;
					bool gate = false;
					for (double ga : angles)
						if (std::fabs(std::remainder(a - ga, 2.0 * kPi)) * rw < 4.2) gate = true;
					if (gate) continue;
					if (w.IsWater(p) || !w.InPlayableBounds(p, 40.0)) continue;
					bool breach = false;
					for (const auto& br : breaches)
						if (std::fabs(std::remainder(a - br.first, 2.0 * kPi)) < br.second) breach = true;
					if (breach)
					{
						// Gravats éparpillés de part et d'autre de la ligne du mur (franchissables).
						const char* rubble[] = { "Rock_Medium_1", "Rock_Medium_2", "Rock_Medium_3", "Prop_Brick1", "Prop_Brick3" };
						const Vec2 jitter = Dir(a) * rng.Range(-1.6, 1.6);
						AddProp(w, rubble[rng.Below(5)], p + jitter, rng.Range(0, 360), rng.Range(0.5, 0.9), 0, 1);
						continue;
					}
					// Tangente : le mur s'étend le long du cercle (axe X local).
					const Vec2 tan{ -std::sin(a), std::cos(a) };
					const double yaw = std::atan2(tan.z, tan.x) * -180.0 / kPi; // X local -> tangente
					if (walls)
					{
						// v1.0.133 — rempart continu sur terrain en pente (Heusgly, Ghrunke Dash, Garundel) :
						// le segment est posé au point le plus BAS du sol sous lui (plus de jour sous le mur)
						// et monté en étages jusqu'à dépasser de 3 m le point le plus HAUT (on ne l'enjambe
						// plus depuis l'amont). Au-delà de 6 m de dénivelé sur 2 m, pas de mur (falaise).
						double lo = 1e18, hi = -1e18;
						for (int q = 0; q < 9; ++q)
						{
							const Vec2 sp = p + tan * ((q % 3 - 1) * 1.05) + Dir(a) * ((q / 3 - 1) * 0.3);
							const double h = w.HeightAt(sp);
							lo = std::min(lo, h);
							hi = std::max(hi, h);
						}
						if (hi - lo > 6.0) continue;
						const double here = w.HeightAt(p);
						const double kWallHm = 3.12;
						const int levels = std::clamp(static_cast<int>(std::ceil((hi - lo + 3.0) / kWallHm - 1e-6)), 1, 3);
						for (int lv = 0; lv < levels; ++lv)
							AddProp(w, "Wall_UnevenBrick_Straight", p, yaw, 1.0, static_cast<uint8_t>(kSolid | kPiece | kOccluder), 0, 0.0,
								(lo - here) + lv * kWallHm);
					}
					else
						AddProp(w, "Prop_WoodenFence_Single", p, yaw, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
					w.Occupy(p, 1.2);
				}
				// Portes : arches + torches + bannières.
				for (double ga : angles)
				{
					const Vec2 dir = Dir(ga);
					const Vec2 tan{ -dir.z, dir.x };
					const Vec2 g = c.center + dir * rw;
					if (w.IsWater(g)) continue;
					const double yaw = std::atan2(tan.z, tan.x) * -180.0 / kPi;
					if (walls)
					{
						AddProp(w, "Wall_Arch", g + tan * 3.2, yaw, 1.0, static_cast<uint8_t>(kSolid | kPiece), 0);
						AddProp(w, "Wall_Arch", g - tan * 3.2, yaw, 1.0, static_cast<uint8_t>(kSolid | kPiece), 0);
						// Vantaux de chêne ferrés, ouverts vers l'intérieur (atelier décors T4, pl.20).
						AddProp(w, "Gate_City_Iron_1", g, std::atan2(-dir.x, -dir.z) * 180.0 / kPi, 1.0, 0, 0);
						AddProp(w, "Banner_1", g + tan * 3.2 + dir * 0.35, YawFacing(dir * -1.0), 1.0, 0, 0, 0.0, 2.6);
						AddProp(w, "Banner_2", g - tan * 3.2 + dir * 0.35, YawFacing(dir * -1.0), 1.0, 0, 0, 0.0, 2.6);
						// Torches fixées sur la face extérieure des arches.
						AddProp(w, "Torch_Metal", g + tan * 3.2 + dir * 0.3, YawFacing(dir * -1.0), 1.0, 0, 1, 0.0, 2.0);
						AddProp(w, "Torch_Metal", g - tan * 3.2 + dir * 0.3, YawFacing(dir * -1.0), 1.0, 0, 1, 0.0, 2.0);
					}
				}
			}

			// --- v1.0.140 (retour du propriétaire : « remplace les champs par des maisons, décale
			//     les champs plus loin ») — FAUBOURGS d'une capitale en gradins : quatre rues en anneau
			//     hors les murs (de R + 20 à R + 95 m), sauf du côté du revers (mer) et dans l'eau,
			//     bordées de maisons des deux côtés ; les champs sont repoussés au-delà (kFaubourgM).
			const double kFaubourgM = c.acropole.enabled ? 110.0 : 0.0;
			if (c.acropole.enabled && c.state != "ruine")
			{
				for (double off : { 20.0, 45.0, 70.0, 95.0 })
				{
					const double rr = R + off;
					const int n = static_cast<int>(std::ceil(2.0 * kPi * rr / 3.0));
					std::vector<Vec2> run;
					int prevId = -1;
					auto flush = [&]() {
						if (run.size() >= 4)
						{
							const std::vector<Vec2> pts = ResamplePolyline(run, 1.0);
							for (const Vec2& q : run) w.Occupy(q, kStreetHalf + 0.4);
							double along = rng.Range(0.0, 3.0);
							while (along < static_cast<double>(pts.size()) - 2.0)
							{
								const size_t k = static_cast<size_t>(along);
								Vec2 tdir = pts[std::min(pts.size() - 1, k + 1)] - pts[k];
								const double tl = std::hypot(tdir.x, tdir.z);
								if (tl < 1e-6) { along += 1.0; continue; }
								tdir = tdir * (1.0 / tl);
								const TemplateRef& t = PickWeighted(pal, rng.Next());
								bool any = false;
								for (int side = -1; side <= 1; side += 2)
								{
									if (rng.Chance(0.06)) continue;
									const Vec2 nrm{ -tdir.z * side, tdir.x * side };
									const Vec2 center = pts[k] + nrm * (kStreetHalf + 1.2 + t.halfD);
									const int b = TryPlaceBuilding(w, c, t, center, nrm * -1.0, "Maison", false);
									if (b >= 0) { L.buildingIndices.push_back(b); any = true; ++lotCount; }
								}
								along += any ? (2.0 * t.halfW + 1.4 + rng.Range(0.0, 1.4)) : 2.0;
							}
						}
						run.clear();
						prevId = -1;
					};
					for (int q = 0; q <= n; ++q)
					{
						const Vec2 dq = Dir(q * 2.0 * kPi / n);
						const Vec2 pt = c.center + dq * rr;
						// Revers de la butte (côté opposé à l'escalier, la mer) : pas de faubourg.
						const bool back = (dq.x * c.acropole.stairDir.x + dq.z * c.acropole.stairDir.z) < -0.35;
						if (back || w.IsWater(pt) || w.IsWater(pt + dq * 8.0)) { flush(); continue; }
						run.push_back(pt);
						if (q % 3 == 0)
						{
							const int id = AddNode(L, pt);
							if (prevId >= 0) Link(L, prevId, id);
							prevId = id;
						}
					}
					flush();
				}
			}

			// --- Champs clôturés hors les murs : toutes les agglomérations habitées
			//     (lore v1.0.52 : villes, grandes villes, capitales et forteresses
			//     comprises, avec des parcelles plus grandes) ; pas de camps, ruines
			//     ni villes démoniaques.
			const FieldPlan fp = FieldsFor(c);
			// Aucune culture aux Enfers (palette v1.0.83 : pas de végétation vivante).
			if (fp.count > 0 && !IsInfernalMap(w.master))
			{
				int made = 0;
				for (int attempt = 0; attempt < fp.attempts && made < fp.count; ++attempt)
				{
					const double a = rng.Range(0.0, 2.0 * kPi);
					const Vec2 dir = Dir(a);
					const double jitter = rng.Range(0.0, 20.0);
					const Vec2 ax = Dir(a + kPi / 2.0), az = dir;
					// Le plus grand champ possible : taille pleine en plaine, sinon 70, 50, 35
					// puis 20 % (≈ ancienne taille) quand routes, rivières ou relief découpent
					// les abords (Kendrhir, sur sa colline, n'a que des petits champs).
					double hw = 0.0, hd = 0.0;
					Vec2 fc{};
					bool placed = false;
					for (double k : { 1.0, 0.7, 0.5, 0.35, 0.2 })
					{
						hw = fp.halfW * k; hd = fp.halfD * k;
						// Au-delà du rempart ou de la palissade (posés à R + 3 m).
						fc = c.center + dir * (R + kFaubourgM + hd + 9.0 + jitter); // v1.0.140 : au-delà des faubourgs
						if (!w.IsOccupied(fc, std::min(hw, hd)) && FieldGroundOk(w, fc, ax, az, hw, hd)) { placed = true; break; }
					}
					if (!placed) continue;
					const double fr = std::hypot(hw, hd) + 1.0;
					w.Occupy(fc, fr);
					++made;
					const double yawAx = std::atan2(ax.z, ax.x) * -180.0 / kPi;
					const double yawAz = std::atan2(az.z, az.x) * -180.0 / kPi;
					// Clôture (segments de ~2 m) sur les quatre côtés, avec une entrée.
					for (double u = -hw + 1.0; u < hw; u += 2.06)
					{
						AddProp(w, "Prop_WoodenFence_Single", fc + ax * u + az * hd, yawAx, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
						if (std::fabs(u) > 1.5)
							AddProp(w, "Prop_WoodenFence_Single", fc + ax * u - az * hd, yawAx, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
					}
					for (double v = -hd + 1.0; v < hd; v += 2.06)
					{
						AddProp(w, "Prop_WoodenFence_Single", fc + az * v + ax * hw, yawAz, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
						AddProp(w, "Prop_WoodenFence_Single", fc + az * v - ax * hw, yawAz, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
					}
					// Cultures : pavage de parcelles de 4 m alignées sur la clôture (rangs
					// parallèles à ax), visibles de loin (classe de culling « grand »).
					const char* crop = PickCrop(rng.Range(0.0, 1.0));
					for (double u = -hw + 3.0; u <= hw - 3.0; u += 4.0)
						for (double v = -hd + 3.0; v <= hd - 3.0; v += 4.0)
							AddProp(w, crop, fc + ax * u + az * v, yawAx, rng.Range(0.95, 1.05), 0, 0);
					AddProp(w, "FarmCrate_Carrot", fc - az * (hd + 1.2) + ax * 2.0, rng.Range(0, 360), 1.0, kSolid, 1, 0.4);
					// Meules de foin coniques (atelier décors T3, pl.26) au bord des champs de
					// céréales (pas des vignes ni du maïs), hameaux, villages et fermes.
					const std::string cs(crop);
					if ((c.kind == "hameau" || c.kind == "village") && cs.find("Vine") == std::string::npos
						&& cs.find("Corn") == std::string::npos)
					{
						const int stacks = 1 + static_cast<int>(rng.Below(3));
						for (int k = 0; k < stacks; ++k)
						{
							const Vec2 hp = fc - az * (hd + 3.5 + 3.4 * (k % 2)) - ax * (hw * 0.3 + 3.6 * k);
							if (w.IsWater(hp) || w.IsOccupied(hp, 1.8)) continue;
							AddProp(w, "Haystack_1", hp, rng.Range(0, 360), rng.Range(0.85, 1.1), kSolid, 0, 1.4);
							w.Occupy(hp, 1.8);
						}
					}
				}
			}

			// --- Cimetière / point de résurrection : hors les murs, près d'une route
			//     (villes du lore et hameaux ; pas pour les fermes, camps et ruines ;
			//     v1.0.111 : ni pour un hameau de `graveyards.without_graveyard`).
			//     v1.0.121 : dans une cellule `graveyards.village_only_cells`, seules les
			//     villes et villages du lore en ont un (pas les hameaux procéduraux).
			if ((c.procedural.empty() || (c.procedural == "hameau" && !VillageOnlyCell(w))) && !w.master.graveyardExcluded.count(c.id))
			{
				L.hasGraveyard = true;
				const double a = angles.empty() ? 0.0 : angles[0] + 0.5;
				Vec2 gy = c.center + Dir(a) * (R + 12.0);
				for (int attempt = 0; attempt < 16; ++attempt)
				{
					if (!w.IsWater(gy) && !w.IsOccupied(gy, 4.0) && w.InPlayableBounds(gy, 60.0)) break;
					gy = c.center + Dir(a + attempt * 0.4) * (R + 12.0 + attempt);
				}
				L.graveyard = gy;
				AddProp(w, "CandleStick_Stand", gy, 0.0, 1.0, 0, 1);
				// Urnes de pierre sur socle (atelier décors T4, pl.40) aux cimetières des grandes villes.
				if (c.kind == "capitale" || c.kind == "grande_ville")
				{
					const Vec2 tv{ c.center.x - gy.x, c.center.z - gy.z };
					const double tl = std::max(1e-6, std::hypot(tv.x, tv.z));
					const Vec2 sd{ -tv.z / tl, tv.x / tl };
					for (const double sg : { -1.0, 1.0 })
						AddProp(w, "Urn_Stone_1", gy + sd * (1.6 * sg), 0.0, 1.0, kSolid, 1, 0.35);
				}
				// Petit mausolée de pierre (atelier décors T7, pl.46-47) au fond des cimetières des
				// grandes villes, porte tournée vers la ville.
				if (c.kind == "capitale" || c.kind == "grande_ville")
				{
					const Vec2 tv{ c.center.x - gy.x, c.center.z - gy.z };
					const double tl = std::max(1e-6, std::hypot(tv.x, tv.z));
					const Vec2 mp = gy - Vec2{ tv.x / tl, tv.z / tl } * 7.0;
					if (!w.IsWater(mp) && !w.IsOccupied(mp, 2.5))
					{
						AddProp(w, "Mausoleum_Small_1", mp, YawFacing(mp - c.center), 1.0, kSolid, 0, 2.4);   // porte en +Z glTF
						w.Occupy(mp, 2.5);
					}
				}
				PlaceOldGraves(w, gy, c.center - gy, GraveCountFor(c), rng);
				w.Occupy(gy, 2.5);
			}
			else L.graveyard = c.center;

			// --- Jardin de capitale (atelier décors T4, pl.3-4) : kiosque à coupole entouré de
			//     massifs d'iris et de lis, cyprès en bac de part et d'autre de l'entrée.
			if (c.kind == "capitale" && c.state != "ruine" && c.procedural.empty())
				for (int attempt = 0; attempt < 16; ++attempt)
				{
					const Vec2 gp = c.center + Dir(rng.Range(0.0, 2.0 * kPi)) * (R * rng.Range(0.55, 0.85));
					if (w.IsWater(gp) || w.IsOccupied(gp, 7.5) || !GroundOk(w, gp, 7.0)) continue;
					const Vec2 to = c.center - gp;
					AddProp(w, "Gazebo_Rotunda_1", gp, YawFacing(to), 1.0, 0, 0);   // traversable (colonnes fines)
					for (int k = 0; k < 8; ++k)
						AddProp(w, k % 2 ? "Flower_Garden_2" : "Flower_Garden_1", gp + Dir(k * kPi / 4.0 + 0.4) * 5.2, rng.Range(0, 360), 1.0, 0, 2);
					const double tl = std::max(1e-6, std::hypot(to.x, to.z));
					const Vec2 td{ to.x / tl, to.z / tl }, sd{ -td.z, td.x };
					for (const double sg : { -1.0, 1.0 })
						AddProp(w, "Tree_Cypress_Potted_1", gp + td * 4.2 + sd * (1.6 * sg), 0.0, 1.0, kSolid, 1, 0.4);
					w.Occupy(gp, 7.5);
					break;
				}

			// --- Ruines (atelier décors T4, pl.40-41 ; T5, pl.2-3) : pans de brique effondrés,
			//     arc brisé, frise tombée, cheminée de pierre sèche d'une hutte brûlée.
			if (c.state == "ruine")
				for (int k = 0; k < 4; ++k)
				{
					static const char* kRuins[] = { "Ruin_Wall_1", "Ruin_Wall_2", c.race == "orcs" ? "Chimney_DryStone_1" : "Ruin_Wall_3" };
					for (int attempt = 0; attempt < 10; ++attempt)
					{
						const Vec2 p = c.center + Dir(rng.Range(0.0, 2.0 * kPi)) * (R * rng.Range(0.2, 0.85));
						if (w.IsWater(p) || w.IsOccupied(p, 3.0)) continue;
						const int v = (k + attempt) % 3;
						AddProp(w, kRuins[v], p, rng.Range(0, 360), rng.Range(0.85, 1.15), kSolid, 0, v == 2 ? 1.2 : 2.4);
						w.Occupy(p, 3.0);
						break;
					}
				}

			// --- Orques et orkhs (atelier décors T5, pl.1-3, 31) : huttes coniques de perches et
			//     de chaume en couronne, tambour de guerre sur la place, poupée de chiffon.
			if (c.race == "orcs" && c.state != "ruine")
			{
				const int huts = c.kind == "hameau" ? 4 : (c.kind == "village" ? 7 : 10);
				for (int k = 0; k < huts; ++k)
					for (int attempt = 0; attempt < 8; ++attempt)
					{
						const Vec2 p = c.center + Dir(rng.Range(0.0, 2.0 * kPi)) * (R * rng.Range(0.35, 0.95));
						if (w.IsWater(p) || w.IsOccupied(p, 2.4) || !GroundOk(w, p, 2.2)) continue;
						AddProp(w, "Hut_Cone_Thatch_1", p, YawFacing(c.center - p), 1.0, kSolid, 0, 2.0);
						w.Occupy(p, 2.4);
						if (rng.Chance(0.15)) AddProp(w, "Doll_Rag_1", p + Dir(rng.Range(0.0, 2.0 * kPi)) * 2.6, rng.Range(0, 360), 1.0, 0, 2);
						break;
					}
				const Vec2 dp = c.center + Dir(rng.Range(0.0, 2.0 * kPi)) * 4.0;
				if (!w.IsOccupied(dp, 0.9)) AddProp(w, "WarDrum_1", dp, 0.0, 1.0, kSolid, 1, 0.8);
			}

			// --- Tour carrée de village (atelier décors T3, pl.26-29) : un village du lore sur
			//     deux, en bordure ; chariot blindé dans les villes naines (pl.17-18, 25, 40).
			if (c.state != "ruine" && c.procedural.empty())
			{
				const bool tower = c.kind == "village" && (HashString(c.id.c_str()) % 2u) == 0u;
				const bool wagon = c.race == "nains" && c.kind != "camp";
				for (int item = 0; item < 2; ++item)
				{
					if ((item == 0 && !tower) || (item == 1 && !wagon)) continue;
					const double rad = item == 0 ? 3.6 : 3.8;
					for (int attempt = 0; attempt < 16; ++attempt)
					{
						const Vec2 p = c.center + Dir(rng.Range(0.0, 2.0 * kPi)) * (R * rng.Range(0.55, 0.9));
						if (w.IsWater(p) || w.IsOccupied(p, rad) || !GroundOk(w, p, rad)) continue;
						const double yaw = rng.Range(0, 360);
						AddProp(w, item == 0 ? "Tower_Square_Village_1" : "Wagon_Armored_1", p, yaw, 1.0, kSolid, 0, item == 0 ? 3.0 : 2.2);
						w.Occupy(p, rad);
						if (item == 1)   // marchepied de roulotte à l'arrière (T5, pl.5)
						{
							const double yr = yaw * kPi / 180.0;
							const Vec2 back{ -std::cos(yr), std::sin(yr) };
							AddProp(w, "Wagon_Steps_1", p + back * 3.4, yaw + 90.0, 1.0, 0, 2);
						}
						break;
					}
				}
			}

			// --- Poteaux indicateurs (atelier décors T2, pl.15) : à la sortie de la ville sur
			//     chaque route (les carrefours en rase campagne : PlaceSignposts, Roads.cpp).
			if (c.state != "ruine" && c.procedural.empty() && c.kind != "camp")
				for (size_t k = 0; k < roadSpokes; ++k)
				{
					const Vec2 out = Dir(angles[k]);
					const Vec2 side{ -out.z, out.x };
					const Vec2 p = c.center + out * (R + 10.0) + side * 4.5;
					if (w.IsWater(p) || w.IsOccupied(p, 0.8)) continue;
					AddProp(w, "Signpost_1", p, YawAlong(out), 1.0, kSolid, 1, 0.15);
					w.Occupy(p, 0.6);
				}

			// --- Gibet (atelier décors T2, pl.15, sans pendu) : hors les murs des capitales et
			//     des forteresses, à l'écart de la première route (8 m de l'axe).
			if ((c.kind == "capitale" || c.kind == "forteresse") && c.state != "ruine" && c.procedural.empty() && !angles.empty())
			{
				const Vec2 out = Dir(angles[0]);
				const Vec2 side{ -out.z, out.x };
				for (int attempt = 0; attempt < 6; ++attempt)
				{
					const Vec2 p = c.center + out * (R + 16.0 + attempt * 4.0) + side * ((attempt % 2) ? -9.0 : 9.0);
					if (w.IsWater(p) || w.IsOccupied(p, 3.0)) continue;
					AddProp(w, "Gallows_1", p, YawFacing(out), 1.0, kSolid, 0, 1.6);
					w.Occupy(p, 3.0);
					break;
				}
			}

			// --- Accessoires de métier des lieux procéduraux.
			if (c.procedural == "bucherons" || c.procedural == "mine" || c.procedural == "bandits")
			{
				const char* bucheron[] = { "Anvil_Log", "Axe_Bronze", "Prop_Wagon", "Barrel", "Crate_Wooden" };
				const char* mine[] = { "Pickaxe_Bronze", "Prop_Wagon", "Crate_Wooden", "Bucket_Metal", "Barrel" };
				// Bandits (BD T1 pl.11-15, horde) : huttes de branchages, braseros sur perche,
				// grande cage, chariot chargé, auvent de toile (atelier décors).
				const char* bandit[] = { "Prop_Wagon", "Chest_Wood", "Cage_Small", "Barrel", "Crate_Wooden",
					"Hut_Branch_1", "Brazier_Pole_1", "Cage_Large", "Prop_Wagon_Loaded", "Tent_Awning_1" };
				const bool isBandit = c.procedural == "bandits";
				const char** set = c.procedural == "mine" ? mine : (isBandit ? bandit : bucheron);
				const uint32_t setN = isBandit ? 10u : 5u;
				for (int k = 0; k < (isBandit ? 14 : 10); ++k)
				{
					const Vec2 p = c.center + Dir(rng.Range(0.0, 2.0 * kPi)) * rng.Range(R * 0.25, R * 0.8);
					if (w.IsOccupied(p, 1.4) || w.IsWater(p)) continue;
					const char* mesh = set[rng.Below(setN)];
					const std::string ms(mesh);
					const bool big = ms == "Prop_Wagon" || ms == "Prop_Wagon_Loaded" || ms == "Hut_Branch_1"
						|| ms == "Tent_Awning_1" || ms == "Cage_Large";
					AddProp(w, mesh, p, rng.Range(0, 360), 1.0, kSolid, 1, big ? 1.4 : 0.5);
					w.Occupy(p, big ? 2.0 : 0.8);
				}
				// Feu de camp (chaudron sur la place).
				AddProp(w, "Cauldron", c.center, 0.0, 1.2, kSolid, 0, 0.8);
				// Butin des bandits (atelier décors T5, pl.8).
				if (isBandit) AddProp(w, "Treasure_Pile_1", c.center + Dir(rng.Range(0.0, 2.0 * kPi)) * 3.2, rng.Range(0, 360), 1.0, kSolid, 1, 0.9);
			}
			// Basse-cour des hameaux et villages (BD T1 pl.8-9) : poulailler et abreuvoir.
			if ((c.kind == "hameau" || c.kind == "village") && c.procedural.empty())
				for (const char* mesh : { "Coop_1", "Trough_Stone_1" })
					for (int t = 0; t < 12; ++t)
					{
						const Vec2 p = c.center + Dir(rng.Range(0.0, 2.0 * kPi)) * rng.Range(R * 0.45, R * 0.85);
						if (w.IsOccupied(p, 1.6) || w.IsWater(p)) continue;
						AddProp(w, mesh, p, rng.Range(0, 360), 1.0, kSolid, 1, 1.1);
						w.Occupy(p, 1.6);
						break;
					}

			// --- Enseignes de service (atelier décors) : villes du lore habitées.
			if (c.procedural.empty() && c.state != "ruine")
				PlaceServiceSigns(w, c, firstBuilding);

			// --- Point de départ (villes de départ) : sur la place, face à l'auberge.
			{
				const Vec2 toInn{ L.innDoor.x - c.center.x, L.innDoor.z - c.center.z };
				const double l = std::hypot(toInn.x, toInn.z);
				const Vec2 dir = l > 1e-3 ? toInn * (1.0 / l) : Vec2{ 0.0, -1.0 };
				L.startSpot = c.center - dir * 3.5;
				L.startYawDeg = YawFacing(dir);
			}

			// --- Pavage : rues et place (villes moyennes+ et forteresses), dalles rondes
			//     pour les villages ; les hameaux, camps et ruines restent en terre.
			if ((c.state == "intact" || c.state == "reconstruction") && c.kind != "hameau" && c.kind != "camp" && c.procedural.empty())
			{
				const bool cobble = (c.kind != "village");
				const char* tile = cobble ? "RockPath_Square_Wide" : "RockPath_Round_Wide";
				for (const auto& st : streets) PaveAlong(w, st, 2, tile, R + 4.0, c.center);
				// Place : anneaux concentriques de dalles.
				for (double rr = 1.5; rr < Rp - 0.5; rr += 2.0)
				{
					std::vector<Vec2> circle;
					const int segs = std::max(6, static_cast<int>(2.0 * kPi * rr / 2.0));
					for (int k = 0; k <= segs; ++k) circle.push_back(c.center + Dir(k * 2.0 * kPi / segs) * rr);
					PaveAlong(w, circle, 1, tile, 0.0, c.center);
				}
				// Approches pavées des grandes routes (90 m hors les murs).
				if (cobble)
					for (const Road& r : w.roads)
						if (r.major && (r.fromId == c.id || r.toId == c.id))
						{
							std::vector<Vec2> seg;
							for (const Vec2& p : r.points)
							{
								const double d = std::hypot(p.x - c.center.x, p.z - c.center.z);
								if (d > R + 2.0 && d < R + 90.0) seg.push_back(p);
							}
							// Pavage irrégulier hors les murs (atelier décors T3, pl.41).
							if (seg.size() > 2) PaveAlong(w, seg, 3, "RockPath_Irregular_Wide", 0.0, c.center);
						}
			}

			if (lotCount == 0 && c.state != "ruine" && c.procedural.empty())
				w.Warn("aucun lot bati pour " + c.id + " (terrain contraint)");
			w.layouts.push_back(std::move(L));
		}
		BuildGouffreProps(w); // v1.0.107 : torches, éboulis et ambiance du gouffre
		BuildIsolatedGraveyard(w); // v1.0.111 : cellule sans cimetière
		w.report.buildings = w.buildings.size();
	}
}
