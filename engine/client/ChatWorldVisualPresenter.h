#pragma once

#include "engine/math/Frustum.h"
#include "engine/math/Math.h"
#include "engine/server/ReplicationTypes.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	struct EmoteRelayMessage;
}

namespace engine::client
{
	/// One Say/Yell bubble projected for HUD / future GPU billboard pass (M29.3).
	struct UIChatBubbleBillboard final
	{
		engine::server::EntityId entityId = 0;
		float anchorWorldX = 0.0f;
		float anchorWorldY = 0.0f;
		float anchorWorldZ = 0.0f;
		/// Normalized device coordinates (Vulkan clip / w), roughly in [-1, 1].
		float ndcX = 0.0f;
		float ndcY = 0.0f;
		float alpha = 1.0f;
		std::string text;
	};

	/// Active emote replicated for gameplay / future skeletal playback (M29.3).
	struct UIActiveEmoteEntry final
	{
		engine::server::EntityId entityId = 0;
		uint8_t emoteWireId = 0;
		bool loop = false;
	};

	/// Client-side chat bubbles (3D anchors + billboard projection) and emote state (M29.3).
	class ChatWorldVisualPresenter final
	{
	public:
		ChatWorldVisualPresenter() = default;

		~ChatWorldVisualPresenter();

		/// Initialize presenter state (call once before use).
		bool Init();

		/// Release presenter state.
		void Shutdown();

		/// Clear bubbles, emotes, and entity position cache.
		void Reset();

		/// Advance ages using wall clock since the last call (clamped). Safe to call from network handlers.
		void PumpAge();

		/// Register a Say/Yell line as a world-space bubble above \p senderEntityId.
		void OnChatRelay(uint8_t channelWire, engine::server::EntityId senderEntityId, std::string_view text);

		/// Apply one authoritative emote event.
		void OnEmoteRelay(const engine::server::EmoteRelayMessage& message);

		/// Refresh last known world positions from snapshot entities.
		void SyncEntityPositions(std::span<const engine::server::SnapshotEntity> entities);

		/// Rebuild visible billboard list (frustum cull, capped to the tracked bubble count).
		void RebuildBillboards(
			const engine::math::Vec3& cameraWorld,
			const engine::math::Frustum& frustum,
			const engine::math::Mat4& viewProj,
			uint32_t viewportWidth,
			uint32_t viewportHeight,
			std::vector<UIChatBubbleBillboard>& outBillboards) const;

		/// Copy current emote playback rows for UI / debug.
		void ExportActiveEmotes(std::vector<UIActiveEmoteEntry>& outEmotes) const;

	private:
		struct BubbleEntry final
		{
			engine::server::EntityId entityId = 0;
			std::string text;
			float ageSeconds = 0.0f;
		};

		struct EmoteEntry final
		{
			uint8_t emoteWireId = 0;
			bool loop = false;
			std::chrono::steady_clock::time_point startMono{};
		};

		void EraseBubbleForEntity(engine::server::EntityId entityId);
		void EnforceBubbleCapacity();
		void ExpireEmotesMonotonic();

		std::deque<BubbleEntry> m_bubbles;
		std::unordered_map<engine::server::EntityId, engine::math::Vec3> m_entityWorld;
		std::unordered_map<engine::server::EntityId, EmoteEntry> m_emotes;
		std::chrono::steady_clock::time_point m_lastAgePump{};
		bool m_agePumpInitialized = false;
		bool m_initialized = false;
	};
}
