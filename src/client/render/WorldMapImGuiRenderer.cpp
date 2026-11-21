// Monde jouable (chantier « Royaumes ») — carte du monde + bannière de lieu.
#include "src/client/render/WorldMapImGuiRenderer.h"
#include "src/client/world/places/WorldMapZoom.h"

#include "src/client/world/discovery/MapDiscoveryClient.h"
#include "src/client/world/places/WorldPlaces.h"
#include "src/shared/world/MapDiscovery.h"
#include "src/shared/world/MapFeatures.h"
#include "src/shared/world/WorldCells.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		/// Couleur de marqueur par peuple dominant de la ville.
		ImU32 RaceColor(const std::string& race, int alpha = 255)
		{
			if (race == "humains") return IM_COL32(232, 208, 140, alpha);
			if (race == "elfes") return IM_COL32(128, 212, 120, alpha);
			if (race == "orcs") return IM_COL32(214, 128, 76, alpha);
			if (race == "nains") return IM_COL32(170, 178, 214, alpha);
			if (race == "demons") return IM_COL32(222, 82, 92, alpha);
			return IM_COL32(196, 196, 196, alpha);
		}

		/// Rayon du marqueur (px, à zoom 1) selon l'importance de la ville.
		float KindRadius(const std::string& kind)
		{
			if (kind == "capitale") return 6.5f;
			if (kind == "grande_ville" || kind == "forteresse") return 5.0f;
			if (kind == "ville") return 4.2f;
			return 3.2f;
		}

		/// Libellé toujours visible (sinon seulement zoomé ou survolé).
		bool AlwaysLabelled(const engine::client::PlaceCity& c)
		{
			return c.kind == "capitale" || c.kind == "grande_ville" || !c.startFor.empty();
		}

		/// Texte avec ombre portée (lisibilité sur la carte).
		void ShadowText(ImDrawList* dl, ImVec2 p, ImU32 col, const char* text)
		{
			dl->AddText(ImVec2(p.x + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, 200), text);
			dl->AddText(p, col, text);
		}

		/// FIX.12 / FIX.15 — Dessine plans d'eau, routes, rues et bâtiments (map_features.bin) visibles
		/// dans la vue. \param worldToScreen projette un point GLOBAL (x, z) à
		/// l'écran ; \param wx0,wz0,wx1,wz1 emprise monde de la vue (m) ;
		/// \param pxPerM échelle courante (px par mètre). Rues masquées au-delà de
		/// 6 km visibles, bâtiments au-delà de 8 km (illisibles, coût inutile).
		/// Doit précéder le voile : ce qui n'est pas découvert reste caché.
		template <typename WorldToScreen>
		void DrawMapFeatures(ImDrawList* dl, const engine::world::mapfeatures::MapFeatures& f,
			float wx0, float wz0, float wx1, float wz1, float pxPerM, WorldToScreen&& worldToScreen)
		{
			namespace mf = engine::world::mapfeatures;
			const float visibleM = wx1 - wx0;
			const bool showStreets = visibleM <= 6000.0f;
			const bool showBuildings = visibleM <= 8000.0f;
			std::vector<ImVec2> pts;
			// FIX.15 — plans d'eau d'abord (routes et ponts par-dessus) : lacs en
			// éventail depuis leur centre (contours étoilés), rivières en
			// polylignes, d'au moins 1,5 px pour rester visibles en vue large.
			const ImU32 waterCol = IM_COL32(64, 112, 168, 255);
			for (const mf::MapWater& w : f.waters)
			{
				if (w.PointCount() < 2u)
					continue;
				if (w.maxX < wx0 || w.minX > wx1 || w.maxZ < wz0 || w.minZ > wz1)
					continue;
				pts.clear();
				for (size_t i = 0; i < w.PointCount(); ++i)
					pts.push_back(worldToScreen(w.xz[2 * i], w.xz[2 * i + 1]));
				if (w.kind == mf::MapWaterKind::Lake && pts.size() >= 3u)
				{
					float cx = 0.0f, cz = 0.0f;
					mf::LakeCenter(w, cx, cz);
					const ImVec2 c = worldToScreen(cx, cz);
					for (size_t i = 0; i < pts.size(); ++i)
						dl->AddTriangleFilled(c, pts[i], pts[(i + 1) % pts.size()], waterCol);
					// Contour : un petit lac reste visible même réduit à quelques pixels.
					dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), waterCol, ImDrawFlags_Closed, 1.5f);
				}
				else
				{
					const float thick = std::clamp(w.widthM * pxPerM, 1.5f, 16.0f);
					dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), waterCol, ImDrawFlags_None, thick);
				}
			}
			// Ordre : chemins et rues d'abord, grandes routes par-dessus.
			static constexpr mf::MapRoadKind kOrder[4] = { mf::MapRoadKind::Street, mf::MapRoadKind::Path, mf::MapRoadKind::Road, mf::MapRoadKind::Major };
			for (mf::MapRoadKind kind : kOrder)
			{
				if (kind == mf::MapRoadKind::Street && !showStreets)
					continue;
				ImU32 col = IM_COL32(176, 140, 92, 255);
				float minPx = 1.5f;
				switch (kind)
				{
				case mf::MapRoadKind::Major: col = IM_COL32(214, 176, 112, 255); minPx = 2.5f; break;
				case mf::MapRoadKind::Road: col = IM_COL32(190, 152, 100, 255); minPx = 2.0f; break;
				case mf::MapRoadKind::Path: col = IM_COL32(156, 128, 92, 255); minPx = 1.2f; break;
				case mf::MapRoadKind::Street: col = IM_COL32(208, 194, 164, 255); minPx = 1.2f; break;
				}
				for (const mf::MapRoad& r : f.roads)
				{
					if (r.kind != kind || r.PointCount() < 2u)
						continue;
					if (r.maxX < wx0 || r.minX > wx1 || r.maxZ < wz0 || r.minZ > wz1)
						continue;
					pts.clear();
					for (size_t i = 0; i < r.PointCount(); ++i)
						pts.push_back(worldToScreen(r.xz[2 * i], r.xz[2 * i + 1]));
					const float thick = std::clamp(r.widthM * pxPerM, minPx, 14.0f);
					dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, ImDrawFlags_None, thick);
				}
			}
			if (!showBuildings)
				return;
			const ImU32 fill = IM_COL32(120, 96, 78, 255);
			const ImU32 edge = IM_COL32(40, 30, 24, 255);
			float c[8];
			for (const mf::MapBuilding& b : f.buildings)
			{
				const float r = std::max(b.halfW, b.halfD) * 1.42f;
				if (b.x + r < wx0 || b.x - r > wx1 || b.z + r < wz0 || b.z - r > wz1)
					continue;
				mf::BuildingCorners(b, c);
				const ImVec2 a = worldToScreen(c[0], c[1]), bb = worldToScreen(c[2], c[3]);
				const ImVec2 cc = worldToScreen(c[4], c[5]), d = worldToScreen(c[6], c[7]);
				dl->AddQuadFilled(a, bb, cc, d, fill);
				// Contour seulement quand le bâtiment fait quelques pixels.
				if (r * pxPerM >= 4.0f)
					dl->AddQuad(a, bb, cc, d, edge, 1.0f);
			}
		}

		/// Voile des zones non découvertes. \param worldToScreen projette un point
		/// GLOBAL (x, z) à l'écran (projection affine : même échelle partout) ;
		/// \param u0,v0,u1,v1 emprise de la vue en coordonnées normalisées carte.
		///
		/// Lot D (bug 4) — trois régimes selon la taille d'une case à l'écran :
		///  - case ≥ 1,5 px : le contour lissé (dsc::IsPointRevealedSmooth) est
		///    calculé dans une grille locale qui inclut les cases des cellules
		///    voisines ; une cellule JAMAIS visitée qui touche une cellule visitée
		///    est traitée comme des cases cachées de cette même grille (le contour
		///    arrondi traverse donc la frontière de cellule au lieu d'être coupé
		///    net). Sous-cases de bord : 2 à 12 par côté dès 6 px, 1 à 2 en dessous
		///    (avant : carrés bruts sous 6 px) ;
		///  - case < 1,5 px : le contour est sous le pixel ; les cases sont
		///    regroupées en blocs d'au moins 1,5 px, voilés si moins de la moitié de
		///    leurs cases est découverte (sous-échantillonnage moyenné, coût ≤ une
		///    lecture par case comme avant) ;
		///  - cellules jamais visitées sans voisine visitée : voilées en entier
		///    (séries fusionnées par rangée).
		/// Coût : voisinage 5 × 5 par table de sommes (O(1) par case). Main thread
		/// (ImGui), entre NewFrame et Render ; n'écrit que dans \p dl.
		template <typename WorldToScreen>
		void DrawDiscoveryFog(ImDrawList* dl, const engine::client::MapDiscoveryClient& disc,
			const engine::client::WorldPlaces& places, float u0, float v0, float u1, float v1, WorldToScreen&& worldToScreen)
		{
			namespace cells = engine::world::cells;
			namespace dsc = engine::world::discovery;
			// FIX.12 — noir opaque : rien de la carte ne doit transparaître hors des
			// zones explorées (un brun très sombre ressortait gris après la
			// conversion sRGB de l'affichage).
			const ImU32 fog = IM_COL32(0, 0, 0, 255);
			float wx0 = 0, wz0 = 0, wx1 = 0, wz1 = 0;
			places.MapUvToWorld(u0, v0, wx0, wz0);
			places.MapUvToWorld(u1, v1, wx1, wz1);
			const cells::CellCoord nw = cells::CellFromPosition(std::min(wx0, wx1), std::min(wz0, wz1));
			const cells::CellCoord se = cells::CellFromPosition(std::max(wx0, wx1), std::max(wz0, wz1));
			const double C = cells::kCellSizeMeters;
			const double T = dsc::kTileM;
			const int R = dsc::kDisplaySmoothRadiusTiles;
			const int N = dsc::kTilesPerSide;
			const double vx0 = std::min(wx0, wx1), vx1 = std::max(wx0, wx1);
			const double vz0 = std::min(wz0, wz1), vz1 = std::max(wz0, wz1);
			auto rect = [&](double xa, double za, double xb, double zb) {
				const ImVec2 a = worldToScreen(static_cast<float>(xa), static_cast<float>(za));
				const ImVec2 b = worldToScreen(static_cast<float>(xb), static_cast<float>(zb));
				dl->AddRectFilled(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), fog);
			};
			// Taille d'une case à l'écran (px), mesurée près de la vue.
			const float pxPerTile = std::fabs(worldToScreen(static_cast<float>(vx0 + T), static_cast<float>(vz0)).x
				- worldToScreen(static_cast<float>(vx0), static_cast<float>(vz0)).x);
			const bool fine = pxPerTile >= 1.5f;
			// Sous-cases par côté pour une case de bord (régime fin).
			const int sub = pxPerTile >= 6.0f ? std::clamp(static_cast<int>(pxPerTile / 4.0f), 2, 12)
				: std::clamp(static_cast<int>(pxPerTile / 2.5f), 1, 2);
			const double subM = T / sub;
			// Poids gaussiens SÉPARABLES de dsc::IsPointRevealedSmooth (même σ =
			// kDisplaySmoothSigmaM, même voisinage (2R + 1)², même seuil 0,5), précalculés
			// pour les \ref sub positions d'un échantillon dans sa case : un point de la
			// sous-case s est à (s + 0,5) / sub case du bord, sa case voisine d à
			// (d + 0,5 − (s + 0,5) / sub) cases. exp(−(dx² + dz²) / 2σ²) =
			// exp(−dx² / 2σ²) · exp(−dz² / 2σ²) : 2 × 5 poids au lieu de 25 exp et 25
			// appels indirects par échantillon (≈ 25 fois plus rapide, même contour).
			const int K = 2 * R + 1;
			std::vector<double> wSub(static_cast<size_t>(sub) * static_cast<size_t>(K));
			std::vector<double> wSum(static_cast<size_t>(sub), 0.0);
			{
				const double inv2s2 = 1.0 / (2.0 * dsc::kDisplaySmoothSigmaM * dsc::kDisplaySmoothSigmaM);
				for (int s = 0; s < sub; ++s)
					for (int d = -R; d <= R; ++d)
					{
						const double off = ((d + 0.5) - (s + 0.5) / sub) * T;
						const double w = std::exp(-off * off * inv2s2);
						wSub[static_cast<size_t>(s) * static_cast<size_t>(K) + static_cast<size_t>(d + R)] = w;
						wSum[static_cast<size_t>(s)] += w;
					}
			}

			// Une des 8 voisines de \p c a-t-elle été visitée ? (son contour lissé
			// peut alors déborder dans \p c).
			auto touchesVisited = [&](cells::CellCoord c) -> bool {
				for (int dn = -1; dn <= 1; ++dn)
					for (int de = -1; de <= 1; ++de)
						if ((dn != 0 || de != 0) && disc.Find(cells::CellCoord{ c.n + dn, c.e + de }) != nullptr)
							return true;
				return false;
			};

			// Grilles réutilisées d'une cellule à l'autre (évite une allocation par cellule).
			std::vector<uint8_t> grid;
			std::vector<int> sat;

			// Régime fin : cellule \p c (\p bm nul = jamais visitée, cases toutes cachées).
			auto drawCellSmooth = [&](cells::CellCoord c, const dsc::CellBitmap* bm) {
				const cells::CellPoint mn = cells::CellMinCorner(c);
				// Cases de la cellule recoupant la vue (le reste est hors écran).
				const int tx0 = std::clamp(static_cast<int>(std::floor((vx0 - mn.x) / T)), 0, N - 1);
				const int tx1 = std::clamp(static_cast<int>(std::floor((vx1 - mn.x) / T)), 0, N - 1);
				const int tz0 = std::clamp(static_cast<int>(std::floor((vz0 - mn.z) / T)), 0, N - 1);
				const int tz1 = std::clamp(static_cast<int>(std::floor((vz1 - mn.z) / T)), 0, N - 1);
				// Grille locale des cases découvertes (vue + marge R), lue une seule
				// fois : les cases hors cellule viennent des cellules voisines.
				const int gw = tx1 - tx0 + 1 + 2 * R, gh = tz1 - tz0 + 1 + 2 * R;
				grid.assign(static_cast<size_t>(gw) * static_cast<size_t>(gh), 0u);
				const dsc::CellBitmap* lastNb = nullptr;
				cells::CellCoord lastCell{ c.n + 1000, c.e };
				for (int lz = 0; lz < gh; ++lz)
					for (int lx = 0; lx < gw; ++lx)
					{
						const int tx = tx0 - R + lx, tz = tz0 - R + lz;
						bool f = false;
						if (tx >= 0 && tz >= 0 && tx < N && tz < N)
							f = bm != nullptr && bm->Get(tx, tz);
						else
						{
							const dsc::TileRef t = dsc::TileAt(mn.x + (tx + 0.5) * T, mn.z + (tz + 0.5) * T);
							if (t.cell != lastCell) { lastCell = t.cell; lastNb = disc.Find(t.cell); }
							f = lastNb != nullptr && lastNb->Get(t.tx, t.tz);
						}
						grid[static_cast<size_t>(lz) * static_cast<size_t>(gw) + static_cast<size_t>(lx)] = f ? 1u : 0u;
					}
				// Table de sommes (gw + 1) × (gh + 1) : voisinage 5 × 5 en O(1).
				const size_t sw = static_cast<size_t>(gw) + 1u;
				sat.assign(sw * (static_cast<size_t>(gh) + 1u), 0);
				for (int lz = 0; lz < gh; ++lz)
					for (int lx = 0; lx < gw; ++lx)
						sat[(static_cast<size_t>(lz) + 1u) * sw + static_cast<size_t>(lx) + 1u] =
							grid[static_cast<size_t>(lz) * static_cast<size_t>(gw) + static_cast<size_t>(lx)]
							+ sat[static_cast<size_t>(lz) * sw + static_cast<size_t>(lx) + 1u]
							+ sat[(static_cast<size_t>(lz) + 1u) * sw + static_cast<size_t>(lx)]
							- sat[static_cast<size_t>(lz) * sw + static_cast<size_t>(lx)];
				// Sous-case (sx, sz) de la case (tx, tz) affichée « découverte » : moyenne
				// gaussienne séparable du voisinage (2R + 1)² lu dans la grille locale
				// (équivalent de dsc::IsPointRevealedSmooth, cf. wSub).
				auto revealed = [&](int tx, int tz, int sx, int sz) -> bool {
					const double* wx = &wSub[static_cast<size_t>(sx) * static_cast<size_t>(K)];
					const double* wz = &wSub[static_cast<size_t>(sz) * static_cast<size_t>(K)];
					const size_t lx0 = static_cast<size_t>(tx - tx0), lz0 = static_cast<size_t>(tz - tz0);
					double f = 0.0;
					for (int dj = 0; dj < K; ++dj)
					{
						const uint8_t* row = &grid[(lz0 + static_cast<size_t>(dj)) * static_cast<size_t>(gw) + lx0];
						double rowSum = 0.0;
						for (int di = 0; di < K; ++di)
							if (row[di] != 0u)
								rowSum += wx[di];
						f += wz[dj] * rowSum;
					}
					return f >= 0.5 * wSum[static_cast<size_t>(sx)] * wSum[static_cast<size_t>(sz)];
				};
				// État du voisinage 5 × 5 d'une case : 0 rien, 1 mélange, 2 tout découvert.
				const int all = (2 * R + 1) * (2 * R + 1);
				auto neighbourhood = [&](int tx, int tz) -> int {
					const size_t xa = static_cast<size_t>(tx - tx0), xb = xa + static_cast<size_t>(2 * R + 1);
					const size_t za = static_cast<size_t>(tz - tz0), zb = za + static_cast<size_t>(2 * R + 1);
					const int n = sat[zb * sw + xb] - sat[za * sw + xb] - sat[zb * sw + xa] + sat[za * sw + xa];
					return n == 0 ? 0 : (n == all ? 2 : 1);
				};
				for (int tz = tz0; tz <= tz1; ++tz)
				{
					int tx = tx0;
					while (tx <= tx1)
					{
						const int state = neighbourhood(tx, tz);
						if (state == 2) { ++tx; continue; }
						if (state == 1)
						{
							// Bord de zone : sous-cases voilées par séries horizontales.
							for (int sz = 0; sz < sub; ++sz)
							{
								int sx = 0;
								while (sx < sub)
								{
									if (revealed(tx, tz, sx, sz)) { ++sx; continue; }
									const int s0 = sx;
									while (sx < sub && !revealed(tx, tz, sx, sz)) ++sx;
									rect(mn.x + tx * T + s0 * subM, mn.z + tz * T + sz * subM,
										mn.x + tx * T + sx * subM, mn.z + tz * T + (sz + 1) * subM);
								}
							}
							++tx;
							continue;
						}
						// Série de cases entièrement voilées (aucune découverte autour).
						const int start = tx;
						while (tx <= tx1 && neighbourhood(tx, tz) == 0) ++tx;
						rect(mn.x + start * T, mn.z + tz * T, mn.x + tx * T, mn.z + (tz + 1) * T);
					}
				}
			};

			// Vue large : blocs de B × B cases (≥ 1,5 px), voilés si moins de la
			// moitié de leurs cases est découverte. \p bm non nul (cellule visitée).
			auto drawCellCoarse = [&](cells::CellCoord c, const dsc::CellBitmap& bm) {
				const cells::CellPoint mn = cells::CellMinCorner(c);
				const int B = std::clamp(static_cast<int>(std::ceil(1.5f / std::max(pxPerTile, 1.0e-4f))), 1, N);
				const int tx0 = std::clamp(static_cast<int>(std::floor((vx0 - mn.x) / T)), 0, N - 1);
				const int tx1 = std::clamp(static_cast<int>(std::floor((vx1 - mn.x) / T)), 0, N - 1);
				const int tz0 = std::clamp(static_cast<int>(std::floor((vz0 - mn.z) / T)), 0, N - 1);
				const int tz1 = std::clamp(static_cast<int>(std::floor((vz1 - mn.z) / T)), 0, N - 1);
				auto blockHidden = [&](int bx, int bz) -> bool {
					const int xa = bx * B, xb = std::min(N, xa + B);
					const int za = bz * B, zb = std::min(N, za + B);
					int n = 0;
					for (int tz = za; tz < zb; ++tz)
						for (int tx = xa; tx < xb; ++tx)
							n += bm.Get(tx, tz) ? 1 : 0;
					return 2 * n < (xb - xa) * (zb - za);
				};
				for (int bz = tz0 / B; bz <= tz1 / B; ++bz)
				{
					int bx = tx0 / B;
					while (bx <= tx1 / B)
					{
						if (!blockHidden(bx, bz)) { ++bx; continue; }
						const int start = bx;
						while (bx <= tx1 / B && blockHidden(bx, bz)) ++bx;
						rect(mn.x + start * B * T, mn.z + bz * B * T,
							mn.x + std::min(N, bx * B) * T, mn.z + std::min(N, (bz + 1) * B) * T);
					}
				}
			};

			// Nord = n le plus grand = z le plus petit.
			for (int n = nw.n; n >= se.n; --n)
			{
				int runStart = 0;
				bool inRun = false;
				for (int e = nw.e; e <= se.e + 1; ++e)
				{
					const bool atEnd = e > se.e;
					const cells::CellCoord c{ n, e };
					const dsc::CellBitmap* bm = atEnd ? nullptr : disc.Find(c);
					// Cellule dessinée case par case : visitée, ou (régime fin) voisine
					// d'une visitée ; sinon voilée en entier, par séries fusionnées.
					const bool detail = !atEnd && (bm != nullptr || (fine && touchesVisited(c)));
					const bool hidden = !atEnd && !detail;
					if (hidden && !inRun) { inRun = true; runStart = e; }
					if ((!hidden || atEnd) && inRun)
					{
						const cells::CellPoint a = cells::CellMinCorner({ n, runStart });
						const cells::CellPoint b = cells::CellMinCorner({ n, e - 1 });
						rect(a.x, a.z, b.x + C, b.z + C);
						inRun = false;
					}
					if (!detail)
						continue;
					if (fine)
						drawCellSmooth(c, bm);
					else
						drawCellCoarse(c, *bm);
				}
			}
		}
	}

	void DrawWorldMapWindow(bool* open, const WorldMapHudInput& in, WorldMapViewState& view)
	{
		if (open == nullptr || !*open || in.places == nullptr || !in.places->IsLoaded())
			return;
		const engine::client::WorldPlaces& places = *in.places;

		// Fenêtre carrée centrée au premier affichage (~80 % de la hauteur).
		const float side0 = std::max(320.0f, std::min(in.viewportW, in.viewportH) * 0.8f);
		ImGui::SetNextWindowSize(ImVec2(side0, side0 + 56.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(in.viewportW * 0.5f, in.viewportH * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
		const std::string title = (places.DisplayName().empty() ? std::string("Carte du monde") : places.DisplayName())
			+ "###carte_du_monde";
		if (!ImGui::Begin(title.c_str(), open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse))
		{
			ImGui::End();
			return;
		}

		float pu = 0.5f, pv = 0.5f;
		const bool playerOnMap = places.WorldToMapUv(in.playerX, in.playerZ, pu, pv);
		// FIX.11 — zoom exprimé en mètres visibles : ouverture sur ≈ defaultViewM
		// autour du joueur (sinon toute la carte : un déplacement ne se voyait pas),
		// zoom maximal ≈ minViewM visibles.
		const float maxZoom = engine::client::WorldMapMaxZoom(places.SizeM(), in.minViewM);
		if (view.initPending)
		{
			view.zoom = engine::client::WorldMapZoomForView(places.SizeM(), in.defaultViewM, maxZoom);
			view.followPlayer = true;
			view.anchorValid = false;
			view.initPending = false;
		}
		view.zoom = std::clamp(view.zoom, 1.0f, maxZoom);
		// Taille des marqueurs / libellés : réglée pour z <= 8, figée au-delà.
		const float markerZoom = engine::client::WorldMapMarkerZoom(view.zoom);

		// Lot D (bug 1) — la zone de dessin suit la région de contenu de la fenêtre à
		// chaque frame (avant : carré min(largeur, hauteur), figé quand on n'élargissait
		// que dans une direction). Pas d'étirement : une seule échelle px / unité carte
		// pour les deux axes ; le zoom fixe ce que montre le PLUS PETIT côté (≈ sizeM /
		// zoom mètres, cf. « ~x km visibles »), le grand côté en montre davantage.
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float footerH = ImGui::GetTextLineHeightWithSpacing() * 1.6f;
		const float canvasW = std::max(64.0f, avail.x);
		const float canvasH = std::max(64.0f, avail.y - footerH);
		const float pxPerUv = std::min(canvasW, canvasH) * view.zoom;
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1(p0.x + canvasW, p0.y + canvasH);
		float halfU = 0.5f * canvasW / pxPerUv;
		float halfV = 0.5f * canvasH / pxPerUv;

		// Interaction : molette = zoom, glisser = déplacer.
		ImGui::InvisibleButton("##carte_zone", ImVec2(canvasW, canvasH));
		const bool hovered = ImGui::IsItemHovered();
		const ImGuiIO& io = ImGui::GetIO();
		// Centre borné : la vue reste sur la carte ; un axe plus grand que la carte
		// entière (fenêtre très allongée, zoom 1) reste centré sur la carte.
		auto clampCenter = [&]()
		{
			const float ref = std::min(canvasW, canvasH) * view.zoom;
			halfU = 0.5f * canvasW / ref;
			halfV = 0.5f * canvasH / ref;
			view.centerU = halfU >= 0.5f ? 0.5f : std::clamp(view.centerU, halfU, 1.0f - halfU);
			view.centerV = halfV >= 0.5f ? 0.5f : std::clamp(view.centerV, halfV, 1.0f - halfV);
		};

		// Lot D (bug 2) — suivi du joueur (comportement choisi) :
		//  - par défaut (ouverture, « Centrer ») la carte suit le joueur : recentrée à
		//    chaque frame ; la molette zoome alors AUTOUR DU JOUEUR sans couper le suivi
		//    (avant : la molette et le moindre glisser coupaient le suivi pour de bon,
		//    il fallait « Centrer » à chaque fois) ;
		//  - glisser la carte coupe le suivi (exploration libre ; la molette zoome alors
		//    autour du curseur) ;
		//  - le suivi revient seul quand, bouton relâché, le joueur s'est déplacé d'au
		//    moins kRefollowMoveM depuis la fin du glisser ET sort de la vue (marge de
		//    10 % de chaque bord) : regarder la carte à l'arrêt ne la fait pas sauter,
		//    marcher hors du cadre la ramène sur le personnage. « Vue d'ensemble »
		//    (carte entière) garde toujours le joueur en vue : pas de retour forcé.
		constexpr float kRefollowMoveM = 12.0f;
		const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
		if (dragging)
		{
			view.followPlayer = false;
			// Ancre = position du joueur à la fin du glisser (mise à jour tant qu'il dure).
			view.anchorX = in.playerX;
			view.anchorZ = in.playerZ;
			view.anchorValid = true;
		}
		else if (!view.followPlayer && playerOnMap)
		{
			if (!view.anchorValid)
			{
				view.anchorX = in.playerX;
				view.anchorZ = in.playerZ;
				view.anchorValid = true;
			}
			const float dx = in.playerX - view.anchorX, dz = in.playerZ - view.anchorZ;
			const bool moved = dx * dx + dz * dz >= kRefollowMoveM * kRefollowMoveM;
			const bool inView = std::fabs(pu - view.centerU) <= halfU * 0.8f && std::fabs(pv - view.centerV) <= halfV * 0.8f;
			if (moved && !inView)
				view.followPlayer = true;
		}
		if (view.followPlayer && playerOnMap) { view.centerU = pu; view.centerV = pv; }
		clampCenter();

		if (hovered && io.MouseWheel != 0.0f)
		{
			const float newZoom = std::clamp(view.zoom * (io.MouseWheel > 0.0f ? 1.25f : 0.8f), 1.0f, maxZoom);
			if (!(view.followPlayer && playerOnMap))
			{
				// Exploration libre : le point sous le curseur reste fixe à l'écran.
				const float mu = view.centerU - halfU + (io.MousePos.x - p0.x) / pxPerUv;
				const float mv = view.centerV - halfV + (io.MousePos.y - p0.y) / pxPerUv;
				const float k = view.zoom / newZoom;
				view.centerU = mu + (view.centerU - mu) * k;
				view.centerV = mv + (view.centerV - mv) * k;
			}
			view.zoom = newZoom;
			if (view.followPlayer && playerOnMap) { view.centerU = pu; view.centerV = pv; }
			clampCenter();
		}
		if (dragging)
		{
			const float ref = std::min(canvasW, canvasH) * view.zoom;
			view.centerU -= io.MouseDelta.x / ref;
			view.centerV -= io.MouseDelta.y / ref;
			clampCenter();
		}

		// Échelle et emprise finales de la frame (après zoom / glisser).
		const float scale = std::min(canvasW, canvasH) * view.zoom; // px par unité carte
		const float u0 = view.centerU - halfU, v0 = view.centerV - halfV;
		const float u1 = view.centerU + halfU, v1 = view.centerV + halfV;
		auto toScreen = [&](float u, float v) { return ImVec2(p0.x + (u - u0) * scale, p0.y + (v - v0) * scale); };

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->PushClipRect(p0, p1, true);
		// Fond (hors carte quand la fenêtre est plus large que la carte entière).
		dl->AddRectFilled(p0, p1, IM_COL32(14, 18, 22, 255));
		if (in.mapTexture != 0)
		{
			// Image limitée à la carte [0, 1] (pas de répétition de texture au-delà).
			const float iu0 = std::max(0.0f, u0), iv0 = std::max(0.0f, v0);
			const float iu1 = std::min(1.0f, u1), iv1 = std::min(1.0f, v1);
			if (iu1 > iu0 && iv1 > iv0)
				dl->AddImage(static_cast<ImTextureID>(in.mapTexture), toScreen(iu0, iv0), toScreen(iu1, iv1), ImVec2(iu0, iv0), ImVec2(iu1, iv1));
		}
		else
		{
			dl->AddRectFilled(p0, p1, IM_COL32(40, 58, 46, 255));
			ShadowText(dl, ImVec2(p0.x + 10.0f, p0.y + 10.0f), IM_COL32(220, 220, 220, 255), "Carte indisponible (world_map.png)");
		}

		auto worldToScreen = [&](float x, float z) {
			float u = 0, v = 0;
			places.WorldToMapUv(x, z, u, v);
			return toScreen(u, v);
		};
		// FIX.12 / FIX.15 — eau, routes, rues et bâtiments (avant le voile : l'inexploré les cache).
		if (in.features != nullptr && !in.features->Empty())
		{
			float wx0 = 0, wz0 = 0, wx1 = 0, wz1 = 0;
			places.MapUvToWorld(u0, v0, wx0, wz0);
			places.MapUvToWorld(u1, v1, wx1, wz1);
			const float pxPerM = scale / std::max(1.0f, places.SizeM());
			DrawMapFeatures(dl, *in.features, wx0, wz0, wx1, wz1, pxPerM, worldToScreen);
		}

		// Carte à découvrir : voile sur ce qui n'a pas été exploré.
		if (in.discovery != nullptr)
			DrawDiscoveryFog(dl, *in.discovery, places, u0, v0, u1, v1, worldToScreen);
		auto discovered = [&](float x, float z) { return in.discovery == nullptr || in.discovery->IsDiscovered(x, z); };

		// Régions : nom en filigrane au centre (vue d'ensemble seulement).
		if (view.zoom <= 2.5f)
			for (const engine::client::PlaceRegion& r : places.Regions())
			{
				// Nom de région : seulement si son centre a été découvert.
				if (!discovered((r.xMin + r.xMax) * 0.5f, (r.zMin + r.zMax) * 0.5f))
					continue;
				float ru = 0, rv = 0;
				places.WorldToMapUv((r.xMin + r.xMax) * 0.5f, (r.zMin + r.zMax) * 0.5f, ru, rv);
				const ImVec2 c = toScreen(ru, rv);
				const ImVec2 ts = ImGui::CalcTextSize(r.name.c_str());
				dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 70), r.name.c_str());
			}

		// Lieux remarquables : losanges violets.
		const engine::client::PlacePoi* hoveredPoi = nullptr;
		const engine::client::PlaceCity* hoveredCity = nullptr;
		float bestHover = 10.0f * 10.0f;
		for (const engine::client::PlacePoi& p : places.Pois())
		{
			if (!discovered(p.x, p.z))
				continue;
			float u = 0, v = 0;
			places.WorldToMapUv(p.x, p.z, u, v);
			const ImVec2 s = toScreen(u, v);
			const float r = 4.0f + markerZoom * 0.4f;
			dl->AddQuadFilled(ImVec2(s.x, s.y - r), ImVec2(s.x + r, s.y), ImVec2(s.x, s.y + r), ImVec2(s.x - r, s.y), IM_COL32(176, 120, 226, 235));
			dl->AddQuad(ImVec2(s.x, s.y - r), ImVec2(s.x + r, s.y), ImVec2(s.x, s.y + r), ImVec2(s.x - r, s.y), IM_COL32(20, 10, 30, 255));
			if (view.zoom >= 2.0f)
				ShadowText(dl, ImVec2(s.x + r + 3.0f, s.y - 7.0f), IM_COL32(220, 196, 255, 255), p.name.c_str());
			const float d2 = (io.MousePos.x - s.x) * (io.MousePos.x - s.x) + (io.MousePos.y - s.y) * (io.MousePos.y - s.y);
			if (hovered && d2 < bestHover) { bestHover = d2; hoveredPoi = &p; }
		}

		// Villes : disque coloré par peuple, liseré doré pour une ville de départ
		// (épais pour celle de la race du joueur), croix grise pour une ruine.
		const std::string myFaction = in.playerFaction ? in.playerFaction : "";
		for (const engine::client::PlaceCity& c : places.Cities())
		{
			if (!discovered(c.x, c.z))
				continue;
			float u = 0, v = 0;
			places.WorldToMapUv(c.x, c.z, u, v);
			const ImVec2 s = toScreen(u, v);
			const float r = KindRadius(c.kind) * (0.8f + 0.25f * markerZoom);
			const bool ruin = c.state == "ruine";
			const bool isStart = !c.startFor.empty();
			const bool myStart = !myFaction.empty() && std::find(c.startFor.begin(), c.startFor.end(), myFaction) != c.startFor.end();
			dl->AddCircleFilled(s, r, ruin ? IM_COL32(120, 112, 104, 235) : RaceColor(c.race, 240));
			dl->AddCircle(s, r, IM_COL32(20, 16, 12, 255), 0, 1.5f);
			if (isStart) dl->AddCircle(s, r + 3.0f, IM_COL32(255, 210, 90, 255), 0, myStart ? 3.0f : 1.5f);
			if (ruin)
			{
				dl->AddLine(ImVec2(s.x - r, s.y - r), ImVec2(s.x + r, s.y + r), IM_COL32(40, 30, 30, 255), 1.5f);
				dl->AddLine(ImVec2(s.x - r, s.y + r), ImVec2(s.x + r, s.y - r), IM_COL32(40, 30, 30, 255), 1.5f);
			}
			else if (c.state == "reconstruction")
			{
				// Ville en reconstruction : un seul trait (demi-croix des ruines).
				dl->AddLine(ImVec2(s.x - r, s.y + r), ImVec2(s.x + r, s.y - r), IM_COL32(40, 30, 30, 255), 1.5f);
			}
			const float d2 = (io.MousePos.x - s.x) * (io.MousePos.x - s.x) + (io.MousePos.y - s.y) * (io.MousePos.y - s.y);
			if (hovered && d2 < std::max(bestHover, (r + 3.0f) * (r + 3.0f))) { bestHover = d2; hoveredCity = &c; hoveredPoi = nullptr; }
			if (AlwaysLabelled(c) || view.zoom >= 2.0f)
				ShadowText(dl, ImVec2(s.x + r + 4.0f, s.y - 7.0f),
					myStart ? IM_COL32(255, 222, 120, 255) : IM_COL32(245, 240, 228, 255), c.name.c_str());
		}

		// Lore v1.0.114 — lieux secrets découverts par le joueur : étoile à quatre
		// branches turquoise cerclée de noir (distincte des losanges violets et des
		// disques des villes), visible même sous le voile (le joueur y est allé).
		const engine::server::DiscoveredPlaceWire* hoveredSecret = nullptr;
		if (in.secretPlaces != nullptr)
			for (const engine::server::DiscoveredPlaceWire& p : *in.secretPlaces)
			{
				if (p.map != in.currentMap)
					continue;
				float u = 0, v = 0;
				places.WorldToMapUv(p.x, p.z, u, v);
				const ImVec2 s = toScreen(u, v);
				const float r = 6.0f + markerZoom * 0.4f, k = r * 0.35f;
				const ImVec2 star[8] = { ImVec2(s.x, s.y - r), ImVec2(s.x + k, s.y - k), ImVec2(s.x + r, s.y), ImVec2(s.x + k, s.y + k),
					ImVec2(s.x, s.y + r), ImVec2(s.x - k, s.y + k), ImVec2(s.x - r, s.y), ImVec2(s.x - k, s.y - k) };
				// Étoile (concave) remplie en éventail depuis le centre.
				for (int i = 0; i < 8; ++i)
					dl->AddTriangleFilled(s, star[i], star[(i + 1) % 8], IM_COL32(64, 224, 208, 245));
				dl->AddPolyline(star, 8, IM_COL32(10, 30, 30, 255), ImDrawFlags_Closed, 1.5f);
				if (view.zoom >= 2.0f)
					ShadowText(dl, ImVec2(s.x + r + 3.0f, s.y - 7.0f), IM_COL32(150, 255, 240, 255), p.name.c_str());
				const float d2 = (io.MousePos.x - s.x) * (io.MousePos.x - s.x) + (io.MousePos.y - s.y) * (io.MousePos.y - s.y);
				if (hovered && d2 < std::max(bestHover, (r + 2.0f) * (r + 2.0f)))
				{
					bestHover = d2;
					hoveredSecret = &p;
					hoveredCity = nullptr;
					hoveredPoi = nullptr;
				}
			}

		// Joueur : flèche orientée selon son cap (avant = (sin yaw, cos yaw), +Z = bas).
		if (playerOnMap)
		{
			const ImVec2 s = toScreen(pu, pv);
			const float fx = std::sin(in.playerYaw), fz = std::cos(in.playerYaw);
			const float L = 11.0f, W = 6.0f;
			const ImVec2 tip(s.x + fx * L, s.y + fz * L);
			const ImVec2 left(s.x - fx * W * 0.6f + fz * W, s.y - fz * W * 0.6f - fx * W);
			const ImVec2 right(s.x - fx * W * 0.6f - fz * W, s.y - fz * W * 0.6f + fx * W);
			dl->AddTriangleFilled(tip, left, right, IM_COL32(255, 70, 50, 255));
			dl->AddTriangle(tip, left, right, IM_COL32(255, 255, 255, 255), 1.5f);
		}
		dl->AddRect(p0, p1, IM_COL32(150, 130, 90, 255), 0.0f, 0, 2.0f);
		dl->PopClipRect();

		// Info-bulle du lieu survolé.
		if (hoveredSecret != nullptr)
		{
			// Lore v1.0.114 — lieu secret découvert : nom, genre, mention « découvert ».
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(hoveredSecret->name.c_str());
			ImGui::TextDisabled("%s", engine::client::PlaceKindLabel(hoveredSecret->kind).c_str());
			ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.85f, 1.0f), "Lieu secret decouvert");
			ImGui::EndTooltip();
		}
		else if (hoveredCity != nullptr)
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(hoveredCity->name.c_str());
			std::string line = engine::client::PlaceKindLabel(hoveredCity->kind);
			if (hoveredCity->state == "ruine") line += " (en ruine)";
			else if (hoveredCity->state == "reconstruction") line += " (en reconstruction)";
			if (!hoveredCity->race.empty()) line += " - " + hoveredCity->race;
			ImGui::TextDisabled("%s", line.c_str());
			if (!hoveredCity->startFor.empty())
			{
				std::string starts;
				for (const std::string& s : hoveredCity->startFor) starts += (starts.empty() ? "" : ", ") + s;
				ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "Ville de depart : %s", starts.c_str());
			}
			if (const engine::client::PlaceRegion* reg = places.RegionAt(hoveredCity->x, hoveredCity->z))
				ImGui::TextDisabled("%s", reg->name.c_str());
			if (!hoveredCity->description.empty())
			{
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
				ImGui::TextUnformatted(hoveredCity->description.c_str());
				ImGui::PopTextWrapPos();
			}
			ImGui::EndTooltip();
		}
		else if (hoveredPoi != nullptr)
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(hoveredPoi->name.c_str());
			ImGui::TextDisabled("%s", engine::client::PlaceKindLabel(hoveredPoi->kind).c_str());
			if (!hoveredPoi->description.empty())
			{
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
				ImGui::TextUnformatted(hoveredPoi->description.c_str());
				ImGui::PopTextWrapPos();
			}
			ImGui::EndTooltip();
		}

		// Pied : lieu courant, coordonnées, aide.
		const engine::client::PlaceCity* here = places.CityAt(in.playerX, in.playerZ, 6.0f);
		const engine::client::PlaceRegion* region = places.RegionAt(in.playerX, in.playerZ);
		std::string where = here ? here->name : (region ? region->name : std::string("Terres sauvages"));
		ImGui::Text("%s  (%.0f, %.0f)", where.c_str(), in.playerX, in.playerZ);
		ImGui::SameLine();
		if (ImGui::SmallButton("Centrer")) { view.followPlayer = true; }
		ImGui::SameLine();
		if (ImGui::SmallButton("Vue d'ensemble"))
		{
			view.zoom = 1.0f;
			view.followPlayer = false;
			view.centerU = view.centerV = 0.5f;
			view.anchorX = in.playerX;
			view.anchorZ = in.playerZ;
			view.anchorValid = true;
		}
		ImGui::SameLine();
		// FIX.11 — échelle courante (côté visible).
		{
			const float visibleM = engine::client::WorldMapVisibleMeters(places.SizeM(), view.zoom);
			if (visibleM >= 1000.0f)
				ImGui::TextDisabled("~%.1f km visibles", visibleM / 1000.0f);
			else
				ImGui::TextDisabled("~%.0f m visibles", visibleM);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("Molette : zoom - Glisser : deplacer - M : fermer");
		ImGui::End();
	}

	void DrawPlaceBanner(ImDrawList* fg, float dw, float /*dh*/, const engine::client::PlaceBannerTracker& banner,
		engine::client::ZoneLevelTone tone)
	{
		if (fg == nullptr || !banner.Visible())
			return;
		const float a = banner.Alpha();
		const int ia = static_cast<int>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
		ImFont* font = ImGui::GetFont();
		const float titleSize = ImGui::GetFontSize() * 2.0f;
		const float subSize = ImGui::GetFontSize() * 1.1f;
		const ImVec2 ts = font->CalcTextSizeA(titleSize, 4096.0f, 0.0f, banner.Title().c_str());
		const ImVec2 ss = font->CalcTextSizeA(subSize, 4096.0f, 0.0f, banner.Subtitle().c_str());
		const float y = 108.0f;
		const float x = (dw - ts.x) * 0.5f;
		// Filets décoratifs de part et d'autre du titre.
		const float lineW = std::max(ts.x, ss.x) * 0.5f + 40.0f;
		const float cx = dw * 0.5f;
		fg->AddLine(ImVec2(cx - lineW, y + ts.y + 4.0f), ImVec2(cx + lineW, y + ts.y + 4.0f), IM_COL32(214, 180, 104, ia * 3 / 4), 1.5f);
		fg->AddText(font, titleSize, ImVec2(x + 2.0f, y + 2.0f), IM_COL32(0, 0, 0, ia * 4 / 5), banner.Title().c_str());
		// v1.0.118 — couleur du nom de zone selon l'écart de niveau (gris, vert, jaune, orange, rouge).
		uint8_t tr = 246, tg = 226, tb = 172;
		engine::client::ZoneLevelToneRgb(tone, tr, tg, tb);
		fg->AddText(font, titleSize, ImVec2(x, y), IM_COL32(tr, tg, tb, ia), banner.Title().c_str());
		if (!banner.Subtitle().empty())
		{
			const ImVec2 sp((dw - ss.x) * 0.5f, y + ts.y + 10.0f);
			fg->AddText(font, subSize, ImVec2(sp.x + 1.0f, sp.y + 1.0f), IM_COL32(0, 0, 0, ia * 4 / 5), banner.Subtitle().c_str());
			fg->AddText(font, subSize, sp, IM_COL32(220, 214, 200, ia), banner.Subtitle().c_str());
		}
	}
}

#else // !_WIN32 — pas d'ImGui lié : no-op (le client jouable est Windows).

namespace engine::render
{
	void DrawWorldMapWindow(bool*, const WorldMapHudInput&, WorldMapViewState&) {}
	void DrawPlaceBanner(ImDrawList*, float, float, const engine::client::PlaceBannerTracker&, engine::client::ZoneLevelTone) {}
}

#endif
