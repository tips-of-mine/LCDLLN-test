# CMANGOS.45 — Auth SRP6 zero-knowledge (optional)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.45_Auth_srp6_zero_knowledge_optional.md](../../../../tickets/CMANGOS/CMANGOS.45_Auth_srp6_zero_knowledge_optional.md)
> **Priorité** : P4 — cas spécifique (optionnel)
> **Cible** : master

## 1. Statut implémentation

❌ **Absent** (volontairement). LCDLLN utilise actuellement le pattern
**hash mot de passe en DB** (auth login + verify hash), explicitement
considéré comme **suffisant** par le ticket source.

## 2. Preuves dans le code

**Existant (auth actuel) :**
- [engine/server/AccountStore.h](../../../../engine/server/AccountStore.h) — gestion comptes + hash password
- [engine/server/AuthRegisterHandler.cpp](../../../../engine/server/AuthRegisterHandler.cpp) — register + verify
- [db/migrations/0004_auth_mvp_m33_1.sql](../../../../db/migrations/0004_auth_mvp_m33_1.sql) — schéma auth
- [db/migrations/0005_auth_m33_2_reset_email.sql](../../../../db/migrations/0005_auth_m33_2_reset_email.sql) — reset email

**Manquant (SRP6 si décision GO) :**
- ❌ `engine/server/auth/srp6/`
- ❌ `BigNumber` wrapper OpenSSL `BIGNUM`
- ❌ Implémentation SRP6 (Secure Remote Password)
- ❌ Migration DB (champ `srp6_verifier` au lieu de `password_hash`)
- ❌ Refonte handlers auth login

## 3. Recouvrement milestones existantes

🟢 **Couvert (par auth actuel)** — M33.1/.2 ont livré le hash auth
fonctionnel.

## 4. Écart par rapport à la spec CMANGOS

100% absent **délibérément**. Le ticket lui-même précise :
> Pas une implémentation immédiate — ce ticket sert de référence si
> LCDLLN décide un jour de quitter le mot-de-passe-hashé-en-base
> actuel pour un protocole où le mot de passe ne transite jamais sur
> le wire.

## 5. Effort estimé

**L** (1 sprint complet) si GO :
- BigNumber wrapper + tests math (modulo, exponentiation, etc.)
- SRP6 protocol (handshake 6 étapes)
- Migration DB (verifier au lieu de hash)
- Refonte handlers auth (côté master + côté client)
- Migration progressive comptes existants

Wire-breaking complet (refonte protocole auth).

## 6. Valeur joueur/serveur

**Faible (opérationnel)** — invisible joueur. Sécurité plus stricte
(mot de passe **jamais** sur le wire, même TLS), résistance aux logs
serveur compromis.

Vs auth actuel (hash en DB + TLS) : gain marginal en pratique.

## 7. Dépendances bloquantes

- AccountStore existant (déjà OK)
- OpenSSL (déjà disponible)

## 8. Risque / piège ⚠️

- ⚠️ **Wire-breaking total** — refonte protocole auth = redéploiement
  master + client lock-step + clients pré-PR ne se connectent plus.
- ⚠️ **Migration comptes existants** — comment convertir hash existant
  → verifier SRP6 ? Soit on demande aux joueurs de re-créer leur
  password, soit on garde 2 systèmes en parallèle (dette).
- ⚠️ **Cryptographie** — implémentation SRP6 délicate, bug = sécurité
  cassée. Tests vectoriels obligatoires (RFC 5054).
- ⚠️ **Performance** — exponentiation modulo grands nombres = CPU
  intensif. Pool worker si load élevé.
- ⚠️ **OpenSSL versioning** — API BIGNUM stable mais évolue. Couche
  abstraction.

## 9. Recommandation finale

🚫 **Ne pas faire** par défaut. **À reconsidérer uniquement si** :
1. Audit sécurité externe l'exige
2. Régulation impose
3. Incident sécurité majeur (compromission DB) post-launch
4. Décision business explicite

L'auth actuel (hash + TLS) est **suffisant** pour la majorité des cas
MMO. SRP6 est un investissement lourd pour un gain marginal.

À garder en référence dans cette fiche pour réactivation future
conditionnelle.

---

*Audit du 2026-05-08. Mises à jour : —*
