# Issue: world-010

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/world_editor/ui/WorldMapEditDocument.h
- game/data/zones/demo_plains/terrain_grass.grms

## Note
Herbe detail surface

---

## Contenu du ticket (world-010)

# 010 — Herbe / détail surface (masque + rendu MVP)

**Statut : livré (MVP masque + rendu + WE)** — **révision périmètre** (tailles, plafond 1 m, densités / dissimulation) : voir § ci-dessous et DoD étendu.

## Chaînement

| Précédent | Ce ticket | Suivant |
|-----------|-----------|---------|
| **009** (instances ponctuelles) | Densité continue « tapis » | **011** (routes) |

Peut démarrer en **parallèle partiel** de 009 si le masque est uniquement splat-based ; la chaîne ci-dessus reflète l’usage auteur le plus naturel.

---

## Objectif

Introduire un **couche de détail** type herbe (billboards ou mesh instancié) **pilotée par un masque** (texture R8 ou canal splat dédié) : peinture ou procédural léger, **visible** dans le jeu et l’éditeur, avec budget perf (distance max, densité).

**Révision (à couvrir par évolution code ou ticket dérivé) — variété et gameplay :**

1. **Plusieurs tailles d’herbe** : au moins **plusieurs presets** ou un facteur d’échelle par **type** / **couche** d’herbe (billboard height, longueur des brins mesh, etc.), pour varier le relief visuel (prairie rasée, herbe moyenne, touffes plus hautes).
2. **Plafond hauteur** : l’herbe **ne doit pas dépasser 1 mètre** de hauteur au-dessus du sol (contrainte **design + technique** : clamp dans les paramètres rendu / données auteur, documenté ; unités cohérentes avec `terrain_world_size_m` / échelle monde).
3. **Niveaux de densité** : prévoir **plusieurs niveaux** (ex. faible / moyen / fort, ou valeur continue avec **seuils** documentés) pilotés par le masque et/ou par presets auteur, afin que certaines zones soient **assez denses** pour qu’un joueur **en position accroupie** puisse **s’y dissimuler** visuellement (couverture). La densité doit être **lisible côté gameplay** ou exportable (même approximation) pour aligner futur système de visibilité / line-of-sight si hors moteur rendu seul.

## Livrables attendus

1. **Format masque** : convention fichier (résolution, espace UV monde aligné terrain), emplacement dans `game/data` / export zone ; si besoin **canal additionnel** ou **seconde texture** pour niveau de densité / type (à documenter pour ne pas casser 008).
2. **Éditeur** : brosse « herbe » (**densité** multi-niveaux ou graduée, **rayon**, **effacer**) ; choix **type / preset** (taille relative dans la limite 1 m) ; réutilisation pipeline 008 si le masque est une couche splat réservée.
3. **Rendu** : passe GPU ou instancing depuis le masque (MVP : tuiles régulières + culling par chunk ou distance), paramètres dans `config.json` ou JSON zone : **hauteur max = 1 m** appliquée ; **mapping densité masque → nombre d’instances / opacité** documenté pour au moins **3 paliers** de densité perceptibles.
4. **Documentation** : limites (pas de vent physique MVP, LOD grossier acceptable) ; **tableau** : palier densité ↔ usage (ex. « dissimulation accroupi visée à partir du palier X ») ; règle **1 m** explicite.

## Critères d’acceptation (DoD)

### MVP (baseline)

- [x] Masque **sauvegardé** et **rechargé** : densité visuelle stable.
- [x] **FPS** : budget raisonnable documenté / validé en usage interne.
- [x] Désactivation possible (flag debug ou slider densité = 0) pour comparaison perf.

### Extension — tailles, plafond hauteur, densité « couverture »

- [ ] **Au moins deux tailles** d’herbe distinctes utilisables en auteur, **toutes** avec hauteur rendue **≤ 1 m** (vérifiable dans la scène ou via valeurs exportées).
- [ ] **Au moins trois niveaux de densité** distincts (faible / moyen / fort ou équivalent) pilotés par le masque ou les outils, avec comportement rendu clairement différent.
- [ ] **Zone de test** ou capture doc montrant une zone « haute densité » où la silhouette accroupie est **sensiblement masquée** par l’herbe (critère visuel ; liaison gameplay visibilité hors moteur = optionnelle mais **densité / paliers** exportés ou documentés pour ne pas dupliquer l’info).

## Fichiers concernés (indicatif)

- `engine/render/terrain/*`, frame graph / `Engine.cpp`
- Shaders `game/data/shaders/` (nouvelle passe ou extension terrain)
- WE : session + UI + chemins export

## Dépendances

- **008** fortement recommandé (alignement UV monde / splat) ; **007** ✅ pour le flux fichiers zone.
