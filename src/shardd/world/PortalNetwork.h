#pragma once

// Seconde carte (Les Enfers, docs/world/ENFERS.md) — réseau de portails entre cartes.
//
// Chaque carte en cellules publie `world/<monde>/portals.json` (GÉNÉRÉ par
// tools/world_gen) : ses portails, leur position GLOBALE (repère de la carte),
// leur rayon de déclenchement et le portail APPARIÉ de l'autre carte
// (`link` = "<monde>:<id>"). Les 5 Portes des enfers du continent sont reliées
// une à une aux 5 portails des Enfers.
//
// Règles (décisions du propriétaire) :
//   - toutes les races passent, dans les deux sens, par n'importe quelle porte ;
//   - un joueur MORT ne passe pas ;
//   - déclenchement dans le rayon du portail (~6 m) ;
//   - arrivée décalée (~8 m) du portail apparié, donc HORS de son rayon ;
//   - réarmement : il faut être sorti de tout rayon de portail depuis 5 s avant
//     qu'un portail se déclenche de nouveau (pas d'aller-retour en boucle).
//
// Module PUR (Config + WorldCells, aucune dépendance réseau ni ServerApp) :
// testé seul (portal_network_tests). Pas thread-safe : thread de tick du shard.

#include "src/shared/world/WorldCells.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core { class Config; }

namespace engine::server
{
	/// Un portail d'une carte (une entrée de portals.json).
	struct PortalEndpoint
	{
		std::string id;              ///< identifiant du POI (ex. "poi_porte_1")
		std::string name;            ///< nom affiché (journaux, MJ)
		engine::world::cells::CellMap map = engine::world::cells::CellMap::Royaumes;
		double x = 0.0, y = 0.0, z = 0.0; ///< centre, repère GLOBAL de sa carte (m)
		float radiusMeters = 6.0f;   ///< rayon de déclenchement (m)
		float arrivalOffsetMeters = 8.0f; ///< décalage (m, vers le sud) du point d'arrivée
		/// Portail apparié : carte et identifiant (lien "<monde>:<id>").
		engine::world::cells::CellMap linkMap = engine::world::cells::CellMap::Royaumes;
		std::string linkId;
	};

	/// Destination d'un passage de portail : point d'arrivée sur l'autre carte.
	struct PortalDestination
	{
		engine::world::cells::CellMap map = engine::world::cells::CellMap::Royaumes;
		double x = 0.0, y = 0.0, z = 0.0; ///< repère GLOBAL de la carte d'arrivée (m)
		std::string fromId;          ///< portail franchi
		std::string toId;            ///< portail d'arrivée
	};

	/// État de passage d'UN joueur (porté par la connexion côté shard).
	struct PortalTravelState
	{
		/// Faux juste après un passage (ou à la connexion) : aucun portail ne se
		/// déclenche tant que le joueur n'est pas resté 5 s hors de tout rayon.
		bool armed = false;
		/// Instant (ms, horloge monotone) de la sortie du dernier rayon ; nullopt
		/// tant que le joueur est encore dans un rayon (ou n'a pas été évalué).
		std::optional<uint64_t> outsideSinceMs;
	};

	class PortalNetwork final
	{
	public:
		/// Délai (ms) hors de tout rayon avant réarmement.
		static constexpr uint64_t kRearmDelayMs = 5000u;

		/// Ajoute les portails d'une carte depuis un texte portals.json (carte = champ
		/// "world"). Les portails sans lien valide sont ignorés. \return false (rien
		/// ajouté) si JSON invalide, schema_version ≠ 1 ou monde inconnu.
		bool AddFromText(std::string_view json, std::string& outError);

		/// Charge `world/royaumes/portals.json` et `world/enfers/portals.json` depuis
		/// `paths.content` (remplace le contenu). Un fichier absent n'est pas une
		/// erreur (pas de portail sur cette carte) : il est cité dans \p outWarning.
		/// \return false si un fichier présent est invalide (table vide alors).
		bool LoadForCellWorld(const engine::core::Config& config, std::string& outError, std::string& outWarning);

		/// Oublie tous les portails.
		void Clear() { m_portals.clear(); }
		/// Nombre de portails chargés (toutes cartes).
		size_t Count() const { return m_portals.size(); }
		/// Nombre de portails dont le portail apparié est chargé (passage possible).
		size_t LinkedCount() const;
		/// Tous les portails chargés (annuaire MJ, journaux).
		const std::vector<PortalEndpoint>& All() const { return m_portals; }

		/// Portail \p id de la carte \p map, ou nullptr.
		const PortalEndpoint* Find(engine::world::cells::CellMap map, std::string_view id) const;
		/// Portail de la carte \p map dont le rayon contient le point (x, z), ou nullptr.
		const PortalEndpoint* PortalAt(engine::world::cells::CellMap map, double x, double z) const;
		/// Point d'arrivée quand on franchit \p from (portail apparié + décalage), ou
		/// nullopt si l'apparié n'est pas chargé.
		std::optional<PortalDestination> DestinationOf(const PortalEndpoint& from) const;

		/// À chaque position acceptée d'un joueur : réarmement (5 s hors de tout
		/// rayon), puis déclenchement si le joueur, vivant et armé, est dans le rayon
		/// d'un portail relié. Effet de bord : \p state (désarmé après un passage).
		/// \param map carte du joueur ; \param x, z position GLOBALE (m) ;
		/// \param alive faux pour un joueur mort (jamais de passage) ;
		/// \param nowMs horloge monotone (ms).
		/// \return la destination si le joueur doit être transféré maintenant.
		std::optional<PortalDestination> Update(PortalTravelState& state, engine::world::cells::CellMap map,
			double x, double z, bool alive, uint64_t nowMs) const;

		/// Désarme \p state (après un passage, une connexion ou un téléport MJ).
		static void Disarm(PortalTravelState& state)
		{
			state.armed = false;
			state.outsideSinceMs.reset();
		}

	private:
		std::vector<PortalEndpoint> m_portals;
	};
}
