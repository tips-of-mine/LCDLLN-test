# LCDLLN-test

## Documentation

- Terrain décalé et World Editor : [docs/terrain_et_world_editor.md](docs/terrain_et_world_editor.md)
- Pipeline zones, export WE, `zone_builder` : [docs/world_editor_zone_pipeline.md](docs/world_editor_zone_pipeline.md)
- Checklist zone démo **`demo_plains`** : [docs/world_zone_demo_checklist.md](docs/world_zone_demo_checklist.md)

## World / zones

Contenu de référence : **`game/data/zones/demo_plains/`** (`zone_id` **demo_plains** — heightmap légère, manifeste, layout stub, `chunks/chunk_0_0/` MVP).

### Build des outils (CMake)

Configurer le projet avec un preset du dépôt ([`CMakePresets.json`](CMakePresets.json)), puis par exemple :

```bash
cmake --build build/vs2022-x64 --config Release --target zone_builder
cmake --build build/vs2022-x64 --config Release --target world_editor_app
```

Adapter `build/vs2022-x64` à votre répertoire binaire (`build/linux-x64`, etc.).

### Où sont les exécutables

Chemins typiques **sous le répertoire de build CMake** (`${CMAKE_BINARY_DIR}`) :

| Cible | Dossier (Windows / Linux) |
|--------|---------------------------|
| `zone_builder` | `pkg/zone_builder/zone_builder` (`.exe` sous Windows) |
| `lcdlln_world_editor` | `pkg/world_editor/lcdlln_world_editor` (`.exe` sous Windows) |

Lancer ces binaires **depuis la racine du dépôt** (là où se trouve `config.json`) pour que `paths.content` et les chemins relatifs résolvent correctement.

### Scripts pont WE → `zone_builder`

Voir [`tools/world/export_zone_with_chunks.ps1`](tools/world/export_zone_with_chunks.ps1) et [`tools/world/export_zone_with_chunks.sh`](tools/world/export_zone_with_chunks.sh) ; détail des arguments dans la doc pipeline §5.1.
