#pragma once

// Camp de l'Oracle (lore v1.0.140) — passages conditionnels d'une même carte.
//
// Une porte « passage » relie deux points d'une MÊME carte : le joueur qui entre dans
// son rayon est replacé au point d'arrivée (ex. palier de la grande porte du rocher de
// l'Oracle -> intérieur de la salle de l'Oracle), à condition de remplir les conditions
// du passage. Le fichier `world/<monde>/passages.json` est GÉNÉRÉ par tools/world_gen
// (camp de l'Oracle : Oracle.cpp) et lu par le shard seul (autorité serveur : le
// client ne voit qu'une porte fermée tant que le shard ne l'a pas fait passer).
//
// Conditions (décisions du propriétaire du 25/11/2026) :
//   - requires_quests : chaque quête listée doit être acceptée, prête à rendre ou
//     terminée (porte SO : « L'Appel de l'Oracle » ; porte E : « L'Épreuve des Échos ») ;
//   - min_group : taille de groupe minimale (porte E : raid, au moins 3 membres) ;
//   - un joueur mort ne passe pas ; les sorties (intérieur -> palier) n'ont aucune condition.
//
// Déclenchement : comme les portails entre cartes (PortalNetwork) — rayon, arrivée hors du
// rayon du passage retour, réarmement après 5 s hors de tout rayon. Un refus n'est
// signalé qu'une fois par entrée dans le rayon (pas de message à chaque Input).
//
// Module PUR (Config + WorldCells, aucune dépendance réseau ni ServerApp) : testé seul
// (door_passages_tests). Pas thread-safe : thread de tick du shard.

#include "src/shared/world/WorldCells.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core { class Config; }

namespace engine::server
{
	/// Un passage d'une carte (une entrée de passages.json).
	struct DoorPassage
	{
		std::string id;                    ///< identifiant stable (ex. "oracle_porte_so_entree")
		std::string name;                  ///< nom affiché (journaux)
		engine::world::cells::CellMap map = engine::world::cells::CellMap::Royaumes;
		double x = 0.0, y = 0.0, z = 0.0;  ///< centre du déclencheur, repère GLOBAL de la carte (m)
		float radiusMeters = 2.5f;         ///< rayon de déclenchement (m)
		double toX = 0.0, toY = 0.0, toZ = 0.0; ///< point d'arrivée (même carte, m)
		float toYawDeg = 0.0f;             ///< orientation à l'arrivée (degrés, informatif)
		std::vector<std::string> requiredQuests; ///< quêtes exigées (acceptée, prête ou terminée)
		uint32_t minGroup = 0;             ///< taille de groupe minimale (0 ou 1 = seul admis)
		std::string deniedMessage;         ///< message système en cas de refus (vide = aucun)
	};

	/// État de passage d'UN joueur (porté par la connexion côté shard).
	struct DoorPassageTravelState
	{
		/// Faux juste après un passage (ou à la connexion) : aucun passage ne se déclenche
		/// tant que le joueur n'est pas resté kRearmDelayMs hors de tout rayon.
		bool armed = false;
		std::optional<uint64_t> outsideSinceMs;
		/// Passage refusé dans lequel le joueur se tient encore (refus déjà signalé).
		std::string deniedInsideId;
	};

	/// Issue d'un Update.
	enum class DoorPassageOutcome : uint8_t
	{
		None = 0,   ///< rien à faire
		Pass = 1,   ///< replacer le joueur au point d'arrivée de \ref DoorPassageResult::passage
		Denied = 2, ///< conditions non remplies : signaler le refus (une fois par entrée)
	};

	struct DoorPassageResult
	{
		DoorPassageOutcome outcome = DoorPassageOutcome::None;
		const DoorPassage* passage = nullptr;
	};

	class DoorPassages final
	{
	public:
		/// Délai (ms) hors de tout rayon avant réarmement (comme PortalNetwork).
		static constexpr uint64_t kRearmDelayMs = 5000u;

		/// Ajoute les passages d'une carte depuis un texte passages.json (carte = champ
		/// "world"). Les entrées sans identifiant ou à coordonnées invalides sont ignorées.
		/// \return false (rien ajouté) si JSON invalide, schema_version ≠ 1 ou monde inconnu.
		bool AddFromText(std::string_view json, std::string& outError);

		/// Charge `world/<monde>/passages.json` de chaque carte depuis `paths.content`
		/// (remplace le contenu). Un fichier absent n'est pas une erreur (cité dans
		/// \p outWarning). \return false si un fichier présent est invalide (table vide).
		bool LoadForCellWorld(const engine::core::Config& config, std::string& outError, std::string& outWarning);

		void Clear() { m_passages.clear(); }
		size_t Count() const { return m_passages.size(); }
		const std::vector<DoorPassage>& All() const { return m_passages; }

		/// Passage \p id de la carte \p map, ou nullptr.
		const DoorPassage* Find(engine::world::cells::CellMap map, std::string_view id) const;
		/// Passage de la carte \p map dont le rayon contient (x, z), ou nullptr.
		const DoorPassage* PassageAt(engine::world::cells::CellMap map, double x, double z) const;

		/// Vrai si chaque quête de \p p est satisfaite selon \p questSatisfied (appelé avec
		/// l'identifiant de quête ; vrai si acceptée, prête à rendre ou terminée) et si
		/// \p groupSize ≥ minGroup. Fonction pure.
		static bool ConditionsMet(const DoorPassage& p, const std::function<bool(std::string_view)>& questSatisfied,
			uint32_t groupSize);

		/// À chaque position acceptée d'un joueur : réarmement (5 s hors de tout rayon),
		/// puis, si le joueur vivant et armé est dans le rayon d'un passage : Pass si
		/// \p allowed(passage) (état désarmé), sinon Denied la première fois (puis None
		/// tant qu'il reste dans ce rayon). Effet de bord : \p state.
		/// \param x, z position GLOBALE (m) ; \param nowMs horloge monotone (ms).
		DoorPassageResult Update(DoorPassageTravelState& state, engine::world::cells::CellMap map,
			double x, double z, bool alive, uint64_t nowMs,
			const std::function<bool(const DoorPassage&)>& allowed) const;

		/// Désarme \p state (après un passage, une connexion ou un téléport).
		static void Disarm(DoorPassageTravelState& state)
		{
			state.armed = false;
			state.outsideSinceMs.reset();
		}

	private:
		std::vector<DoorPassage> m_passages;
	};
}
