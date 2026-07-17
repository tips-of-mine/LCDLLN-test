#pragma once
// EditorTheme — thème visuel sombre du binaire éditeur monde (polish UI
// 2026-07-17, inspiré des conventions Unreal Engine : fonds anthracite,
// contrastes nets, accent doré identité LCDLLN, arrondis discrets,
// espacements généreux). Appliqué UNIQUEMENT quand `isWorldEditorExe`
// (le client de jeu garde le style ImGui de ses UIs propres).

namespace engine::editor::world
{
	/// Applique le thème sombre de l'éditeur monde au style ImGui global.
	/// Doit être appelée APRÈS `ImGui::CreateContext` (le style vit dans le
	/// contexte) et avant le premier rendu. Main thread. Idempotente.
	/// Effet de bord : écrase `ImGui::GetStyle()` (couleurs + métriques).
	/// No-op hors Windows (ImGui absent des autres plateformes).
	void ApplyWorldEditorTheme();
}
