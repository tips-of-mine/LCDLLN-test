#pragma once

#include <string_view>

/// Règles d'éligibilité des points de réapparition (cimetières) au respawn d'un
/// joueur. Header-only (logique pure, testable sans dépendances) — partagé par le
/// shard (sélection serveur) et les tests CPU.
namespace engine::world
{
	/// Détermine si un cimetière est éligible au respawn d'un joueur, selon la règle
	/// du « rayon neutre de faction » :
	///   - un cimetière sans faction propriétaire (`graveyardFaction` vide ou "-")
	///     est NEUTRE partout → toujours éligible ;
	///   - sinon, dans son rayon neutre (`distanceMeters <= neutralRadiusM`) il est
	///     NEUTRE → éligible pour toutes les factions ;
	///   - au-delà du rayon, il n'est éligible que pour les joueurs de SA faction
	///     (`graveyardFaction == playerFaction`).
	///
	/// \param distanceMeters distance (m) entre le lieu de mort et le cimetière.
	/// \param neutralRadiusM rayon (m) dans lequel le cimetière est neutre ; <= 0
	///        désactive la zone neutre (seule la faction propriétaire est éligible
	///        au-delà de 0 m, sauf cimetière neutre).
	/// \param graveyardFaction id de la faction propriétaire du cimetière (vide/"-"
	///        = neutre partout).
	/// \param playerFaction id de la faction du joueur qui respawn (peut être vide
	///        pour un personnage legacy/sans faction).
	/// \return true si le joueur peut réapparaître à ce cimetière.
	inline bool IsGraveyardEligibleForRespawn(float distanceMeters,
	                                          float neutralRadiusM,
	                                          std::string_view graveyardFaction,
	                                          std::string_view playerFaction)
	{
		// Cimetière neutre (sans propriétaire) : éligible partout.
		if (graveyardFaction.empty() || graveyardFaction == "-")
		{
			return true;
		}
		// Dans le rayon neutre : éligible quelle que soit la faction du joueur.
		if (distanceMeters <= neutralRadiusM)
		{
			return true;
		}
		// Au-delà : réservé aux joueurs de la faction propriétaire du cimetière.
		return graveyardFaction == playerFaction;
	}
}
