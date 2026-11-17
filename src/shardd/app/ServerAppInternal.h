#pragma once

// ServerApp — en-tête INTERNE partagé par les fichiers ServerApp*.cpp
// (ServerApp.cpp, ServerAppConnection.cpp, ServerAppCombat.cpp…) : inclusions
// communes, constantes de gameplay par défaut et petits utilitaires
// (anciennement dans l'espace de noms anonyme de ServerApp.cpp ; passés
// `inline` dans engine::server::sa_detail, comportement inchangé).
// Ne pas inclure en dehors de src/shardd/app/ServerApp*.cpp.

#include "src/shardd/app/ServerApp.h"
#include "src/shared/mail/MailTransfer.h"

#include "src/shared/anniversary/CakeItemToken.h" // SP3 anniversaires (2026-07-18)
#include "src/shardd/gameplay/items/ConsumableEffectLibrary.h" // Roadmap-3 (2026-07-19)
#include "src/shared/core/Log.h"
#include "src/shared/net/ChatEmotes.h"
#include "src/shared/net/ChatSystem.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shardd/gameplay/chat/ChatCommandParser.h"
#include "src/shared/world/AdmittedCharacterRegistry.h"
#include "src/shared/db/CharacterOwnership.h" // ETAPE1.2 — un seul propriétaire par personnage
#include "src/shared/net/ChatSanitizer.h" // ETAPE5.1 — nettoyage du chat de proximité
#include "src/shardd/gameplay/character/AutosaveSchedule.h" // ETAPE2.3 — sauvegardes étalées
#include "src/shardd/gameplay/character/CharacterOperationId.h" // ETAPE2.4 — opérations à deux personnages
#include "src/shared/network/ServerProtocol.h"


// Level-up runtime — courbe XP -> niveau (séparée de la boucle réseau, pure et testable).
#include "src/shardd/gameplay/character/LevelProgression.h"
#include "src/shared/formulas/Formulas.h"

// Combat SP2 — résolveur d'attaque pur (précision / critique, RNG injecté).
#include "src/shardd/gameplay/combat/AttackResolver.h"

// Respawn faction-aware — règles d'éligibilité cimetière par zone (header-only, partagé).
#include "src/shared/world/RespawnRules.h"

// TA.4 — pont position : compilé uniquement pour la cible shard Linux (SHARD_POSITION_DB),
// pas pour le build Windows (ServerApp.cpp est partagé). Macro dédié plutôt qu'ENGINE_HAS_MYSQL
// pour ne PAS basculer les autres systèmes gameplay du shard en mode DB.
#if defined(SHARD_POSITION_DB)
#	include "src/shared/db/ConnectionPool.h"
#	include "src/shared/db/DbHelpers.h"
#	include <mysql.h>
#	include <cstdio>
#	include <cstdlib>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <sstream>
#include <format>
#include <functional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace engine::server::sa_detail
{
		/// Fixed authoritative combat numbers for the MVP ticket.
		inline constexpr uint32_t kDefaultPlayerHealth = 100;
		inline constexpr uint32_t kDefaultMobHealth = 60;
		inline constexpr uint32_t kDefaultPlayerDamage = 10;
		inline constexpr uint32_t kDefaultMobDamage = 6;
		inline constexpr uint32_t kDefaultLootBagArchetypeId = 200;
		inline constexpr float kDefaultAttackRangeMeters = 4.0f;
		/// Accuracy de repli pour les profils de mêlée. Le moteur de stats
		/// (`CharacterStatsEngine`) met délibérément `accuracy = 0` pour les classes
		/// de mêlée (range 0) — dans SON domaine, 0 signifie « accuracy non
		/// applicable à la mêlée ». Mais `ResolveAttackRoll` interprète `accuracy = 0`
		/// comme 0 % de chance de toucher → 100 % de ratés. On substitue donc une
		/// accuracy pleine (100 = touche toujours, la mêlée ne fait pas de jet de
		/// précision) au moment de peupler le composant combat. Cf.
		/// `ApplyDerivedCombatStats` (miroir du fallback de portée juste au-dessus).
		inline constexpr float kDefaultMeleeAccuracy = 100.0f;
		inline constexpr float kDefaultMobLeashDistanceMeters = 24.0f;
		inline constexpr float kDefaultMobMoveSpeedMetersPerSecond = 3.0f;
		inline constexpr float kDefaultMobPatrolDistanceMeters = 6.0f;
		inline constexpr float kDefaultLootPickupRangeMeters = 3.0f;

		/// M32.2 — Base XP granted per mob kill (shared among party members in range).
		inline constexpr uint32_t kBaseXpPerMobKill = 10;

		/// M32.2 — Maximum range (metres) for party XP and loot sharing.
		inline constexpr float kPartyShareRangeMeters = 40.0f;

		/// TG.2 — endpoint compacté en uint64 (address sur les 32 bits hauts + port sur les 16
		/// bits bas) pour servir de clé aux index O(1) sur m_clients (cf. ServerApp.h).
		/// Le port étant 16 bits, le décalage n'écrase rien et la clé reste injective ; les
		/// 16 bits du milieu (vides) ne posent pas de risque de collision.
		inline uint64_t PackEndpointKey(const Endpoint& endpoint)
		{
			return (static_cast<uint64_t>(endpoint.address) << 16) | static_cast<uint64_t>(endpoint.port);
		}

		/// M35.2 — Maximum stack size per shop buy/sell request (anti-spam).
		inline constexpr uint32_t kMaxShopQuantityPerRequest = 10000u;

		/// M35.4 — merge persisted mailbox into wallet + bags before spawning the character.
		inline void MergePersistedMailboxIntoState(PersistedCharacterState& state)
		{
			const uint64_t totalGold = static_cast<uint64_t>(state.gold) + static_cast<uint64_t>(state.mailboxGold);
			state.gold = static_cast<uint32_t>(
				std::min<uint64_t>(totalGold, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
			state.mailboxGold = 0;
			for (const ItemStack& m : state.mailboxItems)
			{
				if (m.itemId == 0u || m.quantity == 0u)
				{
					continue;
				}
				bool merged = false;
				for (ItemStack& inv : state.inventory)
				{
					if (inv.itemId == m.itemId)
					{
						const uint64_t q = static_cast<uint64_t>(inv.quantity) + static_cast<uint64_t>(m.quantity);
						inv.quantity = static_cast<uint32_t>(
							std::min<uint64_t>(q, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
						merged = true;
						break;
					}
				}
				if (!merged)
				{
					state.inventory.push_back(m);
				}
			}
			state.mailboxItems.clear();
		}

		/// Parse \p targetId of the form `vendor:<decimal_id>` for M35.2 shop talk routing.
		inline bool TryParseVendorTarget(std::string_view targetId, uint32_t& outVendorId)
		{
			constexpr std::string_view kPrefix = "vendor:";
			if (!targetId.starts_with(kPrefix))
			{
				return false;
			}
			const std::string_view rest = targetId.substr(kPrefix.size());
			if (rest.empty())
			{
				return false;
			}
			uint64_t acc = 0;
			for (const char ch : rest)
			{
				if (ch < '0' || ch > '9')
				{
					return false;
				}
				acc = acc * 10ull + static_cast<uint64_t>(ch - '0');
				if (acc > 0xFFFFFFFFull)
				{
					return false;
				}
			}
			if (acc == 0ull)
			{
				return false;
			}
			outVendorId = static_cast<uint32_t>(acc);
			return true;
		}

		/// Clamp a signed config integer into an unsigned 16-bit range.
		inline uint16_t ClampToU16(int64_t value, uint16_t minValue, uint16_t maxValue)
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
		inline bool ContainsEntityId(const std::vector<EntityId>& entityIds, EntityId entityId)
		{
			return std::find(entityIds.begin(), entityIds.end(), entityId) != entityIds.end();
		}

		/// Build the fixed MVP combat component using the current server tick rate.
		inline CombatComponent BuildDefaultCombatComponent(uint16_t tickHz, uint32_t damagePerHit)
		{
			CombatComponent component{};
			component.damagePerHit = damagePerHit;
			component.attackRangeMeters = kDefaultAttackRangeMeters;
			component.cooldownTicks = std::max<uint32_t>(1u, static_cast<uint32_t>(tickHz / 2u));
			component.nextAttackTick = 0;
			return component;
		}

		/// Combat SP1 — applique les stats data-driven d'un archétype au mob
		/// fraîchement construit (PV, dégâts, portée, cadence d'attaque en ticks,
		/// XP au kill). \p archetype peut être nullptr (garde défensive : les
		/// références sont validées à l'init) → le mob garde les constantes MVP.
		inline void ApplyArchetypeStatsToMob(MobEntity& mob, const CreatureArchetype* archetype, uint16_t tickHz)
		{
			const uint32_t mobHealth = (archetype != nullptr) ? archetype->hp : kDefaultMobHealth;
			const uint32_t mobDamage = (archetype != nullptr) ? archetype->damage : kDefaultMobDamage;
			mob.stats.currentHealth = mobHealth;
			mob.stats.maxHealth = mobHealth;
			mob.combat = BuildDefaultCombatComponent(tickHz, mobDamage);
			if (archetype != nullptr)
			{
				mob.combat.attackRangeMeters = archetype->rangeMeters;
				// attackPeriodMs → ticks (arrondi au tick supérieur, minimum 1 tick).
				mob.combat.cooldownTicks = std::max<uint32_t>(1u,
					(archetype->attackPeriodMs * static_cast<uint32_t>(tickHz) + 999u) / 1000u);
				mob.xpReward = archetype->xpReward;
				// Combat SP2 — jets de précision/critique data-driven (cf. AttackResolver).
				mob.combat.accuracy = archetype->accuracy;
				mob.combat.critRate = archetype->critRate;
				mob.combat.critMult = archetype->critMult;
			}
		}

		/// Combat SP3 — horloge monotone en millisecondes (référence commune des
		/// cooldowns de sorts, casts et expirations d'auras).
		inline uint64_t NowMonotonicMs()
		{
			return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		}

		/// Combat SP3 — somme des pourcentages des auras d'un type donné (0 si aucune).
		inline float SumAuraPercent(const std::vector<ActiveAura>& auras, SpellEffectType type)
		{
			float total = 0.0f;
			for (const ActiveAura& aura : auras)
			{
				if (aura.type == type)
				{
					total += aura.percent;
				}
			}
			return total;
		}

		/// Combat SP3/SP-C — applique les modificateurs d'auras à un jet de dégâts :
		/// × (1 + buffs de dégâts de l'attaquant) × (1 + dégâts subis de la cible)
		/// × (1 − réduction de dégâts de la cible). La réduction est plafonnée à 0
		/// (un facteur négatif serait un soin involontaire — on refuse de descendre
		/// sous 0).
		inline uint32_t ApplyAuraDamageModifiers(
			uint32_t baseDamage,
			const std::vector<ActiveAura>& attackerAuras,
			const std::vector<ActiveAura>& targetAuras)
		{
			const float attackerBonus   = SumAuraPercent(attackerAuras, SpellEffectType::BuffDamagePercent);
			const float takenBonus      = SumAuraPercent(targetAuras,   SpellEffectType::DebuffDamageTakenPercent);
			// SP-C — réduction des dégâts entrants portée par la cible (DamageReductionPercent).
			// Somme clampée à [0, 100] : jamais de soin involontaire, jamais de sur-réduction.
			const float reductionSum    = SumAuraPercent(targetAuras, SpellEffectType::DamageReductionPercent);
			const float reductionFactor = 1.0f - std::min(100.0f, std::max(0.0f, reductionSum)) / 100.0f;
			const double modified = static_cast<double>(baseDamage)
				* (1.0 + static_cast<double>(attackerBonus) / 100.0)
				* (1.0 + static_cast<double>(takenBonus) / 100.0)
				* static_cast<double>(reductionFactor);
			return static_cast<uint32_t>(std::llround(modified));
		}

		/// Roadmap-2 (2026-07-19) — applique les modificateurs de STATS des
		/// auras au DerivedStats : +% PV max et +% vitesses. Les effets de
		/// dégâts restent gérés à la résolution (ApplyAuraDamageModifiers).
		/// Les multiplicateurs sont plafonnés vers le bas (jamais < 10 % de la
		/// base) pour qu'un débuff extrême ne fige/tue pas le personnage.
		inline void ApplyAuraStatModifiers(const std::vector<ActiveAura>& auras,
			engine::server::gameplay::DerivedStats& d)
		{
			const float hpPct = SumAuraPercent(auras, SpellEffectType::MaxHealthPercent);
			if (hpPct != 0.0f)
			{
				const float mult = std::max(0.1f, 1.0f + hpPct / 100.0f);
				d.hp = std::max<uint32_t>(1u, static_cast<uint32_t>(
					std::llround(static_cast<double>(d.hp) * mult)));
			}
			const float spdPct = SumAuraPercent(auras, SpellEffectType::MoveSpeedPercent);
			if (spdPct != 0.0f)
			{
				const float mult = std::max(0.1f, 1.0f + spdPct / 100.0f);
				d.speedWalk   *= mult;
				d.speedRun    *= mult;
				d.speedSprint *= mult;
			}
		}

		/// Combat SP3 — insère ou rafraîchit une aura (même spellId + même casteur
		/// = refresh de la durée, pas d'empilement en V1).
		/// \return true si l'aura vient d'être AJOUTÉE (nouvelle), false si
		/// c'était un simple refresh — les appelants ne déclenchent le recalcul
		/// de stats (Roadmap-2) que sur un vrai ajout.
		inline bool UpsertAura(std::vector<ActiveAura>& auras, ActiveAura&& aura)
		{
			for (ActiveAura& existing : auras)
			{
				if (existing.spellId == aura.spellId && existing.casterEntityId == aura.casterEntityId)
				{
					existing = std::move(aura);
					return false;
				}
			}
			auras.push_back(std::move(aura));
			return true;
		}

		/// Combat SP2 — copie les stats dérivées du moteur de personnages dans le
		/// composant combat du joueur (enter-world + level-up). range 0 = mêlée
		/// pure → on conserve la portée de mêlée MVP (kDefaultAttackRangeMeters).
		inline void ApplyDerivedCombatStats(ConnectedClient& client, const engine::server::gameplay::DerivedStats& derived)
		{
			client.combat.damagePerHit = derived.damage;
			client.combat.attackRangeMeters = (derived.range > 0.0f) ? derived.range : kDefaultAttackRangeMeters;
			// Mêlée : le moteur de stats donne accuracy 0 (« non applicable »), que
			// ResolveAttackRoll lit comme 0 % de touche → 100 % de ratés. Fallback à
			// une accuracy pleine (la mêlée touche toujours). Cf. kDefaultMeleeAccuracy.
			client.combat.accuracy = (derived.accuracy > 0.0f) ? derived.accuracy : kDefaultMeleeAccuracy;
			client.combat.critRate = derived.critRate;
			client.combat.critMult = derived.critMult;
		}

		/// Return the squared XZ distance used by the range validation.
		inline float DistanceSquaredXZ(float ax, float az, float bx, float bz)
		{
			const float dx = ax - bx;
			const float dz = az - bz;
			return (dx * dx) + (dz * dz);
		}

		/// Return a readable name for one mob AI state.
		inline const char* GetMobAiStateName(MobAiState state)
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
		inline const char* GetDynamicEventStatusName(DynamicEventStatus status)
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
		inline uint64_t NowUnixEpochMsUtc()
		{
			using namespace std::chrono;
			return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
		}

		/// Parse the loot visibility token found in the data file.
		inline bool TryParseLootVisibility(std::string_view text, LootVisibility& outVisibility)
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
