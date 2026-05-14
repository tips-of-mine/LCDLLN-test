#include "src/world_editor/zone_presets/ZonePresetRegistry.h"

#include "src/shared/core/Log.h"
#include "src/world_editor/zone_presets/ZonePresetIo.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace engine::editor::world::zone_presets
{
	ZonePresetRegistry& ZonePresetRegistry::Instance()
	{
		static ZonePresetRegistry instance;
		return instance;
	}

	size_t ZonePresetRegistry::LoadFromContentPath(const std::string& contentRoot)
	{
		m_lastContentRoot = contentRoot;
		m_presets.clear();

		const std::filesystem::path dir =
			std::filesystem::path(contentRoot) / "editor" / "zone_presets";
		std::error_code ec;
		if (!std::filesystem::exists(dir, ec) || ec)
		{
			// Dossier absent : pas une erreur (catalogue vide).
			return 0u;
		}

		for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
		{
			if (ec) break;
			if (!entry.is_regular_file()) continue;
			if (entry.path().extension() != ".json") continue;

			std::ifstream f(entry.path());
			if (!f.good())
			{
				LOG_WARN(EditorWorld, "[ZonePresetRegistry] illisible : {}",
					entry.path().string());
				continue;
			}
			std::stringstream buf;
			buf << f.rdbuf();

			ZonePreset preset;
			std::string err;
			if (!ParseZonePresetJson(buf.str(), preset, err))
			{
				LOG_WARN(EditorWorld, "[ZonePresetRegistry] {} ignoré (parse) : {}",
					entry.path().filename().string(), err);
				continue;
			}
			if (!preset.Validate(err))
			{
				LOG_WARN(EditorWorld, "[ZonePresetRegistry] {} ignoré (validation) : {}",
					entry.path().filename().string(), err);
				continue;
			}
			m_presets.push_back(std::move(preset));
		}

		// Tri stable par id pour un ordre d'affichage déterministe dans
		// la grille du dialog (indépendant de l'ordre du système de fichiers).
		std::sort(m_presets.begin(), m_presets.end(),
			[](const ZonePreset& a, const ZonePreset& b) { return a.id < b.id; });

		LOG_INFO(EditorWorld, "[ZonePresetRegistry] {} preset(s) de zone chargé(s)",
			m_presets.size());
		return m_presets.size();
	}

	size_t ZonePresetRegistry::Reload()
	{
		if (m_lastContentRoot.empty()) return 0u;
		return LoadFromContentPath(m_lastContentRoot);
	}

	const ZonePreset* ZonePresetRegistry::FindById(const std::string& id) const
	{
		auto it = std::find_if(m_presets.begin(), m_presets.end(),
			[&id](const ZonePreset& p) { return p.id == id; });
		return (it == m_presets.end()) ? nullptr : &*it;
	}

	void ZonePresetRegistry::ResetForTesting()
	{
		m_presets.clear();
		m_lastContentRoot.clear();
	}
}
