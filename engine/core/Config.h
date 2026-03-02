#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace engine::core
{
	class Config final
	{
	public:
		using Value = std::variant<std::monostate, bool, int64_t, double, std::string>;

		/// Create an empty config (use `SetDefault` to seed defaults).
		Config() = default;

		/// Load config from a JSON/INI file (if present) and apply CLI overrides (`--key=value`).
		static Config Load(std::string_view filePath, int argc, char** argv);

		/// Set a default value (used if no file/override sets the key).
		void SetDefault(std::string_view key, Value value);

		/// Load values from a JSON/INI file (returns false if file is missing/unreadable).
		bool LoadFromFile(std::string_view filePath);

		/// Apply CLI overrides of the form `--key=value` (highest priority).
		void ApplyCli(int argc, char** argv);

		/// True if the key exists after merges.
		bool Has(std::string_view key) const;

		/// Get a string value or return `fallback` if missing/not convertible.
		std::string GetString(std::string_view key, std::string_view fallback = {}) const;

		/// Get an int64 value or return `fallback` if missing/not convertible.
		int64_t GetInt(std::string_view key, int64_t fallback = 0) const;

		/// Get a double value or return `fallback` if missing/not convertible.
		double GetDouble(std::string_view key, double fallback = 0.0) const;

		/// Get a bool value or return `fallback` if missing/not convertible.
		bool GetBool(std::string_view key, bool fallback = false) const;

		/// Set a value explicitly (used by parsers and CLI overrides).
		void SetValue(std::string_view key, Value value);

		/// Parse a string as a scalar value (used by INI/CLI parsers).
		static std::optional<Value> ParseScalar(std::string_view text);

	private:
		static std::string ToOwnedKey(std::string_view key);

		std::unordered_map<std::string, Value> m_values;
	};
}

