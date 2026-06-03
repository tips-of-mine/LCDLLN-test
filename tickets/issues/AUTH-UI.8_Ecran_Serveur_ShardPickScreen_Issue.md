# Issue: AUTH-UI.8

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/client/auth/screens/AuthScreenShardPick.cpp
- src/client/render/auth/screens/AuthImGuiShardPick.cpp

## Note
ShardPickScreen

---

## Contenu du ticket (AUTH-UI.8)

# AUTH-UI.8 — Écran Choix du serveur · ShardPickScreen (split + redesign visuel)

## Dépendances
- AUTH-UI.1 (socle commun)
- AUTH-UI.2 (Login — le shard pick est atteint après authentification réussie)

## Objectif

1. **Split** : déplacer dans `AuthScreenShardPick.cpp` les méthodes relatives à `Phase::ShardPick`.
2. **Split renderer** : implémenter `AuthImGuiShardPick.cpp` aligné sur la maquette `ShardPickScreen` (écran 7 de `Screens5to7.jsx`).
3. **Redesign visuel** : fil d'Ariane, liste de serveurs avec flag lettre / nom / description / événement / barre de charge / ping coloré / badge statut.

---

## Périmètre fonctionnel (Phase::ShardPick)

### Méthodes presenter → `engine/client/auth/screens/AuthScreenShardPick.cpp`

| Méthode |
|---|
| `ImGuiSetShardPickChoiceShardId()` |
| `ImGuiSubmitShardPick()` |
| `ImGuiBackFromShardPickToLogin()` |
| `ShardPickChoiceShardId()` *(accessor inline, reste dans .h)* |
| `ShardPickEntries()` *(accessor inline, reste dans .h)* |
| `BuildModel_ShardPick()` **(nouvelle méthode privée)** |
| `Update_ShardPick()` **(nouvelle méthode privée — navigation ↑↓ + Enter)** |

---

## Cible visuelle (ShardPickScreen — Screens5to7.jsx)

### Structure globale

```
ln-stage (padding clamp 12px→24px)
  ln-stage-col (max 820px)
    Breadcrumb  [01 Compte] [02 Royaume ← actif] [03 Personnage] [04 Entrée]
    Panel
      header: "Choisissez votre royaume"
      subtitle: "Chaque monde possède sa population, ses règles et ses événements."
      versionLabel: "{N en ligne} / {total} en ligne"
      icône "i" : "Vous pourrez changer de royaume plus tard via le portail..."
      body:
        ln-shard-list
          [pour chaque shard] ln-shard-row [selected?] [disabled si status=err]
            ln-shard-flag      ← lettre initiale (ex. "M" pour Morneplaine)
            [col info]
              ln-shard-name    ← nom du shard
              ln-shard-desc    ← description courte
              [si event] ln-shard-event ← événement en cours
            [col charge]
              ln-shard-load-label  "Charge · {pct}%"
              ln-shard-load-bar    ← barre proportionnelle (vert/orange si >85%)
              ln-shard-players     "{players} / {cap}"
            ln-shard-ping [ok|med|bad]  ← "{Nms}" ou "—"
            ln-shard-status [ok|warn|err] ← "En ligne" / "Saturé" / "Hors ligne"
        ln-actions:
          Button ghost/md "Retour"  keycap="Échap"
          ln-actions-right:
            KeycapHint "↑↓" → "naviguer"
            Button primary/md "Entrer dans le monde"  keycap="↵"
              (désactivé si aucune sélection ou shard hors ligne)
```

### Lignes serveur — implémentation ImGui

Chaque `ServerListEntry` de `m_shardPickEntries` est rendue via `ImGui::BeginChild()` ou `ImGui::Selectable()` avec rendu custom :

```
┌──────────────────────────────────────────────────────────────────┐
│ [M]  Morneplaine                          Charge · 61%  28 ms  En ligne │
│      Terres brumeuses, PvE coopératif.  ████████░░░░              │
│      ★ Chasse de la lune noire          1 842 / 3 000             │
└──────────────────────────────────────────────────────────────────┘
```

- **Flag lettre** : `ImDrawList::AddRectFilled` + `AddText` (première lettre du name, police display, ln-accent sur fond sombre)
- **Sélection** : `ImGui::Selectable()` avec `selected=true` → bordure `ln-accent`, fond `rgba(232,197,110,.06)`
- **Shard hors ligne** (`status=err`) : ligne grisée, non sélectionnable (`ImGui::BeginDisabled(true)`)
- **Barre de charge** : rectangle fond `ln-border`, remplissage `ln-success` (< 85%) ou `ln-warning` (≥ 85%)
- **Ping** : coloration par seuils :
  - `< 40 ms` → `ln-success` (vert)
  - `40–79 ms` → `ln-warning` (orange)
  - `≥ 80 ms` ou `null` → `ln-error` (rouge)
- **Statut** :
  - `ok` → `"En ligne"`, ln-success
  - `warn` → `"Saturé"`, ln-warning
  - `err` → `"Hors ligne"`, ln-muted (grisé)
- **Événement en cours** : ligne `ln-shard-event` avec icône étoile `★` + texte italic ln-accent/muted

### Mapping RenderModel → ImGui

La `Phase::ShardPick` n'utilise pas les champs `fields` / `actions` du RenderModel standard — ses données viennent directement de `m_shardPickEntries` et `m_shardPickChoiceShardId`.

| Source | ImGui |
|---|---|
| `ShardPickEntries()` | Liste `ServerListEntry` → rendu des lignes |
| `ShardPickChoiceShardId()` | ID shard sélectionné |
| `GetStatusCache().servers` | Statut/joueurs en temps réel (sonde périodique) |
| Breadcrumb | 4 étapes [Compte / Royaume / Personnage / Entrée], étape 2 active |
| Badge panel | `"{n_online} / {total} en ligne"` |

### Navigation clavier ↑↓

Dans `Update_ShardPick()` :
- `Key::Up` → sélectionner le shard précédent non-err
- `Key::Down` → sélectionner le shard suivant non-err
- `Key::Enter` → `ImGuiSubmitShardPick()` si shard valide sélectionné
- `Key::Escape` → `ImGuiBackFromShardPickToLogin()`

---

## Livrables

**Créés / Complétés :**
- `engine/client/auth/screens/AuthScreenShardPick.cpp`
- `engine/render/auth/screens/AuthImGuiShardPick.cpp`

**Modifiés :**
- `engine/client/AuthUi.h` — `BuildModel_ShardPick()`, `Update_ShardPick()`
- `engine/client/auth/AuthUiPresenterCore.cpp` — dispatch

---

## Definition of Done

- [ ] Build Windows OK
- [ ] Fil d'Ariane 4 étapes, étape 2 ("Royaume") active
- [ ] Badge panel : nombre de shards en ligne / total
- [ ] Chaque ligne affiche : flag-lettre, nom, desc, événement (si présent), barre charge, ping coloré, statut
- [ ] Shard hors ligne (`status=err`) : ligne grisée, non sélectionnable
- [ ] Shard saturé (`status=warn`) : badge "Saturé" orange, barre charge orange
- [ ] Sélection clavier ↑↓ (saute les shards hors ligne)
- [ ] Bouton "Entrer dans le monde" désactivé si aucun shard valide sélectionné
- [ ] Submit → `ImGuiSubmitShardPick()` → flow connexion shard
- [ ] Retour → `ImGuiBackFromShardPickToLogin()`
- [ ] Aucune régression Login / Register
- [ ] Rapport final
