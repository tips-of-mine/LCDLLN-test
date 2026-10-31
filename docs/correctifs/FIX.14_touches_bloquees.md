# FIX.14 — Personnage qui avance seul (touche « bloquée »)

## Symptôme

En jeu, le personnage se met à courir seul dans une direction et le joueur ne
reprend pas la main (appuyer sur une autre direction ne fait que compenser ;
relâcher la relance). Signalé plusieurs fois (orc guerrier, Léviathan, puis
plaine près du hameau de Marn, écran 5760×1080).

## Cause

`engine::platform::Input` suit l'état des touches avec les seuls messages
Win32 `WM_KEYDOWN` / `WM_KEYUP`. Quand la fenêtre perd le clavier pendant
qu'une touche est tenue (clic sur un autre écran, Alt+Tab, notification ou
fenêtre système qui prend le focus…), le `WM_KEYUP` est envoyé à l'AUTRE
fenêtre : le jeu croit la touche toujours enfoncée. Aucun relâchement
n'était fait à la perte de focus (`WM_KILLFOCUS` / `WM_ACTIVATEAPP` n'étaient
pas traités). La touche d'avance (Z / W) restait donc « enfoncée » jusqu'au
prochain appui sur cette même touche.

## Correction

`src/shared/platform/Input.{h,cpp}` :

- `WM_KILLFOCUS` et `WM_ACTIVATEAPP(FALSE)` → `Input::ReleaseAll()` : toutes
  les touches et boutons tenus sont relâchés (`WasReleased` vrai à l'image).
- `Input::SyncWithOs()`, appelée par `Engine::BeginFrame()` juste après le
  traitement des messages : toute touche marquée enfoncée mais physiquement
  relâchée (`GetAsyncKeyState`) est relâchée. Couvre les `WM_KEYUP` perdus
  sans perte de focus. Inactif quand la fenêtre n'a pas le focus.
- Les deux cas sont journalisés (`[Input] … relache(s)` /
  `[Input] touche VK=0x.. relachee hors fenetre`) : si le problème revient,
  le log client dira s'il s'agit bien d'une touche perdue.

`src/shared/platform/Input_linux.cpp` : implémentations vides (backend de
remplacement sans clavier natif).

## Vérification

- Tenir Z, cliquer sur un autre écran ou faire Alt+Tab, relâcher Z, revenir :
  le personnage est arrêté ; log `[Input] perte de focus : 1 touche(s)…`.
- Jeu normal : aucun changement (déplacement, sprint Alt, saut).

## Hors périmètre

Le passage à travers les murs et le blocage dans le décor (cellule Léviathan)
sont un autre sujet (collision à faible FPS), à traiter séparément.
