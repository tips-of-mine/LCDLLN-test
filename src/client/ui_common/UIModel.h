#pragma once

#include "src/client/chat/ChatWorldVisualPresenter.h"
#include "src/shared/math/Frustum.h"
#include "src/shared/math/Math.h"
#include "src/shared/network/ServerProtocol.h"

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
		UIModelChangeShop      = 1u << 11,
		/// M35.4 — auction house browse + filters.
		UIModelChangeAuction   = 1u << 12,
		/// M35.3 — trade window state (items, gold, lock/confirm flags).
		UIModelChangeTrade     = 1u << 13,
		/// M36.1 — harvest cast bar progress (start, fill, cancel/complete).
		UIModelChangeHarvest   = 1u << 14,
		/// M36.2 — crafting panel state (professions, recipe list, cast bar).
		UIModelChangeCrafting  = 1u << 15
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

	/// M35.4 — one auction listing row from \ref engine::server::AuctionListingWireRow.
	struct UIAuctionListingLine
	{
		uint32_t listingId = 0;
		uint32_t itemId = 0;
		uint32_t quantity = 0;
		uint32_t startBid = 0;
		uint32_t buyoutPrice = 0;
		uint32_t currentBid = 0;
		uint32_t expiresAtTick = 0;
	};

	/// M35.4 — client-side filters + snapshot rows (server authoritative).
	struct UIAuctionPanel
	{
		bool isOpen = false;
		uint32_t filterMinPrice = 0;
		uint32_t filterMaxPrice = 0;
		uint32_t filterItemId = 0;
		uint32_t sortMode = 0;
		uint32_t selectedRow = 0;
		std::vector<UIAuctionListingLine> listings;
	};

	/// M35.3 — one side of the trade window mirrored from \ref engine::server::TradeSideWire.
	struct UITradeSide
	{
		uint32_t                              clientId   = 0;
		uint32_t                              goldAmount = 0;
		bool                                  locked     = false;
		bool                                  confirmed  = false;
		std::vector<engine::server::ItemStack> items;
	};

	/// M35.3 — full trade window snapshot from \ref engine::server::TradeWindowUpdateMessage.
	struct UITradeWindowState
	{
		UITradeSide selfSide{};
		UITradeSide otherSide{};
		/// Ticks remaining in the 5 s anti-scam review window (0 when not in review).
		uint32_t    reviewTicksRemaining = 0;
		/// True while a trade session is active (window should be shown).
		bool        isOpen  = false;
		/// True when the local trade has been successfully completed.
		bool        isDone  = false;
		/// Human-readable reason when the trade was cancelled; empty otherwise.
		std::string cancelReason;
	};

	/// M36.2 — one profession entry mirrored from ProfessionUpdate packets.
	struct UIProfessionEntry
	{
		std::string professionKey;
		uint32_t    skillLevel = 1;
		bool        isPrimary  = false;
	};

	/// M36.2 — one recipe row from CraftRecipeListResult.
	struct UICraftRecipeRow
	{
		std::string recipeId;
		uint32_t    skillRequired  = 0;
		uint32_t    outputItemId   = 0;
		uint32_t    outputQuantity = 1;
	};

	/// M36.2 — full crafting panel state (professions + recipe list + active cast bar).
	struct UICraftingState
	{
		/// Known professions with skill levels.
		std::vector<UIProfessionEntry> professions;
		/// Recipe list for the currently open profession tab.
		std::string                    activeProfessionKey;
		std::vector<UICraftRecipeRow>  recipes;
		/// Index of the selected recipe in \ref recipes (UINT32_MAX = none).
		uint32_t                       selectedRecipeIndex = UINT32_MAX;
		/// Crafting cast bar progress (0.0-1.0); 0.0 = idle.
		float                          craftFillFraction = 0.0f;
		uint32_t                       craftDurationTicks = 0;
		std::string                    craftingRecipeId;
		/// True while a craft cast is in progress.
		bool                           isCrafting = false;
		/// Last skill-up result: 1 = gained, 0 = no gain.
		uint8_t                        lastSkillGained = 0;
		uint32_t                       lastNewSkillLevel = 0;
		/// M36.3 — quality tier of the last completed craft (0=Normal…3=Epic).
		uint8_t                        lastQualityTier = 0;
	};

	/// M36.1 — harvest cast bar state replicated from HarvestStart/Complete/Cancelled packets.
	struct UIHarvestProgress
	{
		/// Server-assigned entity id of the resource node being harvested.
		uint64_t nodeEntityId       = 0;
		/// Total duration of the cast in ticks (received from HarvestStart).
		uint32_t totalDurationTicks = 0;
		/// Ticks elapsed since the harvest started (incremented by ApplyModel tick-ups).
		uint32_t elapsedTicks       = 0;
		/// Fill fraction in [0, 1] computed from elapsed/total.
		float    fillFraction       = 0.0f;
		/// True while a harvest cast is in progress.
		bool     inProgress         = false;
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
		/// M35.4 — auction house panel (browse / filters).
		UIAuctionPanel auction{};
		/// M35.3 — trade window state (both sides, lock/confirm, review timer).
		UITradeWindowState tradeWindow{};
		/// M36.1 — harvest cast bar progress state.
		UIHarvestProgress harvest{};
		/// M36.2 — crafting panel state (professions, recipe list, cast bar).
		UICraftingState crafting{};
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

		/// Close vendor shop panel locally (M35.2); does not notify the server.
		bool CloseShop();

		/// Close auction panel locally (M35.4).
		bool CloseAuction();

		/// Update auction browse query params (client-side; used for next AuctionBrowseRequest).
		bool ConfigureAuctionBrowse(uint32_t minPrice, uint32_t maxPrice, uint32_t itemIdFilter, uint32_t sortMode);

		/// Highlight one listing row (0-based); notifies \ref UIModelChangeAuction.
		bool SelectAuctionRow(uint32_t rowIndex);

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

		/// Apply one decoded AuctionBrowseResult message (M35.4).
		bool ApplyAuctionBrowseResult(std::span<const std::byte> packet);

		/// Apply one decoded TradeWindowUpdate message (M35.3).
		bool ApplyTradeWindowUpdate(std::span<const std::byte> packet);

		/// Apply one decoded TradeComplete message (M35.3).
		bool ApplyTradeComplete(std::span<const std::byte> packet);

		/// Apply one decoded TradeCancelled message (M35.3).
		bool ApplyTradeCancelled(std::span<const std::byte> packet);

		/// Apply one decoded HarvestStart message (M36.1).
		bool ApplyHarvestStart(std::span<const std::byte> packet);

		/// Apply one decoded HarvestComplete message (M36.1).
		bool ApplyHarvestComplete(std::span<const std::byte> packet);

		/// Apply one decoded HarvestCancelled message (M36.1).
		bool ApplyHarvestCancelled(std::span<const std::byte> packet);

		/// Apply one decoded ProfessionUpdate message (M36.2).
		bool ApplyProfessionUpdate(std::span<const std::byte> packet);

		/// Apply one decoded CraftRecipeListResult message (M36.2).
		bool ApplyCraftRecipeListResult(std::span<const std::byte> packet);

		/// Apply one decoded CraftStart message (M36.2).
		bool ApplyCraftStart(std::span<const std::byte> packet);

		/// Apply one decoded CraftComplete message (M36.2).
		bool ApplyCraftComplete(std::span<const std::byte> packet);

		/// Apply one decoded CraftCancelled message (M36.2).
		bool ApplyCraftCancelled(std::span<const std::byte> packet);

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
		engine::server::AuctionBrowseResultMessage m_auctionBrowseScratch{};
		engine::server::TradeWindowUpdateMessage   m_tradeWindowScratch{};
		engine::server::TradeCompleteMessage       m_tradeCompleteScratch{};
		engine::server::TradeCancelledMessage      m_tradeCancelledScratch{};
		engine::server::HarvestStartMessage        m_harvestStartScratch{};
		engine::server::HarvestCompleteMessage     m_harvestCompleteScratch{};
		engine::server::HarvestCancelledMessage    m_harvestCancelledScratch{};
		engine::server::ProfessionUpdateMessage    m_professionUpdateScratch{};
		engine::server::CraftRecipeListResultMessage m_craftRecipeListScratch{};
		engine::server::CraftStartMessage          m_craftStartScratch{};
		engine::server::CraftCompleteMessage       m_craftCompleteScratch{};
		engine::server::CraftCancelledMessage      m_craftCancelledScratch{};
		ChatWorldVisualPresenter m_chatWorld{};
		size_t m_nextObserverHandle = 1;
		bool m_initialized = false;
	};
}
