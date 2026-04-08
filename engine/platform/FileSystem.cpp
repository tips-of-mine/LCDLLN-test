#include "engine/platform/FileSystem.h"
#include "engine/core/Log.h"
#include "engine/texr/TexrPath.h"
#include "engine/texr/TexrReader.h"

#include <fstream>
#include <mutex>

namespace engine::platform
{
namespace
{
	std::mutex g_content_texr_mutex;
	std::shared_ptr<lcdlln::texr::TexrReader> g_content_texr;

	bool TryReadBytesFromContentTexr(std::string_view relativeContentPath, std::vector<uint8_t>& out)
	{
		const std::string key = lcdlln::texr::NormalizeRelativePath(std::filesystem::path(relativeContentPath));
		std::lock_guard<std::mutex> lock(g_content_texr_mutex);
		if (!g_content_texr)
		{
			return false;
		}
		std::uint32_t ty = 0;
		std::string err;
		if (!g_content_texr->ReadAsset(key, out, ty, err) || out.empty())
		{
			return false;
		}
		return true;
	}
}  // namespace

	void FileSystem::SetContentTexrReader(std::shared_ptr<lcdlln::texr::TexrReader> reader) noexcept
	{
		std::lock_guard<std::mutex> lock(g_content_texr_mutex);
		g_content_texr = std::move(reader);
	}
	std::filesystem::path FileSystem::Join(std::string_view a, std::string_view b)
	{
		return std::filesystem::path(a) / std::filesystem::path(b);
	}

	std::filesystem::path FileSystem::ResolveContentPath(const engine::core::Config& cfg, std::string_view relativeContentPath)
	{
		const std::string base = cfg.GetString("paths.content", "game/data");
		static bool s_bootContentPathLogged = false;
		if (!s_bootContentPathLogged)
		{
			LOG_INFO(Platform, "[Boot] FileSystem content base: {}", base);
			s_bootContentPathLogged = true;
		}
		return Join(base, relativeContentPath);
	}

	std::filesystem::path FileSystem::ResolveExternalPath(const engine::core::Config& cfg, std::string_view relativeExternalPath)
	{
		const std::string base = cfg.GetString("paths.external", "external");
		static bool s_bootExternalPathLogged = false;
		if (!s_bootExternalPathLogged)
		{
			LOG_INFO(Platform, "[Boot] FileSystem external base: {}", base);
			s_bootExternalPathLogged = true;
		}

		std::filesystem::path basePath(base);
		if (!basePath.is_absolute())
		{
			// Si le binaire est lancé depuis le projet, `external/` est relative au dossier courant.
			basePath = std::filesystem::current_path() / basePath;
		}
		return basePath / std::filesystem::path(relativeExternalPath);
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
		std::vector<uint8_t> from_texr;
		if (TryReadBytesFromContentTexr(relativeContentPath, from_texr))
		{
			return from_texr;
		}
		return ReadAllBytes(ResolveContentPath(cfg, relativeContentPath));
	}

	std::string FileSystem::ReadAllTextContent(const engine::core::Config& cfg, std::string_view relativeContentPath)
	{
		std::vector<uint8_t> from_texr;
		if (TryReadBytesFromContentTexr(relativeContentPath, from_texr))
		{
			return std::string(reinterpret_cast<const char*>(from_texr.data()), from_texr.size());
		}
		return ReadAllText(ResolveContentPath(cfg, relativeContentPath));
	}

	bool FileSystem::WriteAllText(const std::filesystem::path& path, std::string_view text)
	{
		std::error_code ec;
		const std::filesystem::path parent = path.parent_path();
		if (!parent.empty())
		{
			std::filesystem::create_directories(parent, ec);
			if (ec)
			{
				LOG_ERROR(Platform, "[FileSystem] WriteAllText FAILED: create_directories({}, {})", parent.string(), ec.message());
				return false;
			}
		}

		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
		{
			LOG_ERROR(Platform, "[FileSystem] WriteAllText FAILED: open {}", path.string());
			return false;
		}

		out.write(text.data(), static_cast<std::streamsize>(text.size()));
		if (!out.good())
		{
			LOG_ERROR(Platform, "[FileSystem] WriteAllText FAILED: write {}", path.string());
			return false;
		}

		LOG_INFO(Platform, "[FileSystem] WriteAllText OK (path={}, bytes={})", path.string(), text.size());
		return true;
	}

	bool FileSystem::WriteAllTextContent(const engine::core::Config& cfg, std::string_view relativeContentPath, std::string_view text)
	{
		const std::filesystem::path path = ResolveContentPath(cfg, relativeContentPath);
		const bool ok = WriteAllText(path, text);
		if (ok)
		{
			LOG_INFO(Platform, "[FileSystem] WriteAllTextContent OK (relative_path={})", relativeContentPath);
		}
		else
		{
			LOG_ERROR(Platform, "[FileSystem] WriteAllTextContent FAILED (relative_path={})", relativeContentPath);
		}
		return ok;
	}
}

