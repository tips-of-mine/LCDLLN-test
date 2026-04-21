# AUTH-UI.10 — Écran Conditions Générales · Terms (split uniquement)

## Dépendances
- AUTH-UI.1 (socle commun)
- AUTH-UI.2 (Login — Terms est atteint après login si des CGU en attente existent)

## Objectif

**Split uniquement** : déplacer dans `AuthScreenTerms.cpp` et `AuthImGuiTerms.cpp` les méthodes relatives à `Phase::Terms`.

Pas de maquette HTML disponible. Le rendu ImGui STAB.13 est conservé.

---

## Périmètre fonctionnel (Phase::Terms)

`Phase::Terms` est activée lors du login si `termsPendingCount > 0` (le serveur signale des CGU non acceptées). L'utilisateur doit scroller jusqu'en bas puis cocher une case avant de pouvoir valider.

### Méthodes presenter → `engine/client/auth/screens/AuthScreenTerms.cpp`

| Méthode |
|---|
| `StartTermsStatusWorker()` |
| `StartTermsAcceptWorker()` |
| `ImGuiNotifyTermsScrollReachedBottom()` |
| `ImGuiSetTermsAcknowledgeChecked()` |
| `ImGuiTermsPrimaryClick()` |
| `ImGuiTermsDecline()` |
| `TermsFullTextForImGui()` *(accessor inline, reste dans .h)* |
| `BuildModel_Terms()` **(nouvelle méthode privée)** |
| `Update_Terms()` **(nouvelle méthode privée)** |

### Renderer → `engine/render/auth/screens/AuthImGuiTerms.cpp`

Extraction de la logique ImGui Terms depuis `AuthUiRenderer.cpp` vers ce fichier dédié.

---

## Rendu ImGui (état existant STAB.13, non modifié)

L'écran Terms actuel affiche :
- Panel titre + version CGU (`m_termsTitle`, `m_termsVersionLabel`)
- Zone de scroll avec le texte complet des CGU (`m_termsContent`)
  - Scrollbar ImGui : `ImGui::BeginChild("terms_scroll", …, true)`
  - Détection scroll bas → `ImGuiNotifyTermsScrollReachedBottom(true)`
- Case à cocher "J'ai lu et j'accepte les Conditions Générales" (activée seulement si scroll bas atteint)
  - `ImGuiSetTermsAcknowledgeChecked()`
- Bouton primaire "Accepter" (activé si checkbox cochée) → `ImGuiTermsPrimaryClick()`
- Bouton "Refuser" → `ImGuiTermsDecline()` (ferme la fenêtre)
- Banner error si échec serveur

Ce rendu est **conservé tel quel** après le split.

---

## Contrainte : Detect scroll bottom

`ImGuiNotifyTermsScrollReachedBottom()` doit être appelé depuis le renderer quand :
```cpp
float scrollY = ImGui::GetScrollY();
float scrollMax = ImGui::GetScrollMaxY();
if (scrollMax > 0.f && scrollY >= scrollMax - 4.f) {
    presenter.ImGuiNotifyTermsScrollReachedBottom(true);
}
```
Cette logique reste dans `AuthImGuiTerms.cpp`.

---

## Note de conception (pour future maquette)

Éléments prévisibles pour un redesign futur :
- Layout plein écran avec sidebar CGU (titre, version, date)
- Zone de lecture avec fond semi-opaque, scrollbar stylée
- Section acceptation avec toggle + libellé explicite
- Footer avec "Refuser" (danger) et "Accepter" (primary, désactivé jusqu'au scroll bas)

---

## Livrables

**Créés / Complétés :**
- `engine/client/auth/screens/AuthScreenTerms.cpp`
- `engine/render/auth/screens/AuthImGuiTerms.cpp`

**Modifiés :**
- `engine/client/AuthUi.h` — `BuildModel_Terms()`, `Update_Terms()`
- `engine/client/auth/AuthUiPresenterCore.cpp` — dispatch

---

## Definition of Done

- [ ] Build Windows OK
- [ ] Écran Terms s'affiche identiquement à l'état STAB.13
- [ ] Scroll bas détecté → checkbox activée
- [ ] "Accepter" désactivé tant que checkbox non cochée
- [ ] "Accepter" → worker lancé → transition vers CharacterCreate ou ShardPick
- [ ] "Refuser" → fermeture application
- [ ] Aucune régression Login / ShardPick
- [ ] Rapport final
