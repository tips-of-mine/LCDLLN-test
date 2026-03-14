#pragma once

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
		UIModelChangeDebugDump = 1u << 6
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
		uint32_t stateFlags = 0;
		uint32_t zoneId = 0;
		float positionX = 0.0f;
		float positionY = 0.0f;
		float positionZ = 0.0f;
		bool hasMana = false;
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
		size_t m_nextObserverHandle = 1;
		bool m_initialized = false;
	};
}
