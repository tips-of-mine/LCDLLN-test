# Système de Personnages — Factions / Races / Classes / Stats

**Date** : 2026-06-08
**Type** : migration + extension (pas une création depuis zéro)
**Projet** : LCDLLN (C++20 / Vulkan — client Windows, serveur Linux `masterd`+`shardd`, `shared`)
**Langue** : communication FR, code et symboles EN.

---

## 1. Objectif

Faire converger le système de personnages du code vers le design de référence
(classeur `LCDLLN_Faction_Races_Classes.xlsx`, non présent dans le repo — ses
valeurs numériques sont reportées ci-dessous) :

- **9 factions jouables**, chacune avec ses **classes 100 % distinctes** (pas de
  classes génériques partagées),
- **5 races actives**, 2 désactivées, 1 supprimée,
- un **modèle de stats à multiplicateurs** (11 stats dérivées) **calculé côté
  serveur**, niveaux 1 → 100,
- une **nouvelle courbe d'XP** (`base × N^2.6`, cap 100).

Les valeurs vivent dans `game/data/` mais sont **embarquées dans le binaire
`shardd` au build** (anti-triche).

---

## 2. État réel du code (constats d'audit)

Ces constats reformulent le ticket d'origine — certaines de ses hypothèses sont
inexactes.

1. **Le serveur ne calcule aujourd'hui AUCUNE stat.** `Unit`
   (`src/shardd/entities/Unit.h`) ne porte que
   `health/maxHealth/mana/maxMana/level/faction` en `UpdateField`. Il n'existe
   pas de moteur de stats serveur ni de lecture serveur de
   `races.json`/`classes.json` (ces JSON sont lus **uniquement par le client**,
   `src/client/character_creation/CharacterCreationUi.h`). Le modèle à
   multiplicateurs est donc une **création**. La table `characters` stocke
   `level=1` en dur + l'identité (race/class/faction/gender) ; **aucune stat
   persistée**.

2. **Une taxonomie de factions existe déjà en DB et diverge du ticket.** La
   migration `sql/migrations/0040_factions.sql` définit **7 factions** :
   `chevaliers_lumiere`, `chevaliers_justice`, `lune_noire`, `empire_hynn`,
   `dzorak`, `demons`, `chevaliers_dragons` (cette dernière race-lockée sur une
   pseudo-race `chevaliers_dragons`). Le §6.4 du ticket demande **9 factions**
   dont **Maison du Serpent**, **Faction Naine**, **Faction Elfe** (absentes), et
   pose **Chevaliers-Dragons = race Humain**.

3. **Deux chemins de réplication coexistent.** Le modèle `Unit`+`UpdateField`/
   `UpdateMask` (visé par le ticket) **et** un chemin snapshot
   `EntityState`/`StatsComponent` UDP (`src/shared/network/ReplicationTypes.h`)
   qui ne porte que `currentHealth/maxHealth`. Risque à lever (cf. §8).

4. **Deux arbres de migrations SQL** divergents existent : `sql/migrations/` et
   `deploy/docker/sql/migrations/`. Toute nouvelle migration doit être posée dans
   **les deux**.

---

## 3. Décisions arbitrées avec le porteur du projet

| # | Décision |
|---|----------|
| D1 | **Approche stats = recalcul déterministe (A)**, pas de persistance DB des stats. |
| D2 | **Découpage en 2 PR server-first** : PR1 serveur/données, PR2 wire+client. Merge lock-step. |
| D3 | **Factions** : 10 en table, **9 jouables** (= §6.4). `empire_hynn` conservée mais **non jouable** (`playable=false`, sans classes, masquée). `chevaliers_dragons` repasse en `race_lock = humains`. Ajout `maison_serpent`, `faction_naine`, `faction_elfe`. |
| D4 | L'ancienne faction `demons` **garde son id technique** ; seul son `display_name` devient « Légion infernale » (préserve backfill + persos existants). |
| D5 | **Moteur de stats + table embarquée = shardd uniquement** (jamais dans `shared`, sinon fuite des multiplicateurs au client). |
| D6 | `XpToNextLevel(level)` renvoie **0** si `level == 0` ou `level >= 100` (cap). La ligne §10 « `XpToNextLevel(100) > 0` » est traitée comme une coquille (99). |
| D7 | **Pas de build local** : la CI GitHub (Linux serveur + Windows client) valide tout. |

### Pourquoi A est le choix le plus sûr anti-triche

La protection vient de **l'autorité serveur**, pas du lieu de stockage. Dans A et
B le serveur fait autorité et le client ne reçoit que les valeurs finales. A est
**plus** sûre car :
- les multiplicateurs sont **gravés dans le binaire shardd** (ni disque, ni DB),
- **aucune colonne de stats modifiable** n'existe (rien à corrompre),
- une stat ne peut jamais être « fausse » : elle est toujours redérivée de la
  formule (le cap crit ≤ 10 est garanti *par la formule*).

L'identité et la progression (faction, classe, sexe, niveau, XP, position,
apparence, inventaire) restent **toujours stockées en DB** → changer
d'ordinateur ne perd rien (les stats sont re-déduites au login).

---

## 4. Modèle de données

### 4.1 `game/data/races/races.json` (migré)

- Suppression de `corrompus` (+ `configuration/races/corrompus.json` + thème
  `ui/races/corrompus/`).
- Ajout d'un champ `"enabled"` par race :
  `humains/elfes/orcs/nains/demons` → `true` ;
  `morts_vivants/divins` → `false` (présentes, non sélectionnables, **assets
  conservés**).
- `id: "orcs"` conservé (ne pas casser meshs/thèmes).

### 4.2 `game/data/races/factions.json` (NOUVEAU)

Porte la taxonomie faction → classes (abandon de `classes.json` +
`allowedRaces`). Chaque classe = (faction, race fixe via la faction, profil de
stats parmi les 8 archétypes, ressource secondaire). Les sous-classes sont des
**classes à part entière** (id distinct).

```json
{
  "schema_version": 1,
  "factions": [
    {
      "id": "chevaliers_lumiere", "displayName": "Chevaliers de la Lumière",
      "race": "humains", "playable": true,
      "classes": [
        { "id": "guerrier",                "displayName": "Guerrier",                 "statProfile": "melee",    "secondaryResource": "endurance" },
        { "id": "archer",                  "displayName": "Archer",                   "statProfile": "distance", "secondaryResource": "souffle"   },
        { "id": "voleur",                  "displayName": "Voleur",                   "statProfile": "voleur",   "secondaryResource": "reflexes"  },
        { "id": "inquisiteur_chatieur",    "displayName": "Inquisiteur · Châtieur",   "statProfile": "melee",    "secondaryResource": "ferveur"   },
        { "id": "inquisiteur_hospitalier", "displayName": "Inquisiteur · Hospitalier","statProfile": "healer",   "secondaryResource": "ferveur"   }
      ]
    }
  ]
}
```

**Les 9 factions jouables et leurs classes** (§6.4, intitulés exacts du classeur) :

1. **Chevaliers de la Lumière** (humains) : Guerrier(melee,Endurance),
   Archer(distance,Souffle), Voleur(voleur,Réflexes),
   Inquisiteur·Châtieur(melee,Ferveur), Inquisiteur·Hospitalier(healer,Ferveur).
2. **Chevaliers de la Justice** (humains) : Guerrier(melee,Endurance),
   Archer(distance,Souffle), Paladin(sacre,Ferveur),
   Prêtre·du Jugement(lanceur,Ferveur), Prêtre·de la Grâce(healer,Ferveur).
3. **La Lune Noire** (humains) : Guerrier(melee,Endurance),
   Arbalétrier(distance,Souffle), Prêtre de la Lune Noire(lanceur,Dévotion),
   Menthats(lanceur,Magie).
4. **Dzorak** (orcs) : Guerrier(melee,Endurance), Chaman(lanceur,Transe),
   Archer(distance,Souffle), Pisteur(pisteur,Instinct).
5. **Légion infernale** (demons) : Guerrier(melee,Endurance),
   Démoniste(lanceur,Corruption), Tourmenteur(melee,Corruption),
   Sorcier de sang(lanceur,Corruption).
6. **Chevaliers-Dragons** (humains) : Guerrier(melee,Endurance),
   Archimage(lanceur,Flamme draconique), Dragonnier(melee,Furie draconique),
   Gardien d'écailles(tank,Écaille).
7. **Maison du Serpent** (humains) : Guerrier(melee,Endurance),
   Archimage(lanceur,Magie de base), Assassin(voleur,Réflexes),
   Mage(lanceur,Magie de base).
8. **Faction Naine** (nains) : Guerrier(melee,Endurance),
   Pisteur(pisteur,Instinct), Mage(lanceur,Magie de base),
   Brise-roc(tank,Endurance).
9. **Faction Elfe** (elfes) : Guerrier(melee,Endurance),
   Archer·Bois(distance,Souffle), Voleur·Ténébreux(voleur,Réflexes),
   Mage(lanceur,Magie de base).

Plus **`empire_hynn`** : `playable=false`, sans classes (réservée lore).

> Les classes existantes (`warrior`/`mage`/`rogue`/`priest`/`paladin`/`hunter`/
> `necromancer`/`shaman`) sont remplacées par cette structure. `necromancer`
> disparaît. Réutiliser `abilities`/`displayName` pertinents en les rattachant
> aux nouvelles classes équivalentes.

### 4.3 `game/data/gameplay/character_stats.json` (NOUVEAU — embarqué shardd)

`schema_version`, `level_max: 100`, bases lvl1→lvl100 par stat, 8 profils
d'archétype, multiplicateurs de race, profils de sexe, cap crit (10), params XP.

**Bases** (lvl1 → lvl100) : PV 100→4000 · ressource 50→2000 · dégâts 10→400 ·
précision 70→95 · portée 20→45 · crit 2→10 (cap 10) · mult crit base 1.5 ·
marche 2 · course 5 · sprint 8 · endurance 100→1000 · perception 10 (+0.5/niv).

**Profils d'archétype** (ordre hp/resource/damage/accuracy/range/crit_rate/
crit_mult/speed/perception/stealth) :

| profil   | hp | res | dmg | acc | rng | crit | cmul | spd | perc | stlth |
|----------|----|-----|-----|-----|-----|------|------|-----|------|-------|
| tank     |1.30|0.80 |0.70 |1.00 |0.00 |0.40  |0.90  |0.85 |0.80  |0.40   |
| melee    |1.15|1.00 |1.05 |1.00 |0.00 |0.70  |1.00  |1.00 |0.90  |0.70   |
| sacre    |1.10|1.00 |1.00 |1.00 |0.00 |0.70  |1.00  |1.00 |0.95  |0.70   |
| distance |0.90|1.10 |1.15 |1.00 |1.00 |0.85  |1.05  |1.00 |1.30  |0.90   |
| pisteur  |0.95|1.10 |1.10 |1.00 |0.90 |0.80  |1.05  |1.10 |1.25  |1.20   |
| voleur   |0.90|1.10 |1.25 |1.00 |0.30 |1.00  |1.20  |1.15 |1.00  |1.40   |
| healer   |0.85|1.20 |0.65 |1.00 |0.70 |0.40  |0.90  |1.00 |1.00  |0.70   |
| lanceur  |0.75|1.30 |1.30 |1.00 |1.10 |0.70  |1.20  |1.00 |1.15  |0.60   |

**Multiplicateurs de race** (hp/resource/damage/accuracy/range/crit_rate/
crit_mult/speed_walk/speed_run/perception/stealth) :

| race    | hp | res | dmg | acc | rng | crit | cmul | walk | run | perc | stlth |
|---------|----|-----|-----|-----|-----|------|------|------|-----|------|-------|
| humains |1.00|1.00 |1.00 |1.00 |1.00 |1.00  |1.00  |1.00  |1.00 |1.00  |1.00   |
| nains   |1.20|1.00 |1.00 |1.00 |0.95 |0.90  |1.05  |0.85  |0.80 |0.90  |0.80   |
| orcs    |1.15|0.90 |1.20 |0.95 |0.95 |0.95  |1.10  |1.00  |1.00 |0.90  |0.85   |
| elfes   |0.90|1.10 |0.95 |1.10 |1.15 |1.10  |0.95  |1.05  |1.15 |1.25  |1.20   |
| demons  |1.05|1.20 |1.10 |1.00 |1.00 |1.00  |1.10  |1.00  |1.00 |1.05  |0.90   |

**Sexe** : multiplicateur par profil de classe, point fort/faible en miroir
(homme/femme). Taux de crit **neutre** au sexe ; multiplicateur de crit
légèrement favorable à l'homme.

> **Dépendance de données (R3)** : les **valeurs numériques exactes** des
> multiplicateurs de sexe ne figurent ni dans le ticket ni dans le repo (feuille
> « Paramètres » du classeur). Sans elles, les échantillons §9 (« Guerrier Nain
> H 60 » etc.) ne peuvent être comparés à des valeurs « du classeur ». Deux
> options à trancher avant PR1 : (a) le porteur fournit les valeurs sexe ; ou
> (b) on fige des valeurs sexe par défaut dans `character_stats.json` (miroir
> symétrique léger, ex. ±5 % sur la stat forte/faible du profil) et les tests
> deviennent **auto-cohérents** (assertion contre la table embarquée, pas contre
> le classeur). Reco : (b) pour ne pas bloquer, avec ajustement ultérieur si le
> classeur diffère.

**Endurance (jauge universelle)** : 3 vitesses (marche gratuite / course /
sprint), récup 3 paliers (t1 4%/s, t2 7%/s, t3 10%/s du max), immobile ×1.5,
monture = 0, jauge vide = plus de course. Les **valeurs** vivent dans le JSON ;
le runtime (vidage/remplissage) est géré serveur (cf. §8 pour le périmètre).

### 4.4 Dépréciation `game/data/races/classes.json`

Remplacé par `factions.json`. Le présenter client et les tests qui le lisent
sont migrés.

---

## 5. Moteur de stats serveur (shardd uniquement)

Emplacement : `src/shardd/gameplay/character/CharacterStatsEngine.{h,cpp}`
(compilé **uniquement** dans `shardd`).

**Calcul déterministe** pour `(level N, classId, factionId, gender)` :

```
base(N)   = base_lvl1 + (N-1) * (base_lvl100 - base_lvl1) / (level_max - 1)
valeur    = round( base(N) * mult_profil[stat] * mult_race[stat]
                            * mult_sexe[profil].get(stat, 1.0) )
crit_rate = MIN(10, base_crit(N) * mults)        // plafond garanti par formule
```

La **faction** donne la race (race fixe) ; la **classe** donne le profil
d'archétype + la ressource secondaire.

**Embarquement (CMake)** : une cible custom convertit `character_stats.json` +
la partie « calcul » de `factions.json` (faction→race, classe→profil/ressource)
en **header C++ `const` généré** (octets → tableau), régénéré si le JSON change.
Pas de chemin absolu. Au boot, `shardd` parse la table depuis la donnée
**embarquée** (jamais le disque) une fois en mémoire. Format additif,
`schema_version` versionné.

---

## 6. Courbe d'XP

`XP(N→N+1) = base × N^2.6`, `level_max = 100`.

**Calibration** : seul le ratio `base / xp_per_hour` fixe la durée. On fige
`xp_per_hour` (référence classeur) dans `character_stats.json`, puis on dérive
`base` pour que `Σ(N=1..59) base·N^2.6 / xp_per_hour ≈ 420 h`.

**Découpage** : `src/shared/formulas/Formulas.h` garde une fonction **pure et
paramétrée** `XpToNextLevel(level, base, factor, levelMax)` (forme de courbe,
testable, modifiable). Les **valeurs calibrées** viennent du JSON embarqué
shardd ; le moteur les passe à la fonction. Le client n'en a pas besoin (le
serveur réplique `xp` + `level`). `XpToNextLevel` renvoie 0 si `level == 0` ou
`level >= 100`.

---

## 7. Extension `Unit` / `Player` (réplication)

**Nouveaux `UpdateField` sur `Unit`** — indices *appended* à la fin de
`UnitFieldIdx` (ordre stable, le wire en dépend ; ne jamais réassigner un
indice existant) :

`damage`, `accuracy`, `range`, `crit_rate`, `crit_mult`, `speed_walk`,
`speed_run`, `speed_sprint`, `stamina`, `maxStamina`, `perception`, `stealth`,
`secondaryResource`, `maxSecondaryResource`.

- Entiers → `UpdateField<uint32_t>` ; fractionnaires (crit_mult, vitesses,
  perception, accuracy, range, crit_rate, stealth) → `UpdateField<float>`.
- `health/mana/level/faction` conservés.
- **Ressource secondaire** : on réplique seulement valeur + max. Le **libellé**
  est résolu côté client depuis `factions.json` via la classe connue (pas de
  champ wire pour le libellé).

**`Player`** : méthode `ApplyDerivedStats(engine, classId, factionId, gender)`
appelée après chargement DB et à chaque level-up — remplit les `UpdateField`,
`MarkDirty()` pour la première réplication.

---

## 8. Risques & périmètre

- **R1 — Chemin de réplication (BLOQUANT pour le plan)** : confirmer que les
  stats joueur atteignent le client via `UpdateField`/`UpdateMask` et **pas** via
  le snapshot `EntityState`/`StatsComponent` UDP. Si c'est le snapshot, étendre
  aussi `SpawnEntity`/`EntityState` — sinon les `UpdateField` resteront
  invisibles. À lever en début de plan.
- **R2 — Runtime endurance** : ce design fournit les **données** (max stamina,
  paliers de régén) et les **champs** répliqués. La boucle de vidage/remplissage
  (sprint qui consomme, régén par palier, immobile ×1.5, monture=0) est une
  addition gameplay ; à confirmer si elle entre dans PR1 ou en suivi.

### Hors scope (tickets séparés)

Déplacements spéciaux (téléport/vol/dragon), montures, gain d'XP/quêtes,
économie, craft, armure/défense, résistances, relations de factions (PvP),
statut hors-la-loi (stats inchangées).

---

## 9. Tests

- `FormulasTests` : `XpToNextLevel` paramétrée — monotone 1..99, `= 0` à 100,
  niveau 60 cumulé ≈ 420 h (tolérance), positivité.
- **`CharacterStatsEngineTests`** (shardd, ctest Linux) : échantillons §10
  (Guerrier Nain H 60, Lanceur Démon F 100, Voleur Elfe F 1) vs valeurs
  attendues ; cap crit ≤ 10 avec multiplicateurs forcés ; mêlée → portée nulle
  (mult range 0.00) ; round-trip JSON → header embarqué → mémoire.
- `UnitTests`/`PlayerTests` : nouveaux setters/getters + `MarkDirty`.
- `RaceDefinitionTests` : `corrompus` absente, `morts_vivants`/`divins`
  `enabled=false`, 5 actives ; tests meshPath conservés.
- `CharacterCustomizationTests` : retrait références `corrompus`.
- **Nouveau test factions client** : 9 factions jouables chargées, classes
  distinctes, `empire_hynn` non jouable.
- CI : `build-linux` exécute `ctest` (vérifier que les nouveaux tests serveur ne
  tombent pas dans les exclusions `-E`) ; `build-windows` pour le client.

---

## 10. Découpage en PR & déploiement

### PR1 — server-first (CI Linux)

`races.json` migré · `factions.json` · `character_stats.json` · migration
`0072_factions_v2.sql` (dans **`sql/migrations/` ET `deploy/docker/sql/
migrations/`**) · `CharacterStatsEngine` + header généré + cible CMake ·
`Formulas.h` (XP) · extension `Unit`/`Player` + `UpdateFieldIndices.h` · tests
serveur. **Autonome** : le serveur calcule les stats depuis l'identité déjà
stockée, sans toucher au wire.

> **Déploiement PR1** : ⚠️ **redéploiement serveur requis** — migration DB 0072
> + nouveau binaire `shardd` (moteur de stats + champs de réplication).

### PR2 — client (CI Windows + master)

Wire (`factionId` dans `CharacterCreateRequestPayload` ; `faction_str` dans
`CharacterListEntry`) · `CharacterCreateHandler` (masterd : validation
faction/classe/race depuis payload) · refonte `CharacterCreationPresenter`/
`CharacterCreationUi`/`AuthImGuiCharacterCreate.cpp` (flux faction → classe →
sexe ; races désactivées + `empire_hynn` masquées) · tests client.

> **Déploiement PR2** : ⚠️ **redéploiement serveur (master) requis** —
> wire-breaking (nouveau champ payload) + handler master modifié ;
> **client + master en lock-step**.

### Ordre

Merge **PR1 → PR2**, CI verte aux deux, déploiement lock-step.

---

## 11. Critères d'acceptation

- `races.json` : 5 actives, `morts_vivants`+`divins` `enabled:false`, `corrompus`
  supprimée partout (aucune référence résiduelle).
- 9 factions jouables avec classes 100 % distinctes, chargées ; `empire_hynn`
  présente mais non jouable.
- 11 stats calculées correctement (base × profil × race × sexe).
- `Formulas.h` : courbe `base × N^2.6`, niveau max 100, niveau 60 ≈ 420 h ;
  `FormulasTests` à jour.
- Crit de base jamais > 10 quel que soit le réglage.
- Mêlée pure : portée nulle.
- JSON embarqué au build : modifier + rebuild change les stats ; aucun accès
  disque runtime ; client sans multiplicateurs bruts.
- Tous les tests existants impactés au vert. CI Linux + Windows vertes.
