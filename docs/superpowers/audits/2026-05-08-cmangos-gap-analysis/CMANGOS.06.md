# CMANGOS.06 — Accounts (roles / security levels)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.06_Accounts_roles_security_levels.md](../../../../tickets/CMANGOS/CMANGOS.06_Accounts_roles_security_levels.md)
> **Priorité** : P2 — pré-requis Chat
> **Cible** : master

## 1. Statut implémentation

🟡 **Partiel** — un `role` ENUM existe sur `accounts` (`'player'`/`'admin'`,
binaire), mais la hiérarchie 5 niveaux (`Player`/`Moderator`/`GameMaster`/
`Administrator`/`Console`) et l'API `HasLowerSecurity` / `RequireMinRole`
ne sont pas implémentées.

## 2. Preuves dans le code

**Existant :**
- [db/migrations/0023_accounts_profile.sql](../../../../db/migrations/0023_accounts_profile.sql) — colonne `role ENUM('player','admin')
  NOT NULL DEFAULT 'player'` ajoutée à `accounts` (idempotent)
- [engine/server/SecurityAuditLog.h](../../../../engine/server/SecurityAuditLog.h) + `.cpp` — sous-système audit déjà
  en place (réutilisable pour `role_change`)
- [engine/server/AccountStore.h](../../../../engine/server/AccountStore.h) — store comptes (cache, DB sync)
- [engine/server/AuthRegisterHandler.cpp](../../../../engine/server/AuthRegisterHandler.cpp) — création de compte (à modifier
  pour défaut `Player`)
- [engine/server/SecurityTests.cpp](../../../../engine/server/SecurityTests.cpp) — emplacement existant pour tests sécu

**Manquant (vs spec ticket) :**
- ❌ `engine/server/AccountRole.h` — enum class C++20 type-safe
  (5 valeurs : `Player`/`Moderator`/`GameMaster`/`Administrator`/`Console`)
- ❌ `AccountRoleService` (ou intégration `AccountStore`) avec API
  `GetRole`/`SetRole`/`HasLowerSecurity`/`RequireMinRole`
- ❌ Cache mémoire `unordered_map<accountId, AccountRole>` chargé au boot
- ❌ Helper `HasLowerSecurity(target, source)` — l'invariant de sécurité
- ❌ Audit log `role_change` câblé sur `SetRole`
- ❌ Sentinel `Console` (0xFFFFFFFF) pour stdin du process serveur
- ❌ Migration extension de l'ENUM existant (`'player','admin'` →
  `TINYINT UNSIGNED` à 5 valeurs, ou ENUM élargi)
- ❌ Config `accounts.default_new_account_role`, `accounts.console_role_required`,
  `accounts.role_change_audit`

## 3. Recouvrement milestones existantes

❌ **Non couvert** — pas de milestone dédiée à la hiérarchie de rôles.
M33 (security/auth) couvre l'auth/anti-bot mais pas les niveaux GM/Mod.

## 4. Écart par rapport à la spec CMANGOS

L'écart est **modéré** : on part d'une base existante (`role` binaire +
audit log + AccountStore + tests sécu). Les ajouts sont :
- Étendre la colonne `role` à 5 valeurs (migration)
- Créer l'enum + service + helpers de comparaison
- Câbler le cache mémoire au boot
- Câbler l'audit sur `SetRole`

Les patterns de "égalité = refus" et "tout test passe par `HasLowerSecurity`"
sont **les invariants critiques** à respecter — pas du sucre cosmétique. Les
oublis ici créent des escalades de privilège.

## 5. Effort estimé

**M** (2-3 PR) :
- PR 1 : enum + service + helpers + tests (sans cache, version DB-only)
- PR 2 : migration `role` ENUM élargi + cache au boot + audit câblé
- PR 3 : intégration `ChatCommandRouter` (CMANGOS.01) + handlers GM existants
  qui consomment `HasLowerSecurity`

Pas de wire-breaking. Migration DB simple (ALTER COLUMN sur ENUM).

## 6. Valeur joueur/serveur

**Élevée** — pré-requis explicite de CMANGOS.01 Chat (`ChatCommandRouter`
attend `GetAccountRole`). Sans CMANGOS.06, on ne peut pas livrer le dispatch
de commandes GM hiérarchique. Aussi pré-requis pour CMANGOS.32 GMTickets et
tout futur outillage GM.

Valeur **invisible joueur** mais **critique opération** (granularité
modération, prévention escalades).

## 7. Dépendances bloquantes

- **AccountStore existant** — déjà en place, à étendre.
- **Aucune dépendance bloquante non livrée**.
- **Bloque** : CMANGOS.01 Chat (CommandRouter), CMANGOS.32 GMTickets.

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — extension de l'ENUM `'player','admin'` vers une
  hiérarchie 5 valeurs. Doit être idempotente. **Décision** : garder
  ENUM (lisible) ou passer à TINYINT UNSIGNED (plus extensible). Le
  ticket suggère TINYINT, à valider.
- ⚠️ **Sécurité** — toute action affectant un autre compte doit passer par
  `HasLowerSecurity`. Un oubli = escalade de privilège.
- ⚠️ **Égalité = refus** — invariant critique : un GM ne peut pas ban un
  autre GM. Force l'usage du rôle Administrator pour inter-GM.
- ⚠️ **Sentinel Console** (`0xFFFFFFFF`) ne **jamais** persister en DB. Un
  bug qui le persiste doit être détecté au load (warning + traité comme
  Player).
- ⚠️ **Pas de duplication dans SessionToken** — le rôle doit toujours être
  re-lu via `AccountRoleService.GetRole()` à chaque action sensible. Sinon
  une élévation/dégradation ne prend pas effet immédiatement.
- ⚠️ **UX d'erreur** — message "Permission denied: target has equal or higher
  security" peut leak l'existence du target. Pour whisper à GM caché,
  préférer "Player not found".
- ⚠️ **Audit log** — utiliser `SecurityAuditLog` (déjà en place), pas le log
  standard bruyant.
- ⚠️ **Backward compat** — wrapper `IsGm()` sur `AccountStore` qui renvoie
  `role >= GameMaster` pour le code existant qui teste `is_gm`. Suppression
  ultérieure une fois tout migré.

## 9. Recommandation finale

✅ **Faire en l'état**, en priorité élevée — c'est un pré-requis simple
pour débloquer CMANGOS.01 Chat (et tout le futur outillage GM) :

1. **Étape 1** : ajouter migration `00xx_account_roles_extend.sql` qui
   change `role ENUM('player','admin')` → 5 valeurs (ou TINYINT). Idempotente.
2. **Étape 2** : créer `engine/server/AccountRole.h` + `AccountRoleService`
   (peut être intégré dans `AccountStore` si plus simple).
3. **Étape 3** : implémenter `HasLowerSecurity` + `RequireMinRole` + tests
   exhaustifs (les 5 cas listés au ticket).
4. **Étape 4** : câbler le cache au boot (`AccountStore::LoadAllRoles()`)
   + câbler audit `SecurityAuditLog::LogRoleChange`.
5. **Étape 5** : exposer dans `ChatContext` (pour préparer le câblage
   `ChatCommandRouter` quand CMANGOS.01 arrive).

À faire **avant** CMANGOS.01 Chat (déblocant explicite). Effort raisonnable
(M), risque sécurité bien borné si on respecte la règle "tout passe par
`HasLowerSecurity`".

---

*Audit du 2026-05-08. Mises à jour : —*
