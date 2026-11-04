#pragma once
// ADM.1 — État « maître de jeu » d'un joueur connecté (shard).
//
// Porté par ConnectedClient::gm et modifié par les commandes de jeu (/speed,
// /fly, /freeze… puis /invincible, /invisible, /mount dans les tickets suivants).
// Il n'est PAS persisté : une reconnexion remet tout à zéro (choix de sécurité :
// un pouvoir de MJ oublié ne survit pas à la session).
//
// Le client reçoit les drapeaux et le multiplicateur de vitesse par le message
// GmStateUpdate (MessageKind 104) ; le shard reste autoritaire (vitesse maximale
// acceptée, hauteur libre en vol, gel).
//
// Module pur (en-tête seul) : testé par gm_player_state_tests.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace engine::server::gm
{
	/// Bits de GmStateUpdate::flags (wire, ne pas renuméroter).
	enum GmStateFlag : uint32_t
	{
		kGmFlagFly = 1u << 0,        ///< vol libre (pas de gravité, Espace / Accroupi = monter / descendre).
		kGmFlagFrozen = 1u << 1,     ///< gelé par un MJ : aucun déplacement possible.
		kGmFlagInvincible = 1u << 2, ///< ADM.3 : ne subit aucun dégât.
		kGmFlagInvisible = 1u << 3,  ///< ADM.3 : absent des snapshots des autres joueurs.
		kGmFlagNoCost = 1u << 4,     ///< ADM.3 : sorts sans coût ni temps de recharge.
		kGmFlagMounted = 1u << 5,    ///< ADM.5 : monture (vitesse × mountSpeedMultiplier).
	};

	/// Bornes du multiplicateur de vitesse de /speed.
	inline constexpr float kGmMinSpeedMultiplier = 0.1f;
	inline constexpr float kGmMaxSpeedMultiplier = 10.0f;
	/// Multiplicateur d'une monture (ADM.5) ; borné comme /speed.
	inline constexpr float kGmDefaultMountMultiplier = 1.6f;

	/// Une position mémorisée (retour arrière, rappel, favori).
	struct GmSavedPosition
	{
		bool valid = false;
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float yawRadians = 0.0f;
		/// Seconde carte (Les Enfers) — zone au moment de la mémorisation (cellule :
		/// donne la carte) ; 0 = inconnue (même carte que le joueur).
		uint32_t zoneId = 0;
	};

	struct GmPlayerState
	{
		float speedMultiplier = 1.0f;     ///< /speed (1 = normal).
		float mountSpeedMultiplier = kGmDefaultMountMultiplier;
		bool flying = false;              ///< /fly
		bool frozen = false;              ///< /freeze (posé par un autre MJ)
		bool invincible = false;          ///< ADM.3
		bool invisible = false;           ///< ADM.3
		bool noCost = false;              ///< ADM.3
		bool mounted = false;             ///< ADM.5
		/// /back : position d'avant le dernier déplacement MJ de ce joueur.
		GmSavedPosition back;
		/// /recall : position d'avant la dernière convocation (/summon) par un MJ.
		GmSavedPosition recall;

		/// Multiplicateur réellement appliqué à la vitesse (0 si gelé).
		float EffectiveSpeedMultiplier() const
		{
			if (frozen)
				return 0.0f;
			float m = speedMultiplier;
			if (mounted)
				m *= mountSpeedMultiplier;
			return std::clamp(m, kGmMinSpeedMultiplier, kGmMaxSpeedMultiplier * kGmDefaultMountMultiplier);
		}

		/// Drapeaux du wire (GmStateUpdate::flags).
		uint32_t Flags() const
		{
			uint32_t f = 0u;
			if (flying) f |= kGmFlagFly;
			if (frozen) f |= kGmFlagFrozen;
			if (invincible) f |= kGmFlagInvincible;
			if (invisible) f |= kGmFlagInvisible;
			if (noCost) f |= kGmFlagNoCost;
			if (mounted) f |= kGmFlagMounted;
			return f;
		}

		/// Vrai si l'état diffère de l'état neutre (rien à signaler au client sinon).
		bool IsActive() const
		{
			return Flags() != 0u || std::fabs(speedMultiplier - 1.0f) > 1e-4f;
		}
	};

	/// Multiplicateur de /speed lu dans le texte : borné à [0,1 ; 10], nullopt-like
	/// (valeur négative) si le texte n'est pas un nombre strictement positif.
	inline float ClampSpeedMultiplier(double requested)
	{
		if (!std::isfinite(requested) || requested <= 0.0)
			return -1.0f;
		return std::clamp(static_cast<float>(requested), kGmMinSpeedMultiplier, kGmMaxSpeedMultiplier);
	}

	/// Point à \p distanceMeters devant une orientation \p yawRadians (convention du
	/// client : yaw = atan2(dir.x, dir.z), donc avant = (sin yaw, cos yaw)).
	inline void ForwardOffset(float yawRadians, float distanceMeters, float& outDx, float& outDz)
	{
		outDx = std::sin(yawRadians) * distanceMeters;
		outDz = std::cos(yawRadians) * distanceMeters;
	}
}
