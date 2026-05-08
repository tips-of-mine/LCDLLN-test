# CMANGOS.26 — Spells (split template / aura / proc)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.26_Spells_split_template_aura_proc.md](../../../../tickets/CMANGOS/CMANGOS.26_Spells_split_template_aura_proc.md)
> **Priorité** : P2 — gameplay essentiel (gros morceau)
> **Cible** : shard

## 1. Statut implémentation

🟡 **Partiel** — `SkillSystem` + `StatusEffect` côté gameplay, milestones
M28 (Skills) et M31 (Buffs/Debuffs) existent. Mais la décomposition
canonique cmangos (`Spell`/`SpellTemplate`/`SpellAura`/`ProcEvent`)
+ proc system + family mask + stacking rules isolées est probablement
absente.

## 2. Preuves dans le code

**Existant :**
- [engine/gameplay/SkillSystem.h](../../../../engine/gameplay/SkillSystem.h) + `.cpp` — système skills
- [engine/gameplay/StatusEffect.h](../../../../engine/gameplay/StatusEffect.h) + `.cpp` — buffs/debuffs basiques
- [engine/client/AoEPreviewSystem.cpp](../../../../engine/client/AoEPreviewSystem.cpp) — preview AoE côté client
- M28.1 — Skills data-driven definitions + cooldowns
- M28.2 — Skills casting + interruption + channels
- M28.3 — Skills AoE targeting + ground placement
- M28.4 — Skills combo system + resource builders/spenders
- M31.1 — Buffs/Debuffs status effects + stacking
- M31.2 — Buffs/Debuffs UI display + auras

**Manquant (vs spec ticket — décomposition canonique cmangos) :**
- ❌ `engine/server/shard/spells/` — dossier inexistant côté shard
  (la logique est côté `gameplay`)
- ❌ Split rigoureux `Spell` (instance) / `SpellTemplate` (def DB) /
  `SpellAura` (effet appliqué) / `SpellAuraHolder` (collection)
- ❌ State machine de cast `PREPARING → CASTING → FINISHED` formellement
  séparée
- ❌ `SpellProcEvent` system (proc data-driven sur events
  hit/crit/dies)
- ❌ `SpellFamily` + `ClassFamilyMask` (talents qui buffent une famille
  entière)
- ❌ `SpellStacking.cpp` isolé (algèbre des buffs : replace, refresh,
  sum)
- ❌ `SpellTargeting` matrix (target_type → résolution liste Units)
- ❌ Tables DB `spell_template`, `spell_proc_event`, `spell_chain`

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement, autre architecture)** — M28+M31 (6 tickets)
livrent un système skills + status effects qui couvre une grosse
partie du périmètre, mais selon une **architecture LCDLLN propre**.

À auditer : que livrent exactement M28/M31 ? Si proc events + family
masks + stacking rules avancées sont déjà couverts, la spec cmangos
peut être source d'inspiration ponctuelle plus que portage massif.

## 4. Écart par rapport à la spec CMANGOS

L'écart **architectural** est important si on adopte la décomposition
cmangos pure :

1. **Split rigoureux** — `Spell` (instance cast) ≠ `SpellTemplate` (def)
   ≠ `SpellAura` (buff actif). Évite confusion.
2. **State machine de cast** — formelle, applicable à toute action à
   durée (récolte, build, drink). Pattern réutilisable.
3. **ProcEvent system** — réactivité data-driven (pas codée par
   talent). **Game-changer** pour le content design.
4. **SpellFamily + Mask** — "buffer toutes les sorts feu mage"
   sans énumérer.
5. **SpellStacking séparé** — algèbre testable indépendamment des
   effects.

LCDLLN avec M28/M31 a probablement déjà un système qui marche
fonctionnellement. Le gap est sur la **décomposition** et l'**extensibilité**
(proc events, family mask).

## 5. Effort estimé

**XL** (multi-sprints) si on adopte la décomposition cmangos complète.
**M-L** si on adapte (cherrypick proc events + family mask + stacking
isolé) au-dessus de M28/M31 existants.

C'est explicitement marqué "**Gros morceau**" dans le ticket.

## 6. Valeur joueur/serveur

**Critique** — déblocant pour gameplay magique/talents/réactif. Sans
proc system, chaque talent réactif est codé en dur (dette).

Mais **partiellement déjà livré** via M28+M31 — l'urgence dépend de ce
que ces milestones couvrent exactement.

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.02 Entities** — Unit porte la liste d'auras
- **CMANGOS.04 Movement** — cast interrompu par mouvement
- **CMANGOS.05 vmap** — LOS check avant cast
- **CMANGOS.11 Combat** — cast hostile met en combat
- **CMANGOS.13 Maps** — `Spell::Update` tickté par la Map
- **CMANGOS.16 Globals/Conditions** — `required_condition`

→ **CMANGOS.26 dépend de la chaîne P1 + P2 amont**. Pas le premier à
attaquer.

## 8. Risque / piège ⚠️

- ⚠️ **Doublon avec M28/M31** — risque réimplémentation. Audit
  obligatoire avant code.
- ⚠️ **Migration DB** — `spell_template`, `spell_proc_event`,
  `spell_chain` (probablement déjà chez M28.1 via JSON ou DB).
- ⚠️ **State machine cast** — bug subtil sur transitions (cancel pendant
  PREPARING vs CASTING). Tests exhaustifs.
- ⚠️ **Stacking rules** — bug ici = jeu cassé (buffs qui se cumulent
  abusivement, ou se remplacent à tort). `SpellStacking` testable
  isolément, **pas de couplage avec le reste**.
- ⚠️ **ProcEvent récursion** — un proc déclenche un sort qui déclenche
  un proc... profondeur max obligatoire.
- ⚠️ **Family mask** — bitmask 64 bits, runtime out-of-the-box mais
  data design délicat (qui décide quel sort est dans quelle famille).
- ⚠️ **Wire-breaking** — opcodes spell cast (Cancel/Update). Bump.
- ⚠️ **Performance** — 100 mobs × 5 auras chacun × tick = 500 ticks/frame.
  À profiler.

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** audit M28/M31 :

1. **Étape 0 (critique)** : audit complet M28+M31 livraisons. Mesurer
   précisément ce qui est livré.
2. **Étape 1 (selon audit)** :
   - Si M28/M31 couvrent décemment → cherry-pick patterns cmangos
     manquants (proc events, family mask, stacking isolé). **Effort M-L**.
   - Si M28/M31 sont V1 minimal → adopter décomposition cmangos.
     **Effort XL**, multi-sprints.
3. **Étape 2** : `SpellProcEvent` (le pattern le plus rentable) + tests.
4. **Étape 3** : `SpellStacking.cpp` isolé + tests (algèbre buffs).
5. **Étape 4** : `SpellFamily` + `ClassFamilyMask` (extensibilité
   talents).
6. **Étape 5** : intégration LOS check (CMANGOS.05) + interruption
   mouvement (CMANGOS.04).

À planifier après la chaîne P1 + .11 Combat. Effort important mais
**partiellement déjà fait**.

---

*Audit du 2026-05-08. Mises à jour : —*
