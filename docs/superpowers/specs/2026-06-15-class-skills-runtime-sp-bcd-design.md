# Compétences par-classe — Runtime SP-B / SP-C / SP-D — Design

> Statut : design **autonome** (mandat utilisateur 2026-06-15 : finir SP-A→SP-D).
> Suite de SP-A (`2026-06-15-class-skills-pipeline-design.md`) qui a livré les
> données `class_skills/*.json` + `ClassSkillLibrary` (serveur) + `ClassSkillCatalog`
> (client), **inertes**. SP-B/C/D les rendent **vivants** en jeu.

## Vue d'ensemble & ordre

- **SP-B** — `classId` au client + **set de skills connus** persisté + **choix de
  progression** (1 parmi 3 par niveau 1-60). Fondation runtime.
- **SP-C** — **cast par set-connu** : la barre d'action + le Grimoire utilisent les
  compétences de classe connues (au lieu du kit profil) ; effets résolus serveur
  (ajout de l'effet `DamageReductionPercent`).
- **SP-D** — **UI arbre de compétences** : voir les 3 branches × 60 paliers, choisir
  le déblocage à chaque niveau ; le Grimoire affiche les skills connus.

Ordre d'implémentation : **SP-B → SP-D → SP-C** (la UI de choix SP-D précède le cast
SP-C, sinon le set connu reste vide et le cast n'a rien à lancer). Chacun : 1 PR,
poussé, CI verte, corrigé si rouge.

## Décisions transverses

- **Wire rétro-additif** (pas de bump `kProtocolVersion`) : nouveaux kinds en queue.
- **`classId`** transmis via un **nouveau kind** (pas en modifiant `PlayerStatsMessage`,
  pour rester rétro-additif).
- **Persistance** : `PersistedCharacterState.knownSkillIds` (`std::vector<std::string>`,
  format INI count+index comme `chatIgnore`). Serveur-autoritaire.
- **Modèle de progression** : à chaque niveau N ∈ [1,60], le perso choisit **1** skill
  parmi les 3 candidats (le skill de tier N de chaque branche single/aoe/def de sa
  classe). `knownSkillIds` contient au plus 1 skill par niveau atteint. Pas de respec
  en V1 (un niveau choisi est définitif — un `/respec` admin pourra venir plus tard).
- **Validation serveur** : tout choix/cast est validé contre `ClassSkillLibrary`
  (skill ∈ classe, tier ≤ niveau, unicité par niveau).

---

## SP-B — classId + set connu + progression

### Wire (`ServerProtocol`)
Nouveaux kinds (valeurs = prochaines libres après 89) :
- `ClassProgressionUpdate` (shard→client) : `{clientId u32, classId string, u16 count, knownSkillIds[count] string}`. Poussé à l'enter-world ET après chaque choix validé (état autoritaire complet, idempotent).
- `ChooseClassSkillRequest` (client→shard) : `{clientId u32, level u32, skillId string}`.

Encode/Decode + tests roundtrip/tronqué (pattern SP-A/Grimoire).

### Serveur (`ServerApp`)
- `ConnectedClient.knownSkillIds` (`std::vector<std::string>`).
- Load/save : `PersistedCharacterState.knownSkillIds` (INI `knownskill.count` + `knownskill.<i>`), recopie load (~l.1302) / save (~l.2017), comme `actionBarLayout`.
- `SendClassProgression(client)` : émet `ClassProgressionUpdate` (classId = `client.classId`, knownSkillIds). Appelé à l'enter-world (après `SendActionBarLayout`).
- `HandleChooseClassSkill(endpoint, msg)` : FindClient + clientId match ; résoudre la classe (`client.classId`) ; vérifier `level ∈ [1,60]`, `level ≤ client.level`, `skillId` ∈ candidats de ce tier pour la classe (via `m_classSkills.FindSkill(classId, skillId)` + le skill a `tier == level`), pas déjà un skill choisi pour ce `level` (unicité par niveau) → ajoute à `knownSkillIds`, `SaveConnectedClient(.,"skill_choice")`, `SendClassProgression`. Sinon : renvoie l'état inchangé.
- Membre `ClassSkillLibrary m_classSkills;` + `Init()` strict au boot (comme `m_spellKits`).
- ⚠️ ajouter `ClassSkillLibrary.cpp` au `shard_app` (déjà fait en SP-A) ; instancier + Init dans ServerApp.

### Client (`UIModel`)
- `UIModel.classId` (string) + `UIModel.knownSkillIds` (`std::vector<std::string>`).
- `ApplyClassProgressionUpdate` (dispatch du nouveau kind) → remplit classId + knownSkillIds, `NotifyObservers`.
- `GameplayUdpClient::SendChooseClassSkill(clientId, level, skillId)`.

### Tests
- Wire roundtrip (2 kinds). Logique de choix : skill hors-tier rejeté, doublon de niveau rejeté, choix valide ajouté (test serveur si une cible existe ; sinon couvert par revue + intégration).

### Déploiement : ⚠️ redéploiement shardd (nouveau handler + persistance) + client lock-step (la feature exige les deux).

---

## SP-D — UI arbre de compétences

### Client
- `ClassSkillTreeUiPresenter` (`src/client/skills/` ou `src/client/grimoire/`) : lit `ClassSkillCatalog.GetClassSkills(classId)` + `UIModel.knownSkillIds`. Construit, par branche, les 60 paliers ; pour chaque niveau ≤ `playerLevel`, marque : choisi (quel skill) / à choisir (3 candidats) ; niveaux > playerLevel = verrouillés.
- `ClassSkillTreeImGuiRenderer` (`src/client/render/`) : 3 colonnes (single/aoe/def) × paliers ; bouton « Choisir » sur les niveaux en attente → `SendChooseClassSkill(level, skillId)` (optimiste + réconcilié par `ClassProgressionUpdate`).
- **Grimoire** : sa liste de sorts passe des kits profil aux **skills connus** (filtre `ClassSkillCatalog` par `knownSkillIds`). Réutilise le panneau Grimoire existant (onglet « Arbre » + onglet « Connus »), ou un nouveau toggle.
- Keybind : réutiliser `controls.keybind.grimoire` (V) avec onglets, ou un bind dédié.

### Déploiement : client uniquement (consomme le wire SP-B).

---

## SP-C — cast par set-connu

### Serveur (`ServerApp` / combat SP3)
- Ajouter l'effet **`DamageReductionPercent`** à `SpellEffectType` (SpellKitLibrary.h) + sa consommation dans `ApplyAuraDamageModifiers` (réduit les dégâts entrants sur soi/allié ; `percent` borné, durée 8s) — cf. spec SP-A §10.1.
- `HandleCastRequest` (ou un nouveau `HandleClassCastRequest`) : valider le `skillId` contre le **set connu** (`knownSkillIds`) + `ClassSkillLibrary` (au lieu du kit profil). Résoudre les effets : `Damage→DirectDamage` (+ AoE `areaRadiusMeters`), `Heal→DirectHeal`, `Defense→DamageReductionPercent`. Coût/cooldown/cast depuis le `ClassSkillDef`.
- Décision : la barre d'action SP3 envoie déjà `spellId` ; le serveur route vers `ClassSkillLibrary` si `classId`/known connu, sinon fallback kit profil (transition douce). À terme, kit profil retiré.

### Client (`Engine`)
- La barre d'action (10 slots) et le Grimoire lisent les **skills connus** de la classe (via `ClassSkillCatalog` + `knownSkillIds`) au lieu de `SpellKitCatalog.FindKit(profileId)`. Le layout `ActionBarLayout` (Grimoire) référence des `skillId` de classe.
- Cast : touche slot → `SendCastRequest(clientId, target, skillId)` (skillId de classe). Affichage cooldown/coût depuis `ClassSkillDisplay`.

### Déploiement : ⚠️ lock-step shardd + client (cast + effet moteur).

---

## Risques / garde-fous
- **Set connu vide au départ** (perso existant niveau 1 sans choix) → la barre/Grimoire montrent peu/pas de skills tant que le joueur n'a pas choisi (SP-D). Acceptable ; au niveau 1 il choisit son 1er skill.
- **Compat parseur** : réutiliser le parseur maison (gère apostrophes littérales depuis SP-A).
- **Pas de build local** → CI = garde-fou ; revue finale (sous-agent) avant chaque push.
- **Transition kit-profil → classe** : pendant SP-C, garder un fallback pour ne pas casser les persos sans classId résolu.
