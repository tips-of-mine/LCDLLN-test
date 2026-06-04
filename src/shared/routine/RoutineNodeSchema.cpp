// M101.1 — Implémentation du schéma + mapping RoutineNodeType <-> chaîne.

#include "src/shared/routine/RoutineNodeSchema.h"

namespace engine::routine
{
	namespace
	{
		// Fabriques de pins/propriétés concises pour la table de schémas. Les ids
		// de pin sont des placeholders locaux au gabarit (réassignés à
		// l'instanciation, M101.5).
		RoutinePin ExecPin(uint32_t id, PinDirection dir, const char* name)
		{
			RoutinePin p;
			p.id = id; p.direction = dir; p.kind = PinKind::Exec;
			p.dataType = RoutineDataType::None; p.name = name;
			return p;
		}

		RoutinePin DataPin(uint32_t id, PinDirection dir, RoutineDataType dt, const char* name)
		{
			RoutinePin p;
			p.id = id; p.direction = dir; p.kind = PinKind::Data;
			p.dataType = dt; p.name = name;
			return p;
		}

		RoutineProperty Prop(const char* key, RoutineDataType type)
		{
			RoutineProperty pr;
			pr.key = key; pr.type = type;
			return pr;
		}

		constexpr uint8_t kNpc  = 1u << static_cast<int>(RoutineGraphKind::NpcRoutine);
		constexpr uint8_t kZone = 1u << static_cast<int>(RoutineGraphKind::ZoneEvent);
		constexpr uint8_t kBoth = kNpc | kZone;

		std::vector<RoutineNodeSchema> BuildSchemas()
		{
			std::vector<RoutineNodeSchema> t;

			// --- commun ---
			t.push_back({ RoutineNodeType::Comment, "Commentaire", kBoth,
				{}, { Prop("text", RoutineDataType::String) } });

			// --- zone_event ---
			t.push_back({ RoutineNodeType::EventOnZoneEnter, "Événement : entrée de zone", kZone,
				{ ExecPin(1, PinDirection::Output, "fired") },
				{ Prop("zoneId", RoutineDataType::EntityRef) } });

			t.push_back({ RoutineNodeType::EventOnZoneExit, "Événement : sortie de zone", kZone,
				{ ExecPin(1, PinDirection::Output, "fired") },
				{ Prop("zoneId", RoutineDataType::EntityRef) } });

			t.push_back({ RoutineNodeType::EventOnInteract, "Événement : interaction", kZone,
				{ ExecPin(1, PinDirection::Output, "fired") },
				{ Prop("interactiveId", RoutineDataType::EntityRef) } });

			t.push_back({ RoutineNodeType::BranchIf, "Branche (si)", kZone,
				{ ExecPin(1, PinDirection::Input, "in"),
				  ExecPin(2, PinDirection::Output, "true"),
				  ExecPin(3, PinDirection::Output, "false"),
				  DataPin(4, PinDirection::Input, RoutineDataType::Bool, "cond") },
				{} });

			t.push_back({ RoutineNodeType::ActionOpenInteractive, "Action : ouvrir/fermer objet", kZone,
				{ ExecPin(1, PinDirection::Input, "in"),
				  ExecPin(2, PinDirection::Output, "out") },
				{ Prop("interactiveId", RoutineDataType::EntityRef),
				  Prop("open", RoutineDataType::Bool) } });

			t.push_back({ RoutineNodeType::ActionBroadcastSeason, "Action : broadcast saison", kZone,
				{ ExecPin(1, PinDirection::Input, "in"),
				  ExecPin(2, PinDirection::Output, "out") },
				{ Prop("seasonIndex", RoutineDataType::Int) } });

			t.push_back({ RoutineNodeType::ActionBroadcastWeather, "Action : broadcast météo", kZone,
				{ ExecPin(1, PinDirection::Input, "in"),
				  ExecPin(2, PinDirection::Output, "out") },
				{ Prop("weatherIndex", RoutineDataType::Int) } });

			// --- npc_routine ---
			t.push_back({ RoutineNodeType::NpcStateRoot, "PNJ : racine d'états", kNpc,
				{ ExecPin(1, PinDirection::Output, "enter") }, {} });

			t.push_back({ RoutineNodeType::NpcState, "PNJ : état", kNpc,
				{ ExecPin(1, PinDirection::Input, "enter"),
				  ExecPin(2, PinDirection::Output, "tasks") },
				{ Prop("name", RoutineDataType::String) } });

			t.push_back({ RoutineNodeType::SensorPlayerInRange, "Capteur : joueur à portée", kNpc,
				{ DataPin(1, PinDirection::Output, RoutineDataType::Bool, "inRange") },
				{ Prop("rangeMeters", RoutineDataType::Float) } });

			t.push_back({ RoutineNodeType::SensorTimeOfDay, "Capteur : heure du jour", kNpc,
				{ DataPin(1, PinDirection::Output, RoutineDataType::Float, "hours") }, {} });

			t.push_back({ RoutineNodeType::TaskPlayAnim, "Tâche : jouer animation", kNpc,
				{ ExecPin(1, PinDirection::Input, "in"),
				  ExecPin(2, PinDirection::Output, "out") },
				{ Prop("animId", RoutineDataType::String) } });

			t.push_back({ RoutineNodeType::TaskMoveTo, "Tâche : se déplacer vers", kNpc,
				{ ExecPin(1, PinDirection::Input, "in"),
				  ExecPin(2, PinDirection::Output, "out") },
				{ Prop("targetRef", RoutineDataType::EntityRef) } });

			t.push_back({ RoutineNodeType::TaskSetEmotion, "Tâche : changer d'émotion", kNpc,
				{ ExecPin(1, PinDirection::Input, "in"),
				  ExecPin(2, PinDirection::Output, "out") },
				{ Prop("emotion", RoutineDataType::String) } });

			return t;
		}
	} // namespace

	const std::vector<RoutineNodeSchema>& AllSchemas()
	{
		static const std::vector<RoutineNodeSchema> kSchemas = BuildSchemas();
		return kSchemas;
	}

	const RoutineNodeSchema* FindSchema(RoutineNodeType type)
	{
		for (const auto& s : AllSchemas())
		{
			if (s.type == type) return &s;
		}
		return nullptr;
	}

	// ---- mapping RoutineNodeType <-> chaîne (déclaré dans RoutineGraph.h) ----

	const char* ToString(RoutineNodeType t)
	{
		switch (t)
		{
			case RoutineNodeType::Comment:               return "Comment";
			case RoutineNodeType::EventOnZoneEnter:      return "EventOnZoneEnter";
			case RoutineNodeType::EventOnZoneExit:       return "EventOnZoneExit";
			case RoutineNodeType::EventOnInteract:       return "EventOnInteract";
			case RoutineNodeType::BranchIf:              return "BranchIf";
			case RoutineNodeType::ActionOpenInteractive: return "ActionOpenInteractive";
			case RoutineNodeType::ActionBroadcastSeason: return "ActionBroadcastSeason";
			case RoutineNodeType::ActionBroadcastWeather:return "ActionBroadcastWeather";
			case RoutineNodeType::NpcStateRoot:          return "NpcStateRoot";
			case RoutineNodeType::NpcState:              return "NpcState";
			case RoutineNodeType::SensorPlayerInRange:   return "SensorPlayerInRange";
			case RoutineNodeType::SensorTimeOfDay:       return "SensorTimeOfDay";
			case RoutineNodeType::TaskPlayAnim:          return "TaskPlayAnim";
			case RoutineNodeType::TaskMoveTo:            return "TaskMoveTo";
			case RoutineNodeType::TaskSetEmotion:        return "TaskSetEmotion";
		}
		return "Comment";
	}

	bool FromString(std::string_view s, RoutineNodeType& out)
	{
		for (const auto& sch : AllSchemas())
		{
			if (s == ToString(sch.type)) { out = sch.type; return true; }
		}
		return false;
	}
}
