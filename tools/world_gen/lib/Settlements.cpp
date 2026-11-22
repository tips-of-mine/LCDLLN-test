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
		/// \param firstBuilding indice du premier bâtiment posé pour cette ville.
		void PlaceServiceSigns(World& w, const CityDef& c, size_t firstBuilding)
		{
			std::vector<std::string> todo;
			for (const std::string& s : c.services)
				if (HasFacadeSign(s) && std::find(todo.begin(), todo.end(), s) == todo.end()) todo.push_back(s);
			if (todo.empty()) return;
			std::vector<size_t> cand;
			for (size_t i = firstBuilding; i < w.buildings.size(); ++i)
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
				const double l = std::hypot(f.x, f.z);
				if (l < 1e-6) continue;
				f = f * (1.0 / l);
				const Vec2 side{ -f.z, f.x };
				const double off = std::min(std::max(b.halfW * 0.55, 1.2), std::max(0.6, b.halfW - 0.2));
				// Origine du mesh au milieu de la potence : poteau à 0,15 m de la façade.
				const Vec2 p = b.position + f * (b.halfD + 0.6) + side * off;
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
					st.push_back(c.center + Dir(a + bend * t) * d);
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
				for (int k = 0; k < 48; k += 3) ringIds.push_back(AddNode(L, st[static_cast<size_t>(k)]));
				for (size_t k = 0; k < ringIds.size(); ++k) Link(L, ringIds[k], ringIds[(k + 1) % ringIds.size()]);
				// Raccorde l'anneau aux rues rayonnantes les plus proches.
				for (int id : ringIds)
				{
					const int nb = Nearest(L, L.nodes[static_cast<size_t>(id)].p, id);
					if (nb >= 0 && std::find(ringIds.begin(), ringIds.end(), nb) == ringIds.end()
						&& std::hypot(L.nodes[static_cast<size_t>(nb)].p.x - L.nodes[static_cast<size_t>(id)].p.x,
							L.nodes[static_cast<size_t>(nb)].p.z - L.nodes[static_cast<size_t>(id)].p.z) < 9.0)
						Link(L, id, nb);
				}
				streets.push_back(st);
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
						if (rng.Chance(c.kind == "hameau" ? 0.35 : 0.12)) continue; // jardins / vides
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
				if (c.kind == "capitale") attempts = 260;
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
						if (!w.IsOccupied(sp, 0.6)) AddProp(w, k ? "Statue_Garden_2" : "Statue_Garden_1", sp, YawFacing(c.center - sp), 1.0, kSolid, 0, 0.5);
					}
					if (c.kind == "capitale")
						for (int k = 0; k < 4; ++k)
						{
							const Vec2 hp = c.center + Dir(k * kPi / 2.0) * 2.6;
							if (!w.IsOccupied(hp, 0.5)) AddProp(w, "Hedge_Trimmed_1", hp, 0.0, 1.0, kSolid, 1, 0.7);
						}
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
						fc = c.center + dir * (R + hd + 9.0 + jitter);
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
