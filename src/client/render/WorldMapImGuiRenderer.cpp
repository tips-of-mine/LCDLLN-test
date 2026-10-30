// Monde jouable (chantier « Royaumes ») — carte du monde + bannière de lieu.
#include "src/client/render/WorldMapImGuiRenderer.h"
#include "src/client/world/places/WorldMapZoom.h"

#include "src/client/world/discovery/MapDiscoveryClient.h"
#include "src/client/world/places/WorldPlaces.h"
#include "src/shared/world/MapDiscovery.h"
#include "src/shared/world/WorldCells.h"

#include <algorithm>
#include <cmath>
#include <string>

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

		/// Voile des zones non découvertes. \param worldToScreen projette un point
		/// GLOBAL (x, z) à l'écran. Les cellules jamais visitées sont voilées en
		/// entier (séries fusionnées par rangée) ; dans une cellule visitée, les
		/// cases non découvertes sont voilées par séries horizontales.
		template <typename WorldToScreen>
		void DrawDiscoveryFog(ImDrawList* dl, const engine::client::MapDiscoveryClient& disc,
			const engine::client::WorldPlaces& places, float u0, float v0, float u1, float v1, WorldToScreen&& worldToScreen)
		{
			namespace cells = engine::world::cells;
			namespace dsc = engine::world::discovery;
			// Opaque : rien de la carte ne doit transparaître hors des zones explorées.
			const ImU32 fog = IM_COL32(26, 22, 18, 255);
			float wx0 = 0, wz0 = 0, wx1 = 0, wz1 = 0;
			places.MapUvToWorld(u0, v0, wx0, wz0);
			places.MapUvToWorld(u1, v1, wx1, wz1);
			const cells::CellCoord nw = cells::CellFromPosition(std::min(wx0, wx1), std::min(wz0, wz1));
			const cells::CellCoord se = cells::CellFromPosition(std::max(wx0, wx1), std::max(wz0, wz1));
			const double C = cells::kCellSizeMeters;
			auto rect = [&](double xa, double za, double xb, double zb) {
				const ImVec2 a = worldToScreen(static_cast<float>(xa), static_cast<float>(za));
				const ImVec2 b = worldToScreen(static_cast<float>(xb), static_cast<float>(zb));
				dl->AddRectFilled(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), fog);
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
					const bool hidden = !atEnd && bm == nullptr;
					if (hidden && !inRun) { inRun = true; runStart = e; }
					if ((!hidden || atEnd) && inRun)
					{
						const cells::CellPoint a = cells::CellMinCorner({ n, runStart });
						const cells::CellPoint b = cells::CellMinCorner({ n, e - 1 });
						rect(a.x, a.z, b.x + C, b.z + C);
						inRun = false;
					}
					if (bm == nullptr)
						continue;
					// Cellule visitée : séries de cases non découvertes, rangée par rangée.
					const cells::CellPoint mn = cells::CellMinCorner(c);
					for (int tz = 0; tz < dsc::kTilesPerSide; ++tz)
					{
						int tx = 0;
						while (tx < dsc::kTilesPerSide)
						{
							if (bm->Get(tx, tz)) { ++tx; continue; }
							const int start = tx;
							while (tx < dsc::kTilesPerSide && !bm->Get(tx, tz)) ++tx;
							rect(mn.x + start * dsc::kTileM, mn.z + tz * dsc::kTileM,
								mn.x + tx * dsc::kTileM, mn.z + (tz + 1) * dsc::kTileM);
						}
					}
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
			view.initPending = false;
		}
		view.zoom = std::clamp(view.zoom, 1.0f, maxZoom);
		// Taille des marqueurs / libellés : réglée pour z <= 8, figée au-delà.
		const float markerZoom = engine::client::WorldMapMarkerZoom(view.zoom);
		if (view.followPlayer && playerOnMap) { view.centerU = pu; view.centerV = pv; }

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float footerH = ImGui::GetTextLineHeightWithSpacing() * 1.6f;
		const float side = std::max(64.0f, std::min(avail.x, avail.y - footerH));
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1(p0.x + side, p0.y + side);

		// Interaction : molette = zoom autour du curseur, glisser = déplacer.
		ImGui::InvisibleButton("##carte_zone", ImVec2(side, side));
		const bool hovered = ImGui::IsItemHovered();
		const ImGuiIO& io = ImGui::GetIO();
		float half = 0.5f / view.zoom;
		auto clampCenter = [&]()
		{
			half = 0.5f / view.zoom;
			view.centerU = std::clamp(view.centerU, half, 1.0f - half);
			view.centerV = std::clamp(view.centerV, half, 1.0f - half);
		};
		clampCenter();
		if (hovered && io.MouseWheel != 0.0f)
		{
			const float mu = view.centerU - half + (io.MousePos.x - p0.x) / side * 2.0f * half;
			const float mv = view.centerV - half + (io.MousePos.y - p0.y) / side * 2.0f * half;
			const float newZoom = std::clamp(view.zoom * (io.MouseWheel > 0.0f ? 1.25f : 0.8f), 1.0f, maxZoom);
			// Le point sous le curseur reste fixe à l'écran.
			const float k = view.zoom / newZoom;
			view.centerU = mu + (view.centerU - mu) * k;
			view.centerV = mv + (view.centerV - mv) * k;
			view.zoom = newZoom;
			view.followPlayer = false;
			clampCenter();
		}
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
		{
			view.centerU -= io.MouseDelta.x / side * 2.0f * half;
			view.centerV -= io.MouseDelta.y / side * 2.0f * half;
			view.followPlayer = false;
			clampCenter();
		}

		const float u0 = view.centerU - half, v0 = view.centerV - half;
		auto toScreen = [&](float u, float v) { return ImVec2(p0.x + (u - u0) / (2.0f * half) * side, p0.y + (v - v0) / (2.0f * half) * side); };

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->PushClipRect(p0, p1, true);
		if (in.mapTexture != 0)
			dl->AddImage(static_cast<ImTextureID>(in.mapTexture), p0, p1, ImVec2(u0, v0), ImVec2(u0 + 2.0f * half, v0 + 2.0f * half));
		else
		{
			dl->AddRectFilled(p0, p1, IM_COL32(40, 58, 46, 255));
			ShadowText(dl, ImVec2(p0.x + 10.0f, p0.y + 10.0f), IM_COL32(220, 220, 220, 255), "Carte indisponible (world_map.png)");
		}

		// Carte à découvrir : voile sur ce qui n'a pas été exploré.
		auto worldToScreen = [&](float x, float z) {
			float u = 0, v = 0;
			places.WorldToMapUv(x, z, u, v);
			return toScreen(u, v);
		};
		if (in.discovery != nullptr)
			DrawDiscoveryFog(dl, *in.discovery, places, u0, v0, u0 + 2.0f * half, v0 + 2.0f * half, worldToScreen);
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
		const std::string myRace = in.playerRace ? in.playerRace : "";
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
			const bool myStart = !myRace.empty() && std::find(c.startFor.begin(), c.startFor.end(), myRace) != c.startFor.end();
			dl->AddCircleFilled(s, r, ruin ? IM_COL32(120, 112, 104, 235) : RaceColor(c.race, 240));
			dl->AddCircle(s, r, IM_COL32(20, 16, 12, 255), 0, 1.5f);
			if (isStart) dl->AddCircle(s, r + 3.0f, IM_COL32(255, 210, 90, 255), 0, myStart ? 3.0f : 1.5f);
			if (ruin)
			{
				dl->AddLine(ImVec2(s.x - r, s.y - r), ImVec2(s.x + r, s.y + r), IM_COL32(40, 30, 30, 255), 1.5f);
				dl->AddLine(ImVec2(s.x - r, s.y + r), ImVec2(s.x + r, s.y - r), IM_COL32(40, 30, 30, 255), 1.5f);
			}
			const float d2 = (io.MousePos.x - s.x) * (io.MousePos.x - s.x) + (io.MousePos.y - s.y) * (io.MousePos.y - s.y);
			if (hovered && d2 < std::max(bestHover, (r + 3.0f) * (r + 3.0f))) { bestHover = d2; hoveredCity = &c; hoveredPoi = nullptr; }
			if (AlwaysLabelled(c) || view.zoom >= 2.0f)
				ShadowText(dl, ImVec2(s.x + r + 4.0f, s.y - 7.0f),
					myStart ? IM_COL32(255, 222, 120, 255) : IM_COL32(245, 240, 228, 255), c.name.c_str());
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
		if (hoveredCity != nullptr)
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(hoveredCity->name.c_str());
			std::string line = engine::client::PlaceKindLabel(hoveredCity->kind);
			if (hoveredCity->state == "ruine") line += " (en ruine)";
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
		if (ImGui::SmallButton("Vue d'ensemble")) { view.zoom = 1.0f; view.followPlayer = false; view.centerU = view.centerV = 0.5f; }
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

	void DrawPlaceBanner(ImDrawList* fg, float dw, float /*dh*/, const engine::client::PlaceBannerTracker& banner)
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
		fg->AddText(font, titleSize, ImVec2(x, y), IM_COL32(246, 226, 172, ia), banner.Title().c_str());
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
	void DrawPlaceBanner(ImDrawList*, float, float, const engine::client::PlaceBannerTracker&) {}
}

#endif
