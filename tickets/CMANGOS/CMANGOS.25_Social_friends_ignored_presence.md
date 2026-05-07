# CMANGOS.25_Social_friends_ignored_presence

## Objectif

Mettre en place le **système social** (amis / ignorés / notes / présence)
côté master LCDLLN, inspiré de `src/game/Social` cmangos. Quatre piliers :

1. **Manager singleton + objet par joueur** : `PlayerSocial` chargé à
   login, déchargé à logout, persisté en DB delta.
2. **Broadcast de présence** : login/logout d'un joueur déclenche
   `BroadcastToFriends()` qui pousse à tous les amis online.
3. **Liste ignorés appliquée côté chat-relay** : filtrage à l'expéditeur
   (pas au récepteur) — économie réseau quand un joueur ignore beaucoup
   de monde.
4. **Notes privées par contact** : champ texte attaché au mapping
   (ami → note). Petit feature mais très demandé.

C'est un **P2 master**, pré-requis dès qu'on a 2+ joueurs et chat
fonctionnel.

## Dépendances

- M00.1 (build base)
- CMANGOS.06 (Accounts)
- CMANGOS.13 (Database)
- CMANGOS.01 (Chat) — pour intégration côté ChatGate (filtrage ignored)

## Livrables

### Côté master (`engine/server/social/`)

- `PlayerSocial.{h,cpp}` :
  ```cpp
  class PlayerSocial {
  public:
    bool AddFriend(uint64_t targetCharacterId, std::string_view note);
    bool RemoveFriend(uint64_t targetCharacterId);
    bool AddIgnore(uint64_t targetCharacterId);
    bool RemoveIgnore(uint64_t targetCharacterId);
    void SetNote(uint64_t targetCharacterId, std::string_view note);
    bool IsFriend(uint64_t targetCharacterId) const;
    bool IsIgnored(uint64_t targetCharacterId) const;
    std::span<const SocialEntry> GetFriends() const;
    std::span<const SocialEntry> GetIgnored() const;
  private:
    uint64_t m_ownerCharacterId;
    std::vector<SocialEntry> m_friends;
    std::vector<SocialEntry> m_ignored;
    bool m_dirty = false;
  };
  ```
- `SocialEntry.h` :
  ```cpp
  struct SocialEntry {
    uint64_t targetCharacterId;
    std::string targetName;          // cache pour affichage
    std::string note;                 // private note
    int64_t addedTs;
  };
  ```
- `SocialManager.{h,cpp}` :
  - `Load(uint64_t characterId)` — load au login.
  - `Save(uint64_t characterId)` — write delta au logout/idle.
  - `void OnPlayerLogin(uint64_t characterId)` — broadcast présence aux amis.
  - `void OnPlayerLogout(uint64_t characterId)` — idem.
- `SocialHandler.{h,cpp}` — opcodes :
  - `kOpcodeFriendList`, `kOpcodeFriendAdd`, `kOpcodeFriendRemove`
  - `kOpcodeIgnoreList`, `kOpcodeIgnoreAdd`, `kOpcodeIgnoreRemove`
  - `kOpcodeFriendNote`
  - `kOpcodeFriendStatusUpdate` (notification online/offline)

### Migration DB

```sql
CREATE TABLE character_social (
  owner_character_id    BIGINT UNSIGNED NOT NULL,
  target_character_id   BIGINT UNSIGNED NOT NULL,
  type                  TINYINT UNSIGNED NOT NULL,    -- 0=friend, 1=ignored
  note                  VARCHAR(64),
  added_ts              BIGINT NOT NULL,
  PRIMARY KEY (owner_character_id, target_character_id, type),
  KEY idx_owner (owner_character_id)
);
```

### Configuration (`config.json`)

```json
"social": {
  "max_friends_per_character": 50,
  "max_ignored_per_character": 50,
  "broadcast_login_logout": true,
  "fuzzy_search_min_chars": 3
}
```

### Tests

- `PlayerSocialTests.cpp` — add/remove friend ; add/remove ignored ; note.
- `SocialManagerTests.cpp` — login broadcast aux amis online.
- `SocialIgnoreFilterTests.cpp` — message d'un joueur ignoré filtré côté ChatGate.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Filtrage ignored côté expéditeur

Quand A envoie un whisper à B :

```cpp
// Côté ChatRelayHandler (master) :
if (g_social.GetForCharacter(B).IsIgnored(A)) {
  // Au lieu de jeter le message côté B (réception), refuser côté A (envoi).
  // L'expéditeur reçoit "[Player B] is ignoring you" ou silence selon politique.
  return;
}
```

Économie : si un raid de 25 personnes a 10 ignorant un troll, ce dernier
envoie 25 paquets pour 15 receivers — préférable que tout passe par
serveur qui filtre, mais déjà 10 paquets économisés en filtrant à
l'envoi via le `ChatGate` (CMANGOS.01).

### 2. Broadcast présence

```cpp
void OnPlayerLogin(uint64_t charId) {
  auto* social = g_social.GetForCharacter(charId);
  for (auto& entry : social->GetFriends()) {
    auto friendSession = g_sessionMgr.FindSessionForCharacter(entry.targetCharacterId);
    if (friendSession) {
      friendSession->Send(kOpcodeFriendStatusUpdate, charId, /* online= */ true);
    }
  }
  // Inverse : envoyer la liste actuelle (avec online flag) au login player.
}
```

### 3. Note privée

```
[Friend list]
- Toto (Online)  -- "Tank de raid, dispo le mercredi"
- Tata (Offline) -- "Pickpocket farm partner"
```

Stockée par le owner uniquement. Le target ne voit pas la note.

### 4. Persistance

Pas un write par modification — write delta au logout ou periodically
(toutes les 5 min) si dirty. Réduit l'I/O DB pour les joueurs qui
toggle ami/ignored fréquemment.

## Étapes d'implémentation

1. Créer `engine/server/social/`.
2. Migration DB.
3. Implémenter `PlayerSocial`.
4. Implémenter `SocialManager` + load/save delta.
5. Implémenter opcodes + handler.
6. Implémenter broadcast présence.
7. Câbler filtrage ignored dans `ChatGate` (CMANGOS.01).
8. Tests : 3 fichiers.
9. Doc : section « Social master » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master)
- [ ] Tests passent
- [ ] Smoke test : 2 players, A ajoute B en friend → A login → B reçoit "A online"
- [ ] A ignore B → B whisper à A → message filtré côté master, pas livré à A
- [ ] Note privée affichée uniquement chez le owner
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Cross-shard** : les amis peuvent être sur des shards différents. Le master gère, les shards relayent juste. Naturel.
- **Friend mutual** : pas de confirmation requise (cmangos style). Si LCDLLN veut Discord-style mutual, ajouter un état `Pending`.
- **Limite raisonnable** : 50 friends + 50 ignored = 100 entrées max par char. Plus = abuse / bot. Refuser au-delà.
- **Fuzzy search** : pour l'auto-complétion (ajout par nom partiel), `fuzzy_search_min_chars = 3`. Évite de scanner tous les players à chaque touche.
- **Renommage character** : si un char est renommé, son nom dans `character_social.target_name` devient stale. Refresh au prochain load ou via hook.
- **Politique ignore-feedback** : silencieux ("vous lui parlez dans le vide") ou explicite ("X vous ignore") ? **Silencieux par défaut** (anti-stalking).
- **Performance broadcast** : 1000 amis online d'un même joueur populaire = 1000 notifs au login. Batcher en 1 paquet `FriendStatusUpdateBatch`.

## Références

- `CMANGOS_ANALYSIS.md` § Social (P2 master)
- cmangos `src/game/Social/SocialMgr.cpp`, `SocialHandler.cpp`
