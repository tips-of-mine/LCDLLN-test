# Design — Système de nuages volumétriques (client)

_Date : 2026-06-06 — Statut : approuvé (brainstorming), prêt pour plan d'implémentation._

## 1. Contexte et périmètre

### Origine
Demande initiale : « est-il possible de mettre un système de vie de nuage dans
le ciel pour la version client ? » — avec, après cadrage, une ambition de
**météo dynamique vivante + effets gameplay** (ombres au sol, pluie/neige,
visibilité réduite, modificateurs chiffrés), en **serveur-autoritaire**, rendu
**volumétrique ray-marché pleine qualité**.

### Découverte déterminante (anti-doublon)
Un système météo serveur-autoritaire **existe déjà** et couvre l'essentiel des
effets demandés :

| Brique existante | État | Preuve |
|------------------|------|--------|
| Météo serveur par zone (types discrets + intensité, reroll Markov) | V1 | `src/shardd/weather/WeatherManager.h`, `WeatherHandler` (opcodes 150-156) |
| Broadcast serveur→clients d'une zone | OK | M100.26 `WeatherBroadcaster` master, push opcode 156 |
| Particules pluie/neige + brouillard + audio (client) | OK mais **non branché au réseau** (cf. §9) | `src/client/render/WeatherSystem.h` (M38.2) |
| Modificateurs de surface (sol mouillé, glissant) | OK | M100.26 `weather_modifiers.json` |
| Zones d'override météo (polygones éditeur) | OK (PR #811) | M100.28 |

**Le seul vrai manque** : il n'y a **aucun nuage dans le ciel**. Le ciel est un
dégradé procédural (soleil/lune) sans couche nuageuse. Tout le système météo
agit au sol et dans l'air proche (précipitations, brouillard, surfaces), jamais
sur le dôme céleste.

### Périmètre retenu
**Un seul spec, 100 % client, aucun changement serveur/wire, aucun
redéploiement.** On ajoute une **passe de rendu de nuages volumétriques
ray-marchés**, pilotée par l'état météo serveur **déjà diffusé**, intégrée au
cycle jour/nuit, à l'IBL et projetant des ombres au sol.

### Décisions de cadrage (validées en brainstorming)
- **Ambition** : météo dynamique + effets gameplay (la plupart déjà fournis par
  l'existant ; ce spec ajoute les nuages célestes manquants).
- **Autorité** : serveur-autoritaire — déjà le cas via le broadcast météo
  existant. Les nuages en **dérivent** côté client (Voie A) : aucun trafic
  réseau ni opcode supplémentaire.
- **Technique** : volumétrique ray-marché, **modèle « weather-map » (style
  Horizon/Nubis)**.
- **Performance** : **pleine qualité**, cible haut de gamme, sans chemin de
  repli billboard. Un *knob* de samples reste exposé en config (réglage de pas,
  pas un second pipeline). Coût élevé sur petites GPU **assumé**.

### Hors-scope explicite
- Toute modification serveur, wire, opcode, handler, migration.
- Re-capture IBL du champ volumétrique complet (amélioration future ; v1 = 
  couplage ambiant scalaire, cf. §7).
- Chemin de rendu billboard de repli pour petites configs.
- Foudre interactive, inondations, hazards (déjà hors-scope M100.26).
- Refonte du système météo existant (particules, surfaces, zones override).

## 2. Approches de modèle de nuage envisagées

- **A — Volumétrique « weather-map » (RETENU)** : une *cloud map* 2D (couverture,
  type, hauteur) module un champ de densité 3D Worley+Perlin échantillonné par
  raymarch ; éclairage Beer-Powder + multi-scatter approché. Standard AAA, riche
  et vivant (variété cumulus/stratus/cumulonimbus).
- **B — Couche homogène simple** : une seule strate raymarchée, bruit 3D sans
  cloud-map. Plus simple, visuellement pauvre. **Écarté** (pas de variété).
- **C — Hybride 2.5D** : couches procédurales à faux volume. **Écarté** (l'objectif
  est le volumétrique plein).

## 3. Architecture / composants

Nouveau sous-système client, isolé et testable. Frontières nettes : la logique
pure (mapping, params) n'a **aucune dépendance Vulkan** ; seul `CloudPass`
touche le GPU.

| Composant | Rôle | Dépendances | Fichier proposé |
|-----------|------|-------------|-----------------|
| `CloudWeatherMapper` | **Pur** : `render::WeatherState` (du `WeatherSystem` existant) + `blendT` → `CloudParams`. Table de correspondance déterministe | aucune (testable sans GPU) | `src/client/render/clouds/CloudWeatherMapper.{h,cpp}` |
| `CloudParams` | État continu (couverture, densité, altitude base/sommet, vent, teinte) + interpolation/fondu | aucune | `src/client/render/clouds/CloudParams.{h,cpp}` |
| `CloudPass` | Passe de rendu **graphique plein écran** (fragment raymarch) + compositing | Vulkan, `DayNightCycle`, depth scene | `src/client/render/CloudPass.{h,cpp}` |
| Shaders | raymarch + compositing (fragment) ; bruit FBM **in-shader** ; shadow en P2 | — | `game/data/shaders/clouds.frag`, `clouds_common.glsl`, `clouds_shadow.comp` (P2) |

Flux : `WeatherSystem` (existant) → `CloudWeatherMapper` → `CloudParams`
(interpolé chaque frame) → push-constants → `CloudPass` (GPU).

> **Raffinements d'implémentation (post-plan, cohérents avec le moteur réel)** :
> (1) **Bruit FBM procédural in-shader** plutôt qu'un sous-système de textures 3D
> `CloudNoise` (comme `sky.frag`) → moins de composants, pas de storage-image ;
> textures 3D = optimisation future. (2) **Passe fragment fullscreen** (pas
> compute) car le FrameGraph ne gère pas les barrières storage-image (cf.
> `VolumetricFogPass`). (3) Insertion **après le Volumetric Fog, avant le Bloom**
> (cf. §5) — câblage minimal + perspective aérienne sur les nuages lointains.

## 4. Modèle de nuage (détail technique)

- **Densité** : `density(p) = profilVertical(p.y) * cloudMap(p.xz).coverage`
  modulée par bruit 3D base (Worley-Perlin) **érodé** par un bruit de détail
  haute fréquence. `cloudMap` porte couverture, type (cumulus↔stratus) et
  hauteur de couche.
- **Animation (vie du ciel)** : le **vent** décale les UV d'échantillonnage en
  fonction du temps → dérive et déformation continues. Les transitions météo
  font **fondre** couverture/densité (réutilise le `blendT`/fondu 30 s existant)
  → formation/dissipation perçue.
- **Éclairage** :
  - Transmittance **Beer** le long du rayon vers le soleil (`DayNightCycle::lightDir`).
  - Terme **Powder** pour l'auto-ombrage sombre des bords.
  - Phase **Henyey-Greenstein** pour les godrays / halo directionnel.
  - Couleurs soleil/ambiant issues de `DayNightCycle::State` (cohérence jour/nuit).

## 5. Intégration dans la frame de rendu

- **Placement** : **après Lighting, avant Volumetric Fog**.
  - Lit le **depth** de la scène → terrain/montagnes occultent correctement les
    nuages.
  - Le brouillard volumétrique existant s'applique **ensuite** par-dessus
    (atmosphère cohérente).
- **Implémentation** : `clouds_raymarch.comp` écrit une cible
  **couleur + transmittance** ; `clouds_composite` mélange sur la scène
  (`color = scene * transmittance + cloudColor`).
- **Résolution** : pleine (choix utilisateur). Nombre de pas raymarch / light
  réglable par config. **TAA existant conservé** en aval (pas de réprojection
  temporelle dédiée en v1).

## 6. Cycle jour/nuit

Couleurs et direction de lumière pilotées **chaque frame** par
`DayNightCycle::State` (soleil/lune, teintes zénith/horizon) : nuages teintés
rose à l'aube, gris/bleutés la nuit (lune), blancs au zénith. Aucune logique
horaire dupliquée — `CloudPass` **consomme** l'état déjà calculé.

## 7. IBL (éclairage ambiant)

- **v1 (ce spec)** : pas de re-capture du champ volumétrique dans la cubemap IBL
  (coût prohibitif). On module l'ambiant/le soleil par un **scalaire de
  couverture** issu de `CloudParams` : ciel couvert → ambiant plus diffus,
  soleil directionnel atténué. Cheap, suffisant pour la cohérence.
- **Future (hors-scope)** : injecter une approximation des nuages dans
  `sky_capture.comp` pour une IBL pleinement consciente des nuages.

## 8. Ombres de nuages au sol

**Implémenté (Phase 2)** : plutôt qu'une `clouds_shadow.comp` + une modification du
`lighting.frag` déféré (descriptor set fragile, cf. CLAUDE.md), les ombres sont
calculées **dans la même passe `clouds.frag`** : pour les pixels de géométrie
(`depth < 1`), on marche le rayon **soleil** à travers la dalle de nuages depuis
la position monde du sol, on accumule `cloudDensity()` (même champ que la dalle
vue → **aucun doublon**) et on assombrit `sceneColor` avant le compositing du
ciel. Avantages : une seule passe, aucun changement de pipeline/descriptor du
lighting, aucune nouvelle ressource frame-graph. Réglable via
`render.clouds.shadow_strength` (0 = désactivé). Ombres mouvantes synchronisées
avec le vent. Une cloud-shadow-map top-down dédiée reste une optimisation future.

## 9. Pilotage par la météo — état réel résolu (anti-doublon)

Investigation faite en amont du plan (exigence « pas de doublons ») :

- **Une seule classe `WeatherSystem` existe** : `src/client/render/WeatherSystem`
  (M38.2), instanciée dans `Engine` (`Engine.h:517 m_weatherSystem`). Le chemin
  `src/client/world/weather/WeatherSystem` du ticket M100.26 **n'a jamais été
  créé** (confirmé par l'audit M100 : « pas de sous-dossier `world/weather/` »).
  → **Le mapper réutilise cette unique classe ; on ne crée AUCUN nouveau
  détenteur d'état météo.**
- **Gap pré-existant** : le broadcast météo serveur (opcode 156
  `WeatherUpdateNotification`) est reçu dans `Engine.cpp` mais routé **uniquement
  vers `m_weatherUi`** (panneau UI/debug). `m_weatherSystem.SetWeather()` n'est
  appelé **nulle part** → aujourd'hui la météo serveur ne pilote **ni** les
  visuels existants **ni** (a fortiori) les nuages.

### Conséquence de conception
Pour éviter tout doublon et corriger la chaîne au bon endroit :

1. Au handler de l'opcode 156 (déjà présent, `Engine.cpp:2968`), **mapper le
   `WeatherKind` serveur → `render::WeatherState`** et appeler
   `m_weatherSystem.SetWeather(...)`. Cela alimente d'un coup les particules/
   brouillard existants **et** les nuages, depuis le **signal autoritaire
   unique**. (Petit ajout ciblé, pas une refonte.)
2. `CloudWeatherMapper` lit l'état de `m_weatherSystem`
   (`GetCurrentState()` / `GetTargetState()` / `GetIntensity()`) — **source
   unique de vérité météo côté client**.

> ⚠️ **Décalage d'enum à gérer dans le mapping (1)** : serveur
> `WeatherKind { Clear=0, Rain=1, Snow=2, Storm=3, Sandstorm=4, Fog=5 }` vs
> client `render::WeatherState { Clear=0, Rain=1, Snow=2, Fog=3, Storm=4 }`
> (pas de `Sandstorm`, indices `Fog`/`Storm` inversés). Le mapping doit être
> explicite, pas un cast. `Sandstorm` → repli (`Fog` ou `Storm`) à trancher.

Table de correspondance `render::WeatherState` → `CloudParams` (indicatif, à
affiner en jeu) :

| WeatherState | Couverture | Type / densité | Teinte |
|--------------|-----------|----------------|--------|
| Clear | faible | cumulus épars | claire |
| Fog | moyenne (bas) | stratus bas dense | gris pâle |
| Rain | élevée | nimbostratus couvert | gris |
| Storm | maximale | cumulonimbus, très dense | sombre |
| Snow | moyenne | stratus clair | blanc froid |

Le **vent** est dérivé **déterministiquement de l'horloge partagée**
(`WorldClock`) → cohérent entre joueurs **sans aucun trafic réseau**.

## 10. Configuration (`config.json`)

Nouvelles clés sous `render.clouds` (lues client uniquement) :

- `enabled` (bool)
- `raymarchSteps` (int) — pas le long du rayon vue
- `lightSteps` (int) — pas le long du rayon soleil
- `shadowMapEnabled` (bool)
- `maxDistanceMeters` (float) — distance de raymarch
- `windScale` (float) — vitesse de dérive

Pas de second pipeline : uniquement des bornes de qualité.

## 11. Convention winding (garde anti-régression)

La passe nuages est **fullscreen** (pas de mesh culé) — **aucun** risque sur le
`frontFace` terrain. Ce spec **ne touche à aucun
`VkPipelineRasterizationStateCreateInfo`** existant (cf. CLAUDE.md, section
winding/face culling).

## 12. Tests

- `CloudWeatherMapperTests` (pur, sans GPU) : chaque `WeatherKind` + `blendT` →
  `CloudParams` attendus ; interpolation monotone aux bornes.
- `CloudParamsTests` : fondu correct sur transition (couverture/densité lerpées
  sur la durée).
- Validation visuelle en jeu pour le raymarch (non testable unitairement) :
  aube/midi/nuit, transition Clear→Storm, ombres au sol mouvantes.

Les tests unitaires nouveaux doivent être ajoutés au `CMakeLists.txt` et exclus
ou inclus selon la convention CI (`build-linux.yml` lance `ctest`).

## 13. CMake

- Ajouter `CloudWeatherMapper.cpp`, `CloudParams.cpp`, `CloudNoise.cpp`,
  `CloudPass.cpp` à `engine_core` (client).
- `CloudWeatherMapper`/`CloudParams` (logique pure) **ne doivent pas** dépendre
  de Vulkan → idéalement compilables côté tests sans device.
- Aucun ajout à `server_app` (feature 100 % client).
- Nouveaux exécutables de test (`cloud_weather_mapper_tests`, etc.) liés à
  `engine_core`.

## 14. Déploiement

> **Déploiement** : ✅ **client uniquement, pas de redéploiement serveur.** Les
> nuages sont dérivés de l'état météo déjà diffusé ; aucun changement de wire,
> d'opcode, de handler ni de migration.

## 15. Risques / pièges

- **Coût GPU** : pleine qualité = lourd sur petites configs (assumé). Le knob
  `raymarchSteps`/`lightSteps` permet d'ajuster sans recompiler.
- **Pas de doublon `WeatherSystem`** : résolu (cf. §9) — une seule classe,
  réutilisée. Vigilance : ne pas réintroduire un détenteur d'état météo
  parallèle ; le mapper lit `m_weatherSystem`.
- **Gap réseau→visuel pré-existant** : le branchement opcode 156 →
  `m_weatherSystem.SetWeather()` (cf. §9) est un correctif inclus dans ce spec.
  Sans lui, ni les nuages ni les particules existantes ne réagissent à la météo
  serveur.
- **Ordre des passes** : insérer strictement avant le Volumetric Fog et après
  Lighting ; vérifier que le depth de la scène est disponible et correctement
  lu (occultation montagnes).
- **Bandes de raymarch** : risque de banding visible ; mitiger par dithering
  bleu-noise sur l'offset de départ du rayon (déjà un pattern dans le repo, cf.
  Interleaved Gradient Noise du SSAO, PR #851).
