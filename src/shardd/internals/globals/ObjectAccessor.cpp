#include "src/shardd/internals/globals/ObjectAccessor.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <shared_mutex>

namespace engine::server::shard::globals
{
	namespace
	{
		std::string Lowercase(std::string_view s)
		{
			std::string out;
			out.reserve(s.size());
			for (char c : s)
				out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
			return out;
		}
	}

	bool ObjectAccessor::Register(const EntitySnapshot& snapshot)
	{
		if (snapshot.entityId == 0)
			return false;
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		m_entities[snapshot.entityId] = snapshot;
		return true;
	}

	bool ObjectAccessor::Unregister(EntityId entityId)
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		return m_entities.erase(entityId) > 0;
	}

	std::optional<EntitySnapshot> ObjectAccessor::Find(EntityId entityId) const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		auto it = m_entities.find(entityId);
		if (it == m_entities.end())
			return std::nullopt;
		return it->second;
	}

	std::optional<EntitySnapshot> ObjectAccessor::FindByName(std::string_view name) const
	{
		const std::string needle = Lowercase(name);
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		for (const auto& [id, snap] : m_entities)
		{
			if (Lowercase(snap.name) == needle)
				return snap;
		}
		return std::nullopt;
	}

	size_t ObjectAccessor::Size() const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		return m_entities.size();
	}
}
