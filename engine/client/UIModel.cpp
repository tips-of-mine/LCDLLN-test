#include "engine/client/UIModel.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace engine::client
{
	namespace
	{
		inline constexpr size_t kMaxCombatLogEntries = 5;

		/// Return the quest entry matching the given id, or null when missing.
		UIQuestEntry* FindQuest(std::vector<UIQuestEntry>& quests, std::string_view questId)
		{
			for (UIQuestEntry& quest : quests)
			{
				if (quest.questId == questId)
				{
					return &quest;
				}
			}

			return nullptr;
		}

		/// Return the event entry matching the given id, or null when missing.
		UIEventEntry* FindEvent(std::vector<UIEventEntry>& events, std::string_view eventId)
		{
			for (UIEventEntry& eventEntry : events)
			{
				if (eventEntry.eventId == eventId)
				{
					return &eventEntry;
				}
			}

			return nullptr;
		}

		/// Append one item stack list to a text debug dump.
		void AppendItemStacks(std::string& outText, std::span<const engine::server::ItemStack> items)
		{
			outText += "[";
			for (size_t index = 0; index < items.size(); ++index)
			{
				if (index > 0)
				{
					outText += ", ";
				}

				outText += "{id=";
				outText += std::to_string(items[index].itemId);
				outText += ", qty=";
				outText += std::to_string(items[index].quantity);
				outText += "}";
			}
			outText += "]";
		}

		/// Append one combat event to the retained HUD combat log.
		void PushCombatLogEntry(
			std::vector<UICombatLogEntry>& combatLog,
			const engine::server::CombatEventMessage& message,
			engine::server::EntityId playerEntityId)
		{
			UICombatLogEntry entry{};
			entry.attackerEntityId = message.attackerEntityId;
			entry.targetEntityId = message.targetEntityId;
			entry.damage = message.damage;
			entry.playerWasAttacker = (message.attackerEntityId == playerEntityId);
			entry.playerWasTarget = (message.targetEntityId == playerEntityId);
			entry.sequence = combatLog.empty() ? 1u : (combatLog.back().sequence + 1u);
			combatLog.push_back(entry);
			if (combatLog.size() > kMaxCombatLogEntries)
			{
				combatLog.erase(combatLog.begin(), combatLog.begin() + static_cast<std::ptrdiff_t>(combatLog.size() - kMaxCombatLogEntries));
			}
		}
	}

	std::string UIModel::BuildDebugDump() const
	{
		std::string dump;
		dump.reserve(256 + (inventory.size() * 24) + (quests.size() * 96) + (events.size() * 96));
		dump += "[UIModel]\n";
		dump += "stats: client=";
		dump += std::to_string(playerStats.clientId);
		dump += " entity=";
		dump += std::to_string(playerStats.playerEntityId);
		dump += " hp=";
		dump += std::to_string(playerStats.currentHealth);
		dump += "/";
		dump += std::to_string(playerStats.maxHealth);
		dump += " mana=";
		dump += std::to_string(playerStats.currentMana);
		dump += "/";
		dump += std::to_string(playerStats.maxMana);
		dump += " tick=";
		dump += std::to_string(playerStats.serverTick);
		dump += " clients=";
		dump += std::to_string(playerStats.connectedClients);
		dump += " entities=";
		dump += std::to_string(playerStats.entityCount);
		dump += " snapshot=";
		dump += playerStats.hasSnapshot ? "true" : "false";
		dump += "\n";

		dump += "inventory(";
		dump += std::to_string(inventory.size());
		dump += ")=";
		AppendItemStacks(dump, inventory);
		dump += "\n";

		dump += "quests(";
		dump += std::to_string(quests.size());
		dump += ")\n";
		for (const UIQuestEntry& quest : quests)
		{
			dump += " - ";
			dump += quest.questId;
			dump += " status=";
			dump += std::to_string(quest.status);
			dump += " steps=";
			dump += std::to_string(quest.steps.size());
			dump += " rewards=";
			AppendItemStacks(dump, quest.rewardItems);
			dump += "\n";
		}

		dump += "events(";
		dump += std::to_string(events.size());
		dump += ")\n";
		for (const UIEventEntry& eventEntry : events)
		{
			dump += " - ";
			dump += eventEntry.eventId;
			dump += " status=";
			dump += std::to_string(eventEntry.status);
			dump += " phase=";
			dump += std::to_string(eventEntry.phaseIndex);
			dump += "/";
			dump += std::to_string(eventEntry.phaseCount);
			dump += " progress=";
			dump += std::to_string(eventEntry.progressCurrent);
			dump += "/";
			dump += std::to_string(eventEntry.progressRequired);
			dump += " rewards=";
			AppendItemStacks(dump, eventEntry.rewardItems);
			dump += "\n";
		}

		dump += "target: id=";
		dump += std::to_string(targetStats.entityId);
		dump += " hp=";
		dump += std::to_string(targetStats.currentHealth);
		dump += "/";
		dump += std::to_string(targetStats.maxHealth);
		dump += " active=";
		dump += targetStats.hasTarget ? "true" : "false";
		dump += "\n";

		dump += "combat(";
		dump += std::to_string(combatLog.size());
		dump += ")\n";
		for (const UICombatLogEntry& entry : combatLog)
		{
			dump += " - seq=";
			dump += std::to_string(entry.sequence);
			dump += " attacker=";
			dump += std::to_string(entry.attackerEntityId);
			dump += " target=";
			dump += std::to_string(entry.targetEntityId);
			dump += " damage=";
			dump += std::to_string(entry.damage);
			dump += " player_role=";
			if (entry.playerWasAttacker)
			{
				dump += "attacker";
			}
			else if (entry.playerWasTarget)
			{
				dump += "target";
			}
			else
			{
				dump += "other";
			}
			dump += "\n";
		}

		return dump;
	}

	UIModelBinding::~UIModelBinding()
	{
		Shutdown();
	}

	bool UIModelBinding::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[UIModelBinding] Init ignored: already initialized");
			return true;
		}

		m_ownerThread = std::this_thread::get_id();
		m_initialized = true;
		m_model.debugDump = m_model.BuildDebugDump();
		LOG_INFO(Net, "[UIModelBinding] Init OK");
		return true;
	}

	void UIModelBinding::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_initialized = false;
		m_observers.clear();
		m_snapshotScratch.clear();
		m_inventoryScratch.clear();
		m_model = {};
		LOG_INFO(Net, "[UIModelBinding] Destroyed");
	}

	bool UIModelBinding::Reset()
	{
		if (!m_initialized)
		{
			LOG_ERROR(Net, "[UIModelBinding] Reset FAILED: binding not initialized");
			return false;
		}

		if (!ValidateMainThread("Reset"))
		{
			return false;
		}

		m_model.playerStats = {};
		m_model.targetStats = {};
		m_model.inventory.clear();
		m_model.quests.clear();
		m_model.events.clear();
		m_model.combatLog.clear();
		NotifyObservers(UIModelChangeStats | UIModelChangeInventory | UIModelChangeQuests | UIModelChangeEvents | UIModelChangeCombat);
		LOG_INFO(Net, "[UIModelBinding] Reset OK");
		return true;
	}

	size_t UIModelBinding::AddObserver(UIModelObserver observer)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Net, "[UIModelBinding] AddObserver FAILED: binding not initialized");
			return 0;
		}

		if (!ValidateMainThread("AddObserver"))
		{
			return 0;
		}

		if (!observer)
		{
			LOG_WARN(Net, "[UIModelBinding] AddObserver ignored: empty callback");
			return 0;
		}

		const size_t handle = m_nextObserverHandle++;
		m_observers.push_back(ObserverSlot{ handle, std::move(observer) });
		LOG_INFO(Net, "[UIModelBinding] Observer added (handle={}, count={})", handle, m_observers.size());
		return handle;
	}

	bool UIModelBinding::RemoveObserver(size_t observerHandle)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Net, "[UIModelBinding] RemoveObserver FAILED: binding not initialized");
			return false;
		}

		if (!ValidateMainThread("RemoveObserver"))
		{
			return false;
		}

		const auto it = std::find_if(m_observers.begin(), m_observers.end(),
			[observerHandle](const ObserverSlot& slot)
			{
				return slot.handle == observerHandle;
			});
		if (it == m_observers.end())
		{
			LOG_WARN(Net, "[UIModelBinding] RemoveObserver ignored: unknown handle {}", observerHandle);
			return false;
		}

		m_observers.erase(it);
		LOG_INFO(Net, "[UIModelBinding] Observer removed (handle={}, count={})", observerHandle, m_observers.size());
		return true;
	}

	bool UIModelBinding::ApplyPacket(std::span<const std::byte> packet)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Net, "[UIModelBinding] ApplyPacket FAILED: binding not initialized");
			return false;
		}

		if (!ValidateMainThread("ApplyPacket"))
		{
			return false;
		}

		engine::server::MessageKind kind{};
		if (!engine::server::PeekMessageKind(packet, kind))
		{
			LOG_WARN(Net, "[UIModelBinding] ApplyPacket FAILED: invalid header");
			return false;
		}

		switch (kind)
		{
		case engine::server::MessageKind::Snapshot:
			return ApplySnapshot(packet);
		case engine::server::MessageKind::CombatEvent:
			return ApplyCombatEvent(packet);
		case engine::server::MessageKind::InventoryDelta:
			return ApplyInventoryDelta(packet);
		case engine::server::MessageKind::QuestDelta:
			return ApplyQuestDelta(packet);
		case engine::server::MessageKind::EventState:
			return ApplyEventState(packet);
		default:
			LOG_WARN(Net, "[UIModelBinding] ApplyPacket ignored: unsupported message kind {}", static_cast<uint16_t>(kind));
			return false;
		}
	}

	bool UIModelBinding::ValidateMainThread(const char* operation) const
	{
		if (std::this_thread::get_id() == m_ownerThread)
		{
			return true;
		}

		LOG_ERROR(Net, "[UIModelBinding] {} FAILED: wrong thread", operation);
		return false;
	}

	void UIModelBinding::NotifyObservers(uint32_t changeMask)
	{
		const uint32_t notifyMask = changeMask | UIModelChangeDebugDump;
		m_model.debugDump = m_model.BuildDebugDump();
		for (const ObserverSlot& slot : m_observers)
		{
			if (slot.callback)
			{
				slot.callback(m_model, notifyMask);
			}
		}
	}

	bool UIModelBinding::ApplySnapshot(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeSnapshot(packet, m_snapshotMessage, m_snapshotScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] Snapshot FAILED: decode error");
			return false;
		}

		UIPlayerStats& stats = m_model.playerStats;
		stats.clientId = m_snapshotMessage.clientId;
		stats.playerEntityId = static_cast<engine::server::EntityId>(m_snapshotMessage.clientId);
		stats.serverTick = m_snapshotMessage.serverTick;
		stats.connectedClients = m_snapshotMessage.connectedClients;
		stats.entityCount = m_snapshotMessage.entityCount;
		stats.receivedPackets = m_snapshotMessage.receivedPackets;
		stats.sentPackets = m_snapshotMessage.sentPackets;
		stats.hasSnapshot = false;

		for (const engine::server::SnapshotEntity& entity : m_snapshotScratch)
		{
			if (entity.entityId != stats.playerEntityId)
			{
				continue;
			}

			stats.currentHealth = entity.state.currentHealth;
			stats.maxHealth = entity.state.maxHealth;
			stats.stateFlags = entity.state.stateFlags;
			stats.hasSnapshot = true;
			break;
		}

		if (!stats.hasSnapshot)
		{
			LOG_WARN(Net, "[UIModelBinding] Snapshot applied without player entity (client_id={}, entities={})",
				stats.clientId,
				m_snapshotScratch.size());
		}
		else
		{
			LOG_INFO(Net, "[UIModelBinding] Snapshot applied (client_id={}, hp={}/{}, entities={})",
				stats.clientId,
				stats.currentHealth,
				stats.maxHealth,
				m_snapshotScratch.size());
		}

		NotifyObservers(UIModelChangeStats);
		return true;
	}

	bool UIModelBinding::ApplyCombatEvent(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeCombatEvent(packet, m_combatEventMessage))
		{
			LOG_WARN(Net, "[UIModelBinding] CombatEvent FAILED: decode error");
			return false;
		}

		const engine::server::EntityId playerEntityId = m_model.playerStats.playerEntityId;
		if (playerEntityId == 0)
		{
			LOG_DEBUG(Net, "[UIModelBinding] CombatEvent ignored: player entity not known yet");
			return true;
		}

		const bool playerWasAttacker = (m_combatEventMessage.attackerEntityId == playerEntityId);
		const bool playerWasTarget = (m_combatEventMessage.targetEntityId == playerEntityId);
		if (!playerWasAttacker && !playerWasTarget)
		{
			LOG_DEBUG(Net, "[UIModelBinding] CombatEvent ignored: unrelated target {}", m_combatEventMessage.targetEntityId);
			return true;
		}

		uint32_t changeMask = UIModelChangeCombat;
		if (playerWasTarget)
		{
			m_model.playerStats.currentHealth = m_combatEventMessage.targetCurrentHealth;
			m_model.playerStats.maxHealth = m_combatEventMessage.targetMaxHealth;
			m_model.playerStats.stateFlags = m_combatEventMessage.targetStateFlags;
			changeMask |= UIModelChangeStats;
		}

		if (playerWasAttacker && m_combatEventMessage.targetEntityId != playerEntityId)
		{
			m_model.targetStats.entityId = m_combatEventMessage.targetEntityId;
			m_model.targetStats.currentHealth = m_combatEventMessage.targetCurrentHealth;
			m_model.targetStats.maxHealth = m_combatEventMessage.targetMaxHealth;
			m_model.targetStats.stateFlags = m_combatEventMessage.targetStateFlags;
			m_model.targetStats.hasTarget = true;
		}

		PushCombatLogEntry(m_model.combatLog, m_combatEventMessage, playerEntityId);
		NotifyObservers(changeMask);
		LOG_INFO(Net, "[UIModelBinding] CombatEvent applied (attacker={}, target={}, damage={}, player_role={})",
			m_combatEventMessage.attackerEntityId,
			m_combatEventMessage.targetEntityId,
			m_combatEventMessage.damage,
			playerWasAttacker ? "attacker" : "target");
		return true;
	}

	bool UIModelBinding::ApplyInventoryDelta(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeInventoryDelta(packet, m_inventoryMessage, m_inventoryScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] InventoryDelta FAILED: decode error");
			return false;
		}

		m_model.inventory = m_inventoryScratch;
		NotifyObservers(UIModelChangeInventory);
		LOG_INFO(Net, "[UIModelBinding] InventoryDelta applied (client_id={}, items={})",
			m_inventoryMessage.clientId,
			m_model.inventory.size());
		return true;
	}

	bool UIModelBinding::ApplyQuestDelta(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeQuestDelta(packet, m_questMessage))
		{
			LOG_WARN(Net, "[UIModelBinding] QuestDelta FAILED: decode error");
			return false;
		}

		UIQuestEntry* quest = FindQuest(m_model.quests, m_questMessage.questId);
		if (!quest)
		{
			m_model.quests.push_back(UIQuestEntry{});
			quest = &m_model.quests.back();
		}

		quest->status = m_questMessage.status;
		quest->questId = m_questMessage.questId;
		quest->rewardExperience = m_questMessage.rewardExperience;
		quest->rewardGold = m_questMessage.rewardGold;
		quest->steps.resize(m_questMessage.steps.size());
		for (size_t index = 0; index < m_questMessage.steps.size(); ++index)
		{
			const engine::server::QuestDeltaStep& source = m_questMessage.steps[index];
			UIQuestStep& target = quest->steps[index];
			target.stepType = source.stepType;
			target.targetId = source.targetId;
			target.currentCount = source.currentCount;
			target.requiredCount = source.requiredCount;
		}
		quest->rewardItems = m_questMessage.rewardItems;

		NotifyObservers(UIModelChangeQuests);
		LOG_INFO(Net, "[UIModelBinding] QuestDelta applied (quest_id={}, status={}, steps={})",
			quest->questId,
			quest->status,
			quest->steps.size());
		return true;
	}

	bool UIModelBinding::ApplyEventState(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeEventState(packet, m_eventMessage))
		{
			LOG_WARN(Net, "[UIModelBinding] EventState FAILED: decode error");
			return false;
		}

		UIEventEntry* eventEntry = FindEvent(m_model.events, m_eventMessage.eventId);
		if (!eventEntry)
		{
			m_model.events.push_back(UIEventEntry{});
			eventEntry = &m_model.events.back();
		}

		eventEntry->zoneId = m_eventMessage.zoneId;
		eventEntry->status = m_eventMessage.status;
		eventEntry->phaseIndex = m_eventMessage.phaseIndex;
		eventEntry->phaseCount = m_eventMessage.phaseCount;
		eventEntry->progressCurrent = m_eventMessage.progressCurrent;
		eventEntry->progressRequired = m_eventMessage.progressRequired;
		eventEntry->eventId = m_eventMessage.eventId;
		eventEntry->notificationText = m_eventMessage.notificationText;
		eventEntry->rewardExperience = m_eventMessage.rewardExperience;
		eventEntry->rewardGold = m_eventMessage.rewardGold;
		eventEntry->rewardItems = m_eventMessage.rewardItems;

		NotifyObservers(UIModelChangeEvents);
		LOG_INFO(Net, "[UIModelBinding] EventState applied (event_id={}, status={}, phase={}/{})",
			eventEntry->eventId,
			eventEntry->status,
			eventEntry->phaseIndex,
			eventEntry->phaseCount);
		return true;
	}
}
