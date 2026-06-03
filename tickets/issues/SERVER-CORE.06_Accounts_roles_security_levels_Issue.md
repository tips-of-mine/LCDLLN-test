# Issue: SERVER-CORE.06

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/masterd/account/AccountRoleService.h
- src/masterd/account/AccountRoleServiceTests.cpp

## Note
Roles/security levels + tests

---

## Contenu du ticket (SERVER-CORE.06)

# SERVER-CORE.06_Accounts_roles_security_levels

## Objectif

Doter le système de comptes LCDLLN d'une **hiérarchie de rôles** au-delà
du simple booléen `is_gm`, inspiré de `src/game/Accounts/AccountMgr` de
server-core. Bénéfices :

1. **Granularité** : un modérateur peut mute/kick mais pas ban ; un GM
   peut spawn des items en test mais pas en prod ; etc.
2. **Pré-requis Chat** : le `ChatCommandRouter` (SERVER-CORE.01) attend une
   fonction `GetAccountRole(accountId)` pour vérifier `minRole` par
   commande.
3. **`HasLowerSecurity` invariant** : helper unique câblé devant **toute**
   action affectant un autre compte (kick, mute, whisper privé d'un GM
   caché). Empêche les escalades de privilèges même quand un handler a
   un bug logique.
4. **Séparation logique / persistance** : `AccountStore` continue de
   gérer la persistance ; un nouvel `AccountRoleService` (ou méthodes
   dans AccountStore) expose la logique.

C'est un **P2 master**, pré-requis pour Chat (SERVER-CORE.01) et tout futur
système GM in-game.

## Dépendances

- M00.1 (build base)
- `engine/server/AccountStore` (déjà existant)
- Pré-requis pour : SERVER-CORE.01 (Chat), tout futur ticket GM (SERVER-CORE.31 GMTickets, etc.)

## Livrables

### Côté master (`engine/server/`)

- `AccountRole.h` — enum class C++20 type-safe :

  ```cpp
  enum class AccountRole : uint8_t {
    Player        = 0,
    Moderator     = 1,
    GameMaster    = 2,
    Administrator = 3,
    Console       = 4,    // commandes lancées depuis stdin du process serveur
  };
  ```

  Constraints :
  - Numérotation **monotone croissante** (un rôle plus haut a plus de droits).
  - **Toujours** comparer via `static_cast<uint8>(a) >= static_cast<uint8>(b)` ou helper.

- `AccountRoleService.{h,cpp}` (ou intégré dans `AccountStore`) :

  ```cpp
  class AccountRoleService {
  public:
    AccountRole GetRole(uint32_t accountId) const;
    void SetRole(uint32_t accountId, AccountRole newRole);
    bool HasLowerSecurity(uint32_t targetAccountId, uint32_t sourceAccountId) const;
    bool RequireMinRole(uint32_t accountId, AccountRole min) const;
  };
  ```

  - `HasLowerSecurity(target, source)` retourne `true` si `target.role < source.role`.
    **Inverse** : si target ≥ source → false (refus de l'action).
  - `RequireMinRole(account, min)` retourne `true` si `account.role >= min`.

- Cache mémoire dans `AccountStore` : `std::unordered_map<uint32_t, AccountRole>`,
  chargé au boot, rafraîchi à `SetRole`. Évite N requêtes DB par session GM
  active.

### Migration DB

- `engine/server/migrations/00xx_account_roles.sql` :

  ```sql
  ALTER TABLE accounts
    ADD COLUMN role TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER email_verified;

  -- Migration des données : si is_gm existait, le mapper.
  UPDATE accounts SET role = 2 WHERE is_gm = 1;   -- GameMaster
  -- (suppression de is_gm dans une migration ultérieure pour garder backward compat).

  CREATE INDEX idx_accounts_role ON accounts(role);
  ```

  Idempotente (`IF NOT EXISTS` patterns à suivre).

### Configuration (`config.json`)

```json
"accounts": {
  "console_role_required": true,
  "default_new_account_role": "Player",
  "role_change_audit": true
}
```

### Audit log

- Toute appel à `SetRole` produit une ligne dans le SecurityAuditLog
  existant (déjà en place côté master) : `[AUDIT] role_change account_id=X
  old=Player new=Moderator by=accountId=42`.

### Tests

- `engine/server/AccountRoleServiceTests.cpp` :
  - `GetRole` retourne le rôle stocké.
  - `SetRole` met à jour DB + cache.
  - `HasLowerSecurity` :
    - Player vs Moderator → true
    - Moderator vs Player → false
    - GameMaster vs GameMaster → false (égalité = refus)
  - `RequireMinRole(Player, Moderator)` → false ; `RequireMinRole(GM, Moderator)` → true.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- Contenu : N/A
- Outils offline : N/A
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Sémantique des rôles

| Rôle | Valeur | Capacités typiques |
|---|---|---|
| `Player` | 0 | Aucune commande GM. Chat normal, gameplay. |
| `Moderator` | 1 | `.mute`, `.kick`, `.warn`. Pas de ban, pas de spawn. |
| `GameMaster` | 2 | Commandes Moderator + `.ban`, `.tele`, `.spawn` (test items), `.go`. |
| `Administrator` | 3 | Commandes GM + `.account create`, `.account delete`, `.set role`, accès aux logs. |
| `Console` | 4 | Toutes commandes + opérations dangereuses (shutdown, reload all). Réservé au stdin du process serveur. |

### 2. `HasLowerSecurity` — règle d'or

**Toute** action affectant un autre compte :
- ban / unban
- kick
- mute / unmute
- whisper à un GM caché (`.gm visible off`) — refus si GM a rôle plus haut
- inspect inventaire / mail
- set role

doit appeler `HasLowerSecurity(targetAccountId, sourceAccountId)` au début
du handler. Si `false` → refuser avec message d'erreur + log audit.

```cpp
bool HandleBanAccount(ChatContext const& ctx, std::string_view args) {
  uint32_t targetId = ParseAccountId(args);
  if (!g_accountRoles.HasLowerSecurity(targetId, ctx.callerAccountId)) {
    ctx.Reply("Permission denied: target has equal or higher security.");
    LOG_WARN(Auth, "[AUDIT] denied_ban target={} by={}", targetId, ctx.callerAccountId);
    return false;
  }
  // ... ban logic
}
```

### 3. Égalité = refus

Choix délibéré : un GM ne peut pas ban un autre GM. Ça force l'usage du
rôle Administrator pour les actions inter-GM, qui est plus rarement
attribué et nécessite une autorité explicite.

### 4. Console role

Le rôle `Console` n'est **jamais** stocké en DB. Il est attribué
runtime à un caller spécial qui exécute des commandes depuis stdin du
process serveur (équivalent du « rcon » de server-core). Une commande de
console n'a pas d'`accountId` — utiliser un sentinel `0xFFFFFFFF` ou
flag dédié dans le `ChatContext`.

### 5. Cache + invalidation

- Cache chargé au boot via `LoadAllRoles()` (1 requête : `SELECT id, role FROM accounts`).
- À `SetRole(id, role)` : update DB + update cache.
- Si une autre instance master modifie le rôle (improbable mais possible
  dans un futur multi-master), pas de propagation pour l'instant.
  **TODO** : ajouter un canal pub/sub Redis ou notification SQL si
  multi-master arrive.

## Étapes d'implémentation

1. **Créer `engine/server/AccountRole.h`** avec l'enum.
2. **Créer la migration** `00xx_account_roles.sql` (idempotente).
3. **Implémenter `AccountRoleService`** ou intégrer dans `AccountStore`.
4. **Charger le cache au boot** (`AccountStore::LoadAllRoles()`).
5. **Implémenter `HasLowerSecurity` + `RequireMinRole`** + tests.
6. **Câbler dans `SecurityAuditLog`** pour tracer les `SetRole`.
7. **Exposer via Config** (`accounts.default_new_account_role`).
8. **Doc** : section « AccountRole » dans `CODEBASE_MAP.md` + lister les commandes par rôle.

## Definition of Done (DoD)

- [ ] Build Linux OK (master) via presets existants
- [ ] Tests `AccountRoleServiceTests` passent (5+ cas)
- [ ] Migration `00xx_account_roles.sql` appliquée et idempotente
- [ ] Cache rôles chargé au boot, log info `[AccountRoles] loaded N entries`
- [ ] Smoke test : `SetRole(42, GameMaster)` → `GetRole(42) == GameMaster` + ligne audit log
- [ ] `HasLowerSecurity(playerA, gmB)` → true ; `HasLowerSecurity(gmA, gmB)` → false
- [ ] Aucun nouveau dossier racine non autorisé créé
- [ ] Rapport final : fichiers modifiés + commandes + résultats + DoD

## Notes / pièges à éviter

- **Comparaison de rôles** : ne **jamais** comparer des `AccountRole` via
  `==`/`!=` pour des contrôles d'accès. Toujours `static_cast<uint8>(a)
  >= static_cast<uint8>(b)` ou le helper. `==` sur enum class est ok
  pour vérifier un rôle exact mais sémantiquement faux pour autoriser.
- **Sentinel `0xFFFFFFFF` pour Console** : ne pas le persister en DB.
  Si un bug le persiste, le `LoadAllRoles` doit le rejeter avec un
  warning et le traiter comme `Player` par défaut.
- **Migration des données existantes** : la table `accounts` actuelle
  n'a probablement pas de colonne `is_gm`. Adapter le `UPDATE` dans la
  migration au schéma réel (potentiellement aucun update — tous les
  comptes existants restent `Player` = 0).
- **Backward compat handler GM existants** : si du code actuel teste
  `account.is_gm`, le wrapper `IsGm()` sur `AccountStore` peut renvoyer
  `role >= GameMaster`. À supprimer une fois tout migré.
- **Audit log** : ne pas mettre le `role_change` dans le log standard
  bruyant ; utiliser un sous-système dédié (`SecurityAuditLog` existe
  déjà côté master). Filtrable via `LogFilter::Auth` (déjà dispo PR #468).
- **UX d'erreur** : un message « Permission denied: target has equal or
  higher security » est explicite mais peut leak l'existence du target.
  Pour les whisper à GM caché, retourner « Player not found » à la
  place — pas d'info-leak.
- **Rôle dans le token de session** : ne **pas** dupliquer le rôle dans
  le SessionToken. Le master fait toujours le lookup via
  `AccountRoleService.GetRole(accountId)` à chaque action sensible.
  Sinon une élévation de privilège ne prend pas effet immédiatement.
- **Pas de règle « plus haut peut tout faire de plus bas » côté code** :
  ne pas écrire `if (role >= GameMaster) { all_moderator_commands = true }`.
  Plutôt enregistrer chaque commande avec son `minRole` dans le
  `ChatCommandRouter` ; lire le rôle requis depuis la table.

## Références

- `SERVER-CORE_ANALYSIS.md` § Accounts (P2 master)
- server-core `src/game/Accounts/AccountMgr.cpp`, `AccountMgr.h`
- À coordonner avec : SERVER-CORE.01 (Chat — `ChatCommandRouter` consomme
  `AccountRole`), `engine/server/SecurityAuditLog`
