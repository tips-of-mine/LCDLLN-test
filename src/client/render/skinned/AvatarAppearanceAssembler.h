#pragma once

/// @file src/client/render/skinned/AvatarAppearanceAssembler.h
/// @brief Assemble un avatar personnalisé : corps de race + pièces du catalogue
///        (visage, coiffure, barbe, moustache, oreilles, cornes, queue, ailes,
///        défenses) et matériaux teintés (peau, cheveux, yeux, corne, ivoire).
///
/// Utilisé par les trois rendus d'avatar : aperçu de création
/// (RacePreviewViewport), avatar local et avatars des autres joueurs
/// (Engine::RecordRemoteAvatars). Les pièces partagent le squelette du corps ;
/// SkinnedRenderer::RecordParts les dessine avec un seul jeu de matrices d'os.
///
/// Chargement paresseux : une pièce .glb est chargée au premier besoin puis
/// gardée en cache (un échec est mémorisé pour ne pas réessayer à chaque image).
/// Les matériaux (texture + teinte) sont créés une fois et réutilisés.
/// Main thread uniquement (pas de verrou) ; Shutdown() avant la destruction du device.

#include "src/client/character_creation/AppearanceCatalog.h"
#include "src/client/render/skinned/SkinnedRenderer.h"
#include "src/shared/network/CharacterAppearance.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::render
{
	class AssetRegistry;
	class MaterialDescriptorCache;
}

namespace engine::render::skinned
{
	/// Résultat d'un assemblage : pièces à dessiner + paramètres de rendu.
	struct AssembledAvatar
	{
		std::vector<SkinnedDrawPart> parts;  ///< corps en premier, puis les pièces
		uint32_t skinMaterialId = 0;         ///< matériau peau (biais de profondeur)
		float heightScale = 1.0f;            ///< facteur de taille (proportions)
		float widthScale = 1.0f;             ///< facteur de largeur (proportions)
	};

	/// Assembleur d'avatars personnalisés (cf. en-tête de fichier).
	class AvatarAppearanceAssembler
	{
	public:
		/// Prépare l'assembleur. \p catalog doit vivre plus longtemps que l'assembleur.
		/// \param contentRoot   racine des données (`paths.content`, ex. « game/data »).
		/// \param outfitMaterialId matériau de la tenue (sous-maillages hors peau du corps).
		/// \param skinNormalMale / skinNormalFemale normal maps de peau par genre
		///        (relatives à contentRoot ; vide = normale plate).
		/// \return false si un paramètre est invalide (assembleur inactif).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
		          engine::render::AssetRegistry* assets,
		          engine::render::MaterialDescriptorCache* materials,
		          const engine::client::AppearanceCatalog* catalog,
		          const std::string& contentRoot,
		          uint32_t outfitMaterialId,
		          const std::string& skinNormalMale,
		          const std::string& skinNormalFemale);

		/// Libère les maillages des pièces (les matériaux appartiennent au cache).
		void Shutdown(VkDevice device);

		/// Vrai si Init a réussi et que le catalogue est chargé.
		bool IsReady() const { return m_device != VK_NULL_HANDLE && m_catalog != nullptr && m_catalog->IsLoaded(); }

		/// Catalogue utilisé (nullptr si non initialisé).
		const engine::client::AppearanceCatalog* Catalog() const { return m_catalog; }

		/// Assemble \p body (corps déjà chargé de la race) avec l'apparence \p a.
		/// \param lod niveau de détail des pièces (0 = proche, 2 = lointain).
		/// \return false si l'assembleur n'est pas prêt ou si la race/le genre sont
		///         absents du catalogue : \p out reste vide et l'appelant garde son
		///         rendu historique (corps seul).
		/// Effet de bord : peut charger des pièces .glb et créer des matériaux.
		bool Assemble(const SkinnedMesh& body, const std::string& raceId, const std::string& gender,
		              const engine::network::CharacterAppearance& a, int lod, AssembledAvatar& out);

	private:
		/// Pièce chargée (cache) ou nullptr si absente / incompatible avec \p body.
		const SkinnedMesh* GetPart(const std::string& relPath, const SkinnedMesh& body);
		/// Matériau (texture + teinte + drapeaux). Créé seulement pendant WarmUp
		/// (hors enregistrement de commandes : CreateMaterial met à jour le
		/// descriptor set bindless, interdit pendant qu'une image l'utilise) ; au
		/// rendu, une clé inconnue retombe sur le matériau de la tenue.
		uint32_t GetMaterial(const std::string& baseColor, const std::string& normal, uint32_t tint, uint32_t flags);
		/// Crée d'avance tous les matériaux du catalogue (peaux, cheveux, yeux,
		/// corne, ivoire). Appelée par Init, au boot.
		void WarmUpMaterials();
		/// Teinte de peau effective (blanc -> aucun drapeau de teinte).
		static uint32_t SkinFlags(uint32_t tint);
		/// Drapeaux des matériaux de pilosité : teinte + découpe alpha.
		static uint32_t HairFlags();
		/// Matériaux résolus pour un avatar, par famille de matériau glTF.
		struct RoutedMaterials
		{
			uint32_t skin = 0;     ///< peau du corps (et anciens visages procéduraux)
			uint32_t hair = 0;     ///< cheveux, pilosité
			uint32_t eye = 0;      ///< anciens globes oculaires
			uint32_t horn = 0;     ///< corne
			uint32_t tusk = 0;     ///< ivoire
			uint32_t face = 0;     ///< peau du visage (atlas du visage, teinte de peau)
			uint32_t faceEye = 0;  ///< yeux peints dans l'atlas du visage (teinte des yeux)
		};
		/// Index matériau d'un sous-maillage de pièce selon son nom de matériau glTF
		/// (MI_Face / MI_FaceEye : atlas du visage ; noms de peau ; MI_Hair ; MI_Eye ;
		/// MI_Horn ; MI_Tusk ; sinon la tenue).
		uint32_t RouteMaterial(const std::string& materialName, const RoutedMaterials& m) const;

		VkDevice m_device = VK_NULL_HANDLE;
		VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
		engine::render::AssetRegistry* m_assets = nullptr;
		engine::render::MaterialDescriptorCache* m_materials = nullptr;
		const engine::client::AppearanceCatalog* m_catalog = nullptr;
		std::string m_contentRoot;
		uint32_t m_outfitMaterialId = 0;
		std::string m_skinNormal[2];
		std::unordered_map<std::string, std::unique_ptr<SkinnedMesh>> m_parts;   ///< nullptr = échec mémorisé
		std::unordered_map<std::string, uint32_t> m_materialIds;
		std::unordered_set<std::string> m_loggedIncompatible;
		bool m_allowMaterialCreation = false;  ///< vrai seulement pendant WarmUpMaterials
	};
}
