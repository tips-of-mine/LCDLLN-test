#pragma once

// Partage de pointeurs ImFont entre WorldEditorImGui (loader) et les renderers
// auth/HUD (consommateurs). Variable statique inline pour eviter les problemes
// de linkage et garantir l'unicite. Type opaque pour ne pas obliger les headers
// non-Windows a inclure imgui.h.
//
// Usage :
//   * WorldEditorImGui.cpp (apres AddFontFromMemoryTTF) :
//       g_largePasswordFont = font_ptr;
//   * AuthImGuiRenderer.cpp (autour InputText password) :
//       if (g_largePasswordFont) ImGui::PushFont(g_largePasswordFont);

namespace engine::render::SharedFontHandles
{
	/// Pointeur vers une fonte chargee a une taille >13 px destinee aux widgets
	/// password (mask '*' lisible). Nul si le loader n'a pas reussi a charger
	/// l'asset, dans ce cas le widget retombe sur la fonte courante.
	///
	/// Type erase a void* car ImFont est defini dans imgui.h qui est include
	/// uniquement sous _WIN32. Le consumer cast en ImFont* avant usage.
	inline void* g_largePasswordFont = nullptr;
}
