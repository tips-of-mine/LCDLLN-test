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

> ⏳ _Compteurs remplis à l'issue de l'audit (Batch 5)_

### Par statut

| Statut | Nb tickets | % |
|---|---|---|
| ✅ Fait | _(à remplir)_ | _(à remplir)_ |
| 🟡 Partiel | _(à remplir)_ | _(à remplir)_ |
| ❌ Absent | _(à remplir)_ | _(à remplir)_ |
| 🔄 En cours | _(à remplir)_ | _(à remplir)_ |

### Par recommandation

| Reco | Nb tickets | % |
|---|---|---|
| ✅ Faire en l'état | _(à remplir)_ | _(à remplir)_ |
| 🔧 Adapter et faire | _(à remplir)_ | _(à remplir)_ |
| ⏸ Reporter | _(à remplir)_ | _(à remplir)_ |
| 🟢 Déjà couvert | _(à remplir)_ | _(à remplir)_ |
| 🚫 Ne pas faire | _(à remplir)_ | _(à remplir)_ |

### Par effort

| Effort | Nb tickets |
|---|---|
| S | _(à remplir)_ |
| M | _(à remplir)_ |
| L | _(à remplir)_ |
| XL | _(à remplir)_ |

---

## Top reco court terme

> ⏳ _Rempli à l'issue de l'audit (Batch 5)_

3 à 5 tickets prioritaires à attaquer en premier, avec rationale en
1 phrase chacun.

1. _(à remplir)_
2. _(à remplir)_
3. _(à remplir)_

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

> ⏳ _Rempli à l'issue de l'audit (Batch 5), avec rationale détaillée_

L'ordre du `tickets/CMANGOS/CMANGOS.INDEX.md` est théorique. Cette
section proposera une version recalibrée selon l'état actuel du code et
les dépendances détectées.

### Phase 1 — Foundations (P1 + pré-requis)

_(à remplir)_

### Phase 2 — Squelette monde (P1 reste)

_(à remplir)_

### Phase 3 — Gameplay essentiel (P2 sélectif)

_(à remplir)_

### Phase 4 — Ajouts de valeur (P3)

_(à remplir)_

### Phase 5 — Optionnels (P4)

_(à remplir)_

---

## Notes globales

> ⏳ _Rempli à l'issue de l'audit (Batch 5)_

Observations transversales relevées en analysant les 44 tickets
(ex: dépendances communes, patterns récurrents, points de friction
généralisés).

---

## Avancement de l'audit

| Batch | Tickets | Statut | Commit |
|---|---|---|---|
| 0 — Squelette | INDEX vide + design | ✅ Done | `6b24889` |
| 1 — P1 | 5 fiches (.01-.05) | ✅ Done | `0649d13` |
| 2a — P2 part 1 | 12 fiches (.06-.17) | ✅ Done | `c82928e` |
| 2b — P2 part 2 | 11 fiches (.18-.28) | ✅ Done | `8454aaa` |
| 3 — P3 | 14 fiches (.29-.42) | ✅ Done | `4359521` |
| 4 — P4 | 2 fiches (.44-.45) | ✅ Done | _(en cours de commit)_ |
| 5 — Consolidation | INDEX rempli | ⏳ Pending | — |

---

*Audit généré le 2026-05-08. Mises à jour : éditer cet index + la
fiche concernée + ajouter une ligne "Mises à jour" en bas de la fiche
si nécessaire.*
