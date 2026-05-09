#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/ProceduralStampGenerators.h"
#include "src/world_editor/terrain/TerrainBrush.h"
#include "src/world_editor/terrain/TerrainDocument.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	/// Mode d'application d'un stamp sur la heightmap (M100.7). Le poids de la
	/// grille de stamp `w[i,j]` (dans [-1, 1] côté procédural, [0, 1] côté
	/// PNG 16-bit) est multiplié par `params.strengthMeters` pour obtenir une
	/// "hauteur cible" `target = w * strength`. Le delta réellement appliqué
	/// dépend du mode :
	///   - Add     : delta = target               (ajoute la cible à l'existant)
	///   - Replace : delta = target - h_actuelle  (force la cellule à `target`)
	///   - Max     : delta = max(0, target - h)   (ne fait que monter)
	///   - Min     : delta = min(0, target - h)   (ne fait que descendre)
	enum class StampMode : uint8_t
	{
		Add     = 0,
		Replace = 1,
		Max     = 2,
		Min     = 3,
	};

	/// Paramètres d'un stamp prêt à appliquer (M100.7). L'UI Tool Properties
	/// expose chaque champ ; le tool consomme ces valeurs sans les normaliser
	/// (les bornes sont enforced par les sliders ImGui côté panel).
	struct StampParams
	{
		/// Source : true = générateur procédural, false = PNG sur disque.
		bool useProcedural = true;
		/// Archétype procédural utilisé si `useProcedural`.
		ProceduralStamp procedural = ProceduralStamp::Mountain;
		/// Chemin du PNG 16-bit grayscale si `!useProcedural`. Lu via
		/// `LoadStampPng16` à chaque rasterisation (pas de cache disque).
		std::string libraryPngPath;
		/// Diamètre (footprint) du stamp en mètres. Le rasterizer convertit en
		/// nombre de cellules via `cellSizeMeters` (1 m / cellule par défaut).
		float footprintMeters = 120.0f;
		/// Amplitude du stamp en mètres : multiplie le poids `[-1..1]` ou
		/// `[0..1]` issu de la grille source.
		float strengthMeters = 60.0f;
		/// Rotation Y (autour de l'axe vertical) en degrés. Appliquée au
		/// moment de la rasterisation par échantillonnage bilinéaire.
		float rotationYDeg = 0.0f;
		/// Mode d'application. Les modes Replace/Max/Min utilisent
		/// `chunk->heights` au moment du rasterize (donc dépendent de l'état
		/// courant — l'undo/redo reste bien défini car on stocke le delta final).
		StampMode mode = StampMode::Add;
	};

	/// Rasterise une grille `outResolution × outResolution` de poids selon
	/// `params` :
	///   - Si `useProcedural` : `GenerateProceduralStamp(params.procedural, outResolution)`.
	///   - Sinon : charge le PNG via `LoadStampPng16(params.libraryPngPath)`,
	///     puis ré-échantillonne bilinéairement à `outResolution × outResolution`.
	///
	/// Applique ensuite une rotation Y de `params.rotationYDeg` autour du
	/// centre de la grille via échantillonnage bilinéaire dans le repère tourné
	/// (les cellules hors disque/grille source reçoivent 0).
	///
	/// La grille retournée est en row-major (`grid[z*N + x]`). Si le PNG ne
	/// peut pas être chargé ou si `outResolution == 0`, retourne un vecteur
	/// vide (le caller doit no-op).
	///
	/// \param params         Paramètres source/rotation (et libraryPngPath).
	/// \param outResolution  Côté de la grille de sortie.
	/// \return Grille de poids row-major (vide en cas d'échec).
	std::vector<float> RasterizeStamp(const StampParams& params,
		uint32_t outResolution);

	/// Outil "stamp" terrain (M100.7). Vit dans le shell éditeur monde,
	/// branché sur le `CommandStack` partagé pour pousser un
	/// `TerrainStampCommand` à chaque application.
	///
	/// Cycle d'utilisation :
	///   - L'utilisateur clique sur le terrain → `OnClickAt(worldX, worldZ)`
	///     calcule la grille `RasterizeStamp` et stocke un delta sparse
	///     multi-chunk dans `m_previewDeltas`. Aucune modification de
	///     `TerrainChunk` à ce stade — c'est une preview pure.
	///   - L'utilisateur valide → `Apply` push une `TerrainStampCommand`
	///     (Execute applique les deltas, Undo les annule).
	///   - L'utilisateur annule → `Cancel` jette `m_previewDeltas` sans push.
	///
	/// Contraintes thread/timing : main thread (modifie le doc terrain et la
	/// pile undo).
	class TerrainStampTool
	{
	public:
		/// Initialise l'outil avec une référence au CommandStack et au
		/// TerrainDocument partagés. Doit être appelé une fois avant
		/// `OnClickAt`. Retourne false si l'un des pointeurs est null.
		bool Init(CommandStack& stack, TerrainDocument& doc);

		/// Met à jour les paramètres live du stamp (footprint, strength,
		/// rotation, mode, source). Toute preview existante reste intacte ; le
		/// caller peut appeler `OnClickAt` à nouveau pour la recalculer.
		void SetParams(const StampParams& p) { m_params = p; }

		/// Accès lecture seule aux paramètres (pour l'UI).
		const StampParams& GetParams() const { return m_params; }

		/// Clic sur le terrain à `(worldX, worldZ)` → calcule la preview en
		/// stockant les deltas dans `m_previewDeltas`. Aucune modification de
		/// `TerrainChunk` à ce stade : c'est purement une accumulation de
		/// `TerrainSculptDeltaCell` qui sera consommée par `Apply`.
		///
		/// Si la preview précédente n'a pas été appliquée ou annulée, elle est
		/// remplacée (un seul preview à la fois).
		///
		/// \param cfg    Source de `paths.content` pour `EnsureLoaded` des
		///               chunks impactés par le stamp.
		/// \param worldX Coordonnée monde X du clic en mètres.
		/// \param worldZ Coordonnée monde Z du clic en mètres.
		void OnClickAt(const engine::core::Config& cfg, float worldX, float worldZ);

		/// Crée un `TerrainStampCommand` à partir de `m_previewDeltas` et le
		/// push sur la pile undo. Le `Push` appelle `Execute` qui applique
		/// effectivement les deltas. No-op si `HasPreview()` est false.
		/// Effet de bord : reset `m_previewDeltas` et `m_hasPreview`.
		void Apply();

		/// Jette `m_previewDeltas` sans rien pousser sur la pile. Utilisé pour
		/// le bouton "Cancel preview" et le raccourci Esc côté shell. No-op
		/// si `HasPreview()` est false.
		void Cancel();

		/// True si une preview a été calculée par `OnClickAt` et pas encore
		/// consommée par `Apply` ou `Cancel`.
		bool HasPreview() const { return m_hasPreview; }

		/// Accès lecture seule aux deltas de preview (pour les tests / future
		/// visualisation overlay).
		const std::vector<TerrainSculptDeltaChunk>& PreviewDeltas() const
		{
			return m_previewDeltas;
		}

	private:
		CommandStack*                        m_stack = nullptr;
		TerrainDocument*                     m_doc = nullptr;
		StampParams                          m_params;
		std::vector<TerrainSculptDeltaChunk> m_previewDeltas;
		bool                                 m_hasPreview = false;
	};
}
