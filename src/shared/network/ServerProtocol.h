#pragma once

#include "src/shared/network/ReplicationTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <span>
#include <vector>

namespace engine::server
{
	/// Protocol version accepted by the server skeleton.
	/// Phase 3.7.5 — bump 1 → 2 : `HelloMessage::clientNonce` est passé de \c uint32 à \c uint64
	/// pour transporter un \c character_id complet (BIGINT UNSIGNED) sans tronquer.
	/// TC.1 — bump 2 → 3 : `InputMessage` gagne `positionMetersY` + `yawRadians` (le client
	/// envoie désormais son altitude et son orientation, plus seulement X/Z).
	/// TD.4 — bump 3 → 4 : `SnapshotEntity` gagne `playerClientId` (uint32) — sert au client
	/// à afficher une plaque de nom "P<clientId>" au-dessus des avatars distants. Vaut 0 pour
	/// les mobs / loot bags.
	/// TG.1 — bump 4 → 5 : `SnapshotMessage` gagne `chunkIndex` + `chunkCount` (uint16 × 2)
	/// pour autoriser le chunking d'un snapshot AoI dépassant le budget MTU UDP (~1200 o).
	/// `chunkCount = 1` (mono-paquet) reste le cas dominant ; `chunkCount > 1` exige une
	/// réassemblage côté client (cf. UIModelBinding::ApplySnapshot).
	/// TD.5/TD.6 — bump 5 → 6 → 7 : `SnapshotEntity` gagne `characterName` puis `gender`
	/// (chaînes préfixées u16) pour la plaque de nom et la sélection du mesh distant.
	/// TD.8 — bump 7 → 8 : `InputMessage` gagne `animationState` (1 octet, client→shard) et
	/// `SnapshotEntity` gagne `animationState` (1 octet, shard→clients) — propage l'état
	/// d'animation (emote/roulade/run/sprint/saut/…) pour que les autres joueurs voient
	/// les bonnes animations au lieu d'un Idle/Walk dérivé de la vélocité.
	/// Wire-breaking : client + shard doivent se déployer ensemble.
	/// Présence enrichie (heartbeat) — PAS de bump : `SHARD_HEARTBEAT` (shard→master)
	/// gagne un tableau OPTIONNEL de joueurs `{accountId, characterId, level, zoneId}`
	/// en QUEUE de payload. C'est rétro-compatible : un master legacy lit les 16 octets
	/// fixes et ignore la queue ; un master neuf lit la queue si présente ; un heartbeat
	/// legacy (sans queue) reste valide. Comme `kProtocolVersion` borne aussi le framing
	/// client↔master, le laisser à 8 évitait de casser les clients v8 déjà distribués
	/// (le bump 8→9 initial, PR #770, était inutile et a été annulé ici).
	/// Combat SP1 — bump 8 → 9 : `SnapshotEntity` gagne `archetypeId` (uint32,
	/// shard→clients) — 0 = joueur/loot bag, ≠ 0 = mob ; le client résout
	/// nom/niveau/mesh/échelle dans son CreatureCatalog pour rendre la créature.
	/// Wire-breaking : ce bump invalide AUSSI le framing client↔master (version
	/// partagée) → déployer master + shardd + client en lock-step.
	/// Combat SP2 — bump 9 → 10 : `CombatEventMessage` gagne `flags` (uint32 :
	/// bit0 critique, bit1 raté ; payload 32 → 36 o) et nouveau kind
	/// `RespawnRequest` (80, client→shard, réapparition d'un joueur mort).
	/// Wire-breaking : même contrainte lock-step master + shardd + client.
	/// Combat SP3 — bump 10 → 11 : sorts et auras. Nouveaux kinds `CastRequest`
	/// (81), `ResourceUpdate` (82), `CastBarUpdate` (83), `AuraUpdate` (84) ;
	/// `PlayerStatsMessage` gagne `profileId` (chaîne préfixée u16 en queue —
	/// le client résout son kit de sorts gameplay/spells/<profil>.json).
	/// Wire-breaking : lock-step master + shardd + client.
	/// Combat SP4 — bump 11 → 12 : nouveau kind `ThreatUpdate` (85, shard →
	/// clients intéressés) — table de menace d'un mob répliquée (threat meter).
	/// Wire-breaking : lock-step master + shardd + client.
	/// Validation v12 — bump 12 → 13 : `RespawnRequestMessage` gagne
	/// `destination` (1 octet : cimetière/auberge le plus proche, payload
	/// 4 → 5). Les kinds ForcePosition (86) et LootNotify (87) ajoutés pendant
	/// la fenêtre v12 restaient rétro-additifs ; ce changement-ci modifie un
	/// payload existant → wire-breaking : lock-step master + shardd + client.
	/// Chantier 2 SP-A — bump 14 → 15 : opcodes équipement EquipRequest (96),
	/// UnequipRequest (97), EquipmentUpdate (98). Nouveaux opcodes client→serveur
	/// (equip/unequip) : un vieux serveur les ignorerait → bump pour rejeter les
	/// paires client/serveur incompatibles. Lock-step master + shardd + client.
	inline constexpr uint16_t kProtocolVersion = 15;

	/// Message kinds exchanged by the server skeleton.
	enum class MessageKind : uint16_t
	{
		Hello = 1,
		Welcome = 2,
		Input = 3,
		Snapshot = 4,
		Spawn = 5,
		Despawn = 6,
		ZoneChange = 7,
		AttackRequest = 8,
		CombatEvent = 9,
		PickupRequest = 10,
		InventoryDelta = 11,
		TalkRequest = 12,
		QuestDelta = 13,
		EventState = 14,
		/// Client-authored chat line routed by the authoritative server (M29.1).
		ChatSend = 15,
		/// Server relay of one chat line to interested clients (M29.1).
		ChatRelay = 16,
		/// Server relay of one emote gesture to nearby clients (M29.3).
		EmoteRelay = 17,
		/// Client requests to add a friend by name (M32.1).
		FriendRequest = 18,
		/// Server notifies target player of an incoming friend request (M32.1).
		FriendRequestNotify = 19,
		/// Client accepts a pending friend request by requester name (M32.1).
		FriendAccept = 20,
		/// Client declines a pending friend request by requester name (M32.1).
		FriendDecline = 21,
		/// Client removes an accepted friend by name (M32.1).
		FriendRemove = 22,
		/// Server sends the full friends list to a client on login (M32.1).
		FriendListSync = 23,
		/// Server notifies a client of a friend's presence status change (M32.1).
		FriendStatusUpdate = 24,

		// M32.2 — Party system messages ----------------------------------------

		/// Client sends /invite <name> to the server (M32.2).
		PartyInvite = 25,
		/// Server pushes an incoming invite notification to the target client (M32.2).
		PartyInviteNotify = 26,
		/// Invitee accepts the pending invite (M32.2).
		PartyAccept = 27,
		/// Invitee declines the pending invite (M32.2).
		PartyDecline = 28,
		/// Leader kicks a member from the party via /pkick (M32.2).
		PartyKick = 29,
		/// Server broadcasts full party state to all members after any change (M32.2).
		PartyUpdate = 30,
		/// Leader changes the party loot mode via /loot (M32.2).
		PartyLootMode = 31,
		/// Client voluntarily leaves their party via /leave (M32.2).
		PartyLeave = 32,

		// M32.3 — Guild system messages ----------------------------------------

		/// Client sends /guild create <name> to the server (M32.3).
		GuildCreate = 33,
		/// Server result of a guild creation attempt (M32.3).
		GuildCreateResult = 34,
		/// Client sends /ginvite <name> to the server (M32.3).
		GuildInvite = 35,
		/// Server pushes an incoming guild invite notification to the target (M32.3).
		GuildInviteNotify = 36,
		/// Target accepts a pending guild invite (M32.3).
		GuildInviteAccept = 37,
		/// Target declines a pending guild invite (M32.3).
		GuildInviteDecline = 38,
		/// Leader kicks a member from the guild via /gkick (M32.3).
		GuildKick = 39,
		/// Leader/Officer promotes a member via /gpromote (M32.3).
		GuildPromote = 40,
		/// Server broadcasts full guild roster to all members after any change (M32.3).
		GuildRosterSync = 41,
		/// Client or Officer updates the guild MOTD (M32.3).
		GuildMotdUpdate = 42,

		// M35.1 — Multi-currency wallet ----------------------------------------

		/// Server pushes current wallet balances to the owning client.
		WalletUpdate = 43,

		// M35.2 — Vendor shop ---------------------------------------------------

		/// Server opens the shop panel for one client.
		ShopOpen = 44,
		/// Client buys items from the active vendor.
		ShopBuyRequest = 45,
		/// Client sells items back to the vendor (25% buy price server-side).
		ShopSellRequest = 46,

		// M35.4 — Auction house ------------------------------------------------

		/// Client queries listings (filters + sort).
		AuctionBrowseRequest = 47,
		/// Server returns a browse snapshot for one client.
		AuctionBrowseResult = 48,
		/// Client posts item stack with start bid, optional buyout, duration (12/24/48 h).
		AuctionListItemRequest = 49,
		AuctionBidRequest = 50,
		AuctionBuyoutRequest = 51,

		// M35.3 — Direct player-to-player trade --------------------------------

		/// Client → server: /trade <name> initiates a trade request (M35.3).
		TradeRequest = 52,
		/// Server → target: incoming trade request notification (M35.3).
		TradeRequestNotify = 53,
		/// Client → server: accept a pending incoming trade request (M35.3).
		TradeAccept = 54,
		/// Client → server: decline a pending incoming trade request (M35.3).
		TradeDecline = 55,
		/// Client → server: add one item stack to own trade slot (M35.3).
		TradeAddItem = 56,
		/// Client → server: set offered gold amount (replaces previous value) (M35.3).
		TradeSetGold = 57,
		/// Client → server: lock own side — enter review phase (M35.3).
		TradeLock = 58,
		/// Client → server: final confirm after both sides locked (M35.3).
		TradeConfirm = 59,
		/// Client → server: cancel the ongoing trade session (M35.3).
		TradeCancel = 60,
		/// Server → both clients: current trade window state snapshot (M35.3).
		TradeWindowUpdate = 61,
		/// Server → both clients: trade completed successfully (M35.3).
		TradeComplete = 62,
		/// Server → both clients: trade was cancelled (M35.3).
		TradeCancelled = 63,

		// M36.1 — Gathering / harvesting resource nodes ------------------

		/// Client → server: request to start harvesting a resource node (M36.1).
		HarvestRequest = 64,
		/// Server → client: harvest cast bar started (M36.1).
		HarvestStart = 65,
		/// Server → client: harvest completed, items granted (M36.1).
		HarvestComplete = 66,
		/// Client → server: player explicitly cancels an ongoing harvest (M36.1).
		HarvestCancelRequest = 67,
		/// Server → client: harvest was cancelled (movement, combat, server-forced) (M36.1).
		HarvestCancelled = 68,

		// M36.2 — Crafting / profession skill system --------------------

		/// Client → server: request to learn a profession (M36.2).
		LearnProfessionRequest = 69,
		/// Server → client: full profession snapshot for the owning client (M36.2).
		ProfessionUpdate = 70,
		/// Client → server: request the recipe list for a profession (M36.2).
		CraftRecipeListRequest = 71,
		/// Server → client: list of recipes the player can see (M36.2).
		CraftRecipeListResult = 72,
		/// Client → server: start crafting a recipe (M36.2).
		CraftRequest = 73,
		/// Server → client: crafting cast bar started (M36.2).
		CraftStart = 74,
		/// Server → client: crafting completed, output items granted (M36.2).
		CraftComplete = 75,
		/// Client → server: cancel the current crafting session (M36.2).
		CraftCancelRequest = 76,
		/// Server → client: crafting session was cancelled (M36.2).
		CraftCancelled = 77,
		/// Client → server: départ propre du joueur (fermeture / retour menu). Permet au
		/// shard d'évincer immédiatement l'entité au lieu d'attendre le timeout d'inactivité
		/// (sinon l'avatar du joueur parti reste un « fantôme » visible des autres). Ajout
		/// rétro-compatible : un shard qui ne connaît pas cet opcode l'ignore (fallback timeout).
		Goodbye = 78,
		/// Server → client: stats dérivées complètes du joueur local (R1-B). Poussé à
		/// l'enter-world. Ajout rétro-additif : un vieux client qui ne connaît pas cet
		/// opcode l'ignore (pas de bump de `kProtocolVersion`).
		PlayerStats = 79,
		/// Combat SP2 — client → shard : réapparition d'un joueur mort (téléport au
		/// spawn mémorisé à l'admission, PV pleins, flag dead retiré). Refusé si le
		/// joueur n'est pas mort. Livré avec le bump v9→v10.
		RespawnRequest = 80,

		// Combat SP3 — sorts et auras (bump v10→v11) ------------------------

		/// Client → shard : cast d'un sort du kit du profil (spellId + cible).
		CastRequest = 81,
		/// Shard → casteur : ressource secondaire courante/max (régén, coûts).
		ResourceUpdate = 82,
		/// Shard → casteur : barre de cast (début / fin / annulation).
		CastBarUpdate = 83,
		/// Shard → clients intéressés : liste complète des auras d'une entité
		/// (idempotent — remplace l'état précédent côté client).
		AuraUpdate = 84,
		/// Combat SP4 — shard → clients intéressés : table de menace d'un mob
		/// (liste complète idempotente, vide = effacement). Alimente le threat
		/// meter (AdvancedCombatUi). Bump v11→v12.
		ThreatUpdate = 85,
		/// Correction SP1 — shard → client : position IMPOSÉE par le serveur
		/// (respawn, rejet anti-triche, téléport). Le mouvement reste
		/// client-autoritaire (T0.1) ; ce kind est le canal de correction.
		/// Ajout rétro-additif : un client qui ne le connaît pas l'ignore
		/// (pas de bump de kProtocolVersion).
		ForcePosition = 86,
		/// Validation v12 — shard → client : butin auto-crédité à la mort d'un
		/// mob (liste des objets gagnés). Le client ouvre/abonde la fenêtre de
		/// butin ; l'état d'inventaire transite séparément (InventoryDelta).
		/// Ajout rétro-additif (pas de bump).
		LootNotify = 87,
		/// Grimoire — client → shard : réassignation des 10 slots de la barre
		/// d'action (slot i → spellId, "" = vide). Validé contre le kit du profil.
		/// Ajout rétro-additif (pas de bump de kProtocolVersion).
		SetActionBarLayout = 88,
		/// Grimoire — shard → client : layout autoritaire des 10 slots, poussé à
		/// l'enter-world ET en réponse à un SetActionBarLayout (invalide = layout
		/// inchangé renvoyé). Ajout rétro-additif (pas de bump).
		ActionBarLayoutUpdate = 89,
		/// SP-B — shard → client : état autoritaire de progression (classId + skills connus),
		/// poussé à l'enter-world et après chaque choix. Rétro-additif.
		ClassProgressionUpdate = 90,
		/// SP-B — client → shard : le joueur choisit 1 skill (parmi 3) à un niveau donné.
		ChooseClassSkillRequest = 91,

		// SP1 quêtes — cycle de vie complet (giver-list, accept, turn-in) — bump v13->v14.

		/// SP1 quêtes — liste des quêtes offertes/rendables d'un PNJ (serveur→client).
		QuestGiverList = 92,
		/// SP1 quêtes — le joueur accepte une quête au PNJ giver (client→serveur).
		QuestAcceptRequest = 93,
		/// SP1 quêtes — le joueur rend une quête au PNJ turn-in (client→serveur).
		QuestTurnInRequest = 94,

		/// PR-C — progression de niveau du joueur local (serveur→client) : niveau,
		/// XP dans le niveau courant, XP requise pour le suivant. Poussé à l'enter-world
		/// ET à chaque gain d'XP (level-up ou non). Rétro-additif (vieux clients
		/// l'ignorent) : PAS de bump de kProtocolVersion.
		PlayerXpUpdate = 95,

		/// Chantier 2 SP-A — équipement d'objets. Wire-breaking (bump 14→15).
		/// EquipRequest (client→serveur) : équiper l'objet `itemId` depuis le sac ;
		/// le SERVEUR détermine le slot depuis SON catalogue (anti-triche).
		EquipRequest = 96,
		/// UnequipRequest (client→serveur) : retirer l'objet du slot `slot` (renvoyé
		/// au sac).
		UnequipRequest = 97,
		/// EquipmentUpdate (serveur→client) : snapshot complet de l'équipement porté
		/// (liste des slots occupés → itemId). Idempotent, comme InventoryDelta.
		EquipmentUpdate = 98,
		// Roadmap-3 (2026-07-19) — Ceinture : barre d'objets ACTIFS (4 slots,
		// jetons "item:<id>" : gâteaux, potions, nourriture). Kinds ADDITIFS
		// (pas de bump kProtocolVersion) sur le modèle 88/89.
		SetBeltLayout = 99,     ///< Client → Shard : pose le layout ceinture.
		BeltLayoutUpdate = 100  ///< Shard → Client : layout autoritaire (enter-world / ACK).
	};

	/// Initial client handshake sent before any other message.
	/// Phase 3.7.5 — `clientNonce` élargi à \c uint64 pour porter le `character_id` (BIGINT
	/// UNSIGNED) tel quel. Le shard interprète cette valeur comme `tentativeCharacterKey`
	/// (cf. ServerApp::HandleHello). Auparavant tronqué côté client à `& 0xFFFFFFFF`.
	struct HelloMessage
	{
		uint16_t requestedTickHz = 0;
		uint16_t requestedSnapshotHz = 0;
		uint64_t clientNonce = 0;
	};

	/// Minimal input envelope accepted by the server authoritative loop.
	struct InputMessage
	{
		uint32_t clientId = 0;
		uint32_t inputSequence = 0;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f; ///< TC.1 : altitude (terrain accidenté).
		float positionMetersZ = 0.0f;
		float yawRadians = 0.0f;      ///< TC.1 : orientation envoyée par le client.
		/// TD.8 : état d'animation courant de l'avatar local (valeur d'AvatarAnimState).
		/// Le shard le stocke et le réémet dans le SnapshotEntity pour que les autres
		/// joueurs voient les emotes/roulades/etc. Payload Input 24 → 25 octets (v7→v8).
		uint8_t animationState = 0;
	};

	/// Handshake acknowledgement emitted after a client is accepted.
	struct WelcomeMessage
	{
		uint32_t clientId = 0;
		uint16_t tickHz = 0;
		uint16_t snapshotHz = 0;
	};

	/// Départ propre du joueur (client → shard). Permet l'éviction immédiate de l'entité.
	struct GoodbyeMessage
	{
		uint32_t clientId = 0;
	};

	/// Stats dérivées complètes du joueur local (R1-B). Poussé à l'enter-world.
	/// Type de message rétro-additif (vieux clients l'ignorent ; pas de bump de version).
	struct PlayerStatsMessage
	{
		uint32_t clientId = 0;
		uint32_t maxHealth = 0;
		uint32_t resource = 0;   ///< ressource secondaire max
		uint32_t stamina = 0;    ///< endurance max
		uint32_t damage = 0;
		float    accuracy = 0.0f;
		float    range = 0.0f;
		float    critRate = 0.0f;
		float    critMult = 0.0f;
		float    speedWalk = 0.0f;
		float    speedRun = 0.0f;
		float    speedSprint = 0.0f;
		float    perception = 0.0f;
		float    stealth = 0.0f;
		std::string resourceKey; ///< ex. "ferveur" (libellé résolu côté client)
		/// Combat SP3 (wire v11) — profil de classe ("melee", "tank", …) ; le
		/// client résout son kit de sorts gameplay/spells/<profil>.json. Vide =
		/// perso legacy sans faction/classe → pas de barre d'action.
		std::string profileId;
	};

	/// Snapshot envelope carrying timing, connection stats and entity state count.
	/// TG.1 — `chunkIndex` / `chunkCount` permettent au serveur de découper un snapshot AoI
	/// trop gros pour un seul datagramme (~1200 o MTU). Valeurs par défaut 0/1 = snapshot
	/// mono-paquet (cas dominant). `chunkCount > 1` : le client doit accumuler les
	/// `chunkCount` chunks (même `serverTick`) avant de commiter le snapshot global ;
	/// `entityCount` ne référence alors que les entités de CE chunk.
	struct SnapshotMessage
	{
		uint32_t clientId = 0;
		uint32_t serverTick = 0;
		uint16_t connectedClients = 0;
		uint16_t entityCount = 0;
		uint32_t receivedPackets = 0;
		uint32_t sentPackets = 0;
		uint16_t chunkIndex = 0;
		uint16_t chunkCount = 1;
	};

	/// Server-authoritative zone assignment sent when a transition volume is validated.
	struct ZoneChangeMessage
	{
		uint32_t zoneId = 0;
		float spawnPositionX = 0.0f;
		float spawnPositionY = 0.0f;
		float spawnPositionZ = 0.0f;
	};

	/// Client request asking the authoritative server to attack one target entity.
	struct AttackRequestMessage
	{
		uint32_t clientId = 0;
		EntityId targetEntityId = 0;
	};

	/// Combat SP2 — bits du champ `CombatEventMessage::flags` (wire v10).
	inline constexpr uint32_t kCombatEventFlagCrit = 1u << 0;
	inline constexpr uint32_t kCombatEventFlagMiss = 1u << 1;
	/// Validation v12 — événement synthétique de RÉSURRECTION (respawn) : porte
	/// les PV pleins et les stateFlags nettoyés du ressuscité. Les snapshots
	/// excluant l'entité du joueur lui-même, c'est le seul canal qui rafraîchit
	/// ses PV/flags — sans cet événement l'écran de mort ne se fermait jamais.
	/// Le client met à jour les stats mais N'ÉCRIT PAS de ligne de log combat.
	inline constexpr uint32_t kCombatEventFlagResurrection = 1u << 2;

	/// Authoritative combat result broadcast to interested clients.
	/// Combat SP2 (wire v10) — `flags` porte critique/raté (cf. kCombatEventFlag*) ;
	/// un raté a `damage == 0` et des PV cible inchangés.
	struct CombatEventMessage
	{
		EntityId attackerEntityId = 0;
		EntityId targetEntityId = 0;
		uint32_t damage = 0;
		uint32_t targetCurrentHealth = 0;
		uint32_t targetMaxHealth = 0;
		uint32_t targetStateFlags = 0;
		uint32_t flags = 0;
	};

	/// Validation v12 (wire v13) — destinations de réapparition proposées par
	/// l'écran de mort. Le serveur choisit le point du type demandé LE PLUS
	/// PROCHE du lieu de mort (repli : point d'entrée en monde si la zone n'en
	/// définit aucun — cf. game/data/respawn/respawn_points.txt).
	inline constexpr uint8_t kRespawnDestinationGraveyard = 0;
	inline constexpr uint8_t kRespawnDestinationInn = 1;

	/// Combat SP2 — demande de réapparition d'un joueur mort (client → shard).
	/// Le serveur ne l'honore que si le joueur porte kEntityStateDead.
	/// Wire v13 : + destination (1 octet, kRespawnDestination*) — payload 4 → 5.
	struct RespawnRequestMessage
	{
		uint32_t clientId = 0;
		uint8_t destination = kRespawnDestinationGraveyard;
	};

	// Combat SP3 — sorts et auras (wire v11) --------------------------------

	/// Client → shard : cast d'un sort du kit du profil du joueur.
	/// targetEntityId = 0 pour les sorts sans cible (SelfOnly / AreaAroundSelf) ;
	/// pour SingleAlly, 0 = soi.
	struct CastRequestMessage
	{
		uint32_t clientId = 0;
		EntityId targetEntityId = 0;
		std::string spellId;
	};

	/// Grimoire — slots de barre d'action (10 entrées, slot i → spellId, "" = vide).
	struct SetActionBarLayoutMessage
	{
		uint32_t clientId = 0;
		std::array<std::string, 10> slots{};
	};

	/// Grimoire — layout autoritaire poussé par le shard (enter-world / ACK).
	struct ActionBarLayoutUpdateMessage
	{
		uint32_t clientId = 0;
		std::array<std::string, 10> slots{};
	};

	/// Roadmap-3 (2026-07-19) — Ceinture : slots d'objets actifs (slot i →
	/// jeton "item:<id>", "" = vide). Validée côté shard : objet POSSÉDÉ et
	/// activable (consommable ou gâteau).
	/// Ceinture v2 (2026-07-20) — taille VARIABLE : count u16 + count chaînes
	/// (modèle EquipmentUpdate). La capacité est AUTORITAIRE côté shard
	/// (ceinture équipée en slot Waist : 4 par défaut, 12 max) ; un count
	/// client > capacité est rejeté. Client et shard basculent ensemble
	/// (lock-step, décodeur strict) — pas de bump kProtocolVersion (framing
	/// inchangé, kinds 99/100 conservés).
	struct SetBeltLayoutMessage
	{
		uint32_t clientId = 0;
		std::vector<std::string> slots;
	};

	/// Roadmap-3 — layout ceinture autoritaire poussé par le shard
	/// (enter-world / ACK de SetBeltLayout, même réconciliation que 89).
	/// Ceinture v2 : le nombre de slots envoyés = capacité ACTIVE du joueur.
	struct BeltLayoutUpdateMessage
	{
		uint32_t clientId = 0;
		std::vector<std::string> slots;
	};

	/// SP-B — shard → client : état autoritaire de progression de classe (classId + skills connus).
	/// Poussé à l'enter-world et après chaque choix validé. Rétro-additif.
	struct ClassProgressionUpdateMessage
	{
		uint32_t clientId = 0;
		std::string classId;
		std::vector<std::string> knownSkillIds;
	};

	/// SP-B — client → shard : le joueur choisit 1 skill (parmi 3 proposés) à un niveau donné.
	struct ChooseClassSkillRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t level = 0;
		std::string skillId;
	};

	/// Shard → casteur : ressource secondaire courante (poussée sur variation).
	struct ResourceUpdateMessage
	{
		uint32_t clientId = 0;
		uint32_t currentResource = 0;
		uint32_t maxResource = 0;
	};

	/// Combat SP3 — états de la barre de cast (CastBarUpdateMessage::status).
	inline constexpr uint8_t kCastBarStatusStart = 0;
	inline constexpr uint8_t kCastBarStatusComplete = 1;
	inline constexpr uint8_t kCastBarStatusCancel = 2;

	/// Shard → casteur : début / fin / annulation d'un cast à temps d'incantation.
	struct CastBarUpdateMessage
	{
		uint32_t clientId = 0;
		uint8_t status = kCastBarStatusStart;
		/// Durée totale du cast en ms (status Start) ; 0 sinon.
		uint32_t durationMs = 0;
		std::string spellId;
	};

	/// Une aura active répliquée (cf. SpellEffectType côté shardd pour effectType).
	struct AuraWireEntry
	{
		std::string spellId;
		uint8_t effectType = 0;
		uint32_t remainingMs = 0;
		uint8_t stacks = 1;
	};

	/// Shard → clients intéressés : liste COMPLÈTE des auras d'une entité
	/// (le client remplace son état local — pas de delta, idempotent).
	struct AuraUpdateMessage
	{
		EntityId targetEntityId = 0;
		std::vector<AuraWireEntry> auras;
	};

	/// Combat SP4 — une entrée de menace répliquée (joueur → valeur brute).
	struct ThreatWireEntry
	{
		EntityId playerEntityId = 0;
		uint32_t threatValue = 0;
	};

	/// Combat SP4 — table de menace d'un mob (liste complète idempotente ;
	/// vide = le client efface — mort, evade, reset).
	struct ThreatUpdateMessage
	{
		EntityId mobEntityId = 0;
		std::vector<ThreatWireEntry> entries;
	};

	/// Correction SP1 — raisons d'une position imposée (ForcePositionMessage::reason).
	inline constexpr uint8_t kForcePositionReasonRespawn = 0;
	inline constexpr uint8_t kForcePositionReasonAntiCheat = 1;
	inline constexpr uint8_t kForcePositionReasonTeleport = 2;

	/// Correction SP1 — shard → client : position imposée par le serveur. Le
	/// client téléporte son CharacterController (le mouvement client-autoritaire
	/// reprend ensuite depuis cette position). Payload fixe 21 octets.
	struct ForcePositionMessage
	{
		uint32_t clientId = 0;
		float positionX = 0.0f;
		float positionY = 0.0f;
		float positionZ = 0.0f;
		float yawRadians = 0.0f;
		uint8_t reason = kForcePositionReasonTeleport;
	};

	/// Client request asking the authoritative server to pick up one loot bag entity.
	struct PickupRequestMessage
	{
		uint32_t clientId = 0;
		EntityId lootBagEntityId = 0;
	};

	/// Inventory delta emitted after a successful authoritative pickup.
	struct InventoryDeltaMessage
	{
		uint32_t clientId = 0;
	};

	/// Chantier 2 SP-A — requête d'équipement (client→serveur). Le client demande
	/// à équiper `itemId` (présent dans son sac). Le slot est déterminé par le
	/// serveur depuis SON catalogue (jamais fourni par le client).
	struct EquipRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t itemId = 0;
	};

	/// Chantier 2 SP-A — requête de retrait d'équipement (client→serveur).
	/// `slot` = valeur de engine::items::EquipmentSlot (1..10).
	struct UnequipRequestMessage
	{
		uint32_t clientId = 0;
		uint8_t slot = 0;
	};

	/// Une entrée d'équipement portée : slot occupé → itemId. Slot = valeur de
	/// engine::items::EquipmentSlot (1..10).
	struct EquipmentEntry
	{
		uint8_t slot = 0;
		uint32_t itemId = 0;
	};

	/// Chantier 2 SP-A — snapshot d'équipement (serveur→client). Contient les
	/// slots occupés uniquement ; le client vide puis applique (idempotent).
	struct EquipmentUpdateMessage
	{
		uint32_t clientId = 0;
	};

	/// Client request asking the authoritative server to validate one quest talk target.
	struct TalkRequestMessage
	{
		uint32_t clientId = 0;
		std::string targetId;
	};

	/// One step payload attached to a quest delta message.
	struct QuestDeltaStep
	{
		uint8_t stepType = 0;
		std::string targetId;
		uint32_t currentCount = 0;
		uint32_t requiredCount = 0;
	};

	/// Quest journal delta emitted after one quest state change.
	struct QuestDeltaMessage
	{
		uint32_t clientId = 0;
		uint8_t status = 0;
		std::string questId;
		uint32_t rewardExperience = 0;
		uint32_t rewardGold = 0;
		std::vector<QuestDeltaStep> steps;
		std::vector<ItemStack> rewardItems;
	};

	/// Une entrée de la liste de quêtes d'un PNJ (offer ou turn-in).
	struct QuestGiverEntry
	{
		std::string questId;
		uint8_t role = 0;   ///< 0 = offer (Offered), 1 = turnin (ReadyToTurnIn)
	};

	/// SP1 quêtes — réponse au Talk : quêtes qu'un PNJ propose / que le joueur peut y rendre
	/// (serveur→client).
	struct QuestGiverListMessage
	{
		uint32_t clientId = 0;
		std::string npcTargetId;
		std::vector<QuestGiverEntry> entries;
	};

	/// SP1 quêtes — le joueur accepte une quête au PNJ giver (client→serveur).
	struct QuestAcceptRequestMessage
	{
		uint32_t clientId = 0;
		std::string questId;
		std::string giverTargetId;
	};

	/// SP1 quêtes — le joueur rend une quête au PNJ turn-in (client→serveur).
	struct QuestTurnInRequestMessage
	{
		uint32_t clientId = 0;
		std::string questId;
		std::string npcTargetId;
	};

	/// Client chat send request (parsed prefixes applied client-side; server validates + routes).
	struct ChatSendRequestMessage
	{
		uint32_t clientId = 0;
		uint8_t channel = 0;
		EntityId whisperTargetEntityId = 0;
		std::string text;
	};

	/// One chat line replicated from server to clients.
	struct ChatRelayMessage
	{
		uint8_t channel = 0;
		EntityId senderEntityId = 0;
		uint64_t timestampUnixMs = 0;
		std::string senderDisplay;
		std::string text;
	};

	/// One emote playback event replicated from server to nearby clients (M29.3).
	struct EmoteRelayMessage
	{
		EntityId actorEntityId = 0;
		uint8_t emoteId = 0;
		/// Bit 0: loop posture (e.g. sit). Remaining bits reserved.
		uint8_t flags = 0;
		uint32_t serverTick = 0;
	};

	/// Dynamic event state message emitted after one event state or phase change.
	struct EventStateMessage
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
		std::vector<ItemStack> rewardItems;
	};

	/// Decode a hello packet and validate the protocol header.
	bool DecodeHello(std::span<const std::byte> packet, HelloMessage& outMessage);

	/// Encode initial client handshake (UDP gameplay client, M35.2).
	std::vector<std::byte> EncodeHello(const HelloMessage& message);

	/// Decode an input packet and validate the protocol header.
	bool DecodeInput(std::span<const std::byte> packet, InputMessage& outMessage);

	/// TC.1 — encode un INPUT (client→shard) : symétrique de DecodeInput. 24 octets de payload
	/// (clientId, inputSequence, posX, posY, posZ, yaw).
	std::vector<std::byte> EncodeInput(const InputMessage& message);

	/// Encode/decode un GOODBYE (client→shard) : départ propre, payload 4 octets (clientId).
	std::vector<std::byte> EncodeGoodbye(const GoodbyeMessage& message);
	bool DecodeGoodbye(std::span<const std::byte> packet, GoodbyeMessage& outMessage);

	/// Read only the packet kind after validating the shared protocol header.
	bool PeekMessageKind(std::span<const std::byte> packet, MessageKind& outKind);

	/// Encode a welcome packet with the protocol header.
	std::vector<std::byte> EncodeWelcome(const WelcomeMessage& message);

	/// Decode a welcome packet and validate the protocol header.
	bool DecodeWelcome(std::span<const std::byte> packet, WelcomeMessage& outMessage);

	/// Encode un PLAYER_STATS (shard→client) : stats dérivées complètes du joueur (R1-B).
	std::vector<std::byte> EncodePlayerStats(const PlayerStatsMessage& message);

	/// Decode un PLAYER_STATS et valide le header partagé du protocole (R1-B).
	bool DecodePlayerStats(std::span<const std::byte> packet, PlayerStatsMessage& outMessage);

	/// Encode a snapshot packet with the protocol header.
	std::vector<std::byte> EncodeSnapshot(const SnapshotMessage& message, std::span<const SnapshotEntity> entities);

	/// Decode a snapshot packet and reuse the provided entity buffer for the payload entities.
	bool DecodeSnapshot(std::span<const std::byte> packet, SnapshotMessage& outMessage, std::vector<SnapshotEntity>& outEntities);

	/// Encode a spawn packet with the protocol header.
	std::vector<std::byte> EncodeSpawn(const SpawnEntity& entity);

	/// Encode a despawn packet with the protocol header.
	std::vector<std::byte> EncodeDespawn(const DespawnEntity& entity);

	/// Decode a zone change packet and validate the protocol header.
	bool DecodeZoneChange(std::span<const std::byte> packet, ZoneChangeMessage& outMessage);

	/// Encode a zone change packet with the protocol header.
	std::vector<std::byte> EncodeZoneChange(const ZoneChangeMessage& message);

	/// Decode an attack request packet and validate the protocol header.
	bool DecodeAttackRequest(std::span<const std::byte> packet, AttackRequestMessage& outMessage);

	/// Combat SP2 — encode an attack request packet (client → server).
	std::vector<std::byte> EncodeAttackRequest(const AttackRequestMessage& message);

	/// Combat SP2 — encode a respawn request packet (client → server).
	std::vector<std::byte> EncodeRespawnRequest(const RespawnRequestMessage& message);

	/// Combat SP2 — decode a respawn request packet and validate the protocol header.
	bool DecodeRespawnRequest(std::span<const std::byte> packet, RespawnRequestMessage& outMessage);

	/// Combat SP3 — encode/decode des messages sorts et auras (wire v11).
	std::vector<std::byte> EncodeCastRequest(const CastRequestMessage& message);
	bool DecodeCastRequest(std::span<const std::byte> packet, CastRequestMessage& outMessage);
	std::vector<std::byte> EncodeResourceUpdate(const ResourceUpdateMessage& message);
	bool DecodeResourceUpdate(std::span<const std::byte> packet, ResourceUpdateMessage& outMessage);
	std::vector<std::byte> EncodeCastBarUpdate(const CastBarUpdateMessage& message);
	bool DecodeCastBarUpdate(std::span<const std::byte> packet, CastBarUpdateMessage& outMessage);
	std::vector<std::byte> EncodeAuraUpdate(const AuraUpdateMessage& message);
	bool DecodeAuraUpdate(std::span<const std::byte> packet, AuraUpdateMessage& outMessage);

	/// Combat SP4 — encode/decode de la table de menace répliquée (wire v12).
	std::vector<std::byte> EncodeThreatUpdate(const ThreatUpdateMessage& message);
	bool DecodeThreatUpdate(std::span<const std::byte> packet, ThreatUpdateMessage& outMessage);

	/// Correction SP1 — encode/decode de la position imposée (rétro-additif).
	std::vector<std::byte> EncodeForcePosition(const ForcePositionMessage& message);
	bool DecodeForcePosition(std::span<const std::byte> packet, ForcePositionMessage& outMessage);

	/// Validation v12 — butin auto-crédité à la mort (rétro-additif).
	/// Payload : clientId (4) + count (1) puis par objet itemId (4) + quantity (4).
	struct LootNotifyMessage
	{
		uint32_t clientId = 0;
		std::vector<ItemStack> items;
	};
	std::vector<std::byte> EncodeLootNotify(const LootNotifyMessage& message);
	bool DecodeLootNotify(std::span<const std::byte> packet, LootNotifyMessage& outMessage);

	/// Grimoire — encode/decode des messages de réassignation de barre d'action (rétro-additifs).
	std::vector<std::byte> EncodeSetActionBarLayout(const SetActionBarLayoutMessage& message);
	bool DecodeSetActionBarLayout(std::span<const std::byte> packet, SetActionBarLayoutMessage& outMessage);
	std::vector<std::byte> EncodeActionBarLayoutUpdate(const ActionBarLayoutUpdateMessage& message);
	bool DecodeActionBarLayoutUpdate(std::span<const std::byte> packet, ActionBarLayoutUpdateMessage& outMessage);
	// Roadmap-3 (2026-07-19) — Ceinture (kinds 99/100, modèle 88/89).
	std::vector<std::byte> EncodeSetBeltLayout(const SetBeltLayoutMessage& message);
	bool DecodeSetBeltLayout(std::span<const std::byte> packet, SetBeltLayoutMessage& outMessage);
	std::vector<std::byte> EncodeBeltLayoutUpdate(const BeltLayoutUpdateMessage& message);
	bool DecodeBeltLayoutUpdate(std::span<const std::byte> packet, BeltLayoutUpdateMessage& outMessage);

	/// SP-B — encode/decode de la progression de classe (shard→client, rétro-additif).
	std::vector<std::byte> EncodeClassProgressionUpdate(const ClassProgressionUpdateMessage& message);
	bool DecodeClassProgressionUpdate(std::span<const std::byte> packet, ClassProgressionUpdateMessage& outMessage);

	/// SP-B — encode/decode du choix de skill de classe (client→shard, rétro-additif).
	std::vector<std::byte> EncodeChooseClassSkillRequest(const ChooseClassSkillRequestMessage& message);
	bool DecodeChooseClassSkillRequest(std::span<const std::byte> packet, ChooseClassSkillRequestMessage& outMessage);

	/// Encode a combat event packet with the protocol header.
	std::vector<std::byte> EncodeCombatEvent(const CombatEventMessage& message);

	/// Decode a combat event packet and validate the protocol header.
	bool DecodeCombatEvent(std::span<const std::byte> packet, CombatEventMessage& outMessage);

	/// Decode a pickup request packet and validate the protocol header.
	/// Validation v12 — encodeur côté client (le serveur décodait depuis M28
	/// mais le client n'émettait jamais : ramassage du butin enfin câblé).
	std::vector<std::byte> EncodePickupRequest(const PickupRequestMessage& message);
	bool DecodePickupRequest(std::span<const std::byte> packet, PickupRequestMessage& outMessage);

	/// Encode an inventory delta packet with the protocol header.
	std::vector<std::byte> EncodeInventoryDelta(const InventoryDeltaMessage& message, std::span<const ItemStack> items);

	/// Decode an inventory delta packet and reuse the provided item buffer for the payload items.
	bool DecodeInventoryDelta(std::span<const std::byte> packet, InventoryDeltaMessage& outMessage, std::vector<ItemStack>& outItems);

	/// Chantier 2 SP-A — encode/décode d'une requête d'équipement (payload fixe 8 o).
	std::vector<std::byte> EncodeEquipRequest(const EquipRequestMessage& message);
	bool DecodeEquipRequest(std::span<const std::byte> packet, EquipRequestMessage& outMessage);

	/// Chantier 2 SP-A — encode/décode d'une requête de retrait (payload fixe 5 o).
	std::vector<std::byte> EncodeUnequipRequest(const UnequipRequestMessage& message);
	bool DecodeUnequipRequest(std::span<const std::byte> packet, UnequipRequestMessage& outMessage);

	/// Chantier 2 SP-A — encode/décode d'un snapshot d'équipement.
	/// Payload : clientId (4) + count (2) + count × [slot (1) + itemId (4)].
	std::vector<std::byte> EncodeEquipmentUpdate(const EquipmentUpdateMessage& message, std::span<const EquipmentEntry> entries);
	bool DecodeEquipmentUpdate(std::span<const std::byte> packet, EquipmentUpdateMessage& outMessage, std::vector<EquipmentEntry>& outEntries);

	/// Decode a talk request packet and validate the protocol header.
	bool DecodeTalkRequest(std::span<const std::byte> packet, TalkRequestMessage& outMessage);

	/// Encode a talk request packet (client → server).
	std::vector<std::byte> EncodeTalkRequest(const TalkRequestMessage& message);

	/// Encode a quest delta packet with the protocol header.
	std::vector<std::byte> EncodeQuestDelta(const QuestDeltaMessage& message);

	/// Decode a quest delta packet and validate the protocol header.
	bool DecodeQuestDelta(std::span<const std::byte> packet, QuestDeltaMessage& outMessage);

	/// SP1 quêtes — encode/decode de la liste des quêtes offertes/rendables d'un PNJ (serveur→client).
	std::vector<std::byte> EncodeQuestGiverList(const QuestGiverListMessage& message);
	bool DecodeQuestGiverList(std::span<const std::byte> packet, QuestGiverListMessage& outMessage);

	/// SP1 quêtes — encode/decode de l'acceptation d'une quête (client→serveur).
	std::vector<std::byte> EncodeQuestAcceptRequest(const QuestAcceptRequestMessage& message);
	bool DecodeQuestAcceptRequest(std::span<const std::byte> packet, QuestAcceptRequestMessage& outMessage);

	/// SP1 quêtes — encode/decode du rendu d'une quête (client→serveur).
	std::vector<std::byte> EncodeQuestTurnInRequest(const QuestTurnInRequestMessage& message);
	bool DecodeQuestTurnInRequest(std::span<const std::byte> packet, QuestTurnInRequestMessage& outMessage);

	/// Encode a dynamic event state packet with the protocol header.
	std::vector<std::byte> EncodeEventState(const EventStateMessage& message);

	/// Decode a dynamic event state packet and validate the protocol header.
	bool DecodeEventState(std::span<const std::byte> packet, EventStateMessage& outMessage);

	/// Encode one chat send request packet with the protocol header.
	std::vector<std::byte> EncodeChatSend(const ChatSendRequestMessage& message);

	/// Decode a chat send request packet and validate the protocol header.
	bool DecodeChatSend(std::span<const std::byte> packet, ChatSendRequestMessage& outMessage);

	/// Encode one chat relay packet with the protocol header.
	std::vector<std::byte> EncodeChatRelay(const ChatRelayMessage& message);

	/// Convenience: encode a server-authored broadcast on the Server channel (wire=7).
	/// senderEntityId is always 0 and senderDisplay is fixed to "[Serveur]".
	std::vector<std::byte> EncodeServerNotify(const std::string& text, uint64_t timestampUnixMs);

	/// Decode a chat relay packet and validate the protocol header.
	bool DecodeChatRelay(std::span<const std::byte> packet, ChatRelayMessage& outMessage);

	/// Encode one emote relay packet with the protocol header.
	std::vector<std::byte> EncodeEmoteRelay(const EmoteRelayMessage& message);

	/// Decode an emote relay packet and validate the protocol header.
	bool DecodeEmoteRelay(std::span<const std::byte> packet, EmoteRelayMessage& outMessage);

	// -------------------------------------------------------------------------
	// M32.1 — Friend system messages
	// -------------------------------------------------------------------------

	/// Presence status values used by the friend system (wire-stable).
	enum class PresenceStatus : uint8_t
	{
		Offline = 0,
		Online  = 1,
		Away    = 2,
		Busy    = 3
	};

	/// Client request to add a friend by display name (/friend add <name>).
	struct FriendRequestMessage
	{
		uint32_t    clientId = 0;
		std::string targetName;
	};

	/// Server notification pushed to the target player of an incoming request.
	struct FriendRequestNotifyMessage
	{
		std::string requesterName;
	};

	/// Client acceptance of a pending friend request (/friend accept <name>).
	struct FriendAcceptMessage
	{
		uint32_t    clientId = 0;
		std::string requesterName;
	};

	/// Client decline of a pending friend request (/friend decline <name>).
	struct FriendDeclineMessage
	{
		uint32_t    clientId = 0;
		std::string requesterName;
	};

	/// Client removal of an accepted friend (/friend remove <name>).
	struct FriendRemoveMessage
	{
		uint32_t    clientId = 0;
		std::string friendName;
	};

	/// One entry in the friends list sent on login.
	struct FriendListEntry
	{
		std::string    name;
		PresenceStatus presenceStatus = PresenceStatus::Offline;
		/// True when this entry is a pending inbound request (awaiting local player acceptance).
		bool           isPendingInbound = false;
	};

	/// Server-sent full friends list delivered once after successful login.
	struct FriendListSyncMessage
	{
		std::vector<FriendListEntry> friends;
	};

	/// Server notification of one friend's presence change (login, logout, status change).
	struct FriendStatusUpdateMessage
	{
		std::string    friendName;
		PresenceStatus presenceStatus = PresenceStatus::Offline;
	};

	/// Encode a client friend request packet.
	std::vector<std::byte> EncodeFriendRequest(const FriendRequestMessage& message);

	/// Decode a client friend request packet.
	bool DecodeFriendRequest(std::span<const std::byte> packet, FriendRequestMessage& outMessage);

	/// Encode a server friend request notification packet.
	std::vector<std::byte> EncodeFriendRequestNotify(const FriendRequestNotifyMessage& message);

	/// Decode a server friend request notification packet.
	bool DecodeFriendRequestNotify(std::span<const std::byte> packet, FriendRequestNotifyMessage& outMessage);

	/// Encode a client friend accept packet.
	std::vector<std::byte> EncodeFriendAccept(const FriendAcceptMessage& message);

	/// Decode a client friend accept packet.
	bool DecodeFriendAccept(std::span<const std::byte> packet, FriendAcceptMessage& outMessage);

	/// Encode a client friend decline packet.
	std::vector<std::byte> EncodeFriendDecline(const FriendDeclineMessage& message);

	/// Decode a client friend decline packet.
	bool DecodeFriendDecline(std::span<const std::byte> packet, FriendDeclineMessage& outMessage);

	/// Encode a client friend remove packet.
	std::vector<std::byte> EncodeFriendRemove(const FriendRemoveMessage& message);

	/// Decode a client friend remove packet.
	bool DecodeFriendRemove(std::span<const std::byte> packet, FriendRemoveMessage& outMessage);

	/// Encode a server friend list sync packet (sent on login).
	std::vector<std::byte> EncodeFriendListSync(const FriendListSyncMessage& message);

	/// Decode a server friend list sync packet.
	bool DecodeFriendListSync(std::span<const std::byte> packet, FriendListSyncMessage& outMessage);

	/// Encode a server friend status update packet.
	std::vector<std::byte> EncodeFriendStatusUpdate(const FriendStatusUpdateMessage& message);

	/// Decode a server friend status update packet.
	bool DecodeFriendStatusUpdate(std::span<const std::byte> packet, FriendStatusUpdateMessage& outMessage);

	// -------------------------------------------------------------------------
	// M32.2 — Party system messages
	// -------------------------------------------------------------------------

	/// Wire value for LootMode (must stay in sync with engine::server::LootMode).
	enum class WireLootMode : uint8_t
	{
		FreeForAll   = 0,
		RoundRobin   = 1,
		MasterLooter = 2,
		NeedGreed    = 3
	};

	/// One party member entry carried inside a PartyUpdate packet.
	struct PartyMemberEntry
	{
		uint32_t    clientId      = 0;
		uint32_t    currentHealth = 0;
		uint32_t    maxHealth     = 0;
		uint32_t    currentMana   = 0;
		uint32_t    maxMana       = 0;
		std::string displayName;
	};

	/// Client /invite <name> request (M32.2).
	struct PartyInviteMessage
	{
		uint32_t    clientId   = 0;
		std::string targetName; ///< Display name of the player to invite (e.g. "P12").
	};

	/// Server notification pushed to the invite target (M32.2).
	struct PartyInviteNotifyMessage
	{
		std::string inviterName;
	};

	/// Client acceptance of a pending party invite (M32.2).
	struct PartyAcceptMessage
	{
		uint32_t clientId = 0;
	};

	/// Client decline of a pending party invite (M32.2).
	struct PartyDeclineMessage
	{
		uint32_t clientId = 0;
	};

	/// Leader request to kick a member from the party (M32.2).
	struct PartyKickMessage
	{
		uint32_t    clientId   = 0; ///< Leader's clientId.
		std::string targetName;     ///< Display name of the member to kick.
	};

	/// Full party state broadcast to all members after any change (M32.2).
	struct PartyUpdateMessage
	{
		uint32_t partyId  = 0;
		uint32_t leaderId = 0;
		WireLootMode lootMode = WireLootMode::FreeForAll;
		std::vector<PartyMemberEntry> members;
	};

	/// Leader request to change the party loot mode (M32.2).
	struct PartyLootModeMessage
	{
		uint32_t     clientId = 0;
		WireLootMode lootMode = WireLootMode::FreeForAll;
	};

	/// Client voluntary leave request (M32.2).
	struct PartyLeaveMessage
	{
		uint32_t clientId = 0;
	};

	/// Encode a client party invite request packet.
	std::vector<std::byte> EncodePartyInvite(const PartyInviteMessage& message);

	/// Decode a client party invite request packet.
	bool DecodePartyInvite(std::span<const std::byte> packet, PartyInviteMessage& outMessage);

	/// Encode a server party invite notification packet.
	std::vector<std::byte> EncodePartyInviteNotify(const PartyInviteNotifyMessage& message);

	/// Decode a server party invite notification packet.
	bool DecodePartyInviteNotify(std::span<const std::byte> packet, PartyInviteNotifyMessage& outMessage);

	/// Encode a client party accept packet.
	std::vector<std::byte> EncodePartyAccept(const PartyAcceptMessage& message);

	/// Decode a client party accept packet.
	bool DecodePartyAccept(std::span<const std::byte> packet, PartyAcceptMessage& outMessage);

	/// Encode a client party decline packet.
	std::vector<std::byte> EncodePartyDecline(const PartyDeclineMessage& message);

	/// Decode a client party decline packet.
	bool DecodePartyDecline(std::span<const std::byte> packet, PartyDeclineMessage& outMessage);

	/// Encode a leader party kick request packet.
	std::vector<std::byte> EncodePartyKick(const PartyKickMessage& message);

	/// Decode a leader party kick request packet.
	bool DecodePartyKick(std::span<const std::byte> packet, PartyKickMessage& outMessage);

	/// Encode a full party state update packet (server → all party members).
	std::vector<std::byte> EncodePartyUpdate(const PartyUpdateMessage& message);

	/// Decode a full party state update packet.
	bool DecodePartyUpdate(std::span<const std::byte> packet, PartyUpdateMessage& outMessage);

	/// Encode a leader loot-mode change request packet.
	std::vector<std::byte> EncodePartyLootMode(const PartyLootModeMessage& message);

	/// Decode a leader loot-mode change request packet.
	bool DecodePartyLootMode(std::span<const std::byte> packet, PartyLootModeMessage& outMessage);

	/// Encode a client party leave packet.
	std::vector<std::byte> EncodePartyLeave(const PartyLeaveMessage& message);

	/// Decode a client party leave packet.
	bool DecodePartyLeave(std::span<const std::byte> packet, PartyLeaveMessage& outMessage);

	// -------------------------------------------------------------------------
	// M32.3 — Guild system messages
	// -------------------------------------------------------------------------

	/// Client request to create a new guild via /guild create <name> (M32.3).
	struct GuildCreateMessage
	{
		uint32_t    clientId  = 0;
		std::string guildName; ///< Desired guild name, 3-20 chars.
	};

	/// Server result of a guild creation attempt (M32.3).
	struct GuildCreateResultMessage
	{
		uint8_t     success  = 0; ///< 1 = created, 0 = failed.
		uint64_t    guildId  = 0; ///< New guild id on success; 0 on failure.
		std::string guildName;
		std::string errorReason; ///< Human-readable reason on failure.
	};

	/// Client /ginvite <name> request (M32.3).
	struct GuildInviteMessage
	{
		uint32_t    clientId   = 0;
		std::string targetName; ///< Display name of the player to invite.
	};

	/// Server notification pushed to the invite target (M32.3).
	struct GuildInviteNotifyMessage
	{
		std::string inviterName;
		std::string guildName;
	};

	/// Target accepts a pending guild invite (M32.3).
	struct GuildInviteAcceptMessage
	{
		uint32_t clientId = 0;
	};

	/// Target declines a pending guild invite (M32.3).
	struct GuildInviteDeclineMessage
	{
		uint32_t clientId = 0;
	};

	/// Leader request to kick a member from the guild via /gkick (M32.3).
	struct GuildKickMessage
	{
		uint32_t    clientId   = 0; ///< Kicker's clientId.
		std::string targetName; ///< Display name of the member to kick.
	};

	/// Officer/Leader request to promote a member via /gpromote (M32.3).
	struct GuildPromoteMessage
	{
		uint32_t    clientId   = 0; ///< Promoter's clientId.
		std::string targetName; ///< Display name of the member to promote.
	};

	/// One entry in the guild roster sync packet (M32.3).
	struct GuildRosterEntry
	{
		uint64_t    playerId = 0;
		uint8_t     rankId   = 3; ///< 0=GM, 1=Officer, 2=Member, 3=Recruit.
		uint8_t     online   = 0; ///< 1 when the member is currently online.
		std::string playerName;
		std::string rankName;
	};

	/// Full guild roster broadcast to all members after any change (M32.3).
	struct GuildRosterSyncMessage
	{
		uint64_t                   guildId  = 0;
		std::string                guildName;
		std::string                motd;
		std::vector<GuildRosterEntry> members;
	};

	/// Client/Officer MOTD update request (M32.3).
	struct GuildMotdUpdateMessage
	{
		uint32_t    clientId = 0;
		std::string motd;
	};

	// -------------------------------------------------------------------------
	// M35.1 — Wallet replication
	// -------------------------------------------------------------------------

	/// Server → client wallet snapshot (gold, honor, badges, premium).
	struct WalletUpdateMessage
	{
		uint32_t clientId = 0;
		uint32_t gold = 0;
		uint32_t honor = 0;
		uint32_t badges = 0;
		uint32_t premiumCurrency = 0;
	};

	/// Encode a wallet update packet (server authoritative).
	std::vector<std::byte> EncodeWalletUpdate(const WalletUpdateMessage& message);

	/// Decode a wallet update packet.
	bool DecodeWalletUpdate(std::span<const std::byte> packet, WalletUpdateMessage& outMessage);

	// -------------------------------------------------------------------------
	// PR-C — Réplication de la progression de niveau (barre d'XP)
	// -------------------------------------------------------------------------

	/// Server → client : progression de niveau du joueur local.
	/// \c xpForNextLevel = 0 signale le cap de niveau (barre pleine, pas de suivant).
	struct PlayerXpUpdateMessage
	{
		uint32_t clientId = 0;
		uint32_t level = 0;
		uint32_t xpIntoLevel = 0;    ///< XP accumulée DANS le niveau courant.
		uint32_t xpForNextLevel = 0; ///< XP requise pour passer au niveau suivant (0 = cap).
	};

	/// Encode a player XP update packet (server authoritative).
	std::vector<std::byte> EncodePlayerXpUpdate(const PlayerXpUpdateMessage& message);

	/// Decode a player XP update packet.
	bool DecodePlayerXpUpdate(std::span<const std::byte> packet, PlayerXpUpdateMessage& outMessage);

	// -------------------------------------------------------------------------
	// M35.2 — Vendor shop
	// -------------------------------------------------------------------------

	inline constexpr uint32_t kShopInfiniteStockWire = 0xFFFFFFFFu;
	inline constexpr uint16_t kMaxShopOffersPerPacket = 64;

	/// One row in a ShopOpen grid.
	struct ShopOfferWire
	{
		uint32_t itemId = 0;
		uint32_t buyPrice = 0;
		/// Remaining stock for finite items; \ref kShopInfiniteStockWire when unlimited.
		uint32_t stock = 0;
	};

	/// Server → client shop inventory snapshot.
	struct ShopOpenMessage
	{
		uint32_t vendorId = 0;
		std::string displayName;
		std::vector<ShopOfferWire> offers;
	};

	/// Client → server buy request.
	struct ShopBuyRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t vendorId = 0;
		uint32_t itemId = 0;
		uint32_t quantity = 0;
	};

	/// Client → server sell request.
	struct ShopSellRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t vendorId = 0;
		uint32_t itemId = 0;
		uint32_t quantity = 0;
	};

	std::vector<std::byte> EncodeShopOpen(const ShopOpenMessage& message);
	bool DecodeShopOpen(std::span<const std::byte> packet, ShopOpenMessage& outMessage);

	std::vector<std::byte> EncodeShopBuyRequest(const ShopBuyRequestMessage& message);
	bool DecodeShopBuyRequest(std::span<const std::byte> packet, ShopBuyRequestMessage& outMessage);

	std::vector<std::byte> EncodeShopSellRequest(const ShopSellRequestMessage& message);
	bool DecodeShopSellRequest(std::span<const std::byte> packet, ShopSellRequestMessage& outMessage);

	// -------------------------------------------------------------------------
	// M35.4 — Auction house
	// -------------------------------------------------------------------------

	inline constexpr uint16_t kMaxAuctionBrowseRowsWire = 32;

	/// One listing row in browse results (M35.4).
	struct AuctionListingWireRow
	{
		uint32_t listingId = 0;
		uint32_t itemId = 0;
		uint32_t quantity = 0;
		uint32_t startBid = 0;
		uint32_t buyoutPrice = 0;
		uint32_t currentBid = 0;
		uint32_t expiresAtTick = 0;
	};

	struct AuctionBrowseRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t minPrice = 0;
		uint32_t maxPrice = 0;
		/// 0 = any item.
		uint32_t itemIdFilter = 0;
		/// 0 = price asc, 1 = price desc, 2 = expiry asc.
		uint32_t sortMode = 0;
		uint32_t maxRows = 32;
	};

	struct AuctionBrowseResultMessage
	{
		uint32_t clientId = 0;
		std::vector<AuctionListingWireRow> rows;
	};

	struct AuctionListItemRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t itemId = 0;
		uint32_t quantity = 0;
		uint32_t startBid = 0;
		uint32_t buyoutPrice = 0;
		/// Hours: 12, 24, or 48.
		uint32_t durationHours = 24;
	};

	struct AuctionBidRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t listingId = 0;
		uint32_t bidAmount = 0;
	};

	struct AuctionBuyoutRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t listingId = 0;
	};

	std::vector<std::byte> EncodeAuctionBrowseRequest(const AuctionBrowseRequestMessage& message);
	bool DecodeAuctionBrowseRequest(std::span<const std::byte> packet, AuctionBrowseRequestMessage& outMessage);

	std::vector<std::byte> EncodeAuctionBrowseResult(const AuctionBrowseResultMessage& message);
	bool DecodeAuctionBrowseResult(std::span<const std::byte> packet, AuctionBrowseResultMessage& outMessage);

	std::vector<std::byte> EncodeAuctionListItemRequest(const AuctionListItemRequestMessage& message);
	bool DecodeAuctionListItemRequest(std::span<const std::byte> packet, AuctionListItemRequestMessage& outMessage);

	std::vector<std::byte> EncodeAuctionBidRequest(const AuctionBidRequestMessage& message);
	bool DecodeAuctionBidRequest(std::span<const std::byte> packet, AuctionBidRequestMessage& outMessage);

	std::vector<std::byte> EncodeAuctionBuyoutRequest(const AuctionBuyoutRequestMessage& message);
	bool DecodeAuctionBuyoutRequest(std::span<const std::byte> packet, AuctionBuyoutRequestMessage& outMessage);

	// -------------------------------------------------------------------------
	// M35.3 — Direct player-to-player trade messages
	// -------------------------------------------------------------------------

	/// Maximum number of item stacks offered per side of a trade window (M35.3).
	inline constexpr uint8_t kMaxTradeItemSlots = 8;

	/// Client → server: /trade <name> initiates a trade request (M35.3).
	struct TradeRequestMessage
	{
		uint32_t    clientId = 0;
		std::string targetName; ///< Display name of the player to trade with.
	};

	/// Server → target: incoming trade request notification (M35.3).
	struct TradeRequestNotifyMessage
	{
		std::string initiatorName; ///< Display name of the initiating player.
	};

	/// Client → server: accept a pending incoming trade request (M35.3).
	struct TradeAcceptMessage
	{
		uint32_t clientId = 0;
	};

	/// Client → server: decline a pending incoming trade request (M35.3).
	struct TradeDeclineMessage
	{
		uint32_t clientId = 0;
	};

	/// Client → server: add one item to own trade slot (M35.3).
	struct TradeAddItemMessage
	{
		uint32_t clientId  = 0;
		uint32_t itemId    = 0;
		uint32_t quantity  = 0;
	};

	/// Client → server: set own offered gold amount (M35.3).
	struct TradeSetGoldMessage
	{
		uint32_t clientId   = 0;
		uint32_t goldAmount = 0;
	};

	/// Client → server: lock own side — enter 5 s review phase (M35.3).
	struct TradeLockMessage
	{
		uint32_t clientId = 0;
	};

	/// Client → server: final irreversible confirm (M35.3).
	struct TradeConfirmMessage
	{
		uint32_t clientId = 0;
	};

	/// Client → server: cancel the ongoing trade session (M35.3).
	struct TradeCancelMessage
	{
		uint32_t clientId = 0;
	};

	/// One side of the trade window state replicated to both players (M35.3).
	struct TradeSideWire
	{
		uint32_t                  clientId   = 0;
		uint32_t                  goldAmount = 0;
		uint8_t                   locked     = 0; ///< 1 when this side has pressed Lock.
		uint8_t                   confirmed  = 0; ///< 1 when this side has pressed Confirm.
		std::vector<ItemStack>    items;
	};

	/// Server → both clients: full trade window state snapshot (M35.3).
	struct TradeWindowUpdateMessage
	{
		TradeSideWire self;  ///< The receiving player's side.
		TradeSideWire other; ///< The remote player's side.
		/// Ticks remaining in the review phase (0 when not in review).
		uint32_t reviewTicksRemaining = 0;
	};

	/// Server → both clients: trade completed successfully (M35.3).
	struct TradeCompleteMessage
	{
		uint32_t clientId = 0; ///< Echoed to the receiver for UI confirmation.
	};

	/// Server → both clients: trade was cancelled with a human-readable reason (M35.3).
	struct TradeCancelledMessage
	{
		std::string reason;
	};

	std::vector<std::byte> EncodeTradeRequest(const TradeRequestMessage& message);
	bool DecodeTradeRequest(std::span<const std::byte> packet, TradeRequestMessage& outMessage);

	std::vector<std::byte> EncodeTradeRequestNotify(const TradeRequestNotifyMessage& message);
	bool DecodeTradeRequestNotify(std::span<const std::byte> packet, TradeRequestNotifyMessage& outMessage);

	std::vector<std::byte> EncodeTradeAccept(const TradeAcceptMessage& message);
	bool DecodeTradeAccept(std::span<const std::byte> packet, TradeAcceptMessage& outMessage);

	std::vector<std::byte> EncodeTradeDecline(const TradeDeclineMessage& message);
	bool DecodeTradeDecline(std::span<const std::byte> packet, TradeDeclineMessage& outMessage);

	std::vector<std::byte> EncodeTradeAddItem(const TradeAddItemMessage& message);
	bool DecodeTradeAddItem(std::span<const std::byte> packet, TradeAddItemMessage& outMessage);

	std::vector<std::byte> EncodeTradeSetGold(const TradeSetGoldMessage& message);
	bool DecodeTradeSetGold(std::span<const std::byte> packet, TradeSetGoldMessage& outMessage);

	std::vector<std::byte> EncodeTradeLock(const TradeLockMessage& message);
	bool DecodeTradeLock(std::span<const std::byte> packet, TradeLockMessage& outMessage);

	std::vector<std::byte> EncodeTradeConfirm(const TradeConfirmMessage& message);
	bool DecodeTradeConfirm(std::span<const std::byte> packet, TradeConfirmMessage& outMessage);

	std::vector<std::byte> EncodeTradeCancel(const TradeCancelMessage& message);
	bool DecodeTradeCancel(std::span<const std::byte> packet, TradeCancelMessage& outMessage);

	std::vector<std::byte> EncodeTradeWindowUpdate(const TradeWindowUpdateMessage& message);
	bool DecodeTradeWindowUpdate(std::span<const std::byte> packet, TradeWindowUpdateMessage& outMessage);

	std::vector<std::byte> EncodeTradeComplete(const TradeCompleteMessage& message);
	bool DecodeTradeComplete(std::span<const std::byte> packet, TradeCompleteMessage& outMessage);

	std::vector<std::byte> EncodeTradeCancelled(const TradeCancelledMessage& message);
	bool DecodeTradeCancelled(std::span<const std::byte> packet, TradeCancelledMessage& outMessage);

	// -------------------------------------------------------------------------
	// M36.1 — Gathering / harvesting resource nodes
	// -------------------------------------------------------------------------

	/// Client → server: request to begin harvesting the node with the given id (M36.1).
	struct HarvestRequestMessage
	{
		uint32_t clientId        = 0;
		uint64_t nodeEntityId    = 0; ///< Server-assigned entity id of the resource node.
	};

	/// Server → client: harvest cast bar started; client must show progress bar (M36.1).
	struct HarvestStartMessage
	{
		uint64_t nodeEntityId        = 0;
		uint32_t harvestDurationTicks = 0; ///< Total server ticks until HarvestComplete fires.
	};

	/// Server → client: harvest completed; InventoryDelta follows (M36.1).
	struct HarvestCompleteMessage
	{
		uint64_t nodeEntityId = 0;
	};

	/// Client → server: player explicitly cancels their current harvest (M36.1).
	struct HarvestCancelRequestMessage
	{
		uint32_t clientId = 0;
	};

	/// Reason code for a server-initiated harvest cancellation (M36.1).
	enum class HarvestCancelReason : uint8_t
	{
		PlayerRequested = 0, ///< Player pressed cancel.
		PlayerMoved     = 1, ///< Player moved outside the harvest range.
		PlayerDamaged   = 2, ///< Player received damage.
		NodeGone        = 3, ///< Node was harvested by another player.
	};

	/// Server → client: harvest cancelled — discard cast bar (M36.1).
	struct HarvestCancelledMessage
	{
		uint64_t nodeEntityId = 0;
		HarvestCancelReason reason = HarvestCancelReason::PlayerRequested;
	};

	std::vector<std::byte> EncodeHarvestRequest(const HarvestRequestMessage& message);
	bool DecodeHarvestRequest(std::span<const std::byte> packet, HarvestRequestMessage& outMessage);

	std::vector<std::byte> EncodeHarvestStart(const HarvestStartMessage& message);
	bool DecodeHarvestStart(std::span<const std::byte> packet, HarvestStartMessage& outMessage);

	std::vector<std::byte> EncodeHarvestComplete(const HarvestCompleteMessage& message);
	bool DecodeHarvestComplete(std::span<const std::byte> packet, HarvestCompleteMessage& outMessage);

	std::vector<std::byte> EncodeHarvestCancelRequest(const HarvestCancelRequestMessage& message);
	bool DecodeHarvestCancelRequest(std::span<const std::byte> packet, HarvestCancelRequestMessage& outMessage);

	std::vector<std::byte> EncodeHarvestCancelled(const HarvestCancelledMessage& message);
	bool DecodeHarvestCancelled(std::span<const std::byte> packet, HarvestCancelledMessage& outMessage);

	// -------------------------------------------------------------------------
	// M36.2 — Crafting / profession skill system messages
	// -------------------------------------------------------------------------

	/// Maximum skill level per profession (M36.2).
	inline constexpr uint32_t kMaxProfessionSkillLevel = 300u;
	/// Maximum number of primary professions per character (M36.2).
	inline constexpr uint32_t kMaxPrimaryProfessions = 2u;
	/// Maximum number of recipe entries in one CraftRecipeListResult packet (M36.2).
	inline constexpr uint16_t kMaxCraftRecipeListRows = 64u;

	/// One profession entry replicated to the client (M36.2).
	struct ProfessionWireEntry
	{
		std::string professionKey; ///< E.g. "blacksmithing", "alchemy".
		uint32_t    skillLevel  = 1;
		uint8_t     isPrimary   = 0; ///< 1 when this is a primary profession slot.
	};

	/// Client → server: request to learn a profession by key (M36.2).
	struct LearnProfessionRequestMessage
	{
		uint32_t    clientId       = 0;
		std::string professionKey;
		uint8_t     asPrimary      = 0; ///< 1 when the player wants it as a primary slot.
	};

	/// Server → client: full profession state for the owning client (M36.2).
	struct ProfessionUpdateMessage
	{
		uint32_t clientId = 0;
		std::vector<ProfessionWireEntry> professions;
	};

	/// Client → server: request the recipe list for one profession (M36.2).
	struct CraftRecipeListRequestMessage
	{
		uint32_t    clientId       = 0;
		std::string professionKey;
	};

	/// One recipe summary row sent in CraftRecipeListResult (M36.2).
	struct CraftRecipeWireRow
	{
		std::string recipeId;
		uint32_t    skillRequired  = 0;
		uint32_t    outputItemId   = 0;
		uint32_t    outputQuantity = 1;
	};

	/// Server → client: list of recipes for the requested profession (M36.2).
	struct CraftRecipeListResultMessage
	{
		uint32_t    clientId = 0;
		std::string professionKey;
		std::vector<CraftRecipeWireRow> recipes;
	};

	/// Client → server: start crafting the given recipe (M36.2).
	struct CraftRequestMessage
	{
		uint32_t    clientId  = 0;
		std::string recipeId;
	};

	/// Server → client: crafting cast bar started (M36.2).
	struct CraftStartMessage
	{
		std::string recipeId;
		uint32_t    durationTicks = 0;
	};

	/// Server → client: crafting completed — output items granted via InventoryDelta (M36.2/M36.3).
	struct CraftCompleteMessage
	{
		std::string recipeId;
		uint8_t     skillGained   = 0; ///< 1 when the player gained a skill point this craft.
		uint32_t    newSkillLevel = 0;
		/// M36.3 — quality tier rolled for this craft (0=Normal, 1=Uncommon, 2=Rare, 3=Epic).
		uint8_t     qualityTier  = 0;
	};

	/// Client → server: cancel the current crafting session (M36.2).
	struct CraftCancelRequestMessage
	{
		uint32_t clientId = 0;
	};

	/// Server → client: crafting session was cancelled (M36.2).
	struct CraftCancelledMessage
	{
		std::string recipeId;
	};

	std::vector<std::byte> EncodeLearnProfessionRequest(const LearnProfessionRequestMessage& message);
	bool DecodeLearnProfessionRequest(std::span<const std::byte> packet, LearnProfessionRequestMessage& outMessage);

	std::vector<std::byte> EncodeProfessionUpdate(const ProfessionUpdateMessage& message);
	bool DecodeProfessionUpdate(std::span<const std::byte> packet, ProfessionUpdateMessage& outMessage);

	std::vector<std::byte> EncodeCraftRecipeListRequest(const CraftRecipeListRequestMessage& message);
	bool DecodeCraftRecipeListRequest(std::span<const std::byte> packet, CraftRecipeListRequestMessage& outMessage);

	std::vector<std::byte> EncodeCraftRecipeListResult(const CraftRecipeListResultMessage& message);
	bool DecodeCraftRecipeListResult(std::span<const std::byte> packet, CraftRecipeListResultMessage& outMessage);

	std::vector<std::byte> EncodeCraftRequest(const CraftRequestMessage& message);
	bool DecodeCraftRequest(std::span<const std::byte> packet, CraftRequestMessage& outMessage);

	std::vector<std::byte> EncodeCraftStart(const CraftStartMessage& message);
	bool DecodeCraftStart(std::span<const std::byte> packet, CraftStartMessage& outMessage);

	std::vector<std::byte> EncodeCraftComplete(const CraftCompleteMessage& message);
	bool DecodeCraftComplete(std::span<const std::byte> packet, CraftCompleteMessage& outMessage);

	std::vector<std::byte> EncodeCraftCancelRequest(const CraftCancelRequestMessage& message);
	bool DecodeCraftCancelRequest(std::span<const std::byte> packet, CraftCancelRequestMessage& outMessage);

	std::vector<std::byte> EncodeCraftCancelled(const CraftCancelledMessage& message);
	bool DecodeCraftCancelled(std::span<const std::byte> packet, CraftCancelledMessage& outMessage);
}
