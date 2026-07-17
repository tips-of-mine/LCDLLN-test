// EditorTheme — implémentation. Voir EditorTheme.h.

#include "src/world_editor/ui/EditorTheme.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world
{
#if defined(_WIN32)
	namespace
	{
		/// Convertit une couleur 0xRRGGBB + alpha [0..1] en ImVec4.
		ImVec4 Rgb(unsigned int rgb, float a = 1.0f)
		{
			return ImVec4(
				static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
				static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
				static_cast<float>(rgb & 0xFF) / 255.0f,
				a);
		}
	}

	void ApplyWorldEditorTheme()
	{
		ImGuiStyle& style = ImGui::GetStyle();

		// ── Métriques : arrondis discrets, paddings généreux (lisibilité). ──
		style.WindowRounding    = 6.0f;
		style.ChildRounding     = 4.0f;
		style.FrameRounding     = 4.0f;
		style.PopupRounding     = 4.0f;
		style.GrabRounding      = 4.0f;
		style.TabRounding       = 4.0f;
		style.ScrollbarRounding = 8.0f;
		style.WindowPadding     = ImVec2(10.0f, 10.0f);
		style.FramePadding      = ImVec2(9.0f, 5.0f);
		style.CellPadding       = ImVec2(6.0f, 4.0f);
		style.ItemSpacing       = ImVec2(8.0f, 6.0f);
		style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
		style.IndentSpacing     = 20.0f;
		style.ScrollbarSize     = 13.0f;
		style.GrabMinSize       = 12.0f;
		style.WindowBorderSize  = 1.0f;
		style.ChildBorderSize   = 1.0f;
		style.PopupBorderSize   = 1.0f;
		style.FrameBorderSize   = 0.0f;
		style.TabBarBorderSize  = 1.0f;
		style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
		style.SeparatorTextBorderSize = 2.0f;
		style.DockingSeparatorSize    = 2.0f;

		// ── Palette : anthracite + accent doré (identité LCDLLN). ──────────
		const ImVec4 kBgDeep    = Rgb(0x131417);        // fonds les plus sombres
		const ImVec4 kBgWindow  = Rgb(0x1b1d21);        // fond de fenêtre
		const ImVec4 kBgPanel   = Rgb(0x22242a);        // frames / champs
		const ImVec4 kBgHover   = Rgb(0x2d3038);
		const ImVec4 kBgActive  = Rgb(0x3a3e48);
		const ImVec4 kAccent    = Rgb(0xd9a133);        // doré LCDLLN
		const ImVec4 kAccentDim = Rgb(0x8a6a2a);
		const ImVec4 kText      = Rgb(0xe8e6e2);
		const ImVec4 kTextDim   = Rgb(0x8d929b);

		ImVec4* c = style.Colors;
		c[ImGuiCol_Text]                 = kText;
		c[ImGuiCol_TextDisabled]         = kTextDim;
		c[ImGuiCol_WindowBg]             = kBgWindow;
		c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_PopupBg]              = Rgb(0x1b1d21, 0.98f);
		c[ImGuiCol_Border]               = Rgb(0x000000, 0.55f);
		c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_FrameBg]              = kBgPanel;
		c[ImGuiCol_FrameBgHovered]       = kBgHover;
		c[ImGuiCol_FrameBgActive]        = kBgActive;
		c[ImGuiCol_TitleBg]              = kBgDeep;
		c[ImGuiCol_TitleBgActive]        = Rgb(0x212329);
		c[ImGuiCol_TitleBgCollapsed]     = Rgb(0x131417, 0.85f);
		c[ImGuiCol_MenuBarBg]            = Rgb(0x1f2126);
		c[ImGuiCol_ScrollbarBg]          = Rgb(0x131417, 0.6f);
		c[ImGuiCol_ScrollbarGrab]        = Rgb(0x3a3e48);
		c[ImGuiCol_ScrollbarGrabHovered] = Rgb(0x4a4f5a);
		c[ImGuiCol_ScrollbarGrabActive]  = kAccentDim;
		c[ImGuiCol_CheckMark]            = kAccent;
		c[ImGuiCol_SliderGrab]           = Rgb(0xb8892e);
		c[ImGuiCol_SliderGrabActive]     = kAccent;
		c[ImGuiCol_Button]               = Rgb(0x2a2d34);
		c[ImGuiCol_ButtonHovered]        = Rgb(0x383c45);
		c[ImGuiCol_ButtonActive]         = Rgb(0x4a4f5a);
		c[ImGuiCol_Header]               = Rgb(0x2d3038);
		c[ImGuiCol_HeaderHovered]        = Rgb(0x3a3e48);
		c[ImGuiCol_HeaderActive]         = Rgb(0x454a55);
		c[ImGuiCol_Separator]            = Rgb(0x33363e);
		c[ImGuiCol_SeparatorHovered]     = kAccentDim;
		c[ImGuiCol_SeparatorActive]      = kAccent;
		c[ImGuiCol_ResizeGrip]           = Rgb(0x33363e, 0.6f);
		c[ImGuiCol_ResizeGripHovered]    = kAccentDim;
		c[ImGuiCol_ResizeGripActive]     = kAccent;
		c[ImGuiCol_Tab]                  = Rgb(0x1b1d21);
		c[ImGuiCol_TabHovered]           = Rgb(0x3a3e48);
		c[ImGuiCol_TabSelected]          = Rgb(0x2d3038);
		c[ImGuiCol_TabSelectedOverline]  = kAccent;
		c[ImGuiCol_TabDimmed]            = Rgb(0x17181c);
		c[ImGuiCol_TabDimmedSelected]    = Rgb(0x22242a);
		c[ImGuiCol_TabDimmedSelectedOverline] = kAccentDim;
		c[ImGuiCol_DockingPreview]       = Rgb(0xd9a133, 0.35f);
		c[ImGuiCol_DockingEmptyBg]       = ImVec4(0, 0, 0, 0); // passthrough 3D
		c[ImGuiCol_PlotLines]            = kAccent;
		c[ImGuiCol_PlotHistogram]        = kAccent;
		c[ImGuiCol_TableHeaderBg]        = Rgb(0x22242a);
		c[ImGuiCol_TableBorderStrong]    = Rgb(0x33363e);
		c[ImGuiCol_TableBorderLight]     = Rgb(0x282b31);
		c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_TableRowBgAlt]        = Rgb(0xffffff, 0.03f);
		c[ImGuiCol_TextSelectedBg]       = Rgb(0xd9a133, 0.30f);
		c[ImGuiCol_DragDropTarget]       = kAccent;
		c[ImGuiCol_NavCursor]            = kAccent;
		c[ImGuiCol_NavWindowingHighlight] = Rgb(0xffffff, 0.6f);
		c[ImGuiCol_NavWindowingDimBg]    = Rgb(0x000000, 0.5f);
		c[ImGuiCol_ModalWindowDimBg]     = Rgb(0x000000, 0.55f);
	}
#else
	void ApplyWorldEditorTheme() {}
#endif
}
