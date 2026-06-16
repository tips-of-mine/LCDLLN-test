#pragma once
#include "src/world_editor/core/IPanel.h"
#include "src/world_editor/assets/AssetCatalog.h"

#include <functional>
#include <string>
#include <vector>

namespace engine::editor::world::panels
{
	/// Panneau Asset Browser : liste les meshes props scannés sur disque,
	/// groupés par catégorie, et notifie l'asset sélectionné (chemin relatif)
	/// via un callback (branché par le shell sur PlacementParams.assetPath).
	class AssetBrowserPanel final : public IPanel
	{
	public:
		/// Callback invoqué quand l'utilisateur clique un asset.
		/// \param relativePath chemin mesh relatif (ex. "meshes/props/Wall_Plaster_Straight.gltf").
		using OnAssetPicked = std::function<void(const std::string&)>;

		const char* GetName() const override { return "Asset Browser"; }

		/// Rend la liste des assets groupés par catégorie.
		/// Effet de bord : crée une window ImGui "Asset Browser" ; invoque
		/// OnAssetPicked au clic. Doit être appelée en main thread (phase ImGui).
		void Render() override;

		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

		/// Charge/rafraîchit la liste depuis le disque.
		/// \param absoluteDir    dossier props absolu à scanner.
		/// \param relativePrefix préfixe chemin relatif (ex. "meshes/props/").
		/// Effet de bord : lecture disque (à appeler hors frame critique, p. ex.
		/// à l'init du shell).
		void Refresh(const std::string& absoluteDir, const std::string& relativePrefix);

		/// Installe l'observateur de sélection (remplace le précédent).
		void SetOnAssetPicked(OnAssetPicked cb) { m_onPicked = std::move(cb); }

	private:
		bool m_visible = true;
		std::vector<assets::AssetEntry> m_entries;
		std::string m_selectedPath;
		OnAssetPicked m_onPicked;
	};
}
