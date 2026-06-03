# SERVER-CORE.12_Server_packetlog_opcodetable_dbcstores

> **Etat : PARTIEL** (verifie 2026-06-03)
> - Fait / preuves : src/shared/network/PacketLog.h
> - Manque : OpcodeRegistry/replay tool absents
> - Resume : Packetlog partiel

## Objectif

Mettre en place les **patterns infra serveur** pour LCDLLN, inspirés de
`src/game/Server` server-core. Trois piliers :

1. **`PacketLog` rejouable** : capture binaire de toutes les frames
   réseau, rejouable offline. Inestimable pour debug protocole et
   reproduire des bugs joueurs ("envoie-moi ton .pktlog").
2. **Table d'opcodes typée** : `Opcodes.cpp` = tableau statique
   `{opcode, name, status, processing, handler}`. Permet logging
   unifié, throttling par opcode, stats. Modèle direct pour
   `OpcodeRegistry` côté master/shard.
3. **`DBCStores`** : préchargement RAM-mapped des fichiers de données
   statiques au boot. Pour LCDLLN équivalent = vos data tables compilées
   (terrain, items, sorts) chargées une fois au boot du shard.

C'est un **P2 cross master+shard**, gain QoL prod important.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.13 (Database — pour les vraies stores SQL)
- Logger (PR #468) — pour activer/désactiver via `LogFilter::PacketIo`

## Livrables

### Couche partagée (`engine/network/`)

- `OpcodeRegistry.{h,cpp}` :
  ```cpp
  enum class OpcodeStatus : uint8 {
    Loaded,           // pas encore handler-bound
    UnHandled,        // ignored intentionnellement
    Active,           // dispatché normalement
    Deprecated,       // log warning si reçu
  };
  enum class OpcodeProcessing : uint8 {
    InPlace,          // exécuté tout de suite dans le worker IO
    InMapTick,        // queue pour tick map (si shard)
    InMasterTick,     // queue pour tick master
  };
  struct OpcodeMeta {
    uint16_t code;
    std::string_view name;
    OpcodeStatus status;
    OpcodeProcessing processing;
  };
  class OpcodeRegistry {
  public:
    static void Register(OpcodeMeta meta);
    static OpcodeMeta const* Get(uint16_t code);
  };
  ```
- `PacketLog.{h,cpp}` :
  - `void Open(std::filesystem::path file)` — démarre la capture.
  - `void Close()`
  - `void LogPacket(Direction dir, uint16_t opcode, std::span<const uint8_t> payload)` — append au fichier.
  - `void Replay(std::filesystem::path file, std::function<void(Direction, uint16_t, std::span<const uint8_t>)> consumer)` — rejouer offline.

### Outil offline (`tools/packet_replay/`)

- `tools/packet_replay/packet_replay.cpp` — outil qui prend un `.pktlog`
  + un fichier de breakpoints, rejoue dans un mock client, affiche
  l'état observable.

### Configuration (`config.json`)

```json
"server": {
  "packet_log_enabled": false,
  "packet_log_path": "logs/packets.pktlog",
  "packet_log_max_size_mb": 100,
  "opcode_stats_enabled": false
}
```

### Tests

- `OpcodeRegistryTests.cpp` — register + lookup, double register détecté.
- `PacketLogTests.cpp` — round-trip : enregistrer N paquets, replay, vérifier identique.
- `PacketLogRotationTests.cpp` — taille max → rotation.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- Outils offline : uniquement sous `/tools` (`tools/packet_replay/`)
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Format `.pktlog`

```
[ Magic 4 bytes "LPKT" ]
[ Version uint16 ]
[ Header 16 bytes : start_ts, server_id, etc. ]
[ Records sequentiels :
    [ Direction 1 byte (0=IN, 1=OUT) ]
    [ Timestamp delta ms (uint32) ]
    [ Connection ID (uint32) ]
    [ Opcode (uint16) ]
    [ Payload size (uint16) ]
    [ Payload (bytes) ]
]
```

Compact, append-only. Rotation à `packet_log_max_size_mb`.

### 2. OpcodeRegistry

Au boot, chaque module (auth, chat, character, etc.) appelle
`OpcodeRegistry::Register({kOpcodeXxx, "XXX", Active, InPlace})`. Le
registry valide l'unicité.

Gain :
- Logging unifié : `LOG_FILTERED(Debug, PacketIo, Net, "RX {} (0x{:04x})", reg->name, opcode)`.
- Throttling : si un opcode reçoit > N msgs/sec, log warn.
- Stats : compteur par opcode, exposable via `/metrics`.

### 3. Replay

```cpp
PacketLog::Replay("crash.pktlog", [](Direction dir, uint16_t opcode, auto payload) {
  // Reproduire l'état avec tous les paquets pré-crash
  if (dir == Direction::In) {
    g_master->HandlePacket(opcode, payload);
  }
});
```

Cas d'usage : un joueur reproduit un crash, télécharge son `.pktlog`,
tu lances localement avec `replay foo.pktlog --break-at=line=1234`,
tu vois exactement ce qui plante.

### 4. DBCStores équivalent

Pour LCDLLN, pas de fichiers DBC du client (server-core lit des fichiers
extraits du client WoW). Mais pattern réutilisable : préchargement RAM
de tables statiques DB au boot via `SQLStorage` (SERVER-CORE.13). Donc
**rien de spécifique à coder pour DBCStores** — c'est déjà couvert par
le ticket Database.

## Étapes d'implémentation

1. Créer `engine/network/OpcodeRegistry.{h,cpp}`.
2. Migrer la définition des opcodes existants (PR #468 pattern, AuthRegister, Chat etc.) vers `OpcodeRegistry::Register(...)` dans un init module.
3. Implémenter `PacketLog` (open / close / log / rotation).
4. Câbler `NetServer` pour appeler `PacketLog::LogPacket` quand actif.
5. Implémenter `Replay`.
6. Créer `tools/packet_replay/` minimal.
7. Tests : 3 fichiers.
8. Doc : section « Server infra » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master + shard) + outil packet_replay
- [ ] Tests passent
- [ ] Smoke test : activer `packet_log_enabled = true`, faire un échange client-serveur, le `.pktlog` est valide et replay reproduit la séquence
- [ ] OpcodeRegistry : double-register détecté (assert ou log error)
- [ ] Aucun dossier racine non autorisé (sauf `/tools/packet_replay/`)
- [ ] Rapport final

## Notes / pièges à éviter

- **Coût packet log** : append-only à chaque paquet — perf OK si async write. Désactivé par défaut, activable via config.
- **Données sensibles** : `.pktlog` peut contenir mots de passe (au handshake initial). **Sanitize** côté logger (skip auth opcodes ou mask).
- **Format wire-locked** : un .pktlog v1 ne peut pas être rejoué par un serveur v2 avec opcodes différents. Inclure `version` dans le header pour détecter et rejeter avec un message clair.
- **Replay deterministic** : le replay ne reproduit pas la timing exact (un paquet sleep 10s est rejoué instantanément). Pour les bugs liés au timing, utiliser un mode "real-time" qui respecte les delta ts.
- **OpcodeRegistry et duplication maître/shard** : les deux processus doivent enregistrer les mêmes opcodes pour interpréter pareillement. Mettre les `Register(...)` dans une lib commune `engine/network/OpcodeDefinitions.cpp`.
- **DBCStores** : pas de DBC chez nous — n'introduit pas un système séparé de SQLStorage. Si demain on a des fichiers binaires (ex. `.lod` pour terrain), créer `BinaryStores` à part, pas confondre.

## Références

- `SERVER-CORE_ANALYSIS.md` § Server (P2 cross)
- server-core `src/game/Server/Opcodes.cpp`, `WorldPacket.h`,
  `WorldSocket.cpp`, `PacketLog.cpp`, `DBCStores.cpp`
