#pragma once

// M101.1 — Modèle de graphe de routine (cluster M101 : éditeur de routines nodal).
//
// Lib PURE `routine_graph` : aucune dépendance Vulkan/GLFW/réseau, et aucune
// dépendance à `engine_core` (le shard, qui ne linke pas `engine_core`, doit
// pouvoir réutiliser ce modèle pour la traduction PNJ — voir M101.7). Seule
// dépendance externe autorisée : `engine::math::Vec3` (header-only, partagé).
//
// Ce header pose le CONTRAT DE DONNÉES partagé par tout le cluster M101 :
// structs du graphe, enums bornés/versionnés, et les conversions enum<->chaîne
// utilisées par la sérialisation JSON (M101.1) et le schéma (M101.1).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::routine
{
	/// Version du wire JSON / du modèle. Tout changement de format de
	/// sérialisation incrémente cette constante ; `FromJson` rejette proprement
	/// une version supérieure inconnue (erreur, pas de crash).
	inline constexpr uint32_t kRoutineGraphVersion = 1u;

	/// Cible d'un graphe. Discrimine les deux schémas/VM (décision M101 : deux
	/// types de graphes distincts partageant la sérialisation).
	enum class RoutineGraphKind : uint8_t
	{
		NpcRoutine = 0,   ///< StateTree-like (cible A — M101.7, Blocked).
		ZoneEvent  = 1    ///< Blueprint-like (cible B — M101.8).
	};

	/// Direction d'un pin.
	enum class PinDirection : uint8_t { Input = 0, Output = 1 };

	/// Famille d'un pin : flux d'exécution (Exec) ou valeur typée (Data). Cette
	/// distinction est fondamentale (héritée du modèle Blueprint).
	enum class PinKind : uint8_t { Exec = 0, Data = 1 };

	/// Type de donnée porté par un pin Data (ou une propriété).
	enum class RoutineDataType : uint8_t
	{
		None = 0, Bool = 1, Int = 2, Float = 3, Vec3 = 4, String = 5, EntityRef = 6
	};

	/// Type de nœud. Enum BORNÉ ET VERSIONNÉ : plages réservées par cible.
	/// Tout ajout se fait EN FIN de la plage concernée ; ne JAMAIS réordonner ni
	/// recycler une valeur (casse les graphes déjà sérialisés).
	enum class RoutineNodeType : uint16_t
	{
		// --- commun (0..99) ---
		Comment               = 0,

		// --- zone_event / Blueprint-like (100..199) — détaillé en M101.8 ---
		EventOnZoneEnter      = 100,
		EventOnZoneExit       = 101,
		EventOnInteract       = 102,
		BranchIf              = 120,
		ActionOpenInteractive = 130,
		ActionBroadcastSeason = 131,
		ActionBroadcastWeather= 132,

		// --- npc_routine / StateTree-like (200..299) — détaillé en M101.7 ---
		NpcStateRoot          = 200,
		NpcState              = 201,
		SensorPlayerInRange   = 210,   ///< « evaluator » : lit/calcule, ne déclenche pas.
		SensorTimeOfDay       = 211,
		TaskPlayAnim          = 220,   ///< « task » : agit.
		TaskMoveTo            = 221,
		TaskSetEmotion        = 222
	};

	/// Propriété typée d'un nœud (sac clé->valeur). Un seul champ « value » est
	/// pertinent selon `type`.
	struct RoutineProperty
	{
		std::string       key;
		RoutineDataType   type = RoutineDataType::None;
		bool              bValue = false;
		int64_t           iValue = 0;
		float             fValue = 0.0f;
		engine::math::Vec3 vValue{ 0.0f, 0.0f, 0.0f };
		std::string       sValue;   ///< String, ou id textuel pour EntityRef.
	};

	/// Pin d'un nœud (point de connexion).
	struct RoutinePin
	{
		uint32_t        id = 0;          ///< Unique dans le graphe.
		PinDirection    direction = PinDirection::Input;
		PinKind         kind = PinKind::Exec;
		RoutineDataType dataType = RoutineDataType::None; ///< Pertinent si kind == Data.
		std::string     name;
	};

	/// Nœud du graphe.
	struct RoutineNode
	{
		uint32_t        id = 0;          ///< Unique dans le graphe.
		RoutineNodeType type = RoutineNodeType::Comment;
		float           canvasX = 0.0f;  ///< Disposition éditeur uniquement.
		float           canvasY = 0.0f;
		std::vector<RoutinePin>      pins;
		std::vector<RoutineProperty> properties;
	};

	/// Lien orienté pin->pin.
	struct RoutineLink
	{
		uint32_t id = 0;
		uint32_t fromNodeId = 0;
		uint32_t fromPinId = 0;
		uint32_t toNodeId = 0;
		uint32_t toPinId = 0;
	};

	/// Graphe complet.
	struct RoutineGraph
	{
		uint32_t              version = kRoutineGraphVersion;
		RoutineGraphKind      kind = RoutineGraphKind::ZoneEvent;
		std::string           name;
		std::vector<RoutineNode> nodes;
		std::vector<RoutineLink> links;
	};

	// ---------------------------------------------------------------------
	// Conversions enum <-> chaîne (utilisées par la sérialisation + le schéma).
	// Déterministes et stables. Toute nouvelle valeur d'enum DOIT être ajoutée
	// ici en parallèle, sinon la sérialisation échoue (volontairement).
	// ---------------------------------------------------------------------

	inline const char* ToString(RoutineGraphKind k)
	{
		switch (k)
		{
			case RoutineGraphKind::NpcRoutine: return "npc_routine";
			case RoutineGraphKind::ZoneEvent:  return "zone_event";
		}
		return "zone_event";
	}

	inline bool FromString(std::string_view s, RoutineGraphKind& out)
	{
		if (s == "npc_routine") { out = RoutineGraphKind::NpcRoutine; return true; }
		if (s == "zone_event")  { out = RoutineGraphKind::ZoneEvent;  return true; }
		return false;
	}

	inline const char* ToString(PinDirection d)
	{
		return d == PinDirection::Input ? "in" : "out";
	}

	inline bool FromString(std::string_view s, PinDirection& out)
	{
		if (s == "in")  { out = PinDirection::Input;  return true; }
		if (s == "out") { out = PinDirection::Output; return true; }
		return false;
	}

	inline const char* ToString(PinKind k)
	{
		return k == PinKind::Exec ? "exec" : "data";
	}

	inline bool FromString(std::string_view s, PinKind& out)
	{
		if (s == "exec") { out = PinKind::Exec; return true; }
		if (s == "data") { out = PinKind::Data; return true; }
		return false;
	}

	inline const char* ToString(RoutineDataType t)
	{
		switch (t)
		{
			case RoutineDataType::None:      return "none";
			case RoutineDataType::Bool:      return "bool";
			case RoutineDataType::Int:       return "int";
			case RoutineDataType::Float:     return "float";
			case RoutineDataType::Vec3:      return "vec3";
			case RoutineDataType::String:    return "string";
			case RoutineDataType::EntityRef: return "entity_ref";
		}
		return "none";
	}

	inline bool FromString(std::string_view s, RoutineDataType& out)
	{
		if (s == "none")       { out = RoutineDataType::None;      return true; }
		if (s == "bool")       { out = RoutineDataType::Bool;      return true; }
		if (s == "int")        { out = RoutineDataType::Int;       return true; }
		if (s == "float")      { out = RoutineDataType::Float;     return true; }
		if (s == "vec3")       { out = RoutineDataType::Vec3;      return true; }
		if (s == "string")     { out = RoutineDataType::String;    return true; }
		if (s == "entity_ref") { out = RoutineDataType::EntityRef; return true; }
		return false;
	}

	/// Nom stable d'un type de nœud (clé de sérialisation + libellé schéma).
	const char* ToString(RoutineNodeType t);
	/// Inverse de `ToString(RoutineNodeType)`. Retourne false si inconnu.
	bool FromString(std::string_view s, RoutineNodeType& out);
}
