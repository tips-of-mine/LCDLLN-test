#include "GltfImporter.h"

#include "JsonDocument.h"
#include "engine/core/Log.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace tools::zone_builder
{
	namespace
	{
		std::filesystem::path ResolveContentPath(const engine::core::Config& config, std::string_view relativePath)
		{
			std::string contentRoot = config.GetString("paths.content", "game/data");
			if (contentRoot.empty())
			{
				contentRoot = "game/data";
				LOG_WARN(Core, "[ZoneBuilder] paths.content is empty, fallback to {}", contentRoot);
			}

			return std::filesystem::path(contentRoot) / std::filesystem::path(relativePath);
		}

		void ExtractNamedEntries(const JsonValue& root, std::string_view arrayName, std::string_view defaultPrefix, std::vector<std::string>& outNames)
		{
			const JsonValue* arrayValue = FindObjectMember(root, arrayName);
			if (arrayValue == nullptr || arrayValue->type != JsonType::Array)
			{
				return;
			}

			outNames.reserve(arrayValue->arrayValue.size());
			for (size_t index = 0; index < arrayValue->arrayValue.size(); ++index)
			{
				const JsonValue& entry = arrayValue->arrayValue[index];
				if (entry.type != JsonType::Object)
				{
					continue;
				}

				const JsonValue* nameValue = FindObjectMember(entry, "name");
				if (nameValue != nullptr && nameValue->type == JsonType::String && !nameValue->stringValue.empty())
				{
					outNames.push_back(nameValue->stringValue);
					continue;
				}

				outNames.push_back(std::string(defaultPrefix) + std::to_string(index));
			}
		}
	}

	bool LoadGltfAsset(const engine::core::Config& config, std::string_view relativeGltfPath, GltfAsset& outAsset, std::string& outError)
	{
		outAsset = GltfAsset{};
		outAsset.relativePath = std::string(relativeGltfPath);

		const std::filesystem::path gltfPath = ResolveContentPath(config, relativeGltfPath);
		LOG_INFO(Core, "[ZoneBuilder] Loading glTF {}", gltfPath.string());

		if (gltfPath.extension() != ".gltf")
		{
			outError = "only textual .gltf files are supported by the current importer";
			LOG_ERROR(Core, "[ZoneBuilder] Load glTF FAILED (path={}, reason={})", gltfPath.string(), outError);
			return false;
		}

		std::ifstream gltfStream(gltfPath, std::ios::in | std::ios::binary);
		if (!gltfStream.is_open())
		{
			outError = "failed to open glTF file";
			LOG_ERROR(Core, "[ZoneBuilder] Load glTF FAILED (path={}, reason={})", gltfPath.string(), outError);
			return false;
		}

		const std::string text((std::istreambuf_iterator<char>(gltfStream)), std::istreambuf_iterator<char>());
		if (text.empty())
		{
			outError = "glTF file is empty";
			LOG_ERROR(Core, "[ZoneBuilder] Load glTF FAILED (path={}, reason={})", gltfPath.string(), outError);
			return false;
		}

		JsonValue root;
		if (!ParseJsonDocument(text, root, outError))
		{
			LOG_ERROR(Core, "[ZoneBuilder] Load glTF FAILED (path={}, reason={})", gltfPath.string(), outError);
			return false;
		}

		if (root.type != JsonType::Object)
		{
			outError = "glTF root must be an object";
			LOG_ERROR(Core, "[ZoneBuilder] Load glTF FAILED (path={}, reason={})", gltfPath.string(), outError);
			return false;
		}

		ExtractNamedEntries(root, "meshes", "mesh_", outAsset.meshNames);
		ExtractNamedEntries(root, "materials", "material_", outAsset.materialNames);

		LOG_INFO(Core, "[ZoneBuilder] glTF loaded (path={}, meshes={}, materials={})",
			gltfPath.string(),
			outAsset.meshNames.size(),
			outAsset.materialNames.size());
		return true;
	}
}
