// EditorToolbar — barre d'actions (réorganisation UI 2026-07-17).
// Remplace la rangée d'outils M100.35 : les outils vivent désormais dans la
// palette latérale (ToolPalettePanel) ; la barre du haut ne porte plus que
// les actions générales du registre (save / undo / redo / validate / export).

#include "src/world_editor/ui/EditorToolbar.h"

#include "src/world_editor/ui/ToolbarIconAtlas.h"

#include <algorithm>

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world
{
	EditorToolbar::EditorToolbar(WorldEditorShell& shell)
		: m_shell(shell)
	{
		// Ordre d'affichage. Groupes visuels : Sauvegarder | Annuler/Rétablir
		// | Valider/Exporter (indices 1 et 3 démarrent un nouveau groupe).
		m_orderedActionIds = {
			"file.save",
			"edit.undo",
			"edit.redo",
			"zone.validate",
			"zone.export",
		};
		m_groupStartIndices = { 1u, 3u };
	}

	bool EditorToolbar::IsGroupStart(size_t idx) const
	{
		return std::find(m_groupStartIndices.begin(), m_groupStartIndices.end(), idx)
			!= m_groupStartIndices.end();
	}

	ToolbarLayout EditorToolbar::BuildLayout(float viewportWidthPx,
		float menuBarHeightPx) const
	{
		ToolbarLayout layout;
		layout.toolbarY      = menuBarHeightPx;
		layout.toolbarHeight = kToolbarHeightPx;
		layout.toolbarWidth  = viewportWidthPx;
		layout.buttons.reserve(m_orderedActionIds.size());

		const actions::EditorActionRegistry& reg = m_shell.GetActionRegistry();

		float cursorX = kToolbarPaddingPx;
		const float cursorY = layout.toolbarY + kToolbarPaddingPx;
		for (size_t i = 0; i < m_orderedActionIds.size(); ++i)
		{
			// Un id absent du registre (ex. actions session non enregistrées
			// en mode shell in-game) ne produit pas de bouton.
			if (reg.Find(m_orderedActionIds[i]) == nullptr) continue;

			if (IsGroupStart(i) && !layout.buttons.empty())
			{
				cursorX += kGroupSpacingPx;
			}

			ToolbarButtonRect r;
			r.x = cursorX;
			r.y = cursorY;
			r.width = kButtonSizePx;
			r.height = kButtonSizePx;
			r.actionId = m_orderedActionIds[i];
			layout.buttons.push_back(std::move(r));
			cursorX += kButtonSizePx + kButtonSpacingPx;
		}

		return layout;
	}

	bool EditorToolbar::HitTest(const ToolbarLayout& layout,
		float mouseX, float mouseY, size_t& outButtonIndex)
	{
		for (size_t i = 0; i < layout.buttons.size(); ++i)
		{
			const ToolbarButtonRect& r = layout.buttons[i];
			if (mouseX >= r.x && mouseX < r.x + r.width &&
				mouseY >= r.y && mouseY < r.y + r.height)
			{
				outButtonIndex = i;
				return true;
			}
		}
		return false;
	}

	void EditorToolbar::HandleClick(const ToolbarLayout& layout,
		size_t buttonIndex)
	{
		if (buttonIndex >= layout.buttons.size()) return;
		const actions::EditorAction* a =
			m_shell.GetActionRegistry().Find(layout.buttons[buttonIndex].actionId);
		if (a == nullptr) return;
		if (!actions::EditorActionRegistry::IsEnabled(*a)) return;
		if (a->execute) { a->execute(); }
	}

#if defined(_WIN32)
	namespace
	{
		/// Polish UI 2026-07-17 — Dessine le glyphe vectoriel d'une action de
		/// la barre (disquette, flèches undo/redo, coche, export) directement
		/// via ImDrawList : pas d'assets PNG à pipeliner, rendu net à toute
		/// échelle. \return false si l'id n'a pas de glyphe mappé — l'appelant
		/// retombe alors sur la lettre placeholder de l'atlas.
		bool DrawActionGlyph(ImDrawList* dl, const std::string& actionId,
			const ImVec2& btnMin, const ImVec2& btnMax, ImU32 col)
		{
			const float inset = 11.0f;
			const ImVec2 a{ btnMin.x + inset, btnMin.y + inset };
			const ImVec2 b{ btnMax.x - inset, btnMax.y - inset };
			const float w = b.x - a.x;
			const float h = b.y - a.y;
			const float th = 2.0f;

			if (actionId == "file.save")
			{
				// Disquette : contour arrondi + volet haut + étiquette basse.
				dl->AddRect(a, b, col, 2.0f, 0, th);
				dl->AddRectFilled(ImVec2(a.x + w * 0.30f, a.y + th),
					ImVec2(a.x + w * 0.70f, a.y + h * 0.35f), col);
				dl->AddRectFilled(ImVec2(a.x + w * 0.22f, a.y + h * 0.58f),
					ImVec2(a.x + w * 0.78f, b.y - th), col);
				return true;
			}
			if (actionId == "edit.undo" || actionId == "edit.redo")
			{
				// Flèche à queue courbée ; redo = miroir horizontal de undo.
				const bool mirror = (actionId == "edit.redo");
				auto mx = [&](float x) { return mirror ? (a.x + b.x - x) : x; };
				const float tipY = a.y + h * 0.32f;
				const ImVec2 tip{ mx(a.x), tipY };
				dl->AddTriangleFilled(tip,
					ImVec2(mx(a.x + w * 0.34f), tipY - h * 0.30f),
					ImVec2(mx(a.x + w * 0.34f), tipY + h * 0.30f), col);
				dl->PathLineTo(ImVec2(mx(a.x + w * 0.30f), tipY));
				dl->PathBezierCubicCurveTo(
					ImVec2(mx(b.x), tipY),
					ImVec2(mx(b.x), b.y),
					ImVec2(mx(a.x + w * 0.30f), b.y));
				dl->PathStroke(col, 0, th);
				return true;
			}
			if (actionId == "zone.validate")
			{
				// Coche.
				const ImVec2 pts[3] = {
					ImVec2(a.x, a.y + h * 0.55f),
					ImVec2(a.x + w * 0.38f, b.y),
					ImVec2(b.x, a.y + h * 0.08f),
				};
				dl->AddPolyline(pts, 3, col, 0, 2.5f);
				return true;
			}
			if (actionId == "zone.export")
			{
				// Plateau ouvert + flèche montante (export vers le runtime).
				const ImVec2 tray[4] = {
					ImVec2(a.x, a.y + h * 0.55f),
					ImVec2(a.x, b.y),
					ImVec2(b.x, b.y),
					ImVec2(b.x, a.y + h * 0.55f),
				};
				dl->AddPolyline(tray, 4, col, 0, th);
				const float cx = (a.x + b.x) * 0.5f;
				dl->AddLine(ImVec2(cx, a.y + h * 0.18f), ImVec2(cx, a.y + h * 0.72f), col, th);
				dl->AddTriangleFilled(
					ImVec2(cx, a.y - 1.0f),
					ImVec2(cx - w * 0.24f, a.y + h * 0.30f),
					ImVec2(cx + w * 0.24f, a.y + h * 0.30f), col);
				return true;
			}
			return false;
		}
	}

	void EditorToolbar::Render()
	{
		// Construction de la fenêtre : ancrée sous le menu bar, largeur =
		// viewport principal, hauteur fixe (mêmes flags que la toolbar
		// M100.35 — la barre ne couvre jamais la zone 3D).
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		const ImVec2 toolbarPos{ vp->WorkPos.x, vp->WorkPos.y };
		const ImVec2 toolbarSize{ vp->WorkSize.x, kToolbarHeightPx };

		ImGui::SetNextWindowPos(toolbarPos);
		ImGui::SetNextWindowSize(toolbarSize);

		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoBringToFrontOnFocus;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
			ImVec2(kToolbarPaddingPx, kToolbarPaddingPx));
		if (!ImGui::Begin("##EditorToolbar", nullptr, flags))
		{
			ImGui::End();
			ImGui::PopStyleVar(3);
			return;
		}

		const actions::EditorActionRegistry& reg = m_shell.GetActionRegistry();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 windowOrigin = ImGui::GetCursorScreenPos();

		float cursorX = 0.0f;
		bool anyDrawn = false;
		for (size_t i = 0; i < m_orderedActionIds.size(); ++i)
		{
			const actions::EditorAction* a = reg.Find(m_orderedActionIds[i]);
			if (a == nullptr) continue;

			if (IsGroupStart(i) && anyDrawn)
			{
				cursorX += kGroupSpacingPx;
			}
			anyDrawn = true;

			const bool enabled = actions::EditorActionRegistry::IsEnabled(*a);
			const ToolIconStyle style = ToolbarIconAtlas::GetForAction(a->id);

			ImGui::PushID(static_cast<int>(i));
			ImGui::SetCursorPosX(cursorX);
			ImGui::SetCursorPosY(0.0f);

			const ImVec2 buttonScreenMin{
				windowOrigin.x + cursorX,
				windowOrigin.y };
			const ImVec2 buttonScreenMax{
				buttonScreenMin.x + kButtonSizePx,
				buttonScreenMin.y + kButtonSizePx };

			// Fond : couleur de l'icône, assombrie si l'action est grisée.
			ImU32 baseColor = static_cast<ImU32>(style.bgColorArgb);
			if (!enabled)
			{
				baseColor = IM_COL32(70, 70, 70, 160);
			}
			dl->AddRectFilled(buttonScreenMin, buttonScreenMax, baseColor, 4.0f);

			// Glyphe vectoriel (polish 2026-07-17) ; lettre placeholder de
			// l'atlas en repli si l'id n'est pas mappé.
			const ImU32 glyphCol = enabled
				? IM_COL32_WHITE : IM_COL32(160, 160, 160, 200);
			if (!DrawActionGlyph(dl, a->id, buttonScreenMin, buttonScreenMax, glyphCol))
			{
				const ImVec2 textSize = ImGui::CalcTextSize(style.letter);
				const ImVec2 textPos{
					buttonScreenMin.x + (kButtonSizePx - textSize.x) * 0.5f,
					buttonScreenMin.y + (kButtonSizePx - textSize.y) * 0.5f };
				dl->AddText(textPos, glyphCol, style.letter);
			}

			dl->AddRect(buttonScreenMin, buttonScreenMax,
				IM_COL32(40, 40, 40, 200), 4.0f, 0, 1.5f);

			// InvisibleButton occupe exactement le rect du bouton.
			ImGui::InvisibleButton("##btn", ImVec2(kButtonSizePx, kButtonSizePx));
			const bool clicked = ImGui::IsItemHovered()
				&& ImGui::IsMouseClicked(ImGuiMouseButton_Left);
			if (clicked && enabled && a->execute)
			{
				a->execute();
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				// Tooltip « libellé (raccourci) » depuis le registre — source
				// unique avec les menus.
				if (a->shortcutText.empty())
				{
					ImGui::SetTooltip("%s", a->label.c_str());
				}
				else
				{
					ImGui::SetTooltip("%s (%s)", a->label.c_str(), a->shortcutText.c_str());
				}
			}

			ImGui::PopID();
			cursorX += kButtonSizePx + kButtonSpacingPx;
			ImGui::SameLine(); // s'aligne pour l'InvisibleButton suivant
		}

		ImGui::End();
		ImGui::PopStyleVar(3);
	}
#endif
}
