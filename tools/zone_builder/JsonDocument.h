#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tools::zone_builder
{
	enum class JsonType
	{
		Null,
		Bool,
		Number,
		String,
		Object,
		Array
	};

	struct JsonValue
	{
		JsonType type = JsonType::Null;
		bool boolValue = false;
		double numberValue = 0.0;
		std::string stringValue;
		std::unordered_map<std::string, JsonValue> objectValue;
		std::vector<JsonValue> arrayValue;
	};

	/// Parse a JSON document into a minimal DOM tree used by zone_builder.
	bool ParseJsonDocument(std::string_view text, JsonValue& outRoot, std::string& outError);

	/// Return the string name of a JSON type for diagnostics.
	const char* JsonTypeName(JsonType type);

	/// Return an object member or `nullptr` when the key is absent.
	const JsonValue* FindObjectMember(const JsonValue& object, std::string_view key);
}
