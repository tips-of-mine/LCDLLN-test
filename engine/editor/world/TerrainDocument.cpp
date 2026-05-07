#include "engine/editor/world/TerrainDocument.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/world/terrain/TerrainChunkLoader.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace engine::editor::world
{
	uint64_t TerrainDocument::PackCoord(engine::world::GlobalChunkCoord c)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32)
			 | static_cast<uint64_t>(static_cast<uint32_t>(c.z));
	}

	std::shared_ptr<engine::world::terrain::TerrainChunk>
	TerrainDocument::EnsureLoaded(const engine::core::Config& config, int chunkX, int chunkZ)
	{
		const engine::world::GlobalChunkCoord coord{chunkX, chunkZ};
		const uint64_t key = PackCoord(coord);
		auto it = m_chunks.find(key);
		if (it != m_chunks.end()) return it->second.chunk;

		// Tentative de chargement disque via la clé canonique.
		const std::string contentRoot = config.GetString("paths.content", "game/data");
		std::ostringstream pathStream;
		pathStream << contentRoot << "/chunks/chunk_" << chunkX << "_" << chunkZ
		           << "/terrain.bin";
		const std::string fullPath = pathStream.str();

		std::shared_ptr<engine::world::terrain::TerrainChunk> chunk;
		std::ifstream f(fullPath, std::ios::binary);
		if (f.good())
		{
			f.seekg(0, std::ios::end);
			const std::streamsize size = f.tellg();
			f.seekg(0, std::ios::beg);
			if (size > 0)
			{
				std::vector<uint8_t> blob(static_cast<size_t>(size));
				f.read(reinterpret_cast<char*>(blob.data()), size);
				auto loaded = std::make_shared<engine::world::terrain::TerrainChunk>();
				std::span<const uint8_t> bytes(blob.data(), blob.size());
				std::string err;
				if (engine::world::terrain::LoadTerrainBin(bytes, *loaded, err))
				{
					chunk = std::move(loaded);
				}
				else
				{
					LOG_WARN(EditorWorld,
						"[TerrainDocument] LoadTerrainBin fail ({}): {}", fullPath, err);
				}
			}
		}
		// Si chargement échoué ou fichier absent, créer un chunk plat à 0.
		if (!chunk)
		{
			chunk = std::make_shared<engine::world::terrain::TerrainChunk>(
				engine::world::terrain::TerrainChunk::MakeFlat(0.0f));
		}

		m_chunks.emplace(key, ChunkSlot{chunk, false});
		return chunk;
	}

	void TerrainDocument::MarkDirty(engine::world::GlobalChunkCoord coord)
	{
		auto it = m_chunks.find(PackCoord(coord));
		if (it == m_chunks.end()) return; // sécurité : on ne marque pas un chunk non chargé
		it->second.dirty = true;
	}

	bool TerrainDocument::HasDirtyChunks() const
	{
		for (const auto& [key, slot] : m_chunks)
			if (slot.dirty) return true;
		return false;
	}

	std::shared_ptr<engine::world::terrain::TerrainChunk>
	TerrainDocument::Find(engine::world::GlobalChunkCoord coord) const
	{
		auto it = m_chunks.find(PackCoord(coord));
		return (it == m_chunks.end()) ? nullptr : it->second.chunk;
	}

	size_t TerrainDocument::SaveDirtyToDisk(const engine::core::Config& config)
	{
		size_t written = 0;
		const std::string contentRoot = config.GetString("paths.content", "game/data");
		for (auto& [key, slot] : m_chunks)
		{
			if (!slot.dirty) continue;
			// Reconstruction des coords depuis la clé empaquetée.
			const uint32_t hi = static_cast<uint32_t>(key >> 32);
			const uint32_t lo = static_cast<uint32_t>(key & 0xFFFFFFFFu);
			const int chunkX = static_cast<int>(static_cast<int32_t>(hi));
			const int chunkZ = static_cast<int>(static_cast<int32_t>(lo));

			std::ostringstream dirStream;
			dirStream << contentRoot << "/chunks/chunk_" << chunkX << "_" << chunkZ;
			const std::filesystem::path dir(dirStream.str());
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			if (ec)
			{
				LOG_WARN(EditorWorld,
					"[TerrainDocument] mkdir fail {}: {}", dir.string(), ec.message());
				continue;
			}

			std::vector<uint8_t> bytes;
			std::string err;
			if (!engine::world::terrain::SaveTerrainBin(*slot.chunk, bytes, err))
			{
				LOG_WARN(EditorWorld,
					"[TerrainDocument] SaveTerrainBin fail ({},{}): {}", chunkX, chunkZ, err);
				continue;
			}

			const std::filesystem::path file = dir / "terrain.bin";
			std::ofstream out(file, std::ios::binary | std::ios::trunc);
			if (!out.good())
			{
				LOG_WARN(EditorWorld,
					"[TerrainDocument] open fail {}", file.string());
				continue;
			}
			out.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
			if (!out.good())
			{
				LOG_WARN(EditorWorld,
					"[TerrainDocument] write fail {}", file.string());
				continue;
			}
			slot.dirty = false;
			++written;
		}
		return written;
	}
}
