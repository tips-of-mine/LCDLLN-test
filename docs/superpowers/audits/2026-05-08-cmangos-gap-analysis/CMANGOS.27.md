# CMANGOS.27 — Trade (two-phase commit / anti-scam)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.27_Trade_two_phase_commit_anti_scam.md](../../../../tickets/CMANGOS/CMANGOS.27_Trade_two_phase_commit_anti_scam.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : master

## 1. Statut implémentation

✅ **Fait (largement)** — `TradeSystem` LCDLLN (M35.3) implémente la
state machine 2-phase commit avec phases `Negotiation`/`Review`/
`Confirming` et fenêtre anti-scam 5s. Très proche du pattern cmangos.

## 2. Preuves dans le code

**Existant :**
- [engine/server/TradeSystem.h](../../../../engine/server/TradeSystem.h) (202 lignes) :
  - `TradePhase` enum (`Negotiation`/`Review`/`Confirming`)
  - `TradeSide` (clientId, gold, locked, confirmed, items)
  - `TradeSession` (sideA + sideB, lifecycle complet)
- [engine/server/TradeSystem.cpp](../../../../engine/server/TradeSystem.cpp) (602 lignes) — implémentation
- [db/migrations/0018_player_trade_log.sql](../../../../db/migrations/0018_player_trade_log.sql) — log audit trades
- M35.3 milestone — Trade system (déjà livré)

**Différences vs spec ticket :**
- ⏳ Fenêtre anti-scam 5s LCDLLN vs **6s cmangos** (paramétrable, peu
  important)
- 🟡 Pas de `std::array<TradeSlot, 7>` fixe (LCDLLN utilise
  `std::vector<ItemStack>` — plus flexible)
- 🟡 `itemEntry` copie pour anti-stale ? À vérifier dans le code détaillé.

**Manquant (potentiellement) :**
- ⏳ Validation atomique transaction DB rollback si échec
  (vérification manuelle recommandée)
- ⏳ Reset checkboxes à chaque changement (probablement OK via lock
  flag)

## 3. Recouvrement milestones existantes

✅ **Couvert** — M35.3 livre le système Trade complet. CMANGOS.27 est
**déjà couvert pour l'essentiel**.

## 4. Écart par rapport à la spec CMANGOS

L'écart est **minime** — LCDLLN a un système trade fonctionnel et
proche du pattern cmangos. Les patterns critiques sont en place :
- 2-phase commit ✓
- Both-must-confirm ✓
- Fenêtre anti-scam ✓

Points à valider en review approfondie :
1. **Atomicité DB** — la transaction inventory swap est-elle bien
   atomique avec rollback en cas d'échec partiel ?
2. **Anti-stale itemEntry** — copie de l'entry au moment du clic
   accept, pour détecter swap d'items en dernière seconde ?
3. **Reset checkboxes** — bien réinitialisés à chaque changement
   d'item/gold ?

## 5. Effort estimé

**S** (≤1 PR) — pour les ajustements vs spec cmangos. Si l'audit
révèle des manques majeurs, **M** (1 PR refacto).

## 6. Valeur joueur/serveur

**Élevée** — feature visible attendue. Anti-scam = protection
réputation.

Mais **déjà livré pour l'essentiel** — la valeur incrémentale du portage
cmangos est limitée.

## 7. Dépendances bloquantes

Aucune — tout est déjà en place côté LCDLLN (M35.3 + inventory + currency).

## 8. Risque / piège ⚠️

- ⚠️ **Atomicité transaction** — bug ici = perte items/gold ou
  duplication. Test de torture obligatoire (kill connexion en plein
  trade).
- ⚠️ **Anti-stale itemEntry** — sans cette copie, exploit possible :
  accept avec gem rare, swap par gem cheap juste avant confirm.
- ⚠️ **Anti-scam window** — trop court (3s) = pas le temps de relire ;
  trop long (15s) = frustrant. 5s OK. À mesurer feedback joueur.
- ⚠️ **Trade cross-shard** — actuellement shard ? master ? Politique à
  vérifier. Cross-shard = master required, plus complexe.
- ⚠️ **Crashs en phase Confirming** — si crash après "OK" mais avant
  flush DB, état incertain. Idempotence transaction.

## 9. Recommandation finale

🟢 **Déjà couvert** (largement) — fiche pour mémoire :

1. **Étape 1 (audit léger)** : revue de code TradeSystem.cpp pour
   vérifier les 3 points :
   - Atomicité DB transaction (rollback si échec partiel)
   - Anti-stale itemEntry copie au lock
   - Reset checkboxes à chaque changement
2. **Étape 2 (selon audit)** : ajustements ponctuels si manques.
   Effort S.
3. **Étape 3 (optionnel)** : aligner anti-scam window à 6s si
   homologation cmangos pure.

**Priorité basse** — pas de gain joueur visible, juste consolidation.
À faire opportunément quand on touche TradeSystem pour autre raison.

---

*Audit du 2026-05-08. Mises à jour : —*
