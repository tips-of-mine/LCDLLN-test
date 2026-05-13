# Catalogue de templates de donjon — `game/data/meshes/dungeons/`

> M100.43 — Dungeon Portal System (Phase 11 « Volumes 3D »).

## Statut MVP

Le fichier `catalog.json` liste 3 templates (`dungeon_starter_keep`,
`dungeon_crypt_of_echoes`, `dungeon_abyssal_caverns`) avec leur
métadonnées (requiredLevel, difficulty range). Les **assets `.gltf`
décoratifs et `thumbnails/*.png` ne sont pas livrés** — ticket d'art
dédié.

## Architecture M100.43

L'outil **Dungeon Portal** (éditeur) place un portail dans le monde
overworld :

| Composant | Localisation | Statut M100.43 |
|---|---|---|
| **Tool éditeur** | `src/world_editor/volumes/dungeons/` | ✅ livré |
| **Persistance** | `instances/dungeon_portals.bin` (LCDP v1) | ✅ livré |
| **Catalog** | `game/data/meshes/dungeons/catalog.json` | ✅ livré (placeholder) |
| **Opcodes 197/198** | `kOpcodeEnterDungeon{Request,Response}` | ✅ réservés |
| **Payloads** | `src/shared/network/DungeonPayloads.{h,cpp}` | ✅ livré |
| **Migration DB** | `sql/migrations/0063_dungeon_instances.sql` | ✅ livré |
| **Handler shard** | `EnterDungeonHandler` | ⏳ M100.44 |
| **VMap bridge** | overworld ↔ dungeon-instance | ⏳ M100.44 |
| **Rendu glTF** | mesh décoratif d'entrée | ⏳ tinygltf integration |

> ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS** : la migration 0063 est nouvelle.
> Doit être rejouée au boot serveur pour que `dungeon_instances` existe.
> Les opcodes 197/198 sont **réservés mais pas câblés** ; un client qui
> les enverrait aujourd'hui recevrait BAD_REQUEST. M100.44 câblera le
> handler.

## Champs `catalog.json`

| Champ | Type | Description |
|---|---|---|
| `id` | string ≤ 64 octets | Template id consommé par le master (`kMaxDungeonTemplateIdBytes`). |
| `displayName` | string | Nom affiché. |
| `description` | string | Texte multi-ligne (affiché dans le Tool Properties). |
| `decorativeMesh` | string (opt) | glTF d'arche/portail d'entrée (cosmétique). |
| `thumbnail` | string (opt) | PNG 128×128. |
| `requiredLevel` | uint16 | Niveau perso minimum (1..80). |
| `minDifficulty` / `maxDifficulty` | uint8 | Range autorisé (1..5). |
