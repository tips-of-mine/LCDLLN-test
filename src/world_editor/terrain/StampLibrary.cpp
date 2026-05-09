#include "src/world_editor/terrain/StampLibrary.h"

// stb_image est inclus SANS définir STB_IMAGE_IMPLEMENTATION : la macro est
// déjà posée dans 3 autres TU (WorldMapIo.cpp, TexturePreviewCache.cpp,
// AssetRegistry.cpp). Ici on consomme uniquement les déclarations publiques.
#include "external/stb/stb_image.h"

#include <system_error>

namespace engine::editor::world
{
	void ConvertUint16GrayscaleToHeights(const uint16_t* src,
		uint32_t width,
		std::vector<float>& outHeights)
	{
		const size_t total = static_cast<size_t>(width) * width;
		outHeights.assign(total, 0.0f);
		if (src == nullptr || width == 0) return;
		// Constante de normalisation 65535.0 (max uint16). Pré-calculée pour
		// éviter une division flottante par cellule.
		constexpr float kInv16 = 1.0f / 65535.0f;
		for (size_t i = 0; i < total; ++i)
		{
			outHeights[i] = static_cast<float>(src[i]) * kInv16;
		}
	}

	std::vector<StampEntry> EnumerateStampLibrary(const std::filesystem::path& dir)
	{
		std::vector<StampEntry> out;
		std::error_code ec;
		if (!std::filesystem::exists(dir, ec) || ec) return out;
		if (!std::filesystem::is_directory(dir, ec) || ec) return out;

		for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
		{
			if (ec) break;
			if (!entry.is_regular_file()) continue;
			const auto& p = entry.path();
			// Compare l'extension en lower-case pour gérer ".PNG" sur Windows.
			std::string ext = p.extension().string();
			for (auto& c : ext) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			if (ext != ".png") continue;
			StampEntry se;
			se.name = p.stem().string();
			se.path = p;
			out.push_back(std::move(se));
		}
		return out;
	}

	bool LoadStampPng16(const std::filesystem::path& path,
		std::vector<float>& outHeights,
		uint32_t& outResolution,
		std::string& outError)
	{
		outHeights.clear();
		outResolution = 0;
		outError.clear();

		int w = 0, h = 0, channels = 0;
		// `req_comp = 1` force un canal (luminance). Si l'image source est
		// RGBA, stb_image fait la conversion. Si elle est déjà 16-bit GRAY,
		// le buffer est retourné tel quel (uint16 par cellule).
		const std::string pathStr = path.string();
		stbi_us* data = stbi_load_16(pathStr.c_str(), &w, &h, &channels, 1);
		if (data == nullptr)
		{
			const char* reason = stbi_failure_reason();
			outError = reason ? reason : "stbi_load_16 retourned null";
			return false;
		}

		if (w != h)
		{
			stbi_image_free(data);
			outError = "stamp PNG must be square (width == height)";
			return false;
		}

		outResolution = static_cast<uint32_t>(w);
		ConvertUint16GrayscaleToHeights(reinterpret_cast<const uint16_t*>(data),
			outResolution, outHeights);
		stbi_image_free(data);
		return true;
	}
}
