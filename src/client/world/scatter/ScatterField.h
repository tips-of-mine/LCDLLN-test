#pragma once

// Monde jouable (chantier « Royaumes ») — champ de décor instancié, côté CPU.
//
// Contient TOUTES les instances statiques du monde rendues par instanciation
// GPU (scatter.bin : arbres, rochers, mobilier, remparts ; buildings.bin
// développé en pièces) : matrice monde finale (posée sur le terrain), sphère
// englobante, mesh, classe de culling. Les instances sont rangées dans une
// grille XZ uniforme pour ne visiter, chaque frame, que les cases proches de la
// caméra, puis filtrées par distance (par classe) et par frustum.
//
// Module PUR (aucune dépendance Vulkan) → testé en CI (scatter_field_tests).
// Thread : main thread uniquement (pas de verrou).

#include "src/shared/math/Math.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::world::scatter
{
	/// Une instance prête à dessiner.
	struct ScatterItem
	{
		engine::math::Mat4 model = engine::math::Mat4::Identity(); ///< local → monde
		engine::math::Vec3 center{ 0.0f, 0.0f, 0.0f };             ///< centre de la sphère englobante (monde)
		float radius = 1.0f;                                       ///< rayon englobant (m)
		uint32_t meshId = 0;                                       ///< index dans la table de meshes du champ
		uint8_t cullClass = 0;                                     ///< 0 grand, 1 moyen, 2 petit
		uint8_t flags = 0;                                         ///< bits ScatterFlags (scatter.bin)
	};

	/// Distances de culling (m) par classe.
	struct CullDistances
	{
		// Suffixe M (mètres) : `small` est une macro de rpcndr.h sous Windows.
		float largeM = 150.0f;
		float mediumM = 80.0f;
		float smallM = 42.0f;
		float For(uint8_t cls) const { return cls == 0 ? largeM : (cls == 1 ? mediumM : smallM); }
	};

	/// Six plans de frustum (ax + by + cz + d ≥ 0 à l'intérieur), extraits
	/// d'une matrice viewProj column-major (convention du moteur, Vulkan Z [0,1]).
	struct Frustum
	{
		std::array<std::array<float, 4>, 6> planes{};
		/// Construit les plans normalisés depuis `viewProj.m` (column-major).
		static Frustum FromViewProj(const float* m);
		/// Vrai si la sphère (c, r) recoupe le frustum (test conservateur).
		bool SphereVisible(const engine::math::Vec3& c, float r) const;
	};

	class ScatterField final
	{
	public:
		/// Taille des cases de la grille de visibilité (m).
		static constexpr float kCellM = 32.0f;

		/// Vide le champ (instances + table de meshes).
		void Clear();

		/// Enregistre (ou retrouve) un chemin de mesh ; renvoie son meshId.
		uint32_t InternMesh(const std::string& meshPath);
		const std::vector<std::string>& MeshPaths() const { return m_meshPaths; }

		/// Ajoute une instance. Effet de bord : invalide la grille (reconstruite
		/// paresseusement au prochain Gather).
		uint32_t Add(const ScatterItem& item);

		std::size_t Count() const { return m_items.size(); }
		const ScatterItem& Item(uint32_t index) const { return m_items[index]; }

		/// Collecte les indices des instances visibles : dans la distance de leur
		/// classe autour de `eye` (plan XZ) et dans le frustum. Ordre : cases de
		/// la grille puis ordre d'insertion (stable d'une frame à l'autre).
		void Gather(const engine::math::Vec3& eye, const Frustum& frustum,
			const CullDistances& dist, std::vector<uint32_t>& out) const;

		/// Collecte les instances « occulteurs » (flag bit2) dont la sphère est à
		/// moins de `margin` m du segment [a, b] (fondu d'occlusion caméra).
		void GatherNearSegment(const engine::math::Vec3& a, const engine::math::Vec3& b,
			float margin, std::vector<uint32_t>& out) const;

	private:
		void RebuildGrid() const;
		static int64_t Key(int32_t cx, int32_t cz)
		{
			return (static_cast<int64_t>(cx) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(cz));
		}

		std::vector<ScatterItem> m_items;
		std::vector<std::string> m_meshPaths;
		std::unordered_map<std::string, uint32_t> m_meshIndex;
		mutable std::unordered_map<int64_t, std::vector<uint32_t>> m_grid;
		mutable bool m_gridDirty = true;
		mutable float m_maxRadius = 0.0f;
	};

	/// Matrice d'instance standard : T(pos) * RotY(yaw) * RotX(tilt) * S(scale).
	/// Convention identique à BuildPropFromMeshMatrix / LoadBuildings (Engine).
	engine::math::Mat4 ComposeInstanceMatrix(const engine::math::Vec3& position,
		float yawDeg, float tiltDeg, float scale);

	/// Transforme une AABB locale par une matrice et renvoie la sphère englobante
	/// monde (centre de l'AABB transformée, demi-diagonale).
	void TransformBounds(const engine::math::Mat4& m, const engine::math::Vec3& localMin,
		const engine::math::Vec3& localMax, engine::math::Vec3& outCenter, float& outRadius,
		engine::math::Vec3& outWorldMin, engine::math::Vec3& outWorldMax);
}
