#pragma once

// M100.32 — Interactive Props : enum + struct (GELÉS).
//
// Périmètre strictement borné : porte battante, porte coulissante, fenêtre
// battante, trappe, coffre simple. AUCUN autre type. Toute extension doit
// passer par un futur ticket dédié bumpant `kInteractivesVersion`.
//
// Ce header ne dépend que de Math.h (Vec3) : il est inclus côté client
// (rendu/animation), côté éditeur (panneau de pose) et côté serveur relais
// (uniquement pour `InteractiveType`/id, jamais l'animation).

#include <cstdint>
#include <string>

#include "src/shared/math/Math.h"

namespace engine::world::interactive
{
	/// Types d'objets interactifs supportés. VOLONTAIREMENT FIGÉ à 5 valeurs.
	/// Tout ajout doit être fait par un futur ticket dédié, en bumpant
	/// `kInteractivesVersion` et en versionnant la sérialisation.
	enum class InteractiveType : uint16_t
	{
		DoorHinge   = 0, ///< Porte battante (rotation autour d'un pivot/axe local).
		DoorSliding = 1, ///< Porte coulissante (translation le long de l'axe local).
		WindowHinge = 2, ///< Fenêtre battante (rotation, comme DoorHinge).
		Trapdoor    = 3, ///< Trappe (rotation, pivot au bord).
		ChestSimple = 4  ///< Coffre simple (rotation du couvercle ; pas d'inventaire — hors scope).
		// FIGÉ. Ne rien ajouter ici sans bumper kInteractivesVersion (ticket dédié).
	};

	/// Une instance d'objet interactif posée par l'éditeur monde.
	///
	/// `openAngleDeg` est interprété en degrés pour les types rotatifs
	/// (DoorHinge / WindowHinge / Trapdoor / ChestSimple) et en MÈTRES de
	/// translation pour DoorSliding (déplacement le long de `axisLocal`).
	struct InteractivePropInstance
	{
		uint64_t           id              = 0;          ///< Identifiant unique de l'objet dans la zone.
		InteractiveType    type            = InteractiveType::DoorHinge;
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		float              rotationY       = 0.0f;       ///< Orientation de pose (radians, autour de +Y).
		uint32_t           meshAssetId     = 0;          ///< Hash de l'asset glTF rendu.
		engine::math::Vec3 pivotLocal{ 0.0f, 0.0f, 0.0f }; ///< Pivot de rotation (espace local de l'objet).
		engine::math::Vec3 axisLocal{ 0.0f, 1.0f, 0.0f };  ///< Axe de rotation OU direction de translation (espace local).
		float              openAngleDeg    = 90.0f;      ///< Angle d'ouverture (°) ou translation (m) pour DoorSliding.
		float              animDurationSec = 0.5f;       ///< Durée d'animation open/close (secondes).
		uint8_t            initialState    = 0;          ///< 0 = fermé, 1 = ouvert.
		std::string        audioOpenEvent;               ///< Clé d'évènement audio à l'ouverture (vide = aucun).
		std::string        audioCloseEvent;              ///< Clé d'évènement audio à la fermeture (vide = aucun).
	};

	/// Magic du fichier `instances/interactives.bin` ("INCT" little-endian).
	constexpr uint32_t kInteractivesMagic        = 0x54434E49u;
	/// Version de format. À bumper pour toute évolution du layout/enum.
	constexpr uint32_t kInteractivesVersion       = 1u;
	constexpr uint32_t kInteractivesBuilderVersion = 1u;
	constexpr uint32_t kInteractivesEngineVersion  = 1u;
}
