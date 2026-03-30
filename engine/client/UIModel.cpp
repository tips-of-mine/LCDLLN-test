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
		inline constexpr size_t kMaxUiChatLines = 500;

		/// Append one chat line and keep a bounded ring (oldest dropped).
		void PushUiChatLine(std::vector<UIChatLineEntry>& lines, const UIChatLineEntry& entry)
		{
			lines.push_back(entry);
			if (lines.size() > kMaxUiChatLines)
			{
				lines.erase(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(lines.size() - kMaxUiChatLines));
			}
		}

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
		dump += " combo=";
		dump += playerStats.hasCombo ? std::to_string(playerStats.comboPoints) : "n/a";
		dump += "/";
		dump += playerStats.hasCombo ? std::to_string(playerStats.maxCombo) : "n/a";
		dump += " zone=";
		dump += std::to_string(playerStats.zoneId);
		dump += " pos=(";
		dump += std::to_string(playerStats.positionX);
		dump += ", ";
		dump += std::to_string(playerStats.positionY);
		dump += ", ";
		dump += std::to_string(playerStats.positionZ);
		dump += ")";
		dump += " tick=";
		dump += std::to_string(playerStats.serverTick);
		dump += " clients=";
		dump += std::to_string(playerStats.connectedClients);
		dump += " entities=";
		dump += std::to_string(playerStats.entityCount);
		dump += " snapshot=";
		dump += playerStats.hasSnapshot ? "true" : "false";
		dump += "\n";

		dump += "wallet: active=";
		dump += wallet.hasWallet ? "true" : "false";
		dump += " gold=";
		dump += std::to_string(wallet.gold);
		dump += " honor=";
		dump += std::to_string(wallet.honor);
		dump += " badges=";
		dump += std::to_string(wallet.badges);
		dump += " premium=";
		dump += std::to_string(wallet.premiumCurrency);
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
		dump += " pos=(";
		dump += std::to_string(targetStats.positionX);
		dump += ", ";
		dump += std::to_string(targetStats.positionY);
		dump += ", ";
		dump += std::to_string(targetStats.positionZ);
		dump += ")";
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

		dump += "chat(";
		dump += std::to_string(chatLines.size());
		dump += ")\n";
		for (const UIChatLineEntry& chatLine : chatLines)
		{
			dump += " - ch=";
			dump += std::to_string(static_cast<unsigned>(chatLine.channelWire));
			dump += " ts=";
			dump += std::to_string(chatLine.timestampUnixMs);
			dump += " ";
			dump += chatLine.sender;
			dump += ": ";
			dump += chatLine.text;
			dump += "\n";
		}

		dump += "chat_bubbles3d(";
		dump += std::to_string(chatBubbleBillboards.size());
		dump += ")\n";
		for (const UIChatBubbleBillboard& bubble : chatBubbleBillboards)
		{
			dump += " - entity=";
			dump += std::to_string(bubble.entityId);
			dump += " ndc=(";
			dump += std::to_string(bubble.ndcX);
			dump += ",";
			dump += std::to_string(bubble.ndcY);
			dump += ") alpha=";
			dump += std::to_string(bubble.alpha);
			dump += " text=";
			dump += bubble.text;
			dump += "\n";
		}

		dump += "emotes(";
		dump += std::to_string(activeEmotes.size());
		dump += ")\n";
		for (const UIActiveEmoteEntry& emote : activeEmotes)
		{
			dump += " - entity=";
			dump += std::to_string(emote.entityId);
			dump += " wire=";
			dump += std::to_string(static_cast<unsigned>(emote.emoteWireId));
			dump += " loop=";
			dump += emote.loop ? "true" : "false";
			dump += "\n";
		}

		dump += "trade: open=";
		dump += trade.isOpen ? "true" : "false";
		dump += " state=";
		dump += std::to_string(trade.tradeState);
		dump += " other=";
		dump += trade.theirPlayerName.empty() ? "(none)" : trade.theirPlayerName;
		dump += " my_items=";
		dump += std::to_string(trade.myOffer.items.size());
		dump += " my_gold=";
		dump += std::to_string(trade.myOffer.gold);
		dump += " my_locked=";
		dump += trade.myOffer.locked ? "true" : "false";
		dump += " their_items=";
		dump += std::to_string(trade.theirOffer.items.size());
		dump += " their_gold=";
		dump += std::to_string(trade.theirOffer.gold);
		dump += " their_locked=";
		dump += trade.theirOffer.locked ? "true" : "false";
		dump += "\n";

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
		if (!m_chatWorld.Init())
		{
			m_initialized = false;
			LOG_ERROR(Net, "[UIModelBinding] Init FAILED: ChatWorldVisualPresenter");
			return false;
		}

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
		m_model.chatLines.clear();
		m_model.chatBubbleBillboards.clear();
		m_model.activeEmotes.clear();
		m_model.wallet = {};
		m_chatWorld.Reset();
		NotifyObservers(UIModelChangeStats | UIModelChangeInventory | UIModelChangeQuests | UIModelChangeEvents | UIModelChangeCombat | UIModelChangeWorld | UIModelChangeChat | UIModelChangeChatWorld | UIModelChangeWallet);
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
		case engine::server::MessageKind::ZoneChange:
			return ApplyZoneChange(packet);
		case engine::server::MessageKind::InventoryDelta:
			return ApplyInventoryDelta(packet);
		case engine::server::MessageKind::QuestDelta:
			return ApplyQuestDelta(packet);
		case engine::server::MessageKind::EventState:
			return ApplyEventState(packet);
		case engine::server::MessageKind::ChatRelay:
			return ApplyChatRelay(packet);
		// M32.2 — Party system
		case engine::server::MessageKind::PartyUpdate:
			return ApplyPartyUpdate(packet);
		case engine::server::MessageKind::WalletUpdate:
			return ApplyWalletUpdate(packet);
		// M35.2 — Vendor shop
		case engine::server::MessageKind::VendorShopSync:
			return ApplyVendorShopSync(packet);
		case engine::server::MessageKind::VendorTransactionResult:
			return ApplyVendorTransactionResult(packet);
		// M35.3 — Trade window
		case engine::server::MessageKind::TradeWindowSync:
			return ApplyTradeWindowSync(packet);
		case engine::server::MessageKind::TradeResult:
			return ApplyTradeResult(packet);
		// M35.3 — TradeRequestNotify is informational; the client UI reads it as trade open notification
		case engine::server::MessageKind::TradeRequestNotify:
		{
			// Mark trade as pending-open so UI can display the accept/decline prompt.
			m_model.trade.isOpen       = false;
			m_model.trade.tradeState   = 0; // Pending
			m_model.trade.lastResultError.clear();
			NotifyObservers(UIModelChangeTrade);
			return true;
		}
		// M35.4 — Auction house
		case engine::server::MessageKind::AHPostListingResult:
			return ApplyAHPostListingResult(packet);
		case engine::server::MessageKind::AHSearchResult:
			return ApplyAHSearchResult(packet);
		case engine::server::MessageKind::AHBidResult:
			return ApplyAHBidResult(packet);
		case engine::server::MessageKind::AHBuyoutResult:
			return ApplyAHBuyoutResult(packet);
		case engine::server::MessageKind::AHMyListingsResult:
			return ApplyAHMyListingsResult(packet);
		case engine::server::MessageKind::AHCancelResult:
			return ApplyAHCancelResult(packet);
		case engine::server::MessageKind::AHDeliverySync:
			return ApplyAHDeliverySync(packet);
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

		PumpWorldPresenterAge();
		m_chatWorld.SyncEntityPositions(m_snapshotScratch);

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
			stats.positionX = entity.state.positionX;
			stats.positionY = entity.state.positionY;
			stats.positionZ = entity.state.positionZ;
			stats.hasSnapshot = true;
			break;
		}

		if (m_model.targetStats.hasTarget)
		{
			m_model.targetStats.hasPosition = false;
			for (const engine::server::SnapshotEntity& entity : m_snapshotScratch)
			{
				if (entity.entityId != m_model.targetStats.entityId)
				{
					continue;
				}

				m_model.targetStats.positionX = entity.state.positionX;
				m_model.targetStats.positionY = entity.state.positionY;
				m_model.targetStats.positionZ = entity.state.positionZ;
				m_model.targetStats.hasPosition = true;
				break;
			}
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

		NotifyObservers(UIModelChangeStats | UIModelChangeWorld);
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
			m_model.targetStats.hasPosition = false;
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

	bool UIModelBinding::ApplyZoneChange(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeZoneChange(packet, m_zoneChangeMessage))
		{
			LOG_WARN(Net, "[UIModelBinding] ZoneChange FAILED: decode error");
			return false;
		}

		m_model.playerStats.zoneId = m_zoneChangeMessage.zoneId;
		m_model.playerStats.positionX = m_zoneChangeMessage.spawnPositionX;
		m_model.playerStats.positionY = m_zoneChangeMessage.spawnPositionY;
		m_model.playerStats.positionZ = m_zoneChangeMessage.spawnPositionZ;
		m_model.targetStats.hasPosition = false;
		NotifyObservers(UIModelChangeWorld);
		LOG_INFO(Net, "[UIModelBinding] ZoneChange applied (zone_id={}, spawn=({:.2f}, {:.2f}, {:.2f}))",
			m_zoneChangeMessage.zoneId,
			m_zoneChangeMessage.spawnPositionX,
			m_zoneChangeMessage.spawnPositionY,
			m_zoneChangeMessage.spawnPositionZ);
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

	bool UIModelBinding::ApplyChatRelay(std::span<const std::byte> packet)
	{
		PumpWorldPresenterAge();
		if (!engine::server::DecodeChatRelay(packet, m_chatRelayScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] ChatRelay FAILED: decode error");
			return false;
		}

		UIChatLineEntry entry{};
		entry.channelWire = m_chatRelayScratch.channel;
		entry.timestampUnixMs = m_chatRelayScratch.timestampUnixMs;
		entry.sender = m_chatRelayScratch.senderDisplay;
		entry.text = m_chatRelayScratch.text;
		PushUiChatLine(m_model.chatLines, entry);

		m_chatWorld.OnChatRelay(
			m_chatRelayScratch.channel,
			m_chatRelayScratch.senderEntityId,
			m_chatRelayScratch.text);

		NotifyObservers(UIModelChangeChat | UIModelChangeChatWorld);
		LOG_INFO(Net,
			"[UIModelBinding] ChatRelay applied (channel_wire={}, sender_entity_id={}, lines={})",
			m_chatRelayScratch.channel,
			m_chatRelayScratch.senderEntityId,
			m_model.chatLines.size());
		return true;
	}

	void UIModelBinding::PumpWorldPresenterAge()
	{
		if (!m_initialized)
		{
			return;
		}

		m_chatWorld.PumpAge();
	}

	bool UIModelBinding::ApplyEmoteRelay(std::span<const std::byte> packet)
	{
		PumpWorldPresenterAge();
		if (!engine::server::DecodeEmoteRelay(packet, m_emoteRelayScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] EmoteRelay FAILED: decode error");
			return false;
		}

		m_chatWorld.OnEmoteRelay(m_emoteRelayScratch);
		m_chatWorld.ExportActiveEmotes(m_model.activeEmotes);
		NotifyObservers(UIModelChangeChatWorld);
		LOG_INFO(Net,
			"[UIModelBinding] EmoteRelay applied (actor_entity_id={}, emote_id={}, flags={})",
			m_emoteRelayScratch.actorEntityId,
			m_emoteRelayScratch.emoteId,
			m_emoteRelayScratch.flags);
		return true;
	}

	void UIModelBinding::TickChatWorldVisuals(
		const engine::math::Vec3& cameraWorld,
		const engine::math::Frustum& frustum,
		const engine::math::Mat4& viewProj,
		uint32_t viewportWidth,
		uint32_t viewportHeight)
	{
		if (!m_initialized)
		{
			LOG_WARN(Net, "[UIModelBinding] TickChatWorldVisuals ignored: not initialized");
			return;
		}

		if (!ValidateMainThread("TickChatWorldVisuals"))
		{
			return;
		}

		m_chatWorld.RebuildBillboards(
			cameraWorld,
			frustum,
			viewProj,
			viewportWidth,
			viewportHeight,
			m_model.chatBubbleBillboards);
		m_chatWorld.ExportActiveEmotes(m_model.activeEmotes);
		LOG_TRACE(Net,
			"[UIModelBinding] TickChatWorldVisuals OK (bubbles={}, emotes={})",
			m_model.chatBubbleBillboards.size(),
			m_model.activeEmotes.size());
	}

	// -------------------------------------------------------------------------
	// M32.2 — Party update
	// -------------------------------------------------------------------------

	bool UIModelBinding::ApplyPartyUpdate(std::span<const std::byte> packet)
	{
		engine::server::PartyUpdateMessage msg{};
		if (!engine::server::DecodePartyUpdate(packet, msg))
		{
			LOG_WARN(Net, "[UIModelBinding] ApplyPartyUpdate: decode failed");
			return false;
		}

		m_model.partyMembers.clear();
		m_model.partyMembers.reserve(msg.members.size());
		m_model.partyLeaderId = msg.leaderId;
		m_model.inParty       = !msg.members.empty();

		switch (msg.lootMode)
		{
		case engine::server::WireLootMode::FreeForAll:   m_model.partyLootModeLabel = "FreeForAll";   break;
		case engine::server::WireLootMode::RoundRobin:   m_model.partyLootModeLabel = "RoundRobin";   break;
		case engine::server::WireLootMode::MasterLooter: m_model.partyLootModeLabel = "MasterLooter"; break;
		case engine::server::WireLootMode::NeedGreed:    m_model.partyLootModeLabel = "NeedGreed";    break;
		default:                                         m_model.partyLootModeLabel = "Unknown";      break;
		}

		for (const engine::server::PartyMemberEntry& e : msg.members)
		{
			UIPartyMemberEntry entry{};
			entry.clientId      = e.clientId;
			entry.currentHealth = e.currentHealth;
			entry.maxHealth     = e.maxHealth;
			entry.currentMana   = e.currentMana;
			entry.maxMana       = e.maxMana;
			entry.displayName   = e.displayName;
			entry.isLeader      = (e.clientId == msg.leaderId);
			m_model.partyMembers.push_back(std::move(entry));
		}

		LOG_DEBUG(Net, "[UIModelBinding] PartyUpdate applied (party_id={}, members={}, loot_mode={})",
		    msg.partyId, msg.members.size(), m_model.partyLootModeLabel);

		NotifyObservers(UIModelChangeParty);
		return true;
	}

	bool UIModelBinding::ApplyWalletUpdate(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeWalletUpdate(packet, m_walletScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] WalletUpdate FAILED: decode error");
			return false;
		}

		m_model.wallet.gold = m_walletScratch.gold;
		m_model.wallet.honor = m_walletScratch.honor;
		m_model.wallet.badges = m_walletScratch.badges;
		m_model.wallet.premiumCurrency = m_walletScratch.premiumCurrency;
		m_model.wallet.hasWallet = true;

		LOG_INFO(Net,
			"[UIModelBinding] WalletUpdate applied (client_id={}, gold={}, honor={}, badges={}, premium={})",
			m_walletScratch.clientId,
			m_walletScratch.gold,
			m_walletScratch.honor,
			m_walletScratch.badges,
			m_walletScratch.premiumCurrency);

		NotifyObservers(UIModelChangeWallet);
		return true;
	}

	// -------------------------------------------------------------------------
	// M35.2 — Vendor shop
	// -------------------------------------------------------------------------

	bool UIModelBinding::ApplyVendorShopSync(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeVendorShopSync(packet, m_vendorShopScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] VendorShopSync FAILED: decode error");
			return false;
		}

		m_model.shop.vendorId = m_vendorShopScratch.vendorId;
		m_model.shop.items.clear();
		m_model.shop.items.reserve(m_vendorShopScratch.items.size());
		for (const engine::server::VendorShopItemEntry& entry : m_vendorShopScratch.items)
		{
			UIShopItemEntry item{};
			item.itemId    = entry.itemId;
			item.buyPrice  = entry.buyPrice;
			item.sellPrice = entry.sellPrice;
			item.stock     = entry.stock;
			m_model.shop.items.push_back(item);
		}
		m_model.shop.isOpen = true;

		LOG_INFO(Net, "[UIModelBinding] VendorShopSync applied (client_id={}, vendor={}, items={})",
			m_vendorShopScratch.clientId,
			m_vendorShopScratch.vendorId,
			m_vendorShopScratch.items.size());

		NotifyObservers(UIModelChangeShop);
		return true;
	}

	bool UIModelBinding::ApplyVendorTransactionResult(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeVendorTransactionResult(packet, m_vendorTxScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] VendorTransactionResult FAILED: decode error");
			return false;
		}

		if (m_vendorTxScratch.success != 0u)
		{
			// Update the wallet gold balance from the transaction result.
			m_model.wallet.gold = m_vendorTxScratch.newGold;
			m_model.wallet.hasWallet = true;
			LOG_INFO(Net, "[UIModelBinding] VendorTransactionResult: success, new_gold={}",
				m_vendorTxScratch.newGold);
			NotifyObservers(UIModelChangeShop | UIModelChangeWallet);
		}
		else
		{
			LOG_WARN(Net, "[UIModelBinding] VendorTransactionResult: failed (reason={})",
				m_vendorTxScratch.errorReason);
			NotifyObservers(UIModelChangeShop);
		}
		return true;
	}

	// -------------------------------------------------------------------------
	// M35.3 — Trade window apply methods
	// -------------------------------------------------------------------------

	bool UIModelBinding::ApplyTradeWindowSync(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeTradeWindowSync(packet, m_tradeWindowScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] TradeWindowSync FAILED: decode error");
			return false;
		}

		UITradeState& trade = m_model.trade;
		trade.isOpen      = true;
		trade.tradeState  = m_tradeWindowScratch.tradeState;
		trade.theirPlayerName = m_tradeWindowScratch.otherPlayerName;
		trade.reviewTicksRemaining = m_tradeWindowScratch.reviewTicksRemaining;

		trade.myOffer.gold   = m_tradeWindowScratch.myGold;
		trade.myOffer.locked = m_tradeWindowScratch.myLocked;
		trade.myOffer.items.clear();
		for (const engine::server::ItemStack& item : m_tradeWindowScratch.myItems)
		{
			trade.myOffer.items.push_back({ item.itemId, item.quantity });
		}

		trade.theirOffer.gold   = m_tradeWindowScratch.otherGold;
		trade.theirOffer.locked = m_tradeWindowScratch.otherLocked;
		trade.theirOffer.items.clear();
		for (const engine::server::ItemStack& item : m_tradeWindowScratch.otherItems)
		{
			trade.theirOffer.items.push_back({ item.itemId, item.quantity });
		}

		LOG_INFO(Net, "[UIModelBinding] TradeWindowSync applied (state={}, other={}, my_items={}, their_items={}, review_ticks={})",
			trade.tradeState,
			trade.theirPlayerName,
			trade.myOffer.items.size(),
			trade.theirOffer.items.size(),
			trade.reviewTicksRemaining);

		NotifyObservers(UIModelChangeTrade);
		return true;
	}

	bool UIModelBinding::ApplyTradeResult(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeTradeResult(packet, m_tradeResultScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] TradeResult FAILED: decode error");
			return false;
		}

		UITradeState& trade = m_model.trade;
		trade.lastResultSuccess = (m_tradeResultScratch.success != 0u);
		trade.lastResultError   = m_tradeResultScratch.errorReason;

		// Close the trade window regardless of outcome.
		trade.isOpen     = false;
		trade.tradeState = 0;
		trade.myOffer    = {};
		trade.theirOffer = {};
		trade.theirPlayerName.clear();
		trade.reviewTicksRemaining = 0;

		if (trade.lastResultSuccess)
		{
			LOG_INFO(Net, "[UIModelBinding] TradeResult: success");
		}
		else
		{
			LOG_WARN(Net, "[UIModelBinding] TradeResult: failed (reason={})",
				m_tradeResultScratch.errorReason);
		}

		NotifyObservers(UIModelChangeTrade);
		return true;
	}

	// -------------------------------------------------------------------------
	// M35.4 — Auction house handlers
	// -------------------------------------------------------------------------

	namespace
	{
		/// Convert one wire AHListingEntry to a UIAHListingEntry.
		UIAHListingEntry ToUIAHListing(const engine::server::AHListingEntry& e)
		{
			UIAHListingEntry ui{};
			ui.listingId    = e.listingId;
			ui.itemId       = e.sellerItemId;
			ui.itemQuantity = e.itemQuantity;
			ui.startBid     = e.startBid;
			ui.buyout       = e.buyout;
			ui.currentBid   = e.currentBid;
			ui.expiresInSec = e.expiresInSec;
			ui.hasBid       = (e.hasBid != 0u);
			return ui;
		}
	}

	bool UIModelBinding::ApplyAHPostListingResult(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeAHPostListingResult(packet, m_ahPostResultScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] AHPostListingResult FAILED: decode error");
			return false;
		}

		UIAHState& ah = m_model.auctionHouse;
		ah.lastActionSuccess   = (m_ahPostResultScratch.success != 0u);
		ah.lastActionListingId = m_ahPostResultScratch.listingId;
		ah.lastActionError     = m_ahPostResultScratch.errorReason;

		if (ah.lastActionSuccess)
		{
			LOG_INFO(Net, "[UIModelBinding] AHPostListingResult: OK (listing={})",
				m_ahPostResultScratch.listingId);
		}
		else
		{
			LOG_WARN(Net, "[UIModelBinding] AHPostListingResult: FAILED (reason={})",
				m_ahPostResultScratch.errorReason);
		}

		NotifyObservers(UIModelChangeAH);
		return true;
	}

	bool UIModelBinding::ApplyAHSearchResult(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeAHSearchResult(packet, m_ahSearchResultScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] AHSearchResult FAILED: decode error");
			return false;
		}

		UIAHState& ah = m_model.auctionHouse;
		ah.searchTotalCount = m_ahSearchResultScratch.totalCount;
		ah.searchPageIndex  = m_ahSearchResultScratch.pageIndex;
		ah.searchResults.clear();
		ah.searchResults.reserve(m_ahSearchResultScratch.listings.size());
		for (const engine::server::AHListingEntry& entry : m_ahSearchResultScratch.listings)
		{
			ah.searchResults.push_back(ToUIAHListing(entry));
		}

		LOG_INFO(Net, "[UIModelBinding] AHSearchResult applied (page={}, total={}, returned={})",
			ah.searchPageIndex, ah.searchTotalCount, ah.searchResults.size());

		NotifyObservers(UIModelChangeAH);
		return true;
	}

	bool UIModelBinding::ApplyAHBidResult(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeAHBidResult(packet, m_ahBidResultScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] AHBidResult FAILED: decode error");
			return false;
		}

		UIAHState& ah = m_model.auctionHouse;
		ah.lastActionSuccess   = (m_ahBidResultScratch.success != 0u);
		ah.lastActionListingId = m_ahBidResultScratch.listingId;
		ah.lastActionError     = m_ahBidResultScratch.errorReason;

		if (ah.lastActionSuccess)
		{
			LOG_INFO(Net, "[UIModelBinding] AHBidResult: OK (listing={}, newBid={})",
				m_ahBidResultScratch.listingId, m_ahBidResultScratch.newBid);
		}
		else
		{
			LOG_WARN(Net, "[UIModelBinding] AHBidResult: FAILED (listing={}, reason={})",
				m_ahBidResultScratch.listingId, m_ahBidResultScratch.errorReason);
		}

		NotifyObservers(UIModelChangeAH);
		return true;
	}

	bool UIModelBinding::ApplyAHBuyoutResult(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeAHBuyoutResult(packet, m_ahBuyoutResultScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] AHBuyoutResult FAILED: decode error");
			return false;
		}

		UIAHState& ah = m_model.auctionHouse;
		ah.lastActionSuccess   = (m_ahBuyoutResultScratch.success != 0u);
		ah.lastActionListingId = m_ahBuyoutResultScratch.listingId;
		ah.lastActionError     = m_ahBuyoutResultScratch.errorReason;

		if (ah.lastActionSuccess)
		{
			LOG_INFO(Net, "[UIModelBinding] AHBuyoutResult: OK (listing={})",
				m_ahBuyoutResultScratch.listingId);
		}
		else
		{
			LOG_WARN(Net, "[UIModelBinding] AHBuyoutResult: FAILED (listing={}, reason={})",
				m_ahBuyoutResultScratch.listingId, m_ahBuyoutResultScratch.errorReason);
		}

		NotifyObservers(UIModelChangeAH);
		return true;
	}

	bool UIModelBinding::ApplyAHMyListingsResult(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeAHMyListingsResult(packet, m_ahMyListingsScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] AHMyListingsResult FAILED: decode error");
			return false;
		}

		UIAHState& ah = m_model.auctionHouse;
		ah.myListings.clear();
		ah.myListings.reserve(m_ahMyListingsScratch.listings.size());
		for (const engine::server::AHListingEntry& entry : m_ahMyListingsScratch.listings)
		{
			ah.myListings.push_back(ToUIAHListing(entry));
		}

		LOG_INFO(Net, "[UIModelBinding] AHMyListingsResult applied ({} listings)",
			ah.myListings.size());

		NotifyObservers(UIModelChangeAH);
		return true;
	}

	bool UIModelBinding::ApplyAHCancelResult(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeAHCancelResult(packet, m_ahCancelResultScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] AHCancelResult FAILED: decode error");
			return false;
		}

		UIAHState& ah = m_model.auctionHouse;
		ah.lastActionSuccess   = (m_ahCancelResultScratch.success != 0u);
		ah.lastActionListingId = m_ahCancelResultScratch.listingId;
		ah.lastActionError     = m_ahCancelResultScratch.errorReason;

		if (ah.lastActionSuccess)
		{
			LOG_INFO(Net, "[UIModelBinding] AHCancelResult: OK (listing={})",
				m_ahCancelResultScratch.listingId);
		}
		else
		{
			LOG_WARN(Net, "[UIModelBinding] AHCancelResult: FAILED (listing={}, reason={})",
				m_ahCancelResultScratch.listingId, m_ahCancelResultScratch.errorReason);
		}

		NotifyObservers(UIModelChangeAH);
		return true;
	}

	bool UIModelBinding::ApplyAHDeliverySync(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeAHDeliverySync(packet, m_ahDeliveryScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] AHDeliverySync FAILED: decode error");
			return false;
		}

		// AHDeliverySync carries items/gold already applied server-side.
		// The client only needs to know deliveries arrived so it can refresh
		// inventory and wallet (already notified by separate InventoryDelta /
		// WalletUpdate packets). Log them for audit/debug purposes.
		for (const engine::server::AHDeliveryEntry& entry : m_ahDeliveryScratch.deliveries)
		{
			LOG_INFO(Net,
				"[UIModelBinding] AHDeliverySync entry (delivery={}, gold={}, item={}, qty={}, reason={})",
				entry.deliveryId, entry.goldAmount, entry.itemId, entry.itemQuantity, entry.reason);
		}

		LOG_INFO(Net, "[UIModelBinding] AHDeliverySync applied ({} deliveries)",
			m_ahDeliveryScratch.deliveries.size());

		NotifyObservers(UIModelChangeAH);
		return true;
	}
}
