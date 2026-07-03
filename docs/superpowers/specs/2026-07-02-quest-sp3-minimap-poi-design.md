# SP3 — Client : POI minimap des quêtes

**Date** : 2026-07-02
**Statut** : design (à relire)
**Doc parent** : `2026-07-02-quest-system-overview-design.md`
**Dépend de** : SP1 (données quête), SP2 (rendu client, `QuestImGuiRenderer`, presenter) — mergés.
**Portée** : 100 % client. Afficher une **minimap radar** avec les **POI des objectifs de quête actifs** (où aller tuer/parler/entrer). Schématique (sans texture), positions d'objectif depuis un **fichier de contenu**.

> **Déploiement** : ✅ client uniquement, **pas de nouveau wire**, pas de redéploiement serveur. Contenu client ajouté (`quest_poi.json`).

---

## 1. Contexte & décisions

L'échafaudage minimap du `QuestUiPresenter` existe (structures `QuestUiState.minimap*`, `MinimapPoiView`, `RebuildMinimap`, chargement métadonnée `ui/minimap_zones.txt`) mais **n'est jamais rendu**, et `RebuildMinimap` ne résout que les POI `enter:zone:*` (placeholder au centre). Les positions kill/collect/talk ne sont pas résolues ; les mobs lointains ne sont pas répliqués (AoI seul).

**Décisions (validées) :**
1. **Rendu schématique** : minimap **radar centrée joueur**, dessinée à l'`ImDrawList` (cadre + fond + grille légère + points POI + marqueur joueur central + labels). **Aucune texture**, aucun `VkDescriptorSet`.
2. **Positions d'objectif** : depuis un **fichier de contenu** `game/data/quests/quest_poi.json` (`targetId → positions monde`), pas dérivées de l'AoI. Découplé, éditable, cohérent avec les spawners SP5.
3. **Radar centrée joueur** (au lieu de la conversion zone `TryConvertWorldToUv`/`zoneSizeMeters`) : les coords feyhin sont petites (±40 m) ; un radar de **rayon monde fixe** (config, défaut 60 m) place le joueur au centre et les POI en offset relatif. Off-radar → clampé au bord (indice directionnel).

---

## 2. Architecture

```
game/data/quests/quest_poi.json  ──►  QuestPoiTable::Load  (nouveau, src/client/quest/, testable)
   { "mob:100":[[12,-28],...], "npc:elder_marn":[[4,0]], "zone:2":[[cx,cz]] }
                                          │  Positions(targetId) → vector<Vec2 xz>
                                          ▼
UIModel.quests (steps actifs) ─► QuestUiPresenter::RebuildMinimap  (réécrit)
   pour chaque step Active : QuestPoiTable.Positions(step.targetId) → offset vs joueur
   (UIModel.playerStats.position) → MinimapPoiView (u,v centré radar, label, visible)
                                          ▼
QuestImGuiRenderer::RenderMinimap  (nouveau) ─► radar ImDrawList (cadre + POI + joueur)
   appelé depuis Render() après RenderTracker ; bornes/rayon via config.
```

- **`QuestPoiTable`** (`src/client/quest/QuestPoiTable.{h,cpp}`) : charge `quest_poi.json`, expose `const std::vector<Vec2>* Positions(std::string_view targetId) const` (nullptr si absent). Testable (parse + résolution).
- **`QuestUiPresenter`** : `RebuildMinimap` réécrit pour résoudre les POI via `QuestPoiTable` en **coords radar centrées joueur** ; injecte la table (`SetPoiTable`/Load en `Init`) ; le marqueur joueur est au centre (0.5,0.5). Garde `minimap_radius_m` (config).
- **`QuestImGuiRenderer::RenderMinimap`** : dessine le radar (fond semi-transparent + cercle/cadre + croix/grille), les POI (points colorés par type + label court), le joueur au centre. Clamp les POI hors-rayon au bord. Bornes depuis la config (coin, taille).

---

## 3. Résolution des POI (RebuildMinimap)

Pour chaque quête `Active` de `UIModel.quests`, pour chaque `UIQuestStep` non terminé (`currentCount < requiredCount`) :
- `positions = QuestPoiTable.Positions(step.targetId)` (ex. `"mob:100"`, `"npc:elder_marn"`, `"zone:2"`). Si absent → pas de POI (silencieux).
- Pour chaque position `p` : `offset = p - playerPos.xz` ; `radar_uv = 0.5 + offset / (2 * radiusM)` (clampé/marqué off-radar si `|offset| > radiusM`).
- `MinimapPoiView{ u, v, label = libellé court (ex. « Sanglier », « Elder Marn », « Zone 2 »), visible }`. Couleur/type transmis (kill/talk/enter) pour la teinte au rendu.
- Marqueur joueur : centre (0.5,0.5), orienté selon le yaw joueur si dispo (sinon point simple).

`collect:item:*` : résolvable si présent dans `quest_poi.json`, sinon ignoré (le loot n'a pas de position fixe — hors V1).

---

## 4. Rendu (RenderMinimap)

- Fenêtre/zone ImGui aux bornes config (défaut : coin haut-droit, `size_px` ~200). Non-interactive (overlay).
- `ImDrawList` : fond `AddRectFilled` semi-transparent + bord `AddRect` (ou cercle `AddCircle` pour un radar) + croix centrale légère.
- POI : `AddCircleFilled` (teinte par type : kill=rouge, talk=jaune, enter=bleu) + `AddText` label à côté. Clamp au bord si hors-rayon.
- Joueur : petit triangle/point au centre.
- Masqué si `client.quest.minimap.enabled=false` ou pendant dialogue/menu (cohérent avec les autres overlays).

---

## 5. Contenu & config

- **`game/data/quests/quest_poi.json`** (nouveau) : `{ "<targetId>": [[x,z], ...] }`. V1 : entrées pour la quête SP5 — `"mob:100"` = les 4 clusters de `feyhin/spawners.json`, `"npc:elder_marn"` = `[[4,0]]` (villageois), et exemples talk/enter.
- **Config** `config.json` : `client.quest.minimap` = `{ enabled: true, size_px: 200, radius_m: 60 }`.

## 6. Décisions à confirmer (relecture)
1. **Radar centré joueur** (rayon fixe) plutôt que carte de zone à l'échelle. Reco (coords petites, pas de texture). OK ?
2. **POI hors-rayon** : clampés au bord (indice directionnel) plutôt que masqués. OK ?
3. **Types V1** : kill/talk/enter (depuis `quest_poi.json`) ; collect ignoré (loot). OK ?
4. **Emplacement** : `src/client/quest/QuestPoiTable.{h,cpp}` + `RenderMinimap` dans `QuestImGuiRenderer`. OK ?

## 7. Tests
- `QuestPoiTableTests` (non-strippable) : parse `quest_poi.json` ; `Positions("mob:100")` → N positions ; `Positions("inconnu")` → nullptr ; rejet d'entrée mal formée.
- Presenter : `RebuildMinimap` produit un POI par position résolue d'un step actif ; POI hors-rayon marqué ; joueur au centre ; pas de POI pour un targetId absent. (Testable si la logique de conversion radar est extraite en helper pur ; sinon validé en jeu.)
- Rendu ImGui = intégration (validé en jeu).

## 8. Hors périmètre
- Texture de minimap réelle (`VkDescriptorSet`/`ImGui::Image`) = polish ultérieur.
- Liste de mobs poussée par le shard (wire) — non nécessaire (POI depuis contenu).
- POI collect/loot dynamiques ; carte plein écran ; brouillard de guerre.

## 9. Definition of Done
- [ ] `QuestPoiTable` (Load/Positions) + `quest_poi.json` + tests verts.
- [ ] `RebuildMinimap` résout les POI actifs via la table, en coords radar centrées joueur (helper pur testé si possible).
- [ ] `RenderMinimap` (radar schématique : cadre + POI teintés + joueur + labels, clamp bord) branché dans `QuestImGuiRenderer::Render`.
- [ ] Config `client.quest.minimap.*` ; masquage cohérent (dialogue/menu).
- [ ] Commentaires FR ; CI verte. Rapport : ✅ client only, pas de redéploiement serveur.
