#include "LayoutImporter.h"

#include "JsonDocument.h"
#include "engine/core/Log.h"

#include <cmath>
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

		bool ReadRequiredNumberArray3(const JsonValue& parent, std::string_view memberName, double& outX, double& outY, double& outZ, std::string& outError)
		{
			const JsonValue* arrayValue = FindObjectMember(parent, memberName);
			if (arrayValue == nullptr)
			{
				outError = std::string("missing required member '") + std::string(memberName) + "'";
				return false;
			}

			if (arrayValue->type != JsonType::Array || arrayValue->arrayValue.size() != 3)
			{
				outError = std::string("member '") + std::string(memberName) + "' must be an array of 3 numbers";
				return false;
			}

			const JsonValue& x = arrayValue->arrayValue[0];
			const JsonValue& y = arrayValue->arrayValue[1];
			const JsonValue& z = arrayValue->arrayValue[2];
			if (x.type != JsonType::Number || y.type != JsonType::Number || z.type != JsonType::Number)
			{
				outError = std::string("member '") + std::string(memberName) + "' must contain only numbers";
				return false;
			}

			outX = x.numberValue;
			outY = y.numberValue;
			outZ = z.numberValue;
			return true;
		}

		bool ReadRequiredString(const JsonValue& parent, std::string_view memberName, std::string& outValue, std::string& outError)
		{
			const JsonValue* value = FindObjectMember(parent, memberName);
			if (value == nullptr)
			{
				outError = std::string("missing required member '") + std::string(memberName) + "'";
				return false;
			}

			if (value->type != JsonType::String || value->stringValue.empty())
			{
				outError = std::string("member '") + std::string(memberName) + "' must be a non-empty string";
				return false;
			}

			outValue = value->stringValue;
			return true;
		}

		bool ReadRequiredInt(const JsonValue& parent, std::string_view memberName, int& outValue, std::string& outError)
		{
			const JsonValue* value = FindObjectMember(parent, memberName);
			if (value == nullptr)
			{
				outError = std::string("missing required member '") + std::string(memberName) + "'";
				return false;
			}

			if (value->type != JsonType::Number || std::trunc(value->numberValue) != value->numberValue)
			{
				outError = std::string("member '") + std::string(memberName) + "' must be an integer";
				return false;
			}

			outValue = static_cast<int>(value->numberValue);
			return true;
		}
	}

	bool LoadLayoutDocument(const engine::core::Config& config, std::string_view relativeLayoutPath, LayoutDocument& outDocument, std::string& outError)
	{
		outDocument = LayoutDocument{};
		outDocument.relativePath = std::string(relativeLayoutPath);

		const std::filesystem::path layoutPath = ResolveContentPath(config, relativeLayoutPath);
		LOG_INFO(Core, "[ZoneBuilder] Loading layout {}", layoutPath.string());

		std::ifstream layoutStream(layoutPath, std::ios::in | std::ios::binary);
		if (!layoutStream.is_open())
		{
			outError = "failed to open layout file";
			LOG_ERROR(Core, "[ZoneBuilder] Load layout FAILED (path={}, reason={})", layoutPath.string(), outError);
			return false;
		}

		std::string text((std::istreambuf_iterator<char>(layoutStream)), std::istreambuf_iterator<char>());
		if (text.empty())
		{
			outError = "layout file is empty";
			LOG_ERROR(Core, "[ZoneBuilder] Load layout FAILED (path={}, reason={})", layoutPath.string(), outError);
			return false;
		}

		JsonValue root;
		if (!ParseJsonDocument(text, root, outError))
		{
			LOG_ERROR(Core, "[ZoneBuilder] Load layout FAILED (path={}, reason={})", layoutPath.string(), outError);
			return false;
		}

		if (root.type != JsonType::Object)
		{
			outError = "layout root must be an object";
			LOG_ERROR(Core, "[ZoneBuilder] Load layout FAILED (path={}, reason={})", layoutPath.string(), outError);
			return false;
		}

		if (!ReadRequiredInt(root, "version", outDocument.version, outError))
		{
			LOG_ERROR(Core, "[ZoneBuilder] Load layout FAILED (path={}, reason={})", layoutPath.string(), outError);
			return false;
		}

		const JsonValue* instancesValue = FindObjectMember(root, "instances");
		if (instancesValue == nullptr || instancesValue->type != JsonType::Array)
		{
			outError = "member 'instances' must be an array";
			LOG_ERROR(Core, "[ZoneBuilder] Load layout FAILED (path={}, reason={})", layoutPath.string(), outError);
			return false;
		}

		outDocument.instances.reserve(instancesValue->arrayValue.size());
		for (size_t instanceIndex = 0; instanceIndex < instancesValue->arrayValue.size(); ++instanceIndex)
		{
			const JsonValue& instanceValue = instancesValue->arrayValue[instanceIndex];
			if (instanceValue.type != JsonType::Object)
			{
				outError = "each instance must be an object";
				LOG_ERROR(Core, "[ZoneBuilder] Load layout FAILED (path={}, index={}, reason={})", layoutPath.string(), instanceIndex, outError);
				return false;
			}

			LayoutInstance instance;
			if (!ReadRequiredString(instanceValue, "guid", instance.guid, outError) ||
				!ReadRequiredString(instanceValue, "gltf", instance.gltfPath, outError) ||
				!ReadRequiredNumberArray3(instanceValue, "position", instance.positionX, instance.positionY, instance.positionZ, outError))
			{
				LOG_ERROR(Core, "[ZoneBuilder] Load layout FAILED (path={}, index={}, reason={})", layoutPath.string(), instanceIndex, outError);
				return false;
			}

			outDocument.instances.emplace_back(std::move(instance));
		}

		LOG_INFO(Core, "[ZoneBuilder] Layout loaded (path={}, version={}, instances={})",
			layoutPath.string(),
			outDocument.version,
			outDocument.instances.size());
		return true;
	}
}
