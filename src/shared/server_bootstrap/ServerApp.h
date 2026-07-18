#pragma once

#include "src/shardd/gameplay/auction/AuctionHouse.h"
#include "src/shardd/gameplay/character/CharacterPersistence.h"
#include "src/shardd/gameplay/character/CharacterStatsTables.h"
#include "src/shardd/gameplay/character/SpawnStatsResolver.h"
#include "src/shardd/gameplay/crafting/CraftingSystem.h"
#include "src/shardd/gameplay/economy/CurrencyConfig.h"
#include "src/shardd/gameplay/gathering/GatheringSystem.h"
#include "src/shardd/gameplay/economy/PlayerWalletService.h"
#include "src/shardd/gameplay/trade/TradeSystem.h"
#include "src/shardd/gameplay/economy/VendorCatalog.h"
#include "src/shardd/gameplay/event/EventRuntime.h"
#include "src/shardd/gameplay/social/FriendSystem.h"
#include "src/shardd/gameplay/social/PartySystem.h"
#include "src/shardd/gameplay/quest/QuestRuntime.h"
#include "src/shardd/gameplay/spawner/SpawnerRuntime.h"
#include "src/shardd/gameplay/creature/CreatureArchetypeLibrary.h"
#include "src/shardd/gameplay/spell/SpellKitLibrary.h"
#include "src/shardd/gameplay/anniversary/CakeBuffLibrary.h" // SP3 anniversaires (2026-07-18)
#include "src/shardd/gameplay/spell/ClassSkillLibrary.h"
#include "src/shared/items/ItemCatalog.h"
#include "src/shared/core/Config.h"
#include "src/shared/net/ChatSystem.h"
#include "src/shardd/gameplay/chat/ChatCommandParser.h"
#include "src/shared/network/ReplicationTypes.h"
#include "src/shared/network/ShardPayloads.h"
#include "src/shardd/world/ShardPresenceService.h"
#include "src/shared/security/SecurityAuditLog.h"
#include "src/shardd/anticheat/AntiCheatGameplay.h"
#include "src/shared/network/ServerProtocol.h"
#include "src/shardd/world/SpatialPartition.h"
#include "src/shardd/world/TickScheduler.h"
#include "src/shardd/world/UdpTransport.h"
#include "src/shardd/world/ZoneTransitions.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <cstddef>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <span>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::server
{
	/// Minimal mob AI states required by the authoritative aggro ticket.
	enum class MobAiState : uint8_t
	{
		Idle = 0,
		Patrol = 1,
		Aggro = 2,
		Return = 3
	};

	/// One threat contribution stored on the mob authoritative table.
	struct ThreatEntry
	{
		EntityId entityId = 0;
		uint32_t threat = 0;
	};

	/// Combat SP3 — une aura active sur une entité (joueur ou mob). Les montants
	/// périodiques (`tickAmount`) sont FIGÉS à l'application (snapshot des stats
	/// du casteur) : un casteur déconnecté ne casse pas le tick.
	struct ActiveAura
	{
		std::string spellId;
		SpellEffectType type = SpellEffectType::DirectDamage;
		/// Pourcentage (Buff/Debuff/Slow) — 0 pour les effets périodiques.
		float percent = 0.0f;
		/// Montant par tick (DoT = dégâts, HoT = soin) — 0 pour les effets %.
		uint32_t tickAmount = 0;
		uint32_t tickPeriodMs = 0;
		uint64_t expiresAtMs = 0;
		uint64_t nextTickAtMs = 0;
		uint8_t stacks = 1;
		EntityId casterEntityId = 0;
	};

	/// Visibility mode applied to one authoritative loot bag.
	enum class LootVisibility : uint8_t
	{
		Owner = 0,
		Public = 1
	};

	/// Runtime status stored for one dynamic zone event.
	enum class DynamicEventStatus : uint8_t
	{
		Idle = 0,
		Active = 1,
		Cooldown = 2
	};

	/// One row loaded from the loot table data file.
	struct LootTableEntry
	{
		uint32_t sourceArchetypeId = 0;
		ItemStack item{};
		LootVisibility visibility = LootVisibility::Owner;
	};

	/// One connected client tracked by endpoint and assigned server id.
	struct ConnectedClient
	{
		Endpoint endpoint{};
		uint32_t clientId = 0;
		uint32_t zoneId = 1;
		EntityId entityId = 0;
		uint32_t archetypeId = 1;
		uint32_t lastInputSequence = 0;
		/// TA.3 — timeout zombie : rafraichi a chaque Hello/Input (UDP). Utilise par
		/// EvictIdleClients pour evincer un client qui ne souffle plus depuis > N ticks
		/// (le NetServer TCP a son propre timeout, mais l'UDP gameplay non — sans ca,
		/// les avatars de joueurs deconnectes restent broadcastes indefiniment).
		uint64_t lastUdpActivityAtMs = 0;
		/// Phase 3.7.5 — élargi à uint64 pour porter le character_id complet.
		uint64_t helloNonce = 0;
		uint64_t persistenceCharacterKey = 0;
		/// Combat SP2 — position de réapparition (mémorisée en fin d'admission,
		/// après résolution de la position persistée/DB). Utilisée par
		/// HandleRespawnRequest ; le « spawn de zone » V1 = point d'entrée en monde.
		float spawnPositionMetersX = 0.0f;
		float spawnPositionMetersY = 0.0f;
		float spawnPositionMetersZ = 0.0f;
		/// Combat SP3 — profil de classe ("melee", "tank", …) résolu à l'enter-world
		/// via factions.json ; vide = pas de kit de sorts (perso legacy / no-DB).
		std::string profileId;
		/// Combat SP3 — ressource secondaire runtime (le coût des sorts est en %
		/// du max ; régénérée par RegenerateResources : 5 %/s hors combat, 2 %/s
		/// en combat — décision utilisateur).
		uint32_t currentResource = 0;
		uint32_t maxResource = 0;
		/// Accumulateur fractionnaire de régénération (la régén par tick < 1).
		float resourceRegenCarry = 0.0f;
		/// Timestamp ms de la dernière implication dans un CombatEvent (attaquant
		/// ou cible) ; « en combat » = moins de 5 s.
		uint64_t lastCombatInvolvementMs = 0;
		/// Combat SP3 — throttle d'envoi des ResourceUpdate (au plus 1 / 500 ms).
		uint64_t lastResourceUpdateSentMs = 0;
		uint32_t lastResourceUpdateSentValue = 0;
		/// Correction SP1 — throttle d'envoi des ForcePosition anti-triche
		/// (au plus 1 / 500 ms ; les respawns/téléports ne sont pas throttlés).
		uint64_t lastForcePositionSentMs = 0;
		/// Combat SP3 — auras actives (buffs, HoT, debuffs subis).
		std::vector<ActiveAura> auras;
		/// Anniversaires SP3 (2026-07-18) — gâteau ACTIF (0 = aucun). Posé par
		/// un CastRequest "item:<id>" ; le buff est entretenu par TickCakeBuffs
		/// tant que le gâteau reste slotté ET possédé ; NON persisté (à
		/// réactiver après reconnexion — geste volontaire du joueur).
		uint32_t activeCakeItemId = 0;
		/// Anniversaires SP3 — expiration UTC (epoch ms) par gâteau possédé :
		/// posée à l'entrée en inventaire (fin du jour UTC courant), consommée
		/// par la purge de minuit de TickCakeBuffs. Persistée
		/// (clés cake_expiry.<itemId> du fichier personnage).
		std::map<uint32_t, uint64_t> cakeExpiresAtMsUtcByItemId;
		/// Combat SP3 — cast en cours (vide = aucun). Annulé si le joueur meurt ou
		/// se déplace de plus de 0,5 m pendant l'incantation.
		std::string activeCastSpellId;
		uint64_t castFinishAtMs = 0;
		float castStartPosX = 0.0f;
		float castStartPosZ = 0.0f;
		EntityId castTargetEntityId = 0;
		/// Combat SP3 — cooldowns par sort (spellId → fin de cooldown en ms).
		std::unordered_map<std::string, uint64_t> spellCooldownUntilMs;
		/// TD.5 — nom du personnage choisi par le joueur (cf. table SQL characters.name).
		/// Chargé depuis la DB (LoadSpawnFromDb) si le shard a un pool MySQL configuré ;
		/// sinon (mode no-DB) repris du push master AdmitCharacter via
		/// AdmittedCharacterRegistry::AdmittedCharacterName. Recopié dans chaque
		/// SnapshotEntity pour ce client → les avatars distants peuvent afficher le vrai
		/// nom dans leur plaque (au lieu du fallback "P<clientId>").
		std::string characterName;
		/// TD.6 — genre du personnage ("male"/"female", cf. migration 0067). Même chaîne
		/// d'acquisition que `characterName` : LoadSpawnFromDb avec pool DB, sinon via
		/// AdmittedCharacterRegistry::AdmittedGender. Recopié dans chaque SnapshotEntity
		/// pour permettre au client de sélectionner le mesh skinné des avatars distants.
		std::string gender;
		/// R1-A — faction et classe du personnage (table characters.faction/class, chargées au
		/// Hello via LoadSpawnFromDb). Alimentent le moteur de stats (ResolveSpawnHealth) pour
		/// calculer les PV max à l'enter-world. Vides si char legacy / mode no-DB → le resolver
		/// renvoie resolved=false et les PV par défaut (kDefaultPlayerHealth) sont conservés.
		std::string factionId;
		std::string classId;
		uint32_t experiencePoints = 0;
		/// Compte propriétaire (résolu au Hello via AdmittedCharacterRegistry). Clé de la
		/// présence unifiée (ShardPresenceService) ; 0 si non résolu (registre absent).
		uint64_t accountId = 0;
		/// Niveau du personnage (table characters.level, chargé au Hello via LoadSpawnFromDb).
		/// Reporté au master dans le heartbeat enrichi (présence web-portal).
		uint32_t level = 1;
		uint32_t gold = 0;
		/// M35.1 — additional currencies (wallet); gold remains primary trade currency.
		uint32_t honor = 0;
		uint32_t badges = 0;
		uint32_t premiumCurrency = 0;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f;
		float positionMetersZ = 0.0f;
		float yawRadians = 0.0f;
		float velocityMetersPerSecondX = 0.0f;
		float velocityMetersPerSecondY = 0.0f;
		float velocityMetersPerSecondZ = 0.0f;
		/// TD.8 — état d'animation reporté par le client (valeur d'AvatarAnimState). Recopié
		/// dans chaque SnapshotEntity pour que les autres joueurs voient emotes/roulades/etc.
		uint8_t animationState = 0;
		uint32_t stateFlags = 0;
		StatsComponent stats{};
		CombatComponent combat{};
		CellCoord currentCell{};
		bool hasCell = false;
		bool hasReplicatedState = false;
		std::vector<CellCoord> interestCells;
		std::vector<EntityId> interestEntityIds;
		std::vector<EntityId> replicatedEntityIds;
		std::vector<ItemStack> inventory;
		/// Chantier 2 SP-A — équipement porté. Indexé par la valeur de
		/// engine::items::EquipmentSlot (1..kEquipSlotCount) ; index 0 (None) inutilisé.
		/// 0 = slot vide, sinon itemId. Persisté + poussé au client via EquipmentUpdate.
		std::array<uint32_t, engine::items::kEquipSlotCount + 1> equipment{};
		std::vector<QuestState> questStates;
		/// M29.2: ignored chat senders (display names, persisted).
		std::vector<std::string> chatIgnoredDisplayNames;
		/// M29.2: may use `/kick`, `/ban`, `/mute`, `/announce`.
		bool chatModeratorRole = false;
		/// M29.2: block chat sends until this tick (0 = not muted).
		uint32_t chatMutedUntilServerTick = 0;
		/// M36.2: known crafting professions and skill levels.
		std::vector<ProfessionEntry> professions;
		/// Grimoire — 10 slots de barre d'action (slot i → spellId, "" = vide).
		std::array<std::string, 10> actionBarLayout{};
		/// SP-B — compétences par-classe déjà choisies (un skill par tier/niveau débloqué).
		std::vector<std::string> knownSkillIds;
	};

	/// Minimal authoritative mob replicated through the same interest system as players.
	struct MobEntity
	{
		EntityId entityId = 0;
		uint32_t zoneId = 1;
		uint32_t archetypeId = 0;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f;
		float positionMetersZ = 0.0f;
		float yawRadians = 0.0f;
		float velocityMetersPerSecondX = 0.0f;
		float velocityMetersPerSecondY = 0.0f;
		float velocityMetersPerSecondZ = 0.0f;
		uint32_t stateFlags = 0;
		StatsComponent stats{};
		CombatComponent combat{};
		float homePositionMetersX = 0.0f;
		float homePositionMetersY = 0.0f;
		float homePositionMetersZ = 0.0f;
		float patrolTargetMetersX = 0.0f;
		float patrolTargetMetersZ = 0.0f;
		float leashDistanceMeters = 0.0f;
		float moveSpeedMetersPerSecond = 0.0f;
		MobAiState aiState = MobAiState::Idle;
		EntityId aggroTargetEntityId = 0;
		uint32_t nextAiTick = 0;
		uint32_t nextPatrolTick = 0;
		uint32_t owningSpawnerIndex = 0;
		uint32_t owningSpawnerSlot = 0;
		uint32_t owningEventIndex = 0;
		uint32_t owningEventPhaseIndex = 0;
		bool patrolForward = true;
		bool pendingDespawn = false;
		bool isDynamicEventMob = false;
		std::vector<ThreatEntry> threatTable;
		bool hasSpawnedLoot = false;
		/// Combat SP1 — XP attribuée au(x) tueur(s), copiée depuis l'archétype au
		/// spawn (0 = fallback sur kBaseXpPerMobKill, mob d'avant le catalogue).
		uint32_t xpReward = 0;
		/// Combat SP3 — auras actives (DoT, ralentissements, debuffs).
		std::vector<ActiveAura> auras;
		/// Combat SP4 — throttle du push ThreatUpdate (≤ 1/s et sur changement).
		uint64_t lastThreatPushMs = 0;
		uint64_t lastThreatHash = 0;
	};

	/// One spawn slot tracked by the authoritative spawner runtime.
	struct SpawnerSlotState
	{
		EntityId mobEntityId = 0;
		uint32_t nextRespawnTick = 0;
	};

	/// One loaded spawner with runtime activation and respawn state.
	struct SpawnerRuntimeState
	{
		SpawnerDefinition definition{};
		CellCoord centerCell{};
		bool isActive = false;
		std::vector<SpawnerSlotState> slots;
	};

	/// One dynamic event runtime state advanced by the authoritative server tick.
	struct DynamicEventState
	{
		DynamicEventDefinition definition{};
		DynamicEventStatus status = DynamicEventStatus::Idle;
		uint32_t currentPhaseIndex = 0;
		uint32_t currentPhaseProgress = 0;
		uint32_t nextTriggerTick = 0;
		uint32_t cooldownUntilTick = 0;
		std::vector<EntityId> phaseMobEntityIds;
		std::vector<uint32_t> participantClientIds;
	};

	/// Minimal authoritative loot bag spawned from a mob death.
	struct LootBagEntity
	{
		EntityId entityId = 0;
		uint32_t zoneId = 1;
		uint32_t archetypeId = 0;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f;
		float positionMetersZ = 0.0f;
		float yawRadians = 0.0f;
		float velocityMetersPerSecondX = 0.0f;
		float velocityMetersPerSecondY = 0.0f;
		float velocityMetersPerSecondZ = 0.0f;
		uint32_t stateFlags = 0;
		LootVisibility visibility = LootVisibility::Owner;
		EntityId ownerEntityId = 0;
		std::vector<ItemStack> items;
	};

	class AdmittedCharacterRegistry;
	namespace db { class ConnectionPool; }

	/// Headless authoritative server skeleton with fixed ticks, replication and minimal combat.
	class ServerApp final
	{
	public:
		/// Store the merged config used by the server runtime.
		explicit ServerApp(engine::core::Config config);

		/// Ensure transport resources are released on teardown.
		~ServerApp();

		/// Initialize transport, scheduler and runtime settings.
		bool Init();

		/// Run the fixed tick loop until a stop is requested.
		int Run();

		/// Request a graceful shutdown from the main loop.
		void RequestStop();

		/// Release transport resources and emit shutdown logs.
		void Shutdown();

		/// TA.3 : gate de session. Si défini, HandleHello n'accepte un Hello que si son
		/// nonce (character_id) est admis dans ce registre (rempli par le handshake TCP au
		/// vu du ticket signé). Non défini → comportement historique (build Windows). Le
		/// registre appartient à l'appelant (shardd/main_linux) et doit survivre à ServerApp.
		void SetAdmittedCharacterRegistry(AdmittedCharacterRegistry* registry) { m_admittedRegistry = registry; }

		/// Présence enrichie (web-portal) : snapshot thread-safe des joueurs en jeu
		/// `{accountId, characterId, level, zoneId}`. Publié à chaque TickOnce (thread
		/// gameplay) et lu ici depuis un AUTRE thread (boucle shard→master). Renvoie une
		/// copie sous mutex. accountId est résolu via l'AdmittedCharacterRegistry ; une
		/// entrée dont l'accountId n'a pu être résolu (0) est omise.
		std::vector<engine::network::ShardPlayerPresence> GetPlayerPresenceSnapshot() const;

		/// TA.4 : pool MySQL (même base que le master) d'où lire la position de spawn
		/// (table characters) au HandleHello. Optionnel : absent → spawn depuis le fichier
		/// (build Windows ou DB non configurée). Possédé par l'appelant.
		void SetCharacterDbPool(db::ConnectionPool* pool) { m_characterDbPool = pool; }

		/// M35.1 — add currency to a connected player (caps enforced).
		bool TryAddCurrency(uint32_t clientId, uint8_t currencyId, uint64_t amount, std::string& outError);

		/// M35.1 — subtract currency from a connected player (overdraft prevented).
		bool TrySubtractCurrency(uint32_t clientId, uint8_t currencyId, uint64_t amount, std::string& outError);

		/// M35.1 — transfer currency between two connected players (atomic single-threaded).
		bool TryTransferCurrency(
			uint32_t fromClientId,
			uint32_t toClientId,
			uint8_t currencyId,
			uint64_t amount,
			std::string& outError);

	private:
		/// Return a mutable connected client by stable client id, or nullptr.
		ConnectedClient* FindConnectedClient(uint32_t clientId);
		/// Clamp and validate the configured fixed tick rate.
		uint16_t ResolveTickHz() const;

		/// Clamp and validate the configured snapshot send rate.
		uint16_t ResolveSnapshotHz(uint16_t tickHz) const;

		/// Receive pending datagrams and dispatch them to protocol handlers.
		/// TG.3 — quand le mode `split-receive` est actif, lit depuis le buffer rempli par le
		/// thread réseau (sous mutex) au lieu d'appeler directement `m_transport.Receive`.
		void ProcessIncomingPackets();

		/// TG.3 — boucle exécutée par `m_networkThread` : appelle `m_transport.Receive` en
		/// continu, accumule dans `m_networkIngressQueue` sous mutex, dort brièvement quand
		/// la socket est vide (anti-CPU-spin). Sortie quand `m_networkThreadStopRequested`.
		void NetworkPumpLoop();

		/// Handle one protocol packet coming from a client endpoint.
		void ProcessPacket(const Datagram& datagram);

		/// TA.4 : lit la position de spawn (table characters, écrite par le master) pour
		/// \a characterId via m_characterDbPool. Renvoie true + remplit x/y/z/yawDeg si trouvé.
		/// Renvoie false si pas de pool, DB non configurée, ou personnage absent (le caller
		/// garde alors la position fichier). Corps réel sous ENGINE_HAS_MYSQL (sinon no-op).
		/// TA.4 — charge spawn (position + yaw) depuis `characters` ; TD.5 — récupère aussi
		/// `name` pour le porter dans le SnapshotEntity (plaque de nom des avatars distants).
		/// Renvoie false si pas de pool DB (Windows / DB non configurée) ou si character
		/// introuvable ; dans ce cas les out-params ne sont pas modifiés.
		/// R1-A — récupère aussi `faction` et `class` (out-params) pour alimenter le moteur de
		/// stats (PV à l'enter-world). Vides si la colonne est NULL/absente.
		bool LoadSpawnFromDb(uint64_t characterId, float& x, float& y, float& z, float& yawDeg,
		std::string& outName, std::string& outGender, uint32_t& outLevel,
		std::string& outFactionId, std::string& outClassId);

		/// Accept a new client or refresh an existing handshake.
		/// Phase 3.7.5 — \p helloNonce élargi à uint64 (character_id complet).
		void HandleHello(const Endpoint& endpoint, uint64_t helloNonce);

		/// TA.3 — Retraite les Hellos en attente d'admission. Appelé au début de chaque
		/// TickOnce : pour chaque entrée du buffer, si le registry indique maintenant
		/// l'admission, on rappelle HandleHello (l'admission ayant été poussée entre temps
		/// par le master via kOpcodeMasterToShardAdmitCharacter). Entrées expirées (TTL)
		/// supprimées silencieusement.
		void DrainPendingHellos();

		/// TA.3 — Évince les clients dont la dernière activité UDP date de plus de
		/// kUdpClientIdleTimeoutMs. Le UDP est sans connexion : un client qui ferme son
		/// process ne génère aucun FIN, donc sans timeout actif son `ConnectedClient` resterait
		/// dans `m_clients` et sa position continuerait d'être broadcastée aux autres
		/// (avatars / nameplates fantômes). Appelé périodiquement dans TickOnce.
		void EvictIdleUdpClients();

		/// Record the last input sequence for a connected client.
		void HandleInput(const Endpoint& endpoint, uint32_t clientId, uint32_t inputSequence, float positionMetersX, float positionMetersY, float positionMetersZ, float yawRadians, uint8_t animationState);
		/// Départ propre du client (message Goodbye) : éviction immédiate de l'entité au lieu
		/// d'attendre le timeout d'inactivité. Évite l'avatar « fantôme » (cf. CODEBASE_MAP §60).
		void HandleGoodbye(const Endpoint& endpoint, uint32_t clientId);

		/// Advance one authoritative server tick.
		void TickOnce();

		/// Run the placeholder simulation step for the current tick.
		void Simulate();

		/// Load the data-driven spawners and prepare their runtime state.
		bool InitSpawners();

		/// Resolve the autosave cadence required by the persistence ticket.
		uint32_t ResolveCharacterAutosaveIntervalTicks() const;

		/// Persist every connected character when the autosave timer elapses.
		void MaybeAutosaveCharacters();

		/// Persist one connected character to the current runtime store.
		void SaveConnectedClient(const ConnectedClient& client, std::string_view reason);

		/// Load the authoritative loot table data required by the ticket.
		bool InitLootTables();

		/// Validation v12 (wire v13) — charge `respawn/respawn_points.txt`
		/// (cimetières/auberges par zone). NON bloquant : fichier absent =
		/// aucun point, le respawn retombe sur le point d'entrée en monde.
		void InitRespawnPoints();

		/// Load the data-driven quest definitions required by M15.1.
		bool InitQuests();

		/// Load the data-driven dynamic event definitions required by M15.3.
		bool InitDynamicEvents();

		/// Refresh spawner activation, despawn inactive mobs and process respawns.
		void UpdateSpawners();

		/// Advance event triggers, phases, rewards and notifications.
		void UpdateDynamicEvents();

		/// Update the mob AI state machine at a reduced fixed cadence.
		void UpdateMobAi();

		/// Advance one mob through the authoritative AI state machine.
		void UpdateMobAi(MobEntity& mob);

		/// Return true when at least one player is close enough to activate the spawner.
		bool IsSpawnerActivatedByPlayers(const SpawnerRuntimeState& spawner) const;

		/// Return true when one living spawned mob is still in combat.
		bool SpawnerHasCombat(const SpawnerRuntimeState& spawner) const;

		/// Spawn one authoritative mob for the requested spawner slot.
		bool SpawnMobFromSpawner(SpawnerRuntimeState& spawner, uint32_t slotIndex);

		/// Despawn one authoritative mob and optionally arm its respawn timer.
		bool DespawnSpawnerMob(SpawnerRuntimeState& spawner, uint32_t slotIndex, std::string_view reason, bool scheduleRespawn);

		/// Schedule the next activation tick for one dynamic event.
		void ScheduleDynamicEventTrigger(DynamicEventState& eventState, bool fromCooldown);

		/// Start one dynamic event and spawn its first phase.
		bool StartDynamicEvent(DynamicEventState& eventState);

		/// Spawn the currently active phase mobs for one dynamic event.
		bool SpawnDynamicEventPhase(DynamicEventState& eventState);

		/// Spawn one authoritative mob for one dynamic event phase.
		bool SpawnMobForDynamicEvent(
			DynamicEventState& eventState,
			uint32_t phaseIndex,
			const DynamicEventSpawnDefinition& spawnDefinition,
			uint32_t spawnOrdinal);

		/// Despawn one authoritative mob currently owned by a dynamic event.
		bool DespawnDynamicEventMob(DynamicEventState& eventState, EntityId entityId, std::string_view reason);

		/// Advance one event to the next phase or finish it when every phase completed.
		bool AdvanceDynamicEventPhase(DynamicEventState& eventState);

		/// Complete one dynamic event and grant rewards to recorded participants.
		void CompleteDynamicEvent(DynamicEventState& eventState);

		/// Record one player as participant of the given dynamic event.
		void AddDynamicEventParticipant(DynamicEventState& eventState, const ConnectedClient& client);

		/// Return true when at least one active player is present in the requested zone.
		bool HasPlayersInZone(uint32_t zoneId) const;

		/// Return the number of server ticks between two AI updates.
		uint32_t ResolveMobAiIntervalTicks() const;

		/// Build and send empty snapshots according to the snapshot cadence.
		void MaybeSendSnapshots();

		/// Update the client's grid cell and recompute its interest set if needed.
		void UpdateClientInterest(ConnectedClient& client);

		/// Validate and apply a server-authoritative zone transition when a player enters a transition volume.
		void MaybeApplyZoneTransition(ConnectedClient& client);

		/// Validate one attack request, apply damage and broadcast the authoritative event.
		void HandleAttackRequest(const Endpoint& endpoint, uint32_t clientId, EntityId targetEntityId);

		/// Combat SP2 — réapparition d'un joueur mort : PV pleins, flag dead
		/// retiré. Wire v13 : téléport au point de réapparition du type demandé
		/// (\p destination, kRespawnDestination*) le plus proche du lieu de
		/// mort ; repli sur le spawn mémorisé à l'admission si la zone n'en
		/// définit aucun. Ignoré si le joueur est vivant.
		void HandleRespawnRequest(const Endpoint& endpoint, uint32_t clientId, uint8_t destination);

		/// Correction SP1 — impose la position serveur courante au client
		/// (cf. MessageKind::ForcePosition). Le mouvement client-autoritaire
		/// reprend ensuite depuis cette position.
		void SendForcePosition(ConnectedClient& client, uint8_t reason);

		/// Validation v12 — butin AUTOMATIQUE : à la mort d'un mob, crédite
		/// directement l'inventaire du looter (résolu en amont par le mode de
		/// loot du groupe) et pousse InventoryDelta + LootNotify (fenêtre de
		/// butin client). Plus de sac à ramasser au clavier ; si le looter
		/// n'est plus connecté, retombe sur l'ancien SpawnLootBagForMob.
		void AutoLootMobToKiller(const MobEntity& mob, EntityId looterEntityId);

		/// Validate one pickup request, update the inventory and despawn the bag.
		void HandlePickupRequest(const Endpoint& endpoint, uint32_t clientId, EntityId lootBagEntityId);

		/// Chantier 2 SP-A — équipe l'objet `itemId` (présent au sac) du client. Le
		/// slot et le bonus sont résolus depuis m_itemCatalog (autoritaire). Retire un
		/// exemplaire du sac, replace l'éventuel objet déjà porté dans le sac, met à
		/// jour equipment[slot], persiste, puis pousse EquipmentUpdate + PlayerStats.
		void HandleEquipRequest(const Endpoint& endpoint, uint32_t clientId, uint32_t itemId);

		/// Chantier 2 SP-A — retire l'objet du slot `slot` (1..kEquipSlotCount) et le
		/// renvoie au sac. Persiste puis pousse EquipmentUpdate + PlayerStats.
		void HandleUnequipRequest(const Endpoint& endpoint, uint32_t clientId, uint8_t slot);

		/// Chantier 2 SP-A — pousse au client le snapshot complet de son équipement porté.
		bool SendEquipmentUpdate(const ConnectedClient& receiver);

		/// Chantier 2 SP-A — somme des StatBonus de tout l'équipement porté (résolus
		/// via m_itemCatalog). Additionnée aux DerivedStats avant émission (anti-triche).
		engine::items::StatBonus SumEquipmentBonus(const ConnectedClient& client) const;

		/// Chantier 2 SP-A — additionne le bonus d'équipement du client aux stats
		/// dérivées `d` (clamp ≥ 0 ; les champs uint32 passent par int64 pour éviter
		/// tout underflow avec un bonus négatif). Ne lit jamais de valeur client.
		void ApplyEquipmentBonus(const ConnectedClient& client, engine::server::gameplay::DerivedStats& d) const;

		/// Chantier 2 SP-A — recalcule les stats runtime vivantes (combat, ressource
		/// max, PV max) d'un client déjà en monde en tenant compte de l'équipement,
		/// puis clamp les valeurs courantes sur les nouveaux max. Appelée après un
		/// equip/unequip. Sans effet si les tables de stats ou l'identité RPG manquent.
		void RefreshLiveDerivedStats(ConnectedClient& client);

		/// Validate one talk request and forward it to the quest runtime.
		void HandleTalkRequest(const Endpoint& endpoint, uint32_t clientId, std::string_view targetId);

		/// Validate one chat send request, apply rate limiting, route by channel, emit relays.
		void HandleChatSend(const Endpoint& endpoint, const ChatSendRequestMessage& request);

		/// Update one mob threat table from an authoritative combat event.
		void UpdateThreatFromCombatEvent(const CombatEventMessage& message);

		/// Spawn one authoritative loot bag from a dead mob using the loaded loot tables.
		void SpawnLootBagForMob(const MobEntity& mob, EntityId ownerEntityId);

		/// Refresh the mob aggro target from the current threat table.
		void RefreshMobAggroTarget(MobEntity& mob);

		/// Move one mob toward a target point and update its replicated transform.
		bool MoveMobTowards(MobEntity& mob, float targetPositionX, float targetPositionZ);

		/// Synchronize one mob position with the zone spatial partition.
		bool UpdateMobSpatialState(MobEntity& mob);

		/// Transition one mob to a new authoritative AI state.
		void SetMobAiState(MobEntity& mob, MobAiState newState);

		/// Clear one mob threat table and active aggro target.
		void ResetMobThreat(MobEntity& mob);

		/// Combat SP2 — retire une entité de toutes les tables de menace des mobs
		/// (mort ou respawn du joueur) ; les mobs qui la ciblaient repassent en
		/// patrouille au prochain tick d'IA.
		void PurgeThreatForEntity(EntityId entityId);

		/// Combat SP2 — tire un jet uniforme [0,1) sur le RNG combat du serveur.
		/// Les jets sont consommés par AttackResolver (pur) ; le RNG n'est PAS
		/// déterministe (seed random_device au boot) — les tests passent par
		/// ResolveAttackRoll directement.
		float NextCombatRoll01();

		/// Combat SP3 — régénère la ressource secondaire de chaque joueur connecté
		/// (par tick : 5 %/s hors combat, 2 %/s en combat, accumulé en fractionnaire).
		/// Appelée depuis Simulate (main thread du tick monde).
		void RegenerateResources();

		/// Combat SP3 — pousse la ressource courante au casteur (throttle 500 ms,
		/// sauf \p force pour les débits de cast — feedback immédiat).
		void SendResourceUpdate(ConnectedClient& client, bool force);

		/// Combat SP3 — demande de cast d'un sort du kit du profil du joueur.
		/// Valide profil/sort/cooldown/ressource/cible/portée ; instant = résolution
		/// immédiate, sinon cast armé (résolu/annulé par TickActiveCasts).
		void HandleCastRequest(const Endpoint& endpoint, const CastRequestMessage& message);

		/// Grimoire — réassignation des 10 slots de barre d'action (kind 88).
		/// Valide chaque spellId contre le kit du profil + unicité ; applique et
		/// renvoie un ActionBarLayoutUpdate autoritaire. En cas de rejet, renvoie
		/// le layout inchangé (réconciliation silencieuse côté client).
		void HandleSetActionBarLayout(const Endpoint& endpoint, const SetActionBarLayoutMessage& message);

		/// Grimoire — pousse le layout courant du client (enter-world ou ACK).
		/// \return false si l'envoi UDP a échoué.
		bool SendActionBarLayout(const ConnectedClient& client);

		/// SP-B — pousse l'état autoritaire de progression de classe (classId + skills connus)
		/// au client (enter-world ou ACK après choix validé).
		/// \return false si l'envoi UDP a échoué.
		bool SendClassProgression(const ConnectedClient& client);

		/// SP-B — traite un choix de compétence par-classe (kind 91 ChooseClassSkillRequest).
		/// Valide le skill via ClassSkillLibrary, l'unicité par tier et le niveau requis.
		/// En cas de rejet, renvoie l'état inchangé via SendClassProgression.
		void HandleChooseClassSkill(const Endpoint& endpoint, const ChooseClassSkillRequestMessage& message);

		/// Combat SP3 — avance les casts en cours (annulation mort/déplacement,
		/// résolution à l'échéance). Appelée depuis Simulate.
		void TickActiveCasts();

		/// Combat SP3 — tick des auras (DoT/HoT dus, expirations) des joueurs et
		/// des mobs + broadcast AuraUpdate sur changement. Appelée depuis Simulate.
		void TickAuras();

		/// Anniversaires SP3 (2026-07-18) — activation du gâteau \p cakeItemId
		/// (reçue en CastRequest "item:<id>") : vérifie slotté + possédé + non
		/// expiré, pose activeCakeItemId et notifie le joueur (chat système).
		void HandleCakeActivation(ConnectedClient& client, uint32_t cakeItemId);

		/// Anniversaires SP3 — entretien (1 Hz) des buffs de gâteau : aura à
		/// TTL court re-rafraîchie sur le porteur + membres du GROUPE à
		/// portée tant que le gâteau est slotté et possédé (guilde = dette,
		/// le shard n'a pas l'appartenance de guilde) ; ET purge de
		/// minuit UTC (gâteau « mangé » : retiré de l'inventaire et des slots,
		/// notice + resync inventaire/barre). \param nowMonotonicMs horloge
		/// monotone des auras (l'expiration jour J utilise l'horloge UTC).
		void TickCakeBuffs(uint64_t nowMonotonicMs);

		/// Combat SP3 — applique tous les effets d'un sort validé (débit ressource,
		/// cooldown, dégâts/soins/auras/menace) et émet les messages associés.
		void ResolveSpellCast(ConnectedClient& client, const SpellDef& spell, EntityId targetEntityId);

		/// SP-C — résout un cast validé par compétence de classe (chemin additif, jamais
		/// appelé si le fallback kit profil SP3 s'applique). Effets :
		///   Damage/SingleEnemy → DirectDamage × powerValue via ResolveAttackRoll/ApplyPlayerDamageToMob.
		///   Damage/AreaAroundSelf → même chose sur tous les mobs dans areaRadiusMeters.
		///   Heal/SingleAlly → soin direct × powerValue × damagePerHit (chemin DirectHeal SP3).
		///   Defense → aura DamageReductionPercent 8 s sur le casteur (percent clampé [5,50]).
		/// Débite la ressource, arme le cooldown, émet SendResourceUpdate et les CombatEvents.
		/// \param client         Casteur autoritaire (non mort, ressource vérifiée avant appel).
		/// \param cs             Définition immuable du skill (ptr non nul garanti par l'appelant).
		/// \param targetEntityId Entité résolue par HandleCastRequest (0 = AoE/soi).
		void ResolveClassSkillCast(ConnectedClient& client, const ClassSkillDef& cs, EntityId targetEntityId);

		/// Combat SP2/SP3 — applique des dégâts (déjà jetés/multipliés) d'un joueur
		/// sur un mob : PV, événement dynamique, mort/loot/XP, CombatEvent broadcast.
		/// Factorisé entre l'auto-attaque (HandleAttackRequest) et les sorts.
		void ApplyPlayerDamageToMob(ConnectedClient& attacker, MobEntity& target, uint32_t rolledDamage, uint32_t eventFlags);

		/// Combat SP3 — broadcast la liste complète des auras d'une entité aux
		/// clients intéressés (idempotent côté client).
		void BroadcastAuraUpdate(EntityId entityId, const std::vector<ActiveAura>& auras);

		/// Combat SP4 — pousse les tables de menace modifiées (≤ 1/s par mob).
		/// Appelée depuis Simulate.
		void PushThreatUpdates();

		/// Combat SP4 — broadcast immédiat de la table de menace d'un mob (liste
		/// vide = effacement côté client : mort, evade, reset).
		void BroadcastThreatUpdate(MobEntity& mob);

		/// Apply one mob attack against its current target when in range.
		bool TryMobAttackPlayer(MobEntity& mob, ConnectedClient& target);

		/// Add one item stack to the player's authoritative inventory.
		void AddItemToInventory(ConnectedClient& client, const ItemStack& item);

		/// Apply one authoritative quest event and emit any resulting deltas.
		void ApplyQuestEvent(ConnectedClient& client, QuestStepType eventType, std::string_view targetId, uint32_t amount, std::string_view reason);

		/// EXT-3 — post-traitement des deltas de quête d'un client après ApplyEvent :
		/// verse la récompense des quêtes auto-complétées (stamp completedAtEpochMs +
		/// GrantQuestReward), envoie chaque QuestDelta au client, puis persiste l'état.
		/// Partagé par l'acteur et les coéquipiers crédités (fan-out partyShared),
		/// pour éviter toute divergence de traitement entre les deux chemins.
		void FinalizeQuestDeltas(ConnectedClient& client, std::vector<QuestProgressDelta>& deltas, std::string_view reason);

		/// Send the current quest journal state after the client handshake succeeds.
		void SendQuestStateBootstrap(const ConnectedClient& receiver);

		/// SP1 quêtes — le joueur accepte une quête au PNJ giver (Offered -> Active).
		void HandleAcceptQuest(const Endpoint& endpoint, const QuestAcceptRequestMessage& msg);

		/// SP1 quêtes — le joueur rend une quête (ReadyToTurnIn -> Completed) : récompenses versées ici.
		void HandleTurnInQuest(const Endpoint& endpoint, const QuestTurnInRequestMessage& msg);

		/// EXT-2 quêtes — verse la récompense d'une quête terminée à \p client :
		/// XP (avec montées de niveau), or (wallet) et objets (inventaire), puis
		/// réplique les deltas inventaire/wallet. Partagé par le turn-in explicite
		/// (HandleTurnInQuest) et l'auto-complétion (ApplyQuestEvent). Ne modifie
		/// pas le statut de quête ni n'envoie de QuestDelta (à la charge du caller).
		void GrantQuestReward(ConnectedClient& client, const QuestDefinition& def);

		/// SP1 quêtes — envoie la liste des quêtes offertes/rendables d'un PNJ au client.
		void SendQuestGiverList(const ConnectedClient& receiver, std::string_view npcTargetId);

		/// SP1 quêtes — sentinelle « non trouvé » pour FindClientQuestStateIndex.
		static constexpr size_t kInvalidQuestIndex = static_cast<size_t>(-1);

		/// SP1 quêtes — retrouve l'index de l'état de quête \p questId dans
		/// \p client.questStates. Renvoie kInvalidQuestIndex si absent.
		static size_t FindClientQuestStateIndex(const ConnectedClient& client, std::string_view questId);

		/// SP1 quêtes — contrôle de portée joueur/PNJ pour accept/turn-in.
		/// V1 : renvoie toujours true — la portée est aujourd'hui garantie côté
		/// client (le Talk n'est émis qu'à proximité du PNJ) ; le Talk serveur
		/// actuel route uniquement par targetId, sans vérifier de distance.
		/// TODO SP-ultérieur : durcir côté serveur (distance réelle joueur/PNJ).
		bool IsClientNearNpc(const ConnectedClient& client, std::string_view npcTargetId) const;

		/// Send the current event state snapshot for the receiver's zone.
		void SendDynamicEventBootstrap(const ConnectedClient& receiver);

		/// Send one inventory delta after a successful pickup.
		bool SendInventoryDelta(const ConnectedClient& receiver, std::span<const ItemStack> items);

		/// M35.1 — replicate wallet balances to the owning client (CurrencyChanged UI).
		bool SendWalletUpdate(const ConnectedClient& receiver);

		/// M35.2 — open vendor shop for one connected client.
		bool SendShopOpen(const ConnectedClient& receiver, uint32_t vendorId);

		/// M35.2 — authoritative buy from vendor stock.
		void HandleShopBuyRequest(const Endpoint& endpoint, const ShopBuyRequestMessage& message);

		/// M35.2 — authoritative sell-back at 25% buy price.
		void HandleShopSellRequest(const Endpoint& endpoint, const ShopSellRequestMessage& message);

		/// M35.4 — auction browse snapshot to one client.
		bool SendAuctionBrowseResult(const ConnectedClient& receiver, const AuctionBrowseRequestMessage& query);

		void HandleAuctionBrowseRequest(const Endpoint& endpoint, const AuctionBrowseRequestMessage& message);
		void HandleAuctionListItemRequest(const Endpoint& endpoint, const AuctionListItemRequestMessage& message);
		void HandleAuctionBidRequest(const Endpoint& endpoint, const AuctionBidRequestMessage& message);
		void HandleAuctionBuyoutRequest(const Endpoint& endpoint, const AuctionBuyoutRequestMessage& message);

		/// Advance expired auctions and apply mailed / live wallet + inventory updates.
		void ProcessAuctionHouseTick();

		void ApplyAuctionSettlement(const AuctionSettlement& settlement);

		/// Refund gold to an online player or queue to persisted mailbox when offline.
		// Phase 3.7.5 — characterKey élargi à uint64 (alignement avec persistenceCharacterKey).
		void RefundGoldToCharacter(uint64_t characterKey, uint32_t amountGold, std::string_view reason);

		/// Append gold and/or one item stack to a character persistence file (offline delivery).
		bool DepositMailboxDelivery(uint64_t characterKey, uint32_t goldDelta, const ItemStack* itemOptional);

		ConnectedClient* FindConnectedClientByCharacterKey(uint64_t characterKey);

		/// Remove \p quantity of \p itemId from bags (merge stacks); fails if insufficient.
		bool RemoveStackFromInventory(ConnectedClient& client, uint32_t itemId, uint32_t quantity, std::string& outError);

		/// Send one quest delta after a successful quest state update.
		bool SendQuestDelta(const ConnectedClient& receiver, const QuestProgressDelta& delta);

		/// Send one dynamic event state update to one client.
		bool SendDynamicEventState(
			const ConnectedClient& receiver,
			const DynamicEventState& eventState,
			std::string_view notificationText,
			uint32_t rewardExperience,
			uint32_t rewardGold,
			std::span<const ItemStack> rewardItems);

		/// Broadcast one dynamic event state update to clients in the same zone.
		void BroadcastDynamicEventState(
			const DynamicEventState& eventState,
			std::string_view notificationText,
			uint32_t rewardedClientId,
			uint32_t rewardExperience,
			uint32_t rewardGold,
			std::span<const ItemStack> rewardItems);

		/// Refresh spawn/despawn replication for every connected client.
		void RefreshReplication();

		/// Refresh spawn/despawn replication for one connected client.
		void RefreshReplicationForClient(ConnectedClient& client);

		/// Build the list of relevant replicated entities for one client.
		void BuildRelevantEntityIds(const ConnectedClient& client, std::vector<EntityId>& outEntityIds) const;

		/// Convert one connected client to the minimal replicated entity state.
		EntityState BuildEntityState(const ConnectedClient& client) const;

		/// Convert one replicated mob to the minimal replicated entity state.
		EntityState BuildEntityState(const MobEntity& mob) const;

		/// Convert one replicated loot bag to the minimal replicated entity state.
		EntityState BuildEntityState(const LootBagEntity& lootBag) const;

		/// Fill a spawn payload for a replicated player or mob entity.
		bool TryBuildSpawnEntity(EntityId entityId, SpawnEntity& outEntity) const;

		/// Fill a snapshot payload for a replicated player or mob entity.
		bool TryBuildSnapshotEntity(EntityId entityId, SnapshotEntity& outEntity) const;

		/// Return true when the client should receive the combat event broadcast.
		bool ShouldBroadcastCombatEventToClient(const ConnectedClient& receiver, EntityId attackerEntityId, EntityId targetEntityId) const;

		/// Broadcast one authoritative combat event to interested clients.
		void BroadcastCombatEvent(const CombatEventMessage& message);

		/// Send a spawn packet for one relevant entity.
		bool SendSpawn(const ConnectedClient& receiver, EntityId subjectEntityId);

		/// Send a despawn packet for one relevant entity.
		bool SendDespawn(const ConnectedClient& receiver, EntityId entityId);

		/// Send a combat event packet to one interested client.
		bool SendCombatEvent(const ConnectedClient& receiver, const CombatEventMessage& message);

		/// Send one chat relay line to one client (M29.1).
		bool SendChatRelay(const ConnectedClient& receiver, const ChatRelayMessage& message);

		/// Send one emote relay event to one client (M29.3).
		bool SendEmoteRelay(const ConnectedClient& receiver, const EmoteRelayMessage& message);

		/// Send a zone change packet after a validated authoritative transition.
		bool SendZoneChange(const ConnectedClient& receiver, const ZoneChangeMessage& message);

		/// Send a welcome packet to the given connected client.
		bool SendWelcome(const ConnectedClient& client);

		/// R1-B — pousse les stats dérivées complètes (PLAYER_STATS) au client à
		/// l'enter-world. Recalcule depuis le moteur (faction/classe/sexe/niveau).
		/// Retourne false si tables absentes, char legacy (faction/classe vides) ou
		/// calcul/envoi échoué — non fatal pour le handshake (log seulement).
		bool SendPlayerStats(const ConnectedClient& client);
		/// PR-C — pousse la progression de niveau (niveau + XP courante + XP pour le
		/// niveau suivant) au client local pour la barre d'XP. Enter-world + gain d'XP.
		bool SendPlayerXpUpdate(const ConnectedClient& client);

		/// Send one empty snapshot packet to the given connected client.
		bool SendSnapshot(const ConnectedClient& client);

		/// Find a connected client by endpoint.
		ConnectedClient* FindClient(const Endpoint& endpoint);

		/// Find a connected client by endpoint.
		const ConnectedClient* FindClient(const Endpoint& endpoint) const;

		/// Find a connected client by replicated entity id.
		ConnectedClient* FindClientByEntityId(EntityId entityId);

		/// Find a connected client by replicated entity id.
		const ConnectedClient* FindClientByEntityId(EntityId entityId) const;

		/// TG.2 — reconstruit les 3 index O(1) sur m_clients depuis zéro. Appelé après une
		/// suppression dans m_clients (erase) qui décale les indices ; pas nécessaire après
		/// un push_back/emplace_back (les indices existants restent valides, on insère juste
		/// la nouvelle entrée). Coût O(N) où N = m_clients.size().
		void RebuildClientIndexes();

		/// Find a mob by replicated entity id.
		MobEntity* FindMobByEntityId(EntityId entityId);

		/// Find a mob by replicated entity id.
		const MobEntity* FindMobByEntityId(EntityId entityId) const;

		/// Find a loot bag by replicated entity id.
		LootBagEntity* FindLootBagByEntityId(EntityId entityId);

		/// Find a loot bag by replicated entity id.
		const LootBagEntity* FindLootBagByEntityId(EntityId entityId) const;

		/// Return the cell grid attached to the requested zone, creating it on first use.
		CellGrid* GetOrCreateZoneGrid(uint32_t zoneId);

		/// Initialize moderation audit log (M29.2).
		void InitModerationAuditSubsystem();

		/// Load banned character keys from content-relative ban list file.
		void LoadChatBanFile();

		/// Persist banned character keys to content-relative ban list file.
		void SaveChatBanFile();

		/// Send one system line to a single client (M29.2).
		void SendChatSystemNotice(ConnectedClient& receiver, std::string_view text);

		/// Broadcast a global moderation announcement (M29.2).
		void BroadcastModerationAnnouncement(std::string_view text);

		/// Handle one parsed slash command; returns true when handled (no normal chat relay).
		bool HandleChatSlashCommand(ConnectedClient& sender, const ParsedChatSlashCommand& command);

		/// Resolve `P<id>` style display token to a connected client, or nullptr.
		ConnectedClient* FindConnectedClientByChatDisplayName(std::string_view displayToken);

		/// Disconnect and persist one client (e.g. `/kick`).
		void DisconnectConnectedClient(uint32_t clientId, std::string_view persistenceReason);

		/// True when \p receiver has ignored \p senderDisplay (case-insensitive ASCII).
		bool IsChatSenderIgnoredBy(const ConnectedClient& receiver, std::string_view senderDisplay) const;

		/// Report line to audit file or main log fallback.
		void AuditLogChatReport(std::string_view reporterDisplay, std::string_view targetDisplay, std::string_view reason);

		/// Moderation action to audit file or main log fallback.
		void AuditLogModeration(std::string_view action, std::string_view actorDisplay, std::string_view targetDisplay, std::string_view detail);

		// ------------------------------------------------------------------
		// M32.1 — Friend system helpers
		// ------------------------------------------------------------------

		/// Set a player online in the friend presence system and send FriendListSync.
		/// Call on successful client handshake (HandleHello, new client only).
		void OnClientLogin(ConnectedClient& client);

		/// Set a player offline in the friend presence system and notify online friends.
		/// Call before removing the client from m_clients.
		void OnClientLogout(const ConnectedClient& client);

		// ------------------------------------------------------------------
		// M32.2 — Party system helpers
		// ------------------------------------------------------------------

		/// Handle /invite <name> slash command dispatched from HandleChatSlashCommand.
		bool HandleInviteCommand(ConnectedClient& sender, std::string_view argsRemainder);

		/// Handle /leave slash command dispatched from HandleChatSlashCommand.
		bool HandleLeaveCommand(ConnectedClient& sender);

		/// Handle /loot <mode> slash command dispatched from HandleChatSlashCommand (leader only).
		bool HandleLootCommand(ConnectedClient& sender, std::string_view argsRemainder);

		/// Handle /pkick <name> slash command dispatched from HandleChatSlashCommand (leader only).
		bool HandlePartyKickCommand(ConnectedClient& sender, std::string_view argsRemainder);

		/// Broadcast a full PartyUpdate packet to every member of \p party.
		void BroadcastPartyUpdate(const Party& party);

		/// Send a single PartyUpdate packet to one client.
		bool SendPartyUpdate(const ConnectedClient& receiver, const Party& party);

		/// Distribute \p baseXp evenly among party members in range of the kill position.
		/// Falls back to granting \p baseXp only to the attacker when not in a party.
		void DistributePartyXp(ConnectedClient& attacker,
		                       float killPosX,
		                       float killPosZ,
		                       uint32_t baseXp);

		/// Applique un gain d'XP à \p client en franchissant les seuils de niveau.
		/// Sans tables de stats chargées, accumule simplement l'XP (comportement legacy).
		/// À chaque level-up : recalcul des stats au nouveau niveau, soin complet,
		/// re-push de la feuille de stats au client et persistance du personnage.
		void ApplyLevelUpsAfterXp(ConnectedClient& client, uint32_t gainedXp);

		/// Apply the current party loot mode when spawning a loot bag for \p killerClient.
		/// Returns the entityId that should own the bag (0 = public/FFA).
		EntityId ResolvePartyLooterEntityId(const ConnectedClient& killerClient);

		/// Send FriendListSync packet to \p receiver containing all friends with presence status.
		void SendFriendListSync(const ConnectedClient& receiver);

		/// Broadcast a FriendStatusUpdate to all online friends of \p subject.
		void BroadcastFriendStatusUpdate(const ConnectedClient& subject, PresenceStatus status);

		/// Handle /friend sub-command dispatched from HandleChatSlashCommand.
		bool HandleFriendCommand(ConnectedClient& sender, std::string_view argsRemainder);

		// ------------------------------------------------------------------
		// M35.3 — Direct player-to-player trade helpers
		// ------------------------------------------------------------------

		/// Handle /trade <name> slash command.
		bool HandleTradeCommand(ConnectedClient& sender, std::string_view argsRemainder);

		/// Handle an incoming TradeAccept packet from a client.
		void HandleTradeAccept(const Endpoint& endpoint, const TradeAcceptMessage& message);

		/// Handle an incoming TradeDecline packet from a client.
		void HandleTradeDecline(const Endpoint& endpoint, const TradeDeclineMessage& message);

		/// Handle an incoming TradeAddItem packet from a client.
		void HandleTradeAddItem(const Endpoint& endpoint, const TradeAddItemMessage& message);

		/// Handle an incoming TradeSetGold packet from a client.
		void HandleTradeSetGold(const Endpoint& endpoint, const TradeSetGoldMessage& message);

		/// Handle an incoming TradeLock packet from a client.
		void HandleTradeLock(const Endpoint& endpoint, const TradeLockMessage& message);

		/// Handle an incoming TradeConfirm packet from a client.
		void HandleTradeConfirm(const Endpoint& endpoint, const TradeConfirmMessage& message);

		/// Handle an incoming TradeCancel packet from a client.
		void HandleTradeCancelPacket(const Endpoint& endpoint, const TradeCancelMessage& message);

		/// Send a TradeWindowUpdate snapshot to both sides of the active session.
		void BroadcastTradeWindowUpdate(uint32_t clientIdA, uint32_t clientIdB);

		/// Send a TradeComplete notification to both sides.
		void BroadcastTradeComplete(uint32_t clientIdA, uint32_t clientIdB);

		/// Send a TradeCancelled notification to both sides and cancel the session.
		void BroadcastTradeCancelled(uint32_t clientIdA, uint32_t clientIdB, std::string_view reason);

		/// Write one trade audit log line (player_trade_log equivalent, runtime only).
		void LogTradeAudit(
			uint32_t clientIdA,
			uint32_t clientIdB,
			const std::string& itemsA,
			const std::string& itemsB,
			uint32_t goldA,
			uint32_t goldB);

		// ------------------------------------------------------------------
		// M36.1 — Gathering / harvesting resource nodes helpers
		// ------------------------------------------------------------------

		/// Initialize the GatheringSystem from content JSON files.
		bool InitGathering();

		/// Handle a HarvestRequest packet from a client.
		void HandleHarvestRequest(const Endpoint& endpoint, const HarvestRequestMessage& message);

		/// Handle a HarvestCancelRequest packet from a client.
		void HandleHarvestCancelRequest(const Endpoint& endpoint, const HarvestCancelRequestMessage& message);

		/// Advance gathering sessions each server tick.
		void TickGathering();

		/// Send a HarvestCancelled packet to one client.
		bool SendHarvestCancelled(const ConnectedClient& receiver, EntityId nodeEntityId, HarvestCancelReason reason);

		// ------------------------------------------------------------------
		// M36.2 — Crafting / profession skill system helpers
		// ------------------------------------------------------------------

		/// Initialize the CraftingSystem from content JSON.
		bool InitCrafting();

		/// Handle an incoming LearnProfessionRequest packet.
		void HandleLearnProfessionRequest(const Endpoint& endpoint, const LearnProfessionRequestMessage& message);

		/// Handle an incoming CraftRecipeListRequest packet.
		void HandleCraftRecipeListRequest(const Endpoint& endpoint, const CraftRecipeListRequestMessage& message);

		/// Handle an incoming CraftRequest packet.
		void HandleCraftRequest(const Endpoint& endpoint, const CraftRequestMessage& message);

		/// Handle an incoming CraftCancelRequest packet.
		void HandleCraftCancelRequest(const Endpoint& endpoint, const CraftCancelRequestMessage& message);

		/// Advance crafting sessions each server tick.
		void TickCrafting();

		/// Send a ProfessionUpdate snapshot to one client.
		bool SendProfessionUpdate(const ConnectedClient& receiver);

		/// Build ProfessionEntry list from PersistedCharacterState and push to ConnectedClient.
		void ApplyPersistedProfessions(ConnectedClient& client, const PersistedCharacterState& state);

		engine::core::Config m_config;
		CharacterPersistenceStore m_characterPersistence;
		/// R1-A — tables de stats du personnage (JSON embarqué), chargées une seule fois
		/// (lazy, au 1er HandleHello) via CharacterStatsTables::FromEmbedded. nullopt si le
		/// JSON est invalide → les PV par défaut sont conservés (pas de régression).
		std::optional<engine::server::gameplay::CharacterStatsTables> m_statsTables;
		/// R1-A — passe à true une fois la tentative de chargement de m_statsTables effectuée
		/// (évite de réessayer + de logguer l'avertissement à chaque Hello si elle échoue).
		bool m_statsTablesLoadAttempted = false;
		AuctionHouseService m_auctionHouse;
		/// M35.1 — currency definitions + validation caps (config/currencies.json).
		CurrencyConfig m_currencyConfig{};
		PlayerWalletService m_playerWallet{m_currencyConfig};
		/// M35.2 — vendor definitions (`config/vendors.json`) + finite stock book.
		VendorCatalog m_vendorCatalog{};
		VendorStockBook m_vendorStock{};
		/// Chantier 2 SP-A — catalogue d'objets (game/data/items/items.json).
		/// SOURCE AUTORITAIRE : slot + bonus d'un objet équipé sont lus ici, jamais
		/// fournis par le client. Chargé une fois à l'Init (m_itemCatalogLoaded).
		engine::items::ItemCatalog m_itemCatalog{};
		bool m_itemCatalogLoaded = false;
		EventRuntime m_eventRuntime;
		QuestRuntime m_questRuntime;
		SpawnerRuntime m_spawnerRuntime;
		/// Combat SP1 — catalogue d'archétypes de créatures (stats data-driven,
		/// initialisé par InitSpawners avant le chargement des spawners).
		CreatureArchetypeLibrary m_archetypeLibrary;
		/// Combat SP3 — kits de sorts par profil (gameplay/spells/*.json), strict.
		SpellKitLibrary m_spellKits;
		/// SP-B — bibliothèque des compétences par-classe (gameplay/class_skills/*.json), stricte.
		ClassSkillLibrary m_classSkills;
		/// Combat SP2 — RNG des jets d'attaque (précision/critique), seedé au
		/// constructeur. Main thread du tick monde uniquement (pas de verrou).
		std::mt19937 m_combatRng;
		ZoneTransitionMap m_zoneTransitionMap;
		TickScheduler m_tickScheduler;
		UdpTransport m_transport;
		std::vector<Datagram> m_pendingDatagrams;
		std::vector<ConnectedClient> m_clients;
		/// Présence unifiée (Niveau 1) : autorité UNIQUE de la présence shard-locale.
		/// Alimentée aux hooks login/logout/zone ; consommée par FriendSystem (par
		/// character_id) et, via GetPlayerPresenceSnapshot, par le heartbeat → master.
		/// Thread-safe en interne (découple thread gameplay / thread shard→master).
		ShardPresenceService m_shardPresence;
		/// TG.2 : index O(1) sur m_clients pour les 3 lookups chauds (Find/FindByEntityId/
		/// FindConnectedClient). Valeurs = index dans m_clients ; clé endpoint = (address<<16)|port.
		/// Maintenance : insertion incrémentale dans HandleHello, reconstruction complète dans
		/// DisconnectConnectedClient (rare). Wipe dans Init/Shutdown en même temps que m_clients.
		std::unordered_map<uint64_t, size_t> m_clientIndexByPackedEndpoint;
		std::unordered_map<uint32_t, size_t> m_clientIndexByClientId;
		std::unordered_map<EntityId, size_t> m_clientIndexByEntityId;
		std::vector<MobEntity> m_mobs;
		std::vector<LootBagEntity> m_lootBags;
		std::vector<LootTableEntry> m_lootTableEntries;

		/// Validation v12 (wire v13) — un point de réapparition typé d'une zone.
		struct RespawnPointDefinition
		{
			uint32_t zoneId = 0;
			uint8_t destinationType = 0; ///< kRespawnDestination* (0 cimetière, 1 auberge).
			float positionMetersX = 0.0f;
			float positionMetersY = 0.0f;
			float positionMetersZ = 0.0f;
			std::string ownerFactionId;       ///< Faction propriétaire du cimetière (vide/"-" = neutre partout). Ignoré pour les auberges.
			float       neutralRadiusM = 0.0f; ///< Rayon (m) dans lequel le cimetière est neutre (toutes factions). Défaut = globals.graveyard_default_faction_neutral_radius_m.
		};
		std::vector<RespawnPointDefinition> m_respawnPoints;
		std::vector<DynamicEventState> m_dynamicEvents;
		std::vector<SpawnerRuntimeState> m_spawners;
		std::unordered_map<uint32_t, CellGrid> m_zoneGrids;
		std::vector<EntityId> m_relevantEntityScratch;
		std::vector<SnapshotEntity> m_snapshotEntitiesScratch;
		EntityId m_nextServerEntityId = 0x200000000ull;
		uint16_t m_listenPort = 0;
		uint16_t m_tickHz = 0;
		uint16_t m_snapshotHz = 0;
		uint32_t m_nextClientId = 1;
		uint32_t m_currentTick = 0;
		uint32_t m_characterAutosaveIntervalTicks = 0;
		uint32_t m_nextCharacterAutosaveTick = 0;
		uint32_t m_snapshotAccumulator = 0;
		/// TG.4 — cap top-N par distance des entités relayées par client (`BuildRelevantEntityIds`).
		/// 0 (défaut) = pas de cap (comportement historique). Lu au boot depuis la clé
		/// `server.replication.view_cap_entities` du config.json — opt-in pour éviter de couper
		/// silencieusement la visibilité sur les déploiements existants.
		uint32_t m_viewCapEntities = 0;
		bool m_initialized = false;
		bool m_stopRequested = false;

		// ------------------------------------------------------------------
		// TG.3 — split du thread receive (opt-in via `server.network.split_receive_thread`,
		// défaut false ⇒ comportement historique mono-thread préservé). Quand activé, un
		// thread dédié appelle `UdpTransport::Receive` en continu et empile les datagrammes
		// dans `m_networkIngressQueue` ; le thread tick (Run) draine la queue dans
		// `ProcessIncomingPackets`. Décorrèle la jitter réseau de la jitter gameplay.
		// ------------------------------------------------------------------
		bool m_splitReceiveThread = false;
		std::thread m_networkThread;
		std::mutex m_networkQueueMutex;
		/// Buffer rempli par le thread réseau et drainé (swap) par le thread tick à chaque
		/// `ProcessIncomingPackets`. Vide quand le mode n'est pas actif.
		std::vector<Datagram> m_networkIngressQueue;
		/// Signal d'arrêt vu par `NetworkPumpLoop` ; positionné dans `RequestStop`/`Shutdown`.
		std::atomic<bool> m_networkThreadStopRequested{false};

		/// TA.3 : gate de session UDP (optionnel, possédé par l'appelant). Voir
		/// SetAdmittedCharacterRegistry.
		AdmittedCharacterRegistry* m_admittedRegistry = nullptr;

		/// TA.3 — race condition Hello/Admit : le Hello UDP du client peut arriver au shard
		/// AVANT que le push d'admission TCP du master n'ait été traité (master émet le push
		/// puis répond au client ; UDP client→shard est souvent plus court que TCP master→shard
		/// par bufferisation). Sans buffer, le Hello est rejeté et le client ne réessaie pas.
		/// On garde donc en attente, pour un court TTL, les Hellos rejetés pour cause
		/// « not admitted » ; à chaque tick on les retraite si l'admission est devenue valide.
		/// Réécrit uniquement depuis le thread gameplay (HandleHello + DrainPendingHellos),
		/// pas besoin de mutex (le registry sous-jacent est thread-safe).
		struct PendingHello
		{
			Endpoint endpoint{};
			uint64_t characterKey = 0;
			uint64_t arrivedAtMs = 0;
		};
		std::vector<PendingHello> m_pendingHellos;
		/// TTL ms d'un Hello en attente. Au-delà, on abandonne (l'admission n'est pas arrivée).
		/// 5 s suffisent largement vs la latence master→shard (typiquement < 100 ms).
		static constexpr uint64_t kPendingHelloTtlMs = 5000u;

		/// TA.3 — délai d'inactivité au-delà duquel un client UDP est considéré déconnecté.
		/// Choisi a 30 s : largement plus que la cadence Input (50 ms a 20 Hz) tout en restant
		/// court pour eviter des nameplates fantomes longtemps apres une deco brutale.
		static constexpr uint64_t kUdpClientIdleTimeoutMs = 30000u;
		/// Dernier tick ou EvictIdleUdpClients a tourne (throttling 1 Hz au lieu de 20 Hz).
		uint32_t m_lastEvictIdleClientsTick = 0;

		/// TA.3c : anti-triche mouvement (speed/teleport) sur les positions reçues en
		/// Input. Détecteur header-only seedé à la config V1 par défaut (7.5 m/s * 1.5,
		/// saut max 50 m). Suit les vrais joueurs sur le thread gameplay.
		anticheat::AntiCheatGameplay m_antiCheat;
		uint64_t m_antiCheatViolations = 0;

		/// TA.4 : pool MySQL pour le pont position (optionnel, possédé par l'appelant). Voir
		/// SetCharacterDbPool / LoadSpawnFromDb.
		db::ConnectionPool* m_characterDbPool = nullptr;

		/// Per-player chat send rate limiting (M29.1).
		engine::net::ChatRateLimiter m_chatRateLimiter;

		/// M29.2: moderation audit file (chat reports + admin actions).
		SecurityAuditLog m_moderationAuditLog;
		bool m_moderationAuditLogReady = false;
		/// M29.2: character keys rejected at handshake after `/ban`.
		/// Phase 3.7.5 — élargi à uint64 (character_id complet).
		std::unordered_set<uint64_t> m_bannedCharacterKeys;

		/// M32.1: friend system (presence tracking + DB-backed requests when ENGINE_HAS_MYSQL).
		FriendSystem m_friendSystem;

		/// M32.2: party system (formation, loot modes, XP sharing).
		PartySystem m_partySystem;

		/// M35.3: direct player-to-player trade sessions + request queue.
		TradeSystem m_tradeSystem;

		/// M36.1: resource node spawner + harvest session manager.
		GatheringSystem m_gatheringSystem;

		/// M36.2: crafting recipes + profession skill manager.
		CraftingSystem m_craftingSystem;
	};
}
