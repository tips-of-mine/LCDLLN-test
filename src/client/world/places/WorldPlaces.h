#pragma once

// Monde jouable (chantier « Royaumes ») — lieux du monde côté client.
//
// `game/data/world/<monde>/places.json` (GÉNÉRÉ par tools/world_gen) liste les
// villes, lieux remarquables (POI) et régions, avec leur position monde (m).
// Il alimente :
//   - la fenêtre « Carte du monde » (touche M) : marqueurs, libellés, survol ;
//   - la bannière d'entrée de lieu (« Vous entrez à L'hynn — Terres de l'Empereur »).
//
// Module PUR (Config + FileSystem, ni ImGui ni Vulkan) → testé en CI
// (world_places_tests). Le rendu ImGui vit dans WorldMapImGuiRenderer.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core { class Config; }

namespace engine::client
{
	/// Ville / village / camp du monde.
	struct PlaceCity
	{
		std::string id;
		std::string name;
		std::string kind;        ///< capitale, grande_ville, ville, village, hameau, forteresse, camp, ruine…
		std::string race;        ///< peuple dominant (humains, elfes, orcs, nains, demons, neutre…)
		std::string state;       ///< intact / ruine
		std::string description;
		std::vector<std::string> startFor; ///< factions de jeu (factions.json) dont c'est la ville de départ (v1.0.61)
		float x = 0.0f;          ///< position monde (m)
		float z = 0.0f;
		float radiusM = 20.0f;   ///< rayon de l'agglomération (m)
	};

	/// Lieu remarquable (monolithe, ruine, gouffre…).
	struct PlacePoi
	{
		std::string id;
		std::string name;
		std::string kind;
		std::string description;
		float x = 0.0f;
		float z = 0.0f;
	};

	/// v1.0.118 — niveaux de référence d'une région pour une faction de jeu
	/// (places.json `regions[i].levels[j]`, recopiés du lore par tools/lore/sync_levels.py).
	struct PlaceLevelBand
	{
		std::string faction; ///< faction de jeu (game/data/races/factions.json, ex. « lumiere »)
		int minLevel = 1;
		int maxLevel = 1;
	};

	/// Région (rectangle aligné sur les axes, en m).
	struct PlaceRegion
	{
		std::string id;
		std::string name;
		float xMin = 0.0f, xMax = 0.0f, zMin = 0.0f, zMax = 0.0f;
		std::vector<PlaceLevelBand> levels; ///< v1.0.118 — vide = niveaux inconnus
		bool Contains(float x, float z) const { return x >= xMin && x <= xMax && z >= zMin && z <= zMax; }
		float Area() const { return (xMax - xMin) * (zMax - zMin); }
		/// Bande de niveaux de la faction de jeu \p faction, ou nullptr si inconnue.
		const PlaceLevelBand* LevelsFor(std::string_view faction) const;
	};

	/// v1.0.118 — teinte du nom de zone selon l'écart entre le niveau du personnage et la
	/// bande de niveaux de la région pour sa faction (décision du 13/11/2026).
	enum class ZoneLevelTone : uint8_t
	{
		Inconnu = 0, ///< niveaux de la région inconnus : couleur habituelle de la bannière
		Gris,        ///< niveau > max + 5 : zone sans intérêt
		Vert,        ///< max < niveau <= max + 5 : zone facile
		Jaune,       ///< min <= niveau <= max : zone à son niveau
		Orange,      ///< min - 5 <= niveau < min : zone difficile
		Rouge,       ///< niveau < min - 5 : zone dangereuse
	};

	/// Teinte pour un personnage de niveau \p playerLevel dans la bande [\p minLevel, \p maxLevel].
	/// Écart de 5 niveaux (même seuil que la règle des niveaux par faction, LORE_BD §16.32).
	ZoneLevelTone ZoneLevelToneFor(int playerLevel, int minLevel, int maxLevel);
	/// Teinte pour la région \p region (nullptr → Inconnu) et la faction de jeu \p faction.
	ZoneLevelTone ZoneLevelToneFor(const PlaceRegion* region, std::string_view faction, int playerLevel);
	/// Couleur RGB (0-255, sans alpha) d'une teinte ; Inconnu → or pâle de la bannière.
	void ZoneLevelToneRgb(ZoneLevelTone tone, uint8_t& r, uint8_t& g, uint8_t& b);

	class WorldPlaces final
	{
	public:
		/// Parse le JSON en mémoire (remplace le contenu).
		/// \return false si JSON invalide, schema_version ≠ 1 ou terrain absent.
		bool LoadFromText(std::string_view json, std::string& outError);

		/// Charge `world/<worldId>/places.json` depuis `paths.content`.
		/// \return false (table vide) si absent/invalide — non bloquant (pas de carte).
		/// Effet de bord : lecture disque.
		bool LoadFromContent(const engine::core::Config& config, std::string_view worldId, std::string& outError);

		bool IsLoaded() const { return m_loaded; }
		const std::string& DisplayName() const { return m_displayName; }
		/// Chemin RELATIF (depuis paths.content) de l'image de carte (PNG).
		const std::string& MapImage() const { return m_mapImage; }
		const std::vector<PlaceCity>& Cities() const { return m_cities; }
		const std::vector<PlacePoi>& Pois() const { return m_pois; }
		const std::vector<PlaceRegion>& Regions() const { return m_regions; }
		/// Cellules générées (jouables) du monde « Royaumes » : identifiants du lore
		/// (« N003_W009 »), dans l'ordre du fichier. Vide pour un monde historique.
		const std::vector<std::string>& GeneratedCells() const { return m_generatedCells; }

		/// Convertit une position monde (m) en coordonnées normalisées de la
		/// carte : u ∈ [0,1] vers l'est (+X), v ∈ [0,1] vers le sud (+Z).
		/// \return false si la position est hors de l'emprise du terrain.
		bool WorldToMapUv(float x, float z, float& outU, float& outV) const;
		/// Inverse de WorldToMapUv (u, v non bornés).
		void MapUvToWorld(float u, float v, float& outX, float& outZ) const;

		/// Ville dont l'agglomération contient (x, z) — rayon + \p marginM —, la
		/// plus proche du centre si plusieurs ; nullptr sinon.
		const PlaceCity* CityAt(float x, float z, float marginM = 0.0f) const;
		/// Région la plus PETITE contenant (x, z) (les régions peuvent se
		/// chevaucher : la plus spécifique gagne) ; nullptr sinon.
		const PlaceRegion* RegionAt(float x, float z) const;
		/// Région d'identifiant \p id, ou nullptr.
		const PlaceRegion* RegionById(std::string_view id) const;
		/// Ville de départ de la faction de jeu \p faction (identifiant exact de
		/// game/data/races/factions.json, ex. "lumiere" ; v1.0.61), ou nullptr.
		const PlaceCity* StartCityFor(std::string_view faction) const;

		/// FIX.11 — côté du monde couvert par la carte (m, `terrain.size_m`).
		float SizeM() const { return m_sizeM; }

	private:
		bool m_loaded = false;
		std::string m_displayName;
		std::string m_mapImage;
		float m_originX = 0.0f, m_originZ = 0.0f, m_sizeM = 1.0f;
		std::vector<PlaceCity> m_cities;
		std::vector<PlacePoi> m_pois;
		std::vector<PlaceRegion> m_regions;
		std::vector<std::string> m_generatedCells;
	};

	/// Bannière « Vous entrez à … » : suit le lieu courant du joueur et arme un
	/// texte transitoire à chaque changement de ville ou de région.
	/// Hystérésis : on « quitte » une ville à rayon + 14 m (on y « entre » à
	/// rayon + 6 m) pour éviter le clignotement en bordure.
	class PlaceBannerTracker final
	{
	public:
		static constexpr float kDurationSec = 5.0f;

		/// Met à jour l'état depuis la position joueur. \param dtSec durée de la frame (s).
		/// Premier appel : pas de bannière pour la région (seulement pour la ville
		/// si le joueur apparaît dedans — accueil).
		void Update(const WorldPlaces& places, float x, float z, float dtSec);

		bool Visible() const { return m_timerSec > 0.0f && !m_title.empty(); }
		/// Opacité [0,1] : fondu d'entrée 0,4 s et de sortie 1,2 s.
		float Alpha() const;
		const std::string& Title() const { return m_title; }
		const std::string& Subtitle() const { return m_subtitle; }
		const std::string& CurrentCityId() const { return m_cityId; }
		const std::string& CurrentRegionId() const { return m_regionId; }
		/// v1.0.118 — région du lieu affiché par la bannière (celle de la ville ou la région
		/// elle-même), figée à l'armement : sert à la couleur du titre. Vide = aucune.
		const std::string& ShownRegionId() const { return m_shownRegionId; }

	private:
		void Show(std::string title, std::string subtitle, std::string regionId);

		bool m_initialized = false;
		std::string m_cityId;
		std::string m_regionId;
		std::string m_title;
		std::string m_subtitle;
		std::string m_shownRegionId;
		float m_timerSec = 0.0f;
	};

	/// Libellé français lisible d'un type de ville (« capitale » → « Capitale »,
	/// « grande_ville » → « Grande ville »…) ou d'un genre de POI (« faille »,
	/// « portail », « foret », « etang », « champ_bataille », « volcan », « gouffre »,
	/// « temple », « sanctuaire », « port », « cite », « defile », « salle », « elevage »…).
	/// Inconnu → le type brut.
	std::string PlaceKindLabel(std::string_view kind);
}
