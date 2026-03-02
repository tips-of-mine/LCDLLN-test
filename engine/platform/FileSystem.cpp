#include "engine/platform/FileSystem.h"

#include <fstream>

namespace engine::platform
{
	std::filesystem::path FileSystem::Join(std::string_view a, std::string_view b)
	{
		return std::filesystem::path(a) / std::filesystem::path(b);
	}

	std::filesystem::path FileSystem::ResolveContentPath(const engine::core::Config& cfg, std::string_view relativeContentPath)
	{
		const std::string base = cfg.GetString("paths.content", "game/data");
		return Join(base, relativeContentPath);
	}

	bool FileSystem::Exists(const std::filesystem::path& path)
	{
		std::error_code ec;
		return std::filesystem::exists(path, ec);
	}

	std::vector<std::filesystem::path> FileSystem::ListDirectory(const std::filesystem::path& dir)
	{
		std::vector<std::filesystem::path> out;
		std::error_code ec;
		if (!std::filesystem::exists(dir, ec))
		{
			return out;
		}

		for (const auto& e : std::filesystem::directory_iterator(dir, ec))
		{
			if (ec)
			{
				break;
			}
			out.push_back(e.path());
		}
		return out;
	}

	std::vector<uint8_t> FileSystem::ReadAllBytes(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in.is_open())
		{
			return {};
		}
		in.seekg(0, std::ios::end);
		const std::streamsize size = in.tellg();
		in.seekg(0, std::ios::beg);
		if (size <= 0)
		{
			return {};
		}

		std::vector<uint8_t> data(static_cast<size_t>(size));
		in.read(reinterpret_cast<char*>(data.data()), size);
		return data;
	}

	std::string FileSystem::ReadAllText(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in.is_open())
		{
			return {};
		}
		std::string s;
		in.seekg(0, std::ios::end);
		const std::streamsize size = in.tellg();
		in.seekg(0, std::ios::beg);
		if (size <= 0)
		{
			return {};
		}

		s.resize(static_cast<size_t>(size));
		in.read(s.data(), size);
		return s;
	}

	std::vector<uint8_t> FileSystem::ReadAllBytesContent(const engine::core::Config& cfg, std::string_view relativeContentPath)
	{
		return ReadAllBytes(ResolveContentPath(cfg, relativeContentPath));
	}

	std::string FileSystem::ReadAllTextContent(const engine::core::Config& cfg, std::string_view relativeContentPath)
	{
		return ReadAllText(ResolveContentPath(cfg, relativeContentPath));
	}
}

