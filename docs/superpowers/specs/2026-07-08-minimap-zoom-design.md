# Contrôle de zoom du radar minimap — design (2026-07-08)

## Objectif
Permettre au joueur de zoomer/dézoomer la mini-carte via un contrôle circulaire
sur la moitié haute de l'arc du radar. Retour joueur : pouvoir régler la portée
affichée.

## Comportement
- **5 crans de rayon affiché** : 200, 400, 600, 800, 1000 m (index 0→4).
- **Défaut** : 600 m (index 2).
- **Persistance** : cran mémorisé dans `user_settings.json`, clé
  `client.quest.minimap.zoom_level` (int 0-4). Rechargé au boot.
- Remplace le rayon fixe historique (`m_minimapRadiusM`, défaut 60 m) : le rayon
  courant devient `kZoomLevelsM[zoom_level]`.

## Interaction
- **Molette au-dessus du disque du radar** → zoom in/out d'un cran (clamp 0..4).
  La molette pilote déjà le zoom caméra (`OrbitalCameraController`,
  `Camera.cpp` `MouseScrollDelta`). Quand le curseur est sur le disque du radar,
  on **coupe le zoom caméra** (nouveau paramètre `applyZoom` passé à
  `OrbitalCameraController::Update`) et la molette pilote le radar.
- **Clic sur un des 5 repères** de l'arc → saut direct à ce cran (hit-test dans
  un petit rayon autour du repère).

## Visuel
- Arc sur la **moitié haute** du cercle du radar, tracé juste à l'extérieur de la
  bordure (n'empiète pas sur les POI intérieurs).
- **5 repères** répartis sur l'arc : gauche = 200 m … droite = 1000 m.
- Cran courant **surligné** (pastille/poignée plus grosse + teinte accent).
- **Valeur numérique** (« 600 m ») affichée au-dessus du radar (lève l'ambiguïté
  du sens de l'arc).

## Architecture (isolée + testable)
1. **`MinimapZoom` (pur, testé)** — helper : `kZoomLevelsM[5] = {200,400,600,800,
   1000}`, `ClampZoomIndex(int)`, `StepZoomIndex(int idx, int wheelDelta)`,
   `RadiusForZoomIndex(int)`. Aucune dépendance ImGui. Même esprit que
   `WorldToRadarUv`.
2. **`ComputeRadarScreenRect(cfg, displayW, displayH)` (pur, testé)** — extrait la
   géométrie du radar (coin haut-gauche `x0/y0` + taille `size` px) aujourd'hui
   calculée inline dans `QuestImGuiRenderer::RenderMinimap`. Partagée entre le
   **rendu** (arc/repères/POI) et le **hit-test** (Engine : molette + clic) →
   zéro dérive de géométrie.
3. **Engine** — possède le cran courant (chargé de la config au boot). Chaque
   frame in-world : hit-test souris vs disque radar (via `ComputeRadarScreenRect`) ;
   si dessus, lit la molette (`MouseScrollDelta`) → `StepZoomIndex`, et teste le
   clic sur les repères → cran direct. Au changement : `m_questUi.SetMinimapRadius(
   RadiusForZoomIndex(idx))` + persiste `client.quest.minimap.zoom_level`. Passe
   `applyZoom = !mouseOverRadar` à la caméra.
4. **`QuestImGuiRenderer::RenderMinimap`** — dessine l'arc + 5 repères + poignée +
   valeur numérique à partir du cran courant (fourni par Engine via un setter,
   ex. `SetZoomIndex(int)`), en réutilisant `ComputeRadarScreenRect`.
5. **`OrbitalCameraController::Update`** — nouveau paramètre `bool applyZoom`
   (défaut true) : ignore la molette si false.

## Config
- `client.quest.minimap.zoom_level` : int 0-4, défaut 2 (600 m). Lu au boot,
  persisté dans `user_settings.json` (persistance ciblée, comme `client.locale`).

## Tests
- `MinimapZoom` : clamp bornes (idx < 0, idx > 4), step molette (in/out + clamp),
  mapping index→rayon (0→200 … 4→1000).
- `ComputeRadarScreenRect` : coin + taille pour un viewport donné + `size_px`
  config ; radar désactivé (`minimap.enabled=false`) → rect vide/no-op.

## Périmètre / déploiement
100% **client**. Aucun serveur, aucun wire, pas de redéploiement. Fichiers touchés :
`QuestImGuiRenderer.{h,cpp}`, `Engine.cpp`, `Camera.{h,cpp}`, nouveau
`MinimapZoom` + tests, `user_settings` persistance.

## Hors périmètre (YAGNI)
- Pas de zoom continu (5 crans discrets seulement).
- Pas de panoramique (le radar reste centré joueur).
- Pas de zoom par cran configurable au-delà des 5 valeurs fixées.
