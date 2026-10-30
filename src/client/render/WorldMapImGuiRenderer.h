#pragma once

// Monde jouable (chantier « Royaumes ») — rendu ImGui de la carte du monde
// (touche M) et de la bannière d'entrée de lieu.
//
// Données : engine::client::WorldPlaces (places.json) + texture de la carte
// (world/<monde>/world_map.png, chargée par SkillIconCache côté Engine).
// Aucune logique de jeu ici : uniquement du dessin + l'état de vue (zoom/pan).
//
// Thread / timing : main thread, entre ImGui::NewFrame() et ImGui::Render().
// Compilé sur toutes les plateformes, mais le corps n'existe que sous _WIN32
// (ImGui n'est lié que dans le client Windows) ; ailleurs les fonctions sont
// des no-op.

#include <cstdint>

namespace engine::client { class WorldPlaces; class PlaceBannerTracker; class MapDiscoveryClient; }
struct ImDrawList;

namespace engine::render
{
	/// État de vue de la carte (persistant entre frames, propriété d'Engine).
	struct WorldMapViewState
	{
		float zoom = 1.0f;       ///< 1 = carte entière ; max = taille du monde / minViewM (FIX.11)
		float centerU = 0.5f;    ///< centre de vue (coordonnées normalisées carte)
		float centerV = 0.5f;
		bool followPlayer = true;///< recentre sur le joueur à chaque frame (désactivé au glisser)
		/// FIX.11 — vrai à chaque ouverture (Engine remet l'état à zéro) : la frame
		/// suivante fixe le zoom initial (≈ defaultViewM visibles) et recentre.
		bool initPending = true;
	};

	/// Entrées de la fenêtre Carte (une frame).
	struct WorldMapHudInput
	{
		const engine::client::WorldPlaces* places = nullptr;
		uint64_t mapTexture = 0;    ///< ImTextureID de world_map.png (0 = fond uni)
		float playerX = 0.0f;       ///< position joueur (m)
		float playerZ = 0.0f;
		float playerYaw = 0.0f;     ///< cap (rad) : avant = (sin yaw, cos yaw) en XZ
		const char* playerRace = "";///< race du personnage (met en évidence sa ville de départ)
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
	};

	/// Dessine la fenêtre « Carte du monde ». \param open mis à false si le
	/// joueur ferme la fenêtre (✕). \param view zoom / pan, modifié par la molette
	/// et le glisser. Sans effet si `in.places` est nul ou non chargé.
	void DrawWorldMapWindow(bool* open, const WorldMapHudInput& in, WorldMapViewState& view);

	/// Dessine la bannière « Vous entrez à … » (haut-centre, fondu) sur la draw
	/// list \p fg (foreground). \param dw,dh taille du framebuffer (px).
	void DrawPlaceBanner(ImDrawList* fg, float dw, float dh, const engine::client::PlaceBannerTracker& banner);
}
