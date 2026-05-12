# Lune Noire — Game Client UI Kit

Hi-fi React recreation of the **LCDLLN game client** auth & character-creation flow, plus the in-world HUD skeleton.

Source of truth: `engine/client/AuthUi.*`, `engine/client/CharacterCreationUi.*`, `engine/client/UIModel.*` in the repo.

## Components

- `AuthBackdrop.jsx` — full-bleed cursed-castle matte backdrop with fog + moon.
- `AuthPanel.jsx` — the translucent panel used on auth screens.
- `LoginScreen.jsx` — identifiant / mot de passe + status banner + keycap hints.
- `RegisterScreen.jsx` — 3-column grid registration form (matches `gridColumn`/`gridSpan`).
- `ShardPickScreen.jsx` — list of worlds with load + endpoint.
- `CharacterCreateScreen.jsx` — race + class picker with live theme swap.
- `HudOverlay.jsx` — in-world HUD: portrait, action bar, minimap stub, chat.
- `primitives.jsx` — Button, Field, Dropdown, Banner, KeycapHint, RaceChip.

## Screens in `index.html`

The index is a click-thru: **Login → Register → Shard pick → Character create → HUD**. Each screen is reachable from the flow nav at the bottom-right. Race selection on the character screen rebrands everything via `data-race` on the root.
