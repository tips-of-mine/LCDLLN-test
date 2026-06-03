# SERVER-CORE.18_Mails_master_side_cod_expiration_massmail

## Objectif

Mettre en place la **messagerie asynchrone in-game** côté master LCDLLN,
inspirée de `src/game/Mails` server-core. Master-side car cross-shard
naturel. Quatre piliers :

1. **Schéma `mail` + `mail_items`** : un mail = 1 row + N rows attachements.
2. **Workflow COD (Cash On Delivery)** : pièce jointe libérée seulement
   après paiement, sinon retour à l'expéditeur — pattern transactionnel
   utile pour AH/trade asynchrone.
3. **Expiration auto** avec tâche périodique qui retourne ou supprime
   les mails périmés.
4. **`MassMailMgr`** : envoi en masse (events serveur, GM broadcasts,
   retours auction house) avec batching DB pour éviter N inserts.

C'est un **P2 master**, pré-requis dès qu'on a 2 joueurs (whisper
asynchrone) et requis pour AuctionHouse (SERVER-CORE.09).

## Dépendances

- M00.1 (build base)
- SERVER-CORE.13 (Database)
- SERVER-CORE.06 (Accounts — sender/receiver = accountId)
- Item system existant (pré-requis fonctionnel)

## Livrables

### Côté master (`engine/server/mail/`)

- `Mail.h` — struct :
  ```cpp
  struct Mail {
    uint64_t mailId;
    uint32_t senderAccountId;       // 0 = system mail
    uint32_t receiverAccountId;
    std::string subject;
    std::string body;
    std::vector<ObjectGuid> items;
    uint64_t copperGold;            // gold attaché (en copper)
    uint64_t copperCod;              // si != 0, montant à payer pour récupérer items
    int64_t  sentTs;
    int64_t  expirationTs;
    MailState state;                 // Unread, Read, ReturnedToSender, Deleted
  };
  ```
- `MailManager.{h,cpp}` :
  - `uint64_t Send(Mail&& mail)` — persist DB, notify receiver si online.
  - `bool TakeItem(mailId, itemIdx, accountId)` — paye COD si applicable, transfère item.
  - `bool TakeGold(mailId, accountId)`.
  - `bool DeleteMail(mailId, accountId)` (mark deleted).
  - `void ResolveExpired(int64_t nowMs)` — tick périodique.
  - `std::vector<Mail> GetMailsForAccount(accountId, limit)` — pagination.
- `MassMailMgr.{h,cpp}` — envoi en masse :
  - `void SendToAllAccounts(Mail templateMail, std::vector<accountId> targets)` — batch INSERT.
- `MailHandler.{h,cpp}` — opcodes (cf. ci-dessous).

### Migration DB

```sql
CREATE TABLE mail (
  mail_id              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  sender_account_id    INT UNSIGNED NOT NULL DEFAULT 0,
  receiver_account_id  INT UNSIGNED NOT NULL,
  subject              VARCHAR(128) NOT NULL DEFAULT '',
  body                 TEXT,
  copper_gold          BIGINT UNSIGNED NOT NULL DEFAULT 0,
  copper_cod           BIGINT UNSIGNED NOT NULL DEFAULT 0,
  sent_ts              BIGINT NOT NULL,
  expiration_ts        BIGINT NOT NULL,
  state                TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (mail_id),
  KEY idx_receiver_state (receiver_account_id, state),
  KEY idx_expiration (expiration_ts)
);

CREATE TABLE mail_items (
  mail_id     BIGINT UNSIGNED NOT NULL,
  item_guid   BIGINT UNSIGNED NOT NULL,
  taken       TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (mail_id, item_guid)
);
```

### Configuration (`config.json`)

```json
"mail": {
  "default_expiration_days": 30,
  "system_mail_expiration_days": 7,
  "max_mails_per_player": 100,
  "expiration_check_interval_min": 60
}
```

### Opcodes

- `kOpcodeMailListRequest` (client → master)
- `kOpcodeMailSend` (avec attachments)
- `kOpcodeMailTakeItem`
- `kOpcodeMailTakeGold`
- `kOpcodeMailDelete`
- `kOpcodeMailMarkRead`

### Tests

- `MailManagerTests.cpp` — send + receive + take.
- `MailCodTests.cpp` — COD : take refusé sans gold, accepté avec gold.
- `MailExpirationTests.cpp` — expired retourne au sender.
- `MassMailTests.cpp` — batch INSERT 1000 mails en 1 transaction.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. COD workflow

```
Sender: Send(items, cod=100g)
Mail expiration_ts = now + 3d  (COD shorter than normal mails)
Receiver TakeItem:
  - if cod > 0: deduct cod from receiver, send gold to sender (new mail)
  - transfer item to receiver inventory
  - mark item as "taken"
Receiver Delete:
  - if items still attached → REFUSE (ne peut pas delete tant qu'items unclaimed)
  - if no items → mark deleted
```

### 2. Expiration

```cpp
void ResolveExpired(int64_t nowMs) {
  auto expired = QueryExpired(nowMs);
  for (auto& mail : expired) {
    if (mail.HasUntakenItems() || mail.copperGold > 0) {
      // Retour à l'expéditeur (sauf system mail = sender 0)
      if (mail.senderAccountId != 0) {
        ReturnToSender(mail);
      } else {
        DropItems(mail);    // system mail expiré : items perdus
      }
    }
    mail.state = Deleted;
    UpdateDb(mail);
  }
}
```

### 3. MassMail batched

```cpp
void MassMailMgr::SendToAllAccounts(Mail tmpl, std::vector<uint32_t> targets) {
  // 1 transaction DB pour N mails
  BeginTransaction();
  for (auto accountId : targets) {
    auto m = tmpl;
    m.receiverAccountId = accountId;
    m.mailId = NextId();
    InsertMailRow(m);
    InsertItemRows(m);
  }
  CommitTransaction();
  // notify online receivers in batch
}
```

Évite 1000 INSERTs séparés (catastrophique pour MySQL) → 1 INSERT bulk.

## Étapes d'implémentation

1. Créer `engine/server/mail/`.
2. Migrations DB.
3. Implémenter `MailManager` + opcodes.
4. Implémenter `MassMailMgr` (batch INSERT).
5. Câbler `ResolveExpired` dans le tick master.
6. Implémenter COD workflow.
7. Tests : 4 fichiers.
8. Doc : section « Mail master » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master)
- [ ] Tests passent
- [ ] Smoke test : envoi mail avec item → receiver le récupère
- [ ] COD : take refusé sans gold, accepté avec gold + transfert au sender
- [ ] Expiration : mail avec items expire → retour sender
- [ ] MassMail 1000 destinataires en 1 transaction
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Items en mail = inventaire fantôme** : les items attachés ne sont pas dans l'inventaire du sender ni du receiver. Tracker explicitement (table `mail_items`) sinon double-spend.
- **Sender offline** : si receiver take un item COD → gold à transférer au sender. Sender peut être offline. → Créer un mail système au sender avec le gold.
- **Cycles de retour** : si retour sender → expire à nouveau → retour again ? Limiter : un mail "ReturnedToSender" qui expire est **deleted**, pas re-renvoyé.
- **Performance list mails** : un joueur avec 100 mails → query lourde au login. Paginer (`limit + offset`) côté handler.
- **Multi-shard delivery** : le master sait qu'un receiver est online sur shard X via `SessionManager`. Push une notification "New mail" via le shard.
- **Item GUID stale** : si un item attaché en mail est référencé ailleurs (oubli de detach), corruption. Toujours `Detach(itemGuid)` avant `Send`.
- **Anti-spam** : `max_mails_per_player = 100`. Au delà, refuser. Empêcher les bots d'inonder.

## Références

- `SERVER-CORE_ANALYSIS.md` § Mails (P2 master)
- server-core `src/game/Mails/Mail.cpp`, `MailHandler.cpp`,
  `MassMailMgr.cpp`
