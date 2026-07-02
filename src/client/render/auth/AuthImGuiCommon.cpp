// AUTH-UI.0 — Helpers ImGui réutilisés par tous les écrans d'authentification

// Fournit les primitives communes : bannières, toggle, boutons (primaire, fantôme, danger, texte), drapeaux vectoriels et indicateurs de raccourcis clavier.
#include "src/client/render/auth/AuthImGuiCommon.h"

#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"
#	include "src/client/render/LnThemeImGui.h"

namespace engine::render
{
	namespace
	{
		using LnTheme::ToImVec4;
	}

	/// Dessine une bannière colorée avec un titre accentué et un message de corps enroulé.
	void DrawAuthBanner(std::string_view title, std::string_view message, float r, float g, float b)
	{
		ImVec4 bg(r, g, b, 0.12f);
		ImVec4 bd(r, g, b, 1.f);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
		ImGui::PushStyleColor(ImGuiCol_Border, bd);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
		std::string bannerId = "##auth_banner_";
		bannerId.append(title.data(), title.size());
		// AutoResizeY : la bannière s'adapte à la hauteur réelle du texte (1 à 3 lignes typiquement)
		// au lieu de remplir toute la hauteur disponible — ce qui poussait les champs hors écran.
		ImGui::BeginChild(bannerId.c_str(), ImVec2(-FLT_MIN, 0.f),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
			ImGuiWindowFlags_NoScrollbar);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(r, g, b, 1.f));
		if (!title.empty())
		{
			ImGui::TextUnformatted(title.data(), title.data() + static_cast<int>(title.size()));
		}
		ImGui::PopStyleColor();
		if (!message.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
			ImGui::TextWrapped("%.*s", static_cast<int>(message.size()), message.data());
			ImGui::PopStyleColor();
		}
		ImGui::EndChild();
		ImGui::Spacing();
	}

	/// Affiche un toggle on/off avec libellé et texte d'aide optionnel ; met à jour *checked si cliqué.
	bool DrawAuthToggle(std::string_view label, bool* checked, std::string_view hint)
	{
		if (checked == nullptr)
		{
			return false;
		}
		const float trackW = 46.f;
		const float trackH = 22.f;
		const float pad = 3.f;
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		std::string id = "##auth_toggle_";
		id.append(label.data(), label.size());
		ImGui::InvisibleButton(id.c_str(), ImVec2(trackW + 4.f, trackH + 6.f));
		if (ImGui::IsItemClicked())
		{
			*checked = !*checked;
		}
		const bool on = *checked;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 a(p0.x + 2.f, p0.y + 3.f);
		const ImVec2 b(a.x + trackW, a.y + trackH);
		const ImU32 fillOff = IM_COL32(40, 48, 62, 255);
		const ImU32 fillOn = IM_COL32(72, 90, 48, 255);
		dl->AddRectFilled(a, b, on ? fillOn : fillOff, trackH * 0.5f);
		dl->AddRect(a, b, ImGui::ColorConvertFloat4ToU32(ToImVec4(LnTheme::kBorder)), trackH * 0.5f, 0, 1.f);
		const float thumbR = (trackH - pad * 2.f) * 0.5f;
		const float cx = on ? (b.x - pad - thumbR) : (a.x + pad + thumbR);
		const float cy = (a.y + b.y) * 0.5f;
		dl->AddCircleFilled(ImVec2(cx, cy), thumbR, ImGui::ColorConvertFloat4ToU32(ToImVec4(LnTheme::kAccent)));

		ImGui::SameLine(0.f, 12.f);
		ImGui::BeginGroup();
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
		ImGui::TextUnformatted(label.data(), label.data() + static_cast<int>(label.size()));
		ImGui::PopStyleColor();
		if (!hint.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.82f);
			ImGui::TextWrapped("%.*s", static_cast<int>(hint.size()), hint.data());
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
		}
		ImGui::EndGroup();
		return false;
	}

	/// Dessine un bouton de style texte atténué (lien secondaire) avec une zone de clic plus
	/// confortable que SmallButton : la hauteur native de SmallButton coupait visuellement les
	/// jambages des libellés en français (« Récupération », « créer », « bureau »…). On utilise
	/// donc Button avec un FramePadding vertical élargi et un fond/bordure transparents pour
	/// conserver le style « texte ».
	bool DrawAuthButtonText(std::string_view label, std::string_view idSuffix)
	{
		std::string id;
		id.reserve(label.size() + idSuffix.size() + 4u);
		id.append(label.data(), label.size());
		id.append(idSuffix.data(), idSuffix.size());
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(LnTheme::AccentDim(0.10f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(LnTheme::AccentDim(0.18f)));
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 8.f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
		const bool pressed = ImGui::Button(id.c_str());
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(4);
		return pressed;
	}

	/// Dessine le bouton d'action principal (couleur primaire) avec état désactivé optionnel.
	bool DrawAuthButtonPrimary(std::string_view label, std::string_view idSuffix, bool disabled)
	{
		if (disabled)
		{
			ImGui::BeginDisabled();
		}
		ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(LnTheme::kPrimary));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.39f, 0.58f, 0.82f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.38f, 0.62f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		std::string id;
		id.append(label.data(), label.size());
		id.append(idSuffix.data(), idSuffix.size());
		const bool pressed = ImGui::Button(id.c_str(), ImVec2(220.f, 34.f));
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(4);
		if (disabled)
		{
			ImGui::EndDisabled();
		}
		return pressed;
	}

	/// Dessine un bouton fantôme (fond surface, bordure texte) occupant toute la largeur disponible.
	bool DrawAuthButtonGhost(std::string_view label, std::string_view idSuffix)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(LnTheme::AccentDim(0.1f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(LnTheme::AccentDim(0.16f)));
		ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kText));
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		std::string id;
		id.append(label.data(), label.size());
		id.append(idSuffix.data(), idSuffix.size());
		const bool pressed = ImGui::Button(id.c_str(), ImVec2(-FLT_MIN, 34.f));
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(5);
		return pressed;
	}

	/// Affiche une ligne de trois raccourcis clavier formatés [touche] description en couleur atténuée.
	void DrawAuthKeycapRow(std::string_view leftKey, std::string_view leftDesc, std::string_view midKey, std::string_view midDesc,
		std::string_view rightKey, std::string_view rightDesc)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
		ImGui::Text("[%.*s] %.*s", static_cast<int>(leftKey.size()), leftKey.data(), static_cast<int>(leftDesc.size()), leftDesc.data());
		ImGui::SameLine(0.f, 14.f);
		ImGui::Text("[%.*s] %.*s", static_cast<int>(midKey.size()), midKey.data(), static_cast<int>(midDesc.size()), midDesc.data());
		ImGui::SameLine(0.f, 14.f);
		ImGui::Text("[%.*s] %.*s", static_cast<int>(rightKey.size()), rightKey.data(), static_cast<int>(rightDesc.size()), rightDesc.data());
		ImGui::PopStyleColor();
	}

	/// Dessine une ligne action / touche sur deux colonnes avec séparateur, utilisée dans les panneaux de réglages.
	void DrawAuthKeybind(std::string_view actionName, std::string_view keyLabel)
	{
		ImGui::PushID(static_cast<int>(reinterpret_cast<std::uintptr_t>(actionName.data()) ^ (actionName.size() << 3)));
		const float avail = ImGui::GetContentRegionAvail().x;
		ImGui::Columns(2, "##auth_keybind", false);
		ImGui::SetColumnWidth(0, (std::max)(120.f, avail * 0.55f));
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
		ImGui::TextUnformatted(actionName.data(), actionName.data() + static_cast<int>(actionName.size()));
		ImGui::PopStyleColor();
		ImGui::NextColumn();
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
		ImGui::TextUnformatted(keyLabel.data(), keyLabel.data() + static_cast<int>(keyLabel.size()));
		ImGui::PopStyleColor();
		ImGui::Columns(1);
		ImGui::PushStyleColor(ImGuiCol_Separator, ToImVec4(LnTheme::kBorder));
		ImGui::Separator();
		ImGui::PopStyleColor();
		ImGui::PopID();
	}

	/// Dessine un bouton d'action destructrice (fond rouge erreur) pleine largeur.
	bool DrawAuthButtonDanger(std::string_view label, std::string_view idSuffix)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(LnTheme::kErrorCol));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(LnTheme::AccentDim(0.18f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(LnTheme::kErrorCol));
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		std::string id;
		id.append(label.data(), label.size());
		id.append(idSuffix.data(), idSuffix.size());
		const bool pressed = ImGui::Button(id.c_str(), ImVec2(-FLT_MIN, 32.f));
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(4);
		return pressed;
	}

	/// Dessine le drapeau tricolore français (bleu-blanc-rouge) en trois bandes verticales via ImDrawList.
	void DrawFlagFR(ImDrawList* dl, float posX, float posY, float sizeW, float sizeH)
	{
		if (dl == nullptr)
		{
			return;
		}
		const ImVec2 pos(posX, posY);
		const ImVec2 size(sizeW, sizeH);
		const float w3 = size.x / 3.f;
		dl->AddRectFilled(pos, ImVec2(pos.x + w3, pos.y + size.y), IM_COL32(0, 38, 84, 255));
		dl->AddRectFilled(ImVec2(pos.x + w3, pos.y), ImVec2(pos.x + 2.f * w3, pos.y + size.y), IM_COL32(255, 255, 255, 255));
		dl->AddRectFilled(ImVec2(pos.x + 2.f * w3, pos.y), ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(237, 41, 57, 255));
	}

	/// Dessine le drapeau britannique (Union Jack) simplifié avec croix de Saint-Georges et diagonales via ImDrawList.
	void DrawFlagEN(ImDrawList* dl, float posX, float posY, float sizeW, float sizeH)
	{
		if (dl == nullptr)
		{
			return;
		}
		const ImVec2 p0(posX, posY);
		const ImVec2 p1(posX + sizeW, posY + sizeH);
		const ImU32 navy = IM_COL32(1, 33, 105, 255);
		const ImU32 white = IM_COL32(255, 255, 255, 255);
		const ImU32 red = IM_COL32(207, 20, 43, 255);
		dl->AddRectFilled(p0, p1, navy);
		const float cx = (p0.x + p1.x) * 0.5f;
		const float cy = (p0.y + p1.y) * 0.5f;
		const float h = p1.y - p0.y;
		const float w = p1.x - p0.x;
		const float tW = h * 0.10f;
		const float tU = w * 0.10f;
		dl->AddRectFilled(ImVec2(p0.x, cy - tW), ImVec2(p1.x, cy + tW), white);
		dl->AddRectFilled(ImVec2(cx - tU, p0.y), ImVec2(cx + tU, p1.y), white);
		dl->AddRectFilled(ImVec2(p0.x, cy - tW * 0.45f), ImVec2(p1.x, cy + tW * 0.45f), red);
		dl->AddRectFilled(ImVec2(cx - tU * 0.45f, p0.y), ImVec2(cx + tU * 0.45f, p1.y), red);
		const float diag = std::min(w, h) * 0.08f;
		const ImU32 redDim = IM_COL32(180, 0, 0, 255);
		dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y), white, diag);
		dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p0.y), white, diag);
		dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y), redDim, diag * 0.45f);
		dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p0.y), redDim, diag * 0.45f);
	}
} // namespace engine::render

#endif
