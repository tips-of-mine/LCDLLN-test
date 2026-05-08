# CMANGOS.18 — Mails (master-side / COD / expiration / massmail)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.18_Mails_master_side_cod_expiration_massmail.md](../../../../tickets/CMANGOS/CMANGOS.18_Mails_master_side_cod_expiration_massmail.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : master

## 1. Statut implémentation

❌ **Absent** — pas de système de messagerie in-game. `SmtpMailer` existe
mais c'est pour les emails SMTP externes (confirmation compte, password
reset), orthogonal au mail in-game cmangos.

## 2. Preuves dans le code

**Existant (orthogonal) :**
- [engine/server/SmtpMailer.cpp](../../../../engine/server/SmtpMailer.cpp) — emails SMTP externes (account
  verify, password reset). PAS le mail in-game.
- (`AuctionHouse.cpp` mentionne "INI mailbox" comme fallback persistance
  M35.4 mais ce n'est pas le système Mails général.)

**Manquant (vs spec ticket) :**
- ❌ `engine/server/mail/` — dossier inexistant
- ❌ `Mail` struct (mailId, sender/receiver accountId, subject, body,
  items, copperGold, copperCod, sentTs, expirationTs, state)
- ❌ `MailManager` (Send/TakeItem/TakeGold/DeleteMail/ResolveExpired/
  GetMailsForAccount)
- ❌ `MassMailMgr` (envoi batch événements serveur, GM broadcasts)
- ❌ `MailHandler` opcodes (Send/Get/TakeItem/TakeGold/Delete/MarkRead)
- ❌ Tables `mail` + `mail_items`
- ❌ Workflow COD (Cash On Delivery — paiement avant retrait)
- ❌ Expiration auto (tick périodique)
- ❌ Migration DB

## 3. Recouvrement milestones existantes

❌ **Non couvert** — aucune milestone messagerie in-game dans M00-M44.

## 4. Écart par rapport à la spec CMANGOS

100% absent. La messagerie in-game est un système **central** :
- Pré-requis pour CMANGOS.09 AuctionHouse (livraison après vente)
- Pré-requis pour CMANGOS.08 Arena (distribution récompenses hebdo)
- Outil GM (envoyer des items, gold à un joueur offline)
- Quêtes différées (récompense via mail)
- Communication asynchrone joueur (whisper offline)

## 5. Effort estimé

**L** (1 sprint complet) :
- Migration DB (`mail` + `mail_items`)
- `Mail`/`MailManager` Load + Send + Take + Delete
- Workflow COD (transaction paiement → libération items)
- Expiration auto (tick périodique)
- `MailHandler` opcodes (~6 opcodes)
- `MassMailMgr` (batch INSERT)
- Tests : workflow complet, COD success/refus, expiration, mass send
- Notification online si receiver connecté

Wire-breaking probable (nouveaux opcodes, mais pas de modification
opcode existant).

## 6. Valeur joueur/serveur

**Élevée** — feature visible joueur attendue. Aussi déblocant pour
plusieurs systèmes downstream (AH, Arena, Trade asynchrone).

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.13 Database** — pour persistance + `SQLStorage` (cache mails
  unread par compte ?)
- **CMANGOS.06 Accounts** — sender/receiver = accountId
- **Item system existant** (en place LCDLLN via inventaire)

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — 2 tables (`mail`, `mail_items`). Idempotent.
- ⚠️ **Wire-breaking** — ~6 nouveaux opcodes. Bump `kProtocolVersion` +
  redéploiement master + client lock-step.
- ⚠️ **COD transaction** — paiement + transfer item = 2 ops à atomiser.
  Si crash entre les 2, soit on perd l'item, soit on prend l'argent
  sans donner l'item. Pattern : transaction DB unique avec rollback.
- ⚠️ **Expiration auto** — race condition : joueur prend item au moment
  où le tick expire. Lock optimiste + recheck.
- ⚠️ **MassMail volumétrie** — envoi à 10000 comptes = 10000 INSERTs.
  Batch INSERT (1000 par chunk) + COMMIT par chunk.
- ⚠️ **Anti-spam** — limiter le nombre de mails reçus par compte
  (cmangos = 100). Au-delà, refus envoi.
- ⚠️ **GoldMail anti-fraude** — limiter le montant max par mail (anti
  laundering). Logging audit.
- ⚠️ **Mail système (sender=0)** — distinguer auction mails / quest
  rewards / GM mails. Champ `system_subtype` ?

## 9. Recommandation finale

✅ **Faire en l'état**, **après** CMANGOS.13 Database et CMANGOS.06
Accounts. Avant CMANGOS.09 AuctionHouse V2 et CMANGOS.08 Arena (qui
dépendent du mail system) :

1. **Étape 1** : migration DB + `Mail` struct + `MailManager` Send/Take
   basique + tests workflow simple (pas de COD).
2. **Étape 2** : opcodes `MailHandler` + smoke test client.
3. **Étape 3** : COD workflow + transaction atomique + tests success/
   refus.
4. **Étape 4** : `ResolveExpired` tick + retour expéditeur si pas
   pickup.
5. **Étape 5** : `MassMailMgr` batch + tests volumétrie 10k+.
6. **Étape 6** : intégration AuctionHouse settlement + Arena weekly
   distribution.

À planifier dans la roadmap P2 court-terme. Effort raisonnable, gros
ROI joueur + déblocant downstream.

---

*Audit du 2026-05-08. Mises à jour : —*
