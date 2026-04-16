# 011 — Routes : spline + largeur (MVP) ou couche splat « route »

**Statut : livré (branche A — splat polyline ; branche B spline mesh = extension future)**

## Chaînement

| Précédent | Ce ticket | Suivant |
|-----------|-----------|---------|
| **010** (détail sol) | Grands linéaires sur le monde | **012** (intégration globale) |

Deux **options** mutuellement documentées ; le ticket doit en choisir **une** pour le MVP de la PR (l’autre reste extension).

---

## Objectif

Permettre de **tracer des routes** sur le terrain exportable : soit **dépression / strip mesh** suivant une spline avec largeur (plus proche d’un outil « vrai » route), soit **peinture splat** d’une couche « macadam » (plus rapide à livrer, moins géométrique).

## Livrables attendus (choisir branche A ou B en tête de PR)

### Branche A — Splat « route » (MVP rapide)

1. Couche ou masque splat réservé « route » + brosse ligne / polyline simplifiée (points cliqués).
2. Export identique à 008 pour ce canal.

### Branche B — Spline + largeur (MVP géométrique)

1. Structure données : liste de points 3D (XZ + snap Y terrain), largeur par segment ou globale.
2. Génération mesh bande (CPU) ou tessellation shader (si équipe prête) + rendu dans WE et jeu.
3. Sauvegarde dans JSON zone + export bundle.

## Critères d’acceptation (DoD)

- [x] Au moins **une route continue** sauvegardée, rechargée, exportée, visible après redémarrage (SLAP + JSON `routes`).
- [x] **Pas de crash** si la route sort du monde (clamp des clics et des segments côté `TerrainEditingTools::PaintSplatAlongPolyline` / picks Engine).
- [x] Doc : branche **A** livrée (`docs/terrain_et_world_editor.md`) ; branche **B** = mesh spline + export dédié (à planifier dans un ticket ultérieur si besoin).

## Fichiers concernés (indicatif)

- WE : UI outil route, I/O JSON
- Terrain : snap Y, intersection rayon / MNT
- Rendu : nouveau mesh pass ou extension splat

## Dépendances

- **008** minimum (terrain + splat cohérents) ; **009** utile pour partager conventions d’origine monde.
