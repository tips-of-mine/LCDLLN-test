# Compétences par-classe — SP-A : pipeline de contenu + modèle de données — Design

> Statut : **validé utilisateur** (brainstorming 2026-06-15).
> Premier sous-projet d'un système plus large (voir §11).

## 1. Contexte

L'utilisateur a fourni une donnée de référence externe (`lune-noire-data_41_.json`,
6,3 Mo) contenant, entre autres, des **arbres de compétences complets** :
**27 classes × 180 compétences** (3 branches `single`/`aoe`/`def` × 60 paliers).
Le jeu n'a **pas** ce contenu : il a 8 **kits par profil** de 3-4 sorts (Combat
SP3, `gameplay/spells/<profil>.json`), partagés par toutes les classes d'un même
profil (ex. tous les casters partagent le kit `lanceur`).

> ⚠️ **Distinct du bug en cours** : le Grimoire vide d'un prêtre testé en jeu vient
> d'un `profileId` vide (déploiement shardd / résolution), **pas** d'un manque de
> contenu. Ce chantier est une **enrichissement** séparé.

L'XP, la vie et le `stat_model` de la référence sont **déjà** dans le jeu
(`character_stats.json`, dérivé de cette même source en #861) — rien à importer
de ce côté. Le **seul contenu nouveau** = les arbres de compétences.

### Analyse des mécaniques (4860 skills)

Structurellement simples : `power_kind` ∈ {`damage` (3000), `defense` (1620),
`heal` (240)} ; `target_type` ∈ {`unitaire`, `zone`, `soi_allie`} ;
`power_value` = multiplicateur (1.0→4.5) ; `range_m`/`radius_m`/`range_label`.
Les « stun/root/bouclier/invocation/brûlure… » ne sont **que du texte**
(`description`/`effects`), jamais des mécaniques structurées (pas de champ
tick/durée/absorb). La référence n'a **ni `cost` réel** (toujours 1), **ni
`cooldown`, ni `castTime`**.

## 2. Décomposition du système complet

Le système complet se découpe en 4 sous-projets (server-first) :
- **SP-A (ce spec)** — pipeline de contenu + modèle de données + petite extension
  moteur. Fondation, autonome et testable.
- **SP-B** — wire `classId` au client + set de skills connus par perso
  (serveur-autoritaire, persisté) + progression au level-up (1 choix parmi 3 par
  palier 1-60).
- **SP-C** — cast par set-connu : basculer le cast SP3 + la barre d'action sur les
  compétences par-classe validées contre le set connu (au lieu du kit profil).
- **SP-D** — UI arbre de compétences (voir les 3 branches × 60 paliers, choisir le
  déblocage par niveau) ; le Grimoire (déjà livré) affiche les skills connus.

Ce spec ne couvre que **SP-A**.

## 3. Objectif (SP-A)

Produire, pour les **24 classes existantes du jeu**, des données de compétences
par-classe (180 skills/classe) au format du jeu, chargées par un catalogue
serveur strict et un catalogue client tolérant, avec l'extension moteur minimale
requise pour que tous les skills soient mécaniquement représentables.

## 4. Hors périmètre (non-goals)

- **Progression / set connu / wire classId** → SP-B.
- **Cast réel par-classe / barre d'action** → SP-C (SP-A ne fait que charger les
  données ; le cast reste sur les kits profil jusqu'à SP-C).
- **UI arbre** → SP-D.
- **Fidélité au flavor** : les mécaniques sont les 3 `power_kind` structurés ; le
  texte (brûlure, bouclier, stun…) reste en **description** (cosmétique).
- **Classes/factions/races de la référence absentes du jeu** (Barde, Chevalier
  Errant, Seigneur, etc.) : **non importées**.

## 5. Décisions validées

| Sujet | Décision |
|---|---|
| Fidélité | **Données structurées** (damage/heal/defense + target + power_value + portée) ; flavor en description |
| Périmètre classes | **24 classes existantes** uniquement (mapping §6) |
| Génération | **Générateur déterministe** (PowerShell) lisant la référence **externe** → fichiers data **commités** |
| Référence | Reste **externe** (non versionnée — 6,3 Mo, classes non-jeu exclues) |
| Extension moteur | **1 seul** nouvel effet : `DamageReductionPercent` (défense) |

## 6. Mapping classe (jeu) → arbre (référence)

| Classe jeu | Profil | Arbre référence |
|---|---|---|
| guerrier | melee | class_warrior |
| archer | distance | class_archer |
| archer_bois | distance | class_archer |
| arbaletrier | distance | class_crossbowman |
| voleur | voleur | class_thief |
| voleur_tenebreux | voleur | class_thief |
| assassin | voleur | Assassin |
| mage | lanceur | class_mage |
| archimage | lanceur | Archimage |
| chaman | lanceur | class_shaman |
| paladin | sacre | Paladin |
| pisteur | pisteur | Pisteur |
| demoniste | lanceur | Démoniste |
| tourmenteur | melee | Tourmenteur |
| sorcier_sang | lanceur | Sorcier de sang |
| gardien_ecailles | tank | Gardien d'écailles |
| brise_roc | tank | Brise-roc |
| dragonnier | melee | Dragonnier |
| menthats | lanceur | menthats |
| pretre_lune_noire | lanceur | class_black_moon_priest |
| pretre_jugement | lanceur | Paladin |
| pretre_grace | healer | Prêtre |
| inquisiteur_hospitalier | healer | Inquisiteur |
| inquisiteur_chatieur | melee | Paladin |

Le mapping est une **table figée** dans le générateur. Classes partageant un même
arbre source (ex. archer/archer_bois, voleur/voleur_tenebreux, paladin/
pretre_jugement/inquisiteur_chatieur) produisent chacune **leur propre fichier**
(ids préfixés par la classe jeu) — la source de contenu est partagée, les données
émises sont distinctes.

## 7. Générateur déterministe

Un script **PowerShell** (`tools/skills/GenerateClassSkills.ps1`) :
- Entrée : chemin de la référence externe (paramètre) + la table de mapping §6 +
  les formules §9.
- Sortie : `game/data/gameplay/class_skills/<classId>.json` (24 fichiers), commités.
- Idempotent : relancer régénère à l'identique (pas d'horodatage dans la sortie).

> La référence n'est **pas** versionnée. Le générateur est commité ; les fichiers
> de sortie sont commités. Le générateur échoue explicitement si un arbre mappé est
> introuvable ou si un champ requis manque (politique stricte).

## 8. Schéma de sortie (par compétence)

`class_skills/<classId>.json` :
```json
{
  "classId": "pretre_grace",
  "sourceTree": "Prêtre",
  "skills": [
    {
      "id": "pretre_grace_single_t1",
      "name": "Soin de Lumière",
      "icon": "✨",
      "branch": "single",
      "tier": 1,
      "level": 1,
      "effectKind": "Heal",
      "target": "SingleAlly",
      "powerValue": 1.0,
      "rangeMeters": 6.0,
      "areaRadiusMeters": 0.0,
      "castTimeMs": 1000,
      "cooldownMs": 4000,
      "resourceCostPercent": 9,
      "description": "Soin de Lumière : restaure la vie d'un allié…"
    }
  ]
}
```

Conversions :
- `branch_id` → `branch` (`single`/`aoe`/`def`), `tier`/`level` recopiés.
- `power_kind` → `effectKind` : `damage→Damage`, `heal→Heal`, `defense→Defense`.
- `target_type` → `target` : `unitaire→SingleEnemy` (si Damage) /
  `SingleAlly` (si Heal) ; `zone→AreaAroundSelf` ; `soi_allie→SingleAlly`
  (Heal/Defense sur soi ou allié proche).
- `power_value→powerValue`, `range_m→rangeMeters`, `radius_m→areaRadiusMeters`.
- `description` (ou `effects` si plus riche) → `description`.
- `castTimeMs`/`cooldownMs`/`resourceCostPercent` : **synthétisés** (§9).

## 9. Formules de synthèse (déterministes, ajustables — points de départ d'équilibrage)

- **resourceCostPercent** = `clamp( baseBranch + round((powerValue - 1.0) * 4), 5, 60 )`
  avec `baseBranch` : single 6, aoe 10, def 8.
- **cooldownMs** = `baseBranch` : single 3000, aoe 10000, def 18000 (V1 : plat par
  branche, ajustable au tier ultérieurement).
- **castTimeMs** : `Damage` à distance (`rangeMeters > 5`) = 1500 ; `Damage` mêlée
  (`rangeMeters ≤ 5` ou `range_weapon_relative`) = 0 (instant) ; `Heal` = 1000 ;
  `Defense` = 0.

> Ces valeurs sont des **constantes nommées** dans le générateur (un seul endroit à
> régler). Elles seront affinées en équilibrage (hors SP-A).

## 10. Extension moteur

### 10.1 Nouvel effet `DamageReductionPercent`

- Ajouter `DamageReductionPercent` à `enum class SpellEffectType`
  (`src/shardd/gameplay/spell/SpellKitLibrary.h` — l'énum partagé des effets).
- Sémantique : aura sur **soi/allié** réduisant les dégâts subis de `percent` %
  pendant `durationMs`. Consommée dans le calcul de dégâts entrants
  (`ApplyAuraDamageModifiers` côté SP3 : `× (1 − Σ DamageReductionPercent/100)`,
  plancher 0).
- Mapping depuis `defense` : `percent = clamp( round(powerValue * 11), 5, 50 )`
  (t1 power 1.0 → 11 %, cap 50 %). `durationMs` synthétisé = 8000 (la référence
  n'en a pas). `tickPeriodMs` = 0 (pas de tick).

`Damage`→`DirectDamage` (+ AoE `AreaAroundSelf` via `areaRadiusMeters`) et
`Heal`→`DirectHeal` réutilisent les effets SP3 existants.

### 10.2 Catalogues

- **Serveur** : `ClassSkillLibrary` (`src/shardd/gameplay/spell/`, pattern strict
  `SpellKitLibrary`) : charge `class_skills/*.json`, expose
  `FindSkill(classId, skillId)` et `GetClassSkills(classId)`. Boot strict (fichier
  invalide = pas de boot).
- **Client** : `ClassSkillCatalog` (`src/client/gameplay/`, pattern tolérant
  `SpellKitCatalog`) : charge les mêmes fichiers, expose les métadonnées
  d'affichage par classId. Catalogue vide + LOG_WARN si illisible (non bloquant).

Ces catalogues **coexistent** avec les kits-profil (`SpellKitLibrary`/
`SpellKitCatalog`). SP-A ne bascule **pas** le cast (c'est SP-C) — il ne fait que
charger et exposer les données.

## 11. Tests

- **Générateur** : exécution sur la référence → 24 fichiers, 180 skills chacun ;
  bornes (effectKind ∈ {Damage,Heal,Defense}, target valide, powerValue ≥ 1.0,
  resourceCostPercent ∈ [5,60], mitigation ∈ [5,50]).
- **`ClassSkillLibrary`** (`class_skill_library_tests`, pattern
  `spell_kit_library_tests`) : kit valide (180 skills, branches, effets typés),
  fichier invalide rejeté, `DamageReductionPercent` accepté, `FindSkill` ok/nullptr.
- **Mapping d'effets** : un skill `defense` → `DamageReductionPercent` avec percent
  correct ; un `damage zone` → `DirectDamage` + `areaRadiusMeters`.
- CI : `build-linux` exécute ctest (ajouter les cibles aux listes CMake — serveur
  ET shard_app, cf. convention `SpellKitLibrary`).

## 12. Déploiement

- SP-A = **redéploiement shardd requis** (nouveau `ClassSkillLibrary` chargé au
  boot + nouvel effet moteur ; les données embarquées/lues changent). Le client
  charge aussi le nouveau catalogue (PR client). Mais SP-A **ne change pas le
  comportement de jeu** (cast toujours sur kits profil) → pas de wire-breaking, pas
  de lock-step strict : le catalogue est inerte tant que SP-C n'est pas livré.

## 13. Suite (hors SP-A)

- **SP-B** : `classId` au wire (extension `PlayerStatsMessage` ou nouveau kind) ;
  set connu persisté ; progression 1/3 par palier.
- **SP-C** : cast + barre d'action + Grimoire branchés sur le set connu par-classe.
- **SP-D** : UI arbre de compétences (choix des paliers).
