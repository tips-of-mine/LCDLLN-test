#include "src/client/ui_common/UIModel.h"

#include "src/shared/core/Log.h"

#include <algorithm>
#include <chrono>
#include <iterator>
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
			// Combat SP2 — critique / raté propagés au log (wire v10).
			entry.wasCrit = (message.flags & engine::server::kCombatEventFlagCrit) != 0u;
			entry.wasMiss = (message.flags & engine::server::kCombatEventFlagMiss) != 0u;
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
		// M35.3 — Trade window
		case engine::server::MessageKind::TradeWindowUpdate:
			return ApplyTradeWindowUpdate(packet);
		case engine::server::MessageKind::TradeComplete:
			return ApplyTradeComplete(packet);
		case engine::server::MessageKind::TradeCancelled:
			return ApplyTradeCancelled(packet);
		// M36.1 — Harvest cast bar
		case engine::server::MessageKind::HarvestStart:
			return ApplyHarvestStart(packet);
		case engine::server::MessageKind::HarvestComplete:
			return ApplyHarvestComplete(packet);
		case engine::server::MessageKind::HarvestCancelled:
			return ApplyHarvestCancelled(packet);
		// M36.2 — Crafting / profession messages
		case engine::server::MessageKind::ProfessionUpdate:
			return ApplyProfessionUpdate(packet);
		case engine::server::MessageKind::CraftRecipeListResult:
			return ApplyCraftRecipeListResult(packet);
		case engine::server::MessageKind::CraftStart:
			return ApplyCraftStart(packet);
		case engine::server::MessageKind::CraftComplete:
			return ApplyCraftComplete(packet);
		case engine::server::MessageKind::CraftCancelled:
			return ApplyCraftCancelled(packet);
		// R1-B — feuille de personnage (stats dérivées poussées à l'enter-world)
		case engine::server::MessageKind::PlayerStats:
			return ApplyPlayerStats(packet);
		// Combat SP3 — sorts et auras (wire v11)
		case engine::server::MessageKind::ResourceUpdate:
			return ApplyResourceUpdate(packet);
		case engine::server::MessageKind::CastBarUpdate:
			return ApplyCastBarUpdate(packet);
		case engine::server::MessageKind::AuraUpdate:
			return ApplyAuraUpdate(packet);
		case engine::server::MessageKind::ThreatUpdate:
			return ApplyThreatUpdate(packet);
		// Groupes SP1 — invitation entrante (le reste du flux party était déjà routé).
		case engine::server::MessageKind::PartyInviteNotify:
			return ApplyPartyInviteNotify(packet);
		// Correction SP1 — position imposée par le serveur (respawn/anti-triche).
		case engine::server::MessageKind::ForcePosition:
			return ApplyForcePosition(packet);
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

		// TG.1 — chunking : si le serveur a scinde le snapshot en plusieurs chunks
		// (chunkCount > 1), on accumule jusqu'a avoir tous les chunks du meme serverTick
		// puis on commite l'image globale. Un changement de serverTick abandonne
		// l'accumulation precedente (best-effort UDP, pas de retry).
		if (m_snapshotMessage.chunkCount > 1u)
		{
			if (!m_chunkAccumulator.active || m_chunkAccumulator.serverTick != m_snapshotMessage.serverTick)
			{
				// Nouveau serverTick (premier chunk recu pour ce tick) ou
				// abandon d'un precedent accumulator incomplet.
				m_chunkAccumulator.serverTick = m_snapshotMessage.serverTick;
				m_chunkAccumulator.expectedChunkCount = m_snapshotMessage.chunkCount;
				m_chunkAccumulator.chunksReceived = 0u;
				m_chunkAccumulator.entities.clear();
				m_chunkAccumulator.header = m_snapshotMessage; // garde le header du 1er chunk
				m_chunkAccumulator.active = true;
			}
			m_chunkAccumulator.entities.insert(
				m_chunkAccumulator.entities.end(),
				m_snapshotScratch.begin(), m_snapshotScratch.end());
			++m_chunkAccumulator.chunksReceived;
			if (m_chunkAccumulator.chunksReceived < m_chunkAccumulator.expectedChunkCount)
			{
				// Pas encore tous les chunks : on attend les suivants sans commiter.
				return true;
			}
			// Tous les chunks recus : commit avec l'union des entites. On reutilise
			// m_snapshotMessage / m_snapshotScratch pour la suite du chemin de commit
			// existant ; entityCount du header committe = total des chunks.
			m_snapshotMessage = m_chunkAccumulator.header;
			m_snapshotMessage.entityCount = static_cast<uint16_t>(
				std::min<size_t>(m_chunkAccumulator.entities.size(), 0xFFFFu));
			m_snapshotMessage.chunkIndex = 0u;
			m_snapshotMessage.chunkCount = 1u;
			m_snapshotScratch = std::move(m_chunkAccumulator.entities);
			m_chunkAccumulator.active = false;
			m_chunkAccumulator.entities.clear();
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

		// TD.1 — on n'extrait plus seulement le joueur local : on capture aussi les avatars
		// distants (≠ joueur local) dans m_model.remoteEntities pour le rendu monde (TD.2).
		// Reconstruction complète à chaque snapshot (le snapshot porte l'ensemble courant de
		// l'AoI ; une entité absente du prochain snapshot disparaît donc naturellement).
		m_model.remoteEntities.clear();
		for (const engine::server::SnapshotEntity& entity : m_snapshotScratch)
		{
			if (entity.entityId == stats.playerEntityId)
			{
				stats.currentHealth = entity.state.currentHealth;
				stats.maxHealth = entity.state.maxHealth;
				stats.stateFlags = entity.state.stateFlags;
				stats.positionX = entity.state.positionX;
				stats.positionY = entity.state.positionY;
				stats.positionZ = entity.state.positionZ;
				stats.hasSnapshot = true;
				continue;
			}

			UIRemoteEntity remote;
			remote.entityId = entity.entityId;
			remote.positionX = entity.state.positionX;
			remote.positionY = entity.state.positionY;
			remote.positionZ = entity.state.positionZ;
			remote.yawRadians = entity.state.yawRadians;
			remote.velocityX = entity.state.velocityX;
			remote.velocityY = entity.state.velocityY;
			remote.velocityZ = entity.state.velocityZ;
			remote.currentHealth = entity.state.currentHealth;
			remote.maxHealth = entity.state.maxHealth;
			remote.stateFlags = entity.state.stateFlags;
			// TD.4 : propage le clientId reçu (0 = mob/lootbag, ≠ 0 = joueur distant).
			remote.playerClientId = entity.playerClientId;
			// TD.5 : nom du perso pour la plaque de nom (vide → fallback "P<clientId>").
			remote.displayName = entity.characterName;
			// TD.6 : genre du perso pour le rendu de l'avatar distant (vide → fallback "male").
			remote.gender = entity.gender;
			// TD.8 : état d'animation pour jouer le bon clip sur l'avatar distant.
			remote.animationState = static_cast<uint8_t>(entity.animationState);
			// Combat SP1 : archétype de créature (0 = joueur/loot bag, ≠ 0 = mob).
			remote.archetypeId = entity.archetypeId;
			m_model.remoteEntities.push_back(remote);
		}

		// Combat SP3 — purge des auras des entités sorties de l'AoI (le joueur
		// local garde les siennes, les autres suivent le contenu du snapshot).
		if (!m_model.entityAuras.empty())
		{
			for (auto auraIt = m_model.entityAuras.begin(); auraIt != m_model.entityAuras.end();)
			{
				const engine::server::EntityId auraEntityId = auraIt->first;
				bool stillPresent = (auraEntityId == stats.playerEntityId);
				if (!stillPresent)
				{
					for (const UIRemoteEntity& remote : m_model.remoteEntities)
					{
						if (remote.entityId == auraEntityId)
						{
							stillPresent = true;
							break;
						}
					}
				}
				auraIt = stillPresent ? std::next(auraIt) : m_model.entityAuras.erase(auraIt);
			}
		}
		// Combat SP4 — même purge AoI pour les tables de menace répliquées.
		if (!m_model.threatByMob.empty())
		{
			for (auto threatIt = m_model.threatByMob.begin(); threatIt != m_model.threatByMob.end();)
			{
				bool mobPresent = false;
				for (const UIRemoteEntity& remote : m_model.remoteEntities)
				{
					if (remote.entityId == threatIt->first)
					{
						mobPresent = true;
						break;
					}
				}
				threatIt = mobPresent ? std::next(threatIt) : m_model.threatByMob.erase(threatIt);
			}
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
				// Combat SP2 — la cible suit aussi ses PV/flags entre deux
				// CombatEvent (régénération, dégâts d'un autre joueur, mort).
				m_model.targetStats.currentHealth = entity.state.currentHealth;
				m_model.targetStats.maxHealth = entity.state.maxHealth;
				m_model.targetStats.stateFlags = entity.state.stateFlags;
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

	bool UIModelBinding::ApplyResourceUpdate(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeResourceUpdate(packet, m_resourceUpdateMessage))
		{
			LOG_WARN(Net, "[UIModelBinding] ResourceUpdate FAILED: decode error");
			return false;
		}

		m_model.playerStats.secondaryResourceCurrent = m_resourceUpdateMessage.currentResource;
		m_model.playerStats.secondaryResourceMax = m_resourceUpdateMessage.maxResource;
		NotifyObservers(UIModelChangeStats);
		return true;
	}

	bool UIModelBinding::ApplyCastBarUpdate(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeCastBarUpdate(packet, m_castBarUpdateMessage))
		{
			LOG_WARN(Net, "[UIModelBinding] CastBarUpdate FAILED: decode error");
			return false;
		}

		if (m_castBarUpdateMessage.status == engine::server::kCastBarStatusStart)
		{
			m_model.castBar.active = true;
			m_model.castBar.spellId = m_castBarUpdateMessage.spellId;
			m_model.castBar.durationMs = m_castBarUpdateMessage.durationMs;
			m_model.castBar.startedAtNs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
		}
		else
		{
			// Complete et Cancel masquent la barre (le résultat du sort arrive
			// par CombatEvent / AuraUpdate / ResourceUpdate, pas par cette voie).
			m_model.castBar = UICastBarState{};
		}
		NotifyObservers(UIModelChangeCombat);
		return true;
	}

	bool UIModelBinding::ApplyAuraUpdate(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeAuraUpdate(packet, m_auraUpdateMessage))
		{
			LOG_WARN(Net, "[UIModelBinding] AuraUpdate FAILED: decode error");
			return false;
		}

		if (m_auraUpdateMessage.auras.empty())
		{
			m_model.entityAuras.erase(m_auraUpdateMessage.targetEntityId);
		}
		else
		{
			const uint64_t nowNs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
			std::vector<UIAuraEntry>& auraList = m_model.entityAuras[m_auraUpdateMessage.targetEntityId];
			auraList.clear();
			auraList.reserve(m_auraUpdateMessage.auras.size());
			for (const engine::server::AuraWireEntry& wireAura : m_auraUpdateMessage.auras)
			{
				UIAuraEntry entry{};
				entry.spellId = wireAura.spellId;
				entry.effectType = wireAura.effectType;
				entry.remainingMs = wireAura.remainingMs;
				entry.stacks = wireAura.stacks;
				entry.receivedAtNs = nowNs;
				auraList.push_back(std::move(entry));
			}
		}
		NotifyObservers(UIModelChangeCombat);
		return true;
	}

	bool UIModelBinding::ApplyThreatUpdate(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeThreatUpdate(packet, m_threatUpdateMessage))
		{
			LOG_WARN(Net, "[UIModelBinding] ThreatUpdate FAILED: decode error");
			return false;
		}

		if (m_threatUpdateMessage.entries.empty())
		{
			m_model.threatByMob.erase(m_threatUpdateMessage.mobEntityId);
		}
		else
		{
			std::vector<UIThreatEntry>& threatList = m_model.threatByMob[m_threatUpdateMessage.mobEntityId];
			threatList.clear();
			threatList.reserve(m_threatUpdateMessage.entries.size());
			for (const engine::server::ThreatWireEntry& wireEntry : m_threatUpdateMessage.entries)
			{
				threatList.push_back(UIThreatEntry{ wireEntry.playerEntityId, wireEntry.threatValue });
			}
		}
		NotifyObservers(UIModelChangeCombat);
		return true;
	}

	bool UIModelBinding::ApplyForcePosition(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeForcePosition(packet, m_forcePositionMessage))
		{
			LOG_WARN(Net, "[UIModelBinding] ForcePosition FAILED: decode error");
			return false;
		}

		m_model.forcedPosition.pending = true;
		m_model.forcedPosition.x = m_forcePositionMessage.positionX;
		m_model.forcedPosition.y = m_forcePositionMessage.positionY;
		m_model.forcedPosition.z = m_forcePositionMessage.positionZ;
		m_model.forcedPosition.yawRadians = m_forcePositionMessage.yawRadians;
		m_model.forcedPosition.reason = m_forcePositionMessage.reason;
		LOG_INFO(Net, "[UIModelBinding] ForcePosition received (pos=({:.1f},{:.1f},{:.1f}), reason={})",
			m_model.forcedPosition.x, m_model.forcedPosition.y, m_model.forcedPosition.z,
			m_model.forcedPosition.reason);
		NotifyObservers(UIModelChangeWorld);
		return true;
	}

	void UIModelBinding::ClearForcedPosition()
	{
		if (!ValidateMainThread("ClearForcedPosition"))
		{
			return;
		}
		m_model.forcedPosition = UIForcedPosition{};
	}

	bool UIModelBinding::ApplyPartyInviteNotify(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodePartyInviteNotify(packet, m_partyInviteNotifyMessage))
		{
			LOG_WARN(Net, "[UIModelBinding] PartyInviteNotify FAILED: decode error");
			return false;
		}

		m_model.partyInvite.pending = true;
		m_model.partyInvite.inviterName = m_partyInviteNotifyMessage.inviterName;
		LOG_INFO(Net, "[UIModelBinding] Party invite received (inviter={})", m_model.partyInvite.inviterName);
		NotifyObservers(UIModelChangeParty);
		return true;
	}

	bool UIModelBinding::SelectCraftRecipe(uint32_t rowIndex)
	{
		if (!ValidateMainThread("SelectCraftRecipe"))
		{
			return false;
		}
		if (rowIndex >= m_model.crafting.recipes.size())
		{
			return false;
		}
		m_model.crafting.selectedRecipeIndex = rowIndex;
		NotifyObservers(UIModelChangeCrafting);
		return true;
	}

	void UIModelBinding::ClearPartyInvite()
	{
		if (!ValidateMainThread("ClearPartyInvite"))
		{
			return;
		}
		if (!m_model.partyInvite.pending)
		{
			return;
		}
		m_model.partyInvite = UIPartyInviteState{};
		NotifyObservers(UIModelChangeParty);
	}

	bool UIModelBinding::SetLocalTarget(engine::server::EntityId entityId)
	{
		if (!ValidateMainThread("SetLocalTarget"))
		{
			return false;
		}

		for (const UIRemoteEntity& remote : m_model.remoteEntities)
		{
			if (remote.entityId != entityId)
			{
				continue;
			}

			m_model.targetStats.entityId = remote.entityId;
			m_model.targetStats.currentHealth = remote.currentHealth;
			m_model.targetStats.maxHealth = remote.maxHealth;
			m_model.targetStats.stateFlags = remote.stateFlags;
			m_model.targetStats.positionX = remote.positionX;
			m_model.targetStats.positionY = remote.positionY;
			m_model.targetStats.positionZ = remote.positionZ;
			m_model.targetStats.hasTarget = true;
			m_model.targetStats.hasPosition = true;
			NotifyObservers(UIModelChangeCombat);
			LOG_INFO(Net, "[UIModelBinding] Local target set (entity_id={}, hp={}/{})",
				remote.entityId, remote.currentHealth, remote.maxHealth);
			return true;
		}

		LOG_DEBUG(Net, "[UIModelBinding] SetLocalTarget ignored: entity {} not in AoI", entityId);
		return false;
	}

	void UIModelBinding::ClearLocalTarget()
	{
		if (!ValidateMainThread("ClearLocalTarget"))
		{
			return;
		}

		if (!m_model.targetStats.hasTarget)
		{
			return;
		}

		m_model.targetStats = UITargetStats{};
		NotifyObservers(UIModelChangeCombat);
		LOG_INFO(Net, "[UIModelBinding] Local target cleared");
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
		// Groupes SP1 — rejoindre un groupe consomme l'invitation en attente.
		if (m_model.inParty)
		{
			m_model.partyInvite = UIPartyInviteState{};
		}

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

	bool UIModelBinding::ApplyPlayerStats(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodePlayerStats(packet, m_playerStatsScratch))
		{
			LOG_WARN(Net, "[UIModelBinding] PlayerStats FAILED: decode error");
			return false;
		}

		// R1-B — recopie la feuille de stats dérivées dans la section stats du modèle.
		// Champs distincts du snapshot (health/mana courants) : ici ce sont les maxima
		// et stats statiques calculées par le shard à l'enter-world.
		m_model.playerStats.hasSheet = true;
		m_model.playerStats.sheetMaxHealth = m_playerStatsScratch.maxHealth;
		m_model.playerStats.secondaryResourceMax = m_playerStatsScratch.resource;
		m_model.playerStats.staminaMax = m_playerStatsScratch.stamina;
		m_model.playerStats.damage = m_playerStatsScratch.damage;
		m_model.playerStats.accuracy = m_playerStatsScratch.accuracy;
		m_model.playerStats.range = m_playerStatsScratch.range;
		m_model.playerStats.critRate = m_playerStatsScratch.critRate;
		m_model.playerStats.critMult = m_playerStatsScratch.critMult;
		m_model.playerStats.speedWalk = m_playerStatsScratch.speedWalk;
		m_model.playerStats.speedRun = m_playerStatsScratch.speedRun;
		m_model.playerStats.speedSprint = m_playerStatsScratch.speedSprint;
		m_model.playerStats.perception = m_playerStatsScratch.perception;
		m_model.playerStats.stealth = m_playerStatsScratch.stealth;
		m_model.playerStats.secondaryResourceKey = m_playerStatsScratch.resourceKey;
		// Combat SP3 — profil de classe (kit de la barre d'action) ; la ressource
		// courante part de son max (pleine à l'enter-world, le serveur ne pousse
		// un ResourceUpdate que sur variation).
		m_model.playerStats.profileId = m_playerStatsScratch.profileId;
		m_model.playerStats.secondaryResourceCurrent = m_playerStatsScratch.resource;

		LOG_INFO(Net,
			"[UIModelBinding] PlayerStats applied (client_id={}, max_health={}, resource={}, stamina={}, damage={})",
			m_playerStatsScratch.clientId,
			m_playerStatsScratch.maxHealth,
			m_playerStatsScratch.resource,
			m_playerStatsScratch.stamina,
			m_playerStatsScratch.damage);

		NotifyObservers(UIModelChangeStats);
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
			m_model.auction.selectedRow =
				static_cast<uint32_t>(m_model.auction.listings.size() - 1u);
		}

		LOG_INFO(Net,
			"[UIModelBinding] AuctionBrowseResult applied (client_id={}, rows={})",
			m_auctionBrowseScratch.clientId,
			m_model.auction.listings.size());

	NotifyObservers(UIModelChangeAuction);
	return true;
}

// -------------------------------------------------------------------------
// M35.3 — Trade window Apply functions
// -------------------------------------------------------------------------

bool UIModelBinding::ApplyTradeWindowUpdate(std::span<const std::byte> packet)
{
	m_tradeWindowScratch = {};
	if (!engine::server::DecodeTradeWindowUpdate(packet, m_tradeWindowScratch))
	{
		LOG_WARN(Net, "[UIModelBinding] ApplyTradeWindowUpdate FAILED: decode error");
		return false;
	}

	auto fillSide = [](UITradeSide& dst, const engine::server::TradeSideWire& src)
	{
		dst.clientId   = src.clientId;
		dst.goldAmount = src.goldAmount;
		dst.locked     = (src.locked != 0);
		dst.confirmed  = (src.confirmed != 0);
		dst.items      = src.items;
	};

	m_model.tradeWindow.isOpen              = true;
	m_model.tradeWindow.isDone              = false;
	m_model.tradeWindow.cancelReason.clear();
	m_model.tradeWindow.reviewTicksRemaining = m_tradeWindowScratch.reviewTicksRemaining;
	fillSide(m_model.tradeWindow.selfSide,  m_tradeWindowScratch.self);
	fillSide(m_model.tradeWindow.otherSide, m_tradeWindowScratch.other);

	LOG_DEBUG(Net, "[UIModelBinding] TradeWindowUpdate applied (self={} gold={} locked={} "
	          "other={} gold={} locked={} reviewTicks={})",
	          m_model.tradeWindow.selfSide.clientId,
	          m_model.tradeWindow.selfSide.goldAmount,
	          m_model.tradeWindow.selfSide.locked,
	          m_model.tradeWindow.otherSide.clientId,
	          m_model.tradeWindow.otherSide.goldAmount,
	          m_model.tradeWindow.otherSide.locked,
	          m_model.tradeWindow.reviewTicksRemaining);

	NotifyObservers(UIModelChangeTrade);
	return true;
}

bool UIModelBinding::ApplyTradeComplete(std::span<const std::byte> packet)
{
	m_tradeCompleteScratch = {};
	if (!engine::server::DecodeTradeComplete(packet, m_tradeCompleteScratch))
	{
		LOG_WARN(Net, "[UIModelBinding] ApplyTradeComplete FAILED: decode error");
		return false;
	}

	m_model.tradeWindow.isDone       = true;
	m_model.tradeWindow.isOpen       = false;
	m_model.tradeWindow.cancelReason.clear();
	m_model.tradeWindow.selfSide     = {};
	m_model.tradeWindow.otherSide    = {};
	m_model.tradeWindow.reviewTicksRemaining = 0;

	LOG_INFO(Net, "[UIModelBinding] TradeComplete applied (client_id={})",
	         m_tradeCompleteScratch.clientId);
	NotifyObservers(UIModelChangeTrade);
	return true;
}

bool UIModelBinding::ApplyTradeCancelled(std::span<const std::byte> packet)
{
	m_tradeCancelledScratch = {};
	if (!engine::server::DecodeTradeCancelled(packet, m_tradeCancelledScratch))
	{
		LOG_WARN(Net, "[UIModelBinding] ApplyTradeCancelled FAILED: decode error");
		return false;
	}

	m_model.tradeWindow.isOpen       = false;
	m_model.tradeWindow.isDone       = false;
	m_model.tradeWindow.cancelReason = m_tradeCancelledScratch.reason;
	m_model.tradeWindow.selfSide     = {};
	m_model.tradeWindow.otherSide    = {};
	m_model.tradeWindow.reviewTicksRemaining = 0;

	LOG_INFO(Net, "[UIModelBinding] TradeCancelled applied (reason='{}')",
	         m_tradeCancelledScratch.reason);
	NotifyObservers(UIModelChangeTrade);
	return true;
}

// -------------------------------------------------------------------------
// M36.1 — Harvest cast bar Apply functions
// -------------------------------------------------------------------------

bool UIModelBinding::ApplyHarvestStart(std::span<const std::byte> packet)
{
	m_harvestStartScratch = {};
	if (!engine::server::DecodeHarvestStart(packet, m_harvestStartScratch))
	{
		LOG_WARN(Net, "[UIModelBinding] ApplyHarvestStart FAILED: decode error");
		return false;
	}

	m_model.harvest.nodeEntityId       = m_harvestStartScratch.nodeEntityId;
	m_model.harvest.totalDurationTicks = m_harvestStartScratch.harvestDurationTicks;
	m_model.harvest.elapsedTicks       = 0;
	m_model.harvest.fillFraction       = 0.0f;
	m_model.harvest.inProgress         = true;

	LOG_INFO(Net, "[UIModelBinding] HarvestStart applied (node={}, durationTicks={})",
	         m_harvestStartScratch.nodeEntityId, m_harvestStartScratch.harvestDurationTicks);
	NotifyObservers(UIModelChangeHarvest);
	return true;
}

bool UIModelBinding::ApplyHarvestComplete(std::span<const std::byte> packet)
{
	m_harvestCompleteScratch = {};
	if (!engine::server::DecodeHarvestComplete(packet, m_harvestCompleteScratch))
	{
		LOG_WARN(Net, "[UIModelBinding] ApplyHarvestComplete FAILED: decode error");
		return false;
	}

	m_model.harvest.fillFraction = 1.0f;
	m_model.harvest.inProgress   = false;
	m_model.harvest.elapsedTicks = m_model.harvest.totalDurationTicks;

	LOG_INFO(Net, "[UIModelBinding] HarvestComplete applied (node={})",
	         m_harvestCompleteScratch.nodeEntityId);
	NotifyObservers(UIModelChangeHarvest);
	return true;
}

bool UIModelBinding::ApplyHarvestCancelled(std::span<const std::byte> packet)
{
	m_harvestCancelledScratch = {};
	if (!engine::server::DecodeHarvestCancelled(packet, m_harvestCancelledScratch))
	{
		LOG_WARN(Net, "[UIModelBinding] ApplyHarvestCancelled FAILED: decode error");
		return false;
	}

	m_model.harvest.inProgress   = false;
	m_model.harvest.fillFraction = 0.0f;
	m_model.harvest.elapsedTicks = 0;

	LOG_INFO(Net, "[UIModelBinding] HarvestCancelled applied (node={}, reason={})",
	         m_harvestCancelledScratch.nodeEntityId,
	         static_cast<uint8_t>(m_harvestCancelledScratch.reason));
	NotifyObservers(UIModelChangeHarvest);
	return true;
}

// -------------------------------------------------------------------------
// M36.2 — Crafting / profession Apply functions
// -------------------------------------------------------------------------

bool UIModelBinding::ApplyProfessionUpdate(std::span<const std::byte> packet)
{
	m_professionUpdateScratch = {};
	if (!engine::server::DecodeProfessionUpdate(packet, m_professionUpdateScratch))
	{
		LOG_WARN(Net, "[UIModelBinding] ApplyProfessionUpdate FAILED: decode error");
		return false;
	}

	m_model.crafting.professions.clear();
	m_model.crafting.professions.reserve(m_professionUpdateScratch.professions.size());
	for (const engine::server::ProfessionWireEntry& e : m_professionUpdateScratch.professions)
	{
		UIProfessionEntry entry{};
		entry.professionKey = e.professionKey;
		entry.skillLevel    = e.skillLevel;
		entry.isPrimary     = (e.isPrimary != 0);
		m_model.crafting.professions.push_back(std::move(entry));
	}

	LOG_INFO(Net, "[UIModelBinding] ProfessionUpdate applied (professions={})",
	         m_model.crafting.professions.size());
	NotifyObservers(UIModelChangeCrafting);
	return true;
}

bool UIModelBinding::ApplyCraftRecipeListResult(std::span<const std::byte> packet)
{
	m_craftRecipeListScratch = {};
	if (!engine::server::DecodeCraftRecipeListResult(packet, m_craftRecipeListScratch))
	{
		LOG_WARN(Net, "[UIModelBinding] ApplyCraftRecipeListResult FAILED: decode error");
		return false;
	}

	m_model.crafting.activeProfessionKey = m_craftRecipeListScratch.professionKey;
	m_model.crafting.recipes.clear();
	m_model.crafting.recipes.reserve(m_craftRecipeListScratch.recipes.size());
	for (const engine::server::CraftRecipeWireRow& r : m_craftRecipeListScratch.recipes)
	{
		UICraftRecipeRow row{};
		row.recipeId       = r.recipeId;
		row.skillRequired  = r.skillRequired;
		row.outputItemId   = r.outputItemId;
		row.outputQuantity = r.outputQuantity;
		m_model.crafting.recipes.push_back(std::move(row));
	}
	m_model.crafting.selectedRecipeIndex = UINT32_MAX;

	LOG_INFO(Net, "[UIModelBinding] CraftRecipeListResult applied (profession='{}' recipes={})",
	         m_model.crafting.activeProfessionKey, m_model.crafting.recipes.size());
	NotifyObservers(UIModelChangeCrafting);
	return true;
}

bool UIModelBinding::ApplyCraftStart(std::span<const std::byte> packet)
{
	m_craftStartScratch = {};
	if (!engine::server::DecodeCraftStart(packet, m_craftStartScratch))
	{
		LOG_WARN(Net, "[UIModelBinding] ApplyCraftStart FAILED: decode error");
		return false;
	}

	m_model.crafting.craftingRecipeId  = m_craftStartScratch.recipeId;
	m_model.crafting.craftDurationTicks = m_craftStartScratch.durationTicks;
	m_model.crafting.craftFillFraction = 0.0f;
	m_model.crafting.isCrafting        = true;

	LOG_INFO(Net, "[UIModelBinding] CraftStart applied (recipe='{}' durationTicks={})",
	         m_craftStartScratch.recipeId, m_craftStartScratch.durationTicks);
	NotifyObservers(UIModelChangeCrafting);
	return true;
}

bool UIModelBinding::ApplyCraftComplete(std::span<const std::byte> packet)
{
	m_craftCompleteScratch = {};
	if (!engine::server::DecodeCraftComplete(packet, m_craftCompleteScratch))
	{
		LOG_WARN(Net, "[UIModelBinding] ApplyCraftComplete FAILED: decode error");
		return false;
	}

	m_model.crafting.isCrafting         = false;
	m_model.crafting.craftFillFraction  = 1.0f;
	m_model.crafting.lastSkillGained    = m_craftCompleteScratch.skillGained;
	m_model.crafting.lastNewSkillLevel  = m_craftCompleteScratch.newSkillLevel;
	/// M36.3 — quality tier of the crafted item.
	m_model.crafting.lastQualityTier    = m_craftCompleteScratch.qualityTier;
	m_model.crafting.craftingRecipeId.clear();

	LOG_INFO(Net,
	         "[UIModelBinding] CraftComplete applied (recipe='{}' skillGained={} newLevel={} quality={})",
	         m_craftCompleteScratch.recipeId, m_craftCompleteScratch.skillGained,
	         m_craftCompleteScratch.newSkillLevel, m_craftCompleteScratch.qualityTier);
	NotifyObservers(UIModelChangeCrafting);
	return true;
}

bool UIModelBinding::ApplyCraftCancelled(std::span<const std::byte> packet)
{
	m_craftCancelledScratch = {};
	if (!engine::server::DecodeCraftCancelled(packet, m_craftCancelledScratch))
	{
		LOG_WARN(Net, "[UIModelBinding] ApplyCraftCancelled FAILED: decode error");
		return false;
	}

	m_model.crafting.isCrafting        = false;
	m_model.crafting.craftFillFraction = 0.0f;
	m_model.crafting.craftingRecipeId.clear();

	LOG_INFO(Net, "[UIModelBinding] CraftCancelled applied (recipe='{}')",
	         m_craftCancelledScratch.recipeId);
	NotifyObservers(UIModelChangeCrafting);
	return true;
}
}
