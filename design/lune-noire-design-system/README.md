# Lune Noire Design System

**Les Chroniques de la Lune Noire (LCDLLN)** — a dark fantasy MMORPG by Tips-of-Mine. An obsidian-black moon hangs over a cursed gothic world where eight races — Humains, Elfes, Orcs, Nains, Morts-Vivants, Corrompus, Divins, Démons — wage war across cracked cathedrals and ruined plains.

This design system captures the brand, UI language and visual vocabulary of the game, extracted from the production C++/Vulkan codebase (`tips-of-mine/LCDLLN-test`). Use it to design login flows, in-game HUD, marketing pages, character-creation screens, and promotional decks — all in the same world.

## Sources

- **Primary codebase:** `github.com/tips-of-mine/LCDLLN-test` (default branch `main`)
  - `engine/client/AuthUi.*` — login / register / character-creation state machine
  - `engine/render/AuthGlyphPass.*`, `AuthUiRenderer.*`, `AuthLogoPass.*` — Vulkan UI renderers
  - `game/data/ui/themes/default/theme.json` — master theme tokens
  - `game/data/ui/races/<race>/theme.json` — per-race palette overrides (8 races)
  - `game/data/races/{races,classes}.json` — lore + race/class descriptions
  - `game/data/localization/{fr,en}/*.json` — tone-of-voice reference (French-first)
  - `engine/assets/ui/**/*.prompt.txt` — art-direction prompts for every UI surface
- **Related private repos** (not read here, but part of the product):
  `LCDLLN-client`, `LCDLLN-server2`, `LCDLLN-all`

## Products represented

1. **Le jeu (LCDLLN game client)** — Vulkan-rendered MMO client. Includes auth/login, character creation, world HUD, inventory, chat, guild, auction house, settings.
2. **Web portal** — companion password-recovery & terms portal (`deploy/docker/traefik-web-portal.*`).
3. **World Editor** (`Editeur d'univers/lune-noire-editor-*.html`) — in-house tool for designers.

The UI Kit in this design system focuses on **the game client** — that's where the brand lives.

---

## CONTENT FUNDAMENTALS

**Language.** French is the first-class language; English is a secondary locale. Every copy decision should be drafted in French first. Example title: **"Les Chroniques de la Lune Noire"**.

**Voice.** Formal, slightly archaic, game-master-to-player — the product speaks *vous*, never *tu*. No exclamation points in UI chrome. Errors are stated flatly; help text is instructive, not apologetic.

**Casing.** **Sentence case** for buttons and labels ("Connexion", "Mot de passe oublié ?", "Créer un compte"). Titles use **Title Case** in French style (only the first word capitalised): *"Les Chroniques de la Lune Noire"*, *"Création du personnage"*. Never ALL-CAPS except for short system banners and the panel keycap hints.

**Tone examples (from `fr.json`):**
- Login help: *"Saisissez votre identifiant et votre mot de passe pour vous connecter. En cas d'oubli, utilisez le lien « Mot de passe oublié »."*
- Error: *"Identifiant ou mot de passe incorrect."* (blunt, no apology)
- Status banner: *"Serveurs en maintenance. Réessayez dans quelques instants."*
- Race flavour: *"Revenu des ténèbres, les Morts-Vivants résistent à la mort elle-même."*
- Shard pick: *"Monde #{id} — charge {load} — {endpoint}"*

**Pronouns.** *Vous* to the player. Game-world objects described in third-person narration.

**Emoji.** **Never used.** The game uses chat emotes (text-based: `*rire*`) but no unicode emoji anywhere in UI, marketing, or docs.

**Keyboard hints.** Short, unambiguous, using actual key names. Pattern: `"Entrée pour se connecter"`, `"Ctrl+R créer un compte"`, `"Échap retour"`. Separator is a plain space or em-dash.

**Numbers & placeholders.** Stats use `+10%`, `-50%`, never "+/-". Tokens use `{curly_braces}` in source strings.

**Lore writing.** Short, evocative sentences. *"Touchés par la magie sombre, les Corrompus manient les forces interdites."* Three-word moods per race: *"Chaos, corruption, pouvoir instable"* (Démons), *"Forge, endurance, tradition"* (Nains). Copy this cadence.

---

## VISUAL FOUNDATIONS

**Core palette.** Deep near-black backgrounds (`#0A0D12`), cold steel-blue primary (`#4A7BB8`), warm antique-gold accent (`#E8C56E`). The accent is used like carved metal — for active state, emblems, and single punchy highlights; never as fill colour on large areas.

**Race palettes.** Eight overlays rebrand the entire UI when the player selects a race. Each has a primary, a secondary, an **accent** (the one bright colour), and a near-black panel/surface. Corrompus uses toxic lime (`#B8FF4A`) on purple, Démons use hot red (`#FF3B6A`) on black-wine, Divins use off-white + antique gold. See `colors_and_type.css` and the preview cards.

**Typography.** Two display faces:
- **Windlass** — the master UI font (`game/data/ui/themes/default/theme.json` → `"fontFamily": "Windlass"`). Used for ALL UI: titles, labels, values, hints. Gothic-medieval, carved-stone feel.
- **Morpheus** — secondary "value font" loaded by `AuthGlyphPass::UploadValueFontFromTtf`. Used for field values (usernames, TAG-IDs) where extra display weight is wanted.

Both are shipped as `.ttf` in-game and loaded via stb_truetype. For web fallback we substitute **UnifrakturCook** / **Cinzel** (Google Fonts) — **flagged substitution**, please supply the real TTFs.

**Base size** `14px`, **title size** `22–40px` (varies by race theme).

**Backgrounds.** Almost all full-bleed cinematic matte paintings: *"ancient stone ruins, broken pillars, eroded gothic structures under a massive black moon"* (from `common/background.png.prompt.txt`). UI sits ON TOP of the scene — the login screen uses `authMinimalChrome = true` meaning no big opaque panel, just translucent inputs floating on the photo. Central and lower areas are kept visually calm in the source art so UI has a place to land.

**No gradients as background fill.** The mood comes from photography + fog + cold rim-light, not CSS gradients. Thin cold-blue rim light and faint inner purple leak from cracks are the only "glow" effects.

**Animation.** Sparse and ritual. Spinner on logo *only when checking master-server availability* (`m_authLogoRotationRad`). Fades for phase transitions (Login → Register → Terms → Character-create). No bounces, no springs, no scale-playful animations — everything moves slowly, stone-heavy.
- Default ease: `cubic-bezier(.2, .6, .2, 1)` (ease-out, slightly long tail, 320ms)
- Spinner: linear rotation, `2.4s` per revolution.

**Hover states.** Lighten the text colour by one step (muted → text), NEVER change hue. Fields gain a hairline border in the race primary. Buttons get a subtle inner glow (1px inset using `accent` at 20%).

**Press states.** Darken by ~8%, shift 1px down, keep the border. No scale transforms.

**Borders.** Hairline 1px in `border` token (`#3D4F66` on default theme). For emphasis use 2px in `primary`. Corners: **`radius: 8px`** on default; race themes override from `6px` (Orcs, brutal) up to `18px` (Divins, celestial). Use the race-owned radius on race-coloured surfaces; 8px everywhere else.

**Shadows / elevation.** Outer shadows are **deep and flat**: `0 8px 32px rgba(0,0,0,.6)`. No coloured shadows. Inner shadows are used on panels to fake carved stone: `inset 0 1px 0 rgba(255,255,255,.04), inset 0 -1px 0 rgba(0,0,0,.6)`.

**Transparency / blur.** Translucent panels over matte paintings use `backdrop-filter: blur(12px)` + `rgba(10,13,18,.72)`. **Never** pure-white frosted glass — always toward black. The `authMinimalChrome` flag means some screens skip panels entirely.

**Cards.** Panel colour (`#141C28` default) + 1px border + inset top highlight + outer drop-shadow. No gradient fill. Title runs along the top inside a 1px divider. Think "metal plaque riveted to stone", not "material design card".

**Imagery vibe.** Cold, desaturated, volumetric fog, ash drifting. Warm light only from torches — always small, never the scene's dominant light. Colour grade leans blue-grey with a slight green shadow tilt. Black-and-white is acceptable; warm golden-hour photography is **off-brand**.

**Layout.** Fixed header with edition/version string top-right; footer keycap hints bottom; content grid centered. The auth screen uses a 3-column grid for registration (`gridColumn`, `gridSpan` in `RenderField`). Viewport-aware (`SetViewportSize`), targets 1920×1080 primary, scales to 16:9.

**Iconography.** See below — stone/bronze engraved icons, no line-icon sets.

---

## ICONOGRAPHY

The game does not ship a conventional icon font or SVG sprite. Icons in the production client are **baked PNG textures** loaded via the asset registry (`engine/render/AssetRegistry.*`) and referenced from theme JSON: `"iconHud": "textures/test.texr"`, `"iconButton": "textures/test.texr"`, `"iconTooltip": "textures/test.texr"`.

Every icon exists as a `*.png.prompt.txt` briefing file in `game/data/ui/**` describing the intended art — no SVGs were hand-authored in-source. Categories present in the repo:

- **Splash / logo** — the obsidian Black Moon emblem (primary brand mark)
- **Server list** — `server_online.png`, `server_offline.png`, `server_full.png`
- **Dialogs** — `error.png`, `info.png`, `success.png`
- **Loading** — `loading_bar.png`, `spinner.png`
- **Race emblems** — one per race (`assets/ui/races/<race>/images/emblem.png`)

**Emoji.** Never. The client uses **text emotes** parsed by `engine/net/ChatEmotes.cpp` (e.g. `/sourire`, `*rire*`) — not unicode glyphs.

**Unicode characters as icons.** Limited to geometric chevrons in menus (`">"` is the literal menu separator per `options.menu.chevron`). Checkbox states use bracketed text in the CLI-ish panel builder: `[x]` / `[ ]` (see `auth.panel.accept_checked`).

**Web / design-system substitution.** For HTML artifacts, we substitute **Phosphor Icons (duotone)** at stroke 1.25 — CDN-linked from `unpkg.com/@phosphor-icons/web`. Phosphor's occult/medieval coverage (moon, flame, sword, scroll, shield, skull, key, crown) is the closest match for the brand while remaining readable at UI sizes. **This is a substitution — the final product uses the game's own PNG textures.** Please provide the canonical icon PNGs (or preferred vector set) to close the gap.

Usage rules:
- Icons are **metallic**, not flat — either copied from game textures or rendered with the accent gold over a near-black surface.
- Never coloured (rainbow/multicolour). Monochrome bronze, steel, or white.
- Always paired with a label unless in the HUD action bar.

---

## Index (this folder)

- `README.md` — this file
- `SKILL.md` — Claude Code-compatible skill entrypoint
- `colors_and_type.css` — CSS variables for colour + type tokens (base + semantic)
- `fonts/` — font files (substitutions flagged)
- `assets/` — logos, race emblems, backgrounds, icons
- `preview/` — individual design-system preview cards (one concept per file)
- `ui_kits/game/` — hi-fi React recreation of the LCDLLN game client (auth, character create, HUD)
- `slides/` — *(not generated — no deck template was provided)*

---

## CAVEATS / ASKS (please iterate with me)

1. **Fonts.** The real `Windlass.ttf` and `Morpheus.ttf` are not in the public repo — I've substituted **UnifrakturCook** (display) + **Cinzel** (UI/titles) + **EB Garamond** (body) from Google Fonts. **Please drop the real TTFs into `fonts/`.**
2. **Icons.** The game's baked PNGs are placeholder references (`textures/test.texr`). I've substituted Phosphor Icons for previews. **Please share the canonical icon PNGs or an icon spec.**
3. **Logo.** I have the art-direction prompt but no final logo PNG — the preview uses a placeholder typographic lockup. **Please share `logo.png` + `logo_login.png`.**
4. **Backgrounds.** Same situation — placeholder gradients + fog stand in for the cinematic matte paintings. **Please share the rendered PNGs.**
5. **Web portal.** I focused the UI kit on the game client. If you'd like the same treatment for the password-recovery web portal, say the word.
