#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace engine::render { class AssetRegistry; class TextureHandle; }

namespace engine::client
{
	/// Cache d'icônes pour les panneaux ImGui in-game (arbre de compétences,
	/// Grimoire, barre d'action). Charge une PNG via AssetRegistry puis l'enregistre
	/// comme texture ImGui via ImGui_ImplVulkan_AddTexture, et mémorise l'ImTextureID
	/// (ImU64) par chemin. Modèle calqué sur RacePreviewViewport (layout
	/// SHADER_READ_ONLY_OPTIMAL — identique au cache de matériaux).
	///
	/// Politique TOLÉRANTE : chemin vide / fichier introuvable / échec Vulkan →
	/// retourne 0 (l'appelant retombe sur le libellé texte). Plafond strict
	/// (kMaxIcons) **sans éviction** : évite tout bug de durée de vie de descripteur ;
	/// au-delà, GetOrLoad renvoie 0 (repli texte). Dimensionné sous le pool de
	/// descripteurs ImGui (cf. WorldEditorImGui, 1024 sets).
	///
	/// Thread / timing : main thread uniquement. Init() doit être appelé APRÈS
	/// ImGui_ImplVulkan_Init (le backend Vulkan doit exister pour AddTexture).
	/// Shutdown() doit être appelé AVANT la destruction du backend ImGui
	/// (RemoveTexture libère les descripteurs alloués dans le pool ImGui).
	class SkillIconCache final
	{
	public:
		SkillIconCache() = default;
		SkillIconCache(const SkillIconCache&) = delete;
		SkillIconCache& operator=(const SkillIconCache&) = delete;

		/// Crée le sampler linéaire. \return false si device/registry nuls ou échec
		/// de création du sampler (les icônes sont alors simplement désactivées).
		bool Init(VkDevice device, engine::render::AssetRegistry* registry);

		/// Libère tous les descripteurs ImGui (RemoveTexture) puis le sampler.
		void Shutdown();

		/// Retourne l'ImTextureID (ImU64) de l'icône au chemin RELATIF \p relPath
		/// (depuis paths.content, ex. "icons/skills/<classId>/<fichier>.png"),
		/// ou 0 si vide / introuvable / plafond atteint / non initialisé.
		/// Chargement paresseux + mise en cache (succès comme échec mémorisés).
		/// \param outW,outH (optionnels) remplis avec la taille native en texels
		/// (utile pour préserver le ratio à l'affichage) ; 0 si non chargée.
		uint64_t GetOrLoad(const std::string& relPath, float* outW = nullptr, float* outH = nullptr);

		bool IsInitialized() const { return m_sampler != VK_NULL_HANDLE; }

	private:
		VkDevice m_device = VK_NULL_HANDLE;
		engine::render::AssetRegistry* m_registry = nullptr;
		VkSampler m_sampler = VK_NULL_HANDLE;
		/// Entrée de cache : ImTextureID (0 = échec mémorisé) + taille native en texels.
		struct IconEntry { uint64_t id = 0; float w = 0.0f; float h = 0.0f; };
		std::unordered_map<std::string, IconEntry> m_cache; ///< chemin -> entrée.
		std::vector<VkDescriptorSet> m_descriptors;        ///< descripteurs à libérer au Shutdown.
		static constexpr size_t kMaxIcons = 900;           ///< < pool ImGui (1024 sets).
	};
}
