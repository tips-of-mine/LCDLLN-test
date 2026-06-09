# Système de Personnages — R1-B (feuille de perso : 11 stats répliquées + panneau) Plan

**Goal:** Pousser au client local **toutes** les stats dérivées (ressource secondaire val+max, dégâts, précision, portée, crit %, mult crit, vitesses marche/course/sprint, endurance val+max, perception, discrétion, + clé de ressource) via un **message dédié** (rétro-additif, sans bump de protocole), et les afficher dans un **panneau « feuille de personnage »** (ImGui).

**Architecture:** Nouveau `MessageKind::PlayerStats` (gameplay shardd→client). À l'enter-world, le shard calcule les `DerivedStats` complètes (réutilise `m_statsTables` + faction/classe de R1-A), encode un `PlayerStatsMessage`, l'envoie au client via `m_transport.Send`. Le client décode dans `UIPlayerStats` (étendu) et un panneau ImGui l'affiche (toggle clavier). **Pas de bump `kProtocolVersion`** (nouveau type ignoré par les vieux clients) → pas de lock-step strict (un vieux client ignore le message ; un nouveau client sur vieux shard n'en reçoit juste pas).

**Branche:** `feat/character-system-r1b-stats-sheet`, **stackée sur** `feat/character-system-r1a-server-stats` (réutilise R1-A). Rebaser sur main après merge de R1-A.

---

## Contexte (cartographie)
- Framing gameplay : header 8 octets (`ServerProtocol.cpp` ~155) = magic + version(8) + `MessageKind`(uint16). `MessageKind` enum `ServerProtocol.h:42-210` (dernier `Goodbye=78`). DecodeHeader rejette si version≠8, mais un **nouveau MessageKind à version 8** passe et tombe dans le `default` côté vieux client (ignoré) → additif.
- Envoi 1 client : `m_transport.Send(client.endpoint, packet)` ; pattern `SendWelcome` (`ServerApp.cpp` ~4178), encode via `Encode*`. 
- Level-up runtime : **n'existe pas** → push à l'enter-world seulement.
- Client dispatch : `UIModel.cpp` ~548-608 `switch(kind)` → `Apply*`. `UIPlayerStats` `UIModel.h:181-204` (PAS de stats dérivées). HUD : `CombatHud` (`ApplyModel`/`UpdatePlayerBars`).
- `DerivedStats` (`CharacterStatsEngine.h:17-33`) : hp, resource, damage, accuracy(f), range(f), critRate(f), critMult(f), speedWalk/Run/Sprint(f), stamina, perception(f), stealth(f), resourceKey(string). (hp/maxHealth déjà répliqués par R1-A via snapshot → la feuille peut quand même les inclure pour cohérence d'affichage.)

---

## Task 1 — Message wire `PlayerStats` (+ round-trip test)

**Files:** Modify `src/shared/network/ServerProtocol.h` / `.cpp`; Create `src/shared/network/PlayerStatsMessageTests.cpp`; Modify root `CMakeLists.txt`.

- [ ] **Step 1 (test first):** `PlayerStatsMessageTests.cpp` — plain-main return 0/1 (NDEBUG-safe, pas d'assert). Construire un `PlayerStatsMessage` avec valeurs distinctes pour chaque champ (dont `resourceKey="ferveur"`), `EncodePlayerStats`, `DecodePlayerStats`, vérifier round-trip de TOUS les champs (floats à tolérance 1e-4). Inclure un cas `resourceKey` vide.
- [ ] **Step 2:** `ServerProtocol.h` — ajouter `PlayerStats = 79` à `MessageKind` (après `Goodbye = 78`, ne PAS réordonner). Définir :
```cpp
	/// Stats dérivées complètes du joueur local (R1-B). Poussé à l'enter-world.
	struct PlayerStatsMessage
	{
		uint32_t clientId = 0;
		uint32_t maxHealth = 0;
		uint32_t resource = 0;      ///< ressource secondaire max
		uint32_t stamina = 0;       ///< endurance max
		uint32_t damage = 0;
		float    accuracy = 0.0f;
		float    range = 0.0f;
		float    critRate = 0.0f;
		float    critMult = 0.0f;
		float    speedWalk = 0.0f;
		float    speedRun = 0.0f;
		float    speedSprint = 0.0f;
		float    perception = 0.0f;
		float    stealth = 0.0f;
		std::string resourceKey;    ///< ex. "ferveur" (libellé résolu client via l10n)
	};
	std::vector<std::byte> EncodePlayerStats(const PlayerStatsMessage& m);
	std::optional<PlayerStatsMessage> DecodePlayerStats(std::span<const std::byte> packet);
```
(Adapter les types span/vector exactement à ceux des autres Encode/Decode du fichier — LIRE un Encode/Decode existant, ex. EncodeWelcome/DecodeWelcome, et mirrorer le writer/reader helpers + l'écriture du header via le helper commun.)
- [ ] **Step 3:** `ServerProtocol.cpp` — implémenter `EncodePlayerStats` (header via le helper commun avec `MessageKind::PlayerStats`, puis writer des champs dans l'ordre du struct ; string via le helper string existant) et `DecodePlayerStats` (symétrique, garde de taille). Mirrorer EXACTEMENT le style d'un couple Encode/Decode existant.
- [ ] **Step 4:** Enregistrer `player_stats_message_tests` dans le **root** `CMakeLists.txt` (là où vivent les autres tests `*_message`/`*_payloads` — ex. `character_save_position_payloads_tests`).
- [ ] **Step 5:** commit `feat(wire): MessageKind::PlayerStats + encode/decode (rétro-additif) + test round-trip`.

---

## Task 2 — Shard : calcul complet + `SendPlayerStats` à l'enter-world

**Files:** Modify `src/shared/server_bootstrap/ServerApp.cpp` / `.h`.

- [ ] **Step 1:** Ajouter `bool SendPlayerStats(const ConnectedClient& client);` (déclaration `.h` près de `SendWelcome`). 
- [ ] **Step 2:** Implémenter : si `m_statsTables` (chargé en R1-A) et faction/classe non vides, `auto d = engine::server::gameplay::ComputeStats(*m_statsTables, client.factionId, client.classId, sex(client.gender), client.level);` ; si `d`, remplir un `PlayerStatsMessage` (clientId + tous les champs de `*d`, `resourceKey=d->resourceKey`), `EncodePlayerStats`, `m_transport.Send(client.endpoint, packet)`. Sinon return false (pas de feuille pour legacy). Qualifier `engine::server::gameplay::` (cf. piège R1-A). Log INFO.
- [ ] **Step 3:** Appeler `SendPlayerStats(acceptedClient)` à l'enter-world, **après** le bloc R1-A (PV résolus) et après `SendWelcome` (le client doit être prêt). Trouver le point exact où les autres bootstrap par-client sont envoyés (ex. après SendWelcome / spawn initial). 
- [ ] **Step 4:** commit `feat(shardd): SendPlayerStats — pousse les 11 stats au client à l'enter-world`.

---

## Task 3 — Client : décodage `PlayerStats` → `UIPlayerStats`

**Files:** Modify `src/client/ui_common/UIModel.h` / `.cpp`.

- [ ] **Step 1:** Étendre `UIPlayerStats` (UIModel.h ~204) : `uint32_t derivedMaxHealth, secondaryResourceMax, staminaMax, damage; float accuracy, range, critRate, critMult, speedWalk, speedRun, speedSprint, perception, stealth; std::string secondaryResourceKey; bool hasSheet=false;`. (Ne pas écraser les champs snapshot existants health/mana ; ces nouveaux champs viennent de la feuille.)
- [ ] **Step 2:** `UIModel.cpp` — dans le `switch(kind)` (~605), ajouter `case MessageKind::PlayerStats: return ApplyPlayerStats(packet);` avant le `default`. Implémenter `ApplyPlayerStats` : `DecodePlayerStats`, remplir les champs `UIPlayerStats`, `hasSheet=true`, notifier (mask de changement approprié, ex. `UIModelChangeStats` — mirrorer un Apply existant). Déclarer `ApplyPlayerStats` dans UIModel.h (privé, comme les autres `Apply*`).
- [ ] **Step 3:** commit `feat(client): décode PlayerStats dans UIPlayerStats`.

---

## Task 4 — Client UI : panneau « Feuille de personnage » (ImGui) — VISUEL, non vérifiable sans build

**Files:** Modify `src/client/combat/CombatHud.{h,cpp}` (ou un nouveau module sœur) + le point d'appel de rendu HUD + le toggle clavier.

- [ ] **Step 1:** LIRE comment le HUD est rendu et togglé (chercher un panneau existant togglable, ex. inventaire 'I', social 'O', et le système de keybind). Mirrorer ce pattern pour un toggle (ex. 'C' — vérifier qu'il est libre).
- [ ] **Step 2:** Ajouter `RenderCharacterSheetPanel()` : une fenêtre/`ImGui::Begin` "Personnage" affichant, depuis `model.playerStats` (si `hasSheet`), un tableau libellé→valeur : PV max, Ressource (libellé via l10n `resource.<key>` si dispo sinon la clé), Endurance, Dégâts, Précision %, Portée m, Crit %, Mult crit ×, Vitesses marche/course/sprint, Perception m, Discrétion m. Formats : entiers tels quels, floats `%.1f`/`%.2f`. Si mêlée pure (range==0), afficher "—" pour portée/précision.
- [ ] **Step 3:** Brancher le rendu (toggle) dans la boucle HUD. Minimal, pattern-matched.
- [ ] **Step 4:** (Localisation, optionnel) ajouter des clés `resource.<key>` (ferveur→« Ferveur », etc.) dans fr.json/en.json pour le libellé de ressource ; sinon le client affiche la clé brute.
- [ ] **Step 5:** commit `feat(client): panneau feuille de personnage (11 stats) + toggle`.

> ⚠️ Tâche 4 non vérifiable visuellement sans build local — additions minimales, pattern-matched, à valider en jeu par l'utilisateur.

---

## Task 5 — Vérif + push + PR
- [ ] grep : `kProtocolVersion` inchangé (8). Symboles `engine::server::gameplay::` qualifiés dans ServerApp.
- [ ] push, PR base main (stackée — préciser « à merger après R1-A #863 »). CI : build-linux (ctest `player_stats_message_tests`) + build-windows (client compile).

> **Déploiement R1-B** : ⚠️ redéploiement **shard + client** pour bénéficier de la feuille (mais **rétro-compatible** : nouveau type de message ignoré par anciens clients, donc pas de lock-step strict — le bénéfice n'apparaît que quand les deux sont neufs). Aucun changement master.

## Hors périmètre
Level-up runtime (re-push des stats au changement de niveau) — quand le système d'XP/level-up serveur existera.
