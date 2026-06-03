# SERVER-CORE.45_Auth_srp6_zero_knowledge_optional

## Objectif

Documenter et **réserver le design** d'un éventuel passage à un
protocole d'authentification **zero-knowledge** type SRP6, inspiré de
`src/shared/Auth` server-core. **Pas une implémentation immédiate** — ce
ticket sert de référence si LCDLLN décide un jour de quitter le
mot-de-passe-hashé-en-base actuel pour un protocole où le mot de passe
ne transite **jamais** sur le wire.

C'est un **P4 master**, marginal. À ne réaliser que si une raison
spécifique le justifie (audit sécurité, exigence régulation, etc.).

## Dépendances

- M00.1 (build base)
- AccountStore (existant)
- OpenSSL (déjà disponible)

## Livrables (si décision GO)

### Côté master (`engine/server/auth/srp6/`)

- `BigNumber.{h,cpp}` — wrapper RAII autour de `BIGNUM` OpenSSL avec
  opérateurs surchargés (`+`, `*`, `**=`, etc.).
- `Srp6.{h,cpp}` — flux SRP-6a complet :
  - `RegisterFlow` : à la création de compte, calcul de `verifier =
    g^x mod N` côté client, envoi `(salt, verifier)` au serveur. **Le
    mot de passe ne transite jamais.**
  - `LoginFlow` : challenge-response classique SRP-6a (A, B, M1, M2).
  - À la fin, les 2 partis dérivent une `sessionKey` partagée.
- `CryptoHash.{h,cpp}` — wrapper SHA-256 streaming.
- Modification `accounts` table : remplacer `password_hash` par
  `(salt, verifier)`.

### Migration DB

```sql
ALTER TABLE accounts
  ADD COLUMN srp6_salt BLOB,
  ADD COLUMN srp6_verifier BLOB;
-- Période de migration : les 2 schémas coexistent ; au login, fallback
-- sur ancien hash si nouveaux champs absents.
```

### Tests

- `BigNumberTests.cpp` — opérations modulaires.
- `Srp6FlowTests.cpp` — register + login round-trip.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### Flux register (zero-knowledge)

```
Client:
  x = SHA256(salt | username | password)
  verifier = g^x mod N
  → envoie (salt, verifier) au serveur
Serveur:
  stocke (salt, verifier) en DB. Pas de password!
```

### Flux login

```
Client:
  a = random
  A = g^a mod N
  → envoie (username, A) au serveur
Serveur:
  récupère (salt, verifier)
  b = random
  B = (k*verifier + g^b) mod N
  → envoie (salt, B) au client
Client:
  reçoit B, calcule sessionKey via SRP6 algo
  M1 = HMAC(sessionKey, ...)
  → envoie M1 au serveur
Serveur:
  vérifie M1, si OK → envoie M2
  Les 2 ont la même sessionKey, ne s'est jamais transmise.
```

## Étapes d'implémentation

1. (Décision GO) Créer `engine/server/auth/srp6/`.
2. Implémenter `BigNumber` + `CryptoHash`.
3. Implémenter flux register + login.
4. Migration DB avec coexistence ancien/nouveau hash.
5. Période de transition : lors d'un login réussi avec ancien hash,
   demander au client de fournir `(salt, verifier)` puis migrer.
6. Doc.

## Definition of Done (DoD)

- [ ] Décision GO/NO-GO documentée.
- [ ] (Si GO) tests passent.
- [ ] (Si GO) coexistence ancien/nouveau hash fonctionne.

## Notes / pièges à éviter

- **Pas de regret en cas de NO-GO** : le hash en base avec Argon2 (ou bcrypt) actuellement utilisé est **largement** sécurisé. SRP6 répond à des exigences spécifiques (zero-knowledge, MITM resistance avancée).
- **Coût de migration** : 1000 lignes de code crypto, période de migration utilisateurs. Effort substantiel, gain marginal.
- **Compat client** : le client actuel envoie probablement `password` au serveur (handshake actuel). Migrer = update client + serveur en lock-step.

## Références

- `SERVER-CORE_ANALYSIS.md` § Auth (P4 master)
- server-core `src/shared/Auth/SRP6/`, `BigNumber.h`,
  `CryptoHash.h`
- RFC 5054 : SRP for TLS authentication
