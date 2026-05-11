// src/world_editor/water/WaterDocument.cpp
#include "src/world_editor/water/WaterDocument.h"

#include "src/shared/core/Config.h"

#include <filesystem>
#include <fstream>

namespace engine::editor::world
{
	bool WaterDocument::SaveToDisk(const engine::core::Config& cfg, std::string& outError)
	{
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		const std::filesystem::path path =
			std::filesystem::path(contentRoot) / "instances" / "water.bin";

		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec)
		{
			outError = "WaterDocument::SaveToDisk: mkdir failed: " + ec.message();
			return false;
		}

		std::vector<uint8_t> bytes;
		if (!engine::world::water::SaveWaterBin(m_scene, bytes, outError))
			return false;

		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f.good())
		{
			outError = "WaterDocument::SaveToDisk: cannot open " + path.string();
			return false;
		}
		f.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!f.good())
		{
			outError = "WaterDocument::SaveToDisk: write failed";
			return false;
		}
		m_dirty = false;
		return true;
	}

	bool WaterDocument::LoadFromDisk(const engine::core::Config& cfg, std::string& outError)
	{
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		const std::filesystem::path path =
			std::filesystem::path(contentRoot) / "instances" / "water.bin";

		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f.good())
		{
			// Fichier absent : pas une erreur, scene reste vide.
			m_scene.lakes.clear();
			m_scene.rivers.clear();
			m_dirty = false;
			return true;
		}
		const std::streamsize size = f.tellg();
		if (size <= 0)
		{
			// Fichier vide : traité comme absent (scene reste vide).
			m_scene.lakes.clear();
			m_scene.rivers.clear();
			m_dirty = false;
			return true;
		}
		f.seekg(0, std::ios::beg);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		f.read(reinterpret_cast<char*>(bytes.data()), size);
		if (!f.good() && !f.eof())
		{
			outError = "WaterDocument::LoadFromDisk: read failed for " + path.string();
			return false;
		}

		if (!engine::world::water::LoadWaterBin(
			std::span<const uint8_t>(bytes), m_scene, outError))
			return false;

		m_dirty = false;
		return true;
	}
}
