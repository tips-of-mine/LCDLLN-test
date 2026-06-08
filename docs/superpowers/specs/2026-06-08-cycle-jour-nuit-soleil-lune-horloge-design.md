# Design — Nuit plus sombre, soleil/lune visibles, horloge à la liste des personnages

**Date** : 2026-06-08
**Statut** : validé (brainstorming), prêt pour plan d'implémentation
**Périmètre** : rendu client (cycle jour/nuit, ciel) + réseau master/client (horloge monde)

---

## 1. Contexte

Le projet dispose déjà d'un système d'horloge monde serveur-autoritaire unifié
(jour/nuit + lune), introduit par la PR #850. Côté client, `DayNightCycle`
calcule les couleurs de ciel, la lumière directionnelle, l'ambiant et les
paramètres lunaires ; `SkyPass` rend le ciel (gradient + glow solaire + disque
lunaire procédural) via `game/data/shaders/sky.frag`.

Trois problèmes constatés en jeu :

1. **La nuit est bleu foncé** au lieu d'être vraiment sombre.
2. **Le soleil et la lune ne sont pas visibles.**
3. **L'horloge est récupérée trop tard** : actuellement après l'entrée dans le
   monde (post-spawn, opcode 203). On la veut dès la récupération de la **liste
   des personnages** (écran de sélection).

Le SkyPass fonctionne (le ciel de nuit bleu *s'affiche* déjà), donc l'invisibilité
soleil/lune n'est pas une panne de pipeline mais un problème de
positionnement/taille/couleur.

---

## 2. Décisions de design (validées)

| Sujet | Décision |
|-------|----------|
| Noirceur de la nuit | **Sombre mais navigable** : nettement plus sombre + désaturée, on garde un ambiant minimum et le boost de pleine lune. |
| Soleil / lune | **Diagnostic d'abord** (fait par analyse statique), puis correction ; polish (taille disque/bloom) ajusté après validation en jeu. |
| Horloge → liste perso | **Piggyback** : embarquer l'état horloge dans la réponse liste des personnages (opcode 40). Wire-breaking, redéploiement master requis. |

---

## 3. Volet 1 — Nuit plus sombre et moins bleue

**Fichier principal** : `src/client/render/DayNightCycle.cpp`
**Fichier secondaire** : système d'auto-exposition (`AutoExposure.*` / `TonemapPass.*`)

### 3.1 Couleurs de ciel de nuit

Assombrir et désaturer le bleu (point de départ, à affiner en jeu) :

- `kSkyZenithNight` : `{0.01, 0.02, 0.08}` → `{0.004, 0.005, 0.012}`
- `kSkyHorizonNight` : `{0.04, 0.06, 0.14}` → `{0.012, 0.014, 0.025}`

Objectif : ramener le ratio bleu/rouge de ~8:1 à ~3:1 et baisser la luminance
globale d'un facteur ~2–3.

### 3.2 Ambiant de nuit

- `kAmbientNightMin` : `0.04` → `0.02` (plancher plus bas, nuit plus sombre).
- Réduire le biais bleu de l'ambiant : le canal B vaut actuellement 2× les canaux
  R/G (lignes 301-303). Resserrer vers un gris neutre froid plutôt que franchement
  bleu, tout en gardant un minimum non nul.
- Conserver le `moonBoost` (lignes 311-317) : la pleine lune éclaire toujours
  davantage la scène (navigabilité).

### 3.3 Auto-exposition (levier décisif)

Même en assombrissant les couleurs, l'auto-exposition tend à **réhausser** la
luminosité moyenne la nuit et annulerait l'effet. Il faut **plafonner
l'exposition la nuit**.

Mécanisme : exposer l'état jour/nuit (`isDaytime` et/ou élévation du soleil) au
module d'auto-exposition et réduire `maxExposure` (ou la cible de luminance) quand
on est de nuit. La transition doit être lissée pour éviter un palier brutal à
l'aube/crépuscule.

> Le réglage exact dépend de l'API d'`AutoExposure` ; le plan d'implémentation
> précisera le point d'injection. Si le câblage s'avère trop invasif, il peut être
> isolé dans une sous-tâche dédiée, mais il reste **dans le périmètre** : sans lui,
> l'assombrissement du ciel ne se voit pas en jeu.

### 3.4 Critère d'acceptation

À minuit, pleine lune : la scène est nettement plus sombre qu'aujourd'hui, la
dominante bleue a disparu (gris froid neutre), mais le terrain reste discernable.

---

## 4. Volet 2 — Soleil et lune visibles

**Fichiers** : `src/client/render/DayNightCycle.{h,cpp}`,
`src/client/app/Engine.cpp` (push constants SkyPass + IBL),
`game/data/shaders/sky.frag`.

### 4.1 Cause racine (diagnostic)

1. **Lune sous l'horizon** : la nuit, `ComputeState` bascule `lightDir` sur la
   direction de la **lune**. Or `Engine.cpp:5565` calcule `moonDir = -lightDir`,
   convention valable uniquement quand `lightDir = soleil`. La nuit, cela place
   la lune du côté opposé (sous l'horizon) → disque jamais dessiné. La même
   inversion existe pour l'IBL (`Engine.cpp:4078` et `7988`).
2. **Glow solaire mal alimenté la nuit** : `sky.frag` dessine le halo « soleil »
   à partir de `pc.lightDir`, qui vaut la lune la nuit → halo blanc parasite à la
   position de la lune.
3. **Trajectoire déformée** : `DayNightCycle.cpp:240-242` calcule
   `cos(clampedElev)` alors que `clampedElev` est un **sinus** d'élévation
   (`cos(sin(x))`), ce qui déforme l'azimut/élévation projeté, surtout haut dans
   le ciel.
4. **Soleil minuscule** : `pow(sunDot, 256)` produit un disque sous-degré, peu
   visible, sans cœur lumineux net ni bloom.

### 4.2 Corrections

1. **Directions explicites** : `DayNightCycle::State` expose désormais **deux**
   directions indépendantes et toujours correctes :
   - `sunDir` (vers le soleil), valable jour comme nuit (peut être sous l'horizon).
   - `moonDir` (vers la lune), valable jour comme nuit.
   Les deux sont calculées à partir de leur propre élévation/azimut (le soleil et
   la lune décalés de 12 h), indépendamment de quelle source éclaire la scène.
2. **Engine** : pousser `sunDir` et `moonDir` réels au SkyPass (et à l'IBL) au
   lieu de `lightDir` / `-lightDir`. Le shader utilise `sunDir` pour le glow
   solaire et `moonDir` pour le disque lunaire.
3. **Shader** : le glow solaire n'apparaît que lorsque le soleil est au-dessus de
   l'horizon (`sunDir.y > seuil`) ; le disque lunaire lorsque la lune l'est. Plus
   de halo parasite.
4. **Fix `cos(sin)`** : convertir le sinus d'élévation en angle (`asin`) avant le
   `cos`, ou recomposer le vecteur direction proprement (élévation = angle réel).
5. **Lisibilité (après validation en jeu)** : si nécessaire, agrandir le disque
   solaire (baisser l'exposant `pow` et/ou ajouter un cœur plein) et la lune
   (`kMoonRadius`). Réglage final décidé une fois les positions correctes.

### 4.3 Diagnostic d'appoint

Étendre le log de vérification existant (`Engine.cpp:3334-3352`, `[Sky] sunDir=…`)
pour afficher **séparément** `sunDir`, `moonDir` et leurs élévations, afin de
confirmer en jeu après le correctif.

### 4.4 Critère d'acceptation

- De jour : le soleil est visible (disque + halo) à une position cohérente avec
  l'heure (lever est, coucher ouest).
- De nuit : la lune est visible au bon endroit, avec sa phase ; aucun halo blanc
  parasite.

---

## 5. Volet 3 — Horloge récupérée à la liste des personnages (piggyback)

**Fichiers** :
`src/shared/network/CharacterPayloads.{h,cpp}`,
`src/masterd/handlers/character/CharacterListHandler.cpp`,
`src/masterd/handlers/worldclock/WorldClockHandler.{h,cpp}` (lecture de l'état),
`src/client/...` (réception liste + application horloge),
`src/client/app/Engine.cpp` (suppression de la requête 203 initiale).

### 5.1 Wire format

Étendre `CharacterListResponsePayload` :

```
CharacterListResponsePayload {
    uint8  success;
    <entries…>                       // inchangé
    // NOUVEAU — bloc horloge (présent si success == 1) :
    uint8  hasWorldClock;            // 0/1
    WorldClockStateResponse worldClock;  // sérialisé seulement si hasWorldClock == 1
}
```

Le bloc horloge réutilise la sérialisation existante de
`WorldClockStateResponse` (`WorldClockPayloads`). Builder et parser de
`CharacterPayloads.cpp` mis à jour en conséquence. Le parser reste strict
(client + master déployés ensemble), cohérent avec les extensions précédentes
de l'opcode 40.

### 5.2 Master

`CharacterListHandler`, au moment de construire la réponse, lit l'état courant de
l'horloge via le `WorldClockHandler` (état mutable protégé par mutex) et le joint
à la réponse (avec `serverTimeUnixMs` capturé au moment de l'envoi, comme pour
l'opcode 204).

### 5.3 Client

À la réception de la réponse liste des personnages (opcode 40) :

1. Parser le bloc horloge s'il est présent.
2. Appeler `DayNightCycle::SetServerClock(params, serverTimeUnixMs, clientRecvUnixMs)`.
   → le cycle jour/nuit devient correct **dès l'écran de sélection de personnage**.
3. Conséquence : la requête horloge initiale (opcode 203) déclenchée post-EnterWorld
   (`Engine.cpp` ~8370-8382) devient redondante et est **supprimée**.

La **re-synchro périodique anti-drift** (opcode 203/204, `Engine.cpp` ~7947-7963)
reste **inchangée**.

> Point à trancher en implémentation : l'acheminement de l'état horloge depuis la
> couche auth/flow (où la liste est reçue) jusqu'au `DayNightCycle` de l'`Engine`.
> Deux options : (a) un porteur partagé consommé par l'Engine (à l'image de
> `EnterWorldCommand`), ou (b) appliquer `SetServerClock` directement si le
> `DayNightCycle` est déjà accessible à ce stade. Le plan retiendra l'option la
> moins invasive après lecture du flux exact (`MasterShardClientFlow` /
> `AuthScreenCharacterSelect`).

### 5.4 Critère d'acceptation

- L'écran de sélection de personnage reflète l'heure du serveur (jour/nuit).
- Plus aucune requête 203 « initiale » post-spawn ; seule la re-synchro
  périodique subsiste.
- Round-trip de l'opcode 40 correct avec et sans bloc horloge.

---

## 6. Tests

### 6.1 Tests unitaires (CI build-linux / ctest)

- **Sérialisation opcode 40** : round-trip `Build…`/`Parse…` de
  `CharacterListResponsePayload` avec `hasWorldClock = 0` puis `= 1` (vérifier
  l'égalité des champs horloge).
- **DayNightCycle** : cohérence des directions — `sunDir` et `moonDir` opposés
  (~12 h de décalage), élévations attendues à 0 h / 6 h / 12 h / 18 h, absence de
  `cos(sin)` (vérifier qu'à midi `sunDir` pointe quasi au zénith).

### 6.2 Validation en jeu (manuelle, non automatisable)

- Nuit sombre, désaturée, mais navigable (pleine lune).
- Soleil visible le jour, lune visible la nuit (avec phase), sans halo parasite.
- Heure correcte dès l'écran de sélection de personnage.

> Rappel CI : `build-linux` lance ctest ; `build-windows` ne lance pas les tests.
> Attention au piège assert + NDEBUG.

---

## 7. Déploiement

- **Volet 1 (nuit)** : ✅ client uniquement.
- **Volet 2 (soleil/lune)** : ✅ client uniquement.
- **Volet 3 (piggyback horloge)** : ⚠️ **redéploiement serveur master requis**
  (wire-break opcode 40). Client + master doivent être déployés en **lock-step**
  (un client neuf parlerait à un master ancien sinon, et inversement).

Mention finale à inclure dans la description de PR :
> **Déploiement** : ⚠️ redéploiement serveur (master) requis — wire-break opcode 40
> (horloge embarquée dans la réponse liste des personnages). Client + master en
> lock-step. Volets rendu (nuit + soleil/lune) : client uniquement.

---

## 8. Hors périmètre (YAGNI)

- Pas de vraie texture lunaire (asset) — le disque procédural actuel suffit.
- Pas de trajectoire astronomique réaliste (latitude/longitude, libration,
  réfraction atmosphérique).
- Pas de lens flare ni de refonte du pipeline bloom (au-delà d'un éventuel
  agrandissement du disque solaire).
- Variante réseau « sans wire-break » (requête 203 envoyée dès réception de la
  liste) écartée au profit du piggyback.
