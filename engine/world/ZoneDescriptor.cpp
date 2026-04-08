#include "engine/world/ZoneDescriptor.h"

#include "engine/render/terrain/HeightmapLoader.h"

#include <nlohmann/json.hpp>

namespace engine::world
{
	namespace
	{
		bool ParseHeightmapDims(const nlohmann::json& root, uint32_t& outW, uint32_t& outH, std::string& err)
		{
			err.clear();
			if (!root.contains("heightmap_width") && !root.contains("heightmap_height"))
				return false;
			if (!root.contains("heightmap_width") || !root["heightmap_width"].is_number_integer() ||
			    !root.contains("heightmap_height") || !root["heightmap_height"].is_number_integer())
			{
				err = "heightmap_width and heightmap_height must both be present as integers";
				return false;
			}
			const int64_t w = root["heightmap_width"].get<int64_t>();
			const int64_t h = root["heightmap_height"].get<int64_t>();
			if (w <= 0 || h <= 0 || w > static_cast<int64_t>(UINT32_MAX) || h > static_cast<int64_t>(UINT32_MAX))
			{
				err = "heightmap_width/height must be in 1..UINT32_MAX";
				return false;
			}
			outW = static_cast<uint32_t>(w);
			outH = static_cast<uint32_t>(h);
			return true;
		}
	} // namespace

	bool ParseZoneDescriptorJson(std::string_view jsonUtf8, ZoneDescriptorV1& out, std::string& err)
	{
		out = ZoneDescriptorV1{};
		err.clear();
		nlohmann::json root;
		try
		{
			root = nlohmann::json::parse(jsonUtf8.begin(), jsonUtf8.end());
		}
		catch (const std::exception& e)
		{
			err = std::string("zone.json parse error: ") + e.what();
			return false;
		}

		if (!root.is_object())
		{
			err = "zone.json root must be object";
			return false;
		}

		if (!root.contains("world_editor_format") || !root["world_editor_format"].is_number_integer())
		{
			err = "zone.json missing world_editor_format (int)";
			return false;
		}
		out.format_version = root["world_editor_format"].get<int>();
		if (out.format_version < ZoneDescriptorV1::kMinWorldEditorFormat ||
		    out.format_version > ZoneDescriptorV1::kMaxWorldEditorFormat)
		{
			err = "zone.json world_editor_format not supported (got " + std::to_string(out.format_version) + ", expected "
				+ std::to_string(ZoneDescriptorV1::kMinWorldEditorFormat) + ".."
				+ std::to_string(ZoneDescriptorV1::kMaxWorldEditorFormat) + ")";
			return false;
		}

		if (root.contains("zone_schema"))
		{
			if (!root["zone_schema"].is_object())
			{
				err = "zone_schema must be object";
				return false;
			}
			const auto& zs = root["zone_schema"];
			if (!zs.contains("name") || !zs["name"].is_string())
			{
				err = "zone_schema.name required (string)";
				return false;
			}
			if (!zs.contains("version") || !zs["version"].is_number_integer())
			{
				err = "zone_schema.version required (int)";
				return false;
			}
			out.zone_schema.name = zs["name"].get<std::string>();
			out.zone_schema.version = zs["version"].get<int>();
			out.has_zone_schema = true;
		}

		std::string dimErr;
		uint32_t dw = 0, dh = 0;
		if (ParseHeightmapDims(root, dw, dh, dimErr))
		{
			out.heightmap_width = dw;
			out.heightmap_height = dh;
			out.has_heightmap_dims = true;
		}
		else if (!dimErr.empty())
		{
			err = dimErr;
			return false;
		}

		if (out.format_version >= 2)
		{
			if (!out.has_zone_schema)
			{
				err = "world_editor_format >= 2 requires zone_schema";
				return false;
			}
			if (out.zone_schema.name != ZoneDescriptorV1::kExpectedZoneSchemaName ||
			    out.zone_schema.version != ZoneDescriptorV1::kExpectedZoneSchemaVersion)
			{
				err = std::string("zone_schema must be { name: \"") + ZoneDescriptorV1::kExpectedZoneSchemaName
					+ "\", version: " + std::to_string(ZoneDescriptorV1::kExpectedZoneSchemaVersion) + " } for format 2";
				return false;
			}
			if (!out.has_heightmap_dims)
			{
				err = "world_editor_format >= 2 requires heightmap_width and heightmap_height";
				return false;
			}
		}

		if (!root.contains("zone_id") || !root["zone_id"].is_string())
		{
			err = "zone.json missing zone_id (string)";
			return false;
		}
		out.zone_id = root["zone_id"].get<std::string>();
		if (out.zone_id.empty())
		{
			err = "zone_id must be non-empty";
			return false;
		}

		if (!root.contains("heightmap_r16h") || !root["heightmap_r16h"].is_string())
		{
			err = "zone.json missing heightmap_r16h (string, path relative to zone folder)";
			return false;
		}
		out.heightmap_r16h = root["heightmap_r16h"].get<std::string>();
		if (out.heightmap_r16h.empty())
		{
			err = "heightmap_r16h must be non-empty";
			return false;
		}

		if (root.contains("seed"))
		{
			if (!root["seed"].is_number_integer())
			{
				err = "zone.json seed must be integer if present";
				return false;
			}
			out.seed = root["seed"].get<int64_t>();
			out.has_seed = true;
		}

		if (root.contains("texture_layers"))
		{
			if (!root["texture_layers"].is_array())
			{
				err = "texture_layers must be array of strings";
				return false;
			}
			for (const auto& item : root["texture_layers"])
			{
				if (!item.is_string())
				{
					err = "texture_layers entries must be strings";
					return false;
				}
				out.texture_layers.push_back(item.get<std::string>());
			}
		}

		return true;
	}

	std::filesystem::path ResolveZoneHeightmapPath(const std::filesystem::path& zoneJsonPath,
	                                               const ZoneDescriptorV1& desc)
	{
		return zoneJsonPath.parent_path() / std::filesystem::path(desc.heightmap_r16h);
	}

	bool ValidateZoneHeightmapAgainstFile(const std::filesystem::path& zoneJsonPath,
	                                      const ZoneDescriptorV1& desc, std::string& err)
	{
		err.clear();
		const bool mustCheck = desc.format_version >= 2 ||
			(desc.format_version == 1 && desc.has_heightmap_dims);
		if (!mustCheck)
			return true;

		if (!desc.has_heightmap_dims)
		{
			err = "heightmap dimensions missing (required for this format)";
			return false;
		}

		const std::filesystem::path hmPath = ResolveZoneHeightmapPath(zoneJsonPath, desc);
		uint32_t fw = 0, fh = 0;
		std::string peekErr;
		if (!engine::render::terrain::HeightmapLoader::PeekR16hFileDimensions(hmPath.string(), fw, fh, peekErr))
		{
			err = peekErr;
			return false;
		}
		if (fw != desc.heightmap_width || fh != desc.heightmap_height)
		{
			err = "heightmap dimensions mismatch: zone.json declares " + std::to_string(desc.heightmap_width) + "x"
				+ std::to_string(desc.heightmap_height) + " but .r16h header is " + std::to_string(fw) + "x"
				+ std::to_string(fh);
			return false;
		}
		return true;
	}
}
