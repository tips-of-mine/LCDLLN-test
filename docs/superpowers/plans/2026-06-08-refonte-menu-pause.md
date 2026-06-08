# Refonte du menu Pause (variante C) — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recentrer et restyler le menu Pause in-game (variante « médiéval accentué ») en pilotant toutes ses couleurs par `LnTheme::Active()`.

**Architecture :** Remplacement du bloc `if (m_inGamePauseMenuVisible)` dans `src/client/app/Engine.cpp`. Cadre + titre + séparateur dorés (accent du thème), boutons thémés centrés avec halo de bordure au survol, *Quitter* en rouge danger. Aucune nouvelle classe/fichier. Stacké sur la branche `theming-runtime-spec` (PR #855) car dépend de `LnTheme::Active()`.

**Tech Stack :** Dear ImGui (immediate mode), `LnTheme` runtime (sous-projet 1).

---

## Task 1 : Recentrage + restyle variante C du menu Pause

**Files:**
- Modify: `src/client/app/Engine.cpp` (bloc `if (m_inGamePauseMenuVisible)`, ~lignes 10183-10224)

- [ ] **Step 1 : Remplacer le bloc complet du menu Pause**

Dans `src/client/app/Engine.cpp`, remplacer EXACTEMENT ce bloc existant (le menu Pause ImGui, qui commence par `if (m_inGamePauseMenuVisible)` et finit au `ImGui::End();` correspondant — vérifier d'abord en lisant la zone que le texte ci-dessous correspond bien à l'actuel, car les numéros de ligne ont pu bouger) :

```cpp
			if (m_inGamePauseMenuVisible)
			{
				const float menuW = 320.f;
				const float menuH = 220.f;
				ImGui::SetNextWindowPos(ImVec2((dw - menuW) * 0.5f, (dh - menuH) * 0.5f), ImGuiCond_Always);
				ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);
				ImGui::SetNextWindowBgAlpha(0.92f);
				ImGui::Begin("##ln_pause_menu", nullptr,
					ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
					| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
					| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
				ImGui::SetWindowFontScale(1.2f);
				const char* title = "PAUSE";
				const float titleW = ImGui::CalcTextSize(title).x;
				ImGui::SetCursorPosX((menuW - titleW) * 0.5f);
				ImGui::TextUnformatted(title);
				ImGui::SetWindowFontScale(1.f);
				ImGui::Separator();
				ImGui::Spacing();
				const float btnW = menuW - 40.f;
				if (ImGui::Button("Reprendre", ImVec2(btnW, 32.f)))
				{
					m_inGamePauseMenuVisible = false;
				}
				ImGui::Spacing();
				if (ImGui::Button("Options", ImVec2(btnW, 32.f)))
				{
					m_inGamePauseMenuVisible = false;
					m_inGameOptionsPanelVisible = true;
				}
				ImGui::Spacing();
				if (ImGui::Button("Se deconnecter", ImVec2(btnW, 32.f)))
				{
					RequestLogoutToLoginScreen();
				}
				ImGui::Spacing();
				if (ImGui::Button("Quitter le jeu", ImVec2(btnW, 32.f)))
				{
					OnQuit();
				}
				ImGui::End();
			}
```

…par EXACTEMENT ce nouveau bloc (indentation : tabulations, comme le fichier) :

```cpp
			if (m_inGamePauseMenuVisible)
			{
				const LnTheme::Palette& th = LnTheme::Active();
				// Conversion couleur thème -> ImVec4 ImGui, locale au menu pause.
				auto iv = [](const LnTheme::Rgba& c, float a) -> ImVec4 {
					return ImVec4(c.r, c.g, c.b, a);
				};
				const float menuW = 340.f;
				const float menuH = 250.f;
				const float btnW = menuW - 64.f;
				ImGui::SetNextWindowPos(ImVec2((dw - menuW) * 0.5f, (dh - menuH) * 0.5f), ImGuiCond_Always);
				ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);
				// Chrome variante C : fond panneau + cadre accentué (doré en Or royal,
				// vert en Sylve émeraude). On retire SetNextWindowBgAlpha : il écraserait
				// l'alpha du WindowBg ci-dessous.
				ImGui::PushStyleColor(ImGuiCol_WindowBg, iv(th.panel, 0.96f));
				ImGui::PushStyleColor(ImGuiCol_Border, iv(th.accent, 1.f));
				ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
				ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.f);
				ImGui::Begin("##ln_pause_menu", nullptr,
					ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
					| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
					| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);

				// Titre PAUSE : couleur accent, centré sur la zone de contenu réelle
				// (GetContentRegionAvail tient compte du padding -> centrage correct).
				ImGui::SetWindowFontScale(1.3f);
				ImGui::PushStyleColor(ImGuiCol_Text, iv(th.accent, 1.f));
				const char* title = "PAUSE";
				const float titleW = ImGui::CalcTextSize(title).x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - titleW) * 0.5f);
				ImGui::TextUnformatted(title);
				ImGui::PopStyleColor();
				ImGui::SetWindowFontScale(1.f);

				// Séparateur doré.
				ImGui::PushStyleColor(ImGuiCol_Separator, iv(th.accent, 0.7f));
				ImGui::Separator();
				ImGui::PopStyleColor();
				ImGui::Spacing();
				ImGui::Spacing();

				// Bouton thémé centré, avec halo de bordure (accent, ou rouge danger)
				// dessiné par-dessus au survol.
				auto pauseButton = [&](const char* label, bool danger) -> bool {
					const LnTheme::Rgba hov = danger ? th.errorCol : th.accent;
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - btnW) * 0.5f);
					ImGui::PushStyleColor(ImGuiCol_Button, iv(th.surface, 1.f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, iv(hov, 0.22f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, iv(hov, 0.35f));
					ImGui::PushStyleColor(ImGuiCol_Border, iv(th.border, 1.f));
					ImGui::PushStyleColor(ImGuiCol_Text, danger ? iv(th.errorCol, 1.f) : iv(th.text, 1.f));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
					const bool pressed = ImGui::Button(label, ImVec2(btnW, 34.f));
					if (ImGui::IsItemHovered())
					{
						ImGui::GetWindowDrawList()->AddRect(
							ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
							ImGui::ColorConvertFloat4ToU32(iv(hov, 1.f)), 3.f, 0, 1.5f);
					}
					ImGui::PopStyleVar(2);
					ImGui::PopStyleColor(5);
					return pressed;
				};

				if (pauseButton("Reprendre", false))
				{
					m_inGamePauseMenuVisible = false;
				}
				ImGui::Spacing();
				if (pauseButton("Options", false))
				{
					m_inGamePauseMenuVisible = false;
					m_inGameOptionsPanelVisible = true;
				}
				ImGui::Spacing();
				if (pauseButton("Se deconnecter", false))
				{
					RequestLogoutToLoginScreen();
				}
				ImGui::Spacing();
				if (pauseButton("Quitter le jeu", true))
				{
					OnQuit();
				}
				ImGui::End();
				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor(2);
			}
```

- [ ] **Step 2 : Vérifier l'équilibrage Push/Pop ImGui (relecture)**

Relire le bloc et confirmer, par paire :
- Chrome fenêtre : 2 `PushStyleColor` (WindowBg, Border) + 2 `PushStyleVar`
  (WindowBorderSize, WindowRounding) → après `End()` : `PopStyleVar(2)` +
  `PopStyleColor(2)`. ✓
- Titre : 1 push/1 pop couleur Text. ✓
- Séparateur : 1 push/1 pop couleur Separator. ✓
- `pauseButton` : 5 `PushStyleColor` + 2 `PushStyleVar` → `PopStyleVar(2)` +
  `PopStyleColor(5)`. ✓

Aucune logique d'action changée : Reprendre ferme le menu, Options ouvre le
panneau, Se déconnecter appelle `RequestLogoutToLoginScreen()`, Quitter appelle
`OnQuit()`.

- [ ] **Step 3 : Build (CI uniquement)**

Pas de toolchain locale (cmake/MSVC/vcpkg absents) → ne pas compiler en local.
La compilation client est vérifiée par la CI (build-windows / build-linux).
`LnTheme::Palette`/`Rgba`/`Active()` sont disponibles (`LnTheme.h` déjà inclus
dans `Engine.cpp` depuis le sous-projet 1).

- [ ] **Step 4 : Validation manuelle en jeu (post-merge ou build local utilisateur)**

Ouvrir le menu Pause : titre/boutons centrés (marges égales), cadre + titre +
séparateur dorés, survol dore la bordure des boutons, *Quitter* dore en rouge.
Changer de thème (Options → Sylve émeraude) : le menu Pause se recolore en vert.

- [ ] **Step 5 : Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(ui): refonte menu Pause variante C — centrage + cadre/boutons thematises"
```
Terminer le corps du commit par :
Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>

---

## Self-review (rédaction)

- **Couverture spec** : centrage titre+boutons (zone de contenu réelle) ✓ ;
  chrome doré (fond panel + bordure accent + rounding) ✓ ; titre accent centré ✓ ;
  séparateur doré ✓ ; boutons thémés centrés + halo survol ✓ ; Quitter en danger ✓ ;
  couleurs 100 % issues de `Active()` ✓ ; actions inchangées ✓ ; déploiement
  client-only ✓.
- **Placeholders** : aucun — bloc de remplacement complet fourni.
- **Cohérence** : équilibrage Push/Pop vérifié à l'étape 2 ; `iv()`/`th` capturés
  par la lambda dans la même portée (durée de vie OK, `Active()` renvoie une
  référence au statique).

## Déploiement

> **Déploiement** : ✅ client uniquement, pas de redéploiement serveur. Stacké sur
> PR #855 — merger #855 **avant** cette PR (lock-step).
