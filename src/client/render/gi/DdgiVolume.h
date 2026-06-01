#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <string>

namespace engine::render::gi
{
	/// Configuration d'une grille DDGI (Dynamic Diffuse Global Illumination).
	///
	/// La grille est un pavage régulier de sondes (probes) dans le monde. Chaque
	/// sonde stocke une irradiance encodée en projection octaédrique (irradianceTexels²)
	/// et une visibilité/profondeur (visibilityTexels²). M45.6 ne fait que décrire
	/// la STRUCTURE : aucune sonde n'est rendue, aucune ressource GPU n'est allouée
	/// tant que l'utilisateur ne l'active pas explicitement (gating côté Engine).
	struct DdgiGridConfig
	{
		float origin[3]{ 0.0f, 0.0f, 0.0f }; ///< Origine monde de la sonde (0,0,0), en mètres.
		float spacing[3]{ 2.0f, 2.0f, 2.0f }; ///< Espacement entre sondes par axe, en mètres.
		uint32_t counts[3]{ 8u, 8u, 4u }; ///< Nombre de sondes par axe (X, Y, Z).
		uint32_t irradianceTexels{ 8u }; ///< Résolution octaédrique d'irradiance par sonde (côté, hors bordure).
		uint32_t visibilityTexels{ 16u }; ///< Résolution octaédrique de visibilité par sonde (côté, hors bordure).
	};

	/// Structure et ressources d'un volume DDGI.
	///
	/// PHASE 1 (M45.6) : la classe fournit toute la math d'indexation et de layout
	/// d'atlas (méthodes CPU pures, testables sans device Vulkan) ainsi qu'une
	/// allocation de ressources GPU persistantes (Allocate/Destroy) qui n'est JAMAIS
	/// appelée par défaut. Aucune passe de rendu, aucun shader, aucun usage VRAM avec
	/// la configuration par défaut.
	///
	/// PACKING D'ATLAS (figé, réutilisé par M45.7) :
	/// Chaque sonde occupe une tuile carrée de (texels + 2) pixels : 1 pixel de bordure
	/// de chaque côté pour permettre un filtrage bilinéaire correct au bord octaédrique.
	/// Les tuiles sont disposées en grille 2D :
	///   - AtlasCols = counts[0] * counts[2]  (le plan XZ « déplié » sur une ligne)
	///   - AtlasRows = counts[1]              (un étage vertical par rangée)
	/// La colonne d'une sonde d'indice (ix, iy, iz) est col = ix + iz * counts[0],
	/// sa rangée est row = iy. L'origine pixel de sa tuile est donc
	/// (col * tileSize, row * tileSize). Ce mapping est déterministe et identique
	/// pour l'atlas d'irradiance et de visibilité (seul tileSize diffère).
	class DdgiVolume
	{
	public:
		DdgiVolume() = default;
		DdgiVolume(const DdgiVolume&) = delete;
		DdgiVolume& operator=(const DdgiVolume&) = delete;

		// --- Configuration (CPU pur) ---

		/// Renvoie la configuration courante de la grille.
		const DdgiGridConfig& Config() const { return m_config; }

		/// Remplace la configuration de la grille. N'alloue rien : à appeler avant Allocate.
		void SetConfig(const DdgiGridConfig& config) { m_config = config; }

		// --- Indexation des sondes (CPU pur) ---

		/// Nombre total de sondes = counts[0] * counts[1] * counts[2].
		uint32_t ProbeCount() const;

		/// Indice linéaire d'une sonde à partir de ses coordonnées de grille.
		/// Ordre : X varie le plus vite, puis Y, puis Z.
		uint32_t ProbeIndex(uint32_t ix, uint32_t iy, uint32_t iz) const;

		/// Inverse de ProbeIndex : retrouve (ix, iy, iz) depuis l'indice linéaire.
		void GridCoord(uint32_t index, uint32_t& ix, uint32_t& iy, uint32_t& iz) const;

		/// Position monde du centre d'une sonde = origin + i * spacing par axe (mètres).
		void ProbeWorldPos(uint32_t ix, uint32_t iy, uint32_t iz, float& x, float& y, float& z) const;

		// --- Layout d'atlas (CPU pur, cf. PACKING D'ATLAS ci-dessus) ---

		/// Côté en pixels d'une tuile d'irradiance = irradianceTexels + 2 (bordure 1px).
		uint32_t IrradianceTileSize() const { return m_config.irradianceTexels + 2u; }

		/// Côté en pixels d'une tuile de visibilité = visibilityTexels + 2 (bordure 1px).
		uint32_t VisibilityTileSize() const { return m_config.visibilityTexels + 2u; }

		/// Nombre de colonnes de tuiles dans l'atlas = counts[0] * counts[2] (plan XZ déplié).
		uint32_t AtlasCols() const { return m_config.counts[0] * m_config.counts[2]; }

		/// Nombre de rangées de tuiles dans l'atlas = counts[1] (un étage par rangée).
		uint32_t AtlasRows() const { return m_config.counts[1]; }

		/// Largeur en pixels de l'atlas d'irradiance = AtlasCols * IrradianceTileSize.
		uint32_t IrradianceAtlasWidth() const { return AtlasCols() * IrradianceTileSize(); }

		/// Hauteur en pixels de l'atlas d'irradiance = AtlasRows * IrradianceTileSize.
		uint32_t IrradianceAtlasHeight() const { return AtlasRows() * IrradianceTileSize(); }

		/// Largeur en pixels de l'atlas de visibilité = AtlasCols * VisibilityTileSize.
		uint32_t VisibilityAtlasWidth() const { return AtlasCols() * VisibilityTileSize(); }

		/// Hauteur en pixels de l'atlas de visibilité = AtlasRows * VisibilityTileSize.
		uint32_t VisibilityAtlasHeight() const { return AtlasRows() * VisibilityTileSize(); }

		/// Origine pixel (coin haut-gauche) de la tuile d'une sonde dans l'atlas.
		/// \param probeIndex indice linéaire de la sonde (cf. ProbeIndex).
		/// \param tileSize côté de tuile à utiliser (IrradianceTileSize ou VisibilityTileSize).
		/// \param px [out] colonne pixel de l'origine de la tuile.
		/// \param py [out] rangée pixel de l'origine de la tuile.
		void ProbeAtlasTileOrigin(uint32_t probeIndex, uint32_t tileSize, uint32_t& px, uint32_t& py) const;

		// --- Ressources GPU (NON appelées par défaut, gated côté Engine) ---

		/// Alloue les 2 images persistantes (irradiance R16G16B16A16_SFLOAT, visibilité
		/// R16G16_SFLOAT) + leurs vues, dimensionnées sur les atlas ci-dessus.
		/// Usage STORAGE | SAMPLED. Mémoire device-local via vkAllocateMemory (miroir du
		/// pattern d'image persistante de HiZPyramidPass — pas de dépendance VMA requise).
		/// Logue la VRAM estimée. En cas d'échec, nettoie tout et renseigne `err`.
		/// \param vmaAllocator inutilisé en phase 1 (pattern vkAllocateMemory) ; réservé.
		/// \return true si les deux images + vues sont prêtes.
		/// \note NE PAS appeler depuis un chemin de rendu ; réservé à un boot gated (gi.ddgi.enabled).
		bool Allocate(VkDevice device, VkPhysicalDevice phys, void* vmaAllocator, std::string& err);

		/// Libère les images, mémoires et vues. Sûr même si rien n'est alloué.
		/// \param vmaAllocator inutilisé en phase 1 ; réservé pour symétrie avec Allocate.
		void Destroy(VkDevice device, void* vmaAllocator);

		/// Vue de l'atlas d'irradiance (VK_NULL_HANDLE si non alloué).
		VkImageView IrradianceView() const { return m_irradianceView; }

		/// Image de l'atlas d'irradiance (VK_NULL_HANDLE si non alloué).
		/// M45.7 : nécessaire pour poser les barrières de layout (GENERAL <->
		/// SHADER_READ_ONLY) sur l'image persistante depuis la passe de mise à jour.
		VkImage IrradianceImage() const { return m_irradianceImage; }

		/// Vue de l'atlas de visibilité (VK_NULL_HANDLE si non alloué).
		VkImageView VisibilityView() const { return m_visibilityView; }

		/// true si Allocate a réussi et que les ressources GPU sont vivantes.
		bool IsAllocated() const { return m_allocated; }

	private:
		/// Crée une VkImage 2D device-local + mémoire (pattern vkCreateImage/vkAllocateMemory).
		/// \param format format de l'image (ex. R16G16B16A16_SFLOAT).
		/// \param outImage [out] image créée. \param outMemory [out] mémoire liée.
		/// \return false (et nettoie) si une étape Vulkan échoue.
		bool CreateImage(VkDevice device, VkPhysicalDevice phys,
			uint32_t width, uint32_t height, VkFormat format,
			VkImage& outImage, VkDeviceMemory& outMemory, std::string& err) const;

		/// Crée une vue 2D couleur, mip 0, layer 0 sur `image` au format `format`.
		VkImageView CreateView(VkDevice device, VkImage image, VkFormat format) const;

		DdgiGridConfig m_config{};

		VkImage m_irradianceImage = VK_NULL_HANDLE;
		VkDeviceMemory m_irradianceMemory = VK_NULL_HANDLE;
		VkImageView m_irradianceView = VK_NULL_HANDLE;

		VkImage m_visibilityImage = VK_NULL_HANDLE;
		VkDeviceMemory m_visibilityMemory = VK_NULL_HANDLE;
		VkImageView m_visibilityView = VK_NULL_HANDLE;

		bool m_allocated = false;
	};
} // namespace engine::render::gi
