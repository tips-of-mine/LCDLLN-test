#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace engine::editor
{
	/// Polyligne " route " (ticket **011** branche A) : points monde XZ + largeur ; peinture sur une couche splat (ex. terre = macadam).
	struct WorldMapRoutePolyline
	{
		std::vector<std::pair<double, double>> pointsXz;
		double     widthM     = 4.0;
		uint32_t splatLayer = 1u; ///< 0-3 (R,G,B,A) - defaut 1 = terre.
	};

	/// Instance decor / prop pour export `layout_from_editor.json` (schema minimal \c zone_builder : \c guid, \c gltf, \c position).
	struct WorldMapEditLayoutInstance
	{
		std::string guid;
		/// Chemin glTF relatif au content (ex. \c zones/zone_0/zone_0.gltf).
		std::string gltfContentRelativePath;
		/// Position monde metres (snap sol) - convertie a l'export en coordonnees attendues par \c LayoutImporter.
		double worldX = 0.0;
		double worldY = 0.0;
		double worldZ = 0.0;
		double yawDegrees = 0.0;
		double uniformScale = 1.0;
		/// Ticket **013** : catalogue arbres (vide = prop generique type rocher / ancien combo 009).
		std::string speciesId;
		uint32_t shapeVariantIndex = 0u;
	};

	/// Document d'edition carte (JSON lisible, versionne). Les chemins \c heightmap* sont relatifs a \c paths.content.
	struct WorldMapEditDocument
	{
		static constexpr int kFormatVersion = 1;

		std::string zoneId = "untitled_zone";
		int formatVersion = kFormatVersion;
		/// Resolution NxN du heightmap (fichier .r16h).
		uint32_t heightmapResolution = 256;
		bool hasSeed = false;
		int64_t seed = 0;
		std::string heightmapContentRelativePath;
		/// Splat RGBA (fichier SLAP, voir `TerrainEditingTools::SaveSplatMap`), relatif a \c paths.content.
		std::string splatmapContentRelativePath;
		/// Masque herbe / detail surface R8 (fichier GRMS, ticket 010), meme resolution que la splat, relatif au content.
		std::string grassMaskContentRelativePath;
		std::vector<std::string> textureAssets;
		/// Liste des sons importes par l'editeur (chemins relatifs au content, ex. \c audio/footstep/sand.wav).
		/// Persiste en JSON (cle \c audio_assets). Sert de catalogue pour les dropdowns " son de pas par couche "
		/// et pour la persistance generale des sons utilises par cette carte.
		std::vector<std::string> audioAssets;
		/// Mapping couche splat -> texture importee (reference un chemin present dans \c textureAssets).
		/// Index : 0=Herbe (R), 1=Terre (G), 2=Roc (B), 3=Neige (A). Vide = couche par defaut moteur.
		/// Persiste en JSON (cle \c splat_layer_texture_refs). Utilise par l'export runtime pour
		/// substituer la texture engine-default par celle choisie par l'utilisateur.
		std::array<std::string, 4> splatLayerTextureRefs{};
		/// Mapping couche splat -> son de pas (reference un chemin present dans \c audioAssets).
		/// Index : 0=Herbe (R), 1=Terre (G), 2=Roc (B), 3=Neige (A). Vide = aucun son specifique.
		/// Persiste en JSON (cle \c splat_layer_footstep_audio_refs). Le branchement gameplay (lecture
		/// au deplacement du joueur en lisant la couche splat dominante sous ses pieds) sera fait dans
		/// une iteration ulterieure cote runtime du jeu - l'editeur en assure deja la persistance.
		std::array<std::string, 4> splatLayerFootstepAudioRefs{};
		/// Identifiants de prefabs / objets (MVP : chaines libres).
		std::vector<std::string> objectPrefabIds;
		/// Instances pour \c layout_from_editor.json (ticket 009).
		std::vector<WorldMapEditLayoutInstance> layoutInstances;
		/// Routes splat (ticket **011** A) : metadonnees ; le rendu persistant est dans le fichier SLAP.
		std::vector<WorldMapRoutePolyline> routes;

		/// Si vrai, \ref terrainWorldSizeM remplace `terrain.world_size` pour l'init terrain du World Editor (alignement zone logique).
		bool   hasTerrainWorldSizeM = false;
		double terrainWorldSizeM    = 10000.0;

		/// Eau (Lot G) : si \c waterEnabled, une surface plane a \c waterLevelMeters (Y monde) doit etre rendue.
		/// Persiste en JSON (cles \c water_enabled, \c water_level_m). Le rendu effectif (passe Vulkan
		/// transparent + shader simple) sera branche dans une iteration moteur ulterieure - l'editeur
		/// expose deja la donnee pour que la creation de cartes avec eau soit possible des maintenant.
		bool   waterEnabled    = false;
		double waterLevelMeters = 0.0;

		/// Atmosphere (C5) : etat du cycle jour/nuit sauvegarde par zone. Si
		/// hasAtmosphere=true, l'editeur applique ces valeurs au DayNightCycle
		/// au chargement de la carte, et les sauvegarde au save / export.
		/// timeOfDayHours dans [0, 24) ; timeScale en secondes reelles par heure
		/// in-game (60 = 24 min reel = 1 jour jeu, 3600 = 1:1 reel).
		/// Persiste en JSON (cles \c atmosphere.time_of_day_h, \c atmosphere.time_scale).
		bool   hasAtmosphere    = false;
		double timeOfDayHours   = 8.0;
		double timeScale        = 60.0;
	};

} // namespace engine::editor
