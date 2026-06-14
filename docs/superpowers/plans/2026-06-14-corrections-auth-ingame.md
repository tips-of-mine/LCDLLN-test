# Corrections auth / in-game + mail CGU — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Corriger 7 bugs/ajustements auth & in-game (Ctrl+R/O/F, Enter login, ciblage TAB, saccadement ALT, thèmes de race, unification menu options) + diagnostiquer/corriger le mail de validation CGU, le tout dans **une seule PR**.

**Architecture :** Client C++ Vulkan/ImGui (`src/client`, `src/shared/platform`) pour A+B ; serveur master (`src/masterd`) pour C. Le système de thème `LnTheme` est un registre data-driven (ajouter un thème = +1 entrée). Le menu options en jeu réutilisera le renderer auth existant (`AuthImGuiRenderer::RenderOptionsScreen`, déjà possédé par `Engine` via `m_authImGui`).

**Tech Stack :** C++17, Dear ImGui, Win32 (window proc), CMake (compilation via CI / VS — **pas de toolchain locale**, cf. mémoire projet). Tests serveur via `ctest` (build-linux).

**Spec :** `docs/superpowers/specs/2026-06-14-corrections-auth-ingame-design.md`

**Déploiement :** ⚠️ lock-step client + master (à cause du Lot C). Mention finale à confirmer une fois la cause C identifiée.

**Note build/test :** aucune toolchain locale. « Run » = pousser sur la branche et laisser la **CI** compiler (`build-windows` pour le client, `build-linux` + `ctest` pour le serveur), ou compiler via Visual Studio. Les items UI (ImGui / window proc / in-game) ne sont pas couverts par `ctest` → vérification **manuelle en jeu** décrite par item. Les tests unitaires ne sont ajoutés que là où une fonction pure existe (Lot C).

---

## Préliminaire : branche de travail

- [ ] **Step 1 : créer la branche feature depuis `main`**

Ne pas travailler sur `main` (branche par défaut). Partir d'un `main` à jour.

```bash
git fetch origin
git checkout main && git pull --ff-only
git checkout -b fix/corrections-auth-ingame-cgu
```

- [ ] **Step 2 : récupérer le document de spec sur la branche** (si absent)

Le fichier `docs/superpowers/specs/2026-06-14-corrections-auth-ingame-design.md` vit sur la branche `docs/spec-corrections-auth-ingame`. Le rapatrier si besoin :

```bash
git checkout docs/spec-corrections-auth-ingame -- docs/superpowers/specs/2026-06-14-corrections-auth-ingame-design.md docs/superpowers/plans/2026-06-14-corrections-auth-ingame.md
git add docs/superpowers && git commit -m "docs: spec+plan corrections auth/in-game sur la branche feature"
```

---

## Task 1 (A1) : Supprimer les raccourcis Ctrl+R / Ctrl+O / Ctrl+F (login)

**Files:**
- Modify: `src/client/auth/screens/AuthScreenLogin.cpp:129-154`

**Contexte :** `Update_LoginShortcuts` capte Ctrl+R/F/O. Les actions restent accessibles par les boutons à l'écran (Register, lien mot-de-passe oublié, bouton OPTIONS). On vide le corps des raccourcis mais on garde la fonction (signature appelée ailleurs) pour minimiser la surface du diff.

- [ ] **Step 1 : retirer les trois branches de raccourci**

Remplacer le corps de `Update_LoginShortcuts` (lignes 130-154) par :

```cpp
/// Anciennement : raccourcis Ctrl+R/F/O sur l'écran de connexion. Retirés à la
/// demande utilisateur — les actions restent accessibles via les boutons à
/// l'écran (Register, lien mot-de-passe oublié, bouton OPTIONS). La fonction est
/// conservée (no-op) pour ne pas toucher son site d'appel.
void AuthUiPresenter::Update_LoginShortcuts(engine::platform::Input& input, const engine::core::Config& cfg,
	engine::platform::Window& window, bool usingNativeAuth, bool authUiImguiMode)
{
	(void)input;
	(void)cfg;
	(void)window;
	(void)usingNativeAuth;
	(void)authUiImguiMode;
}
```

- [ ] **Step 2 : vérifier qu'aucun warning « unused private method »**

Confirmer que `ImGuiNavigateToRegisterFromLogin`, `ImGuiOpenForgotPasswordPortal`, `OpenLanguageOptions` restent appelées par leurs boutons respectifs (recherche d'usage). Elles le sont (boutons auth). Aucune suppression de méthode.

```bash
git grep -n "ImGuiOpenForgotPasswordPortal\|ImGuiNavigateToRegisterFromLogin\|OpenLanguageOptions" src/client
```
Attendu : au moins un appel hors `Update_LoginShortcuts` pour chacune (boutons UI).

- [ ] **Step 3 : commit**

```bash
git add src/client/auth/screens/AuthScreenLogin.cpp
git commit -m "fix(client): retirer les raccourcis Ctrl+R/O/F de l'écran login"
```

**Vérif manuelle (CI verte) :** sur l'écran login, Ctrl+R/O/F ne déclenchent plus rien ; boutons équivalents OK.

---

## Task 2 (A2) : Touche Enter soumet le formulaire de login

**Files:**
- Read d'abord : `src/client/render/auth/screens/AuthImGuiVerifyEmail.cpp:270-285` (pattern de référence `IsKeyPressed(ImGuiKey_Enter)`).
- Read d'abord : `src/client/render/auth/screens/AuthImGuiLogin.cpp` (écran login ImGui : repérer la fin du rendu des champs + le bouton « Se connecter » et le callback `ImGuiSubmitLogin`).
- Modify: `src/client/render/auth/screens/AuthImGuiLogin.cpp` (ajout du handler Enter).

**Contexte :** aucun `EnterReturnsTrue` ni handler Enter sur le login. Le presenter expose déjà `AuthUiPresenter::ImGuiSubmitLogin(cfg, login, password, rememberMe)` (cf. `AuthScreenLogin.cpp:156-168`) — c'est l'action exacte du bouton « Se connecter ». Il faut déclencher la même chose sur Enter.

- [ ] **Step 1 : localiser l'appel existant du bouton « Se connecter »**

Dans `AuthImGuiLogin.cpp`, trouver le bloc `if (ImGui::Button("<libellé connexion>" ...)) { ... ImGuiSubmitLogin(...) ... }` (ou le callback équivalent passé au renderer). Noter les variables locales des buffers identifiant/mot de passe et du flag « se souvenir ».

- [ ] **Step 2 : ajouter le handler Enter juste après les champs de saisie**

Immédiatement après le rendu des champs (et avant/à côté du bouton submit), ajouter, en miroir de `AuthImGuiVerifyEmail.cpp:276` :

```cpp
// Enter soumet le formulaire de connexion (équivalent clavier du bouton
// « Se connecter »). Conditions : identifiant + mot de passe non vides, et pas
// de soumission déjà en cours. ImGuiKey_Enter couvre aussi KeypadEnter via le
// remap ImGui ; on ajoute KeypadEnter par sécurité.
const bool loginReady = (loginBuf[0] != '\0') && (passwordBuf[0] != '\0') && !submitInFlight;
if (loginReady && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
{
	// MÊME action que le bouton « Se connecter ».
	onSubmitLogin(loginBuf, passwordBuf, rememberMe);
}
```

Adapter les noms (`loginBuf`, `passwordBuf`, `rememberMe`, `submitInFlight`, `onSubmitLogin`) à ceux réellement présents dans `AuthImGuiLogin.cpp`. `onSubmitLogin` = le même chemin que le clic bouton (callback vers `ImGuiSubmitLogin`). Si le code appelle directement le presenter, appeler la même méthode que le bouton.

- [ ] **Step 3 : ne PAS réafficher les chips de légende**

Laisser les chips `[Tab][Entree][Echap]` masquées (cf. `AuthImGuiLogin.cpp:163-165`). Comportement clavier seulement.

- [ ] **Step 4 : commit**

```bash
git add src/client/render/auth/screens/AuthImGuiLogin.cpp
git commit -m "fix(client): Enter soumet le formulaire sur l'écran login"
```

**Vérif manuelle :** saisir identifiant + mot de passe, presser Enter → soumission identique au clic « Se connecter ». Champs vides → Enter ne fait rien.

---

## Task 3 (A4) : Corriger le saccadement de la course avec ALT

**Files:**
- Modify: `src/shared/platform/Window.cpp:812-933` (window proc : intercepter le menu système ALT).
- Vérifier : `src/shared/platform/Input.cpp:25-50` (keyup `WM_SYSKEYUP` déjà traité symétriquement — OK, ne rien changer).

**Cause :** ALT (`VK_MENU`) déclenche le menu système Win32 via `DefWindowProcW` (`WM_SYSCOMMAND`/`SC_KEYMENU` → boucle modale interne), d'où des frames perdues = saccadement. `Input.cpp` traite déjà `WM_SYSKEYDOWN`/`WM_SYSKEYUP` symétriquement (lignes 29-49) : la capture de l'état touche est correcte. Le seul correctif nécessaire est de **bloquer l'activation du menu système**.

- [ ] **Step 1 : intercepter `WM_SYSCOMMAND`/`SC_KEYMENU` dans la window proc**

Dans `Window::HandleMessage` (`Window.cpp`), ajouter un `case` dans le `switch (msg)` (lignes 812-933), avant le `default:` :

```cpp
		case WM_SYSCOMMAND:
			// Bloquer l'activation du menu système par ALT (SC_KEYMENU) : sans ça,
			// DefWindowProc entre dans une boucle modale de menu à chaque appui ALT,
			// ce qui fait perdre des frames -> saccadement visible quand ALT sert de
			// touche de sprint. On laisse passer les autres SYSCOMMAND (déplacement,
			// fermeture, etc.). Les bits bas de wParam sont réservés au système :
			// masquer avec 0xFFF0 avant comparaison (doc Win32).
			if ((wparam & 0xFFF0u) == SC_KEYMENU)
			{
				return 0; // consommé : pas de menu système
			}
			break;
```

- [ ] **Step 2 : (défense en profondeur) neutraliser le bip/menu sur `WM_SYSKEYDOWN` ALT seul**

Toujours dans le `switch`, ajouter :

```cpp
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
			// Input a déjà reçu le message via m_msgHook (ligne ~805). On retourne 0
			// pour empêcher DefWindowProc de traiter ALT comme entrée de menu (évite
			// le bip système et toute amorce de boucle menu). N'affecte pas la capture
			// clavier : Input::HandleMessage est appelé en amont inconditionnellement.
			return 0;
```

Note : `m_msgHook(msg, wparam, lparam)` (ligne 803-806) est appelé **avant** ce `switch`, donc `Input` voit toujours le message. Retourner 0 ici ne prive pas `Input` de l'événement.

- [ ] **Step 3 : vérifier qu'aucun autre code ne dépend du menu système ALT**

```bash
git grep -n "WM_SYSCOMMAND\|SC_KEYMENU\|WM_SYSKEYDOWN" src/shared/platform
```
Attendu : seuls `Input.cpp` (capture) et le nouveau code `Window.cpp`. Pas de menu applicatif Win32 attaché (le jeu rend via Vulkan/ImGui).

- [ ] **Step 4 : commit**

```bash
git add src/shared/platform/Window.cpp
git commit -m "fix(client): bloquer le menu système ALT (saccadement course ALT)"
```

**Vérif manuelle :** en jeu, courir en maintenant ALT = aussi fluide que SHIFT, aucune micro-coupure à l'appui/relâché ; ALT reste rebindable (menu Contrôles). Vérifier qu'Alt+F4 ferme toujours la fenêtre (géré par `WM_CLOSE`, non impacté car SC_CLOSE ≠ SC_KEYMENU).

---

## Task 4 (A3) : Ciblage TAB — filtre frustum + diagnostic de sélection

**Files:**
- Modify: `src/client/app/Engine.cpp:10685-10717` (bloc TAB).
- Read d'abord : repérer dans `Engine` l'accès à la matrice **view-projection** de la frame courante (celle utilisée pour le rendu monde). Chercher la caméra / la VP.

```bash
git grep -n "PerspectiveVulkan\|m_camera\|viewProj\|ViewProjection\|m_viewProj" src/client/app/Engine.cpp | head -40
```

**Contexte :** le bloc TAB (10686-10717) collecte les `remoteEntities`, filtre par archétype/état, trie par distance XZ, et appelle `m_uiModelBinding.SetLocalTarget(...)`. Deux problèmes : (a) aucun filtre de visibilité (un ennemi dans le dos est ciblable) ; (b) l'utilisateur rapporte que TAB ne sélectionne « rien » — à diagnostiquer.

### Sous-tâche 4a — Diagnostic « TAB ne sélectionne rien »

- [ ] **Step 1 : instrumenter temporairement le bloc TAB**

Ajouter, dans le `if (... Key::Tab)` juste avant `if (!candidates.empty())` :

```cpp
LOG_INFO(Core, "[TabTarget] candidates={} hasTarget={} curTargetId={}",
	candidates.size(), uiModel.targetStats.hasTarget,
	uiModel.targetStats.hasTarget ? uiModel.targetStats.entityId : 0u);
```

- [ ] **Step 2 : reproduire en jeu et lire le log**

Lancer le client (via VS / build CI déployé), entrer en jeu avec des mobs à l'écran, presser TAB.
- Si `candidates=0` alors que des mobs sont visibles → la cause est le **filtre** (10692-10693) : `archetypeId==0`, bit mort `stateFlags&1`, ou seuil `kGatheringNodeArchetypeBase` trop bas qui exclut les vrais ennemis. Inspecter les `archetypeId` réels des mobs (ajouter un log par entité) et corriger la condition.
- Si `candidates>0` mais la cible ne change pas visuellement → `SetLocalTarget` n'est pas reflété dans l'UI cible (vérifier `m_uiModelBinding.SetLocalTarget` et la propagation vers `uiModel.targetStats`).

- [ ] **Step 3 : appliquer le correctif de la cause identifiée**

Selon le diagnostic, corriger soit la condition de filtre (10692-10693), soit la propagation de `SetLocalTarget`. (Utiliser `superpowers:systematic-debugging` : une hypothèse à la fois, vérifiée par le log avant correction.)

- [ ] **Step 4 : retirer l'instrumentation temporaire** (les `LOG_INFO` de diag) une fois la cause corrigée.

### Sous-tâche 4b — Filtre frustum (ennemis non visibles exclus)

- [ ] **Step 5 : ajouter un helper de visibilité écran**

Au-dessus du bloc TAB (fonction libre anonyme ou lambda locale), ajouter — en réutilisant la VP de la frame repérée au Read initial (nommée ici `viewProj`) :

```cpp
// Vrai si la position monde se projette dans le frustum caméra (visible à
// l'écran) : devant la caméra (w > 0) et NDC x,y dans [-1,1]. Sert à interdire
// le ciblage TAB d'un ennemi hors écran / dans le dos du joueur.
auto isOnScreen = [](const engine::math::Mat4& viewProj, const engine::math::Vec3& world) -> bool {
	const engine::math::Vec4 clip = viewProj * engine::math::Vec4{world.x, world.y, world.z, 1.f};
	if (clip.w <= 0.0001f)
		return false; // derrière la caméra
	const float ndcX = clip.x / clip.w;
	const float ndcY = clip.y / clip.w;
	return ndcX >= -1.f && ndcX <= 1.f && ndcY >= -1.f && ndcY <= 1.f;
};
```

Adapter `Vec4` / l'opérateur `Mat4 * Vec4` aux signatures réelles de `src/shared/math/Math.h` (vérifier le nom exact et l'ordre ligne/colonne ; si seul `Mat4 * Vec4` n'existe pas, utiliser la fonction de transformation disponible). Récupérer la `viewProj` exacte utilisée par le rendu monde (même matrice, sinon le test ne correspond pas à « ce qu'on voit »).

- [ ] **Step 6 : filtrer les candidats avec le helper**

Dans la boucle de collecte (après le `continue` de filtre archétype, lignes 10692-10694), ajouter :

```cpp
const engine::math::Vec3 enemyPos{re.positionX, re.positionY, re.positionZ};
if (!isOnScreen(viewProj, enemyPos))
	continue; // ennemi hors écran / dans le dos : non ciblable
```

(Insérer juste avant le calcul `dxc/dzc`.)

- [ ] **Step 7 : commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "fix(client): ciblage TAB — filtre frustum + correction sélection"
```

**Vérif manuelle :** TAB sélectionne l'ennemi visible le plus proche ; cycle uniquement entre ennemis **à l'écran** ; un ennemi derrière le joueur (hors champ) n'est jamais ciblé ; se retourner le rend ciblable.

---

## Task 5 (B1) : Thèmes de race (recolor + persistance via LnTheme)

**Files:**
- Modify: `src/client/render/LnTheme.h` (ajouter 6 entrées au registre).
- Modify: `src/client/render/AuthImGuiRenderer.cpp:966-1044` (`DrawAuthTweaksPanel` : câbler les boutons).
- Modify: fichier(s) i18n des clés `options.interface.theme.<id>` (localiser).
- Test: `src/world_editor/tests/` ou suite client — voir Step 4 (test de registre).

**Contexte :** `LnTheme` est un registre `std::array<Entry, N>` (`LnTheme.h:81-88`) ; `SetActive(name)` mute la palette en place ; persistance = `ui_theme.json` (cf. `AuthImGuiOptions.cpp:431-435`). Les boutons de race (`AuthImGuiRenderer.cpp:1018-1042`) ne font aujourd'hui que `m_langTweakRace = idx`. Libellés : `{"DEFAUT","HUMAINS","ELFES","NAINS","ORCS","DEMONS"}` (ordre des indices 0..5).

### Mapping couleurs (source : spec)

Chaque thème de race dérive de la structure `Palette` (12 rôles). On fixe `primary` = dominante, `accent` = accent ; `background`/`surface`/`panel` = teintes sombres dérivées de la dominante ; `text`/`muted`/`success`/`warning`/`errorCol` repris d'`or_royal` (lisibilité + sémantique danger conservée). `secondary` = dominante éclaircie. `border` = dominante désaturée.

Hex → RGBA float (÷255) :
- Humains : dominante `#4A6FA5`→(0.290,0.435,0.647), accent `#C0C8D0`→(0.753,0.784,0.816)
- Orkhs : dominante `#5A6B3B`→(0.353,0.420,0.231), accent `#D8CFA8`→(0.847,0.812,0.659)
- Démons : dominante `#8B1A1A`→(0.545,0.102,0.102), accent `#FF5722`→(1.000,0.341,0.133)
- Nains : dominante `#8C5A2B`→(0.549,0.353,0.169), accent `#D4A017`→(0.831,0.627,0.090)
- Elfes : dominante `#5E4B8B`→(0.369,0.294,0.545), accent `#C9BCE0`→(0.788,0.737,0.878)
- Défaut : alias d'`or_royal` (id `defaut` non requis — voir Step 2).

- [ ] **Step 1 : ajouter les 5 palettes de race dans `LnTheme.h`**

Dans `namespace detail`, après `kSylveEmeraude` (ligne 71), ajouter (exemple Humains ; répéter pour les 4 autres avec leurs hex). Pour chaque race : `background/surface/panel` = dominante multipliée par ~0.10/0.16/0.20 ; `secondary` = dominante +30 % vers blanc ; `border` = dominante ×0.7 ; le reste copié d'`kOrRoyal`.

```cpp
		// Thèmes de race (spec 2026-06-14) : primary = couleur dominante de la
		// race, accent = couleur d'accent. Fonds sombres dérivés de la dominante.
		// text/muted/success/warning/errorCol repris d'or_royal (lisibilité +
		// sémantique danger). secondary/border dérivés de la dominante.
		inline constexpr Palette kRaceHumains{
			/*primary   */ {0.290f, 0.435f, 0.647f, 1.f}, // #4A6FA5
			/*secondary */ {0.470f, 0.590f, 0.750f, 1.f},
			/*accent    */ {0.753f, 0.784f, 0.816f, 1.f}, // #C0C8D0
			/*background */ {0.029f, 0.044f, 0.065f, 1.f},
			/*surface   */ {0.046f, 0.070f, 0.104f, 1.f},
			/*panel     */ {0.058f, 0.087f, 0.129f, 1.f},
			/*text      */ {0.949f, 0.957f, 0.973f, 1.f},
			/*muted     */ {0.608f, 0.659f, 0.722f, 1.f},
			/*border    */ {0.203f, 0.305f, 0.453f, 1.f},
			/*success   */ {0.373f, 0.722f, 0.431f, 1.f},
			/*warning   */ {0.910f, 0.647f, 0.361f, 1.f},
			/*errorCol  */ {0.769f, 0.251f, 0.251f, 1.f},
		};
		// kRaceOrkhs : dominante #5A6B3B / accent #D8CFA8
		// kRaceDemons : dominante #8B1A1A / accent #FF5722
		// kRaceNains  : dominante #8C5A2B / accent #D4A017
		// kRaceElfes  : dominante #5E4B8B / accent #C9BCE0
```

Créer `kRaceOrkhs`, `kRaceDemons`, `kRaceNains`, `kRaceElfes` sur le même modèle (primary/accent exacts ci-dessus ; fonds = primary ×{0.10,0.16,0.20} ; secondary = primary éclaircie ; border = primary ×0.7).

- [ ] **Step 2 : enregistrer les nouveaux thèmes dans le registre**

Remplacer `Registry()` (`LnTheme.h:81-88`) pour passer de `array<Entry,2>` à `array<Entry,7>` :

```cpp
		inline const std::array<Entry, 7>& Registry()
		{
			static const std::array<Entry, 7> kRegistry{{
				{"or_royal", kOrRoyal},
				{"sylve_emeraude", kSylveEmeraude},
				{"humains", kRaceHumains},
				{"orkhs", kRaceOrkhs},
				{"demons", kRaceDemons},
				{"nains", kRaceNains},
				{"elfes", kRaceElfes},
			}};
			return kRegistry;
		}
```

Le bouton « DEFAUT » mappera vers `or_royal` (thème neutre existant) — pas de nouvel id `defaut`.

- [ ] **Step 3 : câbler les boutons de race (`DrawAuthTweaksPanel`)**

Dans `AuthImGuiRenderer.cpp`, remplacer le corps du `if (ImGui::Button(...))` (lignes 1031-1034) :

```cpp
					if (ImGui::Button(id, ImVec2(btnW, 0.f)))
					{
						m_langTweakRace = idx;
						// Mapping indice bouton -> id thème LnTheme. Ordre des libellés :
						// {DEFAUT, HUMAINS, ELFES, NAINS, ORCS, DEMONS}.
						static constexpr const char* kRaceThemeIds[] = {
							"or_royal", "humains", "elfes", "nains", "orkhs", "demons"};
						if (LnTheme::SetActive(kRaceThemeIds[idx]))
						{
							// Persistance ui_theme.json (même pipeline que le combo Options).
							const std::string js =
								std::string("{\n  \"ui\": { \"theme\": \"")
								+ std::string(LnTheme::ActiveName()) + "\" }\n}\n";
							(void)engine::platform::FileSystem::WriteAllText("ui_theme.json", js);
						}
					}
```

Vérifier l'include de `FileSystem` dans `AuthImGuiRenderer.cpp` (présent dans `AuthImGuiOptions.cpp` ; l'ajouter si manquant : `#include "src/shared/platform/FileSystem.h"`).

Note : « ORCS » (libellé bouton, idx 4) → id thème `orkhs` (cohérent factions.json / spec « Orkhs (Dzorak) »).

- [ ] **Step 4 : synchroniser l'état sélectionné au thème actif**

Pour que la grille reflète le thème courant à l'ouverture (pas seulement le dernier clic), initialiser `m_langTweakRace` depuis `LnTheme::ActiveName()` au début de `DrawAuthTweaksPanel` (mapping inverse ; `or_royal`/inconnu → 0). Ajouter avant la boucle (après ligne 1008) :

```cpp
	// Refléter le thème actif dans la sélection de la grille (mapping inverse).
	{
		const std::string_view cur = LnTheme::ActiveName();
		const char* order[] = {"or_royal","humains","elfes","nains","orkhs","demons"};
		for (int i = 0; i < 6; ++i)
			if (cur == order[i]) { m_langTweakRace = i; break; }
	}
```

- [ ] **Step 5 : ajouter les clés i18n des thèmes de race**

```bash
git grep -rln "options.interface.theme.or_royal" -- . | head
```
Dans le(s) fichier(s) de locale trouvé(s), ajouter les clés (FR au minimum, autres langues si présentes) :
`options.interface.theme.humains` = « Humains », `...orkhs` = « Orkhs », `...demons` = « Démons », `...nains` = « Nains », `...elfes` = « Elfes ». (`or_royal`/`sylve_emeraude` existent déjà.)

- [ ] **Step 6 : test unitaire du registre (compilable côté serveur/CI)**

`LnTheme.h` est header-only sans dépendance ImGui → testable. Ajouter un test (réutiliser une cible de test existante de la suite ; localiser via `git grep -n "LnTheme" src/**/tests`) :

```cpp
// Vérifie que les 7 thèmes sont enregistrés et que SetActive applique la palette.
TEST(LnThemeRegistry, RaceThemesRegisteredAndApplied) {
	const auto names = LnTheme::Names();
	EXPECT_EQ(names.size(), 7u);
	EXPECT_TRUE(LnTheme::SetActive("demons"));
	// Démons : primary = #8B1A1A.
	EXPECT_NEAR(LnTheme::Active().primary.r, 0.545f, 0.01f);
	EXPECT_FALSE(LnTheme::SetActive("inexistant"));
	LnTheme::SetActive("or_royal"); // restaurer l'état par défaut
}
```

Si aucune cible de test ne référence `LnTheme`, créer un petit test dans la suite client/ shared la plus proche et l'ajouter au `CMakeLists` de tests. Si l'intégration test est trop lourde pour ce header UI, **documenter** la vérification manuelle à la place et ne pas bloquer.

- [ ] **Step 7 : commit**

```bash
git add src/client/render/LnTheme.h src/client/render/AuthImGuiRenderer.cpp <fichiers i18n> <test>
git commit -m "feat(client): thèmes de race auth (recolor live + persistance ui_theme.json)"
```

**Vérif manuelle :** sur l'auth, cliquer chaque race recolore immédiatement fond/cadres/accents aux bonnes couleurs ; le choix survit à un redémarrage (relecture `ui_theme.json` au boot) ; « DEFAUT » revient à or_royal.

---

## Task 6 (B2) : Unifier le menu d'options (réutiliser l'écran auth en jeu)

**Files:**
- Read d'abord : `src/client/render/AuthImGuiRenderer.h` (signature `RenderOptionsScreen`, membres d'état options `m_opt*`, et comment l'écran lit/écrit la config + gère Retour/Appliquer).
- Read d'abord : `src/client/app/Engine.cpp` autour de `m_authImGui->Render(...)` (comment l'auth construit le `RenderModel` + `VisualState` pour l'écran options).
- Modify: `src/client/app/Engine.cpp:11959-11963` (bouton « Options » du menu Pause — inchangé fonctionnellement : continue de poser le flag).
- Modify: `src/client/app/Engine.cpp:11978-12150+` (SUPPRIMER le mini-panneau, le remplacer par un appel au renderer auth options).
- Modify: `src/client/app/Engine.h` (éventuel : état options en jeu).

**Contrainte utilisateur :** ne PAS toucher au menu Pause (ESC) lui-même (lignes 11888-11977). Seul son lien « Options » doit ouvrir l'écran d'options auth. Le flag `m_inGameOptionsPanelVisible` est conservé comme déclencheur, mais **ce qu'il affiche change**.

- [ ] **Step 1 : comprendre la surface d'appel de `RenderOptionsScreen`**

Lire `AuthImGuiRenderer.h` : noter la signature exacte (`RenderOptionsScreen(const RenderModel&, ImVec2)`), d'où viennent les valeurs (`m_optUiScalePct`, `m_optPanelOpacityPct`, etc. — chargées depuis la config), et comment l'écran signale « Retour » (fermeture). Identifier le `RenderModel` minimal requis (probablement juste libellés/i18n + viewport). Vérifier comment l'auth déclenche `RenderOptionsScreen` (via `VisualState.options`).

- [ ] **Step 2 : exposer un point d'entrée « options overlay » réutilisable**

Deux options selon ce que révèle le Step 1 :
- **(préféré) Méthode publique dédiée** sur `AuthImGuiRenderer`, ex. `void RenderOptionsOverlay(ImVec2 vp);` qui fait le `Begin/End` de l'écran options sans dépendre de la machine à états auth (réutilise le corps de `RenderOptionsScreen`). Si `RenderOptionsScreen` dépend d'un `RenderModel`, fournir une surcharge construisant un `RenderModel` minimal en interne (i18n via le même `tr()`).
- **(repli)** Construire dans `Engine` le `RenderModel`/`VisualState` minimal et appeler `m_authImGui->RenderOptionsScreen(model, vp)` directement.

Documenter la nouvelle méthode (`///` Doxygen : rôle, effet de bord ImGui, thread main).

- [ ] **Step 3 : remplacer le mini-panneau par l'appel à l'écran options auth**

Dans `Engine.cpp`, supprimer entièrement le bloc `if (m_inGameOptionsPanelVisible) { ... }` (du commentaire ligne 11978 jusqu'à la fin du bloc, ≥ ligne 12150 — inclure le bouton « Fermer » et la fin `ImGui::End()`), et le remplacer par :

```cpp
			// Menu Options en jeu : on réutilise EXACTEMENT l'écran d'options de
			// l'auth (source unique, cf. spec 2026-06-14). Le menu Pause (ESC) reste
			// inchangé ; seul son lien « Options » mène ici.
			if (m_inGameOptionsPanelVisible)
			{
				const ImVec2 vp(static_cast<float>(dw), static_cast<float>(dh));
				const bool stillOpen = m_authImGui->RenderOptionsOverlay(vp); // false quand l'utilisateur ferme/Retour
				if (!stillOpen)
					m_inGamePauseMenuVisible = true; // retour au menu Pause
				m_inGameOptionsPanelVisible = stillOpen;
			}
```

Faire renvoyer à `RenderOptionsOverlay` un `bool` « encore ouvert » (false sur clic Retour/Fermer/Échap) pour piloter la fermeture côté Engine. Adapter si le Step 1 montre un autre mécanisme de fermeture.

- [ ] **Step 4 : supprimer l'état et le code mort du mini-panneau**

- Retirer les usages spécifiques au mini-panneau désormais morts s'ils ne servent plus ailleurs : variables locales de rebind in-game (`m_rebindingAction`, `m_keybindWarning`, `kRebindableKeys`) **uniquement si** elles ne sont plus utilisées (l'écran auth gère ses propres contrôles). Vérifier par `git grep` avant suppression — NE PAS retirer ce qui reste référencé.

```bash
git grep -n "m_rebindingAction\|m_keybindWarning\|kRebindableKeys\|m_inGameOptionsPanelVisible" src/client
```
Supprimer uniquement les symboles sans plus aucun usage hors du bloc retiré. Garder `m_inGameOptionsPanelVisible` (toujours utilisé comme déclencheur).

- [ ] **Step 5 : cohérence de persistance**

S'assurer que l'écran options auth persiste comme avant (`ui_theme.json`, config, keybinds) — il a déjà sa propre logique (cf. `AuthImGuiOptions.cpp`). Vérifier qu'en jeu les changements (volume, vsync, sensibilité, thème, keybinds) s'appliquent runtime comme le faisait le mini-panneau (au besoin, brancher les mêmes effets live : `m_audioEngine.SetMasterVolume`, etc., si l'écran auth ne le fait pas en contexte in-game).

- [ ] **Step 6 : commit**

```bash
git add src/client/app/Engine.cpp src/client/app/Engine.h src/client/render/AuthImGuiRenderer.cpp src/client/render/AuthImGuiRenderer.h
git commit -m "refactor(client): menu Pause Options ouvre l'écran d'options auth (source unique)"
```

**Vérif manuelle :** en jeu, ESC → menu Pause inchangé ; clic « Options » → ouvre le **même** écran complet que l'auth (7 onglets) ; Retour/Fermer → revient au menu Pause ; réglages appliqués/persistés ; plus aucune trace de l'ancien mini-panneau.

---

## Task 7 (C) : Mail de validation CGU — diagnostic + correctif

**Files:**
- Read : `src/masterd/handlers/terms/TermsHandler.cpp:104-167` (déjà analysé : envoi conditionné à `m_smtp && host && m_accounts && ar && !ar->email.empty()`).
- Read : impl concrète d'`AccountStore` (interface `src/masterd/account/AccountStore.h` ; trouver l'impl MySQL : `git grep -ln "FindByAccountId" src/masterd`).
- Read : `src/masterd/handlers/auth/AuthRegisterHandler.cpp:205-238` (flux mail inscription qui marche : source de l'email utilisée).
- Modify (selon cause) : impl `AccountStore` (persistance/lecture email) **ou** `TermsHandler.cpp` (résolution session) + logs.

**Méthode :** `superpowers:systematic-debugging`, par revue de code. Câblage `main_linux.cpp` déjà vérifié **complet** (`SetAccountStore` 350, `SetSmtpConfig` 352) → hypothèse « non câblé » écartée. Hypothèses restantes par ordre de suspicion :

1. **Email non persisté / non relu** : l'inscription envoie le mail avec l'email **fraîchement saisi** (`parsed->email`), mais `FindByAccountId` renvoie un `AccountRecord` à `email` **vide** → condition `!ar->email.empty()` fausse → mail CGU sauté silencieusement (l'acceptation, elle, réussit).
2. **Session/account_id non résolu depuis l'interface du jeu** : `m_connMap->GetSessionId(connId)` ou `m_sessions->GetAccountId(*sess)` renvoie `nullopt` en contexte in-game → `return` précoce (124-128) → ni acceptation ni mail. (Si l'acceptation CGU fonctionne réellement en jeu, cette hypothèse tombe.)

- [ ] **Step 1 : déterminer si l'acceptation CGU elle-même réussit en jeu**

Confirmer (côté produit/log) si, depuis l'interface du jeu, l'acceptation est **enregistrée** (CGU non re-demandées ensuite) bien que le mail manque. 
- Si **oui** → hypothèse 1 (email vide). Aller Step 2.
- Si **non** (acceptation aussi KO) → hypothèse 2 (session). Aller Step 4.

- [ ] **Step 2 : vérifier la persistance de l'email dans l'impl `AccountStore`**

```bash
git grep -ln "FindByAccountId" src/masterd
```
Lire l'impl : (a) `CreateAccount` écrit-il bien la colonne/champ `email` ? (b) `FindByAccountId` sélectionne-t-il et remplit-il `AccountRecord::email` (et `email_locale`) ? Comparer avec `FindByLogin`/le chemin utilisé à l'inscription.

- [ ] **Step 3 : corriger la persistance/lecture de l'email** (si c'est la cause)

Selon le défaut trouvé : ajouter la colonne `email` au `SELECT` de `FindByAccountId`, ou persister `email` dans `CreateAccount`, de sorte que `ar->email`/`ar->email_locale` soient renseignés. Écrire/étendre un **test unitaire** (suite serveur, exécutée par `ctest`) :

```cpp
// Après création d'un compte avec email, FindByAccountId doit renvoyer l'email.
TEST(AccountStore, FindByAccountIdReturnsEmail) {
	auto store = MakeTestAccountStore();           // helper de la suite existante
	const uint64_t id = store->CreateAccount("tag_test", "joueur@example.com",
		"client_hash", /*...*/);
	ASSERT_GT(id, 0u);
	auto rec = store->FindByAccountId(id);
	ASSERT_TRUE(rec.has_value());
	EXPECT_EQ(rec->email, "joueur@example.com");
}
```
Adapter à la fabrique de test et à la signature réelle de `CreateAccount`. Faire échouer d'abord (si bug confirmé), puis corriger.

- [ ] **Step 4 : (si hypothèse 2) tracer la résolution de session in-game**

Ajouter des logs distinctifs dans `TermsHandler::HandlePacket` (branche `kOpcodeTermsAcceptRequest`) pour lever l'ambiguïté du `return` muet :

```cpp
				auto sess = m_connMap->GetSessionId(connId);
				if (!sess) { LOG_WARN(Auth, "[TermsHandler] accept: pas de session pour connId={}", connId); return; }
				auto accOpt = m_sessions->GetAccountId(*sess);
				if (!accOpt) { LOG_WARN(Auth, "[TermsHandler] accept: session sans account_id (connId={})", connId); return; }
```

Puis, si la session in-game n'a pas d'account_id, corriger le chemin in-game qui envoie l'opcode CGU pour qu'il utilise une connexion master avec session authentifiée (ou router l'acceptation par le canal authentifié approprié). La correction précise dépend de l'architecture session révélée — rester dans `systematic-debugging`.

- [ ] **Step 5 : durcir les logs de diagnostic (toujours utile)**

Ajouter une trace quand le mail est **sauté** faute d'email, pour les diagnostics futurs (juste après la condition ligne 143-146) :

```cpp
				if (m_smtp && !m_smtp->host.empty() && m_accounts)
				{
					auto ar = m_accounts->FindByAccountId(*accOpt);
					if (!ar || ar->email.empty())
						LOG_WARN(Auth, "[TermsHandler] mail CGU sauté : email absent pour account_id={}", *accOpt);
					else { /* envoi existant */ }
				}
				else
					LOG_WARN(Auth, "[TermsHandler] mail CGU sauté : smtp/accounts non disponibles");
```

- [ ] **Step 6 : commit**

```bash
git add src/masterd/...
git commit -m "fix(masterd): mail de validation CGU — <cause identifiée> + logs diagnostic"
```

- [ ] **Step 7 : exécuter la suite serveur**

La CI `build-linux` lance `ctest`. Vérifier que les tests `AccountStore`/terms passent. (Attention `assert`+`NDEBUG`, cf. mémoire CI.)

**Déploiement (à figer après diagnostic) :** ⚠️ **redéploiement master requis** (correctif handler/AccountStore serveur).

---

## Task 8 : Intégration finale, CI, PR unique

- [ ] **Step 1 : revue de cohérence locale**

```bash
git log --oneline main..HEAD
git diff --stat main..HEAD
```
Vérifier que les 7 items sont présents et qu'aucun fichier hors périmètre n'est touché (pas de `legacy/`).

- [ ] **Step 2 : pousser et laisser la CI compiler**

```bash
git push -u origin fix/corrections-auth-ingame-cgu
```
Surveiller `build-windows` (client) et `build-linux`+`ctest` (serveur) — cf. mémoire : 1er check Windows ~29-30 min ; monitors en `gh`+`grep` (pas de `jq`).

- [ ] **Step 3 : corriger les éventuelles erreurs CI**, recommit, repush jusqu'au vert.

- [ ] **Step 4 : ouvrir la PR unique**

Titre : `fix: corrections auth/in-game + mail CGU (Ctrl shortcuts, Enter, TAB, ALT, thèmes race, menu options, CGU)`.
Description : lister les 7 items + la ligne **Déploiement** : ⚠️ redéploiement serveur **master** requis (Lot C) ; client neuf + master neuf en **lock-step**. Préciser la cause CGU trouvée.

```bash
gh pr create --base main --head fix/corrections-auth-ingame-cgu --title "..." --body "..."
```

- [ ] **Step 5 : indiquer à l'utilisateur quand/comment merger** (CI verte + déploiement lock-step client+master), conformément à la convention du repo.

---

## Self-review (couverture spec)

- A1 Ctrl+R/O/F → Task 1 ✅
- A2 Enter login → Task 2 ✅
- A3 TAB frustum + diag sélection → Task 4 ✅
- A4 saccadement ALT → Task 3 ✅
- B1 thèmes race recolor+persist → Task 5 ✅
- B2 unification menu options → Task 6 ✅
- C mail CGU → Task 7 ✅
- PR unique + déploiement lock-step → Task 8 ✅

**Points nécessitant une lecture d'exécution (pas des placeholders, mais des ancrages explicites)** : noms exacts des buffers login (Task 2), accesseur `viewProj` réel + API `Mat4*Vec4` (Task 4), signature/fermeture de `RenderOptionsScreen` (Task 6), impl concrète `AccountStore` (Task 7). Chaque cas indique le fichier précis à lire en première étape de sa tâche.
