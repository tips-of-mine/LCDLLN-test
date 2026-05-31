#include "src/masterd/world/ZoneNameRegistry.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <filesystem>

namespace engine::server
{
	size_t ZoneNameRegistry::Load(const std::string& zonesRoot)
	{
		m_byId.clear();

		std::filesystem::path root(zonesRoot);
		if (!engine::platform::FileSystem::Exists(root))
		{
			LOG_INFO(Server, "[ZoneNameRegistry] dossier zones absent ({}) — noms de région indisponibles (fallback portail).", zonesRoot);
			return 0;
		}

		for (const auto& entry : engine::platform::FileSystem::ListDirectory(root))
		{
			std::error_code ec;
			if (!std::filesystem::is_directory(entry, ec))
				continue;

			const std::filesystem::path manifest = entry / "runtime_manifest.json";
			if (!engine::platform::FileSystem::Exists(manifest))
				continue;

			engine::core::Config cfg;
			if (!cfg.LoadFromFile(manifest.string()))
				continue;

			const int64_t numericId = cfg.GetInt("zone_numeric_id", 0);
			const std::string name = cfg.GetString("display_name", "");
			if (numericId > 0 && !name.empty())
				m_byId[static_cast<uint32_t>(numericId)] = name;
		}

		LOG_INFO(Server, "[ZoneNameRegistry] {} zone(s) nommée(s) chargée(s) depuis {}", m_byId.size(), zonesRoot);
		return m_byId.size();
	}

	std::string ZoneNameRegistry::NameFor(uint32_t zoneId) const
	{
		auto it = m_byId.find(zoneId);
		return it != m_byId.end() ? it->second : std::string();
	}
}
