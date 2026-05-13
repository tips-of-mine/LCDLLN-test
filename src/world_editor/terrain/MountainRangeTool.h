#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/PolylineMacroCore.h"

#include <cstddef>
#include <cstdint>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	class TerrainDocument;

	/// Outil "Mountain Range" (M100.35). L'utilisateur clique successivement
	/// sur le sol pour poser jusqu'à 32 vertices ; chaque vertex porte une
	/// largeur, une hauteur, une amplitude de bruit et une asymétrie locales.
	/// L'outil maintient en mémoire la polyline en cours (vertices + paramètres
	/// globaux) jusqu'à `Apply` (pousse une `MountainRangeCommand`) ou
	/// `Cancel` (abandon silencieux).
	///
	/// Pas de preview GPU dans cette première implémentation (overlay 2D
	/// futur) — la spec impose que la rasterisation finale ne touche au
	/// `TerrainChunk` qu'au moment du `Push`. La sélection de vertex et la
	/// modification de paramètres sont donc purement CPU side jusque-là.
	///
	/// Contraintes thread/timing : main thread (mute `WorldEditorShell` /
	/// `CommandStack`).
	class MountainRangeTool
	{
	public:
		/// Branche l'outil sur la pile undo et le document terrain partagés.
		/// La référence `Config` est mémorisée pour les appels `EnsureLoaded`
		/// des chunks impactés au moment de `Apply`. Retourne false si l'un
		/// des pointeurs est null.
		bool Init(CommandStack& stack, TerrainDocument& doc,
			const engine::core::Config& cfg);

		/// Réinitialise l'état (polyline vide). Idempotent.
		void Reset();

		/// Ajoute un vertex au bout de la polyline. No-op si la polyline est
		/// déjà à `kMacroPolylineMaxVertices` vertices.
		/// Effet de bord : modifie `m_params.vertices`.
		void AddVertex(float worldX, float worldZ);

		/// Supprime le vertex d'index `idx` si valide. No-op sinon.
		void RemoveVertex(size_t idx);

		/// Déplace le vertex d'index `idx` à la position monde fournie. No-op
		/// si idx invalide.
		void MoveVertex(size_t idx, float worldX, float worldZ);

		/// Bascule entre `Open` et `Loop` (fermeture de polyline). Effet de
		/// bord : modifie `m_params.mode`.
		void ToggleLoop();

		/// Mute les paramètres globaux (profile / seed / freq). Les autres
		/// (vertices) restent en place.
		void SetGlobalParams(FlankProfile profile, uint32_t seed, float freq);

		/// Pose le vertex actif (utile pour drag) sur la cible courante.
		void SetActiveVertex(size_t idx) { m_activeVertex = idx; }
		size_t GetActiveVertex() const { return m_activeVertex; }

		/// Rasterise la polyline courante (sans Apply) et retourne les deltas.
		/// Utile pour la preview overlay (lecture seule sur le terrain) et
		/// les tests d'invariant.
		SparseChunkDeltas BuildDeltas() const;

		/// Pousse une `MountainRangeCommand` sur la pile undo construite à
		/// partir de `BuildDeltas()`. No-op si moins de 2 vertices ou si les
		/// deltas sont vides. Effet de bord : reset la polyline courante,
		/// l'outil reste actif (prêt à dessiner une autre polyline).
		///
		/// Utilise la `Config` mémorisée à `Init` pour `EnsureLoaded` des
		/// chunks impactés. No-op silencieux si Init n'a pas été appelé.
		///
		/// \return true si la commande a effectivement été poussée.
		bool Apply();

		/// Abandonne la polyline en cours sans rien pousser. No-op si vide.
		void Cancel();

		/// Accès lecture seule à la polyline courante (pour la UI / les tests).
		const MacroPolylineParams& Params() const { return m_params; }
		MacroPolylineParams& MutableParams() { return m_params; }

		size_t VertexCount() const { return m_params.vertices.size(); }
		bool   HasPolyline() const { return !m_params.vertices.empty(); }

	private:
		CommandStack*               m_stack = nullptr;
		TerrainDocument*            m_doc   = nullptr;
		const engine::core::Config* m_cfg   = nullptr;
		MacroPolylineParams         m_params;
		/// Index du vertex actuellement édité (sélectionné dans le panneau).
		/// Égal à `m_params.vertices.size()` si aucun.
		size_t                      m_activeVertex = 0;
	};
}
