# SERVER-CORE.15_Groups_groupref_loot_rules_master

> **Etat : PARTIEL** (verifie 2026-06-03)
> - Fait / preuves : src/masterd/Groups/Group.h;src/masterd/Groups/GroupManager.h
> - Manque : GroupReference/loot rules/raid absents
> - Resume : Groups partiel

## Objectif

Mettre en place les **groupes/raids de joueurs** côté master LCDLLN,
inspirés de `src/game/Groups` server-core. Quatre piliers :

1. **`GroupReference` / `GroupRefManager` intrusive list** : un Player
   détient une `GroupReference` qui s'invalide automatiquement quand
   le groupe se dissout. Pattern intrusive list évitant les dangling
   pointers, sans coût atomique `shared_ptr`.
2. **Loot rules par groupe** (FreeForAll, RoundRobin, MasterLooter,
   NeedBeforeGreed) encodées comme enum + politique pluggable. Ajouter
   une nouvelle règle = ajouter une stratégie.
3. **Persistance partielle** : seuls les raids permanents sont en DB,
   les groupes 5-man sont in-memory volatiles — économise des
   écritures.
4. **Cross-shard ready** : un groupe est un objet **master-side**
   (logique sociale), les positions des membres viennent du shard —
   séparation naturelle pour LCDLLN.

C'est un **P2 master**, pré-requis pour PvE de groupe et raid.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.06 (Accounts)
- SERVER-CORE.13 (Database — pour persistance raid permanente)
- SERVER-CORE.17 (Loot — règles de groupe)

## Livrables

### Côté master (`engine/server/groups/`)

- `Group.{h,cpp}` :
  ```cpp
  class Group {
  public:
    enum class Type : uint8 { Party, Raid };
    enum class LootMethod : uint8 { FreeForAll, RoundRobin, MasterLooter, NeedBeforeGreed };

    bool AddMember(uint32_t accountId, uint64_t characterId);
    bool RemoveMember(uint32_t accountId);
    void Disband();
    void SetLeader(uint32_t accountId);
    void SetLootMethod(LootMethod m);
    bool ConvertToRaid();
    Type GetType() const;
    LootMethod GetLootMethod() const;
    std::span<GroupMember const> GetMembers() const;
  private:
    uint64_t m_groupId;
    Type m_type;
    LootMethod m_lootMethod;
    uint32_t m_leaderAccountId;
    std::vector<GroupMember> m_members;
    bool m_persistent;        // raid persisté
  };
  ```
- `GroupReference.{h,cpp}` (templated intrusive list) — quand un Player
  rejoint un Group, il porte un `GroupReference` qui pointe vers Group.
  Auto-removed à la destruction.
- `GroupManager.{h,cpp}` :
  - `Group* CreateGroup(leaderAccountId, Type)`
  - `void DisbandGroup(uint64_t groupId)`
  - `Group* GetGroupForAccount(uint32_t accountId)`
- `GroupHandler.{h,cpp}` — opcodes :
  - `kOpcodeGroupInvite`, `kOpcodeGroupAccept`, `kOpcodeGroupDecline`
  - `kOpcodeGroupKick`, `kOpcodeGroupLeave`
  - `kOpcodeGroupLootMethod`, `kOpcodeGroupSetLeader`
  - `kOpcodeGroupConvertToRaid`

### Migration DB

```sql
CREATE TABLE persistent_group (
  group_id            BIGINT UNSIGNED NOT NULL,
  type                TINYINT UNSIGNED NOT NULL,
  loot_method         TINYINT UNSIGNED NOT NULL,
  leader_account_id   INT UNSIGNED NOT NULL,
  created_ts          BIGINT NOT NULL,
  PRIMARY KEY (group_id)
);

CREATE TABLE persistent_group_member (
  group_id            BIGINT UNSIGNED NOT NULL,
  account_id          INT UNSIGNED NOT NULL,
  character_id        BIGINT UNSIGNED NOT NULL,
  joined_ts           BIGINT NOT NULL,
  PRIMARY KEY (group_id, account_id)
);
```

### Configuration (`config.json`)

```json
"group": {
  "max_party_members": 5,
  "max_raid_members": 40,
  "default_loot_method": "RoundRobin",
  "raid_persistence_default": false,
  "invite_timeout_sec": 60
}
```

### Tests

- `GroupReferenceTests.cpp` — invalidation automatique à Disband.
- `GroupManagerTests.cpp` — create + add + kick + leave.
- `GroupLootRulesTests.cpp` — chaque rule applique correctement.
- `GroupPersistenceTests.cpp` — raid permanent survit reboot.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. GroupReference intrusive

```cpp
template <class T>
class GroupReference {
public:
  ~GroupReference() { Unlink(); }
  void Link(T* group);
  void Unlink();
private:
  T* m_target = nullptr;
  GroupReference* m_next = nullptr;
  GroupReference* m_prev = nullptr;
};
```

Quand `Group::Disband()` est appelé, parcours les `GroupReference` de
ses membres et les unlink. Aucun pointeur dangling, pas besoin d'atomic.

### 2. Persistance partielle

| Type | Persisté |
|---|---|
| Party (5-man) | Non — disparait au logout du dernier membre |
| Raid (40-man) | Optionnel : si `raid_persistence_default` true ou commande explicite, persiste en DB |

Cohérent avec WoW : groupes éphémères = pas de trace ; raids = lock à
la mort d'un boss = persistance.

### 3. Cross-shard

Groupe vit sur le master, **pas** sur le shard. Le shard a une vue
read-only « ce joueur appartient au groupe X », via une notification
master → shard à chaque changement.

Conséquence : pour calculer `IsInGroupWith(player)` côté shard, on
maintient une copie locale du groupId par session shard. Push depuis
master au login + à chaque join/leave.

### 4. Loot rule application

```cpp
// SERVER-CORE.17 (Loot) consulte le group du killer pour déterminer la rule.
auto* group = g_groups.GetGroupForAccount(killer.GetAccountId());
auto rule = group ? group->GetLootMethod() : LootMethod::FreeForAll;
ApplyLootRule(rule, group->GetMembers(), lootPile);
```

## Étapes d'implémentation

1. Créer `engine/server/groups/`.
2. Implémenter `GroupReference` (intrusive list).
3. Implémenter `Group` + `GroupMember`.
4. Implémenter `GroupManager` (create/destroy/lookup).
5. Implémenter opcodes + handler (invite/accept/leave/kick/etc.).
6. Implémenter persistance raid.
7. Câbler avec Loot (SERVER-CORE.17).
8. Tests : 4 fichiers.
9. Doc : section « Groups master » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master)
- [ ] Tests passent
- [ ] Smoke test : create party → invite + accept → leader passe les loot rules → kick fonctionne
- [ ] GroupReference auto-cleanup à Disband (pas de dangling pointer)
- [ ] Raid permanent : member kill boss → boss-kill enregistré pour tous les membres présents
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Invite timeout** : un invite non accepté en 60s expire. Cleanup côté master.
- **Invite cross-account** : un account peut être dans plusieurs personnages mais un seul group à la fois.
- **Logout in group** : si tous les membres logout, le party (volatile) disparait. Le raid persistant reste en DB.
- **Kick from raid lock** : un joueur kick d'un raid avec lock de boss reste lock — il garde l'instance, ne peut plus rentrer mais le ID raid reste.
- **Master Looter offline** : si le master looter logout, fall back à RoundRobin pour cette session.
- **GroupReference threading** : le master est mono-thread pour la logique groupe (typique). Si on parallèlise, mutex sur Group + GroupRefManager.
- **Notif shard** : à chaque change groupe, batcher les notifs (ne pas envoyer 5 paquets à 5 shards si tout est local).

## Références

- `SERVER-CORE_ANALYSIS.md` § Groups (P2 master)
- server-core `src/game/Groups/Group.cpp`, `GroupHandler.cpp`,
  `GroupReference.h`, `GroupRefManager.h`
