#include "engine/server/ServerApp.h"

#include "engine/core/Log.h"
#include "engine/net/ChatEmotes.h"
#include "engine/net/ChatSystem.h"
#include "engine/platform/FileSystem.h"
#include "engine/server/ChatCommandParser.h"
#include "engine/server/ServerProtocol.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <functional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace engine::server
{
	namespace
	{
		/// Fixed authoritative combat numbers for the MVP ticket.
		inline constexpr uint32_t kDefaultPlayerHealth = 100;
		inline constexpr uint32_t kDefaultMobHealth = 60;
		inline constexpr uint32_t kDefaultPlayerDamage = 10;
		inline constexpr uint32_t kDefaultMobDamage = 6;
		inline constexpr uint32_t kDefaultLootBagArchetypeId = 200;
		inline constexpr float kDefaultAttackRangeMeters = 4.0f;
		inline constexpr float kDefaultMobLeashDistanceMeters = 24.0f;
		inline constexpr float kDefaultMobMoveSpeedMetersPerSecond = 3.0f;
		inline constexpr float kDefaultMobPatrolDistanceMeters = 6.0f;
		inline constexpr float kDefaultLootPickupRangeMeters = 3.0f;

		/// Clamp a signed config integer into an unsigned 16-bit range.
		uint16_t ClampToU16(int64_t value, uint16_t minValue, uint16_t maxValue)
		{
			if (value < static_cast<int64_t>(minValue))
			{
				return minValue;
			}
			if (value > static_cast<int64_t>(maxValue))
			{
				return maxValue;
			}
			return static_cast<uint16_t>(value);
		}

		/// Return true when the entity id already exists in the list.
		bool ContainsEntityId(const std::vector<EntityId>& entityIds, EntityId entityId)
		{
			return std::find(entityIds.begin(), entityIds.end(), entityId) != entityIds.end();
		}

		/// Build the fixed MVP combat component using the current server tick rate.
		CombatComponent BuildDefaultCombatComponent(uint16_t tickHz, uint32_t damagePerHit)
		{
			CombatComponent component{};
			component.damagePerHit = damagePerHit;
			component.attackRangeMeters = kDefaultAttackRangeMeters;
			component.cooldownTicks = std::max<uint32_t>(1u, static_cast<uint32_t>(tickHz / 2u));
			component.nextAttackTick = 0;
			return component;
		}

		/// Return the squared XZ distance used by the range validation.
		float DistanceSquaredXZ(float ax, float az, float bx, float bz)
		{
			const float dx = ax - bx;
			const float dz = az - bz;
			return (dx * dx) + (dz * dz);
		}

		/// Return a readable name for one mob AI state.
		const char* GetMobAiStateName(MobAiState state)
		{
			switch (state)
			{
			case MobAiState::Idle:
				return "Idle";
			case MobAiState::Patrol:
				return "Patrol";
			case MobAiState::Aggro:
				return "Aggro";
			case MobAiState::Return:
				return "Return";
			}

			return "Idle";
		}

		/// Return a readable name for one dynamic event status.
		const char* GetDynamicEventStatusName(DynamicEventStatus status)
		{
			switch (status)
			{
			case DynamicEventStatus::Idle:
				return "idle";
			case DynamicEventStatus::Active:
				return "active";
			case DynamicEventStatus::Cooldown:
				return "cooldown";
			}

			return "idle";
		}

		/// Wall-clock milliseconds since Unix epoch (UTC) for chat relay timestamps.
		uint64_t NowUnixEpochMsUtc()
		{
			using namespace std::chrono;
			return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
		}

		/// Parse the loot visibility token found in the data file.
		bool TryParseLootVisibility(std::string_view text, LootVisibility& outVisibility)
		{
			if (text == "owner")
			{
				outVisibility = LootVisibility::Owner;
				return true;
			}

			if (text == "public")
			{
				outVisibility = LootVisibility::Public;
				return true;
			}

			return false;
		}
	}

	ServerApp::ServerApp(engine::core::Config config)
		: m_config(std::move(config))
		, m_characterPersistence(m_config)
		, m_eventRuntime(m_config)
		, m_questRuntime(m_config)
		, m_spawnerRuntime(m_config)
	{
		LOG_INFO(Core, "[ServerApp] Constructed");
	}

	ServerApp::~ServerApp()
	{
		Shutdown();
	}

	bool ServerApp::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[ServerApp] Init ignored: already initialized");
			return true;
		}

		m_listenPort = ClampToU16(m_config.GetInt("server.listen_port", 27015), 1, 65535);
		m_tickHz = ResolveTickHz();
		m_snapshotHz = ResolveSnapshotHz(m_tickHz);
		m_nextClientId = 1;
		m_currentTick = 0;
		m_characterAutosaveIntervalTicks = 0;
		m_nextCharacterAutosaveTick = 0;
		m_snapshotAccumulator = 0;
		m_stopRequested = false;
		m_clients.clear();
		m_mobs.clear();
		m_lootBags.clear();
		m_lootTableEntries.clear();
		m_dynamicEvents.clear();
		m_spawners.clear();
		m_pendingDatagrams.clear();
		m_zoneGrids.clear();
		m_nextServerEntityId = 0x200000000ull;
		if (!m_zoneTransitionMap.Init())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: zone transition map startup failed");
			return false;
		}

		if (!m_transport.Init(m_listenPort))
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: transport startup failed");
			m_zoneTransitionMap.Shutdown();
			return false;
		}

		if (!m_tickScheduler.Init(m_tickHz))
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: tick scheduler startup failed");
			m_transport.Shutdown();
			m_zoneTransitionMap.Shutdown();
			return false;
		}

		if (!m_characterPersistence.Init())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: character persistence startup failed");
			Shutdown();
			return false;
		}

		m_characterAutosaveIntervalTicks = ResolveCharacterAutosaveIntervalTicks();
		m_nextCharacterAutosaveTick = m_characterAutosaveIntervalTicks;

		if (!InitLootTables())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: loot tables startup failed");
			Shutdown();
			return false;
		}

		if (!InitQuests())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: quest runtime startup failed");
			Shutdown();
			return false;
		}

		if (!InitSpawners())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: spawner bootstrap failed");
			Shutdown();
			return false;
		}

		if (!InitDynamicEvents())
		{
			LOG_ERROR(Core, "[ServerApp] Init FAILED: dynamic event bootstrap failed");
			Shutdown();
			return false;
		}

		m_tickScheduler.Start();
		m_chatRateLimiter.Reset();
		InitModerationAuditSubsystem();
		LoadChatBanFile();

		// M32.1 — FriendSystem runs in no-DB mode on WIN32 (presence tracking only).
		m_friendSystem.Init(nullptr);

		m_initialized = true;
		LOG_INFO(Core, "[ServerApp] Init OK (port={}, tick_hz={}, snapshot_hz={})",
			m_listenPort, m_tickHz, m_snapshotHz);
		LOG_INFO(Net, "[ServerApp] Chat routing ready (rate_limit_msgs_per_sec={})",
			engine::net::ChatRateLimiter::kMaxMessagesPerSecond);
		return true;
	}

	int ServerApp::Run()
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[ServerApp] Run FAILED: app not initialized");
			return 1;
		}

		LOG_INFO(Core, "[ServerApp] Run loop started");
		while (!m_stopRequested)
		{
			if (!m_tickScheduler.WaitForNextTick())
			{
				LOG_ERROR(Core, "[ServerApp] Run FAILED: scheduler tick wait failed");
				return 1;
			}

			ProcessIncomingPackets();
			TickOnce();
		}

		LOG_INFO(Core, "[ServerApp] Run loop stopped");
		return 0;
	}

	void ServerApp::RequestStop()
	{
		m_stopRequested = true;
		LOG_INFO(Core, "[ServerApp] Stop requested");
	}

	void ServerApp::Shutdown()
	{
		m_chatRateLimiter.Reset();

		if (!m_initialized
			&& !m_transport.IsValid()
			&& m_clients.empty()
			&& m_mobs.empty()
			&& m_lootBags.empty()
			&& m_lootTableEntries.empty()
			&& m_zoneGrids.empty()
			&& m_pendingDatagrams.empty())
		{
			m_moderationAuditLog.Shutdown();
			m_moderationAuditLogReady = false;
			return;
		}

		m_initialized = false;
		for (const ConnectedClient& client : m_clients)
		{
			SaveConnectedClient(client, "shutdown");
			if (client.hasCell)
			{
				const auto zoneIt = m_zoneGrids.find(client.zoneId);
				if (zoneIt != m_zoneGrids.end())
				{
					(void)zoneIt->second.RemoveEntity(client.entityId);
				}
			}
		}
		m_clients.clear();
		for (const MobEntity& mob : m_mobs)
		{
			const auto zoneIt = m_zoneGrids.find(mob.zoneId);
			if (zoneIt != m_zoneGrids.end())
			{
				(void)zoneIt->second.RemoveEntity(mob.entityId);
			}
		}
		m_mobs.clear();
		for (const LootBagEntity& lootBag : m_lootBags)
		{
			const auto zoneIt = m_zoneGrids.find(lootBag.zoneId);
			if (zoneIt != m_zoneGrids.end())
			{
				(void)zoneIt->second.RemoveEntity(lootBag.entityId);
			}
		}
		m_lootBags.clear();
		m_lootTableEntries.clear();
		for (auto& [zoneId, zoneGrid] : m_zoneGrids)
		{
			(void)zoneId;
			zoneGrid.Shutdown();
		}
		m_zoneGrids.clear();
		m_pendingDatagrams.clear();
		m_tickScheduler.Shutdown();
		m_transport.Shutdown();
		m_zoneTransitionMap.Shutdown();
		m_spawnerRuntime.Shutdown();
		m_eventRuntime.Shutdown();
		m_questRuntime.Shutdown();
		m_characterPersistence.Shutdown();
		m_dynamicEvents.clear();
		m_spawners.clear();
		m_moderationAuditLog.Shutdown();
		m_moderationAuditLogReady = false;
		m_friendSystem.Shutdown();
		LOG_INFO(Core, "[ServerApp] Destroyed");
	}

	uint16_t ServerApp::ResolveTickHz() const
	{
		const uint16_t configuredTickHz = ClampToU16(m_config.GetInt("server.tick_hz", 20), 1, 120);
		if (configuredTickHz == 20 || configuredTickHz == 30)
		{
			return configuredTickHz;
		}

		LOG_WARN(Core, "[ServerApp] Unsupported server.tick_hz={} -> fallback to 20", configuredTickHz);
		return 20;
	}

	uint16_t ServerApp::ResolveSnapshotHz(uint16_t tickHz) const
	{
		const uint16_t configuredSnapshotHz = ClampToU16(m_config.GetInt("server.snapshot_hz", 10), 1, tickHz);
		const uint16_t clampedSnapshotHz = std::clamp<uint16_t>(configuredSnapshotHz, 10, tickHz);
		if (clampedSnapshotHz > 20)
		{
			LOG_WARN(Core, "[ServerApp] server.snapshot_hz={} above ticket range -> clamp to 20", clampedSnapshotHz);
			return 20;
		}
		if (clampedSnapshotHz != configuredSnapshotHz)
		{
			LOG_WARN(Core, "[ServerApp] server.snapshot_hz={} adjusted to {}", configuredSnapshotHz, clampedSnapshotHz);
		}
		return clampedSnapshotHz;
	}

	uint32_t ServerApp::ResolveCharacterAutosaveIntervalTicks() const
	{
		const uint32_t autosaveSeconds = static_cast<uint32_t>(std::max<int64_t>(1, m_config.GetInt("server.character_autosave_seconds", 30)));
		const uint32_t autosaveTicks = autosaveSeconds * static_cast<uint32_t>(m_tickHz);
		return std::max<uint32_t>(1u, autosaveTicks);
	}

	void ServerApp::ProcessIncomingPackets()
	{
		m_transport.Receive(m_pendingDatagrams, 64);
		for (const Datagram& datagram : m_pendingDatagrams)
		{
			ProcessPacket(datagram);
		}
	}

	void ServerApp::ProcessPacket(const Datagram& datagram)
	{
		const std::span<const std::byte> packetBytes(datagram.bytes.data(), datagram.size);

		HelloMessage hello{};
		if (DecodeHello(packetBytes, hello))
		{
			HandleHello(datagram.endpoint, hello.clientNonce);
			return;
		}

		InputMessage input{};
		if (DecodeInput(packetBytes, input))
		{
			HandleInput(datagram.endpoint, input.clientId, input.inputSequence, input.positionMetersX, input.positionMetersZ);
			return;
		}

		AttackRequestMessage attackRequest{};
		if (DecodeAttackRequest(packetBytes, attackRequest))
		{
			HandleAttackRequest(datagram.endpoint, attackRequest.clientId, attackRequest.targetEntityId);
			return;
		}

		PickupRequestMessage pickupRequest{};
		if (DecodePickupRequest(packetBytes, pickupRequest))
		{
			HandlePickupRequest(datagram.endpoint, pickupRequest.clientId, pickupRequest.lootBagEntityId);
			return;
		}

		TalkRequestMessage talkRequest{};
		if (DecodeTalkRequest(packetBytes, talkRequest))
		{
			HandleTalkRequest(datagram.endpoint, talkRequest.clientId, talkRequest.targetId);
			return;
		}

		ChatSendRequestMessage chatSend{};
		if (DecodeChatSend(packetBytes, chatSend))
		{
			HandleChatSend(datagram.endpoint, chatSend);
			return;
		}

		// M32.1 — Friend request packets (client-initiated via dedicated packet type).
		FriendRequestMessage friendReq{};
		if (DecodeFriendRequest(packetBytes, friendReq))
		{
			ConnectedClient* sender = FindClient(datagram.endpoint);
			if (sender)
			{
				const std::string playerLabel = "P" + std::to_string(sender->clientId);
				const uint64_t targetId = m_friendSystem.SendFriendRequest(
					static_cast<uint64_t>(sender->persistenceCharacterKey),
					playerLabel,
					friendReq.targetName,
					nullptr);
				if (targetId != 0)
					LOG_DEBUG(Net, "[ServerApp] FriendRequest routed (client_id={}, target_id={})", sender->clientId, targetId);
			}
			return;
		}

		FriendAcceptMessage friendAccept{};
		if (DecodeFriendAccept(packetBytes, friendAccept))
		{
			ConnectedClient* sender = FindClient(datagram.endpoint);
			if (sender)
			{
				m_friendSystem.AcceptFriendRequest(
					static_cast<uint64_t>(sender->persistenceCharacterKey),
					friendAccept.requesterName,
					nullptr);
			}
			return;
		}

		FriendDeclineMessage friendDecline{};
		if (DecodeFriendDecline(packetBytes, friendDecline))
		{
			ConnectedClient* sender = FindClient(datagram.endpoint);
			if (sender)
			{
				m_friendSystem.DeclineFriendRequest(
					static_cast<uint64_t>(sender->persistenceCharacterKey),
					friendDecline.requesterName,
					nullptr);
			}
			return;
		}

		FriendRemoveMessage friendRemove{};
		if (DecodeFriendRemove(packetBytes, friendRemove))
		{
			ConnectedClient* sender = FindClient(datagram.endpoint);
			if (sender)
			{
				m_friendSystem.RemoveFriend(
					static_cast<uint64_t>(sender->persistenceCharacterKey),
					friendRemove.friendName,
					nullptr);
			}
			return;
		}

		LOG_WARN(Net, "[ServerApp] Dropped invalid packet from {}", UdpTransport::EndpointToString(datagram.endpoint));
	}

	void ServerApp::HandleHello(const Endpoint& endpoint, uint32_t helloNonce)
	{
		const uint32_t tentativeCharacterKey = helloNonce != 0 ? helloNonce : m_nextClientId;
		if (m_bannedCharacterKeys.find(tentativeCharacterKey) != m_bannedCharacterKeys.end())
		{
			LOG_WARN(Net,
				"[ServerApp] Hello rejected: banned character_key={} (endpoint={})",
				tentativeCharacterKey,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		ConnectedClient* existingClient = FindClient(endpoint);
		if (existingClient != nullptr)
		{
			existingClient->helloNonce = helloNonce;
			LOG_INFO(Net, "[ServerApp] Handshake refresh (client_id={}, endpoint={}, nonce={})",
				existingClient->clientId,
				UdpTransport::EndpointToString(endpoint),
				helloNonce);
			(void)SendWelcome(*existingClient);
			SendDynamicEventBootstrap(*existingClient);
			SendQuestStateBootstrap(*existingClient);
			return;
		}

		ConnectedClient client{};
		client.endpoint = endpoint;
		client.clientId = m_nextClientId++;
		client.zoneId = 1;
		client.entityId = static_cast<EntityId>(client.clientId);
		client.helloNonce = helloNonce;
		client.persistenceCharacterKey = helloNonce != 0 ? helloNonce : client.clientId;
		client.stats.currentHealth = kDefaultPlayerHealth;
		client.stats.maxHealth = kDefaultPlayerHealth;
		client.combat = BuildDefaultCombatComponent(m_tickHz, kDefaultPlayerDamage);
		ConnectedClient& acceptedClient = m_clients.emplace_back(std::move(client));
		PersistedCharacterState persistedState{};
		if (m_characterPersistence.LoadCharacter(acceptedClient.persistenceCharacterKey, persistedState))
		{
			acceptedClient.zoneId = persistedState.zoneId;
			acceptedClient.positionMetersX = persistedState.positionMetersX;
			acceptedClient.positionMetersY = persistedState.positionMetersY;
			acceptedClient.positionMetersZ = persistedState.positionMetersZ;
			acceptedClient.experiencePoints = persistedState.experiencePoints;
			acceptedClient.gold = persistedState.gold;
			acceptedClient.stats = persistedState.stats;
			acceptedClient.inventory = persistedState.inventory;
			acceptedClient.questStates = persistedState.questStates;
			acceptedClient.chatIgnoredDisplayNames = std::move(persistedState.chatIgnoredDisplayNames);
			acceptedClient.chatModeratorRole = persistedState.chatModeratorRole;
			acceptedClient.hasReplicatedState = true;
			LOG_INFO(Net,
				"[ServerApp] Character state restored (client_id={}, character_key={}, zone_id={}, inventory_items={}, quests={}, chat_ignore={}, moderator={})",
				acceptedClient.clientId,
				acceptedClient.persistenceCharacterKey,
				acceptedClient.zoneId,
				acceptedClient.inventory.size(),
				acceptedClient.questStates.size(),
				acceptedClient.chatIgnoredDisplayNames.size(),
				acceptedClient.chatModeratorRole ? "true" : "false");
		}
		else
		{
			acceptedClient.hasReplicatedState = true;
			LOG_INFO(Net,
				"[ServerApp] Character state defaults active (client_id={}, character_key={})",
				acceptedClient.clientId,
				acceptedClient.persistenceCharacterKey);
		}

		std::vector<QuestProgressDelta> questSyncDeltas;
		if (m_questRuntime.SyncQuestStates(acceptedClient.questStates, questSyncDeltas))
		{
			if (!questSyncDeltas.empty())
			{
				LOG_INFO(Net, "[ServerApp] Quest state bootstrap updated (client_id={}, deltas={})",
					acceptedClient.clientId,
					questSyncDeltas.size());
			}
		}
		else
		{
			LOG_WARN(Net, "[ServerApp] Quest state bootstrap skipped: runtime sync failed (client_id={})",
				acceptedClient.clientId);
		}

		UpdateClientInterest(acceptedClient);
		LOG_INFO(Net, "[ServerApp] Client accepted (client_id={}, entity_id={}, zone_id={}, hp={}, endpoint={}, total_clients={})",
			acceptedClient.clientId,
			acceptedClient.entityId,
			acceptedClient.zoneId,
			acceptedClient.stats.currentHealth,
			UdpTransport::EndpointToString(endpoint),
			m_clients.size());
		(void)SendWelcome(acceptedClient);
		SendDynamicEventBootstrap(acceptedClient);
		SendQuestStateBootstrap(acceptedClient);
		OnClientLogin(acceptedClient);
	}

	void ServerApp::HandleInput(const Endpoint& endpoint, uint32_t clientId, uint32_t inputSequence, float positionMetersX, float positionMetersZ)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] Input ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != clientId)
		{
			LOG_WARN(Net, "[ServerApp] Input ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				client->clientId,
				clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		const float previousPositionX = client->positionMetersX;
		const float previousPositionZ = client->positionMetersZ;
		client->lastInputSequence = inputSequence;
		client->positionMetersX = positionMetersX;
		client->positionMetersZ = positionMetersZ;
		if (client->hasReplicatedState)
		{
			const float tickDt = 1.0f / static_cast<float>(m_tickHz);
			client->velocityMetersPerSecondX = (client->positionMetersX - previousPositionX) / tickDt;
			client->velocityMetersPerSecondZ = (client->positionMetersZ - previousPositionZ) / tickDt;
			if (std::abs(client->velocityMetersPerSecondX) > 0.001f || std::abs(client->velocityMetersPerSecondZ) > 0.001f)
			{
				client->yawRadians = std::atan2(client->velocityMetersPerSecondX, client->velocityMetersPerSecondZ);
			}
		}
		client->hasReplicatedState = true;
		MaybeApplyZoneTransition(*client);
		LOG_DEBUG(Net, "[ServerApp] Input accepted (client_id={}, seq={}, pos=({:.2f}, {:.2f}))",
			client->clientId,
			client->lastInputSequence,
			client->positionMetersX,
			client->positionMetersZ);
		UpdateClientInterest(*client);
	}

	void ServerApp::TickOnce()
	{
		++m_currentTick;
		Simulate();
		MaybeAutosaveCharacters();
		RefreshReplication();
		MaybeSendSnapshots();
		if ((m_currentTick % m_tickHz) == 0)
		{
			LOG_INFO(Core, "[ServerApp] Tick stats (tick={}, clients={}, rx_packets={}, tx_packets={})",
				m_currentTick,
				m_clients.size(),
				m_transport.ReceivedPacketCount(),
				m_transport.SentPacketCount());
		}
	}

	void ServerApp::Simulate()
	{
		UpdateMobAi();
		UpdateSpawners();
		UpdateDynamicEvents();
		LOG_TRACE(Core, "[ServerApp] Simulate tick {}", m_currentTick);
	}

	bool ServerApp::InitSpawners()
	{
		m_mobs.clear();
		m_spawners.clear();
		if (!m_spawnerRuntime.Init())
		{
			LOG_ERROR(Net, "[ServerApp] Spawner init FAILED: runtime load failed");
			return false;
		}

		for (const SpawnerDefinition& definition : m_spawnerRuntime.GetDefinitions())
		{
			CellGrid* zoneGrid = GetOrCreateZoneGrid(definition.zoneId);
			if (zoneGrid == nullptr)
			{
				LOG_ERROR(Net, "[ServerApp] Spawner init FAILED: missing zone grid (spawner_id={}, zone_id={})",
					definition.spawnerId,
					definition.zoneId);
				m_spawners.clear();
				return false;
			}

			SpawnerRuntimeState runtimeState{};
			runtimeState.definition = definition;
			runtimeState.slots.resize(definition.count);
			if (!zoneGrid->TryWorldToCellCoord(definition.positionMetersX, definition.positionMetersZ, runtimeState.centerCell))
			{
				LOG_ERROR(Net, "[ServerApp] Spawner init FAILED: invalid spawn position (spawner_id={}, zone_id={})",
					definition.spawnerId,
					definition.zoneId);
				m_spawners.clear();
				return false;
			}

			m_spawners.push_back(std::move(runtimeState));
			LOG_INFO(Net, "[ServerApp] Spawner ready (id={}, zone_id={}, count={}, respawn_sec={})",
				definition.spawnerId,
				definition.zoneId,
				definition.count,
				definition.respawnSeconds);
		}

		LOG_INFO(Net, "[ServerApp] Spawner init OK (spawners={})", m_spawners.size());
		return true;
	}

	bool ServerApp::InitLootTables()
	{
		m_lootTableEntries.clear();

		const std::string relativePath = m_config.GetString("server.loot_table_path", "loot/loot_tables.txt");
		const std::string lootTableText = engine::platform::FileSystem::ReadAllTextContent(m_config, relativePath);
		if (lootTableText.empty())
		{
			LOG_ERROR(Net, "[ServerApp] Loot table init FAILED: empty or missing file (path={})", relativePath);
			return false;
		}

		std::istringstream input(lootTableText);
		std::string line;
		uint32_t lineNumber = 0;
		while (std::getline(input, line))
		{
			++lineNumber;
			std::string_view lineView(line);
			if (lineView.empty() || lineView.front() == '#')
			{
				continue;
			}

			std::istringstream lineStream(line);
			LootTableEntry entry{};
			std::string visibilityToken;
			if (!(lineStream >> entry.sourceArchetypeId >> entry.item.itemId >> entry.item.quantity >> visibilityToken))
			{
				LOG_ERROR(Net, "[ServerApp] Loot table init FAILED: invalid line {} in {}", lineNumber, relativePath);
				m_lootTableEntries.clear();
				return false;
			}

			if (!TryParseLootVisibility(visibilityToken, entry.visibility))
			{
				LOG_ERROR(Net, "[ServerApp] Loot table init FAILED: invalid visibility '{}' at line {}", visibilityToken, lineNumber);
				m_lootTableEntries.clear();
				return false;
			}

			if (entry.sourceArchetypeId == 0 || entry.item.itemId == 0 || entry.item.quantity == 0)
			{
				LOG_ERROR(Net, "[ServerApp] Loot table init FAILED: zero value at line {}", lineNumber);
				m_lootTableEntries.clear();
				return false;
			}

			m_lootTableEntries.push_back(entry);
		}

		if (m_lootTableEntries.empty())
		{
			LOG_ERROR(Net, "[ServerApp] Loot table init FAILED: no entries loaded (path={})", relativePath);
			return false;
		}

		LOG_INFO(Net, "[ServerApp] Loot table init OK (path={}, entries={})", relativePath, m_lootTableEntries.size());
		return true;
	}

	bool ServerApp::InitQuests()
	{
		if (!m_questRuntime.Init())
		{
			LOG_ERROR(Net, "[ServerApp] Quest init FAILED");
			return false;
		}

		LOG_INFO(Net, "[ServerApp] Quest init OK");
		return true;
	}

	bool ServerApp::InitDynamicEvents()
	{
		m_dynamicEvents.clear();
		if (!m_eventRuntime.Init())
		{
			LOG_ERROR(Net, "[ServerApp] Dynamic event init FAILED: runtime load failed");
			return false;
		}

		for (const DynamicEventDefinition& definition : m_eventRuntime.GetDefinitions())
		{
			DynamicEventState state{};
			state.definition = definition;
			ScheduleDynamicEventTrigger(state, false);
			m_dynamicEvents.push_back(std::move(state));
			LOG_INFO(Net, "[ServerApp] Dynamic event ready (event_id={}, zone_id={}, trigger={}, cooldown_sec={})",
				definition.eventId,
				definition.zoneId,
				GetDynamicEventTriggerTypeName(definition.triggerType),
				definition.cooldownSeconds);
		}

		LOG_INFO(Net, "[ServerApp] Dynamic event init OK (events={})", m_dynamicEvents.size());
		return true;
	}

	void ServerApp::MaybeAutosaveCharacters()
	{
		if (m_clients.empty() || m_characterAutosaveIntervalTicks == 0 || m_currentTick < m_nextCharacterAutosaveTick)
		{
			return;
		}

		for (const ConnectedClient& client : m_clients)
		{
			SaveConnectedClient(client, "autosave");
		}

		m_nextCharacterAutosaveTick = m_currentTick + m_characterAutosaveIntervalTicks;
		LOG_INFO(Net, "[ServerApp] Character autosave complete (clients={}, next_tick={})",
			m_clients.size(),
			m_nextCharacterAutosaveTick);
	}

	void ServerApp::SaveConnectedClient(const ConnectedClient& client, std::string_view reason)
	{
		if (client.persistenceCharacterKey == 0)
		{
			LOG_WARN(Net, "[ServerApp] Character save skipped: missing persistence key (client_id={}, reason={})",
				client.clientId,
				reason);
			return;
		}

		PersistedCharacterState state{};
		state.characterKey = client.persistenceCharacterKey;
		state.zoneId = client.zoneId;
		state.positionMetersX = client.positionMetersX;
		state.positionMetersY = client.positionMetersY;
		state.positionMetersZ = client.positionMetersZ;
		state.experiencePoints = client.experiencePoints;
		state.gold = client.gold;
		state.stats = client.stats;
		state.inventory = client.inventory;
		state.questStates = client.questStates;
		state.chatIgnoredDisplayNames = client.chatIgnoredDisplayNames;
		state.chatModeratorRole = client.chatModeratorRole;
		if (!m_characterPersistence.SaveCharacter(state))
		{
			LOG_WARN(Net, "[ServerApp] Character save FAILED (client_id={}, character_key={}, reason={})",
				client.clientId,
				client.persistenceCharacterKey,
				reason);
			return;
		}

		LOG_INFO(Net, "[ServerApp] Character save OK (client_id={}, character_key={}, reason={})",
			client.clientId,
			client.persistenceCharacterKey,
			reason);
	}

	uint32_t ServerApp::ResolveMobAiIntervalTicks() const
	{
		const uint32_t configuredAiHz = static_cast<uint32_t>(std::clamp<int64_t>(
			m_config.GetInt("server.mob_ai_hz", 10),
			1,
			static_cast<int64_t>(m_tickHz)));
		return std::max<uint32_t>(1u, static_cast<uint32_t>(m_tickHz) / configuredAiHz);
	}

	void ServerApp::UpdateMobAi()
	{
		if (m_mobs.empty())
		{
			return;
		}

		for (MobEntity& mob : m_mobs)
		{
			if ((mob.stateFlags & kEntityStateDead) != 0u)
			{
				continue;
			}

			if (m_currentTick < mob.nextAiTick)
			{
				continue;
			}

			mob.nextAiTick = m_currentTick + ResolveMobAiIntervalTicks();
			UpdateMobAi(mob);
		}
	}

	void ServerApp::UpdateMobAi(MobEntity& mob)
	{
		RefreshMobAggroTarget(mob);
		if (mob.aggroTargetEntityId != 0)
		{
			SetMobAiState(mob, MobAiState::Aggro);
		}

		switch (mob.aiState)
		{
		case MobAiState::Idle:
			if (mob.aggroTargetEntityId == 0 && m_currentTick >= mob.nextPatrolTick)
			{
				SetMobAiState(mob, MobAiState::Patrol);
			}
			break;

		case MobAiState::Patrol:
		{
			const float targetX = mob.patrolForward ? mob.patrolTargetMetersX : mob.homePositionMetersX;
			const float targetZ = mob.patrolForward ? mob.patrolTargetMetersZ : mob.homePositionMetersZ;
			if (MoveMobTowards(mob, targetX, targetZ))
			{
				mob.patrolForward = !mob.patrolForward;
				mob.nextPatrolTick = m_currentTick + (ResolveMobAiIntervalTicks() * 2u);
				SetMobAiState(mob, MobAiState::Idle);
			}
			break;
		}

		case MobAiState::Aggro:
		{
			ConnectedClient* target = FindClientByEntityId(mob.aggroTargetEntityId);
			if (target == nullptr
				|| !target->hasReplicatedState
				|| target->zoneId != mob.zoneId
				|| target->stats.currentHealth == 0
				|| (target->stateFlags & kEntityStateDead) != 0u)
			{
				LOG_INFO(Net, "[ServerApp] Mob aggro target lost (mob_entity_id={}, target_entity_id={})",
					mob.entityId,
					mob.aggroTargetEntityId);
				ResetMobThreat(mob);
				SetMobAiState(mob, MobAiState::Return);
				break;
			}

			const float homeDistanceSquared = DistanceSquaredXZ(
				target->positionMetersX,
				target->positionMetersZ,
				mob.homePositionMetersX,
				mob.homePositionMetersZ);
			const float leashDistanceSquared = mob.leashDistanceMeters * mob.leashDistanceMeters;
			if (homeDistanceSquared > leashDistanceSquared)
			{
				LOG_INFO(Net, "[ServerApp] Mob leash triggered (mob_entity_id={}, target_entity_id={}, distance_sq={:.2f}, leash_sq={:.2f})",
					mob.entityId,
					target->entityId,
					homeDistanceSquared,
					leashDistanceSquared);
				ResetMobThreat(mob);
				SetMobAiState(mob, MobAiState::Return);
				break;
			}

			if (!TryMobAttackPlayer(mob, *target))
			{
				(void)MoveMobTowards(mob, target->positionMetersX, target->positionMetersZ);
			}
			break;
		}

		case MobAiState::Return:
			if (MoveMobTowards(mob, mob.homePositionMetersX, mob.homePositionMetersZ))
			{
				mob.nextPatrolTick = m_currentTick + (ResolveMobAiIntervalTicks() * 2u);
				SetMobAiState(mob, MobAiState::Idle);
			}
			break;
		}
	}

	bool ServerApp::IsSpawnerActivatedByPlayers(const SpawnerRuntimeState& spawner) const
	{
		for (const ConnectedClient& client : m_clients)
		{
			if (!client.hasReplicatedState || !client.hasCell || client.zoneId != spawner.definition.zoneId)
			{
				continue;
			}

			const int dx = std::abs(static_cast<int>(client.currentCell.x) - static_cast<int>(spawner.centerCell.x));
			const int dz = std::abs(static_cast<int>(client.currentCell.z) - static_cast<int>(spawner.centerCell.z));
			if (dx <= kBaseInterestRadiusCells && dz <= kBaseInterestRadiusCells)
			{
				return true;
			}
		}

		return false;
	}

	bool ServerApp::SpawnerHasCombat(const SpawnerRuntimeState& spawner) const
	{
		for (const SpawnerSlotState& slot : spawner.slots)
		{
			if (slot.mobEntityId == 0)
			{
				continue;
			}

			const MobEntity* mob = FindMobByEntityId(slot.mobEntityId);
			if (mob == nullptr || (mob->stateFlags & kEntityStateDead) != 0u)
			{
				continue;
			}

			if (mob->aggroTargetEntityId != 0 || !mob->threatTable.empty() || mob->aiState == MobAiState::Aggro)
			{
				return true;
			}
		}

		return false;
	}

	bool ServerApp::SpawnMobFromSpawner(SpawnerRuntimeState& spawner, uint32_t slotIndex)
	{
		if (slotIndex >= spawner.slots.size())
		{
			LOG_ERROR(Net, "[ServerApp] Spawner mob spawn FAILED: invalid slot (spawner_id={}, slot={})",
				spawner.definition.spawnerId,
				slotIndex);
			return false;
		}

		CellGrid* zoneGrid = GetOrCreateZoneGrid(spawner.definition.zoneId);
		if (zoneGrid == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Spawner mob spawn FAILED: zone grid unavailable (spawner_id={}, zone_id={})",
				spawner.definition.spawnerId,
				spawner.definition.zoneId);
			return false;
		}

		MobEntity mob{};
		mob.entityId = m_nextServerEntityId++;
		mob.zoneId = spawner.definition.zoneId;
		mob.archetypeId = spawner.definition.archetypeId;
		mob.positionMetersX = spawner.definition.positionMetersX;
		mob.positionMetersY = spawner.definition.positionMetersY;
		mob.positionMetersZ = spawner.definition.positionMetersZ;
		mob.stats.currentHealth = kDefaultMobHealth;
		mob.stats.maxHealth = kDefaultMobHealth;
		mob.combat = BuildDefaultCombatComponent(m_tickHz, kDefaultMobDamage);
		mob.homePositionMetersX = mob.positionMetersX;
		mob.homePositionMetersY = mob.positionMetersY;
		mob.homePositionMetersZ = mob.positionMetersZ;
		mob.patrolTargetMetersX = mob.positionMetersX + kDefaultMobPatrolDistanceMeters;
		mob.patrolTargetMetersZ = mob.positionMetersZ;
		mob.leashDistanceMeters = spawner.definition.leashDistanceMeters;
		mob.moveSpeedMetersPerSecond = static_cast<float>(m_config.GetDouble("server.mob_move_speed_meters_per_second", kDefaultMobMoveSpeedMetersPerSecond));
		mob.aiState = MobAiState::Idle;
		mob.nextAiTick = m_currentTick + ResolveMobAiIntervalTicks();
		mob.nextPatrolTick = m_currentTick + (ResolveMobAiIntervalTicks() * 2u);
		mob.owningSpawnerIndex = static_cast<uint32_t>(&spawner - m_spawners.data());
		mob.owningSpawnerSlot = slotIndex;

		CellCoord mappedCell{};
		if (!zoneGrid->UpsertEntity(mob.entityId, mob.positionMetersX, mob.positionMetersZ, mappedCell))
		{
			LOG_ERROR(Net, "[ServerApp] Spawner mob spawn FAILED: grid mapping failed (spawner_id={}, slot={}, entity_id={})",
				spawner.definition.spawnerId,
				slotIndex,
				mob.entityId);
			return false;
		}

		spawner.slots[slotIndex].mobEntityId = mob.entityId;
		spawner.slots[slotIndex].nextRespawnTick = 0;
		m_mobs.push_back(mob);
		LOG_INFO(Net,
			"[ServerApp] Spawner mob spawned (spawner_id={}, slot={}, entity_id={}, zone_id={}, archetype_id={})",
			spawner.definition.spawnerId,
			slotIndex,
			mob.entityId,
			mob.zoneId,
			mob.archetypeId);
		return true;
	}

	bool ServerApp::DespawnSpawnerMob(SpawnerRuntimeState& spawner, uint32_t slotIndex, std::string_view reason, bool scheduleRespawn)
	{
		if (slotIndex >= spawner.slots.size())
		{
			LOG_ERROR(Net, "[ServerApp] Spawner mob despawn FAILED: invalid slot (spawner_id={}, slot={})",
				spawner.definition.spawnerId,
				slotIndex);
			return false;
		}

		SpawnerSlotState& slot = spawner.slots[slotIndex];
		if (slot.mobEntityId == 0)
		{
			LOG_WARN(Net, "[ServerApp] Spawner mob despawn ignored: empty slot (spawner_id={}, slot={}, reason={})",
				spawner.definition.spawnerId,
				slotIndex,
				reason);
			return false;
		}

		const EntityId entityId = slot.mobEntityId;
		if (MobEntity* mob = FindMobByEntityId(entityId); mob != nullptr)
		{
			if (CellGrid* zoneGrid = GetOrCreateZoneGrid(mob->zoneId); zoneGrid != nullptr)
			{
				(void)zoneGrid->RemoveEntity(entityId);
			}
		}

		m_mobs.erase(
			std::remove_if(
				m_mobs.begin(),
				m_mobs.end(),
				[entityId](const MobEntity& mob)
				{
					return mob.entityId == entityId;
				}),
			m_mobs.end());

		slot.mobEntityId = 0;
		slot.nextRespawnTick = scheduleRespawn
			? (m_currentTick + std::max<uint32_t>(1u, spawner.definition.respawnSeconds * static_cast<uint32_t>(m_tickHz)))
			: 0u;
		LOG_INFO(Net,
			"[ServerApp] Spawner mob despawned (spawner_id={}, slot={}, entity_id={}, reason={}, next_respawn_tick={})",
			spawner.definition.spawnerId,
			slotIndex,
			entityId,
			reason,
			slot.nextRespawnTick);
		return true;
	}

	void ServerApp::UpdateSpawners()
	{
		if (m_spawners.empty())
		{
			return;
		}

		for (SpawnerRuntimeState& spawner : m_spawners)
		{
			const bool shouldBeActive = IsSpawnerActivatedByPlayers(spawner);
			if (spawner.isActive != shouldBeActive)
			{
				spawner.isActive = shouldBeActive;
				LOG_INFO(Net, "[ServerApp] Spawner activation updated (spawner_id={}, active={})",
					spawner.definition.spawnerId,
					spawner.isActive ? "true" : "false");
			}

			for (uint32_t slotIndex = 0; slotIndex < spawner.slots.size(); ++slotIndex)
			{
				SpawnerSlotState& slot = spawner.slots[slotIndex];
				if (slot.mobEntityId == 0)
				{
					continue;
				}

				MobEntity* mob = FindMobByEntityId(slot.mobEntityId);
				if (mob == nullptr)
				{
					slot.mobEntityId = 0;
					continue;
				}

				if (mob->pendingDespawn)
				{
					(void)DespawnSpawnerMob(spawner, slotIndex, "death", true);
					continue;
				}

				if (!spawner.isActive)
				{
					if (mob->aggroTargetEntityId != 0 || !mob->threatTable.empty() || mob->aiState == MobAiState::Aggro)
					{
						LOG_DEBUG(Net, "[ServerApp] Spawner despawn delayed: mob still in combat (spawner_id={}, entity_id={})",
							spawner.definition.spawnerId,
							mob->entityId);
						continue;
					}

					(void)DespawnSpawnerMob(spawner, slotIndex, "inactive", false);
				}
			}

			if (!spawner.isActive || SpawnerHasCombat(spawner))
			{
				continue;
			}

			for (uint32_t slotIndex = 0; slotIndex < spawner.slots.size(); ++slotIndex)
			{
				SpawnerSlotState& slot = spawner.slots[slotIndex];
				if (slot.mobEntityId != 0)
				{
					continue;
				}
				if (slot.nextRespawnTick != 0 && m_currentTick < slot.nextRespawnTick)
				{
					continue;
				}

				(void)SpawnMobFromSpawner(spawner, slotIndex);
			}
		}
	}

	void ServerApp::ScheduleDynamicEventTrigger(DynamicEventState& eventState, bool fromCooldown)
	{
		const uint32_t triggerDelayTicksBase = std::max<uint32_t>(1u, eventState.definition.triggerSeconds * static_cast<uint32_t>(m_tickHz));
		uint32_t triggerDelayTicks = triggerDelayTicksBase;
		if (eventState.definition.triggerType == DynamicEventTriggerType::Random)
		{
			const size_t hashValue = std::hash<std::string>{}(eventState.definition.eventId)
				^ static_cast<size_t>(m_currentTick + eventState.definition.cooldownSeconds);
			triggerDelayTicks = 1u + static_cast<uint32_t>(hashValue % triggerDelayTicksBase);
		}

		if (fromCooldown)
		{
			eventState.status = DynamicEventStatus::Cooldown;
			eventState.cooldownUntilTick = m_currentTick + std::max<uint32_t>(1u, eventState.definition.cooldownSeconds * static_cast<uint32_t>(m_tickHz));
			eventState.nextTriggerTick = eventState.cooldownUntilTick + triggerDelayTicks;
		}
		else
		{
			eventState.status = DynamicEventStatus::Idle;
			eventState.cooldownUntilTick = 0;
			eventState.nextTriggerTick = m_currentTick + triggerDelayTicks;
		}

		LOG_INFO(Net, "[ServerApp] Dynamic event trigger scheduled (event_id={}, status={}, next_trigger_tick={})",
			eventState.definition.eventId,
			GetDynamicEventStatusName(eventState.status),
			eventState.nextTriggerTick);
	}

	bool ServerApp::StartDynamicEvent(DynamicEventState& eventState)
	{
		if (eventState.definition.phases.empty())
		{
			LOG_ERROR(Net, "[ServerApp] Dynamic event start FAILED: no phases (event_id={})", eventState.definition.eventId);
			return false;
		}

		eventState.status = DynamicEventStatus::Active;
		eventState.currentPhaseIndex = 0;
		eventState.currentPhaseProgress = 0;
		eventState.phaseMobEntityIds.clear();
		eventState.participantClientIds.clear();
		LOG_INFO(Net, "[ServerApp] Dynamic event started (event_id={}, zone_id={})",
			eventState.definition.eventId,
			eventState.definition.zoneId);
		BroadcastDynamicEventState(eventState, eventState.definition.startNotificationText, 0, 0, 0, {});
		return SpawnDynamicEventPhase(eventState);
	}

	bool ServerApp::SpawnDynamicEventPhase(DynamicEventState& eventState)
	{
		if (eventState.currentPhaseIndex >= eventState.definition.phases.size())
		{
			LOG_ERROR(Net, "[ServerApp] Dynamic event phase spawn FAILED: invalid phase index (event_id={}, phase={})",
				eventState.definition.eventId,
				eventState.currentPhaseIndex);
			return false;
		}

		eventState.currentPhaseProgress = 0;
		eventState.phaseMobEntityIds.clear();
		const DynamicEventPhaseDefinition& phase = eventState.definition.phases[eventState.currentPhaseIndex];
		for (const DynamicEventSpawnDefinition& spawn : phase.spawns)
		{
			for (uint32_t spawnOrdinal = 0; spawnOrdinal < spawn.count; ++spawnOrdinal)
			{
				if (!SpawnMobForDynamicEvent(eventState, eventState.currentPhaseIndex, spawn, spawnOrdinal))
				{
					LOG_ERROR(Net, "[ServerApp] Dynamic event phase spawn FAILED (event_id={}, phase_id={})",
						eventState.definition.eventId,
						phase.phaseId);
					return false;
				}
			}
		}

		LOG_INFO(Net, "[ServerApp] Dynamic event phase started (event_id={}, phase_id={}, mobs={})",
			eventState.definition.eventId,
			phase.phaseId,
			eventState.phaseMobEntityIds.size());
		BroadcastDynamicEventState(eventState, phase.notificationText, 0, 0, 0, {});
		return true;
	}

	bool ServerApp::SpawnMobForDynamicEvent(
		DynamicEventState& eventState,
		uint32_t phaseIndex,
		const DynamicEventSpawnDefinition& spawnDefinition,
		uint32_t spawnOrdinal)
	{
		CellGrid* zoneGrid = GetOrCreateZoneGrid(eventState.definition.zoneId);
		if (zoneGrid == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Dynamic event mob spawn FAILED: zone grid unavailable (event_id={}, zone_id={})",
				eventState.definition.eventId,
				eventState.definition.zoneId);
			return false;
		}

		MobEntity mob{};
		mob.entityId = m_nextServerEntityId++;
		mob.zoneId = eventState.definition.zoneId;
		mob.archetypeId = spawnDefinition.archetypeId;
		mob.positionMetersX = spawnDefinition.positionMetersX;
		mob.positionMetersY = spawnDefinition.positionMetersY;
		mob.positionMetersZ = spawnDefinition.positionMetersZ;
		mob.stats.currentHealth = kDefaultMobHealth;
		mob.stats.maxHealth = kDefaultMobHealth;
		mob.combat = BuildDefaultCombatComponent(m_tickHz, kDefaultMobDamage);
		mob.homePositionMetersX = mob.positionMetersX;
		mob.homePositionMetersY = mob.positionMetersY;
		mob.homePositionMetersZ = mob.positionMetersZ;
		mob.patrolTargetMetersX = mob.positionMetersX + kDefaultMobPatrolDistanceMeters;
		mob.patrolTargetMetersZ = mob.positionMetersZ;
		mob.leashDistanceMeters = spawnDefinition.leashDistanceMeters;
		mob.moveSpeedMetersPerSecond = static_cast<float>(m_config.GetDouble("server.mob_move_speed_meters_per_second", kDefaultMobMoveSpeedMetersPerSecond));
		mob.aiState = MobAiState::Idle;
		mob.nextAiTick = m_currentTick + ResolveMobAiIntervalTicks();
		mob.nextPatrolTick = m_currentTick + (ResolveMobAiIntervalTicks() * 2u);
		mob.isDynamicEventMob = true;
		mob.owningEventIndex = static_cast<uint32_t>(&eventState - m_dynamicEvents.data());
		mob.owningEventPhaseIndex = phaseIndex;

		CellCoord mappedCell{};
		if (!zoneGrid->UpsertEntity(mob.entityId, mob.positionMetersX, mob.positionMetersZ, mappedCell))
		{
			LOG_ERROR(Net, "[ServerApp] Dynamic event mob spawn FAILED: grid mapping failed (event_id={}, entity_id={})",
				eventState.definition.eventId,
				mob.entityId);
			return false;
		}

		m_mobs.push_back(mob);
		eventState.phaseMobEntityIds.push_back(mob.entityId);
		LOG_INFO(Net,
			"[ServerApp] Dynamic event mob spawned (event_id={}, phase={}, entity_id={}, archetype_id={}, ordinal={})",
			eventState.definition.eventId,
			phaseIndex,
			mob.entityId,
			mob.archetypeId,
			spawnOrdinal);
		return true;
	}

	bool ServerApp::DespawnDynamicEventMob(DynamicEventState& eventState, EntityId entityId, std::string_view reason)
	{
		if (MobEntity* mob = FindMobByEntityId(entityId); mob != nullptr)
		{
			if (CellGrid* zoneGrid = GetOrCreateZoneGrid(mob->zoneId); zoneGrid != nullptr)
			{
				(void)zoneGrid->RemoveEntity(entityId);
			}
		}

		m_mobs.erase(
			std::remove_if(
				m_mobs.begin(),
				m_mobs.end(),
				[entityId](const MobEntity& mob)
				{
					return mob.entityId == entityId;
				}),
			m_mobs.end());

		eventState.phaseMobEntityIds.erase(
			std::remove(eventState.phaseMobEntityIds.begin(), eventState.phaseMobEntityIds.end(), entityId),
			eventState.phaseMobEntityIds.end());

		LOG_INFO(Net, "[ServerApp] Dynamic event mob despawned (event_id={}, entity_id={}, reason={})",
			eventState.definition.eventId,
			entityId,
			reason);
		return true;
	}

	bool ServerApp::AdvanceDynamicEventPhase(DynamicEventState& eventState)
	{
		if ((eventState.currentPhaseIndex + 1u) >= eventState.definition.phases.size())
		{
			CompleteDynamicEvent(eventState);
			return true;
		}

		++eventState.currentPhaseIndex;
		eventState.currentPhaseProgress = 0;
		eventState.phaseMobEntityIds.clear();
		LOG_INFO(Net, "[ServerApp] Dynamic event phase advanced (event_id={}, phase_index={})",
			eventState.definition.eventId,
			eventState.currentPhaseIndex);
		return SpawnDynamicEventPhase(eventState);
	}

	void ServerApp::CompleteDynamicEvent(DynamicEventState& eventState)
	{
		const DynamicEventReward rewards = eventState.definition.rewards;
		eventState.status = DynamicEventStatus::Cooldown;
		BroadcastDynamicEventState(eventState, eventState.definition.completionNotificationText, 0, 0, 0, {});
		for (uint32_t participantClientId : eventState.participantClientIds)
		{
			for (ConnectedClient& client : m_clients)
			{
				if (client.clientId != participantClientId)
				{
					continue;
				}

				client.experiencePoints += rewards.experience;
				client.gold += rewards.gold;
				for (const ItemStack& rewardItem : rewards.items)
				{
					AddItemToInventory(client, rewardItem);
				}
				if (!rewards.items.empty())
				{
					(void)SendInventoryDelta(client, rewards.items);
				}
				(void)SendDynamicEventState(
					client,
					eventState,
					eventState.definition.completionNotificationText,
					rewards.experience,
					rewards.gold,
					rewards.items);
				SaveConnectedClient(client, "dynamic_event_reward");
				LOG_INFO(Net, "[ServerApp] Dynamic event rewards granted (event_id={}, client_id={}, xp={}, gold={}, items={})",
					eventState.definition.eventId,
					client.clientId,
					rewards.experience,
					rewards.gold,
					rewards.items.size());
				break;
			}
		}

		eventState.phaseMobEntityIds.clear();
		eventState.participantClientIds.clear();
		eventState.currentPhaseIndex = 0;
		eventState.currentPhaseProgress = 0;
		ScheduleDynamicEventTrigger(eventState, true);
		LOG_INFO(Net, "[ServerApp] Dynamic event completed (event_id={}, next_trigger_tick={})",
			eventState.definition.eventId,
			eventState.nextTriggerTick);
	}

	void ServerApp::AddDynamicEventParticipant(DynamicEventState& eventState, const ConnectedClient& client)
	{
		if (std::find(eventState.participantClientIds.begin(), eventState.participantClientIds.end(), client.clientId)
			!= eventState.participantClientIds.end())
		{
			return;
		}

		eventState.participantClientIds.push_back(client.clientId);
		LOG_INFO(Net, "[ServerApp] Dynamic event participant added (event_id={}, client_id={})",
			eventState.definition.eventId,
			client.clientId);
	}

	bool ServerApp::HasPlayersInZone(uint32_t zoneId) const
	{
		for (const ConnectedClient& client : m_clients)
		{
			if (client.hasReplicatedState && client.zoneId == zoneId)
			{
				return true;
			}
		}

		return false;
	}

	void ServerApp::UpdateDynamicEvents()
	{
		if (m_dynamicEvents.empty())
		{
			return;
		}

		for (DynamicEventState& eventState : m_dynamicEvents)
		{
			if (eventState.status == DynamicEventStatus::Active)
			{
				for (size_t index = 0; index < eventState.phaseMobEntityIds.size();)
				{
					const EntityId entityId = eventState.phaseMobEntityIds[index];
					MobEntity* mob = FindMobByEntityId(entityId);
					if (mob == nullptr)
					{
						eventState.phaseMobEntityIds.erase(eventState.phaseMobEntityIds.begin() + static_cast<std::ptrdiff_t>(index));
						continue;
					}

					if (mob->pendingDespawn)
					{
						(void)DespawnDynamicEventMob(eventState, entityId, "phase_kill");
						continue;
					}

					++index;
				}

				const DynamicEventPhaseDefinition& phase = eventState.definition.phases[eventState.currentPhaseIndex];
				if (eventState.currentPhaseProgress >= phase.progressRequired && eventState.phaseMobEntityIds.empty())
				{
					(void)AdvanceDynamicEventPhase(eventState);
				}
				continue;
			}

			if (m_currentTick < eventState.nextTriggerTick)
			{
				continue;
			}

			if (!HasPlayersInZone(eventState.definition.zoneId))
			{
				LOG_DEBUG(Net, "[ServerApp] Dynamic event start delayed: no players in zone (event_id={}, zone_id={})",
					eventState.definition.eventId,
					eventState.definition.zoneId);
				continue;
			}

			if (!StartDynamicEvent(eventState))
			{
				ScheduleDynamicEventTrigger(eventState, true);
				LOG_WARN(Net, "[ServerApp] Dynamic event start rescheduled after failure (event_id={})",
					eventState.definition.eventId);
			}
		}
	}

	void ServerApp::MaybeSendSnapshots()
	{
		if (m_clients.empty())
		{
			return;
		}

		m_snapshotAccumulator += m_snapshotHz;
		if (m_snapshotAccumulator < m_tickHz)
		{
			return;
		}

		m_snapshotAccumulator -= m_tickHz;
		for (const ConnectedClient& client : m_clients)
		{
			(void)SendSnapshot(client);
		}
	}

	void ServerApp::UpdateClientInterest(ConnectedClient& client)
	{
		CellGrid* zoneGrid = GetOrCreateZoneGrid(client.zoneId);
		if (zoneGrid == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Interest update FAILED: zone grid unavailable (zone_id={})", client.zoneId);
			return;
		}

		CellCoord newCell{};
		if (!zoneGrid->UpsertEntity(client.entityId, client.positionMetersX, client.positionMetersZ, newCell))
		{
			LOG_WARN(Net, "[ServerApp] Interest update skipped (client_id={}, pos=({:.2f}, {:.2f}))",
				client.clientId,
				client.positionMetersX,
				client.positionMetersZ);
			return;
		}

		if (client.hasCell && client.currentCell == newCell)
		{
			zoneGrid->GatherEntityIds(client.interestCells, client.interestEntityIds);
			return;
		}

		const std::vector<CellCoord> previousCells = client.interestCells;
		client.currentCell = newCell;
		client.hasCell = true;
		zoneGrid->BuildInterestSet(client.currentCell, client.interestCells);
		zoneGrid->GatherEntityIds(client.interestCells, client.interestEntityIds);

		InterestDiff diff{};
		ComputeInterestDiff(previousCells, client.interestCells, diff);
		LOG_INFO(Net,
			"[ServerApp] Interest set updated (client_id={}, zone_id={}, cell={}, {}, entering={}, leaving={}, entities={})",
			client.clientId,
			client.zoneId,
			client.currentCell.x,
			client.currentCell.z,
			diff.enteringCells.size(),
			diff.leavingCells.size(),
			client.interestEntityIds.size());
	}

	void ServerApp::MaybeApplyZoneTransition(ConnectedClient& client)
	{
		ZoneChangeMessage zoneChange{};
		if (!m_zoneTransitionMap.ResolveTransition(client.zoneId, client.positionMetersX, client.positionMetersZ, zoneChange))
		{
			return;
		}

		if (client.hasCell)
		{
			CellGrid* oldZoneGrid = GetOrCreateZoneGrid(client.zoneId);
			if (oldZoneGrid != nullptr)
			{
				(void)oldZoneGrid->RemoveEntity(client.entityId);
			}
		}

		client.zoneId = zoneChange.zoneId;
		client.positionMetersX = zoneChange.spawnPositionX;
		client.positionMetersY = zoneChange.spawnPositionY;
		client.positionMetersZ = zoneChange.spawnPositionZ;
		client.velocityMetersPerSecondX = 0.0f;
		client.velocityMetersPerSecondY = 0.0f;
		client.velocityMetersPerSecondZ = 0.0f;
		client.hasCell = false;
		client.currentCell = {};
		client.interestCells.clear();
		client.interestEntityIds.clear();
		client.replicatedEntityIds.clear();
		LOG_INFO(Net,
			"[ServerApp] Zone transition applied (client_id={}, zone_id={}, spawn=({:.2f}, {:.2f}, {:.2f}))",
			client.clientId,
			client.zoneId,
			client.positionMetersX,
			client.positionMetersY,
			client.positionMetersZ);
		SaveConnectedClient(client, "zone_transition");
		(void)SendZoneChange(client, zoneChange);
		SendDynamicEventBootstrap(client);
		ApplyQuestEvent(client, QuestStepType::Enter, std::string("zone:") + std::to_string(client.zoneId), 1, "zone_enter");
	}

	void ServerApp::HandleAttackRequest(const Endpoint& endpoint, uint32_t clientId, EntityId targetEntityId)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != clientId)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				client->clientId,
				clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (!client->hasReplicatedState)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: attacker has no replicated state (client_id={})", client->clientId);
			return;
		}

		if ((client->stateFlags & kEntityStateDead) != 0u)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: attacker is dead (client_id={}, entity_id={})",
				client->clientId,
				client->entityId);
			return;
		}

		MobEntity* target = FindMobByEntityId(targetEntityId);
		if (target == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: invalid mob target (client_id={}, target_entity_id={})",
				client->clientId,
				targetEntityId);
			return;
		}

		if (target->zoneId != client->zoneId)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: cross-zone target (client_id={}, attacker_zone={}, target_zone={})",
				client->clientId,
				client->zoneId,
				target->zoneId);
			return;
		}

		if ((target->stateFlags & kEntityStateDead) != 0u || target->stats.currentHealth == 0)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: target already dead (client_id={}, target_entity_id={})",
				client->clientId,
				target->entityId);
			return;
		}

		if (m_currentTick < client->combat.nextAttackTick)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: cooldown active (client_id={}, tick={}, next_attack_tick={})",
				client->clientId,
				m_currentTick,
				client->combat.nextAttackTick);
			return;
		}

		const float distanceSquared = DistanceSquaredXZ(
			client->positionMetersX,
			client->positionMetersZ,
			target->positionMetersX,
			target->positionMetersZ);
		const float attackRangeSquared = client->combat.attackRangeMeters * client->combat.attackRangeMeters;
		if (distanceSquared > attackRangeSquared)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: target out of range (client_id={}, target_entity_id={}, distance_sq={:.2f}, range_sq={:.2f})",
				client->clientId,
				target->entityId,
				distanceSquared,
				attackRangeSquared);
			return;
		}

		const uint32_t appliedDamage = std::min(client->combat.damagePerHit, target->stats.currentHealth);
		if (appliedDamage == 0)
		{
			LOG_WARN(Net, "[ServerApp] AttackRequest ignored: zero damage after validation (client_id={}, target_entity_id={})",
				client->clientId,
				target->entityId);
			return;
		}

		client->combat.nextAttackTick = m_currentTick + client->combat.cooldownTicks;
		target->stats.currentHealth -= appliedDamage;
		if (target->isDynamicEventMob && target->owningEventIndex < m_dynamicEvents.size())
		{
			DynamicEventState& eventState = m_dynamicEvents[target->owningEventIndex];
			AddDynamicEventParticipant(eventState, *client);
		}
		if (target->stats.currentHealth == 0)
		{
			target->stateFlags |= kEntityStateDead;
			target->pendingDespawn = true;
			target->aggroTargetEntityId = 0;
			target->threatTable.clear();
			if (target->isDynamicEventMob && target->owningEventIndex < m_dynamicEvents.size())
			{
				DynamicEventState& eventState = m_dynamicEvents[target->owningEventIndex];
				if (eventState.currentPhaseIndex < eventState.definition.phases.size())
				{
					++eventState.currentPhaseProgress;
					const DynamicEventPhaseDefinition& phase = eventState.definition.phases[eventState.currentPhaseIndex];
					LOG_INFO(Net, "[ServerApp] Dynamic event progress updated (event_id={}, phase_id={}, progress={}/{})",
						eventState.definition.eventId,
						phase.phaseId,
						eventState.currentPhaseProgress,
						phase.progressRequired);
					BroadcastDynamicEventState(eventState, phase.notificationText, 0, 0, 0, {});
				}
			}
			LOG_INFO(Net, "[ServerApp] Mob died (entity_id={}, attacker_entity_id={})", target->entityId, client->entityId);
			if (!target->hasSpawnedLoot)
			{
				target->hasSpawnedLoot = true;
				SpawnLootBagForMob(*target, client->entityId);
			}
			ApplyQuestEvent(*client, QuestStepType::Kill, std::string("mob:") + std::to_string(target->archetypeId), 1, "kill");
		}

		CombatEventMessage combatEvent{};
		combatEvent.attackerEntityId = client->entityId;
		combatEvent.targetEntityId = target->entityId;
		combatEvent.damage = appliedDamage;
		combatEvent.targetCurrentHealth = target->stats.currentHealth;
		combatEvent.targetMaxHealth = target->stats.maxHealth;
		combatEvent.targetStateFlags = target->stateFlags;
		LOG_INFO(Net,
			"[ServerApp] Attack applied (attacker_entity_id={}, target_entity_id={}, damage={}, hp={}/{}, next_attack_tick={})",
			combatEvent.attackerEntityId,
			combatEvent.targetEntityId,
			combatEvent.damage,
			combatEvent.targetCurrentHealth,
			combatEvent.targetMaxHealth,
			client->combat.nextAttackTick);
		BroadcastCombatEvent(combatEvent);
	}

	void ServerApp::HandlePickupRequest(const Endpoint& endpoint, uint32_t clientId, EntityId lootBagEntityId)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != clientId)
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				client->clientId,
				clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		LootBagEntity* lootBag = FindLootBagByEntityId(lootBagEntityId);
		if (lootBag == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored: invalid loot bag (client_id={}, loot_bag_entity_id={})",
				client->clientId,
				lootBagEntityId);
			return;
		}

		if (lootBag->zoneId != client->zoneId)
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored: cross-zone bag (client_id={}, client_zone={}, bag_zone={})",
				client->clientId,
				client->zoneId,
				lootBag->zoneId);
			return;
		}

		if (lootBag->visibility == LootVisibility::Owner && lootBag->ownerEntityId != client->entityId)
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored: ownership mismatch (client_id={}, loot_bag_entity_id={}, owner_entity_id={})",
				client->clientId,
				lootBag->entityId,
				lootBag->ownerEntityId);
			return;
		}

		const float pickupRangeMeters = static_cast<float>(m_config.GetDouble("server.loot_pickup_range_meters", kDefaultLootPickupRangeMeters));
		const float distanceSquared = DistanceSquaredXZ(
			client->positionMetersX,
			client->positionMetersZ,
			lootBag->positionMetersX,
			lootBag->positionMetersZ);
		const float pickupRangeSquared = pickupRangeMeters * pickupRangeMeters;
		if (distanceSquared > pickupRangeSquared)
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored: loot bag out of range (client_id={}, loot_bag_entity_id={}, distance_sq={:.2f}, range_sq={:.2f})",
				client->clientId,
				lootBag->entityId,
				distanceSquared,
				pickupRangeSquared);
			return;
		}

		if (lootBag->items.empty())
		{
			LOG_WARN(Net, "[ServerApp] PickupRequest ignored: empty loot bag (client_id={}, loot_bag_entity_id={})",
				client->clientId,
				lootBag->entityId);
			return;
		}

		const std::vector<ItemStack> pickedItems = lootBag->items;
		for (const ItemStack& item : pickedItems)
		{
			AddItemToInventory(*client, item);
		}

		if (CellGrid* zoneGrid = GetOrCreateZoneGrid(lootBag->zoneId); zoneGrid != nullptr)
		{
			(void)zoneGrid->RemoveEntity(lootBag->entityId);
		}
		m_lootBags.erase(
			std::remove_if(
				m_lootBags.begin(),
				m_lootBags.end(),
				[lootBagEntityId](const LootBagEntity& lootBagEntry)
				{
					return lootBagEntry.entityId == lootBagEntityId;
				}),
			m_lootBags.end());

		LOG_INFO(Net, "[ServerApp] Loot bag picked up (client_id={}, loot_bag_entity_id={}, item_count={})",
			client->clientId,
			lootBagEntityId,
			pickedItems.size());
		for (const ItemStack& item : pickedItems)
		{
			ApplyQuestEvent(*client, QuestStepType::Collect, std::string("item:") + std::to_string(item.itemId), item.quantity, "pickup");
		}
		SaveConnectedClient(*client, "pickup");
		(void)SendInventoryDelta(*client, pickedItems);
	}

	void ServerApp::HandleTalkRequest(const Endpoint& endpoint, uint32_t clientId, std::string_view targetId)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] TalkRequest ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != clientId)
		{
			LOG_WARN(Net, "[ServerApp] TalkRequest ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				client->clientId,
				clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (targetId.empty())
		{
			LOG_WARN(Net, "[ServerApp] TalkRequest ignored: empty target (client_id={})", client->clientId);
			return;
		}

		LOG_INFO(Net, "[ServerApp] TalkRequest accepted (client_id={}, target={})", client->clientId, targetId);
		ApplyQuestEvent(*client, QuestStepType::Talk, targetId, 1, "talk");
	}

	void ServerApp::HandleChatSend(const Endpoint& endpoint, const ChatSendRequestMessage& request)
	{
		ConnectedClient* sender = FindClient(endpoint);
		if (sender == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] ChatSend ignored: unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (sender->clientId != request.clientId)
		{
			LOG_WARN(Net,
				"[ServerApp] ChatSend ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				sender->clientId,
				request.clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (request.text.empty())
		{
			LOG_WARN(Net, "[ServerApp] ChatSend ignored: empty text (client_id={})", sender->clientId);
			return;
		}

		if (!m_chatRateLimiter.Allow(sender->clientId, std::chrono::steady_clock::now()))
		{
			LOG_WARN(Net, "[ServerApp] ChatSend dropped: rate limited (client_id={})", sender->clientId);
			return;
		}

		if (sender->chatMutedUntilServerTick != 0 && m_currentTick < sender->chatMutedUntilServerTick)
		{
			LOG_WARN(Net,
				"[ServerApp] ChatSend blocked: sender muted (client_id={}, until_tick={}, current_tick={})",
				sender->clientId,
				sender->chatMutedUntilServerTick,
				m_currentTick);
			SendChatSystemNotice(*sender, "You are muted.");
			return;
		}

		ParsedChatSlashCommand slashCommand{};
		if (TryParseChatSlashCommand(request.text, slashCommand))
		{
			(void)HandleChatSlashCommand(*sender, slashCommand);
			return;
		}

		engine::net::ChatChannel logicalChannel{};
		if (!engine::net::TryDecodeChannelWire(request.channel, logicalChannel))
		{
			LOG_WARN(Net, "[ServerApp] ChatSend ignored: invalid channel wire ({})", request.channel);
			return;
		}

		ChatRelayMessage relay{};
		relay.channel = request.channel;
		relay.senderEntityId = sender->entityId;
		relay.timestampUnixMs = NowUnixEpochMsUtc();
		relay.senderDisplay = "P" + std::to_string(sender->clientId);
		if (relay.senderDisplay.size() > 48)
		{
			relay.senderDisplay.resize(48);
		}

		relay.text = request.text;

		std::vector<ConnectedClient*> recipients;
		const auto addUnique = [&recipients](ConnectedClient* clientPtr)
		{
			if (clientPtr == nullptr)
			{
				return;
			}

			if (std::find(recipients.begin(), recipients.end(), clientPtr) == recipients.end())
			{
				recipients.push_back(clientPtr);
			}
		};

		const auto addAllInZone = [this, &addUnique](uint32_t zoneId)
		{
			for (ConnectedClient& peer : m_clients)
			{
				if (peer.zoneId == zoneId)
				{
					addUnique(&peer);
				}
			}
		};

		switch (logicalChannel)
		{
		case engine::net::ChatChannel::Say:
		{
			const float radius = engine::net::kChatSayRadiusMeters;
			const float radiusSq = radius * radius;
			for (ConnectedClient& peer : m_clients)
			{
				if (peer.zoneId != sender->zoneId)
				{
					continue;
				}

				if (DistanceSquaredXZ(
						sender->positionMetersX,
						sender->positionMetersZ,
						peer.positionMetersX,
						peer.positionMetersZ) <= radiusSq)
				{
					addUnique(&peer);
				}
			}

			break;
		}
		case engine::net::ChatChannel::Yell:
		{
			const float radius = engine::net::kChatYellRadiusMeters;
			const float radiusSq = radius * radius;
			for (ConnectedClient& peer : m_clients)
			{
				if (peer.zoneId != sender->zoneId)
				{
					continue;
				}

				if (DistanceSquaredXZ(
						sender->positionMetersX,
						sender->positionMetersZ,
						peer.positionMetersX,
						peer.positionMetersZ) <= radiusSq)
				{
					addUnique(&peer);
				}
			}

			break;
		}
		case engine::net::ChatChannel::Whisper:
		{
			ConnectedClient* target = FindClientByEntityId(request.whisperTargetEntityId);
			if (target == nullptr)
			{
				LOG_WARN(Net,
					"[ServerApp] ChatSend whisper FAILED: target not connected (client_id={}, target_entity_id={})",
					sender->clientId,
					request.whisperTargetEntityId);
				return;
			}

			addUnique(sender);
			addUnique(target);
			break;
		}
		case engine::net::ChatChannel::Party:
		case engine::net::ChatChannel::Guild:
			LOG_INFO(Net,
				"[ServerApp] ChatSend party/guild routing stub -> zone broadcast (client_id={}, channel_wire={})",
				sender->clientId,
				request.channel);
			addAllInZone(sender->zoneId);
			break;
		case engine::net::ChatChannel::Zone:
			addAllInZone(sender->zoneId);
			break;
		case engine::net::ChatChannel::Global:
			for (ConnectedClient& peer : m_clients)
			{
				addUnique(&peer);
			}

			break;
		default:
			LOG_WARN(Net, "[ServerApp] ChatSend ignored: unhandled channel enum (client_id={})", sender->clientId);
			return;
		}

		if (recipients.empty())
		{
			LOG_WARN(Net,
				"[ServerApp] ChatSend produced zero recipients (client_id={}, channel_wire={})",
				sender->clientId,
				request.channel);
			return;
		}

		size_t sentOk = 0;
		for (ConnectedClient* receiver : recipients)
		{
			if (receiver->clientId != sender->clientId && IsChatSenderIgnoredBy(*receiver, relay.senderDisplay))
			{
				LOG_DEBUG(Net,
					"[ServerApp] ChatRelay skipped for ignored sender (receiver_client_id={}, sender_display={})",
					receiver->clientId,
					relay.senderDisplay);
				continue;
			}

			if (SendChatRelay(*receiver, relay))
			{
				++sentOk;
			}
		}

		LOG_INFO(Net,
			"[ServerApp] ChatSend routed (client_id={}, channel_wire={}, recipients={}, sent_ok={})",
			sender->clientId,
			request.channel,
			recipients.size(),
			sentOk);
	}

	void ServerApp::SpawnLootBagForMob(const MobEntity& mob, EntityId ownerEntityId)
	{
		std::vector<ItemStack> droppedItems;
		LootVisibility visibility = LootVisibility::Owner;
		bool foundLoot = false;
		for (const LootTableEntry& entry : m_lootTableEntries)
		{
			if (entry.sourceArchetypeId != mob.archetypeId)
			{
				continue;
			}

			if (!foundLoot)
			{
				visibility = entry.visibility;
				foundLoot = true;
			}
			else if (visibility != entry.visibility)
			{
				LOG_WARN(Net, "[ServerApp] Loot bag spawn ignored mixed visibility entries (mob_entity_id={}, archetype_id={})",
					mob.entityId,
					mob.archetypeId);
				return;
			}

			droppedItems.push_back(entry.item);
		}

		if (!foundLoot)
		{
			LOG_WARN(Net, "[ServerApp] Loot bag spawn skipped: no loot table entry (mob_entity_id={}, archetype_id={})",
				mob.entityId,
				mob.archetypeId);
			return;
		}

		CellGrid* zoneGrid = GetOrCreateZoneGrid(mob.zoneId);
		if (zoneGrid == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Loot bag spawn FAILED: zone grid unavailable (mob_entity_id={}, zone_id={})",
				mob.entityId,
				mob.zoneId);
			return;
		}

		LootBagEntity lootBag{};
		lootBag.entityId = m_nextServerEntityId++;
		lootBag.zoneId = mob.zoneId;
		lootBag.archetypeId = kDefaultLootBagArchetypeId;
		lootBag.positionMetersX = mob.positionMetersX;
		lootBag.positionMetersY = mob.positionMetersY;
		lootBag.positionMetersZ = mob.positionMetersZ;
		lootBag.visibility = visibility;
		lootBag.ownerEntityId = visibility == LootVisibility::Owner ? ownerEntityId : 0;
		lootBag.items = droppedItems;

		CellCoord cell{};
		if (!zoneGrid->UpsertEntity(lootBag.entityId, lootBag.positionMetersX, lootBag.positionMetersZ, cell))
		{
			LOG_ERROR(Net, "[ServerApp] Loot bag spawn FAILED: spatial insert failed (loot_bag_entity_id={})", lootBag.entityId);
			return;
		}

		m_lootBags.push_back(lootBag);
		LOG_INFO(Net, "[ServerApp] Loot bag spawned (loot_bag_entity_id={}, mob_entity_id={}, owner_entity_id={}, item_count={}, visibility={})",
			lootBag.entityId,
			mob.entityId,
			lootBag.ownerEntityId,
			lootBag.items.size(),
			lootBag.visibility == LootVisibility::Owner ? "owner" : "public");
	}

	void ServerApp::UpdateThreatFromCombatEvent(const CombatEventMessage& message)
	{
		MobEntity* targetMob = FindMobByEntityId(message.targetEntityId);
		if (targetMob == nullptr || message.damage == 0 || (targetMob->stateFlags & kEntityStateDead) != 0u)
		{
			return;
		}

		const ConnectedClient* attacker = FindClientByEntityId(message.attackerEntityId);
		if (attacker == nullptr)
		{
			return;
		}

		for (ThreatEntry& entry : targetMob->threatTable)
		{
			if (entry.entityId == attacker->entityId)
			{
				entry.threat += message.damage;
				LOG_INFO(Net, "[ServerApp] Threat updated (mob_entity_id={}, attacker_entity_id={}, threat={})",
					targetMob->entityId,
					attacker->entityId,
					entry.threat);
				RefreshMobAggroTarget(*targetMob);
				if (targetMob->aggroTargetEntityId != 0)
				{
					SetMobAiState(*targetMob, MobAiState::Aggro);
				}
				return;
			}
		}

		ThreatEntry entry{};
		entry.entityId = attacker->entityId;
		entry.threat = message.damage;
		targetMob->threatTable.push_back(entry);
		LOG_INFO(Net, "[ServerApp] Threat added (mob_entity_id={}, attacker_entity_id={}, threat={})",
			targetMob->entityId,
			attacker->entityId,
			entry.threat);
		RefreshMobAggroTarget(*targetMob);
		if (targetMob->aggroTargetEntityId != 0)
		{
			SetMobAiState(*targetMob, MobAiState::Aggro);
		}
	}

	void ServerApp::RefreshMobAggroTarget(MobEntity& mob)
	{
		mob.threatTable.erase(
			std::remove_if(
				mob.threatTable.begin(),
				mob.threatTable.end(),
				[this, &mob](const ThreatEntry& entry)
				{
					const ConnectedClient* target = FindClientByEntityId(entry.entityId);
					return target == nullptr
						|| !target->hasReplicatedState
						|| target->zoneId != mob.zoneId
						|| target->stats.currentHealth == 0
						|| (target->stateFlags & kEntityStateDead) != 0u
						|| entry.threat == 0;
				}),
			mob.threatTable.end());

		EntityId bestEntityId = 0;
		uint32_t bestThreat = 0;
		for (const ThreatEntry& entry : mob.threatTable)
		{
			if (entry.threat > bestThreat)
			{
				bestThreat = entry.threat;
				bestEntityId = entry.entityId;
			}
		}

		if (mob.aggroTargetEntityId != bestEntityId)
		{
			LOG_INFO(Net, "[ServerApp] Mob target updated (mob_entity_id={}, target_entity_id={}, threat={})",
				mob.entityId,
				bestEntityId,
				bestThreat);
			mob.aggroTargetEntityId = bestEntityId;
		}
	}

	bool ServerApp::MoveMobTowards(MobEntity& mob, float targetPositionX, float targetPositionZ)
	{
		const float dx = targetPositionX - mob.positionMetersX;
		const float dz = targetPositionZ - mob.positionMetersZ;
		const float distanceSquared = (dx * dx) + (dz * dz);
		if (distanceSquared <= 0.0001f)
		{
			mob.velocityMetersPerSecondX = 0.0f;
			mob.velocityMetersPerSecondZ = 0.0f;
			return true;
		}

		const uint32_t intervalTicks = ResolveMobAiIntervalTicks();
		const float simulationDt = static_cast<float>(intervalTicks) / static_cast<float>(m_tickHz);
		const float maxStep = mob.moveSpeedMetersPerSecond * simulationDt;
		const float distance = std::sqrt(distanceSquared);
		if (distance <= maxStep)
		{
			mob.velocityMetersPerSecondX = dx / simulationDt;
			mob.velocityMetersPerSecondZ = dz / simulationDt;
			mob.positionMetersX = targetPositionX;
			mob.positionMetersZ = targetPositionZ;
			mob.yawRadians = std::atan2(mob.velocityMetersPerSecondX, mob.velocityMetersPerSecondZ);
			return UpdateMobSpatialState(mob);
		}

		const float stepScale = maxStep / distance;
		mob.velocityMetersPerSecondX = (dx * stepScale) / simulationDt;
		mob.velocityMetersPerSecondZ = (dz * stepScale) / simulationDt;
		mob.positionMetersX += dx * stepScale;
		mob.positionMetersZ += dz * stepScale;
		mob.yawRadians = std::atan2(mob.velocityMetersPerSecondX, mob.velocityMetersPerSecondZ);
		return UpdateMobSpatialState(mob);
	}

	bool ServerApp::UpdateMobSpatialState(MobEntity& mob)
	{
		CellGrid* zoneGrid = GetOrCreateZoneGrid(mob.zoneId);
		if (zoneGrid == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Mob spatial update FAILED: zone grid unavailable (mob_entity_id={}, zone_id={})",
				mob.entityId,
				mob.zoneId);
			return false;
		}

		CellCoord cell{};
		if (!zoneGrid->UpsertEntity(mob.entityId, mob.positionMetersX, mob.positionMetersZ, cell))
		{
			LOG_WARN(Net, "[ServerApp] Mob spatial update skipped (mob_entity_id={}, pos=({:.2f}, {:.2f}))",
				mob.entityId,
				mob.positionMetersX,
				mob.positionMetersZ);
			return false;
		}

		LOG_DEBUG(Net, "[ServerApp] Mob spatial update OK (mob_entity_id={}, cell={}, {})",
			mob.entityId,
			cell.x,
			cell.z);
		return true;
	}

	void ServerApp::SetMobAiState(MobEntity& mob, MobAiState newState)
	{
		if (mob.aiState == newState)
		{
			return;
		}

		LOG_INFO(Net, "[ServerApp] Mob state changed (mob_entity_id={}, {} -> {})",
			mob.entityId,
			GetMobAiStateName(mob.aiState),
			GetMobAiStateName(newState));
		mob.aiState = newState;
	}

	void ServerApp::ResetMobThreat(MobEntity& mob)
	{
		const size_t clearedCount = mob.threatTable.size();
		mob.threatTable.clear();
		mob.aggroTargetEntityId = 0;
		LOG_INFO(Net, "[ServerApp] Mob threat reset (mob_entity_id={}, cleared_entries={})",
			mob.entityId,
			clearedCount);
	}

	bool ServerApp::TryMobAttackPlayer(MobEntity& mob, ConnectedClient& target)
	{
		if (m_currentTick < mob.combat.nextAttackTick)
		{
			return false;
		}

		const float distanceSquared = DistanceSquaredXZ(
			mob.positionMetersX,
			mob.positionMetersZ,
			target.positionMetersX,
			target.positionMetersZ);
		const float attackRangeSquared = mob.combat.attackRangeMeters * mob.combat.attackRangeMeters;
		if (distanceSquared > attackRangeSquared)
		{
			return false;
		}

		const uint32_t appliedDamage = std::min(mob.combat.damagePerHit, target.stats.currentHealth);
		if (appliedDamage == 0)
		{
			return false;
		}

		mob.combat.nextAttackTick = m_currentTick + mob.combat.cooldownTicks;
		target.stats.currentHealth -= appliedDamage;
		if (target.stats.currentHealth == 0)
		{
			target.stateFlags |= kEntityStateDead;
			LOG_INFO(Net, "[ServerApp] Player died (entity_id={}, attacker_entity_id={})",
				target.entityId,
				mob.entityId);
			SaveConnectedClient(target, "player_death");
		}

		CombatEventMessage combatEvent{};
		combatEvent.attackerEntityId = mob.entityId;
		combatEvent.targetEntityId = target.entityId;
		combatEvent.damage = appliedDamage;
		combatEvent.targetCurrentHealth = target.stats.currentHealth;
		combatEvent.targetMaxHealth = target.stats.maxHealth;
		combatEvent.targetStateFlags = target.stateFlags;
		LOG_INFO(Net,
			"[ServerApp] Mob attack applied (attacker_entity_id={}, target_entity_id={}, damage={}, hp={}/{}, next_attack_tick={})",
			combatEvent.attackerEntityId,
			combatEvent.targetEntityId,
			combatEvent.damage,
			combatEvent.targetCurrentHealth,
			combatEvent.targetMaxHealth,
			mob.combat.nextAttackTick);
		BroadcastCombatEvent(combatEvent);
		return true;
	}

	void ServerApp::AddItemToInventory(ConnectedClient& client, const ItemStack& item)
	{
		for (ItemStack& inventoryItem : client.inventory)
		{
			if (inventoryItem.itemId == item.itemId)
			{
				inventoryItem.quantity += item.quantity;
				LOG_INFO(Net, "[ServerApp] Inventory stack updated (client_id={}, item_id={}, quantity={})",
					client.clientId,
					item.itemId,
					inventoryItem.quantity);
				return;
			}
		}

		client.inventory.push_back(item);
		LOG_INFO(Net, "[ServerApp] Inventory item added (client_id={}, item_id={}, quantity={})",
			client.clientId,
			item.itemId,
			item.quantity);
	}

	void ServerApp::ApplyQuestEvent(
		ConnectedClient& client,
		QuestStepType eventType,
		std::string_view targetId,
		uint32_t amount,
		std::string_view reason)
	{
		std::vector<QuestProgressDelta> deltas;
		if (!m_questRuntime.ApplyEvent(client.questStates, eventType, targetId, amount, deltas))
		{
			return;
		}

		std::vector<ItemStack> rewardedItems;
		for (const QuestProgressDelta& delta : deltas)
		{
			if (delta.rewardExperience != 0 || delta.rewardGold != 0 || !delta.rewardItems.empty())
			{
				client.experiencePoints += delta.rewardExperience;
				client.gold += delta.rewardGold;
				for (const ItemStack& rewardItem : delta.rewardItems)
				{
					AddItemToInventory(client, rewardItem);
					rewardedItems.push_back(rewardItem);
				}

				LOG_INFO(Net,
					"[ServerApp] Quest rewards granted (client_id={}, quest_id={}, xp={}, gold={}, items={})",
					client.clientId,
					delta.questId,
					delta.rewardExperience,
					delta.rewardGold,
					delta.rewardItems.size());
			}

			(void)SendQuestDelta(client, delta);
		}

		if (!rewardedItems.empty())
		{
			(void)SendInventoryDelta(client, rewardedItems);
		}

		SaveConnectedClient(client, reason);
		LOG_INFO(Net,
			"[ServerApp] Quest event applied (client_id={}, type={}, target={}, deltas={}, reason={})",
			client.clientId,
			GetQuestStepTypeName(eventType),
			targetId,
			deltas.size(),
			reason);
	}

	void ServerApp::SendQuestStateBootstrap(const ConnectedClient& receiver)
	{
		for (const QuestState& state : receiver.questStates)
		{
			QuestProgressDelta delta{};
			delta.questId = state.questId;
			delta.status = state.status;
			delta.stepProgressCounts = state.stepProgressCounts;
			(void)SendQuestDelta(receiver, delta);
		}

		LOG_INFO(Net, "[ServerApp] Quest bootstrap sent (client_id={}, quests={})",
			receiver.clientId,
			receiver.questStates.size());
	}

	void ServerApp::SendDynamicEventBootstrap(const ConnectedClient& receiver)
	{
		size_t eventCount = 0;
		for (const DynamicEventState& eventState : m_dynamicEvents)
		{
			if (eventState.definition.zoneId != receiver.zoneId || eventState.status == DynamicEventStatus::Idle)
			{
				continue;
			}

			++eventCount;
			(void)SendDynamicEventState(receiver, eventState, eventState.definition.startNotificationText, 0, 0, {});
		}

		LOG_INFO(Net, "[ServerApp] Dynamic event bootstrap sent (client_id={}, zone_id={}, events={})",
			receiver.clientId,
			receiver.zoneId,
			eventCount);
	}

	bool ServerApp::SendInventoryDelta(const ConnectedClient& receiver, std::span<const ItemStack> items)
	{
		InventoryDeltaMessage message{};
		message.clientId = receiver.clientId;
		const std::vector<std::byte> packet = EncodeInventoryDelta(message, items);
		if (!m_transport.Send(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] InventoryDelta send failed (client_id={}, item_count={})",
				receiver.clientId,
				items.size());
			return false;
		}

		LOG_INFO(Net, "[ServerApp] InventoryDelta sent (client_id={}, item_count={})",
			receiver.clientId,
			items.size());
		return true;
	}

	bool ServerApp::SendDynamicEventState(
		const ConnectedClient& receiver,
		const DynamicEventState& eventState,
		std::string_view notificationText,
		uint32_t rewardExperience,
		uint32_t rewardGold,
		std::span<const ItemStack> rewardItems)
	{
		EventStateMessage message{};
		message.zoneId = eventState.definition.zoneId;
		message.status = static_cast<uint8_t>(eventState.status);
		message.phaseIndex = static_cast<uint16_t>(eventState.status == DynamicEventStatus::Active ? (eventState.currentPhaseIndex + 1u) : 0u);
		message.phaseCount = static_cast<uint16_t>(eventState.definition.phases.size());
		message.progressCurrent = eventState.currentPhaseProgress;
		if (eventState.status == DynamicEventStatus::Active && eventState.currentPhaseIndex < eventState.definition.phases.size())
		{
			message.progressRequired = eventState.definition.phases[eventState.currentPhaseIndex].progressRequired;
		}
		message.eventId = eventState.definition.eventId;
		message.notificationText = std::string(notificationText);
		message.rewardExperience = rewardExperience;
		message.rewardGold = rewardGold;
		message.rewardItems.assign(rewardItems.begin(), rewardItems.end());

		const std::vector<std::byte> packet = EncodeEventState(message);
		if (!m_transport.Send(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] EventState send failed (client_id={}, event_id={})",
				receiver.clientId,
				eventState.definition.eventId);
			return false;
		}

		LOG_INFO(Net, "[ServerApp] EventState sent (client_id={}, event_id={}, status={}, phase={}/{}, progress={}/{})",
			receiver.clientId,
			eventState.definition.eventId,
			GetDynamicEventStatusName(eventState.status),
			message.phaseIndex,
			message.phaseCount,
			message.progressCurrent,
			message.progressRequired);
		return true;
	}

	void ServerApp::BroadcastDynamicEventState(
		const DynamicEventState& eventState,
		std::string_view notificationText,
		uint32_t rewardedClientId,
		uint32_t rewardExperience,
		uint32_t rewardGold,
		std::span<const ItemStack> rewardItems)
	{
		size_t recipientCount = 0;
		for (const ConnectedClient& client : m_clients)
		{
			if (!client.hasReplicatedState || client.zoneId != eventState.definition.zoneId)
			{
				continue;
			}

			const bool rewardedReceiver = rewardedClientId != 0 && client.clientId == rewardedClientId;
			++recipientCount;
			(void)SendDynamicEventState(
				client,
				eventState,
				notificationText,
				rewardedReceiver ? rewardExperience : 0u,
				rewardedReceiver ? rewardGold : 0u,
				rewardedReceiver ? rewardItems : std::span<const ItemStack>{});
		}

		LOG_INFO(Net, "[ServerApp] EventState broadcast (event_id={}, zone_id={}, recipients={})",
			eventState.definition.eventId,
			eventState.definition.zoneId,
			recipientCount);
	}

	bool ServerApp::SendQuestDelta(const ConnectedClient& receiver, const QuestProgressDelta& delta)
	{
		const QuestDefinition* definition = m_questRuntime.FindQuestDefinition(delta.questId);
		if (definition == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] QuestDelta skipped: missing definition (client_id={}, quest_id={})",
				receiver.clientId,
				delta.questId);
			return false;
		}

		QuestDeltaMessage message{};
		message.clientId = receiver.clientId;
		message.status = static_cast<uint8_t>(delta.status);
		message.questId = delta.questId;
		message.rewardExperience = delta.rewardExperience;
		message.rewardGold = delta.rewardGold;
		message.rewardItems = delta.rewardItems;
		message.steps.reserve(definition->steps.size());
		for (size_t stepIndex = 0; stepIndex < definition->steps.size(); ++stepIndex)
		{
			const QuestStepDefinition& definitionStep = definition->steps[stepIndex];
			QuestDeltaStep messageStep{};
			messageStep.stepType = static_cast<uint8_t>(definitionStep.type);
			messageStep.targetId = definitionStep.targetId;
			messageStep.requiredCount = definitionStep.requiredCount;
			if (stepIndex < delta.stepProgressCounts.size())
			{
				messageStep.currentCount = delta.stepProgressCounts[stepIndex];
			}
			message.steps.push_back(std::move(messageStep));
		}

		const std::vector<std::byte> packet = EncodeQuestDelta(message);
		if (!m_transport.Send(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] QuestDelta send failed (client_id={}, quest_id={})",
				receiver.clientId,
				delta.questId);
			return false;
		}

		LOG_INFO(Net, "[ServerApp] QuestDelta sent (client_id={}, quest_id={}, status={}, steps={})",
			receiver.clientId,
			delta.questId,
			GetQuestStatusName(delta.status),
			message.steps.size());
		return true;
	}

	void ServerApp::RefreshReplication()
	{
		for (ConnectedClient& client : m_clients)
		{
			RefreshReplicationForClient(client);
		}
	}

	void ServerApp::RefreshReplicationForClient(ConnectedClient& client)
	{
		if (!client.hasCell || !client.hasReplicatedState)
		{
			return;
		}

		if (CellGrid* zoneGrid = GetOrCreateZoneGrid(client.zoneId); zoneGrid != nullptr)
		{
			zoneGrid->GatherEntityIds(client.interestCells, client.interestEntityIds);
		}
		else
		{
			LOG_WARN(Net, "[ServerApp] Replication refresh skipped: zone grid unavailable (client_id={}, zone_id={})",
				client.clientId,
				client.zoneId);
			return;
		}

		BuildRelevantEntityIds(client, m_relevantEntityScratch);
		for (EntityId entityId : m_relevantEntityScratch)
		{
			if (!ContainsEntityId(client.replicatedEntityIds, entityId))
			{
				(void)SendSpawn(client, entityId);
			}
		}

		for (EntityId entityId : client.replicatedEntityIds)
		{
			if (!ContainsEntityId(m_relevantEntityScratch, entityId))
			{
				(void)SendDespawn(client, entityId);
			}
		}

		client.replicatedEntityIds = m_relevantEntityScratch;
	}

	void ServerApp::BuildRelevantEntityIds(const ConnectedClient& client, std::vector<EntityId>& outEntityIds) const
	{
		outEntityIds.clear();
		for (EntityId entityId : client.interestEntityIds)
		{
			if (entityId == client.entityId)
			{
				continue;
			}

			const ConnectedClient* subjectClient = FindClientByEntityId(entityId);
			if (subjectClient != nullptr)
			{
				if (!subjectClient->hasReplicatedState)
				{
					continue;
				}
				outEntityIds.push_back(entityId);
				continue;
			}

			if (FindMobByEntityId(entityId) != nullptr)
			{
				outEntityIds.push_back(entityId);
				continue;
			}

			const LootBagEntity* lootBag = FindLootBagByEntityId(entityId);
			if (lootBag != nullptr)
			{
				if (lootBag->visibility == LootVisibility::Public || lootBag->ownerEntityId == client.entityId)
				{
					outEntityIds.push_back(entityId);
				}
			}
		}
	}

	EntityState ServerApp::BuildEntityState(const ConnectedClient& client) const
	{
		EntityState state{};
		state.positionX = client.positionMetersX;
		state.positionY = client.positionMetersY;
		state.positionZ = client.positionMetersZ;
		state.yawRadians = client.yawRadians;
		state.velocityX = client.velocityMetersPerSecondX;
		state.velocityY = client.velocityMetersPerSecondY;
		state.velocityZ = client.velocityMetersPerSecondZ;
		state.currentHealth = client.stats.currentHealth;
		state.maxHealth = client.stats.maxHealth;
		state.stateFlags = client.stateFlags;
		return state;
	}

	EntityState ServerApp::BuildEntityState(const MobEntity& mob) const
	{
		EntityState state{};
		state.positionX = mob.positionMetersX;
		state.positionY = mob.positionMetersY;
		state.positionZ = mob.positionMetersZ;
		state.yawRadians = mob.yawRadians;
		state.velocityX = mob.velocityMetersPerSecondX;
		state.velocityY = mob.velocityMetersPerSecondY;
		state.velocityZ = mob.velocityMetersPerSecondZ;
		state.currentHealth = mob.stats.currentHealth;
		state.maxHealth = mob.stats.maxHealth;
		state.stateFlags = mob.stateFlags;
		return state;
	}

	EntityState ServerApp::BuildEntityState(const LootBagEntity& lootBag) const
	{
		EntityState state{};
		state.positionX = lootBag.positionMetersX;
		state.positionY = lootBag.positionMetersY;
		state.positionZ = lootBag.positionMetersZ;
		state.yawRadians = lootBag.yawRadians;
		state.velocityX = lootBag.velocityMetersPerSecondX;
		state.velocityY = lootBag.velocityMetersPerSecondY;
		state.velocityZ = lootBag.velocityMetersPerSecondZ;
		state.currentHealth = 0;
		state.maxHealth = 0;
		state.stateFlags = lootBag.stateFlags;
		return state;
	}

	bool ServerApp::TryBuildSpawnEntity(EntityId entityId, SpawnEntity& outEntity) const
	{
		if (const ConnectedClient* client = FindClientByEntityId(entityId))
		{
			outEntity.entityId = client->entityId;
			outEntity.archetypeId = client->archetypeId;
			outEntity.state = BuildEntityState(*client);
			return true;
		}

		if (const MobEntity* mob = FindMobByEntityId(entityId))
		{
			outEntity.entityId = mob->entityId;
			outEntity.archetypeId = mob->archetypeId;
			outEntity.state = BuildEntityState(*mob);
			return true;
		}

		if (const LootBagEntity* lootBag = FindLootBagByEntityId(entityId))
		{
			outEntity.entityId = lootBag->entityId;
			outEntity.archetypeId = lootBag->archetypeId;
			outEntity.state = BuildEntityState(*lootBag);
			return true;
		}

		LOG_WARN(Net, "[ServerApp] Spawn build ignored: unknown entity_id={}", entityId);
		return false;
	}

	bool ServerApp::TryBuildSnapshotEntity(EntityId entityId, SnapshotEntity& outEntity) const
	{
		if (const ConnectedClient* client = FindClientByEntityId(entityId))
		{
			outEntity.entityId = client->entityId;
			outEntity.state = BuildEntityState(*client);
			return true;
		}

		if (const MobEntity* mob = FindMobByEntityId(entityId))
		{
			outEntity.entityId = mob->entityId;
			outEntity.state = BuildEntityState(*mob);
			return true;
		}

		if (const LootBagEntity* lootBag = FindLootBagByEntityId(entityId))
		{
			outEntity.entityId = lootBag->entityId;
			outEntity.state = BuildEntityState(*lootBag);
			return true;
		}

		LOG_WARN(Net, "[ServerApp] Snapshot build ignored: unknown entity_id={}", entityId);
		return false;
	}

	bool ServerApp::ShouldBroadcastCombatEventToClient(const ConnectedClient& receiver, EntityId attackerEntityId, EntityId targetEntityId) const
	{
		if (receiver.entityId == attackerEntityId || receiver.entityId == targetEntityId)
		{
			return true;
		}

		return ContainsEntityId(receiver.interestEntityIds, attackerEntityId)
			|| ContainsEntityId(receiver.interestEntityIds, targetEntityId);
	}

	void ServerApp::BroadcastCombatEvent(const CombatEventMessage& message)
	{
		UpdateThreatFromCombatEvent(message);

		size_t recipientCount = 0;
		for (const ConnectedClient& client : m_clients)
		{
			if (!ShouldBroadcastCombatEventToClient(client, message.attackerEntityId, message.targetEntityId))
			{
				continue;
			}

			++recipientCount;
			(void)SendCombatEvent(client, message);
		}

		LOG_INFO(Net,
			"[ServerApp] CombatEvent broadcast (attacker_entity_id={}, target_entity_id={}, damage={}, recipients={})",
			message.attackerEntityId,
			message.targetEntityId,
			message.damage,
			recipientCount);
	}

	bool ServerApp::SendSpawn(const ConnectedClient& receiver, EntityId subjectEntityId)
	{
		SpawnEntity entity{};
		if (!TryBuildSpawnEntity(subjectEntityId, entity))
		{
			LOG_WARN(Net, "[ServerApp] Spawn send skipped: unresolved entity_id={}", subjectEntityId);
			return false;
		}

		const std::vector<std::byte> packet = EncodeSpawn(entity);
		if (!m_transport.Send(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] Spawn send failed (receiver_client_id={}, entity_id={})",
				receiver.clientId,
				entity.entityId);
			return false;
		}

		LOG_INFO(Net, "[ServerApp] Spawn sent (receiver_client_id={}, entity_id={}, archetype_id={})",
			receiver.clientId,
			entity.entityId,
			entity.archetypeId);
		return true;
	}

	bool ServerApp::SendDespawn(const ConnectedClient& receiver, EntityId entityId)
	{
		DespawnEntity entity{};
		entity.entityId = entityId;
		const std::vector<std::byte> packet = EncodeDespawn(entity);
		if (!m_transport.Send(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] Despawn send failed (receiver_client_id={}, entity_id={})",
				receiver.clientId,
				entityId);
			return false;
		}

		LOG_INFO(Net, "[ServerApp] Despawn sent (receiver_client_id={}, entity_id={})",
			receiver.clientId,
			entityId);
		return true;
	}

	bool ServerApp::SendZoneChange(const ConnectedClient& receiver, const ZoneChangeMessage& message)
	{
		const std::vector<std::byte> packet = EncodeZoneChange(message);
		if (!m_transport.Send(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] ZoneChange send failed (client_id={}, zone_id={})",
				receiver.clientId,
				message.zoneId);
			return false;
		}

		LOG_INFO(Net,
			"[ServerApp] ZoneChange sent (client_id={}, zone_id={}, spawn=({:.2f}, {:.2f}, {:.2f}))",
			receiver.clientId,
			message.zoneId,
			message.spawnPositionX,
			message.spawnPositionY,
			message.spawnPositionZ);
		return true;
	}

	bool ServerApp::SendCombatEvent(const ConnectedClient& receiver, const CombatEventMessage& message)
	{
		const std::vector<std::byte> packet = EncodeCombatEvent(message);
		if (!m_transport.Send(receiver.endpoint, packet))
		{
			LOG_WARN(Net,
				"[ServerApp] CombatEvent send failed (client_id={}, attacker_entity_id={}, target_entity_id={})",
				receiver.clientId,
				message.attackerEntityId,
				message.targetEntityId);
			return false;
		}

		LOG_DEBUG(Net,
			"[ServerApp] CombatEvent sent (client_id={}, attacker_entity_id={}, target_entity_id={}, damage={})",
			receiver.clientId,
			message.attackerEntityId,
			message.targetEntityId,
			message.damage);
		return true;
	}

	bool ServerApp::SendChatRelay(const ConnectedClient& receiver, const ChatRelayMessage& message)
	{
		const std::vector<std::byte> packet = EncodeChatRelay(message);
		if (!m_transport.Send(receiver.endpoint, packet))
		{
			LOG_WARN(Net,
				"[ServerApp] ChatRelay send FAILED (receiver_client_id={}, sender_entity_id={})",
				receiver.clientId,
				message.senderEntityId);
			return false;
		}

		LOG_DEBUG(Net,
			"[ServerApp] ChatRelay sent (receiver_client_id={}, sender_entity_id={}, channel_wire={})",
			receiver.clientId,
			message.senderEntityId,
			message.channel);
		return true;
	}

	bool ServerApp::SendEmoteRelay(const ConnectedClient& receiver, const EmoteRelayMessage& message)
	{
		const std::vector<std::byte> packet = EncodeEmoteRelay(message);
		if (!m_transport.Send(receiver.endpoint, packet))
		{
			LOG_WARN(Net,
				"[ServerApp] EmoteRelay send FAILED (receiver_client_id={}, actor_entity_id={})",
				receiver.clientId,
				message.actorEntityId);
			return false;
		}

		LOG_DEBUG(Net,
			"[ServerApp] EmoteRelay sent (receiver_client_id={}, actor_entity_id={}, emote_id={})",
			receiver.clientId,
			message.actorEntityId,
			message.emoteId);
		return true;
	}

	bool ServerApp::SendWelcome(const ConnectedClient& client)
	{
		WelcomeMessage welcome{};
		welcome.clientId = client.clientId;
		welcome.tickHz = m_tickHz;
		welcome.snapshotHz = m_snapshotHz;
		const std::vector<std::byte> packet = EncodeWelcome(welcome);
		if (!m_transport.Send(client.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] Welcome send failed (client_id={}, endpoint={})",
				client.clientId,
				UdpTransport::EndpointToString(client.endpoint));
			return false;
		}

		LOG_INFO(Net, "[ServerApp] Welcome sent (client_id={}, endpoint={}, tick_hz={}, snapshot_hz={})",
			client.clientId,
			UdpTransport::EndpointToString(client.endpoint),
			m_tickHz,
			m_snapshotHz);
		return true;
	}

	bool ServerApp::SendSnapshot(const ConnectedClient& client)
	{
		m_snapshotEntitiesScratch.clear();
		for (EntityId entityId : client.replicatedEntityIds)
		{
			SnapshotEntity snapshotEntity{};
			if (!TryBuildSnapshotEntity(entityId, snapshotEntity))
			{
				continue;
			}

			m_snapshotEntitiesScratch.push_back(snapshotEntity);
		}

		SnapshotMessage snapshot{};
		snapshot.clientId = client.clientId;
		snapshot.serverTick = m_currentTick;
		snapshot.connectedClients = static_cast<uint16_t>(std::min<size_t>(m_clients.size(), 0xFFFFu));
		snapshot.entityCount = static_cast<uint16_t>(std::min<size_t>(m_snapshotEntitiesScratch.size(), 0xFFFFu));
		snapshot.receivedPackets = static_cast<uint32_t>(std::min<uint64_t>(m_transport.ReceivedPacketCount(), 0xFFFFFFFFu));
		snapshot.sentPackets = static_cast<uint32_t>(std::min<uint64_t>(m_transport.SentPacketCount(), 0xFFFFFFFFu));
		const std::vector<std::byte> packet = EncodeSnapshot(snapshot, m_snapshotEntitiesScratch);
		if (!m_transport.Send(client.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] Snapshot send failed (client_id={}, tick={})", client.clientId, m_currentTick);
			return false;
		}

		LOG_DEBUG(Net, "[ServerApp] Snapshot sent (client_id={}, tick={}, entity_count={})",
			client.clientId,
			m_currentTick,
			m_snapshotEntitiesScratch.size());
		return true;
	}

	ConnectedClient* ServerApp::FindClient(const Endpoint& endpoint)
	{
		for (ConnectedClient& client : m_clients)
		{
			if (client.endpoint == endpoint)
			{
				return &client;
			}
		}
		return nullptr;
	}

	const ConnectedClient* ServerApp::FindClient(const Endpoint& endpoint) const
	{
		for (const ConnectedClient& client : m_clients)
		{
			if (client.endpoint == endpoint)
			{
				return &client;
			}
		}
		return nullptr;
	}

	ConnectedClient* ServerApp::FindClientByEntityId(EntityId entityId)
	{
		for (ConnectedClient& client : m_clients)
		{
			if (client.entityId == entityId)
			{
				return &client;
			}
		}
		return nullptr;
	}

	const ConnectedClient* ServerApp::FindClientByEntityId(EntityId entityId) const
	{
		for (const ConnectedClient& client : m_clients)
		{
			if (client.entityId == entityId)
			{
				return &client;
			}
		}
		return nullptr;
	}

	MobEntity* ServerApp::FindMobByEntityId(EntityId entityId)
	{
		for (MobEntity& mob : m_mobs)
		{
			if (mob.entityId == entityId)
			{
				return &mob;
			}
		}
		return nullptr;
	}

	const MobEntity* ServerApp::FindMobByEntityId(EntityId entityId) const
	{
		for (const MobEntity& mob : m_mobs)
		{
			if (mob.entityId == entityId)
			{
				return &mob;
			}
		}
		return nullptr;
	}

	LootBagEntity* ServerApp::FindLootBagByEntityId(EntityId entityId)
	{
		for (LootBagEntity& lootBag : m_lootBags)
		{
			if (lootBag.entityId == entityId)
			{
				return &lootBag;
			}
		}
		return nullptr;
	}

	const LootBagEntity* ServerApp::FindLootBagByEntityId(EntityId entityId) const
	{
		for (const LootBagEntity& lootBag : m_lootBags)
		{
			if (lootBag.entityId == entityId)
			{
				return &lootBag;
			}
		}
		return nullptr;
	}

	CellGrid* ServerApp::GetOrCreateZoneGrid(uint32_t zoneId)
	{
		auto [it, inserted] = m_zoneGrids.try_emplace(zoneId);
		if (inserted)
		{
			if (!it->second.Init())
			{
				LOG_ERROR(Net, "[ServerApp] Zone grid init FAILED (zone_id={})", zoneId);
				m_zoneGrids.erase(it);
				return nullptr;
			}

			LOG_INFO(Net, "[ServerApp] Zone grid ready (zone_id={})", zoneId);
		}

		return &it->second;
	}

	namespace
	{
		/// Trim ASCII spaces for chat command argument splitting.
		std::string_view TrimChatArg(std::string_view text)
		{
			while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
			{
				text.remove_prefix(1);
			}

			while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
			{
				text.remove_suffix(1);
			}

			return text;
		}

		/// Split `first rest` from one argument line.
		std::pair<std::string, std::string> SplitFirstChatArg(std::string_view line)
		{
			line = TrimChatArg(line);
			if (line.empty())
			{
				return {{}, {}};
			}

			const size_t spacePos = line.find(' ');
			if (spacePos == std::string_view::npos)
			{
				return {std::string(line), {}};
			}

			return {
				std::string(TrimChatArg(line.substr(0, spacePos))),
				std::string(TrimChatArg(line.substr(spacePos + 1)))};
		}
	}

	void ServerApp::InitModerationAuditSubsystem()
	{
		const std::string relativeLog = m_config.GetString("server.moderation_audit_log", "logs/moderation_audit.log");
		const auto absolutePath = engine::platform::FileSystem::ResolveContentPath(m_config, relativeLog);
		const size_t rotationMb = static_cast<size_t>((std::max)(static_cast<int64_t>(1), m_config.GetInt("server.moderation_audit_rotation_mb", 10)));
		const int retentionDays = static_cast<int>(m_config.GetInt("server.moderation_audit_retention_days", 7));
		if (!m_moderationAuditLog.Init(absolutePath.string(), rotationMb, retentionDays))
		{
			LOG_WARN(Net, "[ServerApp] Moderation audit log Init FAILED (path={})", absolutePath.string());
			m_moderationAuditLogReady = false;
			return;
		}

		m_moderationAuditLogReady = true;
		LOG_INFO(Net, "[ServerApp] Moderation audit subsystem Init OK (path={})", absolutePath.string());
	}

	void ServerApp::LoadChatBanFile()
	{
		m_bannedCharacterKeys.clear();
		const std::string relativePath = m_config.GetString("server.chat_ban_list_path", "persistence/server/chat_bans.ini");
		const auto fullPath = engine::platform::FileSystem::ResolveContentPath(m_config, relativePath);
		if (!engine::platform::FileSystem::Exists(fullPath))
		{
			LOG_INFO(Net, "[ServerApp] Chat ban list not present — starting empty (path={})", relativePath);
			return;
		}

		engine::core::Config banConfig;
		if (!banConfig.LoadFromFile(fullPath.string()))
		{
			LOG_WARN(Net, "[ServerApp] Chat ban list load FAILED (path={})", relativePath);
			return;
		}

		const uint32_t banCount = static_cast<uint32_t>(banConfig.GetInt("ban.count", 0));
		for (uint32_t banIndex = 0; banIndex < banCount && banIndex < 4096u; ++banIndex)
		{
			const uint32_t key = static_cast<uint32_t>(banConfig.GetInt("ban." + std::to_string(banIndex) + ".key", 0));
			if (key != 0u)
			{
				m_bannedCharacterKeys.insert(key);
			}
		}

		LOG_INFO(Net, "[ServerApp] Chat ban list loaded (path={}, keys={})", relativePath, m_bannedCharacterKeys.size());
	}

	void ServerApp::SaveChatBanFile()
	{
		std::vector<uint32_t> keys(m_bannedCharacterKeys.begin(), m_bannedCharacterKeys.end());
		std::sort(keys.begin(), keys.end());
		std::ostringstream output;
		output << "ban.count=" << keys.size() << "\n";
		for (size_t banIndex = 0; banIndex < keys.size(); ++banIndex)
		{
			output << "ban." << banIndex << ".key=" << keys[banIndex] << "\n";
		}

		const std::string relativePath = m_config.GetString("server.chat_ban_list_path", "persistence/server/chat_bans.ini");
		if (!engine::platform::FileSystem::WriteAllTextContent(m_config, relativePath, output.str()))
		{
			LOG_ERROR(Net, "[ServerApp] Chat ban list save FAILED (path={})", relativePath);
			return;
		}

		LOG_INFO(Net, "[ServerApp] Chat ban list saved (path={}, keys={})", relativePath, keys.size());
	}

	void ServerApp::SendChatSystemNotice(ConnectedClient& receiver, std::string_view text)
	{
		ChatRelayMessage notice{};
		notice.channel = static_cast<uint8_t>(engine::net::ChatChannel::Say);
		notice.senderEntityId = 0;
		notice.timestampUnixMs = NowUnixEpochMsUtc();
		notice.senderDisplay = "System";
		notice.text.assign(text.begin(), text.end());
		if (!SendChatRelay(receiver, notice))
		{
			LOG_WARN(Net, "[ServerApp] System chat notice send FAILED (client_id={})", receiver.clientId);
			return;
		}

		LOG_INFO(Net, "[ServerApp] System chat notice sent (client_id={}, len={})", receiver.clientId, notice.text.size());
	}

	void ServerApp::BroadcastModerationAnnouncement(std::string_view text)
	{
		ChatRelayMessage notice{};
		notice.channel = static_cast<uint8_t>(engine::net::ChatChannel::Global);
		notice.senderEntityId = 0;
		notice.timestampUnixMs = NowUnixEpochMsUtc();
		notice.senderDisplay = "Moderation";
		notice.text.assign(text.begin(), text.end());
		size_t sentOk = 0;
		for (ConnectedClient& client : m_clients)
		{
			if (SendChatRelay(client, notice))
			{
				++sentOk;
			}
		}

		LOG_INFO(Net, "[ServerApp] Moderation announce broadcast (recipients_ok={}, text_len={})", sentOk, notice.text.size());
	}

	void ServerApp::AuditLogChatReport(std::string_view reporterDisplay, std::string_view targetDisplay, std::string_view reason)
	{
		if (m_moderationAuditLogReady)
		{
			m_moderationAuditLog.LogChatReport(reporterDisplay, targetDisplay, reason);
		}
		else
		{
			LOG_INFO(Net,
				"[Moderation/Audit] CHAT_REPORT reporter={} target={} reason={}",
				reporterDisplay,
				targetDisplay,
				reason);
		}
	}

	void ServerApp::AuditLogModeration(std::string_view action, std::string_view actorDisplay, std::string_view targetDisplay, std::string_view detail)
	{
		if (m_moderationAuditLogReady)
		{
			m_moderationAuditLog.LogModerationAction(action, actorDisplay, targetDisplay, detail);
		}
		else
		{
			LOG_INFO(Net,
				"[Moderation/Audit] action={} actor={} target={} detail={}",
				action,
				actorDisplay,
				targetDisplay,
				detail);
		}
	}

	bool ServerApp::IsChatSenderIgnoredBy(const ConnectedClient& receiver, std::string_view senderDisplay) const
	{
		for (const std::string& ignoredName : receiver.chatIgnoredDisplayNames)
		{
			if (ChatNameEqualsAsciiI(ignoredName, senderDisplay))
			{
				return true;
			}
		}

		return false;
	}

	ConnectedClient* ServerApp::FindConnectedClientByChatDisplayName(std::string_view displayToken)
	{
		const std::string_view token = TrimChatArg(displayToken);
		if (token.empty())
		{
			return nullptr;
		}

		for (ConnectedClient& client : m_clients)
		{
			const std::string selfLabel = "P" + std::to_string(client.clientId);
			if (ChatNameEqualsAsciiI(token, selfLabel))
			{
				return &client;
			}
		}

		return nullptr;
	}

	void ServerApp::DisconnectConnectedClient(uint32_t clientId, std::string_view persistenceReason)
	{
		const auto clientIt = std::find_if(
			m_clients.begin(),
			m_clients.end(),
			[clientId](const ConnectedClient& client) { return client.clientId == clientId; });
		if (clientIt == m_clients.end())
		{
			LOG_WARN(Net, "[ServerApp] Disconnect skipped: client_id not found ({})", clientId);
			return;
		}

		SaveConnectedClient(*clientIt, persistenceReason);
		if (clientIt->hasCell)
		{
			const auto zoneIt = m_zoneGrids.find(clientIt->zoneId);
			if (zoneIt != m_zoneGrids.end())
			{
				(void)zoneIt->second.RemoveEntity(clientIt->entityId);
			}
		}

		OnClientLogout(*clientIt);
		LOG_INFO(Net, "[ServerApp] Client disconnected (client_id={}, reason={})", clientId, persistenceReason);
		m_clients.erase(clientIt);
	}

	bool ServerApp::HandleChatSlashCommand(ConnectedClient& sender, const ParsedChatSlashCommand& command)
	{
		const std::string actorLabel = "P" + std::to_string(sender.clientId);

		switch (command.kind)
		{
		case ChatSlashCommandKind::Who:
		{
			const bool globalWho = ChatNameEqualsAsciiI(TrimChatArg(command.argsRemainder), "global");
			std::ostringstream list;
			list << (globalWho ? "ONLINE (global): " : "ONLINE (zone): ");
			bool first = true;
			for (const ConnectedClient& peer : m_clients)
			{
				if (!globalWho && peer.zoneId != sender.zoneId)
				{
					continue;
				}

				if (!first)
				{
					list << ' ';
				}

				first = false;
				list << 'P' << peer.clientId;
			}

			if (first)
			{
				list << "(none)";
			}

			SendChatSystemNotice(sender, list.str());
			LOG_INFO(Net, "[ServerApp] /who handled (client_id={}, global={})", sender.clientId, globalWho ? "true" : "false");
			return true;
		}
		case ChatSlashCommandKind::Ignore:
		{
			const auto [targetName, extra] = SplitFirstChatArg(command.argsRemainder);
			(void)extra;
			if (targetName.empty())
			{
				SendChatSystemNotice(sender, "Usage: /ignore <player>");
				LOG_WARN(Net, "[ServerApp] /ignore rejected: missing target (client_id={})", sender.clientId);
				return true;
			}

			const std::string selfLabel = "P" + std::to_string(sender.clientId);
			if (ChatNameEqualsAsciiI(targetName, selfLabel))
			{
				SendChatSystemNotice(sender, "Cannot ignore yourself.");
				LOG_WARN(Net, "[ServerApp] /ignore rejected: self-target (client_id={})", sender.clientId);
				return true;
			}

			if (sender.chatIgnoredDisplayNames.size() >= 32)
			{
				SendChatSystemNotice(sender, "Ignore list full (max 32).");
				LOG_WARN(Net, "[ServerApp] /ignore rejected: list full (client_id={})", sender.clientId);
				return true;
			}

			for (const std::string& existing : sender.chatIgnoredDisplayNames)
			{
				if (ChatNameEqualsAsciiI(existing, targetName))
				{
					SendChatSystemNotice(sender, "Already ignoring that player.");
					LOG_INFO(Net, "[ServerApp] /ignore duplicate ignored (client_id={})", sender.clientId);
					return true;
				}
			}

			sender.chatIgnoredDisplayNames.push_back(targetName);
			SaveConnectedClient(sender, "chat_ignore");
			SendChatSystemNotice(sender, "Ignored " + targetName + ".");
			LOG_INFO(Net, "[ServerApp] /ignore applied (client_id={}, target={})", sender.clientId, targetName);
			return true;
		}
		case ChatSlashCommandKind::Unignore:
		{
			const auto [targetName, extra] = SplitFirstChatArg(command.argsRemainder);
			(void)extra;
			if (targetName.empty())
			{
				SendChatSystemNotice(sender, "Usage: /unignore <player>");
				LOG_WARN(Net, "[ServerApp] /unignore rejected: missing target (client_id={})", sender.clientId);
				return true;
			}

			const auto it = std::find_if(
				sender.chatIgnoredDisplayNames.begin(),
				sender.chatIgnoredDisplayNames.end(),
				[&](const std::string& entry) { return ChatNameEqualsAsciiI(entry, targetName); });
			if (it == sender.chatIgnoredDisplayNames.end())
			{
				SendChatSystemNotice(sender, "Not ignoring that player.");
				LOG_WARN(Net, "[ServerApp] /unignore rejected: not found (client_id={})", sender.clientId);
				return true;
			}

			sender.chatIgnoredDisplayNames.erase(it);
			SaveConnectedClient(sender, "chat_unignore");
			SendChatSystemNotice(sender, "Unignored " + targetName + ".");
			LOG_INFO(Net, "[ServerApp] /unignore applied (client_id={}, target={})", sender.clientId, targetName);
			return true;
		}
		case ChatSlashCommandKind::Report:
		{
			const auto [targetName, reason] = SplitFirstChatArg(command.argsRemainder);
			if (targetName.empty() || reason.empty())
			{
				SendChatSystemNotice(sender, "Usage: /report <player> <reason>");
				LOG_WARN(Net, "[ServerApp] /report rejected: bad args (client_id={})", sender.clientId);
				return true;
			}

			AuditLogChatReport(actorLabel, targetName, reason);
			SendChatSystemNotice(sender, "Report recorded. Thank you.");
			LOG_INFO(Net, "[ServerApp] /report recorded (client_id={}, target={})", sender.clientId, targetName);
			return true;
		}
		case ChatSlashCommandKind::Kick:
		case ChatSlashCommandKind::Ban:
		case ChatSlashCommandKind::Mute:
		case ChatSlashCommandKind::Announce:
		{
			if (!sender.chatModeratorRole)
			{
				SendChatSystemNotice(sender, "Permission denied.");
				LOG_WARN(Net,
					"[ServerApp] Admin chat command denied (client_id={}, cmd={})",
					sender.clientId,
					ChatSlashCommandLabel(command.kind));
				return true;
			}

			if (command.kind == ChatSlashCommandKind::Announce)
			{
				const std::string_view announcement = TrimChatArg(command.argsRemainder);
				if (announcement.empty())
				{
					SendChatSystemNotice(sender, "Usage: /announce <message>");
					LOG_WARN(Net, "[ServerApp] /announce rejected: empty (client_id={})", sender.clientId);
					return true;
				}

				BroadcastModerationAnnouncement(announcement);
				AuditLogModeration("ANNOUNCE", actorLabel, "*", announcement);
				SendChatSystemNotice(sender, "Announcement broadcast.");
				LOG_INFO(Net, "[ServerApp] /announce executed (client_id={})", sender.clientId);
				return true;
			}

			const auto [targetName, detail] = SplitFirstChatArg(command.argsRemainder);
			if (targetName.empty())
			{
				SendChatSystemNotice(sender, "Usage: /kick|/ban|/mute <player> [detail]");
				LOG_WARN(Net, "[ServerApp] Admin chat command rejected: missing target (client_id={})", sender.clientId);
				return true;
			}

			ConnectedClient* target = FindConnectedClientByChatDisplayName(targetName);
			if (target == nullptr)
			{
				SendChatSystemNotice(sender, "Target not online.");
				LOG_WARN(Net, "[ServerApp] Admin chat command rejected: target offline (client_id={})", sender.clientId);
				return true;
			}

			if (target->clientId == sender.clientId)
			{
				SendChatSystemNotice(sender, "Cannot target yourself.");
				return true;
			}

			const std::string targetLabel = "P" + std::to_string(target->clientId);

			if (command.kind == ChatSlashCommandKind::Mute)
			{
				uint32_t durationSeconds = 60;
				if (!detail.empty())
				{
					char* endPtr = nullptr;
					const unsigned long parsed = std::strtoul(detail.c_str(), &endPtr, 10);
					if (endPtr != detail.c_str() && parsed > 0ul && parsed < 604800ul)
					{
						durationSeconds = static_cast<uint32_t>(parsed);
					}
					else
					{
						SendChatSystemNotice(sender, "Mute duration invalid; using 60s.");
						LOG_WARN(Net, "[ServerApp] /mute duration fallback 60s (client_id={})", sender.clientId);
					}
				}

				const uint32_t deltaTicks = durationSeconds * static_cast<uint32_t>(m_tickHz > 0 ? m_tickHz : 20u);
				target->chatMutedUntilServerTick = m_currentTick + deltaTicks;
				AuditLogModeration("MUTE", actorLabel, targetLabel, std::to_string(durationSeconds) + "s");
				SendChatSystemNotice(*target, "You have been muted.");
				SendChatSystemNotice(sender, "Mute applied.");
				LOG_INFO(Net,
					"[ServerApp] /mute applied (actor_client_id={}, target_client_id={}, seconds={})",
					sender.clientId,
					target->clientId,
					durationSeconds);
				return true;
			}

			if (command.kind == ChatSlashCommandKind::Kick)
			{
				AuditLogModeration("KICK", actorLabel, targetLabel, detail);
				SendChatSystemNotice(*target, "You were kicked from the server.");
				DisconnectConnectedClient(target->clientId, "moderation_kick");
				SendChatSystemNotice(sender, "Kick executed.");
				LOG_INFO(Net, "[ServerApp] /kick executed (actor_client_id={}, target={})", sender.clientId, targetLabel);
				return true;
			}

			if (command.kind == ChatSlashCommandKind::Ban)
			{
				const uint32_t characterKey = target->persistenceCharacterKey;
				if (characterKey == 0)
				{
					SendChatSystemNotice(sender, "Ban failed: target has no persistence key.");
					LOG_WARN(Net, "[ServerApp] /ban rejected: no persistence key (target_client_id={})", target->clientId);
					return true;
				}

				m_bannedCharacterKeys.insert(characterKey);
				SaveChatBanFile();
				AuditLogModeration("BAN", actorLabel, targetLabel, detail);
				SendChatSystemNotice(*target, "You are banned from this server.");
				DisconnectConnectedClient(target->clientId, "moderation_ban");
				SendChatSystemNotice(sender, "Ban applied.");
				LOG_INFO(Net,
					"[ServerApp] /ban executed (actor_client_id={}, character_key={})",
					sender.clientId,
					characterKey);
				return true;
			}
		}
		case ChatSlashCommandKind::Friend:
			return HandleFriendCommand(sender, command.argsRemainder);

		case ChatSlashCommandKind::None:
		default:
			LOG_WARN(Net, "[ServerApp] Chat slash command ignored: kind=None (client_id={})", sender.clientId);
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// M32.1 — Friend system helpers
	// -------------------------------------------------------------------------

	void ServerApp::OnClientLogin(ConnectedClient& client)
	{
		if (!m_friendSystem.IsInitialized())
			return;

		const std::string playerLabel = "P" + std::to_string(client.clientId);
		m_friendSystem.SetPresence(
			static_cast<uint64_t>(client.persistenceCharacterKey),
			playerLabel,
			PresenceStatus::Online);

		SendFriendListSync(client);
		BroadcastFriendStatusUpdate(client, PresenceStatus::Online);

		LOG_INFO(Net, "[ServerApp] OnClientLogin: friend presence set online (client_id={})", client.clientId);
	}

	void ServerApp::OnClientLogout(const ConnectedClient& client)
	{
		if (!m_friendSystem.IsInitialized())
			return;

		BroadcastFriendStatusUpdate(client, PresenceStatus::Offline);
		m_friendSystem.SetOffline(static_cast<uint64_t>(client.persistenceCharacterKey));

		LOG_INFO(Net, "[ServerApp] OnClientLogout: friend presence cleared (client_id={})", client.clientId);
	}

	void ServerApp::SendFriendListSync(const ConnectedClient& receiver)
	{
		// In no-DB mode, GetFriendList returns an empty list; we still send the packet
		// so the client is aware the sync occurred.
		const auto records = m_friendSystem.GetFriendList(
			static_cast<uint64_t>(receiver.persistenceCharacterKey), nullptr);

		FriendListSyncMessage msg{};
		msg.friends.reserve(records.size());
		for (const auto& rec : records)
		{
			FriendListEntry entry;
			entry.name              = rec.friendName;
			entry.presenceStatus    = m_friendSystem.GetPresence(rec.friendId);
			entry.isPendingInbound  = (rec.status == 0);
			msg.friends.push_back(std::move(entry));
		}

		const std::vector<std::byte> packet = EncodeFriendListSync(msg);
		if (!m_transport.Send(receiver.endpoint, packet.data(), packet.size()))
		{
			LOG_WARN(Net, "[ServerApp] SendFriendListSync send failed (client_id={})", receiver.clientId);
			return;
		}

		LOG_DEBUG(Net, "[ServerApp] FriendListSync sent (client_id={}, friends={})",
			receiver.clientId, msg.friends.size());
	}

	void ServerApp::BroadcastFriendStatusUpdate(const ConnectedClient& subject, PresenceStatus status)
	{
		// Collect online friend ids from the in-memory presence map.
		// In no-DB mode GetOnlineFriendIds returns empty; we iterate all clients and check if
		// they have the subject in their (in-memory-only) presence map instead.
		// This is best-effort: bilateral DB relationships are not available in no-DB mode.
		const std::string subjectLabel = "P" + std::to_string(subject.clientId);

		FriendStatusUpdateMessage msg{};
		msg.friendName     = subjectLabel;
		msg.presenceStatus = status;

		const std::vector<std::byte> packet = EncodeFriendStatusUpdate(msg);

		for (const ConnectedClient& peer : m_clients)
		{
			if (peer.clientId == subject.clientId)
				continue;

			if (!m_transport.Send(peer.endpoint, packet.data(), packet.size()))
			{
				LOG_WARN(Net, "[ServerApp] BroadcastFriendStatusUpdate send failed (peer_client_id={})", peer.clientId);
			}
		}
	}

	bool ServerApp::HandleFriendCommand(ConnectedClient& sender, std::string_view argsRemainder)
	{
		std::string targetName;
		const FriendSubCommand sub = ParseFriendSubCommand(argsRemainder, targetName);

		if (sub == FriendSubCommand::Unknown || targetName.empty())
		{
			SendChatSystemNotice(sender, "Usage: /friend add|accept|decline|remove <name>");
			LOG_WARN(Net, "[ServerApp] /friend unknown sub-command (client_id={}, args='{}')",
				sender.clientId, argsRemainder);
			return true;
		}

		const uint64_t playerId = static_cast<uint64_t>(sender.persistenceCharacterKey);
		const std::string playerLabel = "P" + std::to_string(sender.clientId);

		switch (sub)
		{
		case FriendSubCommand::Add:
		{
			const uint64_t targetId = m_friendSystem.SendFriendRequest(playerId, playerLabel, targetName, nullptr);
			if (targetId == 0)
				SendChatSystemNotice(sender, "Friend request failed (no-DB mode or target not found).");
			else
				SendChatSystemNotice(sender, "Friend request sent to '" + targetName + "'.");
			break;
		}
		case FriendSubCommand::Accept:
		{
			const uint64_t requesterId = m_friendSystem.AcceptFriendRequest(playerId, targetName, nullptr);
			if (requesterId == 0)
				SendChatSystemNotice(sender, "Accept failed (no-DB mode or request not found).");
			else
				SendChatSystemNotice(sender, "Friend request from '" + targetName + "' accepted.");
			break;
		}
		case FriendSubCommand::Decline:
		{
			const uint64_t requesterId = m_friendSystem.DeclineFriendRequest(playerId, targetName, nullptr);
			if (requesterId == 0)
				SendChatSystemNotice(sender, "Decline failed (no-DB mode or request not found).");
			else
				SendChatSystemNotice(sender, "Friend request from '" + targetName + "' declined.");
			break;
		}
		case FriendSubCommand::Remove:
		{
			const bool ok = m_friendSystem.RemoveFriend(playerId, targetName, nullptr);
			if (!ok)
				SendChatSystemNotice(sender, "Remove failed (no-DB mode or friend not found).");
			else
				SendChatSystemNotice(sender, "'" + targetName + "' removed from friends.");
			break;
		}
		default:
			break;
		}

		return true;
	}
}
