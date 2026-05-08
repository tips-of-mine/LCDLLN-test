# CMANGOS.10 — BattleGround (framework / instances / score)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.10_BattleGround_framework_instances_score.md](../../../../tickets/CMANGOS/CMANGOS.10_BattleGround_framework_instances_score.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : shard

## 1. Statut implémentation

❌ **Absent** — aucun framework PvP instancié, aucune hiérarchie
`BattleGround`, aucune `BattleGroundQueue`, aucun `BattleGroundManager`.

## 2. Preuves dans le code

**Existant :**
- (Aucun fichier `BattleGround*` dans `engine/`)

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/battleground/` — dossier inexistant
- ❌ `BattleGround` interface abstraite (méthodes virtuelles : `OnStart`,
  `OnTick`, `OnEnd`, `OnPlayerJoin/Leave`, `OnPlayerLoggedIn/Out`,
  `OnPlayerKilled`, `OnFlagCaptured`, `CreateScore`)
- ❌ `BattleGroundScore` sous-classable per-BG
- ❌ `BattleGroundQueue` séparé du Manager
- ❌ `BattleGroundManager` (création/destruction instances)
- ❌ `BattleGroundColosseum` (premier impl)
- ❌ `EventPlayerLoggedIn` / `EventPlayerLoggedOut` reconnexion plein match
- ❌ Auto-reset instances vides
- ❌ Config `battleground.wait_for_players_timeout_sec` etc.

## 3. Recouvrement milestones existantes

❌ **Non couvert** — aucune milestone PvP instancié dans M00-M44.

## 4. Écart par rapport à la spec CMANGOS

100% absent. C'est un déblocant pour CMANGOS.08 Arena/Colisée — sans le
framework BattleGround, le `ColosseumArena` n'a pas de classe parent.

Les 5 piliers du ticket sont tous critiques :
1. Hiérarchie virtuelle (extensibilité content)
2. Queue séparée du Manager (interrogeable sans instance active)
3. Score sous-classable (stats par BG)
4. Reconnexion plein match (critique MMORPG, joueurs déconnectent)
5. Auto-reset instances vides (mémoire shard)

## 5. Effort estimé

**L** (1 sprint complet) :
- Interface `BattleGround` abstraite + `BattleGroundScore`
- `BattleGroundQueue` (matchmaking par taille)
- `BattleGroundManager` (lifecycle instances)
- `BattleGroundColosseum` (premier impl minimal)
- Reconnexion logout/login (lien avec session/auth)
- Tests start→tick→end + match formation + score sous-classé + reconnect
- Smoke test : 5v5 forme un match, démarre, scores tracés, fin

Pas de wire-breaking côté protocole **dans le ticket lui-même** (les
opcodes BG côté client viendraient avec les sous-classes spécifiques),
mais probablement quelques opcodes au final (queue/state/score).

## 6. Valeur joueur/serveur

**Élevée** — déblocant explicite pour CMANGOS.08 Arena/Colisée et toute
future variante PvP instanciée (capture flag, conquête, etc.).

Pas de valeur **directe** joueur tant qu'aucune sous-classe n'est livrée.
Mais infrastructure indispensable sans laquelle aucun PvP instancié ne
peut démarrer.

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.19 Maps** — `BattleGroundMap` est sous-classe de `Map`. Si
  CMANGOS.19 n'est pas fait, la sous-classe Map BG n'a pas de parent.
- **CMANGOS.08 Arena** — relation symétrique : Arena utilise BG. Donc
  BG d'abord, Arena ensuite.
- Système de session/auth pour reconnexion (déjà en place LCDLLN via
  `SessionManager`/`ConnectionSessionMap`).

## 8. Risque / piège ⚠️

- ⚠️ **Reconnexion logout/login plein match** — critique MMORPG. Le
  joueur qui crash a 60s pour revenir et reprendre sa place. À tester
  exhaustivement (état session, état BG, replication).
- ⚠️ **Auto-kick offline at match end** — protection contre AFK. Si trop
  agressif, frustration joueur ; si trop lâche, exploit AFK farming.
- ⚠️ **Match max duration** — 30 min default. Forcer une fin si match
  bloqué (équipes au stalemate). Hash du gagnant requis (tie-break).
- ⚠️ **Queue formation imparfaite** — 9 joueurs en queue 5v5 → un attend.
  Patterns : pré-réservation + timeout, accept/decline UX. Hors scope
  ticket mais à prévoir.
- ⚠️ **Score sous-classé** — sérialisation spécifique par BG (variantes
  WSG/AB/AV/Colosseum). Impact wire si scores envoyés au client.
- ⚠️ **Coordination master ↔ shard** — la queue peut être master (cross-shard)
  ou shard (local). Le ticket n'est pas explicite. Décision : pour le
  colisée local LCDLLN, queue **shard-side** suffit.
- ⚠️ **Redéploiement** — nouveau handler shard. Pas de migration DB
  (instances volatiles, pas persistées au-delà du match).
- Pas de wire-breaking immédiat (les opcodes BG viennent avec les
  sous-classes spécifiques).

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** CMANGOS.19 Maps (pré-requis pour
`BattleGroundMap`) et **avant** CMANGOS.08 Arena (qui en dépend) :

1. **Étape 0** : valider que CMANGOS.19 Maps a livré la classe `Map`
   sous-classable, sinon adapter avec un wrapper temporaire.
2. **Étape 1** : implémenter `BattleGround` abstract + `BattleGroundScore`
   + `BattleGroundManager` minimal (sans queue).
3. **Étape 2** : implémenter `BattleGroundQueue` (matchmaking simple par
   taille).
4. **Étape 3** : implémenter `BattleGroundColosseum` minimal (sous-classe
   2v2/3v3/5v5, win = last team standing).
5. **Étape 4** : reconnexion logout/login (smoke test couper réseau
   client en plein match).
6. **Étape 5** : auto-reset instances vides + tests load.

À planifier en parallèle de la chaîne CMANGOS.07 AI / CMANGOS.11 Combat
(ils nourrissent tous le PvE/PvP en même temps). Pas critique tant que
le PvE de base n'est pas joué.

---

*Audit du 2026-05-08. Mises à jour : —*
