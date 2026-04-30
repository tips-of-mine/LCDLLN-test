#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace engine::editor
{
	/// Polyligne « route » (ticket **011** branche A) : points monde XZ + largeur ; peinture sur une couche splat (ex. terre = macadam).
	struct WorldMapRoutePolyline
	{
		std::vector<std::pair<double, double>> pointsXz;
		double     widthM     = 4.0;
		uint32_t splatLayer = 1u; ///< 0–3 (R,G,B,A) — défaut 1 = terre.
	};

	/// Instance décor / prop pour export `layout_from_editor.json` (schéma minimal \c zone_builder : \c guid, \c gltf, \c position).
	struct WorldMapEditLayoutInstance
	{
		std::string guid;
		/// Chemin glTF relatif au content (ex. \c zones/zone_0/zone_0.gltf).
		std::string gltfContentRelativePath;
		/// Position monde mètres (snap sol) — convertie à l’export en coordonnées attendues par \c LayoutImporter.
		double worldX = 0.0;
		double worldY = 0.0;
		double worldZ = 0.0;
		double yawDegrees = 0.0;
		double uniformScale = 1.0;
		/// Ticket **013** : catalogue arbres (vide = prop générique type rocher / ancien combo 009).
		std::string speciesId;
		uint32_t shapeVariantIndex = 0u;
	};

	/// Document d’édition carte (JSON lisible, versionné). Les chemins \c heightmap* sont relatifs à \c paths.content.
	struct WorldMapEditDocument
	{
		static constexpr int kFormatVersion = 1;

		std::string zoneId = "untitled_zone";
		int formatVersion = kFormatVersion;
		/// Résolution N×N du heightmap (fichier .r16h).
		uint32_t heightmapResolution = 256;
		bool hasSeed = false;
		int64_t seed = 0;
		std::string heightmapContentRelativePath;
		/// Splat RGBA (fichier SLAP, voir `TerrainEditingTools::SaveSplatMap`), relatif à \c paths.content.
		std::string splatmapContentRelativePath;
		/// Masque herbe / détail surface R8 (fichier GRMS, ticket 010), même résolution que la splat, relatif au content.
		std::string grassMaskContentRelativePath;
		std::vector<std::string> textureAssets;
		/// Liste des sons importés par l'éditeur (chemins relatifs au content, ex. \c audio/footstep/sand.wav).
		/// Persisté en JSON (clé \c audio_assets). Sert de catalogue pour les dropdowns « son de pas par couche »
		/// et pour la persistance générale des sons utilisés par cette carte.
		std::vector<std::string> audioAssets;
		/// Mapping couche splat → texture importée (référence un chemin présent dans \c textureAssets).
		/// Index : 0=Herbe (R), 1=Terre (G), 2=Roc (B), 3=Neige (A). Vide = couche par défaut moteur.
		/// Persisté en JSON (clé \c splat_layer_texture_refs). Utilisé par l'export runtime pour
		/// substituer la texture engine-default par celle choisie par l'utilisateur.
		std::array<std::string, 4> splatLayerTextureRefs{};
		/// Mapping couche splat → son de pas (référence un chemin présent dans \c audioAssets).
		/// Index : 0=Herbe (R), 1=Terre (G), 2=Roc (B), 3=Neige (A). Vide = aucun son spécifique.
		/// Persisté en JSON (clé \c splat_layer_footstep_audio_refs). Le branchement gameplay (lecture
		/// au déplacement du joueur en lisant la couche splat dominante sous ses pieds) sera fait dans
		/// une itération ultérieure côté runtime du jeu — l'éditeur en assure déjà la persistance.
		std::array<std::string, 4> splatLayerFootstepAudioRefs{};
		/// Identifiants de préfabs / objets (MVP : chaînes libres).
		std::vector<std::string> objectPrefabIds;
		/// Instances pour \c layout_from_editor.json (ticket 009).
		std::vector<WorldMapEditLayoutInstance> layoutInstances;
		/// Routes splat (ticket **011** A) : métadonnées ; le rendu persistant est dans le fichier SLAP.
		std::vector<WorldMapRoutePolyline> routes;

		/// Si vrai, \ref terrainWorldSizeM remplace `terrain.world_size` pour l’init terrain du World Editor (alignement zone logique).
		bool   hasTerrainWorldSizeM = false;
		double terrainWorldSizeM    = 10000.0;

		/// Eau (Lot G) : si \c waterEnabled, une surface plane à \c waterLevelMeters (Y monde) doit être rendue.
		/// Persisté en JSON (clés \c water_enabled, \c water_level_m). Le rendu effectif (passe Vulkan
		/// transparent + shader simple) sera branché dans une itération moteur ultérieure — l'éditeur
		/// expose déjà la donnée pour que la création de cartes avec eau soit possible dès maintenant.
		bool   waterEnabled    = false;
		double waterLevelMeters = 0.0;
	};

} // namespace engine::editor
