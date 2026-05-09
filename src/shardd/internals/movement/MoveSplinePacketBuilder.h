#pragma once
// CMANGOS.04 (Phase 2.04c) — MoveSplinePacketBuilder : encodage /
// decodage pur du payload `MonsterMove` (un MoveSpline lance par le
// shard, broadcaste aux clients en interest set).
//
// **Pur** dans cette PR : pas encore d'integration ServerProtocol
// (pas de MessageKind alloue, pas de header magic+version+kind, pas
// de bump kProtocolVersion). C'est le builder en isolation testable
// — l'integration wire viendra dans une PR finale dediee qui bumpera
// la version + ajoutera les MessageKind values + cablera dans
// ServerProtocol.cpp.
//
// **Layout** binaire little-endian, sans padding :
//
//   uint64 entityGuid          // ObjectGuid de l'entite (cf. CMANGOS.02)
//   uint32 splineId            // monotone strictement croissant
//   uint32 flags               // MoveSplineFlag bitmask
//   float  velocity            // unites/sec
//   uint16 pointCount          // nombre de control points (max 256)
//   pointCount × { float x, y, z }
//
// Empty/invalide rejete au decode. Le serveur emet une seule fois ;
// le client interpole jusqu'a la prochaine update (cf. audit §8 :
// pour un trajet de 100m a 10 m/s, **1 paquet** au lieu de 100+ en
// streaming positions).
//
// **Hors scope** : MonsterMoveStop (separation logique), TeleportAck
// (separation logique). Viendront avec leurs propres builders puis
// l'integration wire commune.

#include "engine/server/shard/movement/MoveSpline.h"
#include "engine/server/shard/movement/MoveSplineFlag.h"
#include "engine/server/shard/movement/MovementTypedefs.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine::server::shard::movement
{
	/// Limite : 256 control points par paquet (UDP MTU = 1500 bytes max ;
	/// header 14 + payload max ~1486 ; chaque point = 12 bytes ; payload
	/// fixe = 22 bytes → max ~120 points pour rester sous MTU. Cap a 256
	/// pour validation defensive ; un paquet bien dimensionne tient en
	/// 1 MTU). Le caller decoupe les chemins longs en plusieurs paquets.
	inline constexpr uint16_t kMaxMonsterMovePoints = 256;

	struct MonsterMovePayload
	{
		uint64_t       entityGuid = 0;
		uint32_t       splineId   = 0;
		MoveSplineFlag flags      = MoveSplineFlag::None;
		float          velocity   = 0.0f;
		std::vector<Vec3> points;
	};

	enum class MonsterMoveDecodeResult : uint8_t
	{
		OK = 0,
		BufferTooSmall   = 1,
		TooManyPoints    = 2,
		PointCountMismatch = 3,
	};

	/// Encode un MonsterMove vers un buffer binaire (sans header ni
	/// MessageKind — pure payload, l'integration wire ajoutera le
	/// header standard).
	std::vector<uint8_t> EncodeMonsterMove(const MonsterMovePayload& msg);

	/// Decode un payload vers \p out. Retourne `OK` ou un code d'erreur.
	MonsterMoveDecodeResult DecodeMonsterMove(std::span<const uint8_t> in,
		MonsterMovePayload& out);

	/// Helper : extrait un MonsterMovePayload depuis un MoveSpline runtime
	/// (utile pour wirer cote shard une fois l'integration prete).
	MonsterMovePayload BuildPayloadFromSpline(uint64_t entityGuid, const MoveSpline& spline);
}
