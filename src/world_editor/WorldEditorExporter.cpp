// M100.34 — Implémentation WorldEditorExporter (Save Zone).

#include "src/world_editor/WorldEditorExporter.h"

#include <filesystem>
#include <fstream>

namespace engine::editor::world
{
	namespace
	{
		/// Écrit `bytes` dans `path`, créant le dossier parent au besoin.
		/// Retourne false + `outError` rempli en cas d'échec.
		bool WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes, std::string& outError)
		{
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			if (ec)
			{
				outError = "SaveZone: create_directories failed: " + path.parent_path().string();
				return false;
			}
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out.good())
			{
				outError = "SaveZone: open failed: " + path.string();
				return false;
			}
			out.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
			if (!out.good())
			{
				outError = "SaveZone: write failed: " + path.string();
				return false;
			}
			return true;
		}
	}

	bool SaveZone(const std::string& outputDir, const ZoneExportInputs& inputs, std::string& outError)
	{
		namespace fs = std::filesystem;
		const fs::path root(outputDir);
		const fs::path inst = root / "instances";

		// Couches d'instances zone-level (toujours écrites, même vides : un
		// fichier avec count=0 est valide et explicite l'absence de données).
		if (!WriteFile(inst / "props.bin",
			engine::world::instances::SavePropsBin(inputs.props), outError)) return false;
		if (!WriteFile(inst / "buildings.bin",
			engine::world::instances::SaveBuildingsBin(inputs.buildings), outError)) return false;
		if (!WriteFile(inst / "hazards.bin",
			engine::world::hazard::SaveHazardsBin(inputs.hazards), outError)) return false;
		if (!WriteFile(inst / "interactives.bin",
			engine::world::interactive::SaveInteractivesBin(inputs.interactives), outError)) return false;
		if (!WriteFile(inst / "zones.bin",
			engine::world::zones::SaveZonesBin(inputs.zones), outError)) return false;
		if (!WriteFile(inst / "splines.bin",
			engine::world::spline::SaveSplinesBin(inputs.splines), outError)) return false;
		if (!WriteFile(inst / "wind_zones.bin",
			engine::world::wind::SaveWindZonesBin(inputs.windZones), outError)) return false;

		// Données par-chunk : foliage + shade map.
		for (const ChunkExportData& c : inputs.chunks)
		{
			const fs::path chunkDir = root / "chunks" /
				("chunk_" + std::to_string(c.chunkX) + "_" + std::to_string(c.chunkZ));
			if (!WriteFile(chunkDir / "foliage.bin",
				engine::world::foliage::SaveFoliageBin(c.foliage), outError)) return false;
			if (c.hasShade)
			{
				if (!WriteFile(chunkDir / "shade.bin",
					engine::world::thermal::SaveShadeMapBin(c.shade), outError)) return false;
			}
		}

		return true;
	}
}
