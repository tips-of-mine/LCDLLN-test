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
		uint64_t GetOrLoad(const std::string& relPath);

		bool IsInitialized() const { return m_sampler != VK_NULL_HANDLE; }

	private:
		VkDevice m_device = VK_NULL_HANDLE;
		engine::render::AssetRegistry* m_registry = nullptr;
		VkSampler m_sampler = VK_NULL_HANDLE;
		std::unordered_map<std::string, uint64_t> m_cache; ///< chemin -> ImTextureID (0 = échec mémorisé).
		std::vector<VkDescriptorSet> m_descriptors;        ///< descripteurs à libérer au Shutdown.
		static constexpr size_t kMaxIcons = 900;           ///< < pool ImGui (1024 sets).
	};
}
