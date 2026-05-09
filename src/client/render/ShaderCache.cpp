#include "engine/render/ShaderCache.h"

#include <string>

namespace engine::render
{
	std::string ShaderCache::MakeKey(std::string_view path, std::string_view defines)
	{
		if (defines.empty())
		{
			return std::string(path);
		}
		std::string key(path);
		key += "|";
		key.append(defines);
		return key;
	}

	const std::vector<uint32_t>* ShaderCache::Get(std::string_view key) const
	{
		std::lock_guard lock(m_mutex);
		auto it = m_store.find(std::string(key));
		if (it == m_store.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	void ShaderCache::Set(std::string_view key, std::vector<uint32_t> spirv)
	{
		std::lock_guard lock(m_mutex);
		m_store[std::string(key)] = std::move(spirv);
	}

	void ShaderCache::Remove(std::string_view key)
	{
		std::lock_guard lock(m_mutex);
		m_store.erase(std::string(key));
	}
}
