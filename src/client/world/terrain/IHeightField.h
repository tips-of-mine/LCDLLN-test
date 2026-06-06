#pragma once

namespace engine::world::terrain
{
    /// Phase 2 (chantier C) — abstraction d'une source de hauteur de sol
    /// échantillonnable en coordonnées monde (mètres). Découple la collision
    /// (`TerrainCollider`) de la source concrète (heightmap legacy ou chunks
    /// streamés). Implémentations NON-BLOQUANTES, appelables en main thread
    /// (aucune lecture disque synchrone dans le chemin collision).
    class IHeightField
    {
    public:
        virtual ~IHeightField() = default;

        /// Altitude du sol (m monde) en (worldX, worldZ), filtrage bilinéaire.
        /// Toujours fini (jamais NaN) : repli sûr 0.0 si indisponible.
        virtual float HeightAt(float worldX, float worldZ) const = 0;

        /// Vrai si cette source fournit une hauteur fiable en (worldX, worldZ)
        /// SANS déclencher de chargement (aucune I/O). Sert d'aiguillage.
        virtual bool IsLoadedAt(float worldX, float worldZ) const = 0;
    };
}
