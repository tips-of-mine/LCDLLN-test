#pragma once

#include "engine/client/ChatWorldVisualPresenter.h"
#include "engine/math/Frustum.h"
#include "engine/math/Math.h"
#include "engine/server/ServerProtocol.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace engine::client
{
	/// Bitmask describing which UI model section changed after one network packet was applied.
	enum UIModelChange : uint32_t
	{
		UIModelChangeNone = 0u,
		UIModelChangeStats = 1u << 0,
		UIModelChangeInventory = 1u << 1,
		UIModelChangeQuests = 1u << 2,
		UIModelChangeEvents = 1u << 3,
		UIModelChangeCombat = 1u << 4,
		UIModelChangeWorld = 1u << 5,
		UIModelChangeDebugDump = 1u << 6,
		UIModelChangeChat = 1u << 7,
		/// M29.3: world-space chat bubbles + emote playback state.
		UIModelChangeChatWorld = 1u << 8,
		/// M32.2: party composition, member HP/mana, loot mode.
		UIModelChangeParty     = 1u << 9,
		/// M35.1 — wallet balances (gold, honor, badges, premium).
		UIModelChangeWallet    = 1u << 10,
		/// M35.2 — vendor shop panel (offers, prices, stock wire).
		UIModelChangeShop      = 1u << 11
	};

	/// M35.2 — one vendor offer row mirrored from \ref engine::server::ShopOfferWire.
	struct UIShopOfferLine
	{
		uint32_t itemId = 0;
		uint32_t buyPrice = 0;
		/// Same semantics as wire: \ref engine::server::kShopInfiniteStockWire when unlimited.
		uint32_t stock = 0;
	};

	/// M35.2 — vendor shop snapshot pushed by \ref engine::server::ShopOpenMessage.
	struct UIShopPanel
	{
		uint32_t vendorId = 0;
		std::string displayName;
		std::vector<UIShopOfferLine> offers;
		bool isOpen = false;
	};

	/// M35.1 — wallet balances replicated from \ref WalletUpdateMessage.
	struct UIWalletState
	{
		uint32_t gold = 0;
		uint32_t honor = 0;
		uint32_t badges = 0;
		uint32_t premiumCurrency = 0;
		bool hasWallet = false;
	};

	/// Player-focused runtime stats mirrored from server-authoritative packets.
	struct UIPlayerStats
	{
		uint32_t clientId = 0;
		engine::server::EntityId playerEntityId = 0;
		uint32_t serverTick = 0;
		uint16_t connectedClients = 0;
		uint16_t entityCount = 0;
		uint32_t receivedPackets = 0;
		uint32_t sentPackets = 0;
		uint32_t currentHealth = 0;
		uint32_t maxHealth = 0;
		uint32_t currentMana = 0;
		uint32_t maxMana = 0;
		uint32_t comboPoints = 0;
		uint32_t maxCombo = 0;
		uint32_t stateFlags = 0;
		uint32_t zoneId = 0;
		float positionX = 0.0f;
		float positionY = 0.0f;
		float positionZ = 0.0f;
		bool hasMana = false;
		bool hasCombo = false;
		bool hasSnapshot = false;
	};

	/// Current combat target resolved from the latest player-originated combat event.
	struct UITargetStats
	{
		engine::server::EntityId entityId = 0;
		uint32_t currentHealth = 0;
		uint32_t maxHealth = 0;
		uint32_t stateFlags = 0;
		float positionX = 0.0f;
		float positionY = 0.0f;
		float positionZ = 0.0f;
		bool hasTarget = false;
		bool hasPosition = false;
	};

	/// One recent combat event retained for the HUD combat log.
	struct UICombatLogEntry
	{
		engine::server::EntityId attackerEntityId = 0;
		engine::server::EntityId targetEntityId = 0;
		uint32_t damage = 0;
		bool playerWasAttacker = false;
		bool playerWasTarget = false;
		uint64_t sequence = 0;
	};

	/// One quest step displayed by the UI model.
	struct UIQuestStep
	{
		uint8_t stepType = 0;
		std::string targetId;
		uint32_t currentCount = 0;
		uint32_t requiredCount = 0;
	};

	/// One quest entry displayed by the UI model.
	struct UIQuestEntry
	{
		uint8_t status = 0;
		std::string questId;
		uint32_t rewardExperience = 0;
		uint32_t rewardGold = 0;
		std::vector<UIQuestStep> steps;
		std::vector<engine::server::ItemStack> rewardItems;
	};

	/// One chat line mirrored from \ref engine::server::ChatRelayMessage (M29.1).
	struct UIChatLineEntry
	{
		uint8_t channelWire = 0;
		uint64_t timestampUnixMs = 0;
		std::string sender;
		std::string text;
	};

	/// One party member entry mirrored from a PartyUpdate server packet (M32.2).
	struct UIPartyMemberEntry
	{
		uint32_t    clientId      = 0;
		uint32_t    currentHealth = 0;
		uint32_t    maxHealth     = 0;
		uint32_t    currentMana   = 0;
		uint32_t    maxMana       = 0;
		std::string displayName;
		bool        isLeader      = false;
	};

	/// One dynamic event entry displayed by the UI model.
	struct UIEventEntry
	{
		uint32_t zoneId = 0;
		uint8_t status = 0;
		uint16_t phaseIndex = 0;
		uint16_t phaseCount = 0;
		uint32_t progressCurrent = 0;
		uint32_t progressRequired = 0;
		std::string eventId;
		std::string notificationText;
		uint32_t rewardExperience = 0;
		uint32_t rewardGold = 0;
		std::vector<engine::server::ItemStack> rewardItems;
	};

	/// Pure data model consumed by UI views and debug panels.
	struct UIModel
	{
		UIPlayerStats playerStats{};
		UITargetStats targetStats{};
		std::vector<engine::server::ItemStack> inventory;
		std::vector<UIQuestEntry> quests;
		std::vector<UIEventEntry> events;
		std::vector<UICombatLogEntry> combatLog;
		std::vector<UIChatLineEntry> chatLines;
		/// M29.3: Say/Yell bubbles projected for HUD / billboard renderer.
		std::vector<UIChatBubbleBillboard> chatBubbleBillboards;
		/// M29.3: Active emote rows — `emoteWireId` maps to future skeletal clips when an animation stack exists.
		std::vector<UIActiveEmoteEntry> activeEmotes;
		/// M32.2: Party member entries (up to 5), populated from PartyUpdate packets.
		std::vector<UIPartyMemberEntry> partyMembers;
		/// M32.2: True when the local player is currently in a party.
		bool        inParty          = false;
		/// M32.2: Human-readable loot mode label received in the last PartyUpdate (e.g. "FreeForAll").
		std::string partyLootModeLabel;
		/// M32.2: party leader clientId (0 when not in a party).
		uint32_t    partyLeaderId    = 0;
		/// M35.1 — multi-currency wallet snapshot for HUD/debug.
		UIWalletState wallet{};
		/// M35.2 — last opened vendor shop (debug / HUD presenter).
		UIShopPanel shop{};
		std::string debugDump;

		/// Build a text dump suitable for a debug widget or logs.
		std::string BuildDebugDump() const;
	};

	/// Observer callback notified after one or more UI model sections changed.
	using UIModelObserver = std::function<void(const UIModel& model, uint32_t changeMask)>;

	/// Bind server-authoritative packets to a main-thread-owned UI data model.
	class UIModelBinding final
	{
	public:
		/// Construct an uninitialized binding.
		UIModelBinding() = default;

		/// Release binding resources.
		~UIModelBinding();

		/// Initialize the binding and capture the owner thread.
		bool Init();

		/// Shutdown the binding and release observers/scratch buffers.
		void Shutdown();

		/// Clear the model state while preserving reusable allocations.
		bool Reset();

		/// Register one observer callback and return its stable handle.
		size_t AddObserver(UIModelObserver observer);

		/// Remove one observer callback by handle.
		bool RemoveObserver(size_t observerHandle);

		/// Apply one server packet to the UI model.
		bool ApplyPacket(std::span<const std::byte> packet);

		/// Return the current immutable UI model snapshot.
		const UIModel& GetModel() const { return m_model; }

		/// M29.3: Refresh billboard projections (call from render/game thread with camera + viewport).
		void TickChatWorldVisuals(
			const engine::math::Vec3& cameraWorld,
			const engine::math::Frustum& frustum,
			const engine::math::Mat4& viewProj,
			uint32_t viewportWidth,
			uint32_t viewportHeight);

	private:
		struct ObserverSlot
		{
			size_t handle = 0;
			UIModelObserver callback;
		};

		/// Validate that the current call happens on the owner thread captured at Init.
		bool ValidateMainThread(const char* operation) const;

		/// Notify observers after rebuilding the debug dump.
		void NotifyObservers(uint32_t changeMask);

		/// Apply one decoded snapshot to the stats section of the UI model.
		bool ApplySnapshot(std::span<const std::byte> packet);

		/// Apply one decoded combat event to the stats section of the UI model.
		bool ApplyCombatEvent(std::span<const std::byte> packet);

		/// Apply one decoded zone change packet to the world section of the UI model.
		bool ApplyZoneChange(std::span<const std::byte> packet);

		/// Apply one decoded inventory delta to the inventory section of the UI model.
		bool ApplyInventoryDelta(std::span<const std::byte> packet);

		/// Apply one decoded quest delta to the quest section of the UI model.
		bool ApplyQuestDelta(std::span<const std::byte> packet);

		/// Apply one decoded dynamic event state message to the event section of the UI model.
		bool ApplyEventState(std::span<const std::byte> packet);

		/// Apply one decoded chat relay message to the chat log (M29.1).
		bool ApplyChatRelay(std::span<const std::byte> packet);

		/// Apply one decoded emote relay message (M29.3).
		bool ApplyEmoteRelay(std::span<const std::byte> packet);

		/// Apply one decoded PartyUpdate message to the party section of the UI model (M32.2).
		bool ApplyPartyUpdate(std::span<const std::byte> packet);

		/// Apply one decoded WalletUpdate message (M35.1).
		bool ApplyWalletUpdate(std::span<const std::byte> packet);

		/// Apply one decoded ShopOpen message (M35.2).
		bool ApplyShopOpen(std::span<const std::byte> packet);

		/// Advance world presenter ages (wall clock clamped).
		void PumpWorldPresenterAge();

		UIModel m_model{};
		std::thread::id m_ownerThread{};
		std::vector<ObserverSlot> m_observers;
		std::vector<engine::server::SnapshotEntity> m_snapshotScratch;
		std::vector<engine::server::ItemStack> m_inventoryScratch;
		engine::server::SnapshotMessage m_snapshotMessage{};
		engine::server::CombatEventMessage m_combatEventMessage{};
		engine::server::ZoneChangeMessage m_zoneChangeMessage{};
		engine::server::InventoryDeltaMessage m_inventoryMessage{};
		engine::server::QuestDeltaMessage m_questMessage{};
		engine::server::EventStateMessage m_eventMessage{};
		engine::server::ChatRelayMessage m_chatRelayScratch{};
		engine::server::EmoteRelayMessage m_emoteRelayScratch{};
		engine::server::WalletUpdateMessage m_walletScratch{};
		engine::server::ShopOpenMessage m_shopOpenScratch{};
		ChatWorldVisualPresenter m_chatWorld{};
		size_t m_nextObserverHandle = 1;
		bool m_initialized = false;
	};
}
