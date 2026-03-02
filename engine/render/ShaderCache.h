#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::render
{
	/// Runtime shader cache: key = path + defines, value = SPIR-V binary. Thread-safe.
	class ShaderCache final
	{
	public:
		/// Builds a deterministic cache key from relative path and optional defines string.
		static std::string MakeKey(std::string_view path, std::string_view defines = {});

		/// Returns cached SPIR-V for the key, or nullptr if not found.
		const std::vector<uint32_t>* Get(std::string_view key) const;

		/// Stores SPIR-V for the key. Replaces existing entry (fallback: last valid version).
		void Set(std::string_view key, std::vector<uint32_t> spirv);

		/// Removes the entry for the key, if present.
		void Remove(std::string_view key);

	private:
		mutable std::mutex m_mutex;
		std::unordered_map<std::string, std::vector<uint32_t>> m_store;
	};
}
