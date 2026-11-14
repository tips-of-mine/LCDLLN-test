#pragma once
// Lore v1.0.114 — progression d'exploration PAR PERSONNAGE, calculée par le shard
// (décisions du propriétaire du 13/11/2026, docs/world/LORE_BD.md §16.41).
//
//   - Exploit des 666 cimetières (`exploit_pelerin_des_666_tombes`) : un cimetière des
//     Royaumes compte comme visité quand le personnage VIVANT entre dans son rayon
//     (kGraveyardVisitRadiusM) ou y réapparaît après une mort. Les cimetières sont les
//     points `graveyard` des respawn_points.txt, identifiés par leur jeton `gy=<id>`
//     (tools/world_gen). L'exploit n'est débloqué que si le monde chargé compte
//     exactement kPilgrimGraveyardCount cimetières identifiés.
//   - Lieux secrets découverts : un lieu de world/<monde>/secret_places.json (RÉSERVÉ
//     AU SERVEUR) est révélé à CE personnage seulement dès qu'il entre dans son rayon
//     (`radius_m` d'une ville, kSecretPlaceDefaultRadiusM pour un lieu sans rayon).
//
// DiscoveryCatalog : catalogue partagé (cimetières, lieux secrets) avec un index spatial
// par seaux carrés ; CharacterDiscoveryProgress : état d'UN personnage (ensembles
// visités / découverts, éléments nouveaux de la session à persister).
//
// Pur (aucune base, aucun réseau ; Config seulement pour lire le JSON) : testé par
// discovery_progress_tests. Thread : celui de la boucle du shard (aucun verrou).

#include "src/shared/world/WorldCells.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::server
{
	/// Rayon (m) dans lequel un personnage vivant « visite » un cimetière.
	inline constexpr float kGraveyardVisitRadiusM = 15.0f;
	/// Rayon (m) d'un lieu secret sans `radius_m` (lieux remarquables, POI).
	inline constexpr float kSecretPlaceDefaultRadiusM = 40.0f;
	/// Nombre de cimetières des Royaumes (décision du propriétaire, lore v1.0.111).
	inline constexpr uint32_t kPilgrimGraveyardCount = 666u;
	/// Code de l'exploit au catalogue `exploits` (migration 0086).
	inline constexpr std::string_view kPilgrimExploitCode = "exploit_pelerin_des_666_tombes";
	/// Titre de l'exploit (affiché au joueur au déblocage).
	inline constexpr std::string_view kPilgrimExploitTitle = "Le Pèlerin des Six Cent Soixante-Six Tombes";

	/// Un cimetière des Royaumes (position GLOBALE, m).
	struct GraveyardSite
	{
		std::string id;
		float x = 0.0f;
		float z = 0.0f;
	};

	/// Un lieu secret (ville ou lieu remarquable de secret_places.json).
	struct SecretPlaceSite
	{
		std::string id;
		std::string name;
		std::string kind;   ///< genre (« ville », « grotte »…)
		float x = 0.0f;     ///< position GLOBALE (m)
		float z = 0.0f;
		float radiusM = kSecretPlaceDefaultRadiusM;
		engine::world::cells::CellMap map = engine::world::cells::CellMap::Royaumes;
	};

	/// Jetons optionnels d'une ligne de respawn_points.txt après « zone type x y z ».
	struct RespawnTrailingTokens
	{
		std::string faction;        ///< vide ou "-" = neutre
		bool hasNeutralRadius = false;
		float neutralRadiusM = 0.0f;
		std::string graveyardId;    ///< jeton `gy=<id>` (v1.0.114), vide si absent
	};

	/// Lit les jetons optionnels d'une ligne de respawn_points.txt : faction, rayon neutre
	/// (m), et le jeton `gy=<id>` où qu'il soit (format v1.0.114, rétrocompatible : les
	/// lignes sans `gy=` donnent un id vide). Un rayon illisible est ignoré.
	/// \param rest suite de la ligne après la coordonnée z.
	RespawnTrailingTokens ParseRespawnTrailingTokens(std::string_view rest);

	/// Catalogue des cimetières et lieux secrets, avec index spatial.
	class DiscoveryCatalog final
	{
	public:
		/// Remplace les cimetières. Les ids vides ou en double sont écartés (le premier
		/// gagne). \return nombre de cimetières écartés pour doublon.
		size_t SetGraveyards(std::vector<GraveyardSite> sites);

		/// Ajoute les lieux de \p json (schéma de secret_places.json : `world`, `cities`
		/// avec `radius_m`, `pois` sans rayon). Un id déjà présent est ignoré.
		/// \return false (rien ajouté) si le JSON est invalide.
		bool AppendSecretPlacesFromText(std::string_view json, std::string& outError);

		/// Nombre de cimetières identifiés (Royaumes).
		size_t GraveyardCount() const { return m_graveyards.size(); }
		/// Nombre de lieux secrets (toutes cartes).
		size_t SecretPlaceCount() const { return m_secrets.size(); }
		/// Cimetière d'indice \p index (< GraveyardCount()).
		const GraveyardSite& Graveyard(uint32_t index) const { return m_graveyards[index]; }
		/// Lieu secret d'indice \p index (< SecretPlaceCount()).
		const SecretPlaceSite& SecretPlace(uint32_t index) const { return m_secrets[index]; }
		/// \return true si \p id est un cimetière du catalogue.
		bool HasGraveyard(std::string_view id) const;
		/// \return index du lieu secret \p id, ou -1.
		int64_t FindSecretPlace(std::string_view id) const;

		/// Indices des cimetières dont le disque de kGraveyardVisitRadiusM contient
		/// (\p x, \p z) (m, positions GLOBALES des Royaumes). \p out est vidé d'abord.
		void GraveyardsAt(float x, float z, std::vector<uint32_t>& out) const;
		/// Indices des lieux secrets de la carte \p map dont le disque contient (\p x, \p z).
		/// \p out est vidé d'abord.
		void SecretPlacesAt(engine::world::cells::CellMap map, float x, float z, std::vector<uint32_t>& out) const;

		/// L'exploit n'a de sens que pour le monde complet : exactement 666 cimetières.
		bool PilgrimExploitPossible() const { return m_graveyards.size() == kPilgrimGraveyardCount; }

	private:
		/// Clé d'un seau de kBucketM mètres sur la couche \p layer (0 cimetières,
		/// 1 + carte pour les lieux secrets).
		static uint64_t BucketKey(uint8_t layer, int64_t bx, int64_t bz);
		/// Inscrit l'élément \p index dans tous les seaux que touche le disque (\p x, \p z,
		/// \p radiusM). Effet de bord : m_buckets.
		void IndexDisc(uint8_t layer, float x, float z, float radiusM, uint32_t index);
		/// Seau contenant le point (\p x, \p z) de la couche \p layer, nullptr si vide.
		const std::vector<uint32_t>* BucketAt(uint8_t layer, float x, float z) const;

		static constexpr float kBucketM = 256.0f;
		std::vector<GraveyardSite> m_graveyards;
		std::unordered_map<std::string, uint32_t> m_graveyardIndex;
		std::vector<SecretPlaceSite> m_secrets;
		std::unordered_map<std::string, uint32_t> m_secretIndex;
		std::unordered_map<uint64_t, std::vector<uint32_t>> m_buckets;
	};

	/// Nouveautés d'une observation (cimetières visités, lieux découverts, exploit).
	struct DiscoveryEvents
	{
		std::vector<std::string> graveyards;   ///< ids nouvellement visités
		std::vector<uint32_t> secretPlaces;    ///< indices (catalogue) nouvellement découverts
		bool exploitUnlocked = false;          ///< 666 cimetières atteints à l'instant
		/// Vrai si aucune nouveauté.
		bool Empty() const { return graveyards.empty() && secretPlaces.empty() && !exploitUnlocked; }
	};

	/// État d'exploration d'UN personnage connecté.
	class CharacterDiscoveryProgress final
	{
	public:
		/// Charge l'état persistant (lignes de character_graveyard_visits,
		/// character_secret_discoveries, exploit déjà débloqué). Les ids inconnus du
		/// catalogue sont gardés (monde régénéré) mais non comptés.
		/// \return true si tous les cimetières sont visités sans que l'exploit soit
		///         débloqué (déblocage à rattraper : ExploitGrantPending devient vrai).
		bool Load(const DiscoveryCatalog& catalog, const std::vector<std::string>& visitedGraveyards,
			const std::vector<std::string>& discoveredPlaces, bool pilgrimExploitUnlocked);

		/// Personnage VIVANT en (\p x, \p z) (m, GLOBAL) sur la carte \p map : visite les
		/// cimetières (Royaumes seulement) et découvre les lieux secrets dans leur rayon.
		/// Nouveautés ajoutées à \p out (non vidé). Sans effet avant Load.
		void ObservePosition(const DiscoveryCatalog& catalog, engine::world::cells::CellMap map,
			float x, float z, DiscoveryEvents& out);

		/// Réapparition au cimetière \p graveyardId : le compte comme visité.
		/// \return true si c'est une nouvelle visite (ajoutée à \p out).
		bool RecordGraveyardVisit(const DiscoveryCatalog& catalog, std::string_view graveyardId, DiscoveryEvents& out);

		/// Vrai après Load (suivi actif pour la session).
		bool Loaded() const { return m_loaded; }
		/// Cimetières visités présents dans le catalogue.
		uint32_t VisitedCount() const { return m_visitedKnown; }
		/// Vrai si le cimetière \p graveyardId a déjà été visité (même inconnu du catalogue).
		bool HasVisited(std::string_view graveyardId) const { return m_visited.count(std::string(graveyardId)) != 0u; }
		/// Vrai si le lieu secret \p placeId a déjà été découvert.
		bool HasDiscovered(std::string_view placeId) const { return m_discovered.count(std::string(placeId)) != 0u; }
		/// Indices (catalogue) des lieux secrets découverts, dans l'ordre du catalogue.
		std::vector<uint32_t> DiscoveredPlaceIndices(const DiscoveryCatalog& catalog) const;

		/// Cimetières visités pendant la session (à persister par INSERT IGNORE ; la liste
		/// n'est jamais vidée : rejouer une insertion est sans effet).
		const std::vector<std::string>& SessionGraveyardVisits() const { return m_sessionGraveyards; }
		/// Lieux secrets découverts pendant la session (même règle).
		const std::vector<std::string>& SessionSecretDiscoveries() const { return m_sessionSecrets; }
		/// Vrai dès que l'exploit est atteint (pendant la session ou rattrapé au chargement).
		bool ExploitGrantPending() const { return m_grantPending; }
		/// Vrai si l'exploit est débloqué (en base au chargement ou pendant la session).
		bool PilgrimExploitUnlocked() const { return m_pilgrimUnlocked; }

	private:
		/// Pose l'exploit si toutes les conditions sont réunies. \return true s'il vient
		/// d'être débloqué.
		bool CheckPilgrim(const DiscoveryCatalog& catalog);
		/// Ajoute une visite si le cimetière est connu et nouveau. \return true si nouvelle.
		bool AddVisit(const DiscoveryCatalog& catalog, std::string_view graveyardId, DiscoveryEvents& out);

		bool m_loaded = false;
		std::unordered_set<std::string> m_visited;
		std::unordered_set<std::string> m_discovered;
		uint32_t m_visitedKnown = 0;
		std::vector<std::string> m_sessionGraveyards;
		std::vector<std::string> m_sessionSecrets;
		bool m_pilgrimUnlocked = false;
		bool m_grantPending = false;
		std::vector<uint32_t> m_scratch;
	};
}
