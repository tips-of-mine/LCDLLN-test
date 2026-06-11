# Groupes SP1 — PartyHud + invitations + ciblage allié : Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rendre le système de groupes (M32.2, serveur complet et déjà utilisé par l'XP de groupe) utilisable de bout en bout : aujourd'hui `/invite` part bien au serveur via le chat, mais le client IGNORE `PartyInviteNotify` (kind 26 non routé) et n'émet jamais `PartyAccept`/`PartyDecline` — l'invité ne peut littéralement pas rejoindre. Et le `PartyHudPresenter` (orphelin de l'audit) n'affiche rien.

**Architecture:** 100 % client, AUCUN changement wire (kinds 25-32 et leurs Encode/Decode existent depuis M32.2 ; `ApplyPartyUpdate` remplit déjà `UIModel::partyMembers`). Trois briques : (1) routage `PartyInviteNotify` → état d'invitation + popup Accepter/Refuser → `SendPartyAccept/Decline` ; (2) câblage du `PartyHudPresenter` (cadres membres à gauche : nom, badge chef, barres PV/mana, mode de loot) ; (3) **ciblage allié** : clic sur un cadre = cible alliée des sorts `SingleAlly` de SP3 (le serveur l'accepte déjà — le client envoyait toujours 0/soi). Identité : `entityId == clientId` pour les joueurs (invariant ServerApp::HandleHello, documenté au site).

**Hors périmètre (documenté) :** persistance des groupes (tables 0060) — les groupes sont session-scoped comme l'implémentation serveur actuelle ; les tables restent provisionnées pour un futur chantier raid/reconnexion.

**Livraison : 1 PR `party-sp1` (client only)** → ✅ pas de redéploiement serveur (mais la PR part au-dessus du chantier combat : merge après #881).

### Task 1: UIModel — état d'invitation
- `struct UIPartyInviteState { bool pending = false; std::string inviterName; }` ; membre `UIModel::partyInvite`.
- Routage `MessageKind::PartyInviteNotify` → `ApplyPartyInviteNotify` (scratch message, set pending + notify `UIModelChangeParty`).
- `UIModelBinding::ClearPartyInvite()` public (appelé par Engine après Accepter/Refuser) ; `ApplyPartyUpdate` efface aussi `partyInvite` (l'invitation est consommée quand on rejoint).

### Task 2: GameplayUdpClient — `SendPartyAccept(clientId)` / `SendPartyDecline(clientId)` (pattern SendRespawnRequest, encodeurs existants).

### Task 3: Engine — PartyHud + popup + ciblage allié
- Membres : `PartyHudPresenter m_partyHud` (+ include), `uint64_t m_selectedAllyEntityId = 0`.
- GameplayNet Init/Shutdown/SetViewportSize + observer `ApplyModel`.
- Rendu (bloc combat SP2, in-world) : si `inParty` — cadres depuis `GetState().frames[]` (fond, nom + badge « C » chef, barre PV verte, barre mana bleue, label loot mode) sur la gauche ; cadre de l'allié sélectionné surligné or.
- Clic gauche sur un cadre (prioritaire sur le pick mob, mêmes gardes souris) : sélectionne l'allié (`entityId = clientId` du membre ; re-clic = désélection). Mort/départ du membre → reset.
- Sorts `SingleAlly` (barre d'action SP3) : envoient `m_selectedAllyEntityId` si non nul (le serveur valide groupe/portée/vivant, fallback soi).
- Popup invitation : fenêtre centrée « <inviter> vous invite dans un groupe » + [Accepter] [Refuser] → Send* + `ClearPartyInvite()` ; visible même hors gardes clavier (c'est une modale souris).
- `SpellDisplay` (SpellKitCatalog) : + `bool targetsAlly` (targetType == "SingleAlly").

### Task 4: Docs + PR
CODEBASE_MAP (section Groupes SP1), push, PR (base main après merge #881 ; draft cumul sinon). **Déploiement : ✅ client uniquement** (au-dessus du lock-step v12 du chantier combat).

## Self-review
- Boucle complète : /invite (chat, existant) → notify (routé) → popup → accept/decline (émis) → PartyUpdate (déjà routé) → PartyHud (câblé) → XP partagée (déjà active) → soins ciblés (SP3 + ciblage allié).
- Pas de nouveau bind clavier (souris uniquement) ; pas de changement serveur ni wire.
