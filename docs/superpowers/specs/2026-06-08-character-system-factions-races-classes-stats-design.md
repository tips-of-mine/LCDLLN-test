# Système de Personnages — Factions / Races / Classes / Stats

**Date** : 2026-06-08 (révisé : intégration du ticket complet avec valeurs §6.3)
**Type** : migration + extension (pas une création depuis zéro)
**Projet** : LCDLLN (C++20 / Vulkan — client Windows, serveur Linux `masterd`+`shardd`, `shared`)
**Langue** : communication FR, code et symboles EN.
**Source de vérité chiffrée** : le bloc JSON du §6.3 du ticket (reproduit §4.4 ci-dessous). L'agent n'a pas accès au classeur `.xlsx` ; ce JSON EST la référence à recopier verbatim.

---

## 1. Objectif

Faire converger le système de personnages du code vers le design de référence :

- **9 factions jouables** (+ 1 conservée non sélectionnable), chacune avec ses
  **classes 100 % distinctes** (pas de classes génériques partagées),
- **5 races actives**, 2 désactivées, 1 supprimée,
- un **modèle de stats à multiplicateurs** (11 stats dérivées) **calculé côté
  serveur**, niveaux 1 → 100,
- une **nouvelle courbe d'XP** (`base × N^2.6`, cap 100),
- une **zone de texte descriptive** à la création (faction/classe), traduisible.

Les valeurs vivent dans `game/data/` mais les multiplicateurs sont **embarqués
dans le binaire `shardd` au build** (anti-triche).

---

## 2. État réel du code (constats d'audit)

1. **Le serveur ne calcule aujourd'hui AUCUNE stat.** `Unit`
   (`src/shardd/entities/Unit.h`) ne porte que
   `health/maxHealth/mana/maxMana/level/faction` en `UpdateField`. Pas de moteur
   de stats serveur ni de lecture serveur de `races.json`/`classes.json` (lus
   **uniquement par le client**). La table `characters` stocke `level=1` en dur +
   l'identité (race/class/faction/gender) ; **aucune stat persistée**.

2. **Une taxonomie de factions existe déjà en DB et diverge du ticket.**
   `sql/migrations/0040_factions.sql` définit 7 factions avec des **slugs longs**
   (`chevaliers_lumiere`, `chevaliers_justice`, `lune_noire`, `empire_hynn`,
   `dzorak`, `demons`, `chevaliers_dragons`). Le §6.3 utilise des **ids courts**
   (`lumiere`, `justice`, `lune_noire`, `dzorak`, `legion`, `dragons`, `serpent`,
   `naine`, `elfe`, `empire_hynn`). → réconciliation (D4).

3. **Deux chemins de réplication coexistent** : `Unit`+`UpdateField`/`UpdateMask`
   (visé par le ticket) **et** un chemin snapshot `EntityState`/`StatsComponent`
   UDP (`src/shared/network/ReplicationTypes.h`, `currentHealth/maxHealth` seuls).
   Risque R1 (§8).

4. **Deux arbres de migrations SQL** divergents : `sql/migrations/` et
   `deploy/docker/sql/migrations/`. Toute migration neuve va dans **les deux**.

5. **Le wire de création** (`CharacterCreateRequestPayload`,
   `src/shared/network/CharacterPayloads.h`) envoie aujourd'hui
   `name/raceId/classId/customization/gender`. Pas de `factionId`. Le handler
   `CharacterCreateHandler` (masterd) dérive la faction de la race via un lambda.

---

## 3. Décisions arbitrées avec le porteur du projet

| # | Décision |
|---|----------|
| D1 | **Approche stats = recalcul déterministe** sur `shardd`, pas de persistance DB des stats (§5.6 du ticket). |
| D2 | **Découpage en 2 PR server-first** : PR1 serveur/données, PR2 wire+client+localisation. Merge lock-step. *(Garde le 2-PR malgré le §13 « une branche, un PR ».)* |
| D3 | **Factions** : 10 en table, **9 sélectionnables**. `empire_hynn` conservée mais `selectable=false` (suzerain Lumière/Justice, PNJ/narration future). `chevaliers_dragons` → `race_lock = humains`. |
| D4 | **Ids faction alignés sur le JSON §6.3 (ids courts)** : migration 0072 **renomme** les slugs DB vers `lumiere/justice/lune_noire/dzorak/legion/dragons/empire_hynn` + **backfill** `characters.faction_str` des persos existants ; **ajoute** `serpent/naine/elfe`. `legion` remplace `demons`. *(Supersede l'ancienne D4 « garder l'id technique ».)* |
| D5 | **Moteur de stats + multiplicateurs embarqués = shardd uniquement** (jamais `shared`, sinon fuite client). |
| D6 | `XpToNextLevel(level)` renvoie **0** si `level == 0` ou `level >= 100`. La ligne §10 « `XpToNextLevel(100) > 0` » est traitée comme coquille (99). |
| D7 | **Pas de build local** : CI GitHub (Linux serveur + Windows client) valide tout. |
| D8 | **Séparation anti-triche en 2 fichiers** : `factions.json` (taxonomie lisible client, sans multiplicateurs) + `character_stats.json` (bases/multiplicateurs/sexe/xp, embarqué shardd-only). Valeurs recopiées verbatim du §6.3. |
| D9 | **Zone de texte descriptive** à la création (faction + classe), depuis `game/data/localization/<lang>/`, clés stables, traduisible (PR2). |

### Pourquoi le recalcul déterministe est le choix le plus sûr anti-triche

La protection vient de **l'autorité serveur**, pas du lieu de stockage. Le
serveur calcule et détient les stats ; le client ne reçoit que les valeurs
finales. Le recalcul est **plus** sûr : multiplicateurs gravés dans le binaire
shardd (ni disque, ni DB), **aucune colonne de stats modifiable**, stat toujours
conforme à la formule (cap crit ≤ 10 garanti *par la formule*). L'identité et la
progression restent **toujours** en DB → changer d'ordinateur ne perd rien.

---

## 4. Modèle de données

### 4.1 `game/data/races/races.json` (migré)

- Suppression de `corrompus` (+ `configuration/races/corrompus.json` + thème
  `ui/races/corrompus/`).
- Ajout `"enabled"` : `humains/elfes/orcs/nains/demons` → `true` ;
  `morts_vivants/divins` → `false` (assets **conservés**).
- `id: "orcs"` conservé.

### 4.2 `game/data/races/factions.json` (NOUVEAU — taxonomie, lisible client)

Porte faction → classes, **sans aucun multiplicateur** (D8). Chaque classe = (id
unique dans la faction, nom, sous-classe optionnelle, nom de profil d'archétype,
clé de ressource secondaire). Ids alignés sur le §6.3.

```json
{
  "schema_version": 1,
  "factions": [
    {
      "id": "lumiere", "name": "Chevaliers de la Lumière",
      "race": "humains", "selectable": true,
      "classes": [
        { "id": "guerrier",                "name": "Guerrier",     "profile": "melee",    "resource": "endurance" },
        { "id": "archer",                  "name": "Archer",       "profile": "distance", "resource": "souffle"   },
        { "id": "voleur",                  "name": "Voleur",       "profile": "voleur",   "resource": "reflexes"  },
        { "id": "inquisiteur_chatieur",    "name": "Inquisiteur", "subclass": "Châtieur",    "profile": "melee",  "resource": "ferveur" },
        { "id": "inquisiteur_hospitalier", "name": "Inquisiteur", "subclass": "Hospitalier", "profile": "healer", "resource": "ferveur" }
      ]
    }
  ]
}
```

**Identité technique des classes (wire)** : `classId` = slug unique **dans** la
faction (ex. `guerrier`, `inquisiteur_chatieur`). Comme le `factionId` est aussi
envoyé, le couple `(factionId, classId)` est globalement non ambigu.

**Les 10 factions** (ids + classes, §6.4 / §6.3) :

| id | name | race | selectable | classes (id : profil/ressource) |
|----|------|------|-----------|---------------------------------|
| `lumiere` | Chevaliers de la Lumière | humains | ✅ | guerrier:melee/endurance · archer:distance/souffle · voleur:voleur/reflexes · inquisiteur_chatieur:melee/ferveur · inquisiteur_hospitalier:healer/ferveur |
| `justice` | Chevaliers de la Justice | humains | ✅ | guerrier:melee/endurance · archer:distance/souffle · paladin:sacre/ferveur · pretre_jugement:lanceur/ferveur · pretre_grace:healer/ferveur |
| `lune_noire` | La Lune Noire | humains | ✅ | guerrier:melee/endurance · arbaletrier:distance/souffle · pretre_lune_noire:lanceur/devotion · menthats:lanceur/magie |
| `dzorak` | Dzorak | orcs | ✅ | guerrier:melee/endurance · chaman:lanceur/transe · archer:distance/souffle · pisteur:pisteur/instinct |
| `legion` | Légion infernale | demons | ✅ | guerrier:melee/endurance · demoniste:lanceur/corruption · tourmenteur:melee/corruption · sorcier_sang:lanceur/corruption |
| `dragons` | Chevaliers-Dragons | humains | ✅ | guerrier:melee/endurance · archimage:lanceur/flamme_draconique · dragonnier:melee/furie_draconique · gardien_ecailles:tank/ecaille |
| `serpent` | Maison du Serpent | humains | ✅ | guerrier:melee/endurance · archimage:lanceur/magie_base · assassin:voleur/reflexes · mage:lanceur/magie_base |
| `naine` | Faction Naine | nains | ✅ | guerrier:melee/endurance · pisteur:pisteur/instinct · mage:lanceur/magie_base · brise_roc:tank/endurance |
| `elfe` | Faction Elfe | elfes | ✅ | guerrier:melee/endurance · archer_bois:distance/souffle · voleur_tenebreux:voleur/reflexes · mage:lanceur/magie_base |
| `empire_hynn` | L'Empire de L'hynn | humains | ❌ | (aucune) |

> Les classes existantes (`warrior`/`mage`/`rogue`/`priest`/`paladin`/`hunter`/
> `necromancer`/`shaman`) sont remplacées par cette structure. `necromancer`
> disparaît. Réutiliser `abilities`/`displayName` pertinents.

### 4.3 Dépréciation `game/data/races/classes.json`

Remplacé par `factions.json`. Présenter client + tests migrés.

### 4.4 `game/data/gameplay/character_stats.json` (NOUVEAU — embarqué shardd-only)

Contient **uniquement** les valeurs (bases, profils de classe, profils de race,
profils de sexe, params XP, cap crit) — **pas** les noms factions/classes (ceux-ci
sont dans `factions.json`, D8). Le lien se fait par le nom de profil
(`class_profiles[profil]`) et la race (`race_profiles[race]`).

Recopier verbatim depuis le §6.3 du ticket. Valeurs clés :

- **XP** : `factor: 2.6`, `base: 6.185`, `xp_per_hour_ref: 10000`, calib niveau 60 ≈ 420 h.
- **Bases (lvl1→lvl100)** : hp 100→4000 · resource 50→2000 · damage 10→400 ·
  accuracy 70→95 · range 20→45 · crit_rate 2→10 (cap 10) · crit_mult base 1.5 ·
  speed_walk 2 · speed_run 5 · speed_sprint 8 · stamina 100→1000 ·
  perception_lvl1 10 (+0.5/niv).
- **Stamina** : cost_run 4 % / cost_sprint 8 % ; régén t1 4 % / t2 7 % / t3 10 % ;
  idle ×1.5.
- **8 profils d'archétype** `class_profiles` (tank/melee/sacre/distance/pisteur/
  voleur/healer/lanceur), ordre hp,resource,damage,accuracy,range,crit_rate,
  crit_mult,speed,perception,stealth.
- **5 profils de race** `race_profiles` (humains/nains/orcs/elfes/demons).
- **8 profils de sexe** `sex_profiles` (un par profil de classe, sous-clés `H`/`F`),
  crit_rate toujours neutre, multiplicateur de crit légèrement pro-homme.

> Les tableaux numériques complets (class_profiles, race_profiles, sex_profiles)
> sont ceux du §6.3 du ticket — copie octet pour octet, sans réinterprétation.

### 4.5 Textes descriptifs (localisation, D9 / §5.5)

Dans `game/data/localization/<lang>/` : clés stables `faction.<id>.desc` (ex.
`faction.lumiere.desc`) et `class.<factionId>.<classId>.desc` (ex.
`class.lumiere.guerrier.desc`). Traduisibles. L'UI les affiche dans une zone
dédiée mise à jour selon la sélection (faction → classe → sous-classe). Pas de
texte en dur dans le code.

---

## 5. Moteur de stats serveur (shardd uniquement)

Emplacement : `src/shardd/gameplay/character/CharacterStatsEngine.{h,cpp}`
(compilé **uniquement** dans `shardd`).

**Calcul déterministe** pour `(level N, classId, factionId, gender)` :

```
base(N)    = base_lvl1 + (N-1) * (base_lvl100 - base_lvl1) / (level_max - 1)
valeur     = round( base(N) * mult_profil[stat] * mult_race[stat]
                             * mult_sexe[profil].get(stat, 1.0) )   // mult absent => 1.0
crit_rate  = MIN(10, base_crit(N) * mults)            // cap garanti par formule
xp_next(N) = round( xp.base * N^xp.factor )
```

**Règles dérivées** (§6.3) :
- `class_profiles[profil].range == 0` → mêlée pure → **précision/portée nulles**.
- Vitesses : marche = `speed_walk` (classe×race×sexe) ; course/sprint = `speed_run`.
- Discrétion = `(perception_base/2) / (stealth_classe × stealth_race × stealth_sexe)`.
- Endurance = jauge universelle (coûts/régén proportionnels au max).

La **faction** donne la race ; la **classe** donne le profil + la ressource.

**Embarquement (CMake)** : cible custom convertissant `character_stats.json` en
**header C++ `const` généré** (octets → tableau), régénéré si le JSON change.
Pas de chemin absolu. Au boot, `shardd` parse la table **embarquée** (jamais le
disque) une fois en mémoire. `factions.json` (taxonomie) est aussi nécessaire au
serveur pour le mapping faction→race et classe→profil/ressource → embarqué de la
même façon (sa partie « calcul »), ou lu côté serveur ; **les multiplicateurs ne
sont JAMAIS dans `factions.json`** (lisible client).

---

## 6. Courbe d'XP

`XP(N→N+1) = round(6.185 × N^2.6)`, `level_max = 100`. Params dans
`character_stats.json` (embarqué), pas en dur.

`src/shared/formulas/Formulas.h` garde une fonction **pure et paramétrée**
`XpToNextLevel(level, base, factor, levelMax)` (forme testable, modifiable). Les
valeurs calibrées viennent du JSON embarqué shardd ; le moteur les passe à la
fonction. `XpToNextLevel` renvoie 0 si `level == 0` ou `level >= 100` (D6). Le
client n'a pas besoin des params (serveur réplique `xp` + `level`).

---

## 7. Extension `Unit` / `Player` (réplication)

**Nouveaux `UpdateField` sur `Unit`** — indices *appended* en fin de
`UnitFieldIdx` (ordre stable ; ne jamais réassigner un indice existant) :

`damage`, `accuracy`, `range`, `crit_rate`, `crit_mult`, `speed_walk`,
`speed_run`, `speed_sprint`, `stamina`, `maxStamina`, `perception`, `stealth`,
`secondaryResource`, `maxSecondaryResource`.

- Entiers → `UpdateField<uint32_t>` ; fractionnaires (crit_mult, vitesses,
  perception, accuracy, range, crit_rate, stealth) → `UpdateField<float>`.
- `health/mana/level/faction` conservés.
- **Ressource secondaire** : on réplique valeur + max seulement ; le **libellé**
  est résolu côté client depuis `factions.json` via la classe connue.

**`Player`** : méthode `ApplyDerivedStats(engine, classId, factionId, gender)`
appelée après chargement DB et à chaque level-up — remplit les `UpdateField`,
`MarkDirty()` pour la première réplication.

---

## 8. Risques & périmètre

- **R1 — Chemin de réplication (BLOQUANT pour le plan)** : confirmer que les
  stats joueur atteignent le client via `UpdateField`/`UpdateMask` et **pas** via
  le snapshot `EntityState`/`StatsComponent` UDP. Si snapshot : étendre aussi
  `SpawnEntity`/`EntityState`. À lever en début de plan.
- **R2 — Runtime endurance** : ce design fournit les **données** (max, coûts,
  paliers de régén) et les **champs** répliqués. La boucle de
  consommation/régén (sprint, idle ×1.5, monture=0, jauge vide → plus de course)
  est une addition gameplay ; à confirmer PR1 vs suivi.
- **R3 — RÉSOLU** : les multiplicateurs de sexe sont fournis (`sex_profiles`,
  §6.3). Les échantillons §9 sont comparés à la table embarquée (déterministe).

### Hors scope (tickets séparés)

Déplacements spéciaux, montures, gain d'XP/quêtes, économie, craft,
armure/défense, résistances, relations de factions (PvP), statut hors-la-loi.

---

## 9. Tests

- `FormulasTests` : `XpToNextLevel` paramétrée — monotone 1..99, `= 0` à 100,
  niveau 60 cumulé ≈ 420 h (tolérance), positivité.
- **`CharacterStatsEngineTests`** (shardd, ctest Linux) : échantillons §10
  (Guerrier Nain H 60, Lanceur Démon F 100, Voleur Elfe F 1) vs valeurs
  calculées des tables ; cap crit ≤ 10 avec multiplicateurs forcés ; mêlée →
  portée/précision nulles (range profil 0.0) ; round-trip JSON → header embarqué
  → mémoire.
- `UnitTests`/`PlayerTests` : nouveaux setters/getters + `MarkDirty`.
- `RaceDefinitionTests` : `corrompus` absente, `morts_vivants`/`divins`
  `enabled=false`, 5 actives ; tests meshPath conservés.
- `CharacterCustomizationTests` : retrait références `corrompus`.
- **Nouveau test factions client** : 9 factions sélectionnables, classes
  distinctes, `empire_hynn` non sélectionnable, clés de localisation présentes.

---

## 10. Découpage en PR & déploiement

### PR1 — server-first (CI Linux)

`races.json` migré · `factions.json` · `character_stats.json` · migration
`0072_factions_v2.sql` (rename slugs → ids courts + backfill `faction_str` +
`race_lock` dragons=humains + ajout serpent/naine/elfe + empire_hynn
`selectable=false`, **dans `sql/migrations/` ET `deploy/docker/sql/migrations/`**)
· `CharacterStatsEngine` + header généré + cible CMake · `Formulas.h` (XP) ·
extension `Unit`/`Player` + `UpdateFieldIndices.h` · tests serveur. **Autonome** :
le serveur calcule les stats depuis l'identité déjà stockée, sans toucher au wire.

> **Déploiement PR1** : ⚠️ **redéploiement serveur requis** — migration DB 0072
> + nouveau binaire `shardd` (moteur de stats + champs réplication).

### PR2 — client (CI Windows + master)

Wire (`factionId` dans `CharacterCreateRequestPayload` ; `faction_str` dans
`CharacterListEntry`) · `CharacterCreateHandler` (masterd : validation
faction/classe/race depuis payload, stocke `faction_str=factionId`) · refonte
`CharacterCreationPresenter`/`CharacterCreationUi`/`AuthImGuiCharacterCreate.cpp`
(flux faction → classe → sexe ; masquer races désactivées + `empire_hynn` ; zone
de texte descriptive §5.5) · textes de localisation `faction.*`/`class.*` · tests
client.

> **Déploiement PR2** : ⚠️ **redéploiement serveur (master) requis** —
> wire-breaking (nouveau champ payload) + handler master modifié ;
> **client + master en lock-step**.

### Ordre

Merge **PR1 → PR2**, CI verte aux deux, déploiement lock-step.

---

## 11. Critères d'acceptation

- `races.json` : 5 actives, `morts_vivants`+`divins` `enabled:false`, `corrompus`
  supprimée partout.
- `empire_hynn` conservée dans les données mais `selectable:false`.
- 9 factions sélectionnables avec classes 100 % distinctes, chargées (ids courts).
- Zone de texte descriptive affichée à la création, MAJ selon faction/classe,
  textes issus de la localisation (traduisibles).
- Stats **NON** stockées en DB : `characters` ne contient que
  faction/classe/sous-classe/sexe/niveau/xp ; les 11 stats recalculées par `shardd`.
- 11 stats calculées correctement (base × profil × race × sexe), conformes au §6.3.
- `Formulas.h` : courbe `6.185 × N^2.6`, niveau max 100, niveau 60 ≈ 420 h.
- Crit de base jamais > 10.
- Mêlée pure : précision/portée nulles.
- JSON embarqué : modifier + rebuild change les stats ; aucun accès disque
  runtime ; client sans multiplicateurs bruts (`factions.json` seul côté client).
- Tous les tests impactés au vert. CI Linux + Windows vertes.
