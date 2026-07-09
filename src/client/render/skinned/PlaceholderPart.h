#pragma once
// Chantier 2 SP1 — génération d'une PARTIE placeholder (aucun asset) : une boîte
// skinnée à 100% sur un os, centrée sur la position bind-globale de cet os. Sert
// à valider le pipeline modulaire (composition/swap) en attendant les vrais
// assets. Données CPU uniquement (pas de Vulkan) — testable en ctest.

#include "src/client/render/skinned/SkinnedMeshLoader.h" // SkinnedMeshCpuData

namespace engine::render::skinned
{
	struct Skeleton;

	/// Construit une boîte (24 sommets, 36 indices, 1 sous-maillage) de demi-côté
	/// `halfExtentM`, centrée sur la position bind-globale de l'os `boneIndex`,
	/// entièrement pondérée (poids 1) sur cet os. Le squelette `skel` est COPIÉ
	/// dans les données CPU pour partager la pose de l'avatar au rendu.
	/// \param boneIndex hors borne ou < 0 → boîte à l'origine, pondérée sur l'os 0.
	SkinnedMeshCpuData MakePlaceholderBoxPart(const Skeleton& skel, int boneIndex, float halfExtentM);
}
