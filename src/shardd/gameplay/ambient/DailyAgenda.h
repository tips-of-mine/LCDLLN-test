#pragma once
// ETAPE4.4 (chantier 2.7 du rapport City of Heroes) — agenda déterministe des PNJ.
//
// Un PNJ d'ambiance à agenda vit sa journée selon une ROUTINE DE MÉTIER : une suite de
// créneaux horaires (heure de début, lieu, activité). Exemple, l'artisan :
//   0 h domicile (dort) → 6 h 30 atelier → 12 h auberge (mange) → 13 h atelier
//   → 18 h 30 auberge → 21 h domicile (dort).
// Chaque PNJ en reçoit une VARIANTE tirée d'une graine fixe (son identifiant) et du jour
// de jeu : horaires décalés de quelques minutes, auberge ou étal choisis parmi plusieurs,
// petit point d'arrêt différent sur le lieu. Tout est déterministe : la position et
// l'activité de n'importe quel PNJ se calculent à n'importe quelle heure de jeu SANS
// simulation (EvaluateAgenda). Le shard s'en sert pour :
//   - faire apparaître un PNJ là où son agenda le place à l'instant (et pas à sa porte) ;
//   - ne pas le faire apparaître quand il dort chez lui ;
//   - guider sa marche d'un lieu à l'autre quand un joueur est là pour le voir.
// Loin des joueurs, rien n'est calculé : le monde continue pourtant de « vivre », puisque
// l'état à l'instant t se recalcule au retour d'un joueur.
//
// Module PUR (ni réseau ni ServerApp) : testé seul (daily_agenda_tests).

#include "src/shardd/gameplay/ambient/AmbientRoutine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server::ambient
{
	/// Lieux d'un agenda (points fournis par le spawner).
	enum class AgendaPlace : uint8_t
	{
		Home = 0,  ///< domicile (porte)
		Work,      ///< lieu de travail (atelier, champ, étal attitré)
		Tavern,    ///< auberge
		Market,    ///< marché (étals)
		Square,    ///< place
		Temple,    ///< temple, sanctuaire
		Count
	};
	inline constexpr size_t kAgendaPlaceCount = static_cast<size_t>(AgendaPlace::Count);

	/// Activité pendant un créneau.
	enum class AgendaActivity : uint8_t
	{
		Sleep = 0, ///< chez lui : invisible (pas d'entité) une fois arrivé
		Work,      ///< au travail, presque immobile
		Eat,       ///< à l'auberge
		Stroll,    ///< flânerie : petits déplacements autour du lieu
		Pray,      ///< au temple
		Rest,      ///< devant chez lui, dehors
	};

	/// Un créneau d'une routine de métier.
	struct AgendaSlot
	{
		float startHour = 0.0f; ///< heure de jeu de début ∈ [0, 24)
		AgendaPlace place = AgendaPlace::Home;
		AgendaActivity activity = AgendaActivity::Sleep;
	};

	/// Routine de métier intégrée (fermier, artisan, marchand, aubergiste, pretre,
	/// flaneur). nullptr si inconnue. Créneaux triés par heure, le premier à 0 h.
	const std::vector<AgendaSlot>* FindProfession(std::string_view profession);

	/// Identifiants des métiers intégrés (documentation, validation).
	std::vector<std::string_view> ProfessionIds();

	/// Agenda d'un spawner (données JSON `agenda`, immuables).
	struct AgendaDefinition
	{
		std::string profession;                                         ///< métier (FindProfession)
		std::array<std::vector<RoutinePoint>, kAgendaPlaceCount> places; ///< points par lieu
		float jitterMinutes = 20.0f;                                    ///< décalage maximal des horaires (min de jeu)
		float strollRadiusMeters = 4.0f;                                ///< rayon de flânerie autour du lieu
		bool enabled = false;                                           ///< vrai si `agenda` présent et valide
	};

	/// Convertit un libellé JSON de lieu (home, work, tavern, market, square, temple).
	bool ParseAgendaPlace(std::string_view text, AgendaPlace& out);

	/// Graine stable d'un PNJ : FNV-1a de l'identifiant du spawner et de l'emplacement.
	uint64_t AgendaSeed(std::string_view spawnerId, uint32_t slotIndex);

	/// État d'un PNJ à un instant de jeu.
	struct AgendaState
	{
		AgendaPlace place = AgendaPlace::Home;
		AgendaActivity activity = AgendaActivity::Sleep;
		RoutinePoint target;          ///< point d'arrêt sur le lieu du créneau
		RoutinePoint position;        ///< position calculée (en route ou sur place)
		bool travelling = false;      ///< en route depuis le lieu du créneau précédent
		bool indoors = false;         ///< dort chez lui ET arrivé : aucune entité à montrer
		uint32_t slotIndex = 0;       ///< créneau courant dans la routine
		double slotStartGameSeconds = 0.0; ///< début (jeu, absolu) du créneau courant
	};

	/// Calcule l'état du PNJ à l'instant de jeu \p gameSeconds (secondes de jeu depuis
	/// l'origine de l'horloge monde). \p walkSpeedMetersPerSecond = vitesse de marche
	/// (temps réel) ; \p gameSecondsPerRealSecond = rythme de l'horloge monde (24 pour un
	/// jour de jeu en une heure). \p fallback = point utilisé pour un lieu sans point
	/// (domicile, sinon le point d'attache du spawner).
	/// Métier inconnu ou agenda désactivé : état « au repos sur le point de repli ».
	AgendaState EvaluateAgenda(const AgendaDefinition& def, uint64_t seed, double gameSeconds,
		float walkSpeedMetersPerSecond, double gameSecondsPerRealSecond, RoutinePoint fallback);

	/// Point de flânerie (activité Stroll) : change toutes les \p periodGameSeconds autour
	/// de \p state.target, dans strollRadiusMeters ; ailleurs, state.target. Déterministe.
	RoutinePoint StrollPoint(const AgendaDefinition& def, uint64_t seed, const AgendaState& state,
		double gameSeconds, double periodGameSeconds = 120.0);
}
