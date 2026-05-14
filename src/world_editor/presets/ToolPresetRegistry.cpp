#include "src/world_editor/presets/ToolPresetRegistry.h"

#include "src/shared/core/Log.h"
#include "src/world_editor/presets/ToolPresetIo.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace engine::editor::world::presets
{
	namespace
	{
		const std::vector<ToolPreset>& EmptyPresetList()
		{
			static const std::vector<ToolPreset> kEmpty;
			return kEmpty;
		}
	}

	ToolPresetRegistry& ToolPresetRegistry::Instance()
	{
		static ToolPresetRegistry instance;
		return instance;
	}

	size_t ToolPresetRegistry::LoadFromContentPath(const std::string& contentRoot)
	{
		m_lastContentRoot = contentRoot;
		m_presetsByTool.clear();
		m_defaultByTool.clear();

		const std::filesystem::path dir =
			std::filesystem::path(contentRoot) / "editor" / "tool_presets";
		std::error_code ec;
		if (!std::filesystem::exists(dir, ec) || ec)
		{
			// Dossier absent : pas une erreur (premier lancement / dev).
			return 0u;
		}

		size_t loaded = 0u;
		for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
		{
			if (ec) break;
			if (!entry.is_regular_file()) continue;
			if (entry.path().extension() != ".json") continue;

			std::ifstream f(entry.path());
			if (!f.good())
			{
				LOG_WARN(EditorWorld, "[ToolPresetRegistry] illisible : {}",
					entry.path().string());
				continue;
			}
			std::stringstream buf;
			buf << f.rdbuf();

			ToolPresetFile parsed;
			std::string err;
			if (!ParseToolPresetJson(buf.str(), parsed, err))
			{
				// Tolérant : on logue et on continue avec les autres.
				LOG_WARN(EditorWorld, "[ToolPresetRegistry] {} ignoré : {}",
					entry.path().filename().string(), err);
				continue;
			}

			m_presetsByTool[parsed.toolId]  = std::move(parsed.presets);
			m_defaultByTool[parsed.toolId]  = std::move(parsed.defaultPreset);
			++loaded;
		}

		LOG_INFO(EditorWorld, "[ToolPresetRegistry] {} fichier(s) de presets chargé(s)",
			loaded);
		return loaded;
	}

	size_t ToolPresetRegistry::Reload()
	{
		if (m_lastContentRoot.empty()) return 0u;
		return LoadFromContentPath(m_lastContentRoot);
	}

	const std::vector<ToolPreset>& ToolPresetRegistry::GetPresetsForTool(
		const std::string& toolId) const
	{
		auto it = m_presetsByTool.find(toolId);
		return (it == m_presetsByTool.end()) ? EmptyPresetList() : it->second;
	}

	std::string ToolPresetRegistry::GetDefaultPresetId(const std::string& toolId) const
	{
		auto it = m_defaultByTool.find(toolId);
		return (it == m_defaultByTool.end()) ? std::string{} : it->second;
	}

	const ToolPreset* ToolPresetRegistry::FindPreset(const std::string& toolId,
		const std::string& presetId) const
	{
		auto it = m_presetsByTool.find(toolId);
		if (it == m_presetsByTool.end()) return nullptr;
		for (const auto& preset : it->second)
		{
			if (preset.id == presetId) return &preset;
		}
		return nullptr;
	}

	void ToolPresetRegistry::ResetForTesting()
	{
		m_presetsByTool.clear();
		m_defaultByTool.clear();
		m_lastContentRoot.clear();
	}
}
