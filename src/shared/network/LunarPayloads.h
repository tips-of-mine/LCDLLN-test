#pragma once
// Wire payloads pour le systeme de phase lunaire (opcodes 192-194).
// 16 phases (0..15), cycle de 14 jours reels.
//
// Format wire little-endian (cf. autres *Payloads du repo). Aucun champ
// string dans cette serie : tous les champs sont uint8 / uint64 / float
// (uint32 bits memcpy). Pas de PacketBuilder helpers — le LunarHandler
// construit le packet final via BuildPushPacket (pour 194) ou un
// PacketBuilder (pour 193).
//
// V1 : pas de hook event lune <-> GameEvents (CurrentPhase est expose
// par LunarHandler). La pousse vers GameEventManager viendra via une
// PR future quand le seed des events thematiques (Lune Noire) sera
// branche.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::network::lunar
{
	/// Status response Lunar : OK ou Unauthorized (pas de session valide).
	enum class LunarStatus : uint8_t
	{
		Ok           = 0,
		Unauthorized = 1,
	};

	// === Request: client demande l'etat lunaire actuel (vide) ===
	struct LunarStateRequest
	{
		// Empty — payload vide (size == 0).
	};

	// === Response: master envoie phase + cycle info ===
	struct LunarStateResponse
	{
		LunarStatus status         = LunarStatus::Ok;
		uint8_t     phase          = 0;     ///< 0..15
		float       illumination   = 0.0f;  ///< 0..1
		uint64_t    cycleStartMs   = 0;     ///< timestamp ms du debut de cycle
		uint64_t    cycleDurationMs = 0;    ///< duree totale d'un cycle (ms)
	};

	// === Push notification: changement de phase ===
	struct LunarPhaseChangeNotification
	{
		uint8_t  newPhase        = 0;
		float    newIllumination = 0.0f;
		uint64_t nextChangeTsMs  = 0;
	};

	/// Build* : ecrit le payload binaire dans \p out (clear puis push_back).
	void BuildLunarStateRequestPayload(std::vector<uint8_t>& out);
	void BuildLunarStateResponsePayload(const LunarStateResponse& msg, std::vector<uint8_t>& out);
	void BuildLunarPhaseChangeNotificationPayload(const LunarPhaseChangeNotification& msg, std::vector<uint8_t>& out);

	/// Parse* : retourne true si le buffer est valide (taille exacte attendue).
	/// Reject-short et reject-extra : la taille du payload doit egaler exactement
	/// la somme des champs encodes (1 + 1 + 4 + 8 + 8 = 22 pour StateResponse,
	/// 1 + 4 + 8 = 13 pour PhaseChangeNotification, 0 pour StateRequest).
	bool ParseLunarStateRequestPayload(const uint8_t* data, size_t size, LunarStateRequest& out);
	bool ParseLunarStateResponsePayload(const uint8_t* data, size_t size, LunarStateResponse& out);
	bool ParseLunarPhaseChangeNotificationPayload(const uint8_t* data, size_t size, LunarPhaseChangeNotification& out);
}
