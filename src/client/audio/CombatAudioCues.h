#pragma once

// SND.5 — Sons de combat et de sorts déclenchés par CHANGEMENT D'ÉTAT.
//
// Même principe que les sons d'interface (UiAudioCues, SND.3) : Engine prend à
// chaque image une « photo » de l'état de combat répliqué (journal de combat,
// barre d'incantation, PV des entités, auras du joueur) ; DiffCombatAudio()
// compare deux photos successives et renvoie les sons à jouer. Aucun code
// réseau ni de rendu n'est touché, et rien ne joue en double.
//
// Règles :
//   - coup reçu par une entité (nouvelle entrée du journal) : son d'impact à la
//     position de la cible, choisi selon l'attaquant (profil de classe du
//     joueur, ou créature) ; + « combat_crit » sur un critique ; « combat_whoosh »
//     sur un raté ; « combat_player_hurt » (2D) quand le joueur encaisse ;
//   - début d'incantation : « spell_cast » ; fin : « spell_release » si la durée
//     est écoulée (≥ 85 %), sinon « spell_fizzle » (interrompu) ;
//   - entité qui tombe à 0 PV : « combat_death » à sa position ; joueur à 0 PV :
//     « player_death » (2D) ;
//   - nouvelle aura sur le joueur : « aura_buff » (soin, bienfait) ou
//     « aura_debuff » (dégâts sur la durée, malédiction, ralentissement).
//
// Module PUR : testé par combat_audio_cues_tests.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::audio
{
	namespace combat_sounds
	{
		inline constexpr std::string_view kHitBlade = "combat_hit_blade";
		inline constexpr std::string_view kHitBlunt = "combat_hit_blunt";
		inline constexpr std::string_view kHitArrow = "combat_hit_arrow";
		inline constexpr std::string_view kHitMagic = "combat_hit_magic";
		inline constexpr std::string_view kHitClaw = "combat_hit_claw";
		inline constexpr std::string_view kCrit = "combat_crit";
		inline constexpr std::string_view kWhoosh = "combat_whoosh";
		inline constexpr std::string_view kPlayerHurt = "combat_player_hurt";
		inline constexpr std::string_view kDeath = "combat_death";
		inline constexpr std::string_view kPlayerDeath = "player_death";
		inline constexpr std::string_view kSpellCast = "spell_cast";
		inline constexpr std::string_view kSpellRelease = "spell_release";
		inline constexpr std::string_view kSpellFizzle = "spell_fizzle";
		inline constexpr std::string_view kAuraBuff = "aura_buff";
		inline constexpr std::string_view kAuraDebuff = "aura_debuff";
		inline constexpr std::string_view kHeartbeat = "combat_heartbeat";
	}

	/// Une entrée du journal de combat (miroir de UICombatLogEntry).
	struct CombatAudioHit
	{
		uint64_t sequence = 0;
		uint64_t attackerId = 0;
		uint64_t targetId = 0;
		bool playerAttacker = false;
		bool playerTarget = false;
		bool crit = false;
		bool miss = false;
	};

	/// Photo de l'état de combat utile au son.
	struct CombatAudioSnapshot
	{
		bool valid = false;
		uint64_t playerId = 0;
		/// Profil de classe du joueur local ("melee", "lanceur", "distance"…).
		std::string playerProfile;
		uint32_t playerHealth = 0;
		uint32_t playerMaxHealth = 0;
		std::vector<CombatAudioHit> hits;  ///< journal de combat (ordre quelconque)
		/// PV courants des entités distantes visibles.
		std::unordered_map<uint64_t, uint32_t> entityHealth;
		/// Entités distantes qui sont des créatures (archetypeId ≠ 0).
		std::unordered_map<uint64_t, bool> entityIsCreature;
		/// Incantation du joueur.
		bool castActive = false;
		uint64_t castStartNs = 0;
		uint32_t castDurationMs = 0;
		/// Auras du joueur : spellId → type d'effet serveur (SpellEffectType).
		std::unordered_map<std::string, uint8_t> playerAuras;
	};

	/// Un son à jouer. `entityId` = 0 → son 2D (joueur local / interface) ;
	/// sinon son 3D à la position de cette entité.
	struct CombatAudioCue
	{
		std::string_view soundId;
		uint64_t entityId = 0;
		float volume = 1.0f;
	};

	/// Son d'impact selon le profil de classe de l'attaquant ("" = créature).
	std::string_view HitSoundForProfile(std::string_view profile);

	/// Vrai si un type d'effet d'aura (SpellEffectType, src/shardd/gameplay/spell/
	/// SpellKitLibrary.h) est un bienfait pour celui qui le porte. Néfastes :
	/// dégâts sur la durée (1), dégâts subis augmentés (5), ralentissement (7).
	/// Tout le reste (soin sur la durée, bonus, réduction de dégâts, PV max,
	/// vitesse…) est un bienfait.
	bool IsBeneficialAura(uint8_t effectType);

	/// Sons à jouer entre deux photos. `nowNs` = horloge monotone courante
	/// (comparée à castStartNs). Au plus `maxCues` sons (les plus importants
	/// d'abord : mort du joueur, coups reçus, coups portés, sorts, auras).
	std::vector<CombatAudioCue> DiffCombatAudio(const CombatAudioSnapshot& prev, const CombatAudioSnapshot& cur,
		uint64_t nowNs, size_t maxCues = 6);

	/// Battement de cœur : vrai quand le joueur vit sous 25 % de ses PV.
	bool LowHealthHeartbeat(uint32_t health, uint32_t maxHealth);
}
