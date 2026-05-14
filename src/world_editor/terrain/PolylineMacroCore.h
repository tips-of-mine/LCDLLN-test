#pragma once

#include "src/client/world/WorldModel.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace std
{
	/// Spécialisation de hash pour `engine::world::GlobalChunkCoord`.
	/// Combine `x` et `z` (int32) en un uint64 sans collision (x dans
	/// la moitié haute, z dans la moitié basse) puis applique le hash
	/// std::hash<uint64_t> standard. Stable d'une exécution à l'autre,
	/// utilisable comme clé d'`unordered_map`.
	template <>
	struct hash<engine::world::GlobalChunkCoord>
	{
		size_t operator()(const engine::world::GlobalChunkCoord& c) const noexcept
		{
			const uint64_t packed =
				(static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32) |
				 static_cast<uint64_t>(static_cast<uint32_t>(c.z));
			return std::hash<uint64_t>{}(packed);
		}
	};
}

namespace engine::editor::world
{
	/// Mode de fermeture d'une polyline macro (M100.35). `Open` laisse les deux
	/// extrémités libres ; `Loop` connecte le dernier vertex au premier en
	/// rasterisant aussi le segment de fermeture.
	enum class PolylineMode : uint8_t
	{
		Open = 0,
		Loop = 1,
	};

	/// Profil radial du flanc (M100.35). Détermine comment la hauteur décroît
	/// à mesure qu'on s'éloigne de l'axe de la polyline (à `distLat = 0`,
	/// `weight = 1` ; à `distLat = widthLocal`, `weight = 0`).
	///   - `Smoothstep` : 1 - smoothstep(0, 1, u) — transition douce S-shape.
	///   - `Linear`     : 1 - u — décroissance linéaire.
	///   - `Exp`        : exp(-3 * u * u) — gaussienne (flancs longs).
	enum class FlankProfile : uint8_t
	{
		Smoothstep = 0,
		Linear     = 1,
		Exp        = 2,
	};

	/// Un vertex de polyline avec paramètres locaux (M100.35). Les paramètres
	/// `widthMeters / heightMeters / noiseAmplitude / asymmetry` sont interpolés
	/// linéairement le long du segment vers le vertex suivant.
	/// `worldX` / `worldZ` sont en mètres absolus monde (axe XZ, plan horizontal).
	struct PolylineVertex
	{
		float worldX         = 0.0f;
		float worldZ         = 0.0f;
		/// Largeur perpendiculaire à l'axe en mètres (base totale, pas la moitié).
		/// Le profil radial s'étale donc sur [-width/2, +width/2] de chaque côté.
		float widthMeters    = 250.0f;
		/// Amplitude verticale du delta en mètres (positif = monte, négatif
		/// inversé par le flag `invert` au moment de la rasterisation).
		float heightMeters   = 400.0f;
		/// Amplitude du bruit Simplex 2D ajouté à la crête (mètres, ≥ 0).
		float noiseAmplitude = 30.0f;
		/// Coefficient d'asymétrie -1..+1. À 0, flancs symétriques. À +1, flanc
		/// gauche court / flanc droit long (le delta est multiplié par
		/// `1 + asymmetry` côté positif du cross-product, et `1 - asymmetry`
		/// côté négatif).
		float asymmetry      = 0.0f;
	};

	/// Paramètres globaux d'une polyline macro (M100.35). Combinés avec les
	/// vertices, ils décrivent intégralement la rasterisation déterministe.
	struct MacroPolylineParams
	{
		std::vector<PolylineVertex> vertices;
		PolylineMode mode           = PolylineMode::Open;
		FlankProfile profile        = FlankProfile::Smoothstep;
		uint32_t     noiseSeed      = 0;
		/// Fréquence du bruit Simplex (1/mètre). Plus haut = plus de détail.
		float        noiseFrequency = 0.005f;
	};

	/// Deltas terrain par chunk, organisés sparse pour des rasterisations
	/// macro multi-chunks (M100.35). Layout :
	///   `outer[chunkCoord][cellIndex] = deltaMeters`
	/// avec `cellIndex = z * kTerrainResolution + x` (M100.5).
	///
	/// Contrat partagé : M100.36 (River Network), M100.37 (Coastline),
	/// M100.38 / M100.39 (Erosions) consomment ce même typedef pour la
	/// cohérence du pattern d'écriture multi-chunks.
	///
	/// Effet de bord : aucun. Pure data.
	using SparseChunkDeltas =
		std::unordered_map<engine::world::GlobalChunkCoord,
			std::unordered_map<uint32_t, float>>;

	/// Limite haute du nombre de vertices d'une polyline (M100.35). Au-delà,
	/// l'outil refuse d'ajouter un vertex (l'UI affiche "Vertices posés : N / max").
	constexpr size_t kMacroPolylineMaxVertices = 32;

	/// Rasterise les deltas pour la polyline donnée (M100.35).
	///
	/// Algorithme (rappel spec) :
	///   1. Pour chaque cellule monde dans le bounding box élargi (`widthMax *
	///      1.5`), trouver le segment dont la distance latérale est minimale.
	///   2. Interpoler les paramètres `(width, height, noise, asym)` à `t`
	///      (paramètre normalisé sur le segment retenu).
	///   3. Skip si `distLat > widthLocal / 2`.
	///   4. Calculer `u = 2 * distLat / widthLocal ∈ [0, 1]` puis le poids
	///      radial `w = profile(u) * heightLocal`.
	///   5. Appliquer l'asymétrie par signe du cross-product (tangent vs
	///      perpendiculaire).
	///   6. Ajouter le bruit Simplex 2D évalué en coords monde.
	///   7. Empiler `(chunkCoord, cellIndex, delta)` dans la map sparse.
	///
	/// Le delta est positif pour `invert=false` (Mountain) et négatif pour
	/// `invert=true` (Valley) — la formule pure est `delta = w + noise`
	/// puis on multiplie par `invert ? -1 : +1`.
	///
	/// \param params Paramètres complets (vertices + globaux).
	/// \param invert false = Mountain (delta positif), true = Valley (delta négatif).
	/// \return Deltas sparse. Vide si `params.vertices.size() < 2`.
	///
	/// Effet de bord : aucun. Pure function, thread-safe.
	/// Contraintes thread/timing : aucune (pure CPU, pas d'ImGui ni de GPU).
	SparseChunkDeltas RasterizeMacroPolyline(const MacroPolylineParams& params,
		bool invert);
}
