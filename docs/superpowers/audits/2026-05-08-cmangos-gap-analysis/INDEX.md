# CMANGOS Gap Analysis — Index global

> **Date** : 2026-05-08
> **Périmètre** : 44 tickets `tickets/CMANGOS/CMANGOS.NN_*.md`
> **Méthode** : croisement de chaque ticket avec l'état du code engine
> et les milestones M00-M44/M100 existantes. Voir `CMANGOS.NN.md` pour
> le détail par ticket.
> **Sources** : `tickets/CMANGOS/`, `CMANGOS_ANALYSIS.md`, code `engine/`.
> **Design** : voir [docs/superpowers/specs/2026-05-08-cmangos-gap-analysis-design.md](../../specs/2026-05-08-cmangos-gap-analysis-design.md)

---

## Comment lire cet audit

### Légende — Statut implémentation

| Statut | Signification |
|---|---|
| ✅ Fait | Implémenté, conforme à la spec ticket |
| 🟡 Partiel | Composant(s) présent(s) mais incomplet par rapport au ticket |
| ❌ Absent | Aucune trace dans le code |
| 🔄 En cours | Travail en cours dans une milestone active |

### Légende — Effort

| Effort | Borne |
|---|---|
| S | ≤ 1 PR |
| M | 2-3 PR |
| L | 1 sprint |
| XL | multi-sprints |

### Légende — Recommandation

| Reco | Action |
|---|---|
| ✅ Faire en l'état | Spec ticket OK, à exécuter telle quelle |
| 🔧 Adapter et faire | Spec OK sur le fond, ajustements LCDLLN nécessaires |
| ⏸ Reporter | Pas critique court terme, à reprendre plus tard |
| 🚫 Ne pas faire | Out-of-scope pour LCDLLN |
| 🟢 Déjà couvert | Couvert par milestone existante, fiche pour mémoire |

### Légende — Risque

| Pictogramme | Risque |
|---|---|
| ⚠️ wire-breaking | Nouvel opcode ou bump `kProtocolVersion` → redéploiement lock-step |
| ⚠️ migration DB | Nouvelle table/colonne → migration idempotente requise |
| ⚠️ redéploiement | Nouveau handler master/shard requis |
| ⚠️ sécurité | Touche AUTH/Session/AccountStore → review obligatoire |
| ⚠️ config | Nouvelle clé `config.json` à provisionner serveur |
| ⚠️ dépendance | Nouvelle dépendance externe (interdit sans accord, AGENTS.md) |

---

## Synthèse — répartition

### Par statut

| Statut | Nb tickets | % |
|---|---|---|
| ✅ Fait | 1 | 2% |
| 🟡 Partiel | 16 | 36% |
| ❌ Absent | 27 | 61% |
| 🔄 En cours | 0 | 0% |

### Par recommandation

| Reco | Nb tickets | % |
|---|---|---|
| ✅ Faire en l'état | 8 | 18% |
| 🔧 Adapter et faire | 25 | 57% |
| ⏸ Reporter | 8 | 18% |
| 🟢 Déjà couvert | 1 | 2% |
| 🚫 Ne pas faire | 2 | 5% |

### Par effort

| Effort | Nb tickets approximatif |
|---|---|
| S / S-M | ~7 (Trade, Platform crash dump, Skills hooks, Util, Weather, GMTickets, Social ignored) |
| M / M-L | ~22 |
| L | ~12 |
| XL | 3 (Entities, Arena, Maps) |

---

## Top reco court terme

5 tickets prioritaires à attaquer **en premier**, avec rationale :

1. **[CMANGOS.13 Database](./CMANGOS.13.md)** — `SQLStorage` cache RAM
   typed + `SqlDelayThread` async + `SqlPreparedStatement`. **Déblocant
   amont** pour 5+ P2 downstream (Loot, AI, AuctionHouse V2, DBScripts,
   Spells). Aucune dépendance bloquante. Effort M-L. ✅ À attaquer en
   premier.

2. **[CMANGOS.16 Globals/Conditions](./CMANGOS.16.md)** — `ConditionMgr`
   data-driven + `ObjectAccessor` + `LocaleStrings` + `GraveyardManager`.
   **Déblocant amont** pour 5 P2 downstream (Loot, Quests, DBScripts, AI
   EventAI, Spells). Effort M-L. ✅ À attaquer en parallèle de CMANGOS.13.

3. **[CMANGOS.06 Accounts](./CMANGOS.06.md)** — étendre le `role`
   ENUM('player','admin') existant en hiérarchie 5 niveaux + `HasLowerSecurity`
   + `RequireMinRole` + audit. **Pré-requis** explicite pour CMANGOS.01
   Chat (`ChatCommandRouter`) et tout outillage GM. Effort M.

4. **[CMANGOS.01 Chat](./CMANGOS.01.md)** — déblocant MVP **explicite**.
   `ChatRelayHandler` master existe mais sans `ChatGate`/`ChatSanitizer`.
   **Risque sécurité actif** si chat ouvert publiquement (item link
   forging, hyperlink fake). Dépend de CMANGOS.06. Effort L.

5. **[CMANGOS.05 vmap](./CMANGOS.05.md)** — LOS/height server-side.
   Déblocant pour combat à distance crédible, anticheat, donjons
   (portes), spawn de loot au sol. **Plus gros ROI réutilisable**
   selon `CMANGOS_ANALYSIS.md`. Effort L.

**Plan d'attaque suggéré** : (13 + 16 en parallèle) → (06) → (01) →
(05). Cumul ~3-4 sprints pour ces 5 tickets, débloque ~10 P2 downstream.

---

## Tableau récap — 44 tickets

> ⏳ _Lignes remplies progressivement à chaque batch (P1 → P2 → P3 → P4)_

### P1 — Squelette & déblocants (5 tickets)

| # | Module | Cible | Statut | Effort | Valeur | Reco | Risque |
|---|---|---|---|---|---|---|---|
| [01](./CMANGOS.01.md) | Chat | cross | 🟡 Partiel | L | Critique | ✅ Faire | ⚠️ wire-breaking, migration DB, sécurité |
| [02](./CMANGOS.02.md) | Entities | shard | ❌ Absent | XL | Critique | 🔧 Adapter | ⚠️ wire-breaking, migration DB, conflit archi |
| [03](./CMANGOS.03.md) | Grids | shard | 🟡 Partiel | M | Élevée | 🔧 Adapter | — |
| [04](./CMANGOS.04.md) | Movement | cross | ❌ Absent | L | Élevée | 🔧 Adapter | ⚠️ wire-breaking |
| [05](./CMANGOS.05.md) | vmap | shard | ❌ Absent | L | Élevée | ✅ Faire | ⚠️ redéploiement, format binaire versioning |

### P2 — Gameplay essentiel (23 tickets)

| # | Module | Cible | Statut | Effort | Valeur | Reco | Risque |
|---|---|---|---|---|---|---|---|
| [06](./CMANGOS.06.md) | Accounts | master | 🟡 Partiel | M | Élevée | ✅ Faire | ⚠️ migration DB, sécurité |
| [07](./CMANGOS.07.md) | AI | shard | ❌ Absent | L | Critique | 🔧 Adapter | ⚠️ migration DB, redéploiement |
| [08](./CMANGOS.08.md) | Arena | master+shard | ❌ Absent | XL | Élevée | ⏸ Reporter | ⚠️ wire-breaking, migration DB |
| [09](./CMANGOS.09.md) | AuctionHouse | master | 🟡 Partiel | L | Élevée | 🔧 Adapter | ⚠️ wire-breaking si master, migration DB |
| [10](./CMANGOS.10.md) | BattleGround | shard | ❌ Absent | L | Élevée | 🔧 Adapter | — |
| [11](./CMANGOS.11.md) | Combat | shard | ❌ Absent | M-L | Critique | 🔧 Adapter | ⚠️ wire-breaking duel |
| [12](./CMANGOS.12.md) | Server | cross | 🟡 Partiel | M | Élevée | 🔧 Adapter | ⚠️ confidentialité PII |
| [13](./CMANGOS.13.md) | Database | cross | 🟡 Partiel | M-L | Élevée | ✅ Faire | — |
| [14](./CMANGOS.14.md) | DBScripts | shard | ❌ Absent | L | Élevée | 🔧 Adapter | ⚠️ migration DB |
| [15](./CMANGOS.15.md) | Groups | master | ❌ Absent | M-L | Élevée | 🔧 Adapter | ⚠️ wire-breaking, migration DB |
| [16](./CMANGOS.16.md) | Globals | shard | ❌ Absent | M-L | Élevée | ✅ Faire | ⚠️ migration DB |
| [17](./CMANGOS.17.md) | Loot | shard | 🟡 Partiel | L | Critique | ✅ Faire | ⚠️ migration DB |
| [18](./CMANGOS.18.md) | Mails | master | ❌ Absent | L | Élevée | ✅ Faire | ⚠️ wire-breaking, migration DB |
| [19](./CMANGOS.19.md) | Maps | shard | ❌ Absent | XL | Critique | 🔧 Adapter | ⚠️ refonte transversale, conflit archi |
| [20](./CMANGOS.20.md) | MotionGenerators | shard | ❌ Absent | L | Élevée | 🔧 Adapter | ⚠️ dépendance Detour, migration DB |
| [21](./CMANGOS.21.md) | Guilds | master | 🟡 Partiel | M-L | Élevée | 🔧 Adapter | ⚠️ wire-breaking, migration DB |
| [22](./CMANGOS.22.md) | Pools | shard | ❌ Absent | M | Moyenne | 🔧 Adapter | ⚠️ migration DB |
| [23](./CMANGOS.23.md) | Quests | cross | ❌ Absent | L | Critique | 🔧 Adapter | ⚠️ wire-breaking, migration DB, conflit archi |
| [24](./CMANGOS.24.md) | Reputation | shard | 🟡 Partiel | M-L | Élevée | 🔧 Adapter | ⚠️ wire-breaking, migration DB |
| [25](./CMANGOS.25.md) | Social | master | 🟡 Partiel | S-M | Élevée | ✅ Faire | ⚠️ wire-breaking, migration DB |
| [26](./CMANGOS.26.md) | Spells | shard | 🟡 Partiel | M-XL | Critique | 🔧 Adapter | ⚠️ wire-breaking |
| [27](./CMANGOS.27.md) | Trade | master | ✅ Fait | S | Élevée | 🟢 Déjà couvert | — |
| [28](./CMANGOS.28.md) | World | shard | ❌ Absent | M-L | Élevée | 🔧 Adapter | ⚠️ wire-breaking, migration DB |

### P3 — Ajouts à valeur (14 tickets)

| # | Module | Cible | Statut | Effort | Valeur | Reco | Risque |
|---|---|---|---|---|---|---|---|
| [29](./CMANGOS.29.md) | Anticheat | shard | 🟡 Partiel | M | Moyenne | 🔧 Adapter | — |
| [30](./CMANGOS.30.md) | Cinematics | cross | ❌ Absent | M | Moyenne | ⏸ Reporter | ⚠️ wire-breaking |
| [31](./CMANGOS.31.md) | GameEvents | shard | ❌ Absent | M | Moyenne | ⏸ Reporter | ⚠️ migration DB |
| [32](./CMANGOS.32.md) | GMTickets | master | ❌ Absent | S-M | Moyenne | ⏸ Reporter | ⚠️ wire-breaking, migration DB |
| [33](./CMANGOS.33.md) | LFG | master | ❌ Absent | M-L | Moyenne | ⏸ Reporter | ⚠️ wire-breaking |
| [34](./CMANGOS.34.md) | Metric | cross | ❌ Absent | M | Moyenne-Élevée | 🔧 Adapter | — |
| [35](./CMANGOS.35.md) | Multithreading | cross | ❌ Absent | S-M | Moyenne | 🔧 Adapter | — |
| [36](./CMANGOS.36.md) | OutdoorPvP | shard | ❌ Absent | M-L | Moyenne | ⏸ Reporter | ⚠️ wire-breaking |
| [37](./CMANGOS.37.md) | Platform | client | ❌ Absent | S | Élevée | 🔧 Adapter | ⚠️ confidentialité PII |
| [38](./CMANGOS.38.md) | PlayerBot | shard | 🟡 Partiel | M-L | Moyenne | ⏸ Reporter | — |
| [39](./CMANGOS.39.md) | Skills | shard | 🟡 Partiel | S-M | Moyenne | 🔧 Adapter | ⚠️ migration DB |
| [40](./CMANGOS.40.md) | Tools | cross | ❌ Absent | M | Moyenne-Élevée | 🔧 Adapter | — |
| [41](./CMANGOS.41.md) | Util | cross | 🟡 Partiel | S-M | Faible-Moyenne | ⏸ Reporter | — |
| [42](./CMANGOS.42.md) | Weather | shard | 🟡 Partiel | S-M | Moyenne | 🔧 Adapter | ⚠️ wire-breaking |

### P4 — Cas spécifiques (2 tickets)

| # | Module | Cible | Statut | Effort | Valeur | Reco | Risque |
|---|---|---|---|---|---|---|---|
| [44](./CMANGOS.44.md) | AuctionHouseBot | master | ❌ Absent | S-M | Conditionnelle | 🚫 Ne pas faire | — |
| [45](./CMANGOS.45.md) | Auth SRP6 | master | ❌ Absent | L | Faible | 🚫 Ne pas faire | ⚠️ wire-breaking total |

---

## Ordre d'intégration recommandé (recalibré)

L'ordre du `tickets/CMANGOS/CMANGOS.INDEX.md` est théorique. Voici
l'ordre **recalibré selon l'état actuel du code** et les dépendances
détectées.

### Phase 1 — Foundations (déblocants amont, parallèle possible)

Tous **sans dépendance bloquante non livrée** :

1. **CMANGOS.13 Database** _(SQLStorage + async + prepared)_
2. **CMANGOS.16 Globals/Conditions** _(ConditionMgr + ObjectAccessor +
   LocaleStrings + GraveyardManager)_
3. **CMANGOS.06 Accounts** _(rôles 5 niveaux + HasLowerSecurity)_

Ces 3 tickets **débloquent ~10 P2 downstream**. Faire en parallèle de
préférence.

### Phase 2 — Squelette monde (P1 + chat MVP)

Ouvrent la simulation shard moderne :

4. **CMANGOS.01 Chat** _(déblocant MVP + sécurité — dépend de .06)_
5. **CMANGOS.02 Entities** _(cherrypick : ObjectGuid 64-bit +
   UpdateFields/UpdateMask delta — option A pragmatique)_
6. **CMANGOS.03 Grids** _(étendre `SpatialPartition` avec Visitor +
   GridState machine)_
7. **CMANGOS.05 vmap** _(LOS + height shard, gros ROI)_
8. **CMANGOS.04 Movement** _(splines server-auth pour NPCs)_

### Phase 3 — Gameplay essentiel

Le cœur PvE/PvP joueur. Ordre par dépendances :

9. **CMANGOS.18 Mails** _(déblocant AH/Arena/quêtes offline)_
10. **CMANGOS.25 Social** _(étendre FriendSystem avec ignored —
    intégration ChatGate)_
11. **CMANGOS.20 MotionGenerators** _(stack + navmesh)_
12. **CMANGOS.07 AI** _(CreatureAI + EventAI DB-driven)_
13. **CMANGOS.11 Combat** _(audit M14, ajouter ThreatManager +
    HostileRefManager + DuelHandler)_
14. **CMANGOS.17 Loot** _(templates DB + groups + reference + conditions)_
15. **CMANGOS.14 DBScripts** _(DSL ~30 commandes contenu narratif)_
16. **CMANGOS.23 Quests** _(audit M15.1, intègre Conditions + DBScripts +
    Loot + Mails)_
17. **CMANGOS.26 Spells** _(audit M28+M31, ajouter ProcEvent +
    SpellFamily + Stacking isolé)_
18. **CMANGOS.24 Reputation** _(faction template matrix + bitmask +
    spillover)_
19. **CMANGOS.28 World** _(WorldStateExpression DSL leverage éditorial)_
20. **CMANGOS.19 Maps** _(adapter — instanceId + MapPersistentState
    minimal, multi-thread reporté)_
21. **CMANGOS.21 Guilds** _(étendre M32.3 livraison avec banque +
    eventlog)_

### Phase 4 — Ajouts à valeur (P3 sélectifs)

Ops + polish + extensions :

22. **CMANGOS.34 Metric** _(observabilité prod, **avant launch**)_
23. **CMANGOS.37 Platform crash dump** _(**avant launch**)_
24. **CMANGOS.35 Multithreading Messager** _(opportune)_
25. **CMANGOS.10 BattleGround** _(si donjons/colisée roadmap)_
26. **CMANGOS.22 Pools** _(rare spawns weighted)_
27. **CMANGOS.40 Tools** _(Formulas centralisé prioritaire, DBCleaner,
    PlayerDump opportune)_
28. **CMANGOS.27 Trade** _(audit léger — déjà 90% fait)_
29. **CMANGOS.42 Weather** _(driver serveur si M100.26 livré côté
    rendu)_
30. **CMANGOS.39 Skills** _(hooks craft uniquement si craft central)_
31. **CMANGOS.12 Server PacketLog** _(prio haute pour debug prod,
    OpcodeRegistry différé)_

### Phase 5 — Optionnels et polish post-launch

32. **CMANGOS.08 Arena** _(après chaîne PvP : .10 BG + .18 Mails +
    .13 DB livrés)_
33. **CMANGOS.29 Anticheat** gameplay _(quand exploits apparaissent)_
34. **CMANGOS.30 Cinematics** _(polish narratif)_
35. **CMANGOS.31 GameEvents** _(live ops saisonniers)_
36. **CMANGOS.32 GMTickets** _(post-launch support)_
37. **CMANGOS.33 LFG** _(quand donjons matures)_
38. **CMANGOS.36 OutdoorPvP** _(polish PvP zone)_
39. **CMANGOS.38 PlayerBot** _(load test scale-up)_
40. **CMANGOS.41 Util** _(opportune)_

### Skip (Ne pas faire par défaut)

41. **CMANGOS.44 AuctionHouseBot** _(activer uniquement si low-pop avéré)_
42. **CMANGOS.45 Auth SRP6** _(hash + TLS suffisant, refonte non
    justifiée)_

---

## Notes globales

Observations transversales relevées en analysant les 44 tickets :

### 1. Trois déblocants amont multipliers

Trois tickets émergent comme **multi-déblocants** :
- **CMANGOS.13 Database** (SQLStorage cache) → 5+ P2 downstream
- **CMANGOS.16 Globals/Conditions** (ConditionMgr) → 5+ P2 downstream
- **CMANGOS.06 Accounts** (rôles 5 niveaux) → 3+ P2 downstream

Faire ces 3 tickets **avant** la majorité des autres P2 est le meilleur
levier de productivité. Sans eux, chaque ticket downstream réimplémente
ses propres versions de cache/predicate/role → dette accumulée.

### 2. Conflit architectural OOP cmangos vs data-driven LCDLLN

Plusieurs tickets reposent sur la **hiérarchie OOP profonde** cmangos
(`Object → WorldObject → Unit → Player`) qui diverge de l'archi
**data-driven JSON** LCDLLN (`SpawnerRuntime`, archetypes).

Tickets concernés : **.02 Entities, .19 Maps, .23 Quests, .26 Spells**.

→ **Décision archi à valider en amont**. Recommandation par défaut :
**Adapter** (cherrypick patterns utiles : ObjectGuid, UpdateFields,
std::variant Quests) plutôt que porter la hiérarchie complète.

### 3. Audit milestones LCDLLN existantes obligatoire

Ces milestones **livrent ou couvrent partiellement** des tickets CMANGOS,
auditer l'état avant tout code :

| Milestone LCDLLN | Couvre CMANGOS | Action |
|---|---|---|
| M14.1/.2 Combat + Aggro | .11 Combat | Auditer + cherrypick |
| M15.1 Quests JSON | .23 Quests | Décision JSON vs SQL |
| M15.2 Spawners | .07 AI, .22 Pools | Étendre |
| M28+M31 Skills/Buffs | .26 Spells | Auditer + ajouter ProcEvents |
| M32.1 Friends | .25 Social | Étendre avec ignored |
| M32.3 Guilds | .21 Guilds | Compléter banque + eventlog |
| M33.1/.2 Auth | .45 Auth SRP6 | Skip (suffisant) |
| M35.3 Trade | .27 Trade | ✅ Déjà couvert |
| M35.4 AuctionHouse | .09 AH | Étendre V2 multi-house |
| M44.1 Migrations | .13 Database | OK base, ajouter SQLStorage |
| M100.25 Season | .31 GameEvents | Distinct (visuel vs gameplay) |
| M100.26-28 Weather | .42 Weather | Côté rendu OK, driver shard absent |

### 4. Risques wire-breaking groupables

~12 tickets impliquent un bump `kProtocolVersion` + redéploiement
lock-step master+shard+client. **Stratégie** : grouper plusieurs
opcodes dans une même release, pour ne bumper qu'**une fois par
sprint** (au lieu d'un bump par PR).

### 5. Migrations DB systématiques

~15 tickets ajoutent des tables. Toutes doivent être **idempotentes**
(convention LCDLLN respectée par les 40+ migrations existantes :
`IF NOT EXISTS`, vérification colonne avant ALTER, etc.).

### 6. Une seule fiche ✅ Fait

**CMANGOS.27 Trade** est le seul ticket marqué "déjà couvert"
(`TradeSystem` 802 lignes, M35.3 livré). Tous les autres sont 🟡
Partiel ou ❌ Absent.

### 7. Volume de travail réaliste

Effort cumulé : ~22 tickets en L, 3 en XL, ~12 en M, ~7 en S/S-M →
**estimation grossière 25-30 sprints** si tout est attaqué. **Priorisation
indispensable** : la chaîne Phase 1 + Phase 2 (~10 tickets) suffit pour
un MVP serveur joué.

### 8. Pas de redéploiement serveur pour cet audit

Conformément à la règle CLAUDE.md du projet, l'audit est purement
documentaire — **aucun redéploiement serveur master ou shard requis**
pour les commits de cet audit. Les recommandations downstream
mentionnent les redéploiements requis quand applicable (wire-breaking).

### 9. Suite recommandée

Cet audit livre l'**état des lieux**. Suite naturelle :
- **Option A** : créer un plan d'implémentation
  (`docs/superpowers/plans/2026-05-NN-cmangos-phase-1.md`) pour les 3
  tickets Phase 1 (CMANGOS.13 + .16 + .06), via la skill
  `writing-plans`. Effort cumulé estimé : 1-2 sprints.
- **Option B** : laisser cet audit comme référence et piocher au fil
  des sprints sans plan global.

---

## Avancement de l'audit

| Batch | Tickets | Statut | Commit |
|---|---|---|---|
| 0 — Squelette | INDEX vide + design | ✅ Done | `6b24889` |
| 1 — P1 | 5 fiches (.01-.05) | ✅ Done | `0649d13` |
| 2a — P2 part 1 | 12 fiches (.06-.17) | ✅ Done | `c82928e` |
| 2b — P2 part 2 | 11 fiches (.18-.28) | ✅ Done | `8454aaa` |
| 3 — P3 | 14 fiches (.29-.42) | ✅ Done | `4359521` |
| 4 — P4 | 2 fiches (.44-.45) | ✅ Done | `955f410` |
| 5 — Consolidation | INDEX rempli | ✅ Done | _(en cours de commit)_ |

---

*Audit généré le 2026-05-08. Mises à jour : éditer cet index + la
fiche concernée + ajouter une ligne "Mises à jour" en bas de la fiche
si nécessaire.*
