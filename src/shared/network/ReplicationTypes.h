#pragma once

#include <cstdint>
#include <string>

namespace engine::server
{
	/// Stable 64-bit entity identifier required by the replication protocol.
	using EntityId = uint64_t;

	/// Replicated state flag set when an entity reached 0 HP.
	inline constexpr uint32_t kEntityStateDead = 1u << 0;

	/// Métiers SP1 — plage d'archetypeId réservée aux NODES DE RÉCOLTE répliqués
	/// (archetypeId = base + typeId du node). Jamais un archétype de créature :
	/// le client rend un label flottant [E] au lieu d'un mesh, et le ciblage
	/// combat les ignore. `kEntityStateDead` sur un node = épuisé (grisé).
	inline constexpr uint32_t kGatheringNodeArchetypeBase = 1000000u;

	/// Minimal authoritative stats component shared by players and mobs.
	struct StatsComponent
	{
		uint32_t currentHealth = 0;
		uint32_t maxHealth = 0;
	};

	/// Minimal combat component used by the authoritative attack validation.
	/// Combat SP2 — accuracy/critRate/critMult alimentent les jets de précision
	/// et de critique (cf. AttackResolver). Composant purement serveur : ces
	/// champs ne transitent JAMAIS sur le wire (pas d'impact protocole).
	struct CombatComponent
	{
		uint32_t damagePerHit = 0;
		float attackRangeMeters = 0.0f;
		uint32_t cooldownTicks = 0;
		uint32_t nextAttackTick = 0;
		/// Précision en % [0,100] — 100 = ne rate jamais (défaut conservateur).
		float accuracy = 100.0f;
		/// Taux de critique en % [0,100].
		float critRate = 0.0f;
		/// Multiplicateur de dégâts critiques (plancher 1.0 à la résolution).
		float critMult = 1.5f;
	};

	/// Minimal item stack used by loot bags and player inventory.
	struct ItemStack
	{
		uint32_t itemId = 0;
		uint32_t quantity = 0;
	};

	/// Minimal replicated entity state shared by spawn and snapshot messages.
	struct EntityState
	{
		float positionX = 0.0f;
		float positionY = 0.0f;
		float positionZ = 0.0f;
		float yawRadians = 0.0f;
		float velocityX = 0.0f;
		float velocityY = 0.0f;
		float velocityZ = 0.0f;
		uint32_t currentHealth = 0;
		uint32_t maxHealth = 0;
		uint32_t stateFlags = 0;
	};

	/// Spawn payload for one entity entering a client's interest set.
	struct SpawnEntity
	{
		EntityId entityId = 0;
		uint32_t archetypeId = 0;
		EntityState state{};
	};

	/// Despawn payload for one entity leaving a client's interest set.
	struct DespawnEntity
	{
		EntityId entityId = 0;
	};

	/// TD.8 — état d'animation répliqué (client → shard → autres clients). Sans ce champ,
	/// le client distant dérive Idle/Walk de la seule vélocité serveur : les emotes (/dance),
	/// roulades, run/sprint/attaque restaient invisibles aux autres joueurs (seul le saut,
	/// via le déplacement vertical de la position, semblait visible). Les valeurs sont
	/// alignées 1:1 (même ordre) sur `Engine::AvatarLocomotionState`, mais le client fait
	/// un mapping **explicite** (`ToWireAnimState` / `FromWireAnimState`, Engine.cpp) pour
	/// ne pas dépendre de l'ordre de l'enum côté rendu.
	enum class AvatarAnimState : uint8_t
	{
		Idle = 0,
		StartWalking,
		Walk,
		WalkBack,
		Run,
		Sprint,
		CrouchIdle,
		CrouchWalk,
		Roll,
		Emote,
		Attack,
		Cast,
		Interact,
		Punch,
		Jump,
		Fall,
		Land,
		SwimIdle,
		SwimForward
	};

	/// Snapshot payload for one already-spawned entity state update.
	/// TD.4 — `playerClientId` ≠ 0 quand l'entité représente un **joueur connecté** ;
	/// vaut 0 pour les mobs / loot bags. Sert au client à afficher une plaque de nom
	/// au-dessus des avatars distants (cf. `Engine::Update.*Hud`).
	/// TD.5 — `characterName` porte le nom de personnage choisi par le joueur ; non vide
	/// uniquement pour les joueurs (mobs/lootbags ont la chaîne vide). Wire-bump v5→v6 :
	/// chaque entité du Snapshot est suivie d'une chaîne préfixée u16 (taille variable).
	/// TD.6 — `gender` porte le genre du personnage ("male"/"female", cf. migration 0067).
	/// Permet au client de sélectionner le bon mesh skinné (Male_Ranger vs Female_Ranger)
	/// pour le rendu de l'avatar distant. Chaîne vide pour les mobs/lootbags. Wire-bump
	/// v6→v7 : ajout d'une seconde chaîne préfixée u16 après `characterName`.
	struct SnapshotEntity
	{
		EntityId entityId = 0;
		EntityState state{};
		uint32_t playerClientId = 0;
		std::string characterName;
		std::string gender;
		/// TD.8 — état d'animation courant du joueur (Idle pour les mobs/lootbags).
		/// Wire-bump v7→v8 : 1 octet ajouté après le genre dans chaque entité du Snapshot.
		AvatarAnimState animationState = AvatarAnimState::Idle;
		/// Combat SP1 — archétype de créature (0 = joueur ou loot bag). Permet au
		/// client de résoudre nom/niveau/mesh/échelle dans son CreatureCatalog.
		/// Wire-bump v8→v9 : u32 ajouté après animationState dans chaque entité.
		uint32_t archetypeId = 0;
	};
}
