# SERVER-CORE.32_GMTickets_support_in_game

## Objectif

Mettre en place un **système de tickets de support en jeu** côté master
LCDLLN, inspiré de `src/game/GMTickets` server-core. Trois piliers :

1. **Persistence simple** : un ticket = une row DB avec état
   (open/in_progress/closed), pas de file in-memory à reconstruire.
2. **Handler dédié séparé du Mgr** : le réseau (handler) ne touche que
   le manager, le manager ne touche que la DB.
3. **Notification asymétrique** : le joueur ne voit que son ticket, le
   GM voit la file globale.

C'est un **P3 master**.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.06 (Accounts — flag GM)
- SERVER-CORE.13 (Database)

## Livrables

### Côté master (`engine/server/gmtickets/`)

- `GMTicket.h` :
  ```cpp
  enum class TicketState : uint8 { Open, InProgress, Closed };
  struct GMTicket {
    uint64_t ticketId;
    uint32_t reporterAccountId;
    std::string subject;
    std::string body;
    TicketState state;
    uint32_t assignedGmAccountId;       // 0 si non assigné
    int64_t createdTs;
    int64_t lastUpdateTs;
    std::string gmResponse;
  };
  ```
- `GMTicketManager.{h,cpp}` :
  - `Create(reporterId, subject, body)`
  - `void TakeOwnership(ticketId, gmAccountId)`
  - `void Respond(ticketId, gmAccountId, response)`
  - `void Close(ticketId)`
  - `std::vector<GMTicket> ListOpen()` — pour l'UI GM.
  - `std::optional<GMTicket> GetForReporter(accountId)` — un seul ticket actif par reporter.
- `GMTicketHandler.{h,cpp}` — opcodes :
  - `kOpcodeGMTicketCreate`
  - `kOpcodeGMTicketStatus`
  - `kOpcodeGMTicketClose`
  - `kOpcodeGMTicketGmList` (GM only)
  - `kOpcodeGMTicketGmRespond` (GM only)

### Migration DB

```sql
CREATE TABLE gm_ticket (
  ticket_id              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  reporter_account_id    INT UNSIGNED NOT NULL,
  subject                VARCHAR(128) NOT NULL,
  body                   TEXT,
  state                  TINYINT UNSIGNED NOT NULL DEFAULT 0,
  assigned_gm_account_id INT UNSIGNED NOT NULL DEFAULT 0,
  gm_response            TEXT,
  created_ts             BIGINT NOT NULL,
  last_update_ts         BIGINT NOT NULL,
  PRIMARY KEY (ticket_id),
  KEY idx_reporter (reporter_account_id),
  KEY idx_state (state)
);
```

### Configuration (`config.json`)

```json
"gm_tickets": {
  "max_per_reporter": 1,
  "auto_close_inactive_days": 30
}
```

### Tests

- `GMTicketManagerTests.cpp` — create, take ownership, respond, close.
- `GMTicketAuthTests.cpp` — joueur ne peut pas voir tickets d'autres ; GM voit tous.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Étapes d'implémentation

1. Créer `engine/server/gmtickets/`.
2. Migration DB.
3. Implémenter `GMTicketManager`.
4. Implémenter handler + opcodes.
5. Câbler check rôle GM (SERVER-CORE.06).
6. Tests : 2 fichiers.
7. Doc : section « GM Tickets » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master)
- [ ] Tests passent
- [ ] Smoke test : joueur create ticket → GM list voit le ticket → GM répond → joueur reçoit notif
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Spam protection** : `max_per_reporter = 1` empêche un joueur d'inonder. Doit fermer son ticket avant d'en créer un nouveau.
- **GM offline notification** : si aucun GM en ligne, ticket reste en queue. Notif Discord/Slack via webhook si urgence ? Reportable.
- **Visibilité** : une commande GM `.gmticket list` permet de voir la file ; un joueur n'a accès qu'à sa propre commande `.ticket status`.
- **Auto-close** : `auto_close_inactive_days` empêche les tickets de pourrir indéfiniment. Cron périodique.

## Références

- `SERVER-CORE_ANALYSIS.md` § GMTickets (P3 master)
- server-core `src/game/GMTickets/GMTicketMgr.cpp`,
  `GMTicketHandler.cpp`
