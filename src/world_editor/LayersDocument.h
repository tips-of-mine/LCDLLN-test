#pragma once

// M100.34 — LayersDocument : 16 calques nommés (visibilité + verrou + couleur
// d'overlay) + assignment des entités à un calque. Logique PURE (testable
// headless) ; l'Outliner consomme ce document pour afficher/grouper.
//
// L'assignment est conservé EN MÉMOIRE (clé = identifiant d'entité). La
// persistance d'un `layerIndex` dans les formats binaires (props/hazards/zones)
// est différée en 2e passe (bump de version dédié) afin de ne pas casser les
// formats v1 existants ; ce document reste la source d'autorité runtime.

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::editor::world
{
	/// Nombre fixe de calques (cf. ticket : 16 max).
	constexpr uint8_t kLayerCount = 16u;

	/// Un calque de l'éditeur.
	struct Layer
	{
		std::string name;                 ///< Nom affiché dans l'Outliner.
		bool        visible = true;       ///< Masqué → entités non rendues.
		bool        locked  = false;      ///< Verrouillé → édition bloquée.
		uint32_t    overlayColorRgba = 0xFFFFFFFFu; ///< Couleur d'overlay (RGBA8).
	};

	/// Document des 16 calques + table d'assignment entité → calque.
	class LayersDocument
	{
	public:
		LayersDocument();

		/// Accès à un calque par index (0..15). Indices hors borne renvoient le
		/// calque 0 (Default) pour rester sûr.
		Layer&       LayerAt(uint8_t index);
		const Layer& LayerAt(uint8_t index) const;

		void SetLayerName(uint8_t index, const std::string& name);
		void SetVisible(uint8_t index, bool visible);
		void SetLocked(uint8_t index, bool locked);

		/// Assigne l'entité `entityKey` au calque `layerIndex` (clampé 0..15).
		void AssignEntity(uint64_t entityKey, uint8_t layerIndex);

		/// Calque de l'entité (Default=0 si non assignée).
		uint8_t GetEntityLayer(uint64_t entityKey) const;

		/// True si l'entité est visible (= son calque est visible). Une entité
		/// non assignée suit le calque Default.
		bool IsEntityVisible(uint64_t entityKey) const;

		/// True si l'entité est verrouillée (= son calque est verrouillé).
		bool IsEntityLocked(uint64_t entityKey) const;

	private:
		std::array<Layer, kLayerCount>          m_layers;
		std::unordered_map<uint64_t, uint8_t>   m_assignment;
	};
}
