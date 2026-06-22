#pragma once

#include "src/shared/core/Config.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::world
{
	/// Catalogue de collision des pièces de bâtiment, indexé par BASENAME de mesh
	/// (sans dossier ni extension), insensible à la casse. Chargé depuis un JSON
	/// (game/data/collision/building_pieces.json) via engine::core::Config.
	/// Une pièce est soit « passable » (aucune collision : battant de porte), soit
	/// décrite par une liste de boîtes en ESPACE LOCAL du mesh.
	class BuildingCollisionCatalog
	{
	public:
		/// Boîte de collision en espace local du mesh (centre + demi-dimensions, m).
		struct LocalBox { float cx, cy, cz, hx, hy, hz; };

		/// Résultat de Lookup pour une pièce présente au catalogue.
		struct Piece { bool passable = false; std::vector<LocalBox> boxes; };

		/// Charge le catalogue depuis le texte JSON. \return false si parse invalide.
		bool LoadFromJson(const std::string& jsonText);

		/// Renvoie la pièce si \p meshBaseName est au catalogue, sinon nullptr
		/// (l'appelant retombe alors sur la collision cylindre par défaut).
		/// \p meshBaseName : basename sans extension ; la casse est ignorée.
		const Piece* Lookup(std::string_view meshBaseName) const;

	private:
		engine::core::Config m_cfg;
		bool m_loaded = false;
		// Cache des pièces déjà résolues (clé = basename minuscule).
		mutable std::unordered_map<std::string, Piece> m_cache;
	};
}
