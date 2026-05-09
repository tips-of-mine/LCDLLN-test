#include "engine/client/ChatWorldVisualPresenter.h"

#include "engine/core/Log.h"
#include "engine/net/ChatEmotes.h"
#include "engine/net/ChatSystem.h"
#include "engine/server/ReplicationTypes.h"
#include "engine/server/ServerProtocol.h"

#include <algorithm>
#include <cmath>

namespace engine::client
{
	namespace
	{
		constexpr float kBubbleLifetimeSeconds = 4.0f;
		constexpr float kBubbleFadeStartSeconds = 3.0f;
		constexpr size_t kMaxTrackedBubbles = 10;
		constexpr float kHeadOffsetY = 2.0f;
		constexpr float kCullRadiusMeters = 0.35f;

		bool ProjectWorldToNdc(const engine::math::Mat4& vp, float wx, float wy, float wz, float& outNdcX, float& outNdcY)
		{
			const float x = vp.m[0] * wx + vp.m[4] * wy + vp.m[8] * wz + vp.m[12];
			const float y = vp.m[1] * wx + vp.m[5] * wy + vp.m[9] * wz + vp.m[13];
			const float w = vp.m[3] * wx + vp.m[7] * wy + vp.m[11] * wz + vp.m[15];
			if (w <= 1.0e-4f)
			{
				return false;
			}

			outNdcX = x / w;
			outNdcY = y / w;
			return true;
		}
	}

	ChatWorldVisualPresenter::~ChatWorldVisualPresenter()
	{
		Shutdown();
	}

	bool ChatWorldVisualPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[ChatWorldVisual] Init ignored: already initialized");
			return true;
		}

		m_initialized = true;
		LOG_INFO(Net, "[ChatWorldVisual] Init OK (bubbles_max={}, lifetime_s={})", kMaxTrackedBubbles, kBubbleLifetimeSeconds);
		return true;
	}

	void ChatWorldVisualPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		Reset();
		m_initialized = false;
		m_agePumpInitialized = false;
		LOG_INFO(Net, "[ChatWorldVisual] Destroyed");
	}

	void ChatWorldVisualPresenter::Reset()
	{
		m_bubbles.clear();
		m_entityWorld.clear();
		m_emotes.clear();
	}

	void ChatWorldVisualPresenter::PumpAge()
	{
		const auto now = std::chrono::steady_clock::now();
		float dt = 0.0f;
		if (m_agePumpInitialized)
		{
			dt = std::chrono::duration<float>(now - m_lastAgePump).count();
			dt = std::min(dt, 0.25f);
		}

		m_lastAgePump = now;
		m_agePumpInitialized = true;

		if (dt > 0.0f)
		{
			for (BubbleEntry& bubble : m_bubbles)
			{
				bubble.ageSeconds += dt;
			}

			while (!m_bubbles.empty() && m_bubbles.front().ageSeconds >= kBubbleLifetimeSeconds)
			{
				m_bubbles.pop_front();
			}
		}

		ExpireEmotesMonotonic();
	}

	void ChatWorldVisualPresenter::EraseBubbleForEntity(engine::server::EntityId entityId)
	{
		const auto it = std::remove_if(
			m_bubbles.begin(),
			m_bubbles.end(),
			[entityId](const BubbleEntry& b)
			{
				return b.entityId == entityId;
			});
		m_bubbles.erase(it, m_bubbles.end());
	}

	void ChatWorldVisualPresenter::EnforceBubbleCapacity()
	{
		while (m_bubbles.size() > kMaxTrackedBubbles)
		{
			m_bubbles.pop_front();
		}
	}

	void ChatWorldVisualPresenter::ExpireEmotesMonotonic()
	{
		const auto now = std::chrono::steady_clock::now();
		for (auto it = m_emotes.begin(); it != m_emotes.end();)
		{
			engine::net::ChatEmoteWireId emoteEnum = engine::net::ChatEmoteWireId::None;
			if (!engine::net::TryFromWire(it->second.emoteWireId, emoteEnum))
			{
				it = m_emotes.erase(it);
				continue;
			}

			if (it->second.loop)
			{
				++it;
				continue;
			}

			const float dur = engine::net::ChatEmoteOneShotDurationSeconds(emoteEnum);
			if (dur <= 0.0f)
			{
				++it;
				continue;
			}

			const float elapsed = std::chrono::duration<float>(now - it->second.startMono).count();
			if (elapsed >= dur)
			{
				it = m_emotes.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void ChatWorldVisualPresenter::OnChatRelay(
		uint8_t channelWire,
		engine::server::EntityId senderEntityId,
		std::string_view text)
	{
		if (!m_initialized)
		{
			LOG_WARN(Net, "[ChatWorldVisual] OnChatRelay ignored: not initialized");
			return;
		}

		const engine::net::ChatChannel say = engine::net::ChatChannel::Say;
		const engine::net::ChatChannel yell = engine::net::ChatChannel::Yell;
		if (channelWire != engine::net::ToWire(say) && channelWire != engine::net::ToWire(yell))
		{
			return;
		}

		if (text.empty())
		{
			return;
		}

		std::string trimmed(text);
		if (trimmed.size() > 96)
		{
			trimmed.resize(96);
		}

		EraseBubbleForEntity(senderEntityId);
		BubbleEntry bubble{};
		bubble.entityId = senderEntityId;
		bubble.text = std::move(trimmed);
		bubble.ageSeconds = 0.0f;
		m_bubbles.push_back(std::move(bubble));
		EnforceBubbleCapacity();
		LOG_DEBUG(Net,
			"[ChatWorldVisual] Bubble queued (entity_id={}, channel_wire={}, len={})",
			senderEntityId,
			channelWire,
			m_bubbles.back().text.size());
	}

	void ChatWorldVisualPresenter::OnEmoteRelay(const engine::server::EmoteRelayMessage& message)
	{
		if (!m_initialized)
		{
			LOG_WARN(Net, "[ChatWorldVisual] OnEmoteRelay ignored: not initialized");
			return;
		}

		engine::net::ChatEmoteWireId emoteEnum = engine::net::ChatEmoteWireId::None;
		if (!engine::net::TryFromWire(message.emoteId, emoteEnum))
		{
			LOG_WARN(Net, "[ChatWorldVisual] OnEmoteRelay ignored: invalid emote_id ({})", message.emoteId);
			return;
		}

		EmoteEntry row{};
		row.emoteWireId = message.emoteId;
		row.loop = (message.flags & 1u) != 0;
		row.startMono = std::chrono::steady_clock::now();
		m_emotes[message.actorEntityId] = row;
		LOG_DEBUG(Net,
			"[ChatWorldVisual] Emote applied (entity_id={}, emote={}, loop={}, server_tick={})",
			message.actorEntityId,
			engine::net::ChatEmoteName(emoteEnum),
			row.loop ? "yes" : "no",
			message.serverTick);
	}

	void ChatWorldVisualPresenter::SyncEntityPositions(std::span<const engine::server::SnapshotEntity> entities)
	{
		if (!m_initialized)
		{
			return;
		}

		for (const engine::server::SnapshotEntity& entity : entities)
		{
			m_entityWorld[entity.entityId] = engine::math::Vec3(
				entity.state.positionX,
				entity.state.positionY,
				entity.state.positionZ);
		}
	}

	void ChatWorldVisualPresenter::RebuildBillboards(
		const engine::math::Vec3& cameraWorld,
		const engine::math::Frustum& frustum,
		const engine::math::Mat4& viewProj,
		uint32_t viewportWidth,
		uint32_t viewportHeight,
		std::vector<UIChatBubbleBillboard>& outBillboards) const
	{
		outBillboards.clear();
		if (!m_initialized)
		{
			LOG_WARN(Net, "[ChatWorldVisual] RebuildBillboards skipped: not initialized");
			return;
		}

		if (viewportWidth == 0 || viewportHeight == 0)
		{
			LOG_TRACE(Net, "[ChatWorldVisual] RebuildBillboards skipped: viewport not ready");
			return;
		}

		struct Sortable
		{
			UIChatBubbleBillboard data{};
			float distanceSq = 0.0f;
		};

		std::vector<Sortable> scratch;
		scratch.reserve(m_bubbles.size());

		for (const BubbleEntry& bubble : m_bubbles)
		{
			if (bubble.ageSeconds >= kBubbleLifetimeSeconds)
			{
				continue;
			}

			const auto it = m_entityWorld.find(bubble.entityId);
			if (it == m_entityWorld.end())
			{
				continue;
			}

			const engine::math::Vec3 anchor = it->second + engine::math::Vec3(0.0f, kHeadOffsetY, 0.0f);
			const engine::math::Vec3 margin(kCullRadiusMeters, kCullRadiusMeters, kCullRadiusMeters);
			if (!frustum.TestAABB(anchor - margin, anchor + margin))
			{
				continue;
			}

			float ndcX = 0.0f;
			float ndcY = 0.0f;
			if (!ProjectWorldToNdc(viewProj, anchor.x, anchor.y, anchor.z, ndcX, ndcY))
			{
				continue;
			}

			float alpha = 1.0f;
			if (bubble.ageSeconds >= kBubbleFadeStartSeconds)
			{
				const float t = (bubble.ageSeconds - kBubbleFadeStartSeconds)
					/ std::max(1.0e-4f, kBubbleLifetimeSeconds - kBubbleFadeStartSeconds);
				alpha = std::max(0.0f, 1.0f - t);
			}

			if (alpha <= 0.01f)
			{
				continue;
			}

			Sortable row{};
			row.data.entityId = bubble.entityId;
			row.data.anchorWorldX = anchor.x;
			row.data.anchorWorldY = anchor.y;
			row.data.anchorWorldZ = anchor.z;
			row.data.ndcX = ndcX;
			row.data.ndcY = ndcY;
			row.data.alpha = alpha;
			row.data.text = bubble.text;
			const engine::math::Vec3 delta = anchor - cameraWorld;
			row.distanceSq = delta.LengthSq();
			scratch.push_back(std::move(row));
		}

		std::sort(
			scratch.begin(),
			scratch.end(),
			[](const Sortable& a, const Sortable& b)
			{
				return a.distanceSq < b.distanceSq;
			});

		const size_t maxVisible = std::min(kMaxTrackedBubbles, scratch.size());
		outBillboards.reserve(maxVisible);
		for (size_t i = 0; i < maxVisible; ++i)
		{
			outBillboards.push_back(std::move(scratch[i].data));
		}

		LOG_TRACE(Net, "[ChatWorldVisual] RebuildBillboards OK (visible={}, tracked={})", outBillboards.size(), m_bubbles.size());
	}

	void ChatWorldVisualPresenter::ExportActiveEmotes(std::vector<UIActiveEmoteEntry>& outEmotes) const
	{
		outEmotes.clear();
		outEmotes.reserve(m_emotes.size());
		for (const auto& pair : m_emotes)
		{
			UIActiveEmoteEntry row{};
			row.entityId = pair.first;
			row.emoteWireId = pair.second.emoteWireId;
			row.loop = pair.second.loop;
			outEmotes.push_back(row);
		}

		std::sort(
			outEmotes.begin(),
			outEmotes.end(),
			[](const UIActiveEmoteEntry& a, const UIActiveEmoteEntry& b)
			{
				return a.entityId < b.entityId;
			});
	}
}
