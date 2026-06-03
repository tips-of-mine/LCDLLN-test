# Spec — Nettoyage des branches & réorganisation de `tickets/`

**Date :** 2026-06-03
**Branche de travail :** `chore/tickets-reorg`
**Auteur :** Claude (brainstorming dirigé par l'utilisateur)
**Déploiement serveur :** ✅ aucun — changement docs/tickets uniquement (ni client, ni master, ni shard).

---

## 1. Problème & objectif

Le dossier `tickets/` contient **424 fichiers de tickets** répartis sur de
nombreux dossiers (`M00`…`M45`, `M100`, `AUTH-UI`, `CHAR-MODEL`, `CMANGOS`,
`UI`, `annexe`, `world`, `docs`) plus **139 fichiers de clôture** dans
`tickets/issues/`. Une grande partie des tickets a déjà été implémentée, mais
l'utilisateur ne sait plus lesquels sont **terminés**, **partiels** ou **à
faire**.

En parallèle, le dépôt local accumule des branches dont le remote a été
supprimé (mergées) et qui n'ont plus d'utilité.

**Objectif :**
1. Nettoyer les branches locales obsolètes et resynchroniser avec le remote.
2. Vérifier, ticket par ticket, ce qui est réellement implémenté dans le code.
3. Clôturer les tickets terminés (création d'une *Issue* dans `tickets/issues/`).
4. Réorganiser `tickets/` pour que le statut soit lisible d'un coup d'œil.
5. Produire un **tableau de suivi** récapitulatif.

## 2. Décisions verrouillées (validées avec l'utilisateur)

| # | Sujet | Décision |
|---|-------|----------|
| D1 | Nettoyage branches | **Sûr** : supprimer uniquement les branches dont le remote est `gone` ET dont les commits sont confirmés présents dans `main`. Conserver les branches non poussées (travail unique), celles avec PR ouvert, et la branche du worktree. |
| D2 | Dossier `CMANGOS` | **Renommer** en `SERVER-CORE` (dossier + mentions internes du terme), conformément à la règle stricte « pas de terme CMANGOS ». |
| D3 | Matérialisation du statut | **Issue = source de vérité.** Ticket validé implémenté → créer son Issue dans `tickets/issues/` ET **retirer** le `.md` du dossier `Mxx`. Les dossiers `Mxx` ne contiennent plus que le travail restant. Un tableau `TICKETS_STATUS.md` récapitule tout. |
| D4 | Profondeur de vérification | **Approfondie systématique** : lecture réelle du code lié à chaque ticket pour trancher, avec preuves (fichiers + lignes). Parallélisé via sous-agents. |
| D5 | Git | **Branche dédiée `chore/tickets-reorg` + PR** (pas de commit direct sur `main`). |
| D6 | Tickets PARTIAL | Restent dans `Mxx` avec un **encart d'état** en tête (fait / manque). Pas d'Issue tant que non terminé. |

## 3. Convention de statut

Pour chaque ticket, un des trois verdicts :

- **DONE** — tous les livrables du ticket sont présents et corrects dans le
  code (symboles, fichiers, passes, handlers attendus). Preuve = liste de
  fichiers:lignes.
- **PARTIAL** — une partie des livrables existe, une autre manque clairement.
  Preuve = ce qui est fait + ce qui manque.
- **TODO** — aucun livrable significatif présent dans le code.

Règle de décision : on part des sections **Livrables** / **Étapes
d'implémentation** / **Critères de validation (DoD)** du ticket, et on cherche
la contrepartie réelle dans `/engine`, `/game`, `/tools`, `/src`, serveur, etc.
En cas de doute → deep-dive ciblé avant de trancher. Un ticket purement
« spec/docs » (pas de livrable code) est DONE si le document de référence
qu'il décrit existe.

## 4. Format de clôture (Issue)

Réutilise le format existant (`tickets/issues/M06.1_..._Issue.md`) :

```markdown
# Issue: <CODE> — <Titre>

**Status:** Closed

---

## Rapport final

### 1) FICHIERS  (preuves d'implémentation : fichiers existants dans le repo)
### 2) COMMANDES  (rappel build/run si pertinent)
### 3) RÉSULTAT  (implémenté : OUI — résumé de la vérification code)
### 4) VALIDATION DoD  (points couverts)

---

## Ticket content (<CODE>)

<contenu intégral du ticket ré-embarqué>
```

Nommage : `tickets/issues/<stem-du-ticket>_Issue.md` (stem = nom de fichier du
ticket sans `.md`).

## 5. Plan d'exécution (phases)

### Phase 0 — Branches & worktree
1. `git fetch --prune` (déjà fait).
2. Identifier les branches `: gone`. Pour chacune, confirmer que le travail est
   dans `main` (`git branch --merged main` ; pour les squash-merges, vérifier
   que `main..branche` ne contient pas de travail unique perdu).
3. Supprimer les branches confirmées mergées (`git branch -d`, `-D` seulement
   après confirmation explicite du contenu).
4. **Ne pas** supprimer : branches `ahead` non poussées, branches avec PR
   ouvert (remote présent), `feat/prop-draw-distance` (worktree `LCDLLN-foret`).
5. Produire un rapport « supprimées / conservées + raison ».

### Phase 1 — Normalisation (sur `chore/tickets-reorg`)
1. `git mv tickets/CMANGOS tickets/SERVER-CORE` ; renommer les fichiers
   `CMANGOS.xx_*.md` → `SERVER-CORE.xx_*.md` ; remplacer le terme dans le
   contenu de ces tickets.
2. Normaliser les clôtures hors `issues/` (`M03/M03.0_ISSUE.md`,
   `M03/M03.1_ISSUE.md`, `annexe/STAB.13…_Issue.md`) → `tickets/issues/` au
   format `<stem>_Issue.md`.
3. Construire l'inventaire complet (ticket ↔ issue existante) pour ne pas
   re-vérifier ce qui est déjà clôturé.

### Phase 2 — Vérification approfondie
1. Découper les tickets non clôturés en lots par milestone.
2. Dispatcher des sous-agents (`Explore` / `general-purpose`), chacun lit le
   code lié et renvoie un **verdict structuré** par ticket :
   `{ code, statut: DONE|PARTIAL|TODO, preuves: [fichier:ligne], manque: [...] }`.
3. Agréger les verdicts. Point d'avancement par vagues.

### Phase 3 — Application
- **DONE** → créer l'Issue + `git rm` du ticket dans `Mxx`.
- **PARTIAL** → ajouter encart d'état en tête du ticket (reste dans `Mxx`).
- **TODO** → inchangé.
- Supprimer les dossiers milestone **vides** uniquement.

### Phase 4 — Tableau de suivi
- `tickets/TICKETS_STATUS.md` : une table par milestone
  (`Ticket | Titre | Statut | Preuve/Issue | Manque`) + compteurs globaux.

### Phase 5 — Livraison
- Commits logiques par phase sur `chore/tickets-reorg`.
- Pousser, ouvrir une PR, indiquer quand merger.
- Mention déploiement : ✅ aucun redéploiement serveur.

## 6. Garde-fous

- `tickets/` était marqué « lecture seule » dans `AGENTS.md` : cette
  réorganisation est une **exception explicitement demandée** par l'utilisateur
  (override). Le code source (`/engine`, `/game`, `/src`, serveur) **n'est pas
  modifié** par ce travail — uniquement des fichiers Markdown sous `tickets/`
  et `docs/`.
- Aucune suppression de branche `-D` sans confirmation du contenu.
- Aucune suppression de worktree.
- Le dossier `legacy/` n'est pas touché (règle utilisateur).
- Pas de modification serveur, pas de wire-change → pas de redéploiement.

## 7. Hors périmètre (YAGNI)

- Pas de réécriture du contenu des tickets (hors encart d'état PARTIAL et
  retrait du terme CMANGOS).
- Pas de création de nouveaux tickets.
- Pas de modification du code applicatif.
- Pas de fusion/scission de milestones au-delà de la suppression des dossiers
  vides.

## 8. Critères de succès

- [ ] Branches obsolètes supprimées, rapport fourni, dépôt resynchronisé.
- [ ] `tickets/CMANGOS` renommé en `tickets/SERVER-CORE`, terme retiré.
- [ ] Chaque ticket non clôturé a reçu un verdict DONE/PARTIAL/TODO avec preuve.
- [ ] Chaque DONE a son Issue dans `tickets/issues/` et n'apparaît plus dans `Mxx`.
- [ ] Chaque PARTIAL a un encart d'état.
- [ ] Dossiers vides supprimés.
- [ ] `TICKETS_STATUS.md` produit et cohérent avec l'état réel.
- [ ] PR ouverte, instruction de merge donnée.
