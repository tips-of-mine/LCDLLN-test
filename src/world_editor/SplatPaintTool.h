#pragma once

#include "src/world_editor/CommandStack.h"
#include "src/world_editor/SplatPaintCommand.h"
#include "src/world_editor/TerrainDocument.h"
#include "src/client/render/Camera.h"

#include <cstdint>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	/// Paramètres "live" de la brosse splat (M100.10), modifiables depuis le
	/// panneau Tool Properties et lus à chaque tick. Les unités sont en mètres
	/// (rayon, altitudes), degrés (pentes), et `[0..1]` (force, falloff).
	struct SplatPaintParams
	{
		/// Mode auto-rules (true) vs brosse manuelle (false).
		bool autoRules = false;
		/// Index de la layer cible (`[0..7]`). Hors range → clamp à 0 dans le tool.
		uint8_t activeLayer = 0;
		/// Rayon en mètres de la brosse (footprint disque).
		float radiusMeters = 6.0f;
		/// Force d'application par tick `[0..1]`.
		float strength = 0.5f;
		/// Falloff smoothstep `[0..1]` : 0 = bord dur, 1 = transition pleine.
		float falloff = 0.7f;
		/// Pente minimale (degrés) pour le filtre auto-rules.
		float slopeMinDeg = 0.0f;
		/// Pente maximale (degrés) pour le filtre auto-rules.
		float slopeMaxDeg = 90.0f;
		/// Altitude minimale (mètres) pour le filtre auto-rules.
		float altMin = -1024.0f;
		/// Altitude maximale (mètres) pour le filtre auto-rules.
		float altMax =  8192.0f;
	};

	/// Outil de peinture splat (M100.10). Vit dans le shell éditeur monde,
	/// branché sur le `CommandStack` partagé pour pousser une `SplatPaintCommand`
	/// par brushstroke (press → release).
	///
	/// Cycle de vie d'un stroke (mode manual) :
	///   - `OnMouseDown` : raycast, démarre `m_inFlight`, applique 1er tick.
	///   - `OnMouseMove` (répétés tant que pressé) : raycast + tick supplémentaire.
	///   - `OnMouseUp` : pousse `SplatPaintCommand(m_inFlight, m_strokeId)`
	///     sur le `CommandStack`.
	///
	/// Mode auto-rules : déclenchable par `ApplyAutoRulesToChunk` (UI bouton
	/// "Apply to chunk"), pousse une commande non-fusionnable.
	///
	/// Contraintes thread/timing : main thread (modifie le document terrain
	/// et la pile undo).
	class SplatPaintTool
	{
	public:
		/// Initialise l'outil avec une référence au CommandStack et au
		/// TerrainDocument partagés. Doit être appelé une fois avant
		/// `OnMouseDown`. Retourne false si l'un des pointeurs est null.
		bool Init(CommandStack& stack, TerrainDocument& doc);

		/// Met à jour les paramètres live. Lecture immédiate au prochain tick.
		void SetParams(const SplatPaintParams& p) { m_params = p; }

		/// Accès lecture seule aux paramètres (pour l'UI).
		const SplatPaintParams& GetParams() const { return m_params; }

		/// Démarre un brushstroke. Raycast → 1er tick. Génère un nouveau
		/// `m_strokeId` qui servira de `mergeKey` à la commande poussée à
		/// `OnMouseUp`.
		/// \param dtSeconds Delta temps depuis le tick précédent (sec).
		void OnMouseDown(const engine::render::Camera& cam,
			int sx, int sy, int vw, int vh,
			const engine::core::Config& cfg,
			float dtSeconds = 1.0f / 60.0f);

		/// Tick d'un brushstroke en cours. No-op si aucun stroke actif.
		void OnMouseMove(const engine::render::Camera& cam,
			int sx, int sy, int vw, int vh,
			const engine::core::Config& cfg,
			float dtSeconds = 1.0f / 60.0f);

		/// Termine le brushstroke : si `m_inFlight` non vide, pousse une
		/// `SplatPaintCommand` sur le CommandStack avec `m_strokeId` comme
		/// mergeKey, puis vide `m_inFlight`. No-op si rien n'a été touché.
		void OnMouseUp();

		/// True si un stroke est en cours (entre OnMouseDown et OnMouseUp).
		bool IsStroking() const { return m_pressing; }

		/// Nombre de chunks touchés par le stroke en cours.
		size_t InFlightChunkCount() const { return m_inFlight.size(); }

		/// Applique les auto-rules à un chunk entier (mode auto, M100.10).
		/// Pour chaque cellule du chunk satisfaisant `MatchesRules`, ajoute
		/// `delta = uint8(strength * 255)` à la layer active puis renormalise
		/// la cellule pour somme=255. Pousse une `SplatPaintCommand` avec un
		/// `mergeKey` unique (distinct des strokeId, donc non-fusionnable).
		///
		/// Effet de bord : `EnsureLoaded` + `EnsureSplatLoaded` sur le chunk,
		/// `MarkSplatDirty` au commit.
		void ApplyAutoRulesToChunk(const engine::core::Config& cfg, int chunkX, int chunkZ);

	private:
		/// Applique un tick au point monde donné. Multi-chunk + couture de
		/// bord (pour chaque cellule sur le bord d'un chunk, écrit aussi la
		/// cellule miroir du chunk voisin).
		void ApplyTickAtWorldPoint(float worldX, float worldZ,
			const engine::core::Config& cfg, float dtSeconds);

		/// Récupère ou crée la liste de cellules en vol pour un chunk donné
		/// dans `m_inFlight`. Référence stable jusqu'à la prochaine mutation
		/// de `m_inFlight`.
		std::vector<SplatDeltaCell>& EnsureInFlightCells(
			engine::world::GlobalChunkCoord coord);

		/// Cherche une cellule existante dans `m_inFlight` pour le chunk
		/// `coord` aux coordonnées `(x, z)`. Retourne nullptr si absente.
		/// Utilisée pour préserver `prev` au tout premier tick du stroke
		/// (les ticks suivants ne mettent à jour que `next`).
		SplatDeltaCell* FindInFlightCell(
			engine::world::GlobalChunkCoord coord, uint16_t x, uint16_t z);

		CommandStack*               m_stack = nullptr;
		TerrainDocument*            m_doc = nullptr;
		SplatPaintParams            m_params;
		std::vector<SplatDeltaChunk> m_inFlight;
		bool                        m_pressing = false;
		uint64_t                    m_strokeId = 0;
	};
}
