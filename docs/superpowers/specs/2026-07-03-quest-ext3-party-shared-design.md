# EXT-3 — Quêtes partagées en groupe (party-shared)

**Date** : 2026-07-03
**Statut** : design (à relire)
**Doc parent** : `2026-07-02-quest-system-overview-design.md` ; stackée sur EXT-2.
**Portée** : un flag **`partyShared`** par quête : quand un joueur déclenche un événement d'étape (kill/collect/talk/enter), **les membres de son groupe à portée** sont aussi crédités, pour les quêtes `partyShared`. **Shard + éditeur — aucun changement de wire.**

> **Décisions produit validées (user, 2026-07-03)** : **tous les types d'objectif** partagés (kill/collect/talk/enter) ; crédit limité à un **rayon autour de l'acteur** (`server.quest.party_share_radius_m`).

> **« Groupe/raid » = groupe (party)** : `PartySystem` n'a **pas** de concept de raid distinct (capacité max 5, `kMaxPartyMembers`). EXT-3 couvre le groupe ≤ 5 ; un partage « raid » plus large est hors périmètre (nécessiterait un système de raid inexistant).

---

## 1. Modèle de données

- **JSON** : champ optionnel `"partyShared": true` (défaut `false` → rétro-compatible).
- **`QuestDefinition`** (`QuestRuntime.h`, après `autoComplete`) : `bool partyShared = false;`.
- Aucun nouvel état par joueur : la progression créditée aux coéquipiers passe par leur `stepProgressCounts` existant (déjà persisté). **Pas de persistance nouvelle, pas de wire.**

## 2. Crédit filtré dans `QuestRuntime::ApplyEvent`

`ApplyEvent` avance actuellement **toutes** les quêtes `Active` dont l'étape courante matche l'événement. Pour ne créditer aux coéquipiers que les quêtes `partyShared`, ajouter un paramètre **défauté** :
```cpp
bool ApplyEvent(std::vector<QuestState>& states, QuestStepType eventType,
                std::string_view targetId, uint32_t amount,
                std::vector<QuestProgressDelta>& outDeltas,
                bool onlyPartyShared = false) const;
```
- `onlyPartyShared == false` (défaut) : comportement inchangé (l'acteur crédite **toutes** ses quêtes). → tests EXT-1/EXT-2 restent valides (paramètre défauté).
- `onlyPartyShared == true` : ignorer toute définition dont `!def.partyShared` (skip avant d'appliquer la progression).

## 3. Fan-out groupe dans `ServerApp::ApplyQuestEvent` (`ServerApp.cpp:5247-5303`)

### 3.1 Refactor : extraire le post-traitement des deltas
Extraire le bloc **après** `ApplyEvent` (boucle autoComplete EXT-2 → stamp `completedAtEpochMs` + `GrantQuestReward` ; envoi `SendQuestDelta` ; `SaveConnectedClient`) dans un helper privé :
```cpp
void ServerApp::FinalizeQuestDeltas(ConnectedClient& c,
                                    std::vector<QuestProgressDelta>& deltas,
                                    std::string_view reason);
```
L'acteur l'appelle comme aujourd'hui. Évite la divergence acteur ↔ coéquipiers.

### 3.2 Boucle de partage (après le traitement de l'acteur)
```cpp
Party* party = m_partySystem.FindPartyByMember(client.clientId);
if (party != nullptr) {
    const double radiusM = m_config.GetDouble("server.quest.party_share_radius_m", 30.0);
    const float radiusSq = static_cast<float>(radiusM * radiusM);
    for (const PartyMember& member : party->members) {
        if (member.clientId == client.clientId) continue;              // pas l'acteur
        ConnectedClient* mate = FindClientByClientId(member.clientId); // (helper via m_clientIndexByClientId ; l'ajouter s'il n'existe pas)
        if (mate == nullptr) continue;                                 // hors-ligne / non connecté
        if (mate->zoneId != client.zoneId) continue;                   // ⚠️ même zone AVANT le rayon (coords par-zone)
        if (DistanceSquaredXZ(client.positionMetersX, client.positionMetersZ,
                              mate->positionMetersX, mate->positionMetersZ) > radiusSq) continue;
        std::vector<QuestProgressDelta> mateDeltas;
        if (m_questRuntime.ApplyEvent(mate->questStates, eventType, targetId, amount,
                                      mateDeltas, /*onlyPartyShared=*/true)) {
            FinalizeQuestDeltas(*mate, mateDeltas, reason);            // récompense + envoi + save au coéquipier
        }
    }
}
```
- **Garde zone AVANT rayon** : les positions sont en coordonnées **par zone** → exiger `mate->zoneId == client.zoneId` évite un faux positif de distance entre deux zones aux coordonnées proches.
- **Anti-double-crédit** : un coéquipier ≠ l'acteur (skip `member.clientId == client.clientId`). L'acteur a déjà été crédité **toutes quêtes** ; les coéquipiers ne reçoivent que le partagé.
- **Récompense partagée** : via `FinalizeQuestDeltas` → un coéquipier dont la quête partagée auto-complète reçoit sa propre récompense (chaque membre a sa propre instance de quête). Pas de partage du **butin** (chacun a sa progression/récompense).

## 4. Config
- Nouvelle clé **optionnelle** `server.quest.party_share_radius_m` (double, **défaut 30.0**). Lue via `m_config.GetDouble(..., 30.0)`. Fonctionne sans config (défaut) → pas d'exigence de déploiement dure, mais **à documenter**.

## 5. Éditeur (`QuestEditIo` + `QuestEditorPanel`)
- **`EditedQuest`** : `bool partyShared = false;` (miroir exact de `autoComplete`).
- **Parse** (`QuestEditIo.cpp`, miroir `autoComplete` ~494) : `"partyShared"` (bool, optionnel, `TryGetBool`).
- **Sérialisation** (miroir ~970) : `"partyShared": true/false`.
- **`QuestEditorPanel`** : `ImGui::Checkbox("Partagé en groupe", &m_partySharedBuffer)` (miroir autoComplete `318` + buffer load/save/reset `58/84`). **Doc `///`** sur toute fonction touchée (règle éditeur, CLAUDE.md).

## 6. Wire / client
- **Aucun changement de wire.** Les coéquipiers reçoivent leur progression via leur propre `QuestDelta` (déjà câblé). Pas de bump `kProtocolVersion`, client inchangé.

## 7. Tests
- **Shard** (`QuestRuntimeTests.cpp`) : `ApplyEvent(..., onlyPartyShared=true)` n'avance **que** les quêtes `partyShared` (une quête partagée + une non-partagée toutes deux Active/étape matchante → seule la partagée avance) ; `onlyPartyShared=false` (défaut) avance les deux (non-régression) ; parse `"partyShared"` (défaut false, true lu).
- **Fan-out** (`ServerApp`) : logique de groupe (party/rayon/zone) = **intégration** ; le cœur testable unitairement (filtre `ApplyEvent` + parse) est couvert. Le `DistanceSquaredXZ` est déjà éprouvé ailleurs. La boucle de fan-out sera vérifiée en revue (nul-checks, skip acteur, garde zone-avant-rayon, pas de spam).
- **Éditeur** : round-trip `partyShared` ; défaut false.
- Tests **non-strippables** (Release `-DNDEBUG`).

## 8. Déploiement
> **⚠️ REDÉPLOIEMENT SHARD requis** — `QuestRuntime` (parse + filtre) + `ServerApp` (fan-out groupe, config radius). **Éditeur monde** à redéployer pour authorier `partyShared`. **Client inchangé, aucun wire.** Config optionnelle `server.quest.party_share_radius_m` (défaut 30 m). **Stackée sur EXT-2** : merger #933 → #934 → cette PR.

## 9. Hors périmètre
- Partage « raid » > 5 (pas de système de raid). Partage du **butin/objets** (chacun garde sa progression et sa récompense). Partage inter-zone.

## 10. Definition of Done
- [ ] `QuestDefinition.partyShared` + parse ; `ApplyEvent` param `onlyPartyShared` (défaut false).
- [ ] `FinalizeQuestDeltas` (refactor) ; fan-out groupe (rayon+zone, `FindClientByClientId`, skip acteur) ; config radius.
- [ ] Éditeur checkbox `partyShared` + doc `///`.
- [ ] Tests shard (filtre + parse) + éditeur (round-trip), non-strippables.
- [ ] CI verte (shard + éditeur + client inchangé). Rapport : ⚠️ REDÉPLOIEMENT SHARD + éditeur, client inchangé, stack après #934.
