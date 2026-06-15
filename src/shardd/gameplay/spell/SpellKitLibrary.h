#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// Combat SP3 — type d'effet d'un sort (cf. proposition de kits validée).
	/// Tous les types sont résolus côté serveur ; le client ne lit que les
	/// métadonnées d'affichage (nom, coût, cooldown, slot).
	enum class SpellEffectType : uint8_t
	{
		DirectDamage = 0,
		DamageOverTime = 1,
		DirectHeal = 2,
		HealOverTime = 3,
		BuffDamagePercent = 4,
		DebuffDamageTakenPercent = 5,
		TauntThreatMult = 6,
		SlowMobPercent = 7,
		ThreatReducePercent = 8,
		/// SP-C — réduction des dégâts entrants sur l'entité qui subit
		/// (appliquée dans ApplyAuraDamageModifiers côté cible). Analogue à
		/// DebuffDamageTakenPercent mais en signe inverse (réduction, pas amplification).
		DamageReductionPercent = 9,
	};

	/// Combat SP3 — type de cible d'un sort.
	enum class SpellTargetKind : uint8_t
	{
		SingleEnemy = 0,
		SingleAlly = 1,
		SelfOnly = 2,
		AreaAroundSelf = 3,
	};

	/// Combat SP3 — un effet d'un sort (un sort peut en porter plusieurs,
	/// ex. pisteur « Morsure du piege » = DoT + ralentissement).
	struct SpellEffectDef
	{
		SpellEffectType type = SpellEffectType::DirectDamage;
		/// Multiplicateur de la stat `damage` du casteur (DirectDamage/DoT/Heal/HoT/Taunt).
		float mult = 0.0f;
		/// Pourcentage (Buff/Debuff/Slow/ThreatReduce).
		float percent = 0.0f;
		/// Période de tick en ms (DoT/HoT).
		uint32_t tickPeriodMs = 0;
		/// Durée de l'aura en ms (DoT/HoT/Buff/Debuff/Slow).
		uint32_t durationMs = 0;
		/// HoT exprimé en % des PV max par tick (tank « Second souffle ») ;
		/// 0 = utiliser `mult` × damage du casteur.
		float percentMaxHpPerTick = 0.0f;
	};

	/// Combat SP3 — un sort d'un kit de profil (immuable après chargement).
	struct SpellDef
	{
		std::string spellId;
		std::string name;
		/// Slot de la barre d'action [1,4] — unique au sein du kit.
		uint32_t slot = 1;
		uint32_t castTimeMs = 0;
		/// 0 autorisé (sort spammable, ex. healer « Soin rapide »).
		uint32_t cooldownMs = 0;
		/// Coût = resourceCostPercent × ressource max du joueur / 100.
		uint32_t resourceCostPercent = 0;
		/// 0 = mêlée → le serveur substitue la portée d'auto-attaque.
		float rangeMeters = 0.0f;
		SpellTargetKind targetType = SpellTargetKind::SingleEnemy;
		/// Rayon AoE (mètres) — requis > 0 si targetType == AreaAroundSelf.
		float areaRadiusMeters = 0.0f;
		std::vector<SpellEffectDef> effects;
	};

	/// Combat SP3 — bibliothèque serveur des kits de sorts par profil, résolue
	/// depuis `paths.content` (`gameplay/spells/*.json`). Politique stricte
	/// (pattern CreatureArchetypeLibrary) : fichier illisible ou entrée invalide
	/// = échec d'Init, le shard ne boote pas avec des kits corrompus.
	class SpellKitLibrary final
	{
	public:
		/// Capture la config utilisée pour résoudre les JSON des kits.
		explicit SpellKitLibrary(const engine::core::Config& config);

		/// Charge et valide tous les `gameplay/spells/*.json` du content root.
		/// Idempotent (warn + true si déjà initialisé). Retourne false si aucun
		/// kit n'est chargé ou si l'un d'eux est invalide.
		bool Init();

		/// Retourne le kit d'un profil (sorts triés par slot), ou nullptr.
		const std::vector<SpellDef>* FindKit(std::string_view profile) const;

		/// Retourne un sort d'un kit, ou nullptr (profil ou sort inconnu).
		const SpellDef* FindSpell(std::string_view profile, std::string_view spellId) const;

		/// Nombre de profils chargés (0 avant Init).
		size_t KitCount() const { return m_kits.size(); }

		/// Variante testable sans I/O : parse et valide le JSON d'UN kit et
		/// l'insère dans la bibliothèque. outError renseigné en cas d'échec.
		bool LoadKitFromText(std::string_view jsonText, std::string& outError);

	private:
		engine::core::Config m_config;
		std::unordered_map<std::string, std::vector<SpellDef>> m_kits;
		bool m_initialized = false;
	};
}
