# Fenêtre Personnage unifiée (Chantier 1) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Regrouper les panneaux personnage (F1/F2/F3/F4/I) dans une seule fenêtre à onglets ouverte par F1, onglet par défaut = perso 3D + inventaire + caractéristiques + argent.

**Architecture:** Un nouveau renderer conteneur `CharacterWindowImGuiRenderer` dessine une fenêtre ImGui à onglets, rendu au même point de frame que les panneaux existants (single-pass → corrige le doublon d'inventaire). Les onglets Compétences/Techniques/Arbre délèguent aux renderers existants passés en mode « embedded » (sans leur propre `Begin`). L'onglet Personnage assemble `RacePreviewViewport` (3D), `InventoryUiPresenter` (grille) et `CurrencyFormat.h` (argent).

**Tech Stack:** C++17, Dear ImGui (draw list + widgets), Vulkan (via `RacePreviewViewport` offscreen), CMake.

## Global Constraints

- **Client uniquement** — aucun opcode, handler, migration ; pas de redéploiement serveur.
- **Pas de toolchain locale** — compilation via CI uniquement ; le code client ImGui est sous `#if defined(_WIN32)` (non compilé/testé sur la CI Linux ctest). **Vérification = CI Windows verte + test en jeu.** Pas de cycle TDD local.
- **PascalCase** pour tout nouveau fichier/classe/méthode ; commentaires en français.
- **Ne pas toucher** aux `frontFace`/`cullMode` d'aucun pipeline Vulkan (garde anti-régression CLAUDE.md). Le pipeline 3D de `RacePreviewViewport` existe déjà — le réutiliser tel quel.
- Nouveau `.cpp` client → l'ajouter à la liste des sources dans **`CMakeLists.txt` racine** (les renderers y sont, ~ligne 635-651). NE PAS l'ajouter à `server_app`.

## Prérequis (avant Task 1)

Ce chantier consomme #958 (bloc inventaire à retirer) et #959 (`CurrencyFormat.h`).

- [ ] Merger #957 → #958 → #959 dans `main`.
- [ ] Recréer la branche de travail sur `main` à jour et y porter le commit de spec :
  `git fetch origin && git checkout -B claude/character-window origin/main && git cherry-pick <sha-spec>` (ou rebase de la branche spec existante sur `origin/main`).

---

## File Structure

- **Create** `src/client/render/CharacterWindowImGuiRenderer.h` — interface du conteneur à onglets.
- **Create** `src/client/render/CharacterWindowImGuiRenderer.cpp` — rendu fenêtre + onglets + onglet Personnage.
- **Modify** `src/client/render/SkillBookImGuiRenderer.{h,cpp}` — ajout `SetEmbedded(bool)`.
- **Modify** `src/client/render/GrimoireImGuiRenderer.{h,cpp}` — ajout `SetEmbedded(bool)`.
- **Modify** `src/client/render/ClassSkillTreeImGuiRenderer.{h,cpp}` — ajout `SetEmbedded(bool)`.
- **Modify** `src/client/app/Engine.h` — membre renderer + retrait `m_inventoryVisible`.
- **Modify** `src/client/app/Engine.cpp` — init/bind, appel Render dans le bloc panneaux, F1 toggle, retrait des toggles F2/F3/F4/I et du bloc inventaire #958.
- **Modify** `CMakeLists.txt` — ajout `CharacterWindowImGuiRenderer.cpp`.

---

## Task 1 : Mode « embedded » sur les 3 renderers de panneaux

**Files:**
- Modify: `src/client/render/GrimoireImGuiRenderer.h`, `src/client/render/GrimoireImGuiRenderer.cpp`
- Modify: `src/client/render/SkillBookImGuiRenderer.h`, `src/client/render/SkillBookImGuiRenderer.cpp`
- Modify: `src/client/render/ClassSkillTreeImGuiRenderer.h`, `src/client/render/ClassSkillTreeImGuiRenderer.cpp`

**Interfaces:**
- Produces (chaque renderer) : `void SetEmbedded(bool e)` — quand `true`, `Render()` dessine son contenu **sans** ouvrir sa propre fenêtre `ImGui::Begin/End` (le conteneur fournit déjà l'onglet courant).

- [ ] **Step 1 : Header — ajouter le flag (Grimoire, identique pour les 2 autres)**

Dans `GrimoireImGuiRenderer.h`, après `SetEnabled` :

```cpp
		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const { return m_enabled; }
		/// Mode embarqué : Render() dessine son contenu dans la fenêtre/onglet
		/// courant sans ouvrir sa propre fenêtre ImGui (piloté par le conteneur
		/// CharacterWindowImGuiRenderer). Défaut false = fenêtre autonome.
		void SetEmbedded(bool e) { m_embedded = e; }
```

Et dans la section `private:` : `bool m_embedded = false;`

- [ ] **Step 2 : Impl — court-circuiter Begin/End (Grimoire)**

Dans `GrimoireImGuiRenderer.cpp::Render()`, remplacer le motif fenêtre autonome. Structure cible :

```cpp
	void GrimoireImGuiRenderer::Render()
	{
		if (m_presenter == nullptr) return;
		if (!m_embedded && !m_enabled) return;

		bool open = true;
		if (!m_embedded)
		{
			const float vpW = static_cast<float>(m_viewportW);
			const float vpH = static_cast<float>(m_viewportH);
			ImGui::SetNextWindowPos(ImVec2((vpW - panelW) * 0.5f, (vpH - panelH) * 0.5f), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowBgAlpha(0.96f);
			open = ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoCollapse);
		}
		if (open)
		{
			// ... corps existant inchangé (listes, drag&drop, slots) ...
		}
		if (!m_embedded)
			ImGui::End();
	}
```

Conserver le corps existant à l'identique entre `if (open)`. Adapter les noms de variables locales (`panelW`, `title`, etc.) à ceux déjà présents dans le fichier.

- [ ] **Step 3 : Répéter Step 1-2 pour `SkillBookImGuiRenderer` et `ClassSkillTreeImGuiRenderer`**

Même transformation : header `SetEmbedded` + `m_embedded`, et dans `Render()` sauter le `Begin`/`End` quand embarqué, garder le corps. Vérifier la vraie signature/`Begin` de chaque fichier avant d'éditer (certains utilisent `SetNextWindow*` + `ImGui::Begin(...)`).

- [ ] **Step 4 : Commit**

```bash
git add src/client/render/GrimoireImGuiRenderer.h src/client/render/GrimoireImGuiRenderer.cpp \
        src/client/render/SkillBookImGuiRenderer.h src/client/render/SkillBookImGuiRenderer.cpp \
        src/client/render/ClassSkillTreeImGuiRenderer.h src/client/render/ClassSkillTreeImGuiRenderer.cpp
git commit -m "feat(ui): mode embedded (SetEmbedded) sur Grimoire/SkillBook/ClassSkillTree"
```

**Vérification :** CI Windows verte. Comportement inchangé en jeu (embedded défaut false ; F2/F3/F4 ouvrent encore leurs fenêtres comme avant). Pas de test en jeu requis à ce stade.

---

## Task 2 : `CharacterWindowImGuiRenderer` (conteneur à onglets)

**Files:**
- Create: `src/client/render/CharacterWindowImGuiRenderer.h`
- Create: `src/client/render/CharacterWindowImGuiRenderer.cpp`
- Modify: `CMakeLists.txt` (ajout de la source, près de la ligne ~651)

**Interfaces:**
- Consumes : `engine::render::SkillBookImGuiRenderer`, `GrimoireImGuiRenderer`, `ClassSkillTreeImGuiRenderer` (avec `SetEmbedded`/`SetEnabled`/`Render`) ; `engine::client::InventoryUiPresenter::GetState()` ; `engine::client::UIModelBinding::GetModel()` ; `engine::client::SkillIconCache::GetOrLoad(const std::string&)` ; `engine::client::SplitCoins`/`FormatCoinsText` (`CurrencyFormat.h`) ; `engine::render::RacePreviewViewport::GetImguiTextureId()` (Task 4).
- Produces :
  - `void Bind(const engine::core::Config* cfg, const engine::client::UIModelBinding* uiBinding, const engine::client::InventoryUiPresenter* inv, engine::client::SkillIconCache* icons, engine::render::SkillBookImGuiRenderer* skillBook, engine::render::GrimoireImGuiRenderer* grimoire, engine::render::ClassSkillTreeImGuiRenderer* classTree)`
  - `void SetVisible(bool v)` / `bool IsVisible() const` / `bool* VisiblePtr()`
  - `void SetViewportSize(uint32_t w, uint32_t h)`
  - `void SetRaceViewport(engine::render::RacePreviewViewport* vp)` (Task 4 ; peut rester nul en Task 2/3 → placeholder)
  - `void Render(const engine::client::UIModel& model)` — à appeler entre NewFrame et Render, dans le bloc panneaux.

- [ ] **Step 1 : Header**

Créer `src/client/render/CharacterWindowImGuiRenderer.h` :

```cpp
#pragma once
// Fenêtre Personnage unifiée à onglets (Chantier 1). Regroupe perso 3D +
// inventaire + caractéristiques + argent (onglet défaut) et délègue aux
// renderers Compétences/Techniques/Arbre en mode embarqué. Rendu au même point
// de frame que les panneaux existants (single-pass) -> pas de doublon.

#include <cstdint>

namespace engine::core { class Config; }
namespace engine::client { class UIModelBinding; class InventoryUiPresenter; class SkillIconCache; struct UIModel; }
namespace engine::render { class SkillBookImGuiRenderer; class GrimoireImGuiRenderer; class ClassSkillTreeImGuiRenderer; class RacePreviewViewport; }

namespace engine::render
{
	class CharacterWindowImGuiRenderer
	{
	public:
		enum class Tab { Personnage = 0, Competences, Techniques, Arbre };

		void Bind(const engine::core::Config* cfg,
			const engine::client::UIModelBinding* uiBinding,
			const engine::client::InventoryUiPresenter* inv,
			engine::client::SkillIconCache* icons,
			engine::render::SkillBookImGuiRenderer* skillBook,
			engine::render::GrimoireImGuiRenderer* grimoire,
			engine::render::ClassSkillTreeImGuiRenderer* classTree);

		void SetRaceViewport(engine::render::RacePreviewViewport* vp) { m_raceViewport = vp; }
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }
		void SetVisible(bool v) { m_visible = v; }
		void ToggleVisible() { m_visible = !m_visible; }
		bool IsVisible() const { return m_visible; }
		bool* VisiblePtr() { return &m_visible; }
		void SetActiveTab(Tab t) { m_activeTab = t; }

		void Render(const engine::client::UIModel& model);

	private:
		void RenderPersonnageTab(const engine::client::UIModel& model);

		const engine::core::Config* m_cfg = nullptr;
		const engine::client::UIModelBinding* m_uiBinding = nullptr;
		const engine::client::InventoryUiPresenter* m_inv = nullptr;
		engine::client::SkillIconCache* m_icons = nullptr;
		engine::render::SkillBookImGuiRenderer* m_skillBook = nullptr;
		engine::render::GrimoireImGuiRenderer* m_grimoire = nullptr;
		engine::render::ClassSkillTreeImGuiRenderer* m_classTree = nullptr;
		engine::render::RacePreviewViewport* m_raceViewport = nullptr;
		uint32_t m_viewportW = 0;
		uint32_t m_viewportH = 0;
		bool m_visible = false;
		Tab m_activeTab = Tab::Personnage;
	};
}
```

- [ ] **Step 2 : Impl — cadre + barre d'onglets + délégation**

Créer `src/client/render/CharacterWindowImGuiRenderer.cpp`. Garder tout sous `#if defined(_WIN32)` (comme les autres renderers), stub vide sinon.

```cpp
#include "src/client/render/CharacterWindowImGuiRenderer.h"

#include "src/client/render/SkillBookImGuiRenderer.h"
#include "src/client/render/GrimoireImGuiRenderer.h"
#include "src/client/render/ClassSkillTreeImGuiRenderer.h"
#include "src/client/render/race/RacePreviewViewport.h"
#include "src/client/render/SkillIconCache.h"
#include "src/client/ui_common/UIModel.h"
#include "src/client/ui_common/CurrencyFormat.h"
#include "src/client/inventory/InventoryUi.h"
#include "src/shared/core/Config.h"

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	void CharacterWindowImGuiRenderer::Bind(const engine::core::Config* cfg,
		const engine::client::UIModelBinding* uiBinding,
		const engine::client::InventoryUiPresenter* inv,
		engine::client::SkillIconCache* icons,
		engine::render::SkillBookImGuiRenderer* skillBook,
		engine::render::GrimoireImGuiRenderer* grimoire,
		engine::render::ClassSkillTreeImGuiRenderer* classTree)
	{
		m_cfg = cfg; m_uiBinding = uiBinding; m_inv = inv; m_icons = icons;
		m_skillBook = skillBook; m_grimoire = grimoire; m_classTree = classTree;
	}

	void CharacterWindowImGuiRenderer::Render(const engine::client::UIModel& model)
	{
		if (!m_visible) return;

		const float vpW = static_cast<float>(m_viewportW);
		const float vpH = static_cast<float>(m_viewportH);
		const float winW = 640.0f, winH = 440.0f;
		ImGui::SetNextWindowPos(ImVec2((vpW - winW) * 0.5f, (vpH - winH) * 0.5f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.96f);

		if (ImGui::Begin("Personnage##ln_character_window", &m_visible, ImGuiWindowFlags_NoCollapse))
		{
			if (ImGui::BeginTabBar("##ln_character_tabs"))
			{
				if (ImGui::BeginTabItem("Personnage"))
				{
					m_activeTab = Tab::Personnage;
					RenderPersonnageTab(model);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Competences"))
				{
					m_activeTab = Tab::Competences;
					if (m_skillBook) { m_skillBook->SetEmbedded(true); m_skillBook->SetEnabled(true); m_skillBook->Render(); }
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Techniques"))
				{
					m_activeTab = Tab::Techniques;
					if (m_grimoire) { m_grimoire->SetEmbedded(true); m_grimoire->SetEnabled(true); m_grimoire->Render(); }
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Arbre"))
				{
					m_activeTab = Tab::Arbre;
					if (m_classTree) { m_classTree->SetEmbedded(true); m_classTree->SetEnabled(true); m_classTree->Render(); }
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
	}

	void CharacterWindowImGuiRenderer::RenderPersonnageTab(const engine::client::UIModel& model)
	{
		// Colonnes : gauche (3D + slots + stats), droite (inventaire + argent).
		const float leftW = ImGui::GetContentRegionAvail().x * 0.46f;
		ImGui::BeginChild("##ln_char_left", ImVec2(leftW, 0), false);
		{
			// Aperçu 3D (Task 4 : image du viewport ; ici placeholder si absent).
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const float previewH = 190.0f;
			if (m_raceViewport && m_raceViewport->GetImguiTextureId() != 0u)
			{
				ImGui::Image(static_cast<ImTextureID>(m_raceViewport->GetImguiTextureId()),
					ImVec2(avail.x, previewH));
			}
			else
			{
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const ImVec2 p0 = ImGui::GetCursorScreenPos();
				dl->AddRectFilled(p0, ImVec2(p0.x + avail.x, p0.y + previewH), IM_COL32(14, 16, 22, 255), 6.0f);
				dl->AddText(ImVec2(p0.x + avail.x * 0.5f - 30.0f, p0.y + previewH * 0.5f), IM_COL32(120, 130, 160, 255), "Apercu 3D");
				ImGui::Dummy(ImVec2(avail.x, previewH));
			}
			ImGui::TextDisabled("Equipement — Chantier 2");
			ImGui::Separator();
			// Caractéristiques compactes (ex-fiche F1).
			const engine::client::UIPlayerStats& ps = model.playerStats;
			ImGui::Text("Niveau %u", ps.level);
			ImGui::Text("Points de vie : %u", ps.maxHealth);
			ImGui::Text("Ressource : %u", ps.secondaryResourceMax);
			ImGui::Text("Degats : %u", ps.attackDamage);
		}
		ImGui::EndChild();

		ImGui::SameLine();

		ImGui::BeginChild("##ln_char_right", ImVec2(0, 0), false);
		{
			ImGui::TextUnformatted("Inventaire");
			// Grille 4x4 depuis InventoryUiPresenter (repris du bloc #958, single-pass ici).
			if (m_inv)
			{
				const engine::client::InventoryPanelState& gi = m_inv->GetState();
				const int cols = (gi.columns > 0u) ? static_cast<int>(gi.columns) : 4;
				const int count = static_cast<int>(gi.slots.size());
				const float cell = 48.0f, gap = 6.0f;
				ImDrawList* wdl = ImGui::GetWindowDrawList();
				const ImVec2 origin = ImGui::GetCursorScreenPos();
				for (int i = 0; i < count; ++i)
				{
					const engine::client::InventorySlotState& s = gi.slots[static_cast<size_t>(i)];
					const int r = i / cols, c = i % cols;
					const float x0 = origin.x + static_cast<float>(c) * (cell + gap);
					const float y0 = origin.y + static_cast<float>(r) * (cell + gap);
					const ImVec2 mn(x0, y0), mx(x0 + cell, y0 + cell);
					const bool occ = s.occupied && s.itemId != 0u;
					wdl->AddRectFilled(mn, mx, occ ? IM_COL32(26,30,40,235) : IM_COL32(16,18,24,200), 5.0f);
					wdl->AddRect(mn, mx, occ ? IM_COL32(150,130,60,220) : IM_COL32(64,66,74,180), 5.0f, 0, 1.5f);
					if (!occ) continue;
					bool hasIcon = false;
					if (m_icons && !s.iconPath.empty())
					{
						const uint64_t tex = m_icons->GetOrLoad(s.iconPath);
						if (tex != 0) { wdl->AddImage(static_cast<ImTextureID>(tex), ImVec2(x0+3,y0+3), ImVec2(mx.x-3,mx.y-3)); hasIcon = true; }
					}
					if (!hasIcon && !s.label.empty())
						wdl->AddText(ImVec2(x0+4,y0+5), IM_COL32(220,220,225,255), s.label.substr(0,7).c_str());
					if (s.quantity > 1u)
					{
						char q[12]; std::snprintf(q, sizeof(q), "%u", s.quantity);
						const ImVec2 qs = ImGui::CalcTextSize(q);
						wdl->AddText(ImVec2(mx.x-qs.x-3, mx.y-qs.y-2), IM_COL32(255,240,190,255), q);
					}
					if (ImGui::IsMouseHoveringRect(mn, mx) && !s.label.empty())
					{ ImGui::BeginTooltip(); ImGui::TextUnformatted(s.label.c_str()); ImGui::EndTooltip(); }
				}
				const int rows = (count + cols - 1) / cols;
				ImGui::Dummy(ImVec2(0, static_cast<float>(rows) * (cell + gap)));
			}
			// Bourse or/argent/bronze.
			const engine::client::CoinBreakdown coins = engine::client::SplitCoins(model.wallet.gold);
			ImGui::Separator();
			ImGui::Text("Or %u   Arg %u   Br %u", coins.gold, coins.silver, coins.bronze);
		}
		ImGui::EndChild();
	}
}

#else
namespace engine::render
{
	void CharacterWindowImGuiRenderer::Bind(const engine::core::Config*, const engine::client::UIModelBinding*,
		const engine::client::InventoryUiPresenter*, engine::client::SkillIconCache*,
		engine::render::SkillBookImGuiRenderer*, engine::render::GrimoireImGuiRenderer*,
		engine::render::ClassSkillTreeImGuiRenderer*) {}
	void CharacterWindowImGuiRenderer::Render(const engine::client::UIModel&) {}
	void CharacterWindowImGuiRenderer::RenderPersonnageTab(const engine::client::UIModel&) {}
}
#endif
```

> NB : vérifier les vrais noms de champs `UIPlayerStats` (`level`, `maxHealth`, `secondaryResourceMax`, `attackDamage`) et `UIWalletState.gold` dans `src/client/ui_common/UIModel.h` avant compilation ; ajuster si différents. `<cstdio>` pour `snprintf` : ajouter l'include si absent.

- [ ] **Step 3 : CMake**

Dans `CMakeLists.txt` racine, à côté de `src/client/render/GrimoireImGuiRenderer.cpp` (~ligne 651), ajouter :

```cmake
  src/client/render/CharacterWindowImGuiRenderer.cpp
```

- [ ] **Step 4 : Commit**

```bash
git add src/client/render/CharacterWindowImGuiRenderer.h src/client/render/CharacterWindowImGuiRenderer.cpp CMakeLists.txt
git commit -m "feat(ui): CharacterWindowImGuiRenderer (conteneur onglets Personnage)"
```

**Vérification :** CI Windows verte (la classe compile ; encore non instanciée → pas d'effet en jeu).

---

## Task 3 : Câblage Engine + consolidation des touches + retrait #958

**Files:**
- Modify: `src/client/app/Engine.h`
- Modify: `src/client/app/Engine.cpp`

**Interfaces:**
- Consumes : `CharacterWindowImGuiRenderer::Bind/SetViewportSize/ToggleVisible/Render` (Task 2).

- [ ] **Step 1 : Engine.h — membre + retrait m_inventoryVisible**

Ajouter l'include `#include "src/client/render/CharacterWindowImGuiRenderer.h"` près des autres renderers (~ligne 47-64). Ajouter le membre près de `m_classSkillTreeImGui` :

```cpp
		std::unique_ptr<engine::render::CharacterWindowImGuiRenderer> m_characterWindowImGui;
```

Supprimer le membre `bool m_inventoryVisible = false;` (ajouté en #958).

- [ ] **Step 2 : Engine.cpp — init + Bind**

Dans le bloc d'init des renderers ImGui (~8236-8261, là où `m_grimoireImGui`, `m_classSkillTreeImGui` sont créés), ajouter :

```cpp
				m_characterWindowImGui = std::make_unique<engine::render::CharacterWindowImGuiRenderer>();
				m_characterWindowImGui->Bind(&m_cfg, &m_uiModelBinding, &m_invUi, &m_skillIconCache,
					m_skillBookImGui.get(), m_grimoireImGui.get(), m_classSkillTreeImGui.get());
				m_characterWindowImGui->SetRaceViewport(&m_racePreviewViewport); // Task 4 ; nom réel du membre à confirmer
```

> Confirmer le nom du membre `RacePreviewViewport` dans Engine (grep `RacePreviewViewport` dans Engine.h). S'il n'est pas directement accessible, laisser `SetRaceViewport` pour Task 4 et ne pas l'appeler ici.

- [ ] **Step 3 : Engine.cpp — appel Render dans le bloc panneaux**

Dans le bloc ~12580-12740 (où `m_grimoireImGui->Render()` etc. sont appelés), **remplacer** les rendus autonomes de la fiche/skillbook/grimoire/arbre par un seul appel conteneur. Concrètement :

- Retirer/neutraliser les appels `m_characterSheetImGui->Render(...)` (~12608-12610), `m_skillBookImGui->...Render()` (~12638-12640), `m_grimoireImGui->...Render()` (~12675-12677), `m_classSkillTreeImGui->...Render()` (~12697-12699) — ils sont désormais pilotés par le conteneur en mode embedded.
- Ajouter :

```cpp
				if (m_characterWindowImGui)
				{
					m_characterWindowImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
					m_characterWindowImGui->Render(m_uiModelBinding.GetModel());
				}
```

- [ ] **Step 4 : Engine.cpp — retrait du bloc inventaire #958**

Supprimer le bloc `// --- Fenetre d'inventaire (bascule touche I)...` jusqu'à son `ImGui::End(); ImGui::PopStyleColor(2);` (introduit en #958, région ~12360 avant retrait). C'est la source du doublon.

- [ ] **Step 5 : Engine.cpp — touches**

- Remplacer le toggle F1 fiche (~7920-7923) par :

```cpp
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.charactersheet", "F1"), engine::platform::Key::F_1)))
			{
				if (m_characterWindowImGui) m_characterWindowImGui->ToggleVisible();
				LOG_INFO(Core, "[Engine] F1 toggle fenetre Personnage");
			}
```

> Confirmer que `inGameNoMenu`/`chatBlocks` sont en portée à cet endroit ; sinon réutiliser les gardes locales du bloc F1 existant.

- Supprimer le bloc toggle Grimoire (~7744-7758), le bloc toggle ClassSkillTree (~7766-7780), et le toggle inventaire I (#958, ~7317-7321). Les touches F2/F3/F4/I deviennent libres.

- [ ] **Step 6 : Commit**

```bash
git add src/client/app/Engine.h src/client/app/Engine.cpp
git commit -m "feat(ui): F1 ouvre la fenetre Personnage unifiee ; F2/F3/F4/I liberees ; retrait fenetre inventaire #958 (doublon)"
```

**Vérification (CI verte puis EN JEU) :**
1. F1 ouvre/ferme la fenêtre Personnage.
2. 4 onglets présents ; Compétences/Techniques/Arbre montrent le contenu d'avant, **une seule fois** (pas de doublon).
3. Onglet Personnage : inventaire affiché **une seule fois** + argent or/argent/bronze corrects + caractéristiques.
4. F2, F3, F4, I ne font plus rien.
5. Aucune régression du reste du HUD (barre d'action, bourse HUD bas-droite, cadre cible).

---

## Task 4 : Perso 3D dans l'onglet Personnage (`RacePreviewViewport`)

**Files:**
- Modify: `src/client/app/Engine.cpp` (alimenter le viewport quand l'onglet Personnage est actif)
- Modify: `src/client/render/CharacterWindowImGuiRenderer.cpp` (déjà prêt : `ImGui::Image` si `GetImguiTextureId() != 0`)

**Interfaces:**
- Consumes : `RacePreviewViewport::SetMesh/SetGender/SetSkinTone/Tick/RenderOffscreen/GetImguiTextureId`.

- [ ] **Step 1 : Repérer le mesh/gender/skin du perso local**

Grep dans `Engine.cpp` comment l'avatar local est chargé à `EnterWorld` (`genre perso`, `teinte peau`, `Avatar mesh selected`). Récupérer le `SkinnedMesh*` du perso local, le genre (`"male"/"female"`) et la teinte.

- [ ] **Step 2 : Alimenter le viewport quand la fenêtre est visible et l'onglet Personnage actif**

Dans `Engine::Update`, juste avant l'appel `m_characterWindowImGui->Render(...)`, si visible :

```cpp
				if (m_characterWindowImGui && m_characterWindowImGui->IsVisible())
				{
					m_racePreviewViewport.SetGender(localGenderStr);
					m_racePreviewViewport.SetSkinTone(localSkinTone);
					m_racePreviewViewport.SetMesh(localAvatarMesh); // idempotent si inchangé
					m_racePreviewViewport.Tick(static_cast<float>(dt));
					m_racePreviewViewport.RenderOffscreen();
				}
```

> Noms exacts (`m_racePreviewViewport`, `localAvatarMesh`, `localGenderStr`, `localSkinTone`) à confirmer par grep. `RenderOffscreen` doit être appelé au bon moment vis-à-vis de la frame Vulkan (regarder où l'écran de création l'appelle et calquer). Si l'ordonnancement Vulkan est délicat, faire tourner le viewport uniquement quand la fenêtre est ouverte pour limiter le coût.

- [ ] **Step 3 : Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(ui): perso 3D (RacePreviewViewport) dans l'onglet Personnage"
```

**Vérification (CI verte puis EN JEU) :** le perso 3D s'affiche dans l'onglet Personnage ; pas de crash Vulkan (device lost) ; le reste de la scène reste rendu normalement.

---

## Ouverture du chantier suivant

- **Chantier 2** (spec séparée) : activer les emplacements d'équipement (slots fonctionnels, wire equip/unequip, persistance DB, recalcul stats, mesh 3D selon stuff). ⚠️ client + serveur.
- **Correctif indépendant** : repositionner le texte de zoom « 200 m » du radar au-dessus de l'arc (radar-seul) — PR séparée, non liée à cette fenêtre.

---

## Self-Review (couverture spec)

- Fenêtre F1 + 4 onglets → Task 2 (conteneur) + Task 3 (F1).
- F2/F3/F4/I libérées → Task 3 Step 5.
- Onglet défaut 3D + inventaire + stats + argent → Task 2 (inventaire/stats/argent) + Task 4 (3D).
- Délégation panneaux existants → Task 1 (embedded) + Task 2 (appels).
- Correctif doublon inventaire → Task 3 Step 3-4 (rendu single-pass + retrait bloc #958).
- Slots équipement inactifs (placeholder) → Task 2 (`TextDisabled("Equipement — Chantier 2")` ; enrichir en dessins de cases si souhaité).
- Client-only / pas de serveur → aucun opcode/handler/migration dans le plan. ✅
- Réutilisation (RacePreviewViewport, InventoryUiPresenter, CurrencyFormat, renderers) → Tasks 2/4.
