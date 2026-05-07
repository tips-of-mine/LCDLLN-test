# CMANGOS.38_PlayerBot_headless_session_load_test

## Objectif

Implémenter un mode **bot headless** côté shard LCDLLN, inspiré de
`src/game/PlayerBot` cmangos. Adapté à LCDLLN : **pas un binaire séparé**,
mais un mode `--headless-bot` dans le shard qui spawn N `PlayerSession`
factices.

Pilier principal : **WorldSession headless** réutilisant les vrais
handlers serveur — load testing du shard sans client réel, **10× à 100×
plus de bots qu'un vrai client** (pas de sérialisation réseau, pas de
rendu, pas de client process).

C'est un **P3 shard** (outil dev/QA, pas pour prod).

## Dépendances

- M00.1 (build base)
- CMANGOS.02 (Entities — Player headless)
- CMANGOS.04 (Movement — bot peut se déplacer)
- CMANGOS.07 (AI — bot a une stratégie)

## Livrables

### Côté shard (`engine/server/shard/headless/`)

- `HeadlessSession.{h,cpp}` :
  ```cpp
  class HeadlessSession : public PlayerSession {
  public:
    HeadlessSession(uint32_t botId, BotStrategy strat);
    void Tick(int32_t dtMs);            // décide actions, push opcodes
    // Override Send : ne sérialise pas, accumule en RAM si nécessaire
    void Send(uint16_t opcode, std::span<const uint8_t> payload) override;
  private:
    BotStrategy m_strategy;
    uint32_t m_botId;
  };
  ```
- `BotStrategy.h` (interface composable) :
  ```cpp
  class BotStrategy {
  public:
    virtual void OnTick(HeadlessSession&) = 0;
  };
  // Stratégies concrètes :
  class IdleStrategy        : public BotStrategy {};
  class RandomMoveStrategy  : public BotStrategy {};
  class CombatTrainingStrategy : public BotStrategy {};
  class FollowPathStrategy  : public BotStrategy {};
  ```
- `HeadlessBotManager.{h,cpp}` :
  - `void SpawnBots(int count, BotConfig cfg)`
  - `void DespawnAll()`
  - `void Tick(int32_t dtMs)` — ticke tous les bots.
- `--headless-bot=N` CLI option dans `main_shard_linux.cpp`.

### Configuration (`config.json`)

```json
"headless_bot": {
  "enabled": false,
  "default_count": 0,
  "spawn_position_x": 0.0,
  "spawn_position_y": 0.0,
  "spawn_position_z": 100.0,
  "default_strategy": "RandomMove",
  "tick_rate_hz": 10
}
```

### Tests

- `HeadlessSessionTests.cpp` — créer 100 sessions, ticke, vérifier déplacements.
- `HeadlessBotLoadTest.cpp` — load test 1000 bots, mesurer latence du tick.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. WorldSession headless

Hérite de la `PlayerSession` standard mais :

- `Send()` ne fait rien (ou stocke localement pour vérif tests).
- Pas de socket, pas de TLS, pas d'écriture réseau.
- Les opcodes que le bot envoie passent directement aux handlers serveur
  via une fonction commune (pas via la pipe socket).

Gain : 1 bot ≈ 100 KB RAM vs 5-20 MB pour un vrai client. 10× à 100×
plus de bots possibles.

### 2. BotStrategy composable

Permet de tester différents profils de charge :

- `IdleStrategy` : login, stay AFK. Test : combien de connexions idle peut tenir le shard.
- `RandomMoveStrategy` : se déplace aléatoirement dans la zone. Test : charge mouvement + AoI.
- `CombatTrainingStrategy` : aggro un PNJ d'entraînement, cycle attack/heal. Test : charge combat + spell.
- `FollowPathStrategy` : suit un waypoint preset. Test : déterministe, replay.

### 3. CLI

```
./lcdlln_shard --headless-bot=500 --bot-strategy=RandomMove
```

Spawn 500 bots à l'enter world. Utile pour CI ou benchmark local.

## Étapes d'implémentation

1. Créer `engine/server/shard/headless/`.
2. Implémenter `HeadlessSession` (hérite PlayerSession, override Send).
3. Implémenter `BotStrategy` interface + 2-3 stratégies.
4. Implémenter `HeadlessBotManager` + tick.
5. Câbler CLI `--headless-bot=N` dans `main_shard_linux.cpp`.
6. Tests : 2 fichiers.
7. Doc : section « Headless bots » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test : `--headless-bot=100` spawn 100 sessions, le shard tick stable à 10 Hz, RAM raisonnable
- [ ] Load test : 1000 bots `RandomMove` → tick < 200ms p99
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Pas un vrai client** : un bot ne valide pas la chaîne complète client-serveur. Les bugs purement client (rendu Vulkan, ImGui) ne sont pas détectables. Bot = test charge serveur uniquement.
- **Authentification** : les bots doivent avoir des comptes en DB (créer N comptes test). Ne **pas** by-passer l'auth — le bot teste la chaîne réelle.
- **Cleanup** : à `DespawnAll`, properly logout chaque bot pour ne pas laisser de sessions fantômes.
- **Compétition avec vrais joueurs** : ne **pas** activer en prod. Verrou via config + log warning si activé en prod.
- **Stratégies mixtes** : à terme, mixer 30% Idle / 50% RandomMove / 20% Combat pour simuler un workload réaliste.
- **Réinjection des paquets** : le bot court-circuite la couche réseau. Ça veut dire que les bugs liés à la sérialisation/désérialisation ne sont pas testés — utiliser un vrai client pour ces cas.

## Références

- `CMANGOS_ANALYSIS.md` § PlayerBot (P3 shard)
- cmangos `src/game/PlayerBot/Base/`, `AI/`
