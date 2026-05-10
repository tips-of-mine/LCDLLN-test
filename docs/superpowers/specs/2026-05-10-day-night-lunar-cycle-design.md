# Cycle jour/nuit + 16 phases lunaires — Design

**Date** : 2026-05-10
**Auteur** : Claude (brainstorming avec hcornet)
**Statut** : Spec validée, prête pour writing-plans

---

## Résumé exécutif

Étendre le moteur LCDLLN avec un système de phases lunaires à 16 étapes
distinctes, synchronisé serveur ↔ client, avec rendu visuel procédural dans
le sky shader existant. Vérifier en parallèle que le cycle jour/nuit déjà
en place (`DayNightCycle`, `WorldClock`) est fonctionnel via tests
unitaires + commandes debug.

Le cycle lunaire dure **2 semaines réelles** (16 phases × ~21h chacune),
indépendant du cycle jour/nuit en jeu (qui tourne à 24min réelles par jour
en jeu par défaut). La phase est calculée de manière déterministe à partir
de l'epoch Unix, donc identique pour tous les joueurs sans synchronisation
explicite, et trivialement reproductible pour les tests.

La "Lune Noire" du titre LCDLLN correspond aux phases 0/14/15
(illumination ≤ 1%) qui couvrent ~3 nuits par cycle de 14 jours, soit
~21% du temps. Cette phase pourra servir de condition de déclenchement
pour de futurs game events.

---

## Architecture

```
┌─────────────────────────┐    ┌─────────────────────────┐
│  Master (authoritative) │    │  Client (visualization) │
│                         │    │                         │
│  WorldClock (existant)  │    │  DayNightCycle          │
│  LunarCalendar (header) │    │   (existant + extension)│
│  LunarHandler           │    │   + moon phase mirror   │
│   + tick périodique     │    │   + sky shader uniform  │
│   + push on phase change│    │                         │
└──────────┬──────────────┘    └────────▲────────────────┘
           │                            │
           │  opcodes 192-194 :         │
           │   State / Push             │
           └────────────────────────────┘
```

### Composants

1. **`engine::server::world::LunarCalendar`** — header-only à
   `src/shardd/world/LunarCalendar.h`, namespace existant. Calcule la
   phase courante (0..15) depuis `realNowMs`, `cycleStartTsMs`, et
   `cycleDurationMs` (= 14j × 24h × 3600s × 1000ms = 1 209 600 000 ms).
   Pure stateless, méthode statique `Compute(realNowMs, cycleStartTsMs,
   cycleDurationMs)` → struct `{ uint8_t phase; float illumination; }`.
   Partagé par master et shardd (déterministe).

2. **`engine::server::LunarHandler`** — master-side, à
   `src/masterd/handlers/lunar/LunarHandler.{h,cpp}`. Setters
   `SetServer`/`SetSessionManager`/`SetConnectionSessionMap`. Dispatch
   opcode 192 (`LunarStateRequest`). Méthode publique `Tick(nowMs)`
   appelée toutes les 5 minutes depuis la boucle main du master :
   compare `LunarCalendar::Compute()` avec `m_lastBroadcastPhase` ;
   si différent, push `LunarPhaseChangeNotification` (opcode 194) à
   tous les clients connectés via `ConnectionSessionMap::Snapshot()`.
   Méthode publique `CurrentPhase() const` pour intégration future
   avec le système d'events.

3. **`engine::render::DayNightCycle`** étendu — ajouts au struct
   `State` : `uint8_t moonPhase`, `float moonIllumination`. Nouveau
   callback `OnLunarPhaseChange(uint8_t, float)` settable depuis
   l'Engine. Pas de calcul local de la phase (master autoritaire).

4. **Sky pass C++** — découverte pendant la conception : les fichiers
   `game/data/shaders/sky.frag` et `sky.vert` existent (M38.1) mais ne
   sont actuellement **pas wirés** dans le pipeline Vulkan client. Le
   ciel est aujourd'hui une simple `clearColor` issue de
   `DayNightCycle.skyHorizon`. Cette spec inclut donc la création de
   `src/client/render/SkyPass.{h,cpp}` (pattern aligné sur les autres
   passes — `AuthLogoPass`, `LightingPass`, `WaterPass`) qui charge
   `sky.vert.spv` + `sky.frag.spv` et fait un draw fullscreen quad avec
   les push-constants `SkyPC` étendus :

   ```glsl
   layout(push_constant) uniform SkyPC {
       mat4  invViewProj;       // existant
       vec3  lightDir;          // existant (sun ou moon selon nuit/jour)
       float _pad0;
       vec3  zenithColor;       // existant
       float _pad1;
       vec3  horizonColor;      // existant
       float _pad2;
       vec3  moonDir;           // NOUVEAU (12h offset du soleil)
       float moonIntensity;     // NOUVEAU (fade jour/nuit)
       float moonPhase;         // NOUVEAU (0..15)
       float moonIllumination;  // NOUVEAU (0..1)
       vec2  _pad3;
   } pc;
   ```

   Le `sky.frag` actuel est étendu avec ~30 lignes pour le disque
   lunaire (cf. section Rendering). Le `clearColor` actuel est
   conservé en fallback si SkyPass échoue à initialiser.

5. **Engine client (`src/client/app/Engine.{h,cpp}`)** — push handler
   dispatch sur opcodes 193/194. Sur EnterWorld (post-auth), envoi de
   `LunarStateRequest` pour récupérer l'état initial. Slash commands
   debug `/sky info`, `/sky time <hours>`, `/sky moon <phase 0..15>`.

---

## Les 16 phases lunaires

Subdivision des 8 phases astronomiques classiques en `Early` / `Late`,
plus une phase `Earthshine` symétrique pour boucler le cycle.

| Index | Nom (FR) | Nom (EN) | Illumination | Type |
|---|---|---|---|---|
| 0 | Nouvelle Lune | NewMoon | 0% | Lune Noire |
| 1 | Premier Croissant tôt | WaxingCrescentEarly | 6% | Croissant croissant |
| 2 | Premier Croissant tard | WaxingCrescentLate | 19% | Croissant croissant |
| 3 | Premier Quartier | FirstQuarter | 25% | Quartier |
| 4 | Gibbeuse Croissante tôt | WaxingGibbousEarly | 44% | Gibbeuse croissante |
| 5 | Gibbeuse Croissante tard | WaxingGibbousLate | 69% | Gibbeuse croissante |
| 6 | Pleine Lune ascendante | FullMoonRising | 94% | Pleine |
| 7 | Pleine Lune | FullMoon | 100% | Pleine |
| 8 | Pleine Lune descendante | FullMoonSetting | 94% | Pleine |
| 9 | Gibbeuse Décroissante tôt | WaningGibbousEarly | 69% | Gibbeuse décroissante |
| 10 | Gibbeuse Décroissante tard | WaningGibbousLate | 44% | Gibbeuse décroissante |
| 11 | Dernier Quartier | LastQuarter | 25% | Quartier |
| 12 | Dernier Croissant tôt | WaningCrescentEarly | 19% | Croissant décroissant |
| 13 | Dernier Croissant tard | WaningCrescentLate | 6% | Croissant décroissant |
| 14 | Lune Cendrée tôt | EarthshineEarly | 1% | Quasi-noire |
| 15 | Lune Cendrée tard | EarthshineLate | 1% | Quasi-noire |

### Calcul de l'illumination

Sinusoïde centrée sur Full Moon (index 7) :

```cpp
float Illumination(uint8_t phase) {
    float t = (static_cast<int>(phase) - 7) * (3.14159265f / 8.0f);
    return 0.5f * (1.0f + std::cos(t));
}
```

Cette formule donne `1.0` pour phase=7 et `~0` pour phases 0 et 15 (les
extrêmes). Les phases 0/14/15 ont illumination ≤ 1% (lune noire effective).

### Lien avec le lore LCDLLN

Les 3 phases "Lune Noire" (0, 14, 15) couvrent ~3 jours réels par cycle
de 14 jours, soit ~21% du temps. C'est ni trop rare ni trop fréquent
pour servir de condition de déclenchement à de futurs events serveur
("Les Chroniques De La Lune Noire" — fil rouge thématique).

---

## Wire & data flow

### Opcodes (192-194)

```cpp
// =====================================================================
// Lunar wire — phase courante + push sur changement.
// 16 phases (0..15), cycle de 14 jours reels, calcul deterministe depuis
// epoch. Master autoritaire ; client recoit l'etat initial sur connexion
// puis un push toutes les ~21h sur changement de phase.
// =====================================================================
constexpr uint16_t kOpcodeLunarStateRequest             = 192u;
constexpr uint16_t kOpcodeLunarStateResponse            = 193u;
constexpr uint16_t kOpcodeLunarPhaseChangeNotification  = 194u;
```

### Payloads

```cpp
struct LunarStateRequest {
    // (vide)
};

struct LunarStateResponse {
    uint8_t  phase;             // 0..15
    float    illumination;      // 0..1
    uint64_t cycleStartTsMs;    // timestamp ms du début du cycle courant
    uint64_t cycleDurationMs;   // 14j × 24h × 3600s × 1000ms
};

struct LunarPhaseChangeNotification {
    uint8_t  newPhase;          // 0..15
    float    newIllumination;   // 0..1
    uint64_t nextChangeTsMs;    // timestamp ms du prochain changement
};
```

Sérialisation little-endian, helpers `WriteUInt8/16/32/64LE` et
`WriteFloatLE` existants. Pattern strict aligné avec les payloads
récents (Loot, Auction, Weather…).

### Flow

1. **Boot master** : `LunarHandler` initialise `m_cycleStartTsMs` (epoch
   Unix arbitraire, par exemple 2026-01-01 00:00:00 UTC = 1767225600000),
   `m_cycleDurationMs = 1209600000`, et calcule la phase courante.
2. **Connexion client** : sur EnterWorld, le client envoie opcode 192.
   Master répond opcode 193 avec l'état complet.
3. **Tick master (5 min)** : si la phase courante diffère de
   `m_lastBroadcastPhase`, master push opcode 194 à tous les clients
   connectés.
4. **Réception client** : `Engine::DispatchPushHandler` route opcodes
   193/194 vers `m_dayNightCycle.OnLunarPhaseChange(...)`.
5. **Sky shader** : à chaque frame, push-constants `moonPhase` et
   `moonIllumination` sont lus depuis `m_dayNightCycle.GetState()`.

### Pas de Subscribe/Unsubscribe

Différent de Weather/GameEvents : la lune est globale et permanente, le
push est broadcast à tous les clients connectés sans opt-in. Charge
négligeable (1 push par client toutes les ~21h).

---

## Rendering shader procédural

### Push-constants `sky.frag`

```glsl
layout(push_constant) uniform SkyParams {
    vec3 zenithColor;
    vec3 horizonColor;
    vec3 sunDir;
    vec3 moonDir;
    float sunIntensity;
    float moonIntensity;
    float moonPhase;        // 0..15, encode en float
    float moonIllumination; // 0..1
} sky;
```

### Algorithme

```glsl
// Distance angulaire au centre du disque lunaire
float cosA = dot(viewDir, sky.moonDir);
float moonRadius = 0.012; // ~0.7 degre, taille apparente reelle

if (cosA > cos(moonRadius)) {
    // Repere local 2D du disque
    vec3 right = normalize(cross(vec3(0,1,0), sky.moonDir));
    vec3 up    = cross(sky.moonDir, right);
    float u    = dot(viewDir, right) / sin(moonRadius);
    float v    = dot(viewDir, up)    / sin(moonRadius);

    // Masque d'ombre via 2 disques superposes
    float shadowOffset = 1.0 - sky.moonIllumination * 2.0;
    bool  shadowOnLeft = sky.moonPhase < 7.5;
    if (shadowOnLeft) shadowOffset = -shadowOffset;

    float distFromShadow = length(vec2(u - shadowOffset, v));
    float shadowMask     = smoothstep(1.0, 0.95, distFromShadow);

    vec3 moonSurface = vec3(0.95, 0.92, 0.85) * shadowMask;

    // Earthshine sur les phases proches de NewMoon
    if (sky.moonPhase < 3.0 || sky.moonPhase > 13.0) {
        moonSurface += vec3(0.05, 0.06, 0.10) * (1.0 - shadowMask);
    }

    // Halo doux
    float haloFalloff = smoothstep(cos(moonRadius * 1.5),
                                    cos(moonRadius), cosA);

    finalColor = mix(skyColor, moonSurface * sky.moonIntensity,
                     haloFalloff);
}
```

### Caractéristiques

- **Pas de texture** — entièrement procédural, ~30 lignes GLSL
- **Earthshine subtil** sur les phases proches de NewMoon (visible
  même à phase 0 si on regarde attentivement)
- **Halo doux** pour effet "MMO moderne"
- `moonIntensity` masque la lune le jour (fadé par `DayNightCycle`
  selon l'élévation du soleil)
- **Coût GPU négligeable** : un seul `if` par fragment, calculs simples

### Couleur surface lunaire

Constante `vec3(0.95, 0.92, 0.85)` (gris légèrement chaud). Pourra être
exposée en push-constant dans une future itération pour ajustement
artistique sans recompilation du shader.

---

## Tests & vérification

### A. `lunar_calendar_tests` — `src/shardd/world/LunarCalendarTests.cpp`

- `Compute(0, 0, 1209600000)` → phase 0
- `Compute(0, 0, 1209600000)` à `now=cycleDuration/2` → phase 7 ou 8
- `Compute(0, 0, 1209600000)` à `now=cycleDuration` → phase 0 (wrap)
- Boucle paramétrée 0..15 : phase varie correctement à chaque
  1/16 du cycle
- Illumination est sinusoïdale (max à phase 7, min aux phases 0 et 15)
- Toutes les valeurs ∈ [0, 15], jamais 16+

15+ assertions. Pas de framework, pattern existant (assert simples).

### B. `lunar_payloads_tests` — `src/shared/network/LunarPayloadsTests.cpp`

- Round-trip `LunarStateRequest` / `LunarStateResponse` /
  `LunarPhaseChangeNotification`
- Edge cases : phase=0/15, illumination=0.0/1.0, timestamps
  0/UINT64_MAX
- Reject buffer trop court
- 15+ tests

### C. `daynight_cycle_tests` — `src/client/render/DayNightCycleTests.cpp` (NOUVEAU)

Vérification du cycle jour/nuit existant qui n'a actuellement aucun
test unitaire :

- `Init(initialTimeOfDay=8)` → state.timeOfDay = 8, isDaytime = true
- `Advance(deltaSeconds=3600 * timeScale)` → timeOfDay avance d'1h
- À 6h : `lightDir.y ≈ 0` (lever soleil)
- À 12h : `lightDir.y ≈ 1` (zénith)
- À 18h : `lightDir.y ≈ 0` (coucher)
- À 0h : moon prend le relais comme directional light, isDaytime=false
- Couleurs lerpent monotonement entre keyframes
  dawn/noon/dusk/night
- `SetTime(25)` clamp ou wrap à 1.0
- `SetTimeScale(0)` clamp à 0.1 (no divide-by-zero)
- Nouvel état `moonPhase` = 0 par défaut, `moonIllumination` = 0
- Après `OnLunarPhaseChange(7, 1.0)` : state reflète

### D. CMake

3 nouveaux targets via `lcdlln_add_simple_test()` :
- `lunar_calendar_tests`
- `lunar_payloads_tests`
- `daynight_cycle_tests`

### E. Tests visuels in-game (manuels mais documentés)

#### Commande `/sky info`

Imprime dans la console et le log :

```
[Sky] timeOfDay=14:32  isDaytime=true
[Sky] sunDir=(+0.34, +0.91, +0.21)  intensity=0.95
[Sky] moonPhase=7 (FullMoon, illumination=100%)
[Sky] nextPhaseChange=in 18h 24m (real time)
```

#### Commande `/sky time <hours>`

Force `m_dayNightCycle.SetTime(hours)`. Permet de prévisualiser tout
le cycle 24h en quelques secondes.

#### Commande `/sky moon <phase 0..15>`

**Dev only**, override local de `m_dayNightCycle.m_state.moonPhase` et
`moonIllumination`. Master conserve la phase réelle. Permet de
prévisualiser tous les visuels en quelques secondes plutôt qu'attendre
14 jours réels.

### F. Checklist visuelle

À valider en lançant le client + master :

- [ ] Lune visible la nuit, invisible/atténuée le jour
- [ ] Position lune cohérente avec position soleil (opposée, 12h offset)
- [ ] Phase 0 (NewMoon) : lune quasi noire, légère lueur cendrée
- [ ] Phase 3 (FirstQuarter) : moitié droite éclairée
- [ ] Phase 7 (FullMoon) : disque entièrement éclairé, halo subtil
- [ ] Phase 11 (LastQuarter) : moitié gauche éclairée
- [ ] Transition entre phases (`/sky moon` rapide) douce, pas de saut
- [ ] Couleurs ciel cohérentes aux 4 transitions (dawn/noon/dusk/midnight)

---

## Limitations V1 (consignées)

- Lune sans texture surface (gris uniforme + masque). Future PR :
  texture cratère + normal map.
- Pas de gestion des éclipses solaires/lunaires.
- Pas de hook event lune ↔ GameEvents — `LunarHandler::CurrentPhase()`
  est exposé mais non consommé. Future PR : extension de
  `GameEventManager` avec condition `requires_lunar_phase`.
- Pas de persistance DB — `cycleStartTsMs` est hardcodé au boot
  master (epoch fixe 2026-01-01). Suffisant car le calcul est
  déterministe ; future PR pourra exposer `world.lunar.cycle_start`
  dans `config.json`.
- Pas de variation de luminosité ambiante selon la phase (full moon
  ne donne pas plus de lumière qu'une nouvelle lune). Future PR :
  modulation du `lightColor` nocturne par `moonIllumination`.
- Master autoritaire ; pas de SyncLunar RPC entre master et shardd
  (shardd recompute localement via `LunarCalendar` si besoin).

---

## Déploiement

⚠️ **REDÉPLOIEMENT MASTER LINUX REQUIS** — nouveaux opcodes 192-194 +
nouveau `LunarHandler` enregistré dans le dispatch lambda.

✅ Pas de migration DB.

Le client neuf parlera dans le vide à un master ancien (BAD_REQUEST sur
opcode 192) ; déploiement lock-step requis.

---

## Fichiers impactés (récapitulatif)

### Nouveaux

- `src/shared/network/LunarPayloads.{h,cpp}` (+ Tests.cpp)
- `src/shardd/world/LunarCalendar.h` (+ Tests.cpp)
- `src/masterd/handlers/lunar/LunarHandler.{h,cpp}`
- `src/client/render/DayNightCycleTests.cpp` (NOUVEAU)

### Modifiés

- `src/shared/network/ProtocolV1Constants.h` — opcodes 192-194
- `src/masterd/main_linux.cpp` — instanciation + dispatch
  + `&lunarHandler` dans capture-list lambda
- `src/client/render/DayNightCycle.{h,cpp}` — extension state +
  callback `OnLunarPhaseChange`
- `game/data/shaders/sky.frag` — disque lunaire procédural + push
  constants étendus
- `src/client/app/Engine.{h,cpp}` — dispatch + slash commands debug
  + intégration de `SkyPass` dans le pipeline de rendu
- `CMakeLists.txt` + `src/CMakeLists.txt` — nouveaux sources + tests

### Nouveaux (suite)

- `src/client/render/SkyPass.{h,cpp}` — pass Vulkan qui consomme les
  shaders `sky.frag` / `sky.vert` existants (jusqu'ici non wirés). Pattern
  aligné sur `LightingPass` / `WaterPass` / `AuthLogoPass`.
