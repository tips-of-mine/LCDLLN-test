#pragma once

#include <functional>
#include <string_view>

namespace engine::world_editor
{
	struct WorldEditorRunOptions
	{
		/// Appelée pour chaque message diagnostic (stdout + fichier si -log actif côté main).
		std::function<void(std::string_view)> log;
	};

	/// Boucle fenêtre + ImGui (OpenGL 3). Ne dépend pas de Vulkan ni de lcdlln.exe.
	/// \return 0 si sortie normale, non-zéro si échec d’initialisation.
	int RunWorldEditor(const WorldEditorRunOptions& opts);
}
