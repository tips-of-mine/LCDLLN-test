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

		dump += "shop: open=";
		dump += shop.isOpen ? "true" : "false";
		dump += " vendor_id=";
		dump += std::to_string(shop.vendorId);
		dump += " name=";
		dump += shop.displayName;
		dump += " offers=";
		dump += std::to_string(shop.offers.size());
		dump += "\n";
		for (const UIShopOfferLine& line : shop.offers)
		{
			dump += " - item=";
			dump += std::to_string(line.itemId);
			dump += " buy=";
			dump += std::to_string(line.buyPrice);
			dump += " stock=";
			dump += std::to_string(line.stock);
			dump += "\n";
		}

		dump += "auction: open=";
		dump += auction.isOpen ? "true" : "false";
		dump += " sort=";
		dump += std::to_string(auction.sortMode);
		dump += " listings=";
		dump += std::to_string(auction.listings.size());
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
		m_model.shop = {};
		m_model.auction = {};
		m_chatWorld.Reset();
		NotifyObservers(UIModelChangeStats | UIModelChangeInventory | UIModelChangeQuests | UIModelChangeEvents | UIModelChangeCombat | UIModelChangeWorld | UIModelChangeChat | UIModelChangeChatWorld | UIModelChangeWallet | UIModelChangeShop | UIModelChangeAuction);
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

	bool UIModelBinding::CloseShop()
	{
		if (!m_initialized)
		{
			LOG_ERROR(Net, "[UIModelBinding] CloseShop FAILED: binding not initialized");
			return false;
		}
		if (!ValidateMainThread("CloseShop"))
		{
			return false;
		}
		if (!m_model.shop.isOpen)
		{
			return true;
		}
		m_model.shop.isOpen = false;
		m_model.shop.offers.clear();
		m_model.shop.displayName.clear();
		m_model.shop.vendorId = 0;
		LOG_INFO(Net, "[UIModelBinding] Shop closed locally");
		NotifyObservers(UIModelChangeShop);
		return true;
	}

	bool UIModelBinding::CloseAuction()
	{
		if (!m_initialized)
		{
			LOG_ERROR(Net, "[UIModelBinding] CloseAuction FAILED: binding not initialized");
			return false;
		}
		if (!ValidateMainThread("CloseAuction"))
		{
			return false;
		}
		if (!m_model.auction.isOpen)
		{
			return true;
		}
		m_model.auction.isOpen = false;
		m_model.auction.listings.clear();
		m_model.auction.selectedRow = 0;
		LOG_INFO(Net, "[UIModelBinding] Auction panel closed locally");
		NotifyObservers(UIModelChangeAuction);
		return true;
	}

	bool UIModelBinding::ConfigureAuctionBrowse(
		uint32_t minPrice,
		uint32_t maxPrice,
		uint32_t itemIdFilter,
		uint32_t sortMode)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Net, "[UIModelBinding] ConfigureAuctionBrowse FAILED: binding not initialized");
			return false;
		}
		if (!ValidateMainThread("ConfigureAuctionBrowse"))
		{
			return false;
		}
		m_model.auction.filterMinPrice = minPrice;
		m_model.auction.filterMaxPrice = maxPrice;
		m_model.auction.filterItemId = itemIdFilter;
		m_model.auction.sortMode = std::min<uint32_t>(sortMode, 2u);
		LOG_INFO(Net,
			"[UIModelBinding] Auction browse params (min={}, max={}, item={}, sort={})",
			minPrice,
			maxPrice,
			itemIdFilter,
			m_model.auction.sortMode);
		NotifyObservers(UIModelChangeAuction);
		return true;
	}

	bool UIModelBinding::SelectAuctionRow(uint32_t rowIndex)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Net, "[UIModelBinding] SelectAuctionRow FAILED: binding not initialized");
			return false;
		}
		if (!ValidateMainThread("SelectAuctionRow"))
		{
			return false;
		}
		m_model.auction.selectedRow = rowIndex;
		NotifyObservers(UIModelChangeAuction);
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
		case engine::server::MessageKind::ShopOpen:
			return ApplyShopOpen(packet);
		case engine::server::MessageKind::AuctionBrowseResult:
			return ApplyAuctionBrowseResult(packet);
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

	bool UIModelBinding::ApplyShopOpen(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeShopOpen(packet, m_shopOpenScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] ShopOpen FAILED: decode error");
			return false;
		}

		m_model.shop.vendorId = m_shopOpenScratch.vendorId;
		m_model.shop.displayName = m_shopOpenScratch.displayName;
		m_model.shop.offers.clear();
		m_model.shop.offers.reserve(m_shopOpenScratch.offers.size());
		for (const engine::server::ShopOfferWire& row : m_shopOpenScratch.offers)
		{
			UIShopOfferLine line{};
			line.itemId = row.itemId;
			line.buyPrice = row.buyPrice;
			line.stock = row.stock;
			m_model.shop.offers.push_back(line);
		}
		m_model.shop.isOpen = true;

		LOG_INFO(Net,
			"[UIModelBinding] ShopOpen applied (vendor_id={}, name={}, offers={})",
			m_model.shop.vendorId,
			m_model.shop.displayName,
			m_model.shop.offers.size());

		NotifyObservers(UIModelChangeShop);
		return true;
	}

	bool UIModelBinding::ApplyAuctionBrowseResult(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeAuctionBrowseResult(packet, m_auctionBrowseScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] AuctionBrowseResult FAILED: decode error");
			return false;
		}

		if (m_model.playerStats.clientId != 0u && m_auctionBrowseScratch.clientId != m_model.playerStats.clientId)
		{
			LOG_WARN(Net,
				"[UIModelBinding] AuctionBrowseResult client_id mismatch (model={}, packet={})",
				m_model.playerStats.clientId,
				m_auctionBrowseScratch.clientId);
		}

		m_model.auction.listings.clear();
		m_model.auction.listings.reserve(m_auctionBrowseScratch.rows.size());
		for (const engine::server::AuctionListingWireRow& row : m_auctionBrowseScratch.rows)
		{
			UIAuctionListingLine line{};
			line.listingId = row.listingId;
			line.itemId = row.itemId;
			line.quantity = row.quantity;
			line.startBid = row.startBid;
			line.buyoutPrice = row.buyoutPrice;
			line.currentBid = row.currentBid;
			line.expiresAtTick = row.expiresAtTick;
			m_model.auction.listings.push_back(line);
		}
		m_model.auction.isOpen = true;
		if (m_model.auction.listings.empty())
		{
			m_model.auction.selectedRow = 0;
		}
		else if (m_model.auction.selectedRow >= m_model.auction.listings.size())
		{
			m_model.auction.selectedRow = m_model.auction.listings.size() - 1u;
		}

		LOG_INFO(Net,
			"[UIModelBinding] AuctionBrowseResult applied (client_id={}, rows={})",
			m_auctionBrowseScratch.clientId,
			m_model.auction.listings.size());

		NotifyObservers(UIModelChangeAuction);
		return true;
	}
}
