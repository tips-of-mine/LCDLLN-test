#include "engine/server/shard/vmap/VMapStreamer.h"

namespace engine::server::shard::vmap
{
	ManagedModel* VMapStreamer::Acquire(std::string_view key)
	{
		const std::string skey(key);
		auto it = m_tiles.find(skey);
		if (it != m_tiles.end())
		{
			it->second->IncRef();
			return it->second.get();
		}

		// Nouveau tile : charger via le loader.
		if (!m_loader)
			return nullptr;
		auto blob = m_loader(key);
		if (blob.empty())
			return nullptr;

		auto model = std::make_unique<ManagedModel>();
		if (!model->LoadFromBuffer(blob))
			return nullptr;

		model->IncRef();
		auto* raw = model.get();
		m_tiles.emplace(skey, std::move(model));
		return raw;
	}

	void VMapStreamer::Release(std::string_view key, TimePoint now)
	{
		auto it = m_tiles.find(std::string(key));
		if (it == m_tiles.end())
			return;
		it->second->DecRef(now);
	}

	size_t VMapStreamer::Tick(TimePoint now)
	{
		size_t freed = 0;
		for (auto it = m_tiles.begin(); it != m_tiles.end(); )
		{
			if (it->second->ShouldRelease(now, m_cfg.releaseDelay))
			{
				it = m_tiles.erase(it);
				++freed;
			}
			else
			{
				++it;
			}
		}
		return freed;
	}
}
