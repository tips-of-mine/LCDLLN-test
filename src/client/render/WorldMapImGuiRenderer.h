#pragma once

// Monde jouable (chantier « Royaumes ») — rendu ImGui de la carte du monde
// (touche M) et de la bannière d'entrée de lieu.
//
// Données : engine::client::WorldPlaces (places.json) + texture de la carte
// (world/<monde>/world_map.png, chargée par SkillIconCache côté Engine) +
// tracés vectoriels des routes et bâtiments (world/<monde>/map_features.bin, FIX.12).
// Aucune logique de jeu ici : uniquement du dessin + l'état de vue (zoom/pan).
//
// Thread / timing : main thread, entre ImGui::NewFrame() et ImGui::Render().
// Compilé sur toutes les plateformes, mais le corps n'existe que sous _WIN32
// (ImGui n'est lié que dans le client Windows) ; ailleurs les fonctions sont
// des no-op.

#include "src/shared/network/ServerProtocol.h"

#include <cstdint>
#include <vector>

namespace engine::client { class WorldPlaces; class PlaceBannerTracker; class MapDiscoveryClient; enum class ZoneLevelTone : uint8_t; }
namespace engine::world::mapfeatures { struct MapFeatures; }
struct ImDrawList;

namespace engine::render
{
	/// État de vue de la carte (persistant entre frames, propriété d'Engine).
	struct WorldMapViewState
	{
		float zoom = 1.0f;       ///< 1 = carte entière ; max = taille du monde / minViewM (FIX.11)
		float centerU = 0.5f;    ///< centre de vue (coordonnées normalisées carte)
		float centerV = 0.5f;
		/// Recentre sur le joueur à chaque frame. Lot D (bug 2) : coupé seulement en
		/// glissant la carte ; rétabli par « Centrer », ou de lui-même quand le joueur
		/// s'est déplacé depuis la fin du glisser et sort de la vue (cf. DrawWorldMapWindow).
		bool followPlayer = true;
		/// Lot D (bug 2) — position du joueur (m, repère global) quand le suivi a été
		/// coupé (fin du glisser / « Vue d'ensemble ») : sert à détecter qu'il a bougé.
		float anchorX = 0.0f;
		float anchorZ = 0.0f;
		bool anchorValid = false;
		/// FIX.11 — vrai à chaque ouverture (Engine remet l'état à zéro) : la frame
		/// suivante fixe le zoom initial (≈ defaultViewM visibles) et recentre.
		bool initPending = true;
		/// Déroulé du parchemin (23/11/2026) — temps écoulé (s) depuis l'ouverture.
		float unrollElapsedS = 0.0f;
		/// Taille « pleine » de la fenêtre (px), relevée à la première frame de
		/// l'ouverture (celle mémorisée par ImGui, redimensionnement du joueur compris) ;
		/// 0 = pas encore relevée.
		float fullW = 0.0f;
		float fullH = 0.0f;
		/// Vrai une fois le déroulé terminé : la taille n'est plus imposée et le
		/// joueur peut de nouveau redimensionner la fenêtre.
		bool unrollDone = false;
	};

	/// Entrées de la fenêtre Carte (une frame).
	struct WorldMapHudInput
	{
		const engine::client::WorldPlaces* places = nullptr;
		uint64_t mapTexture = 0;    ///< ImTextureID de world_map.png (0 = fond uni)
		/// Habillage parchemin (25/11/2026, textures Blender de game/data/ui/map/) :
		/// ImTextureID chargés par SkillIconCache ; 0 = absente (repli vectoriel).
		uint64_t rollTexture = 0;      ///< ui/map/map_roll.png (rouleau en 3 tranches)
		float rollTexW = 0.0f;         ///< taille native de map_roll.png (texels)
		float rollTexH = 0.0f;
		uint64_t parchmentTexture = 0; ///< ui/map/map_parchment.png (répétable ; teinté = inexploré)
		uint64_t edgeTexture = 0;      ///< ui/map/map_edge.png (bord usé en 9 tranches)
		float playerX = 0.0f;       ///< position joueur (m)
		float playerZ = 0.0f;
		float playerYaw = 0.0f;     ///< cap (rad) : avant = (sin yaw, cos yaw) en XZ
		const char* playerFaction = "";///< faction de jeu du personnage (met en évidence sa ville de départ, v1.0.61)
		float viewportW = 1280.0f;  ///< taille du framebuffer (px) pour le placement initial
		float viewportH = 720.0f;
		/// Carte à découvrir : seules les cases découvertes sont montrées (le reste
		/// est couvert d'un voile, villes et lieux y sont masqués). nullptr = carte
		/// entière visible (zones historiques).
		const engine::client::MapDiscoveryClient* discovery = nullptr;
		/// FIX.11 — côté visible (m) à l'ouverture de la carte
		/// (client.world_map.default_view_m, défaut 2000).
		float defaultViewM = 2000.0f;
		/// FIX.11 — côté visible (m) au zoom maximal (client.world_map.min_view_m,
		/// défaut 250).
		float minViewM = 250.0f;
		/// FIX.12 — routes, rues et bâtiments en vectoriel (map_features.bin),
		/// dessinés sous le voile des zones non découvertes. nullptr = absents.
		const engine::world::mapfeatures::MapFeatures* features = nullptr;
		/// Lore v1.0.114 — lieux secrets découverts PAR CE personnage (UIModel.discovery,
		/// reçus du shard ; jamais dans places.json). Seuls ceux de \ref currentMap sont
		/// dessinés (icône distincte, nom au survol). nullptr = aucun.
		const std::vector<engine::server::DiscoveredPlaceWire>* secretPlaces = nullptr;
		/// Carte affichée (engine::world::cells::CellMap : 0 Royaumes, 1 Enfers).
		uint8_t currentMap = 0;
	};

	/// Dessine la fenêtre « Carte du monde », habillée en parchemin (fond clair, encre
	/// brune, rouleaux de bois en haut et en bas, en-tête dessiné sans barre de titre
	/// ImGui) et déroulée sur kWorldMapUnrollSeconds à chaque ouverture. Les noms des
	/// lieux n'apparaissent qu'au survol (WorldMapPresentation.h).
	/// \param open mis à false si le
	/// joueur ferme la fenêtre (bouton X de l'en-tête). \param view zoom / pan / suivi du joueur, modifié
	/// par la molette, le glisser et les boutons du pied. Lot D : la zone de dessin
	/// occupe toute la région de contenu (redimensionnement suivi à chaque frame,
	/// échelle identique sur les deux axes) et la carte suit le joueur par défaut.
	/// Sans effet si `in.places` est nul ou non chargé. Main thread, entre
	/// ImGui::NewFrame() et ImGui::Render().
	void DrawWorldMapWindow(bool* open, const WorldMapHudInput& in, WorldMapViewState& view);

	/// Dessine la bannière « Vous entrez à … » (haut-centre, fondu) sur la draw
	/// list \p fg (foreground). \param dw,dh taille du framebuffer (px).
	/// \param tone v1.0.118 — teinte du titre selon l'écart de niveau entre le personnage
	/// et la région (ZoneLevelToneFor) ; Inconnu = or pâle habituel.
	void DrawPlaceBanner(ImDrawList* fg, float dw, float dh, const engine::client::PlaceBannerTracker& banner,
		engine::client::ZoneLevelTone tone);
}
