# Spec — Cimetière par défaut par zone (respawn déterministe, anti-triche)

**Date :** 2026-06-19
**Sous-système :** #3 du lot 2026-06-18 (cimetière par-zone)
**Statut :** Design validé (approche A), en attente de plan d'implémentation
**Déploiement :** ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS** (master/shard) — change la sélection de respawn

---

## 1. Contexte & problème

À la mort, le client envoie « respawn cimetière » → le serveur (`ServerApp::HandleRespawnRequest`,
[ServerApp.cpp:3133](../../../src/shared/server_bootstrap/ServerApp.cpp)) choisit, parmi les points de
respawn de la zone du joueur, **le cimetière le PLUS PROCHE du lieu de mort** :

```cpp
// boucle 3162-3196 (simplifiée)
for (const RespawnPointDefinition& point : m_respawnPoints) {
    if (point.zoneId != client->zoneId || point.destinationType != destination) continue;
    const float distSq = sq(point.pos - client->positionMeters);   // <-- position de mort
    if (destination == graveyard && !IsGraveyardEligibleForRespawn(sqrt(distSq), ...)) continue;
    if (distSq < bestDistSq) { best = point; }                     // <-- LE PLUS PROCHE
}
```

**Faille (triche)** : `client->positionMeters` est la position de mort, et le **mouvement est
client-autoritaire** (cf. commentaire ServerApp:3213). Un client triché peut donc **choisir où
mourir / fausser sa position** pour contrôler le cimetière où il réapparaît (ex. revenir au plus
près d'un objectif). La sélection « plus proche » dépend d'une donnée **non fiable**.

Par ailleurs la règle d'éligibilité actuelle `IsGraveyardEligibleForRespawn` (rayon neutre de
faction) est elle-même **dépendante de la distance** (donc de la position).

## 2. Décision (approche A — cimetière par défaut par zone, serveur-autoritaire)

Choisir, pour la zone du joueur (`client->zoneId`, **autoritaire serveur**), **UN cimetière par
défaut désigné**, de façon **déterministe et position-indépendante**. La position de mort
n'entre plus dans le choix → la triche par position est neutralisée.

Approches écartées :
- **B** (garder « plus proche » mais position serveur) : la position reste client-autoritaire → faille non corrigée.
- **C** (défaut côté client) : le respawn est serveur-autoritaire ; le client n'est pas fiable pour de l'anti-triche.

## 3. Architecture & composants

### 3.1 Éligibilité position-indépendante (shared `RespawnRules`)
`IsGraveyardEligibleForRespawn` (distance/rayon neutre) reste pour l'ancien modèle. On AJOUTE une
règle d'éligibilité **par propriété seule** (sans distance), pour le défaut de zone :
```cpp
/// Éligibilité d'un cimetière comme DÉFAUT de zone, indépendante de la position
/// (anti-triche) : un cimetière neutre est éligible pour tous ; sinon seule sa
/// faction propriétaire. Pas de notion de rayon/distance.
inline bool IsGraveyardEligibleAsZoneDefault(std::string_view graveyardFaction,
                                             std::string_view playerFaction)
{
    if (graveyardFaction.empty() || graveyardFaction == "-") return true; // neutre
    return graveyardFaction == playerFaction;
}
```
Header-only, pur, testable (comme l'existant).

### 3.2 Sélection du défaut (serveur `ServerApp::HandleRespawnRequest`)
Pour `destination == kRespawnDestinationGraveyard`, **remplacer la boucle « plus proche »** par :
le **PREMIER** `m_respawnPoints` (ordre de fichier = déterministe) qui satisfait
`zoneId == client->zoneId`, `destinationType == graveyard`, et
`IsGraveyardEligibleAsZoneDefault(point.ownerFactionId, client->factionId)`. **Aucune distance,
aucune dépendance à `client->positionMeters`.** Repli inchangé : si aucun cimetière par défaut
éligible, repli sur le point d'entrée en monde (`client->spawnPositionMeters`).

> L'**auberge** (`kRespawnDestinationInn`) reste **inchangée** (hors périmètre — la demande vise
> le cimetière). Possibilité future de lui appliquer le même traitement.

### 3.3 Données — enrichir chaque zone
Chaque zone doit avoir **au moins un cimetière** dans son `respawn_points.txt` (le défaut).
Convention : le **premier** cimetière éligible (ordre de fichier) de la zone est le défaut — pas
de nouvelle syntaxe requise pour les zones à 1 cimetière (cas actuel : `feyhin` = 1 cimetière
neutre). Enrichir les zones qui en manquent (ex. `demo_plains`). Pour une future zone à plusieurs
cimetières par faction, l'ordre de déclaration fixe la priorité du défaut.

### 3.4 Client
**Inchangé** : il envoie « respawn cimetière », reçoit la position imposée (`SendForcePosition`).
L'affichage d'un marqueur de cimetière par zone est **hors périmètre** (le rendu des marqueurs a
été retiré au profit du décor physique ; cf. Engine.cpp:11759).

## 4. Flux & anti-triche

```
Mort → client: « respawn cimetière » → serveur:
  zone = client->zoneId            (AUTORITAIRE, non spoofable)
  cimetière = 1er point {zone, graveyard, éligible-par-propriété(faction)}
  → position imposée                (SendForcePosition)
La position de mort N'INTERVIENT PLUS.
```

## 5. Tests
- **CI** (`RespawnRulesTests`) : `IsGraveyardEligibleAsZoneDefault` — neutre → tous ; faction
  propriétaire → seulement sa faction ; faction joueur vide vs cimetière neutre/faction.
- Non-régression : `IsGraveyardEligibleForRespawn` (inchangée) reste verte.
- La sélection dans `ServerApp` (UDP, état serveur) n'est pas unit-testable simplement →
  **validation en jeu** : mourir à divers endroits d'une zone → on réapparaît TOUJOURS au même
  cimetière (le défaut de la zone), quelle que soit la position de mort.

## 6. Déploiement

> ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS (master/shard).** Changement de
> `ServerApp::HandleRespawnRequest` (sélection cimetière) + ajout `RespawnRules`. **Pas de
> changement wire** (opcodes respawn v13 inchangés, payload identique) → un **client ancien
> reste compatible** avec le serveur neuf. Mais le binaire serveur DOIT être rejoué pour que la
> nouvelle sélection s'applique. Côté client : **aucun changement** (ni redéploiement client requis).

## 7. Hors périmètre
- Auberge (`inn`) : sélection inchangée.
- `GraveyardManager` (chemin DB, `ClosestGraveyard`) : non utilisé par le respawn UDP actif → non touché.
- Affichage client d'un marqueur de cimetière par zone.
- Cimetières multiples par faction dans une même zone (l'ordre de fichier suffit ; pas de flag `default` dédié pour l'instant — YAGNI).
