#include "src/client/render/LnWidgets.h"

#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <cstdio>

#if defined(_WIN32)
#	include "imgui.h"
#	include "src/client/render/LnThemeImGui.h"

namespace LnWidgets
{
	namespace
	{
		using LnTheme::ToImVec4;
		using LnTheme::ToU32;
	}

	void BeginFullscreenOverlay(float vpW, float vpH, float windowBgAlpha)
	{
		ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
		ImGui::SetNextWindowSize(ImVec2(vpW, vpH));
		ImGui::SetNextWindowBgAlpha(1.f);
		// Fond plein écran FIGÉ sur le thème par défaut (or_royal), invariant au
		// thème actif : changer de thème de race ne doit recolorer que les
		// panneaux, pas le fond (demande utilisateur). Cf. LnTheme::AuthBackdrop.
		ImVec4 bg = ToImVec4(LnTheme::AuthBackdrop());
		bg.w = windowBgAlpha;
		ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
		ImGui::Begin("##ln_auth_overlay",
			nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus
				| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(1);
	}

	void BigTitle(std::string_view line1, std::string_view line2, float vpW, float vpH, const char* stageId)
	{
		// Pattern de reference cale sur l'ecran Login : BeginChild stage 96 % vpW pour
		// permettre au titre 5.0x (720 px) de tenir, h1 centre scale 5.0x, Dummy 28 px
		// pour passer sous la jambe oblique du R de " CHRONIQUES ", h2 centre scale 2.5x.
		// Le caller doit appeler ImGui::EndChild() apres EndPanel.
		const float titleZoneW = vpW * 0.96f;
		ImGui::SetCursorPosX((vpW - titleZoneW) * 0.5f);
		char childId[64];
		std::snprintf(childId, sizeof(childId), "##ln_%s_stage", stageId);
		ImGui::BeginChild(childId, ImVec2(titleZoneW, 0.f), false, ImGuiWindowFlags_NoScrollbar);

		const float topMargin = (std::max)(24.f, vpH * 0.05f);
		ImGui::SetCursorPosY(topMargin);
		ImGui::SetWindowFontScale(5.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
		const float w1 = ImGui::CalcTextSize(line1.data(), line1.data() + line1.size()).x;
		ImGui::SetCursorPosX((std::max)(0.f, (titleZoneW - w1) * 0.5f));
		ImGui::TextUnformatted(line1.data(), line1.data() + static_cast<int>(line1.size()));
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();

		if (!line2.empty())
		{
			ImGui::Dummy(ImVec2(0.f, 28.f));
			ImGui::SetWindowFontScale(2.5f);
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
			const float w2 = ImGui::CalcTextSize(line2.data(), line2.data() + line2.size()).x;
			ImGui::SetCursorPosX((std::max)(0.f, (titleZoneW - w2) * 0.5f));
			ImGui::TextUnformatted(line2.data(), line2.data() + static_cast<int>(line2.size()));
			ImGui::PopStyleColor();
			ImGui::SetWindowFontScale(1.f);
		}
	}

	bool BeginPanel(float width, float vpW, float vpH, std::string_view title,
		std::string_view subtitle, std::string_view versionLabel, bool versionLeadingInfoGlyph, bool subtitleWelcomeAccent,
		float fixedHeight)
	{
		const float panelX = (vpW - width) * 0.5f;
		// Quand fixedHeight > 0 (panel a hauteur fixe connue), on centre verticalement
		// la fenetre dans la viewport. Sinon (auto-resize), on garde l'ancien
		// comportement (panelY = vpH * 0.28) qui laisse de la place pour le titre
		// 'LES CHRONIQUES' au-dessus.
		const float panelY = (fixedHeight > 0.f)
			? (std::max)(0.f, (vpH - fixedHeight) * 0.5f)
			: (vpH * 0.28f);
		ImGui::SetCursorPos(ImVec2(panelX, panelY));

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ToImVec4(LnTheme::PanelBg()));
		ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.f, 18.f));
		// ItemSpacing.y bumpe de 4 (defaut ImGui) a 8 px pour aerer les lignes de texte
		// et de widgets dans tous les panneaux auth (reference visuelle Login).

		// Si fixedHeight > 0 : le panneau a une hauteur figee. Sinon AutoResizeY : la hauteur s'aligne
		// sur le contenu reel - evite les enormes panneaux vides qui poussaient les champs et boutons
		// hors de l'ecran (ex. login apres banniere info, choix de langue).
		const ImVec2 panelSize(width, fixedHeight > 0.f ? fixedHeight : 0.f);
		const ImGuiChildFlags childFlags = (fixedHeight > 0.f)
			? ImGuiChildFlags_Borders
			: (ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		const bool open = ImGui::BeginChild("##ln_panel", panelSize, childFlags,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(2);

		// ItemSpacing.y bumpe a 8 px (defaut 4) pour aerer les lignes de texte et de
		// widgets dans tous les panneaux auth - pope dans EndPanel. Pushe meme si
		// !open : EndPanel doit etre appele dans tous les cas (cf. callers), et le
		// push/pop doit donc rester equilibre.
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 8.f));

		if (!open)
		{
			return false;
		}
		if (!title.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
			ImGui::SetWindowFontScale(1.15f);
			ImGui::TextUnformatted(title.data(), title.data() + static_cast<int>(title.size()));
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
		}
		if (!versionLabel.empty())
		{
			const float vw = ImGui::CalcTextSize(versionLabel.data(), versionLabel.data() + versionLabel.size()).x;
			const float badge = versionLeadingInfoGlyph ? (ImGui::GetFontSize() + 6.f) : 0.f;
			const float gap = 4.f;
			const float slack = ImGui::GetContentRegionAvail().x - vw - badge - gap;
			ImGui::SameLine(0.f, (slack > 0.f) ? slack : 4.f);
			if (versionLeadingInfoGlyph)
			{
				const ImVec2 ip = ImGui::GetCursorScreenPos();
				const float side = ImGui::GetFontSize() * 0.92f;
				const float r = side * 0.42f;
				const ImVec2 center(ip.x + side * 0.5f, ip.y + side * 0.5f);
				ImDrawList* dl = ImGui::GetWindowDrawList();
				dl->AddCircle(center, r, ToU32(LnTheme::kMuted), 0, 1.25f);
				dl->AddText(ImVec2(center.x - ImGui::CalcTextSize("i").x * 0.5f, ip.y + 1.f), ToU32(LnTheme::kMuted), "i");
				ImGui::Dummy(ImVec2(side, side));
				ImGui::SameLine(0.f, gap);
			}
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
			ImGui::TextUnformatted(versionLabel.data(), versionLabel.data() + static_cast<int>(versionLabel.size()));
			ImGui::PopStyleColor();
		}
		if (!subtitle.empty())
		{
			// Petit espace vertical entre le title du panel et son subtitle (welcome) - sans
			// ce Dummy, ItemSpacing.y (4 px) seul collait visuellement les deux lignes.
			ImGui::Dummy(ImVec2(0.f, 6.f));
			if (subtitleWelcomeAccent)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.92f);
				ImGui::SetWindowFontScale(0.95f);
				ImGui::TextWrapped("%.*s", static_cast<int>(subtitle.size()), subtitle.data());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleVar(1);
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
				ImGui::TextWrapped("%.*s", static_cast<int>(subtitle.size()), subtitle.data());
				ImGui::PopStyleColor();
			}
		}
		ImGui::PushStyleColor(ImGuiCol_Separator, ToImVec4(LnTheme::kBorder));
		ImGui::Separator();
		ImGui::PopStyleColor();
		// Anciennement : ImGui::Spacing() (= Dummy(0, 8)) apres le Separator. Total
		// title>content etait  17 px, le retour utilisateur demande ~10 px avec le trait
		// au milieu. ItemSpacing.y (4) est applique automatiquement avant ET apres le
		// Separator par ImGui : 4 + 1 (sep) + 4 = ~9 px > cible quasi atteinte sans
		// supplement. On laisse donc ce bloc nu.
		return true;
	}

	void EndPanel()
	{
		// Pop ItemSpacing pushe dans BeginPanel (uniquement si le panel a ete ouvert
		// avec succes - sinon BeginPanel a return false avant le push).
		ImGui::PopStyleVar(1);
		ImGui::EndChild();
	}

	void FooterHints(std::string_view left, std::string_view right)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
		ImGui::SetWindowFontScale(0.88f);
		if (!left.empty() && !right.empty())
		{
			ImGui::TextUnformatted(left.data(), left.data() + static_cast<int>(left.size()));
			const float rw = ImGui::CalcTextSize(right.data(), right.data() + right.size()).x;
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - rw - ImGui::GetScrollX());
			ImGui::TextUnformatted(right.data(), right.data() + static_cast<int>(right.size()));
		}
		else if (!left.empty())
		{
			ImGui::TextUnformatted(left.data(), left.data() + static_cast<int>(left.size()));
		}
		else if (!right.empty())
		{
			ImGui::TextUnformatted(right.data(), right.data() + static_cast<int>(right.size()));
		}
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
	}

	void FooterChipRow(const std::vector<std::pair<std::string, std::string>>& chips)
	{
		for (size_t ci = 0; ci < chips.size(); ++ci)
		{
			if (ci > 0u)
			{
				ImGui::SameLine(0.f, 8.f);
			}
			const auto& chip = chips[ci];
			const float keyW = ImGui::CalcTextSize(chip.first.c_str()).x + 16.f;
			const float descW = ImGui::CalcTextSize(chip.second.c_str()).x + 12.f;
			const float chipW = keyW + descW + 8.f;
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ToImVec4(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.f);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
			char cid[48];
			std::snprintf(cid, sizeof(cid), "##fchip_%zu", ci);
			ImGui::BeginChild(cid, ImVec2(chipW, 40.f), true, ImGuiWindowFlags_NoScrollbar);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
			ImGui::SetWindowFontScale(0.92f);
			ImGui::TextUnformatted(chip.first.c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine(0.f, 6.f);
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.78f);
			ImGui::TextUnformatted(chip.second.c_str());
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
			ImGui::EndChild();
		}
	}

	void Field(std::string_view label, char* buf, int bufSz, bool password)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
		ImGui::TextUnformatted(label.data(), label.data() + static_cast<int>(label.size()));
		ImGui::PopStyleColor();

		ImGui::PushStyleColor(ImGuiCol_FrameBg, ToImVec4(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ToImVec4(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ToImVec4(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);

		char inputId[48];
		std::snprintf(inputId, sizeof(inputId), "##f_%p", static_cast<void*>(buf));
		ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
		if (password)
		{
			flags |= ImGuiInputTextFlags_Password;
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputText(inputId, buf, static_cast<size_t>(bufSz), flags);

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(4);
		ImGui::Spacing();
	}

	void Banner(std::string_view title, std::string_view msg, float r, float g, float b)
	{
		ImVec4 bg(r, g, b, 0.12f);
		ImVec4 bd(r, g, b, 1.f);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
		ImGui::PushStyleColor(ImGuiCol_Border, bd);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
		std::string bannerId = "##banner_";
		bannerId.append(title.data(), title.size());
		// AutoResizeY : la banniere s'adapte a la hauteur reelle du texte au lieu de remplir tout le panneau.
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
		if (!msg.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
			ImGui::TextWrapped("%.*s", static_cast<int>(msg.size()), msg.data());
			ImGui::PopStyleColor();
		}
		ImGui::EndChild();
		ImGui::Spacing();
	}

	void KeycapHints(std::initializer_list<std::pair<const char*, const char*>> hints)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
		bool first = true;
		for (const auto& kv : hints)
		{
			if (!first)
			{
				ImGui::SameLine(0.f, 14.f);
			}
			ImGui::Text("[%s] %s", kv.first, kv.second);
			first = false;
		}
		ImGui::PopStyleColor();
	}

	bool PrimaryButton(std::string_view label, bool disabled, float width)
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
		char id[160];
		std::snprintf(id, sizeof(id), "%.*s##primary", static_cast<int>(label.size()), label.data());
		const bool clicked = ImGui::Button(id, ImVec2(width, 32.f));
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(4);
		if (disabled)
		{
			ImGui::EndDisabled();
		}
		return clicked;
	}

	bool GhostButton(std::string_view label, bool disabled, float width)
	{
		if (disabled)
		{
			ImGui::BeginDisabled();
		}
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(LnTheme::AccentDim(0.08f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(LnTheme::AccentDim(0.15f)));
		ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
		char id[160];
		std::snprintf(id, sizeof(id), "%.*s##ghost", static_cast<int>(label.size()), label.data());
		const bool clicked = ImGui::Button(id, ImVec2(width, 32.f));
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(5);
		if (disabled)
		{
			ImGui::EndDisabled();
		}
		return clicked;
	}

	void Separator()
	{
		ImGui::PushStyleColor(ImGuiCol_Separator, ToImVec4(LnTheme::kBorder));
		ImGui::Separator();
		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	void Breadcrumb(std::initializer_list<const char*> steps, int current)
	{
		int i = 0;
		for (const char* s : steps)
		{
			const bool done = i < current;
			const bool active = i == current;
			const ImVec4 col = done ? ToImVec4(LnTheme::kSuccess) : (active ? ToImVec4(LnTheme::kAccent) : ToImVec4(LnTheme::kMuted));
			ImGui::PushStyleColor(ImGuiCol_Text, col);
			ImGui::Text("%02d %s", i + 1, s);
			ImGui::PopStyleColor();
			++i;
			const int total = static_cast<int>(steps.size());
			if (i < total)
			{
				ImGui::SameLine(0.f, 12.f);
				ImGui::TextUnformatted(">");
				ImGui::SameLine(0.f, 12.f);
			}
		}
		ImGui::Spacing();
	}

	void Breadcrumb(const std::vector<std::string>& steps, int current)
	{
		const int total = static_cast<int>(steps.size());
		for (int i = 0; i < total; ++i)
		{
			const bool done = i < current;
			const bool active = i == current;
			const ImVec4 col = done ? ToImVec4(LnTheme::kSuccess) : (active ? ToImVec4(LnTheme::kAccent) : ToImVec4(LnTheme::kMuted));
			ImGui::PushStyleColor(ImGuiCol_Text, col);
			ImGui::Text("%02d %s", i + 1, steps[static_cast<size_t>(i)].c_str());
			ImGui::PopStyleColor();
			if (i + 1 < total)
			{
				ImGui::SameLine(0.f, 12.f);
				ImGui::TextUnformatted(">");
				ImGui::SameLine(0.f, 12.f);
			}
		}
		ImGui::Spacing();
	}
} // namespace LnWidgets

#endif
