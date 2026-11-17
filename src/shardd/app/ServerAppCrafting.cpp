// ServerApp — métiers : récolte et artisanat.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	// -------------------------------------------------------------------------
	// M36.1 — Gathering / harvesting resource nodes handlers
	// -------------------------------------------------------------------------

	bool ServerApp::InitGathering()
	{
		if (!m_gatheringSystem.Init(m_config, m_tickHz, m_nextServerEntityId))
		{
			LOG_WARN(Core, "[ServerApp] GatheringSystem Init FAILED — harvesting disabled");
			return false;
		}
		// Métiers SP1 — les nodes entrent dans la grille d'intérêt de leur zone :
		// ils sont alors répliqués par les snapshots (TryBuildSnapshotEntity) et
		// le client peut les afficher et cibler leur entityId au HarvestRequest.
		for (const ResourceNodeRuntimeState& node : m_gatheringSystem.Nodes())
		{
			CellGrid* zoneGrid = GetOrCreateZoneGrid(node.definition.zoneId);
			if (zoneGrid == nullptr)
			{
				LOG_WARN(Net, "[ServerApp] Gathering node grid skipped: no zone grid (node={}, zone_id={})",
					node.definition.nodeId,
					node.definition.zoneId);
				continue;
			}
			CellCoord nodeCell{};
			if (!zoneGrid->UpsertEntity(node.entityId,
				node.definition.positionMetersX, node.definition.positionMetersZ, nodeCell))
			{
				LOG_WARN(Net, "[ServerApp] Gathering node grid insert FAILED (node={})", node.definition.nodeId);
			}
		}
		LOG_INFO(Core, "[ServerApp] GatheringSystem Init OK (nodes={})", m_gatheringSystem.NodeCount());
		return true;
	}

	void ServerApp::HandleHarvestRequest(const Endpoint& endpoint, const HarvestRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Gameplay, "[GatheringSystem] HarvestRequest: unknown endpoint");
			return;
		}
		if (client->clientId != message.clientId)
		{
			LOG_WARN(Gameplay, "[GatheringSystem] HarvestRequest: clientId mismatch (expected={}, got={})",
			         client->clientId, message.clientId);
			return;
		}
		if (!m_gatheringSystem.IsInitialized())
		{
			LOG_WARN(Gameplay, "[GatheringSystem] HarvestRequest ignored: system not initialized");
			return;
		}

		uint32_t durationTicks = 0;
		const HarvestOpResult result = m_gatheringSystem.TryStartHarvest(
			*client, message.nodeEntityId, m_currentTick, durationTicks);

		switch (result)
		{
		case HarvestOpResult::Ok:
		{
			const HarvestStartMessage startMsg{ message.nodeEntityId, durationTicks };
			const auto packet = EncodeHarvestStart(startMsg);
			SendDatagram(client->endpoint, std::span<const std::byte>(packet.data(), packet.size()));
			LOG_INFO(Gameplay, "[GatheringSystem] HarvestRequest OK: client={} node={} ticks={}",
			         client->clientId, message.nodeEntityId, durationTicks);
			break;
		}
		case HarvestOpResult::NodeNotFound:
			LOG_WARN(Gameplay, "[GatheringSystem] HarvestRequest: node {} not found", message.nodeEntityId);
			break;
		case HarvestOpResult::NodeNotAvailable:
			LOG_WARN(Gameplay, "[GatheringSystem] HarvestRequest: node {} depleted", message.nodeEntityId);
			break;
		case HarvestOpResult::OutOfRange:
			LOG_WARN(Gameplay, "[GatheringSystem] HarvestRequest: client {} out of range of node {}",
			         client->clientId, message.nodeEntityId);
			break;
		case HarvestOpResult::AlreadyHarvesting:
			LOG_WARN(Gameplay, "[GatheringSystem] HarvestRequest: client {} already harvesting",
			         client->clientId);
			break;
		}
	}

	void ServerApp::HandleHarvestCancelRequest(const Endpoint& endpoint, [[maybe_unused]] const HarvestCancelRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Gameplay, "[GatheringSystem] HarvestCancelRequest: unknown endpoint");
			return;
		}
		if (!m_gatheringSystem.IsInitialized())
		{
			return;
		}

		const EntityId nodeId = m_gatheringSystem.CancelHarvest(client->clientId);
		if (nodeId != 0)
		{
			(void)SendHarvestCancelled(*client, nodeId, HarvestCancelReason::PlayerRequested);
		}
	}

	void ServerApp::TickGathering()
	{
		if (!m_gatheringSystem.IsInitialized())
		{
			return;
		}

		std::vector<EntityId>                outCompletedNodeIds;
		std::vector<uint32_t>                outCompletedClientIds;
		std::vector<std::vector<ItemStack>>  outCompletedItems;
		std::vector<EntityId>                outMoveCancelledNodeIds;
		std::vector<uint32_t>                outMoveCancelledClientIds;

		m_gatheringSystem.Tick(
			m_currentTick,
			m_clients,
			outCompletedNodeIds,
			outCompletedClientIds,
			outCompletedItems,
			outMoveCancelledNodeIds,
			outMoveCancelledClientIds);

		/// Handle completed harvests — grant loot + notify client.
		for (size_t i = 0; i < outCompletedClientIds.size(); ++i)
		{
			ConnectedClient* client = FindConnectedClient(outCompletedClientIds[i]);
			if (client == nullptr)
			{
				continue;
			}

			/// Add loot to inventory.
			const std::vector<ItemStack>& loot = outCompletedItems[i];
			for (const ItemStack& item : loot)
			{
				AddItemToInventory(*client, item);
			}

			/// Send HarvestComplete then InventoryDelta.
			const HarvestCompleteMessage completeMsg{ outCompletedNodeIds[i] };
			const auto completePkt = EncodeHarvestComplete(completeMsg);
			SendDatagram(client->endpoint, std::span<const std::byte>(completePkt.data(), completePkt.size()));

			if (!loot.empty())
			{
				(void)SendInventoryDelta(
					*client,
					std::span<const ItemStack>(client->inventory.data(), client->inventory.size()));
				SaveConnectedClient(*client, "harvest_complete");
			}

			LOG_INFO(Gameplay,
			         "[GatheringSystem] Harvest delivered: client={} node={} items={}",
			         client->clientId, outCompletedNodeIds[i], loot.size());
		}

		/// Handle movement cancellations.
		for (size_t i = 0; i < outMoveCancelledClientIds.size(); ++i)
		{
			ConnectedClient* client = FindConnectedClient(outMoveCancelledClientIds[i]);
			if (client == nullptr)
			{
				continue;
			}
			(void)SendHarvestCancelled(*client, outMoveCancelledNodeIds[i], HarvestCancelReason::PlayerMoved);
		}
	}

	bool ServerApp::SendHarvestCancelled(
		const ConnectedClient& receiver,
		EntityId               nodeEntityId,
		HarvestCancelReason    reason)
	{
		const HarvestCancelledMessage msg{ nodeEntityId, reason };
		const auto packet = EncodeHarvestCancelled(msg);
		SendDatagram(receiver.endpoint, std::span<const std::byte>(packet.data(), packet.size()));
		LOG_DEBUG(Gameplay, "[GatheringSystem] HarvestCancelled sent: client={} node={} reason={}",
		          receiver.clientId, nodeEntityId, static_cast<uint8_t>(reason));
		return true;
	}

	// -------------------------------------------------------------------------
	// M36.2 — Crafting / profession skill system handlers
	// -------------------------------------------------------------------------

	bool ServerApp::InitCrafting()
	{
		if (!m_craftingSystem.Init(m_config, m_tickHz))
		{
			LOG_WARN(Core, "[ServerApp] CraftingSystem Init FAILED — crafting disabled");
			return false;
		}
		LOG_INFO(Core, "[ServerApp] CraftingSystem Init OK (recipes={})", m_craftingSystem.RecipeCount());
		return true;
	}

	void ServerApp::HandleLearnProfessionRequest(const Endpoint& endpoint, const LearnProfessionRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Gameplay, "[CraftingSystem] LearnProfession: unknown endpoint");
			return;
		}
		if (client->clientId != message.clientId)
		{
			LOG_WARN(Gameplay, "[CraftingSystem] LearnProfession: clientId mismatch");
			return;
		}
		if (!m_craftingSystem.IsInitialized())
		{
			LOG_WARN(Gameplay, "[CraftingSystem] LearnProfession: system not initialized");
			return;
		}

		const bool asPrimary = (message.asPrimary != 0);
		const LearnProfessionResult result = m_craftingSystem.LearnProfession(*client, message.professionKey, asPrimary);

		switch (result)
		{
		case LearnProfessionResult::Ok:
			(void)SendProfessionUpdate(*client);
			SaveConnectedClient(*client, "learn_profession");
			LOG_INFO(Gameplay, "[CraftingSystem] LearnProfession OK: client={} profession='{}'",
			         client->clientId, message.professionKey);
			break;
		case LearnProfessionResult::AlreadyKnown:
			SendChatSystemNotice(*client, "You already know that profession.");
			break;
		case LearnProfessionResult::TooManyPrimary:
			SendChatSystemNotice(*client, "You already have the maximum number of primary professions.");
			LOG_WARN(Gameplay, "[CraftingSystem] LearnProfession blocked: too many primary (client={})",
			         client->clientId);
			break;
		}
	}

	void ServerApp::HandleCraftRecipeListRequest(const Endpoint& endpoint, const CraftRecipeListRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Gameplay, "[CraftingSystem] RecipeListRequest: unknown endpoint");
			return;
		}
		if (!m_craftingSystem.IsInitialized())
		{
			return;
		}

		/// Find the player's skill level for the requested profession.
		uint32_t playerSkill = 0;
		for (const ProfessionEntry& e : client->professions)
		{
			if (e.professionKey == message.professionKey) { playerSkill = e.skillLevel; break; }
		}

		const std::vector<const RecipeDefinition*> recipes =
			m_craftingSystem.GetVisibleRecipes(message.professionKey, playerSkill);

		CraftRecipeListResultMessage result{};
		result.clientId      = client->clientId;
		result.professionKey = message.professionKey;
		for (const RecipeDefinition* r : recipes)
		{
			CraftRecipeWireRow row{};
			row.recipeId       = r->recipeId;
			row.skillRequired  = r->skillRequired;
			row.outputItemId   = r->outputItemId;
			row.outputQuantity = r->outputQuantity;
			result.recipes.push_back(std::move(row));
		}

		const auto packet = EncodeCraftRecipeListResult(result);
		SendDatagram(client->endpoint, std::span<const std::byte>(packet.data(), packet.size()));
		LOG_DEBUG(Gameplay, "[CraftingSystem] RecipeListResult sent: client={} profession='{}' recipes={}",
		          client->clientId, message.professionKey, result.recipes.size());
	}

	void ServerApp::HandleCraftRequest(const Endpoint& endpoint, const CraftRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Gameplay, "[CraftingSystem] CraftRequest: unknown endpoint");
			return;
		}
		if (client->clientId != message.clientId)
		{
			LOG_WARN(Gameplay, "[CraftingSystem] CraftRequest: clientId mismatch");
			return;
		}
		if (!m_craftingSystem.IsInitialized())
		{
			return;
		}

		uint32_t durationTicks = 0;
		const CraftOpResult result = m_craftingSystem.TryStartCraft(
			*client, message.recipeId, m_currentTick, durationTicks);

		switch (result)
		{
		case CraftOpResult::Ok:
		{
			const CraftStartMessage startMsg{ message.recipeId, durationTicks };
			const auto packet = EncodeCraftStart(startMsg);
			SendDatagram(client->endpoint, std::span<const std::byte>(packet.data(), packet.size()));
			LOG_INFO(Gameplay, "[CraftingSystem] CraftStart: client={} recipe='{}' ticks={}",
			         client->clientId, message.recipeId, durationTicks);
			break;
		}
		case CraftOpResult::RecipeNotFound:
			SendChatSystemNotice(*client, "Unknown recipe.");
			break;
		case CraftOpResult::NoProfession:
			SendChatSystemNotice(*client, "You don't have the required profession.");
			break;
		case CraftOpResult::SkillTooLow:
			SendChatSystemNotice(*client, "Your skill level is too low for this recipe.");
			break;
		case CraftOpResult::MissingIngredients:
			SendChatSystemNotice(*client, "You are missing required ingredients.");
			break;
		case CraftOpResult::AlreadyCrafting:
			SendChatSystemNotice(*client, "You are already crafting something.");
			break;
		}
	}

	void ServerApp::HandleCraftCancelRequest(const Endpoint& endpoint, [[maybe_unused]] const CraftCancelRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Gameplay, "[CraftingSystem] CraftCancelRequest: unknown endpoint");
			return;
		}
		if (!m_craftingSystem.IsInitialized())
		{
			return;
		}

		const std::string cancelled = m_craftingSystem.CancelCraft(client->clientId);
		if (!cancelled.empty())
		{
			const CraftCancelledMessage msg{ cancelled };
			const auto packet = EncodeCraftCancelled(msg);
			SendDatagram(client->endpoint, std::span<const std::byte>(packet.data(), packet.size()));
			LOG_INFO(Gameplay, "[CraftingSystem] CraftCancelled: client={} recipe='{}'",
			         client->clientId, cancelled);
		}
	}

	void ServerApp::TickCrafting()
	{
		if (!m_craftingSystem.IsInitialized())
		{
			return;
		}

		std::vector<uint32_t>          completedClientIds;
		std::vector<std::string>       completedRecipeIds;
		std::vector<uint8_t>           skillGained;
		std::vector<uint32_t>          newSkillLevel;
		std::vector<CraftQualityTier>  qualityTiers;

		m_craftingSystem.Tick(
			m_currentTick,
			m_clients,
			completedClientIds,
			completedRecipeIds,
			skillGained,
			newSkillLevel,
			qualityTiers);

		for (size_t i = 0; i < completedClientIds.size(); ++i)
		{
			ConnectedClient* client = FindConnectedClient(completedClientIds[i]);
			if (client == nullptr) continue;

			/// Send CraftComplete (includes quality tier from M36.3).
			CraftCompleteMessage completeMsg{};
			completeMsg.recipeId      = completedRecipeIds[i];
			completeMsg.skillGained   = skillGained[i];
			completeMsg.newSkillLevel = newSkillLevel[i];
			completeMsg.qualityTier   = static_cast<uint8_t>(qualityTiers[i]);
			const auto completePkt = EncodeCraftComplete(completeMsg);
			SendDatagram(client->endpoint, std::span<const std::byte>(completePkt.data(), completePkt.size()));

			/// Send InventoryDelta so the client refreshes its bag.
			(void)SendInventoryDelta(
				*client,
				std::span<const ItemStack>(client->inventory.data(), client->inventory.size()));

			/// If a skill-up occurred, send ProfessionUpdate.
			if (skillGained[i] != 0)
			{
				(void)SendProfessionUpdate(*client);
			}

			SaveConnectedClient(*client, "craft_complete");

			LOG_INFO(Gameplay,
			         "[CraftingSystem] Craft delivered: client={} recipe='{}' quality={} skillGained={} newLevel={}",
			         client->clientId, completedRecipeIds[i],
			         static_cast<uint8_t>(qualityTiers[i]), skillGained[i], newSkillLevel[i]);
		}
	}

	bool ServerApp::SendProfessionUpdate(const ConnectedClient& receiver)
	{
		ProfessionUpdateMessage msg{};
		msg.clientId = receiver.clientId;
		for (const ProfessionEntry& e : receiver.professions)
		{
			ProfessionWireEntry wire{};
			wire.professionKey = e.professionKey;
			wire.skillLevel    = e.skillLevel;
			wire.isPrimary     = e.isPrimary ? 1u : 0u;
			msg.professions.push_back(std::move(wire));
		}
		const auto packet = EncodeProfessionUpdate(msg);
		SendDatagram(receiver.endpoint, std::span<const std::byte>(packet.data(), packet.size()));
		LOG_DEBUG(Gameplay, "[CraftingSystem] ProfessionUpdate sent: client={} professions={}",
		          receiver.clientId, msg.professions.size());
		return true;
	}

	void ServerApp::ApplyPersistedProfessions(ConnectedClient& client, const PersistedCharacterState& state)
	{
		client.professions = state.professions;
		LOG_DEBUG(Gameplay, "[CraftingSystem] Professions restored: client={} count={}",
		          client.clientId, client.professions.size());
	}
}
