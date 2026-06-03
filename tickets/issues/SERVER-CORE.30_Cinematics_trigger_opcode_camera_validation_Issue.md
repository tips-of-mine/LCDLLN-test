# Issue: SERVER-CORE.30

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/masterd/cinematics/CinematicSequence.h
- src/shared/network/CinematicPayloads.h

## Note
Cinematics trigger/camera

---

## Contenu du ticket (SERVER-CORE.30)

# SERVER-CORE.30_Cinematics_trigger_opcode_camera_validation

## Objectif

Mettre en place le **système de cinématiques** côté shard+client LCDLLN,
inspiré de `src/game/Cinematics` server-core. Trois piliers :

1. **Trigger via opcode** `SMSG_TRIGGER_CINEMATIC` avec un ID : le
   serveur ne stream rien, le client a déjà l'asset. Pattern « le
   serveur nomme, le client joue ».
2. **Asset client-side** : la cinématique (vidéo, scripted camera path)
   est packagée avec le client.
3. **Parsing M2/cinematic-data côté serveur** uniquement pour les
   chemins de caméra (anticheat : empêcher un téléport pendant la
   cinématique).

C'est un **P3 cross**, pas pour le MVP.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.05 (vmap) — anticheat consulte la position prévue de la caméra

## Livrables

### Côté shard (`engine/server/shard/cinematics/`)

- `CinematicMgr.{h,cpp}` :
  - `Load(ConnectionPool&)` — charge les métadonnées des cinématiques.
  - `Trigger(Player&, uint32_t cinematicId)` — envoie l'opcode + démarre validation.
  - `Update(int32_t dtMs)` — tick : pour chaque cinématique active,
    vérifie que la position du joueur correspond au path attendu (à `± epsilon`).
- `CinematicCameraPath.{h,cpp}` — descripteur de path :
  ```cpp
  struct CinematicCameraPath {
    uint32_t cinematicId;
    std::vector<Vector3> waypoints;
    std::vector<float> timestampsSec;
  };
  ```

### Côté client (`engine/client/cinematics/`)

- `CinematicPlayer.{h,cpp}` — reçoit `SMSG_TRIGGER_CINEMATIC`, joue
  l'asset (vidéo ou scripted camera + animation).

### Migration DB

```sql
CREATE TABLE cinematic_template (
  cinematic_id    INT UNSIGNED NOT NULL,
  asset_path      VARCHAR(255) NOT NULL,    -- relatif paths.content
  duration_ms     INT UNSIGNED NOT NULL,
  description     VARCHAR(255),
  PRIMARY KEY (cinematic_id)
);

CREATE TABLE cinematic_camera_path (
  cinematic_id    INT UNSIGNED NOT NULL,
  point_idx       INT UNSIGNED NOT NULL,
  position_x      FLOAT NOT NULL,
  position_y      FLOAT NOT NULL,
  position_z      FLOAT NOT NULL,
  timestamp_sec   FLOAT NOT NULL,
  PRIMARY KEY (cinematic_id, point_idx)
);
```

### Configuration (`config.json`)

```json
"cinematics": {
  "enabled": true,
  "anticheat_position_tolerance_m": 5.0,
  "max_concurrent_per_player": 1
}
```

### Tests

- `CinematicMgrTests.cpp` — trigger envoie l'opcode + démarre
  surveillance position.
- `CinematicAnticheatTests.cpp` — joueur s'éloigne du path attendu →
  flag violation.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- Assets cinématique : sous `paths.content + cinematics/`
- ❌ Interdit : créer un dossier racine non autorisé

## Étapes d'implémentation

1. Créer `engine/server/shard/cinematics/` et `engine/client/cinematics/`.
2. Migrations DB.
3. Implémenter `CinematicMgr` côté shard.
4. Allouer opcode `kOpcodeTriggerCinematic`.
5. Implémenter `CinematicPlayer` côté client (stub pour MVP, juste `printf` reçu).
6. Implémenter validation anti-téléport.
7. Tests : 2 fichiers.
8. Doc : section « Cinematics » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard) + Windows OK (client)
- [ ] Tests passent
- [ ] Smoke test : trigger cinematic ID 1 → opcode envoyé, client log "received cinematic 1"
- [ ] Si joueur déclenche un téléport pendant cinematic → violation log
- [ ] Migrations idempotentes
- [ ] Aucun chemin absolu (assets via `paths.content`)
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Asset loading client** : la cinématique = vidéo `.mp4` ou un format scripted. Pour MVP, juste un opcode + ID. Le rendu réel (cutscene Vulkan) est un autre ticket.
- **Pause / skip** : un joueur peut skip une cinématique. Notifier le serveur (`kOpcodeCinematicEnd`) pour stop la validation.
- **Multi-cinematic** : interdit (`max_concurrent_per_player = 1`). Si déjà active, le nouveau trigger remplace.

## Références

- `SERVER-CORE_ANALYSIS.md` § Cinematics (P3 cross)
- server-core `src/game/Cinematics/CinematicMgr.cpp`, `M2Stores.cpp`
