# CMANGOS.25 — Social (friends / ignored / presence)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.25_Social_friends_ignored_presence.md](../../../../tickets/CMANGOS/CMANGOS.25_Social_friends_ignored_presence.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : master

## 1. Statut implémentation

🟡 **Partiel** — `FriendSystem` LCDLLN livré (M32.1) avec friends bilatéral
+ presence + rate limiting + cap 200 friends. Mais la **liste ignored**
et les **notes privées** sont absentes.

## 2. Preuves dans le code

**Existant :**
- [engine/server/FriendSystem.h](../../../../engine/server/FriendSystem.h) + `.cpp` — système amis complet :
  - `FriendRecord` (playerId/friendId/status/name)
  - `FriendPresenceEntry` (presence in-memory)
  - Rate limiting ≤5 requests/60s
  - Cap 200 friends per player
  - Bilateral DB persistence
  - Online-status tracking
- [db/migrations/0002_friends.sql](../../../../db/migrations/0002_friends.sql) — schéma `friends` (status pending/
  accepted/declined)
- M32.1 milestone — Friends list + online status (déjà livré)

**Manquant (vs spec ticket) :**
- ❌ `engine/server/social/` — dossier dédié inexistant (tout est dans
  `FriendSystem`)
- ❌ Liste **ignored** : pas de `IgnoreList`/`IsIgnored`/`AddIgnore`/
  `RemoveIgnore`
- ❌ **Notes privées** par contact (champ texte attaché au mapping)
- ❌ Filtrage côté chat-relay des messages des ignored (économie réseau
  si beaucoup d'ignorés)
- ❌ Table `ignored_players` ou colonne `note` dans table existante
- ❌ Opcodes Ignore (Add/Remove/List)

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement)** — M32.1 livre la partie amis. Pas de
milestone dédiée au système ignored / notes.

## 4. Écart par rapport à la spec CMANGOS

L'écart **fonctionnel** est modéré :

1. **Friends** — déjà livré ✓
2. **Presence broadcast** — déjà livré ✓
3. **Ignored list** — à ajouter (pattern simple : table + check)
4. **Notes privées** — à ajouter (colonne `note VARCHAR(255)` sur
   `friends`)
5. **Filtrage chat-relay côté expéditeur** — intégration avec CMANGOS.01
   (`ChatGate::CanSpeak` consulte `IgnoreList`)

## 5. Effort estimé

**S-M** (1-2 PR) :
- PR 1 : table `ignored_players` + `IgnoreList` C++ + opcodes
  Ignore Add/Remove/List + tests
- PR 2 : colonne `note` sur `friends` + opcode SetNote + intégration
  filtrage chat (consomme ChatGate quand CMANGOS.01 livré)

Wire-breaking probable (nouveaux opcodes ignore). Migration DB simple.

## 6. Valeur joueur/serveur

**Élevée** — feature visible joueur attendue (UX standard MMO).
Filtrage ignored = anti-toxicité.

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.06 Accounts** — pour identifier les joueurs (déjà OK)
- **CMANGOS.13 Database** — persistance (déjà OK via ConnectionPool)
- **CMANGOS.01 Chat** — pour filtrage ignored côté chat-relay

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — table `ignored_players` ou alternative
  (réutiliser `friends.status` avec valeur "ignored" ?). Décision
  archi : table dédiée vs colonne sur friends.
- ⚠️ **Wire-breaking** — opcodes Ignore. Bump `kProtocolVersion` +
  lock-step.
- ⚠️ **Ignored cap** — limiter (50 ? 100 ?) pour éviter abus.
- ⚠️ **Filtrage chat-relay** — intégration ChatGate côté expéditeur
  (cmangos pattern). Économise réseau mais leak l'ignore (le sender
  voit que son message est rejeté ? ou disparu silencieusement ?).
  Politique à acter.
- ⚠️ **Notes privées privacy** — note attachée à un friend. Si la
  relation se termine, garder la note ? La supprimer ?
- ⚠️ **Réplication client** — le client maintient le cache local
  amis/ignorés/notes. Sync au login + delta sur changement.

## 9. Recommandation finale

✅ **Faire en l'état** — extension simple de l'existant, ROI immédiat :

1. **Étape 1** : table `ignored_players` (simple) + `IgnoreList`
   C++ + opcodes Ignore + tests.
2. **Étape 2** : intégration filtrage chat (quand CMANGOS.01 ChatGate
   livré).
3. **Étape 3** : colonne `note` + opcode SetNote (feature mineur,
   peut être différé).

À faire en parallèle de CMANGOS.01 Chat (besoin du filtrage ignored).
Effort minimal, ROI joueur direct.

---

*Audit du 2026-05-08. Mises à jour : —*
