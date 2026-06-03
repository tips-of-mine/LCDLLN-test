# Issue: SERVER-CORE.27

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/masterd/trade/TradeSessionRegistry.h
- src/shared/network/TradePayloads.h

## Note
Trade 2-phase commit anti-scam

---

## Contenu du ticket (SERVER-CORE.27)

# SERVER-CORE.27_Trade_two_phase_commit_anti_scam

## Objectif

Mettre en place le **système de trade fenêtre-à-fenêtre entre 2
joueurs**, inspiré de `src/game/Trade` server-core. Quatre piliers :

1. **State machine 2-phase commit** : both-must-accept avant exécution,
   un changement quelconque reset les checkboxes.
2. **`TradeData` attachée au Player** : chaque joueur a un `TradeData*`
   (null si pas en trade), évite les maps globales.
3. **Validation atomique côté serveur uniquement** : le client envoie
   « j'accepte », le serveur revérifie inventaire + gold + bag space
   pour les deux côtés en une transaction DB, rollback si échec.
4. **Anti-scam** : fenêtre de 6 s avant validation finale après dernier
   changement (« lock-and-confirm »), prévient le swap d'items en
   dernière seconde.

C'est un **P2 master**, à activer dès qu'on a inventaire + monnaie + 2
joueurs.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.06 (Accounts)
- Inventory + currency existants
- SERVER-CORE.13 (Database — transaction atomique)

## Livrables

### Côté master (`engine/server/trade/`)

- `TradeData.h` :
  ```cpp
  struct TradeSlot {
    ObjectGuid itemGuid;
    uint32_t itemEntry;     // copie pour anti-stale
    uint32_t count;
  };
  struct TradeData {
    uint32_t initiatorAccountId;
    uint32_t partnerAccountId;
    std::array<TradeSlot, 7> myItems;     // slot 0-6
    std::array<TradeSlot, 7> partnerItems;
    uint64_t myGold;
    uint64_t partnerGold;
    bool myAccepted = false;
    bool partnerAccepted = false;
    int64_t lastChangeTs;
    TradeState state;
  };
  enum class TradeState : uint8 {
    Initiating,
    Active,
    LockingIn,           // 6s anti-scam window
    Completing,          // both accepted + lock expired
    Cancelled,
  };
  ```
- `TradeManager.{h,cpp}` :
  - `bool BeginTrade(initiatorId, partnerId)` — push TradeRequest au partner.
  - `bool AcceptTradeRequest(partnerId)` — passes Active.
  - `bool SetItemSlot(accountId, slot, ObjectGuid item)` — modifie un slot, reset accepted.
  - `bool SetGold(accountId, gold)`
  - `bool ToggleAccept(accountId)` — bascule own accepted ; si both → LockingIn, démarre timer.
  - `void Tick(int64_t nowMs)` — gère timer 6s ; à expiration → Completing → exécution atomique.
  - `void CancelTrade(accountId, reason)`.
- `TradeHandler.{h,cpp}` — opcodes :
  - `kOpcodeTradeInitiate`, `kOpcodeTradeAccept`, `kOpcodeTradeReject`
  - `kOpcodeTradeSetSlot`, `kOpcodeTradeSetGold`
  - `kOpcodeTradeToggleAccept`, `kOpcodeTradeCancel`
  - Notifications client : `kOpcodeTradeUpdate` (état complet),
    `kOpcodeTradeCompleted`, `kOpcodeTradeFailed(reason)`.

### Configuration (`config.json`)

```json
"trade": {
  "lockin_seconds": 6,
  "max_distance_meters": 11.11,
  "allow_same_account_trade": false
}
```

### Tests

- `TradeStateMachineTests.cpp` — transitions Active → LockingIn → Completing.
- `TradeCancelOnChangeTests.cpp` — un slot modifié pendant LockingIn reset accepted, retour Active.
- `TradeAtomicCommitTests.cpp` — exécution = transfert items + gold dans une seule transaction.
- `TradeAntiScamTests.cpp` — modification dans la fenêtre 6s reset la validation.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. State machine

```
None ----initiate----> Initiating
Initiating ----partner accepts----> Active
Active <----both accepted----> LockingIn (6s)
LockingIn ----any change----> Active (reset accepted)
LockingIn ----6s elapsed----> Completing
Completing ----all valid----> Done
Completing ----item missing----> Cancelled
Active ----cancel----> Cancelled
```

### 2. Atomic commit

```cpp
bool ExecuteTrade(TradeData const& td) {
  BeginTransaction();
  // Validate inventory of both sides
  for (auto& slot : td.myItems) {
    if (!ValidateItemOwnership(td.initiatorAccountId, slot.itemGuid)) {
      Rollback();
      return false;
    }
  }
  // Same for partner
  // Validate gold
  if (GetGold(td.initiatorAccountId) < td.myGold) { Rollback(); return false; }
  if (GetGold(td.partnerAccountId) < td.partnerGold) { Rollback(); return false; }
  // Validate inventory space
  if (!HasFreeSlots(td.partnerAccountId, td.myItems.size())) { Rollback(); return false; }
  // Same other side
  // Transfer
  TransferItems(td.initiatorAccountId, td.partnerAccountId, td.myItems);
  TransferItems(td.partnerAccountId, td.initiatorAccountId, td.partnerItems);
  TransferGold(td.initiatorAccountId, td.partnerAccountId, td.myGold);
  TransferGold(td.partnerAccountId, td.initiatorAccountId, td.partnerGold);
  Commit();
  return true;
}
```

### 3. Anti-scam timer

Quand both accepted → `LockingIn` + start timer 6s. Toute modification
(SetSlot, SetGold) → reset à `Active` + reset checkboxes.

UX : le client affiche un compte à rebours (6 → 0) ; les boutons
"Accept" sont grisés pendant ce temps.

### 4. Distance check

Trade = face à face. Vérifier que les 2 joueurs sont dans
`max_distance_meters` à chaque action. Sortie de portée = cancel auto.

## Étapes d'implémentation

1. Créer `engine/server/trade/`.
2. Implémenter `TradeData` + `TradeState`.
3. Implémenter `TradeManager` + state machine.
4. Implémenter opcodes + handlers.
5. Implémenter `ExecuteTrade` atomic.
6. Implémenter timer anti-scam.
7. Tests : 4 fichiers.
8. Doc : section « Trade master » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master)
- [ ] Tests passent
- [ ] Smoke test : trade items entre 2 joueurs → both accepted → 6s → exécution
- [ ] Anti-scam : changement dans la fenêtre 6s reset accepted
- [ ] Échec validation (item manquant) → rollback complet
- [ ] Sortie portée → cancel auto
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Item locking pendant trade** : un item dans un slot trade ne doit pas être supprimable de l'inventaire (sell, vendor, drop). Marquer `is_in_trade` ou refuser les ops.
- **Anti-cheat** : si le client envoie `SetSlot` avec un `ObjectGuid` qu'il ne possède pas → kick + log audit.
- **Trade et logout** : si un des 2 joueurs déconnecte → cancel auto.
- **Trade et combat** : interdire de démarrer un trade en combat (anti exploit).
- **Trade cross-shard** : 2 joueurs sur shards différents ? Plus complexe (besoin du master comme intermédiaire). **Démarrer same-shard only**, le master refuse cross-shard ou route vers même shard physique.
- **Bag space failure mid-commit** : si la transaction échoue à cause d'un manque de place, rollback DB + retour items aux propriétaires d'origine + notification "trade failed: not enough space".
- **Same-account trade** : par défaut **désactivé** (anti-mule). Activer via config si LCDLLN veut le permettre.

## Références

- `SERVER-CORE_ANALYSIS.md` § Trade (P2 master)
- server-core `src/game/Trade/TradeHandler.cpp`
