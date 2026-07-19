// ToolGlyphs — implémentation. Voir ToolGlyphs.h.
//
// Chaque glyphe est composé de 2 à 6 primitives ImDrawList dans un repère
// normalisé : les helpers X()/Y() mappent [0,1]² sur le rectangle cible
// (avec une petite marge). Épaisseur de trait proportionnelle à la taille
// pour rester lisible du 16 px (palette) au 32 px (usage futur).

#include "src/world_editor/ui/ToolGlyphs.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world
{
#if defined(_WIN32)
	namespace
	{
		/// Contexte de dessin d'un glyphe : rectangle cible + couleur +
		/// épaisseur de trait dérivée de la taille.
		struct GlyphCtx
		{
			ImDrawList* dl = nullptr;
			float x0 = 0.0f, y0 = 0.0f, w = 0.0f, h = 0.0f;
			ImU32 col = 0;
			float th = 1.5f;

			/// Coordonnée écran depuis une abscisse normalisée [0,1].
			float X(float u) const { return x0 + u * w; }
			/// Coordonnée écran depuis une ordonnée normalisée [0,1] (0 = haut).
			float Y(float v) const { return y0 + v * h; }
			ImVec2 P(float u, float v) const { return ImVec2(X(u), Y(v)); }
		};

		// ---- Terrain --------------------------------------------------------

		/// Sculpture : colline pleine + flèche montante (on modèle le relief).
		void GlyphSculpt(const GlyphCtx& c)
		{
			c.dl->AddTriangleFilled(c.P(0.10f, 0.85f), c.P(0.50f, 0.45f), c.P(0.90f, 0.85f), c.col);
			c.dl->AddLine(c.P(0.50f, 0.35f), c.P(0.50f, 0.10f), c.col, c.th);
			c.dl->AddTriangleFilled(c.P(0.38f, 0.22f), c.P(0.62f, 0.22f), c.P(0.50f, 0.05f), c.col);
		}

		/// Tampon : cercle épais + point central (empreinte de stamp).
		void GlyphStamp(const GlyphCtx& c)
		{
			c.dl->AddCircle(c.P(0.5f, 0.5f), c.w * 0.34f, c.col, 0, c.th);
			c.dl->AddCircleFilled(c.P(0.5f, 0.5f), c.w * 0.12f, c.col);
		}

		/// Peinture de texture : goutte de peinture (cercle + pointe).
		void GlyphSplat(const GlyphCtx& c)
		{
			c.dl->AddCircleFilled(c.P(0.5f, 0.62f), c.w * 0.26f, c.col);
			c.dl->AddTriangleFilled(c.P(0.34f, 0.52f), c.P(0.66f, 0.52f), c.P(0.5f, 0.12f), c.col);
		}

		// ---- Eau ------------------------------------------------------------

		/// Lac : plan d'eau elliptique + une vague.
		void GlyphLake(const GlyphCtx& c)
		{
			c.dl->AddEllipse(c.P(0.5f, 0.55f), ImVec2(c.w * 0.36f, c.h * 0.24f), c.col, 0.0f, 0, c.th);
			c.dl->PathLineTo(c.P(0.30f, 0.55f));
			c.dl->PathBezierCubicCurveTo(c.P(0.42f, 0.45f), c.P(0.58f, 0.65f), c.P(0.70f, 0.55f));
			c.dl->PathStroke(c.col, 0, c.th);
		}

		/// Rivière : serpentine verticale épaisse.
		void GlyphRiver(const GlyphCtx& c)
		{
			c.dl->PathLineTo(c.P(0.35f, 0.10f));
			c.dl->PathBezierCubicCurveTo(c.P(0.85f, 0.30f), c.P(0.15f, 0.65f), c.P(0.65f, 0.90f));
			c.dl->PathStroke(c.col, 0, c.th * 1.6f);
		}

		/// Réseau fluvial : affluents en Y ramifié.
		void GlyphRiverNetwork(const GlyphCtx& c)
		{
			c.dl->AddLine(c.P(0.50f, 0.90f), c.P(0.50f, 0.50f), c.col, c.th * 1.4f);
			c.dl->AddLine(c.P(0.50f, 0.50f), c.P(0.22f, 0.14f), c.col, c.th);
			c.dl->AddLine(c.P(0.50f, 0.50f), c.P(0.78f, 0.14f), c.col, c.th);
			c.dl->AddLine(c.P(0.36f, 0.32f), c.P(0.58f, 0.24f), c.col, c.th * 0.8f);
		}

		/// Littoral : ligne de côte ondulée + trait de plage dessous.
		void GlyphCoastline(const GlyphCtx& c)
		{
			c.dl->PathLineTo(c.P(0.10f, 0.42f));
			c.dl->PathBezierCubicCurveTo(c.P(0.35f, 0.25f), c.P(0.60f, 0.58f), c.P(0.90f, 0.40f));
			c.dl->PathStroke(c.col, 0, c.th);
			c.dl->AddLine(c.P(0.16f, 0.68f), c.P(0.84f, 0.68f), c.col, c.th * 0.8f);
			c.dl->AddLine(c.P(0.28f, 0.82f), c.P(0.72f, 0.82f), c.col, c.th * 0.6f);
		}

		// ---- Macro ----------------------------------------------------------

		/// Chaîne de montagnes : deux pics imbriqués.
		void GlyphMountainRange(const GlyphCtx& c)
		{
			c.dl->AddTriangleFilled(c.P(0.08f, 0.85f), c.P(0.38f, 0.25f), c.P(0.68f, 0.85f), c.col);
			c.dl->AddTriangle(c.P(0.45f, 0.85f), c.P(0.72f, 0.40f), c.P(0.95f, 0.85f), c.col, c.th);
		}

		/// Chaîne de vallées : profil en V entre deux crêtes.
		void GlyphValleyChain(const GlyphCtx& c)
		{
			c.dl->PathLineTo(c.P(0.10f, 0.20f));
			c.dl->PathLineTo(c.P(0.50f, 0.80f));
			c.dl->PathLineTo(c.P(0.90f, 0.20f));
			c.dl->PathStroke(c.col, 0, c.th * 1.5f);
			c.dl->AddLine(c.P(0.30f, 0.20f), c.P(0.50f, 0.52f), c.col, c.th * 0.7f);
			c.dl->AddLine(c.P(0.70f, 0.20f), c.P(0.50f, 0.52f), c.col, c.th * 0.7f);
		}

		/// Érosion hydraulique : goutte + traînées de ruissellement.
		void GlyphHydraulicErosion(const GlyphCtx& c)
		{
			c.dl->AddCircleFilled(c.P(0.5f, 0.40f), c.w * 0.20f, c.col);
			c.dl->AddTriangleFilled(c.P(0.36f, 0.32f), c.P(0.64f, 0.32f), c.P(0.5f, 0.08f), c.col);
			c.dl->AddLine(c.P(0.34f, 0.66f), c.P(0.28f, 0.88f), c.col, c.th * 0.8f);
			c.dl->AddLine(c.P(0.50f, 0.66f), c.P(0.50f, 0.92f), c.col, c.th * 0.8f);
			c.dl->AddLine(c.P(0.66f, 0.66f), c.P(0.72f, 0.88f), c.col, c.th * 0.8f);
		}

		/// Érosion thermique/vent : trois filets de vent.
		void GlyphThermalWind(const GlyphCtx& c)
		{
			for (int i = 0; i < 3; ++i)
			{
				const float v = 0.28f + 0.22f * static_cast<float>(i);
				c.dl->PathLineTo(c.P(0.10f, v));
				c.dl->PathBezierCubicCurveTo(c.P(0.45f, v - 0.10f), c.P(0.60f, v + 0.10f), c.P(0.90f, v));
				c.dl->PathStroke(c.col, 0, c.th * 0.9f);
			}
		}

		// ---- Structures -----------------------------------------------------

		/// Grotte : monticule plein avec ouverture sombre (trou négatif).
		void GlyphCave(const GlyphCtx& c)
		{
			c.dl->PathLineTo(c.P(0.10f, 0.85f));
			c.dl->PathBezierCubicCurveTo(c.P(0.15f, 0.25f), c.P(0.85f, 0.25f), c.P(0.90f, 0.85f));
			c.dl->PathFillConvex(c.col);
			// Ouverture : petite arche « creusée » (couleur ~fond, alpha plein).
			c.dl->PathLineTo(c.P(0.38f, 0.85f));
			c.dl->PathBezierCubicCurveTo(c.P(0.40f, 0.52f), c.P(0.60f, 0.52f), c.P(0.62f, 0.85f));
			c.dl->PathFillConvex(IM_COL32(35, 37, 42, 255));
		}

		/// Surplomb : corniche rocheuse en L inversé au-dessus du vide.
		void GlyphOverhang(const GlyphCtx& c)
		{
			c.dl->AddRectFilled(c.P(0.15f, 0.15f), c.P(0.85f, 0.35f), c.col, 2.0f);
			c.dl->AddRectFilled(c.P(0.15f, 0.35f), c.P(0.38f, 0.88f), c.col, 2.0f);
			c.dl->AddLine(c.P(0.60f, 0.48f), c.P(0.60f, 0.60f), c.col, c.th * 0.7f);
			c.dl->AddLine(c.P(0.74f, 0.48f), c.P(0.74f, 0.68f), c.col, c.th * 0.7f);
		}

		/// Arche : arc de pierre sur deux piliers.
		void GlyphArch(const GlyphCtx& c)
		{
			c.dl->PathLineTo(c.P(0.15f, 0.85f));
			c.dl->PathLineTo(c.P(0.15f, 0.45f));
			c.dl->PathBezierCubicCurveTo(c.P(0.20f, 0.12f), c.P(0.80f, 0.12f), c.P(0.85f, 0.45f));
			c.dl->PathLineTo(c.P(0.85f, 0.85f));
			c.dl->PathStroke(c.col, 0, c.th * 1.5f);
		}

		/// Portail de donjon : porte en ogive + poignée.
		void GlyphDungeonPortal(const GlyphCtx& c)
		{
			c.dl->PathLineTo(c.P(0.28f, 0.88f));
			c.dl->PathLineTo(c.P(0.28f, 0.40f));
			c.dl->PathBezierCubicCurveTo(c.P(0.30f, 0.12f), c.P(0.70f, 0.12f), c.P(0.72f, 0.40f));
			c.dl->PathLineTo(c.P(0.72f, 0.88f));
			c.dl->PathFillConvex(c.col);
			c.dl->AddCircleFilled(c.P(0.60f, 0.58f), c.w * 0.05f, IM_COL32(35, 37, 42, 255));
			c.dl->AddLine(c.P(0.22f, 0.88f), c.P(0.78f, 0.88f), c.col, c.th);
		}

		// ---- Gameplay (Roadmap-8) -------------------------------------------

		/// Spline : courbe en S + nœuds ronds (route à points de contrôle).
		void GlyphSpline(const GlyphCtx& c)
		{
			c.dl->PathLineTo(c.P(0.15f, 0.85f));
			c.dl->PathBezierCubicCurveTo(c.P(0.55f, 0.75f), c.P(0.35f, 0.25f), c.P(0.85f, 0.15f));
			c.dl->PathStroke(c.col, 0, c.th * 1.3f);
			c.dl->AddCircleFilled(c.P(0.15f, 0.85f), c.w * 0.07f, c.col);
			c.dl->AddCircleFilled(c.P(0.50f, 0.50f), c.w * 0.07f, c.col);
			c.dl->AddCircleFilled(c.P(0.85f, 0.15f), c.w * 0.07f, c.col);
		}

		/// Zone de gameplay : pentagone ouvert (polygone tracé) + drapeau.
		void GlyphGameplayZone(const GlyphCtx& c)
		{
			c.dl->PathLineTo(c.P(0.20f, 0.80f));
			c.dl->PathLineTo(c.P(0.15f, 0.40f));
			c.dl->PathLineTo(c.P(0.50f, 0.18f));
			c.dl->PathLineTo(c.P(0.85f, 0.42f));
			c.dl->PathLineTo(c.P(0.75f, 0.80f));
			c.dl->PathStroke(c.col, ImDrawFlags_Closed, c.th);
			c.dl->AddLine(c.P(0.50f, 0.62f), c.P(0.50f, 0.36f), c.col, c.th);
			c.dl->AddTriangleFilled(c.P(0.50f, 0.36f), c.P(0.66f, 0.42f), c.P(0.50f, 0.48f), c.col);
		}

		/// Danger : triangle d'avertissement + point d'exclamation.
		void GlyphHazard(const GlyphCtx& c)
		{
			c.dl->AddTriangle(c.P(0.50f, 0.12f), c.P(0.10f, 0.85f), c.P(0.90f, 0.85f), c.col, c.th);
			c.dl->AddLine(c.P(0.50f, 0.36f), c.P(0.50f, 0.62f), c.col, c.th * 1.2f);
			c.dl->AddCircleFilled(c.P(0.50f, 0.74f), c.w * 0.05f, c.col);
		}
	}

	void DrawToolGlyph(ImDrawList* drawList, ActiveTool tool,
		float minX, float minY, float maxX, float maxY,
		unsigned int colorAbgr)
	{
		if (drawList == nullptr) return;

		// Marge intérieure de 12 % pour respirer dans la case.
		const float w = maxX - minX;
		const float h = maxY - minY;
		GlyphCtx c;
		c.dl = drawList;
		c.x0 = minX + w * 0.12f;
		c.y0 = minY + h * 0.12f;
		c.w  = w * 0.76f;
		c.h  = h * 0.76f;
		c.col = colorAbgr;
		c.th  = (c.w < 24.0f) ? 1.4f : 2.0f;

		switch (tool)
		{
			case ActiveTool::TerrainSculpt:      GlyphSculpt(c); break;
			case ActiveTool::TerrainStamp:       GlyphStamp(c); break;
			case ActiveTool::SplatPaint:         GlyphSplat(c); break;
			case ActiveTool::Lake:               GlyphLake(c); break;
			case ActiveTool::River:              GlyphRiver(c); break;
			case ActiveTool::RiverNetwork:       GlyphRiverNetwork(c); break;
			case ActiveTool::Coastline:          GlyphCoastline(c); break;
			case ActiveTool::MountainRange:      GlyphMountainRange(c); break;
			case ActiveTool::ValleyChain:        GlyphValleyChain(c); break;
			case ActiveTool::HydraulicErosion:   GlyphHydraulicErosion(c); break;
			case ActiveTool::ThermalWindErosion: GlyphThermalWind(c); break;
			case ActiveTool::Cave:               GlyphCave(c); break;
			case ActiveTool::Overhang:           GlyphOverhang(c); break;
			case ActiveTool::Arch:               GlyphArch(c); break;
			case ActiveTool::DungeonPortal:      GlyphDungeonPortal(c); break;
			// Roadmap-8 (audit 2026-06-05, 1.1) — outils M100.16/28/29 câblés.
			case ActiveTool::Spline:             GlyphSpline(c); break;
			case ActiveTool::GameplayZone:       GlyphGameplayZone(c); break;
			case ActiveTool::Hazard:             GlyphHazard(c); break;
			case ActiveTool::None:               break; // pas de glyphe
		}
	}
#else
	void DrawToolGlyph(ImDrawList*, ActiveTool, float, float, float, float, unsigned int) {}
#endif
}
