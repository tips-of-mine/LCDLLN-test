# FIX.13 — Diagnostic de performance dans le journal client

Retour joueur du 30/10/2026 : dans la cellule de Léviathan (`cell_n009_e011`), le
client tombe à ~6 images par seconde (60 ailleurs). La cellule compte 741 656 instances
de décor (forêt dense) dont 406 237 obstacles. Le journal ne permet pas de savoir où
part le temps (CPU ou GPU, quelle phase) : le profileur existant (`Profiler`, passes GPU
du frame graph + scopes CPU) n'était journalisé nulle part.

## Ce qui est ajouté

Toutes les `debug.perf_log_interval_s` secondes (défaut 5 ; `0` = désactivé), deux
lignes :

```
[Perf] cpu=83.1ms gpu=12.4ms | cpu: Render=61.2 Update=20.0 ScatterRecord=18.3 … | gpu: geometry=6.1 …
[Perf] scene : decor visibles=… parties=… lots=… (sur 741656 dans 1 champ(s)) ; cellules=… ; props=… ; collisions=… cyl / … boites ; ecran=5760x1080
```

- Scopes CPU mesurés : `Update`, `Render`, `CharacterController`, `CellStreaming`,
  `ScatterRecord` (tri + envoi du décor), `ScatterOccluders` (fondu caméra).
- Passes GPU : celles du frame graph (déjà instrumentées).

## Utilisation

Se placer dans la zone lente ~30 s, puis envoyer le journal client : les lignes
`[Perf]` désignent la phase à optimiser (correctif suivant).

## Code

- `src/shared/core/ProfilerSummary.h` : formatage (testé par `profiler_summary_tests`).
- `Engine::Run` / `Engine::LogPerfSummary` (EngineWorld.cpp) ; scopes dans
  `Engine::Update` et `RecordScatterGeometry`.
