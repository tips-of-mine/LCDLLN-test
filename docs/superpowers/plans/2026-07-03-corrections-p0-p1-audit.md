# Corrections P0/P1 de l'audit 2026-07-03 — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Corriger 8 findings P0/P1 de l'audit (F1-F6, F8, F9) ; F7 différé (documenté).

**Architecture:** Corrections serveur + deploy, sur la branche `claude/funny-bardeen-f6f803`, server-first, un commit par finding. F3 introduit un tag HMAC en **préfixe** (32 octets de tête) sur les payloads REGISTER/HEARTBEAT pour éviter l'ambiguïté avec le tableau optionnel de joueurs en queue du heartbeat.

**Tech Stack:** C++17, OpenSSL (HMAC-SHA256, `CRYPTO_memcmp`), CMake, tests standalone `int main()` façon `ShardPayloadsTests.cpp`, CI Linux `ctest`.

## Global Constraints

- Commentaires et messages en **français** ; identifiants/commandes en anglais.
- **Aucune modification du binaire client de jeu** (F7 différé ; le REGISTER shard part de `shardd`).
- Ne pas utiliser le terme « CMANGOS ».
- Nouveau code / fichier / dossier en **PascalCase** ; `m_camelCase`/`kPascalCase` conservés pour le style existant.
- **Piège `assert`+`NDEBUG`** : ne jamais mettre de logique de validation dans un `assert` (la CI build en release).
- **`server_app` ne linke pas `engine_core`** : tout nouveau `.cpp` partagé utilisé côté serveur doit être ajouté **aussi** à la liste `server_app` dans `src/CMakeLists.txt`.
- **Pas de build local** (ni cmake/MSVC/vcpkg) : compilation et `ctest` via la CI uniquement. Les steps « run test » décrivent le résultat CI attendu.
- F3 est **wire-breaking** + F6 change le **boot** → déploiement **lock-step master+shard**.
- Constantes utiles : `kShardTicketHmacSize = 32` (`src/shared/network/ShardTicketPayloads.h:17`), `kOpcodeResendVerificationRequest = 37`, `kOpcodeShardRegister = 10`, `kOpcodeShardHeartbeat = 13`, `shard.ticket_hmac_secret` = clé du secret partagé.

---

## Cartographie des fichiers

| Finding | Fichiers touchés |
|---------|------------------|
| F9 | `src/shared/account/AccountValidation.cpp`, `src/masterd/email/SmtpMailer.cpp`, test `AccountValidation` |
| F5 | `src/masterd/main_linux.cpp` (dispatch ~1097) |
| F8 | `src/masterd/handlers/password/PasswordResetHandler.cpp` (`HandleVerifyEmail`) |
| F2 | `src/masterd/handlers/shard/ShardRegisterHandler.cpp` (`HandleHeartbeat`) |
| F4 | `src/shared/security/CaptchaVerifier.cpp` (`VerifyHttp`) |
| F3 | **Create** `src/shared/network/ShardWireAuth.{h,cpp}` ; `src/shared/network/ShardToMasterClient.{h,cpp}` ; `src/masterd/handlers/shard/ShardRegisterHandler.{h,cpp}` ; `src/masterd/main_linux.cpp` ; `src/shardd/main_linux.cpp` ; `src/CMakeLists.txt` ; test `src/shared/network/ShardWireAuthTests.cpp` |
| F6 | **Create** `src/shared/security/SharedSecretPolicy.{h,cpp}` ; `src/masterd/main_linux.cpp` ; `src/shardd/main_linux.cpp` ; 3 configs `deploy/docker/config/master.config.json`, `deploy/docker/config/shard.config.json`, `deploy/docker/server-config/config.json` ; `src/CMakeLists.txt` ; test `src/shared/security/SharedSecretPolicyTests.cpp` |
| F1/F10 | `deploy/docker/sql/migrations/` (resync) ; garde CI `.github/workflows/` |
| F7 | `docs/audit/2026-07-03-audit-complet-frais.md` (marquer différé) |

---

## Task 1 : F9 — rejeter les caractères de contrôle dans l'e-mail

**Files:**
- Modify: `src/shared/account/AccountValidation.cpp:36` (`ValidateEmail`)
- Modify: `src/masterd/email/SmtpMailer.cpp` (`Send`, défense en profondeur)
- Test: fichier de test `AccountValidation` (voir Step 1 pour la localisation)

**Interfaces:**
- Consumes: `engine::network::NetErrorCode`, `engine::server::ValidateEmail(std::string_view)`.
- Produces: `ValidateEmail` rejette désormais tout octet `< 0x20` ou `== 0x7F`.

- [ ] **Step 1 : Écrire le test qui échoue**

Localiser le test unitaire d'`AccountValidation` : `grep -rln "ValidateEmail" src --include=*Test*`. S'il existe, y ajouter les cas ci-dessous ; sinon créer `src/shared/account/AccountValidationTests.cpp` sur le modèle de `src/shared/network/ShardPayloadsTests.cpp` (fonction `Assert`, `int main()` renvoyant `s_failCount != 0`) et l'enregistrer dans le `CMakeLists.txt` du dossier comme les autres tests.

```cpp
// Cas à ajouter :
using engine::server::ValidateEmail;
using engine::network::NetErrorCode;
Assert(ValidateEmail("a@b.com") == NetErrorCode::OK, "email valide accepté");
Assert(ValidateEmail(std::string("a@b.com\r\nRCPT TO:<x@evil>")) == NetErrorCode::INVALID_EMAIL,
       "CRLF au milieu rejeté");
Assert(ValidateEmail(std::string("a\tb@c.com")) == NetErrorCode::INVALID_EMAIL, "TAB rejeté");
{ std::string withNul = "a@b.com"; withNul.push_back('\0'); withNul += "x";
  Assert(ValidateEmail(withNul) == NetErrorCode::INVALID_EMAIL, "NUL rejeté"); }
```

- [ ] **Step 2 : Vérifier l'échec (CI)**

En CI, `ctest` sur la cible de test `AccountValidation` doit **échouer** : `ValidateEmail` accepte encore CR/LF/TAB/NUL (aucune vérification de caractères de contrôle). Résultat attendu : `[FAIL] CRLF au milieu rejeté`.

- [ ] **Step 3 : Implémenter**

Dans `src/shared/account/AccountValidation.cpp`, ajouter la vérification en tête de `ValidateEmail` (après le test `empty`, avant le reste) :

```cpp
	engine::network::NetErrorCode ValidateEmail(std::string_view normalisedEmail)
	{
		if (normalisedEmail.empty())
			return engine::network::NetErrorCode::INVALID_EMAIL;
		// Sécurité (audit F9) : aucun caractère de contrôle (0x00-0x1F, 0x7F) où que ce soit —
		// empêche l'injection SMTP (CR/LF vers RCPT TO/headers) via l'adresse enregistrée.
		for (unsigned char c : normalisedEmail)
		{
			if (c < 0x20u || c == 0x7Fu)
				return engine::network::NetErrorCode::INVALID_EMAIL;
		}
		if (normalisedEmail.size() > kAccountEmailMaxLength)
			return engine::network::NetErrorCode::INVALID_EMAIL;
		// ... reste inchangé (@, domaine, '.') ...
```

Défense en profondeur — dans `src/masterd/email/SmtpMailer.cpp`, au début de `Send` (repérer la signature via `grep -n "::Send" src/masterd/email/SmtpMailer.cpp`), rejeter tout CR/LF dans les champs insérés dans le protocole :

```cpp
	// Sécurité (audit F9) : refuser CR/LF dans les champs injectés dans les commandes/headers SMTP.
	auto hasCrlf = [](std::string_view s) { return s.find('\r') != std::string_view::npos || s.find('\n') != std::string_view::npos; };
	if (hasCrlf(to) || hasCrlf(subject) || hasCrlf(from_address))
	{
		LOG_WARN(Core, "[SmtpMailer] Send refusé : CR/LF détecté dans to/subject/from");
		return false; // adapter au type de retour réel de Send (bool attendu)
	}
```

Adapter les noms de paramètres (`to`/`subject`/`from_address`) à la signature réelle et le `return false` au type de retour effectif.

- [ ] **Step 4 : Vérifier le passage (CI)**

`ctest` sur la cible `AccountValidation` doit **passer** (4 cas verts).

- [ ] **Step 5 : Commit**

```bash
git add src/shared/account/AccountValidation.cpp src/masterd/email/SmtpMailer.cpp src/shared/account/AccountValidationTests.cpp
git commit -m "fix(security): F9 — rejet des caractères de contrôle dans l'e-mail (anti-injection SMTP)"
```

---

## Task 2 : F5 — router `kOpcodeResendVerificationRequest`

**Files:**
- Modify: `src/masterd/main_linux.cpp:1097-1099` (condition de dispatch)

**Interfaces:**
- Consumes: `kOpcodeResendVerificationRequest` (=37), `passwordResetHandler.HandlePacket(...)` qui sait déjà traiter cet opcode.
- Produces: l'opcode 37 est routé vers `PasswordResetHandler`.

- [ ] **Step 1 : Implémenter (une ligne)**

Dans `src/masterd/main_linux.cpp`, la condition (lignes ~1097-1099) :

```cpp
		else if (opcode == kOpcodeForgotPasswordRequest
		      || opcode == kOpcodeResetPasswordRequest
		      || opcode == kOpcodeVerifyEmailRequest)
			passwordResetHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
```

devient :

```cpp
		else if (opcode == kOpcodeForgotPasswordRequest
		      || opcode == kOpcodeResetPasswordRequest
		      || opcode == kOpcodeVerifyEmailRequest
		      || opcode == kOpcodeResendVerificationRequest)
			passwordResetHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
```

- [ ] **Step 2 : Vérifier (revue)**

Pas de test unitaire isolé du `main`. Vérifier par lecture que `PasswordResetHandler::HandlePacket` route bien `kOpcodeResendVerificationRequest` vers `HandleResendVerification` (déjà le cas, PasswordResetHandler.cpp:~49) et que la condition compile.

- [ ] **Step 3 : Commit**

```bash
git add src/masterd/main_linux.cpp
git commit -m "fix(auth): F5 — router kOpcodeResendVerificationRequest vers PasswordResetHandler"
```

---

## Task 3 : F8 — rate-limit sur `HandleVerifyEmail`

**Files:**
- Modify: `src/masterd/handlers/password/PasswordResetHandler.cpp:230-291` (`HandleVerifyEmail`)
- Test: `src/masterd/handlers/password/` (test existant du handler ; voir Step 1)

**Interfaces:**
- Consumes: `m_rateLimit` (`engine::server::RateLimitAndBan*`, déjà injecté via `SetRateLimitAndBan`, actuellement inutilisé). API : `bool IsBanned(std::string_view) const`, `void RecordAuthFailure(std::string_view)` (bannit après `max_failures_before_ban`, défaut 5, pour `ban_duration_sec`, défaut 3600s).
- Produces: `HandleVerifyEmail` refuse les tentatives après N codes erronés, clé `"vemail:"+account_id`.

- [ ] **Step 1 : Écrire le test qui échoue**

Localiser le test du handler : `grep -rln "PasswordResetHandler\|HandleVerifyEmail" src/masterd --include=*Test*`. Ajouter un cas (ou créer le fichier sur le modèle standalone) : construire un `PasswordResetHandler` avec un `RateLimitAndBan` réel (`SetConfig` par défaut, `max_failures_before_ban=5`), un `AccountStore` en mémoire avec un compte non vérifié, un `PasswordResetStore` avec un code connu. Envoyer 5 `HandleVerifyEmail` avec un **mauvais** code puis un 6ᵉ avec le **bon** code ; asserter que le 6ᵉ est refusé (compte verrouillé) malgré le bon code.

```cpp
// Pseudo-cas (adapter aux helpers de test existants du handler) :
for (int i = 0; i < 5; ++i) handler.HandleVerifyEmail(conn, req, sess, badPayload.data(), badPayload.size());
handler.HandleVerifyEmail(conn, req, sess, goodPayload.data(), goodPayload.size());
Assert(!account.email_verified, "verrouillé après 5 échecs : bon code refusé");
```

- [ ] **Step 2 : Vérifier l'échec (CI)**

Sans rate-limit, le 6ᵉ appel avec le bon code marque `email_verified=true` → l'assert échoue.

- [ ] **Step 3 : Implémenter**

Dans `src/masterd/handlers/password/PasswordResetHandler.cpp`, `HandleVerifyEmail`, après avoir obtenu `account_id` (ligne ~252) et **avant** `ValidateVerificationCode` :

```cpp
		const uint64_t account_id = parsed->account_id;
		// Sécurité (audit F8) : verrou anti-brute-force sur le code à 6 chiffres, clé par compte.
		const std::string rlKey = "vemail:" + std::to_string(account_id);
		if (m_rateLimit && m_rateLimit->IsBanned(rlKey))
		{
			LOG_WARN(Auth, "[PasswordResetHandler] VerifyEmail: verrouillé (trop d'échecs) account_id={}", account_id);
			auto pkt = BuildVerifyEmailResponseErrorPacket(NetErrorCode::VERIFICATION_CODE_INVALID, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
```

Puis, dans la branche « code invalide » (ligne ~274-280), enregistrer l'échec :

```cpp
		if (!m_resetStore->ValidateVerificationCode(account_id, parsed->code))
		{
			if (m_rateLimit) m_rateLimit->RecordAuthFailure(rlKey); // audit F8 : bannit après N échecs
			LOG_WARN(Auth, "[PasswordResetHandler] VerifyEmail: invalid or expired code (account_id={})", account_id);
			auto pkt = BuildVerifyEmailResponseErrorPacket(NetErrorCode::VERIFICATION_CODE_INVALID, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
```

Note : la réponse en cas de verrou est **identique** à celle d'un code invalide (`VERIFICATION_CODE_INVALID`) — pas de divulgation « verrouillé » qui aiderait l'énumération. La clé par `account_id` limite le verrou au seul compte visé (DoS mineur assumé sur l'email-verify de ce compte).

- [ ] **Step 4 : Vérifier le passage (CI)**

Le 6ᵉ appel est rejeté par `IsBanned` → `email_verified` reste `false` → assert vert.

- [ ] **Step 5 : Commit**

```bash
git add src/masterd/handlers/password/PasswordResetHandler.cpp <fichier de test>
git commit -m "fix(security): F8 — rate-limit anti-brute-force sur la vérification d'e-mail"
```

---

## Task 4 : F2 — lier le heartbeat shard à la connexion enregistrée

**Files:**
- Modify: `src/masterd/handlers/shard/ShardRegisterHandler.cpp:127-148` (`HandleHeartbeat`)

**Interfaces:**
- Consumes: `m_registry->GetShardConnection(uint32_t shard_id)` (déclaré `ShardRegistry.h:96-99`, renvoie la `connId` enregistrée ; vérifier le type de retour exact — probablement `uint32_t`, 0 si absent).
- Produces: `HandleHeartbeat` rejette un heartbeat dont la `connId` ne correspond pas au shard enregistré.

- [ ] **Step 1 : Vérifier l'API `GetShardConnection`**

`grep -n "GetShardConnection" src/masterd/shards/ShardRegistry.h` pour confirmer la signature exacte (paramètre `shard_id`, type de retour). Adapter le code du Step 2 en conséquence (ex. `std::optional<uint32_t>` vs `uint32_t` sentinelle 0).

- [ ] **Step 2 : Implémenter**

Dans `src/masterd/handlers/shard/ShardRegisterHandler.cpp`, remplacer la signature `HandleHeartbeat(uint32_t /*connId*/, …)` par `HandleHeartbeat(uint32_t connId, …)` et ajouter la vérification après le parse réussi, avant `UpdateHeartbeat` :

```cpp
	void ShardRegisterHandler::HandleHeartbeat(uint32_t connId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseShardHeartbeatPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Server, "[SREG] HandleHeartbeat: parse failed");
			return;
		}
		// Sécurité (audit F2) : le shard_id du heartbeat doit correspondre à la connId qui a
		// réalisé le REGISTER. Rejette un heartbeat forgé pour un shard_id qu'on ne possède pas.
		const uint32_t registered = m_registry->GetShardConnection(parsed->shard_id); // adapter si optional
		if (registered == 0u || registered != connId)
		{
			LOG_WARN(Server, "[SREG] HandleHeartbeat rejeté : connId={} ne correspond pas au shard_id={} (attendu connId={})",
				connId, parsed->shard_id, registered);
			return;
		}
		m_registry->UpdateHeartbeat(parsed->shard_id, parsed->current_load);
		if (m_presenceCache)
			m_presenceCache->Update(parsed->shard_id, parsed->players);
	}
```

Mettre à jour la déclaration correspondante dans `src/masterd/handlers/shard/ShardRegisterHandler.h` (paramètre `connId` nommé). L'appel `HandleHeartbeat(connId, payload, payloadSize)` existe déjà (ShardRegisterHandler.cpp:44).

- [ ] **Step 3 : Vérifier (revue + CI compile)**

Revue : le chemin REGISTER appelle bien `SetShardConnection(shard_id, connId)` (lignes 93/115), donc `GetShardConnection` est peuplé avant tout heartbeat légitime. Compilation CI verte.

- [ ] **Step 4 : Commit**

```bash
git add src/masterd/handlers/shard/ShardRegisterHandler.cpp src/masterd/handlers/shard/ShardRegisterHandler.h
git commit -m "fix(security): F2 — lier le heartbeat shard à la connId enregistrée"
```

---

## Task 5 : F4 — vérifier le certificat TLS dans `CaptchaVerifier`

**Files:**
- Modify: `src/shared/security/CaptchaVerifier.cpp:215` (`VerifyHttp`)

**Interfaces:**
- Consumes: OpenSSL (`SSL_CTX`, `SSL`), la variable locale `host` de `VerifyHttp`.
- Produces: `VerifyHttp` échoue si le certificat serveur n'est pas validé par le trust store système.

- [ ] **Step 1 : Vérifier le trust store Docker**

`ca-certificates` est **déjà** installé dans `deploy/docker/Dockerfile.master:14` — aucun changement Docker requis. Confirmer par lecture.

- [ ] **Step 2 : Implémenter**

Dans `src/shared/security/CaptchaVerifier.cpp`, après `SSL_CTX_new(TLS_client_method())` (~ligne 215) et avant `SSL_connect` :

```cpp
	// Sécurité (audit F4) : vérifier le certificat serveur (défaut OpenSSL = SSL_VERIFY_NONE).
	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
	if (SSL_CTX_set_default_verify_paths(ctx) != 1)
	{
		LOG_WARN(Core, "[CaptchaVerifier] trust store système indisponible");
		// poursuivre : SSL_get_verify_result rejettera si le cert n'est pas validable.
	}
	// ... création de SSL* ssl = SSL_new(ctx); ... (code existant) ...
	// Avant SSL_connect, épingler le hostname attendu :
	SSL_set1_host(ssl, host.c_str()); // host = domaine cible (hcaptcha.com / google.com)
```

Après le handshake `SSL_connect` réussi, ajouter :

```cpp
	// Sécurité (audit F4) : refuser si la chaîne/hostname n'est pas validée.
	const long verifyResult = SSL_get_verify_result(ssl);
	if (verifyResult != X509_V_OK)
	{
		LOG_WARN(Core, "[CaptchaVerifier] validation certificat échouée (code={})", verifyResult);
		/* fermeture/cleanup identique au chemin d'erreur existant */
		return false; // adapter au type/chemin de sortie réel de VerifyHttp
	}
```

Adapter les noms (`ctx`, `ssl`, `host`) et le chemin de cleanup/retour à la structure réelle de la fonction. Includes nécessaires : `<openssl/x509.h>` (pour `X509_V_OK`), déjà tiré indirectement par `<openssl/ssl.h>`.

- [ ] **Step 3 : Vérifier (compile CI + revue)**

Difficile à tester en unitaire (réseau réel). Vérifier : compilation CI verte, présence des 3 gardes (`SSL_VERIFY_PEER`, `SSL_set1_host`, `SSL_get_verify_result`). Note : le chemin Windows (`Verify()` renvoyant `true` sans réseau) est un stub dev hors périmètre (serveur = Linux).

- [ ] **Step 4 : Commit**

```bash
git add src/shared/security/CaptchaVerifier.cpp
git commit -m "fix(security): F4 — vérification du certificat TLS sur l'appel CAPTCHA"
```

---

## Task 6 : F3 — authentifier `SHARD_REGISTER` / `SHARD_HEARTBEAT` (wire-breaking)

**Files:**
- Create: `src/shared/network/ShardWireAuth.h`, `src/shared/network/ShardWireAuth.cpp`
- Create: `src/shared/network/ShardWireAuthTests.cpp`
- Modify: `src/shared/network/ShardToMasterClient.h`, `src/shared/network/ShardToMasterClient.cpp`
- Modify: `src/masterd/handlers/shard/ShardRegisterHandler.h`, `src/masterd/handlers/shard/ShardRegisterHandler.cpp`
- Modify: `src/masterd/main_linux.cpp` (câbler le secret), `src/shardd/main_linux.cpp` (câbler le secret)
- Modify: `src/CMakeLists.txt` (ajouter `ShardWireAuth.cpp` à la lib réseau partagée **et** à `server_app`)

**Interfaces:**
- Produces:
  - `constexpr size_t engine::network::kShardAuthTagSize = 32u;`
  - `std::vector<uint8_t> engine::network::WrapShardAuth(std::string_view secret, const std::vector<uint8_t>& body);` — renvoie `tag(32)||body`, ou `{}` si secret vide/erreur.
  - `std::optional<std::pair<const uint8_t*, size_t>> engine::network::UnwrapShardAuth(std::string_view secret, const uint8_t* payload, size_t size);` — vérifie le tag préfixe (temps constant) et renvoie `{body, bodySize}` (pointeur après le tag), `nullopt` si trop court / tag invalide / secret vide.
  - `void ShardToMasterClient::SetSharedSecret(std::string secret);`
  - `void ShardRegisterHandler::SetSecret(std::string secret);`

- [ ] **Step 1 : Écrire le test qui échoue**

Créer `src/shared/network/ShardWireAuthTests.cpp` sur le modèle de `ShardPayloadsTests.cpp` (fonction `Assert`, `int main()` renvoyant non-zéro si échec) et l'enregistrer dans le `CMakeLists.txt` qui déclare `ShardPayloadsTests` (repérer via `grep -rn "ShardPayloadsTests" src`).

```cpp
#include "src/shared/network/ShardWireAuth.h"
#include <cstdlib>
#include <iostream>
#include <vector>
using engine::network::WrapShardAuth;
using engine::network::UnwrapShardAuth;
using engine::network::kShardAuthTagSize;

int main()
{
	int fails = 0;
	auto A = [&](bool c, const char* m){ if(!c){ ++fails; std::cerr << "[FAIL] " << m << "\n"; } };
	const std::string secret = "un-secret-fort-de-test";
	std::vector<uint8_t> body = {1,2,3,4,5};

	auto wrapped = WrapShardAuth(secret, body);
	A(wrapped.size() == kShardAuthTagSize + body.size(), "wrap = tag(32) + body");

	auto ok = UnwrapShardAuth(secret, wrapped.data(), wrapped.size());
	A(ok.has_value(), "unwrap avec bon secret réussit");
	A(ok && ok->second == body.size(), "body de bonne taille");
	A(ok && std::equal(body.begin(), body.end(), ok->first), "body identique");

	auto bad = UnwrapShardAuth("mauvais-secret", wrapped.data(), wrapped.size());
	A(!bad.has_value(), "unwrap avec mauvais secret rejeté");

	std::vector<uint8_t> tampered = wrapped; tampered.back() ^= 0xFF;
	A(!UnwrapShardAuth(secret, tampered.data(), tampered.size()).has_value(), "body falsifié rejeté");

	A(WrapShardAuth("", body).empty(), "wrap avec secret vide -> vide");
	A(!UnwrapShardAuth(secret, wrapped.data(), kShardAuthTagSize - 1).has_value(), "trop court rejeté");

	if (fails) { std::cerr << fails << " FAIL\n"; return 1; }
	std::cout << "OK\n"; return 0;
}
```

- [ ] **Step 2 : Vérifier l'échec (CI)**

`ShardWireAuth.h` n'existe pas → la cible ne compile pas / le test échoue.

- [ ] **Step 3 : Implémenter `ShardWireAuth`**

`src/shared/network/ShardWireAuth.h` :

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::network
{
	/// Taille du tag d'authentification HMAC-SHA256 préfixé aux payloads shard↔master.
	constexpr size_t kShardAuthTagSize = 32u;

	/// Préfixe un tag HMAC-SHA256(secret, body) de 32 octets à \a body.
	/// Renvoie tag||body, ou {} si \a secret est vide ou en cas d'erreur OpenSSL.
	std::vector<uint8_t> WrapShardAuth(std::string_view secret, const std::vector<uint8_t>& body);

	/// Vérifie le tag préfixe (comparaison à temps constant) sur les octets restants.
	/// Renvoie {pointeur_body, taille_body} après le tag, ou nullopt si trop court,
	/// tag invalide, ou secret vide.
	std::optional<std::pair<const uint8_t*, size_t>> UnwrapShardAuth(
		std::string_view secret, const uint8_t* payload, size_t size);
}
```

`src/shared/network/ShardWireAuth.cpp` :

```cpp
#include "src/shared/network/ShardWireAuth.h"

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>

#include <cstring>

namespace engine::network
{
	namespace
	{
		bool ComputeTag(std::string_view secret, const uint8_t* body, size_t bodySize, uint8_t* outTag)
		{
			unsigned int len = 0;
			unsigned char* r = HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
				body, bodySize, outTag, &len);
			return r != nullptr && len == kShardAuthTagSize;
		}
	}

	std::vector<uint8_t> WrapShardAuth(std::string_view secret, const std::vector<uint8_t>& body)
	{
		if (secret.empty())
			return {};
		std::vector<uint8_t> out(kShardAuthTagSize + body.size(), 0u);
		if (!ComputeTag(secret, body.data(), body.size(), out.data()))
			return {};
		std::memcpy(out.data() + kShardAuthTagSize, body.data(), body.size());
		return out;
	}

	std::optional<std::pair<const uint8_t*, size_t>> UnwrapShardAuth(
		std::string_view secret, const uint8_t* payload, size_t size)
	{
		if (secret.empty() || payload == nullptr || size < kShardAuthTagSize)
			return std::nullopt;
		const uint8_t* body = payload + kShardAuthTagSize;
		const size_t bodySize = size - kShardAuthTagSize;
		uint8_t expected[kShardAuthTagSize];
		if (!ComputeTag(secret, body, bodySize, expected))
			return std::nullopt;
		// Comparaison à temps constant (corrige aussi F27 pour ce chemin).
		if (CRYPTO_memcmp(expected, payload, kShardAuthTagSize) != 0)
			return std::nullopt;
		return std::make_pair(body, bodySize);
	}
}
```

Dans `src/CMakeLists.txt`, ajouter `src/shared/network/ShardWireAuth.cpp` à la même liste de sources que `ShardPayloads.cpp` (lib réseau partagée) **et** à la liste `server_app` (`grep -n "ShardPayloads.cpp" src/CMakeLists.txt` pour trouver les emplacements). Enregistrer `ShardWireAuthTests.cpp` comme cible de test à côté de `ShardPayloadsTests`.

- [ ] **Step 4 : Vérifier le passage (CI)**

`ctest` sur `ShardWireAuthTests` doit **passer** (tous les `Assert` verts).

- [ ] **Step 5 : Côté shard — joindre le tag à l'émission**

Dans `src/shared/network/ShardToMasterClient.h`, ajouter un membre et un setter :

```cpp
	void SetSharedSecret(std::string secret);
	// ...
	std::string m_shared_secret;
```

Dans `src/shared/network/ShardToMasterClient.cpp` : `#include "src/shared/network/ShardWireAuth.h"`, définir le setter, et envelopper les payloads dans `SendRegister` et `SendHeartbeat` **avant** de les écrire dans le `PacketBuilder`.

```cpp
	void ShardToMasterClient::SetSharedSecret(std::string secret) { m_shared_secret = std::move(secret); }
```

Dans `SendRegister`, après `auto payload = BuildShardRegisterPayload(...)` et le test `payload.empty()` :

```cpp
		// Sécurité (audit F3) : authentifier le REGISTER par tag HMAC préfixe.
		payload = WrapShardAuth(m_shared_secret, payload);
		if (payload.empty())
		{
			LOG_ERROR(Core, "[ShardToMasterClient] WrapShardAuth register échoué (secret vide ?)");
			return;
		}
```

(La suite écrit déjà `payload` dans le `PacketBuilder` — inchangée.) Idem dans `SendHeartbeat`, après `auto payload = BuildShardHeartbeatPayload(...)` et le test `payload.empty()` :

```cpp
		payload = WrapShardAuth(m_shared_secret, payload); // audit F3
		if (payload.empty())
			return;
```

- [ ] **Step 6 : Côté master — vérifier et retirer le tag avant parse**

Dans `src/masterd/handlers/shard/ShardRegisterHandler.h`, ajouter `void SetSecret(std::string secret);` et un membre `std::string m_secret;`.

Dans `src/masterd/handlers/shard/ShardRegisterHandler.cpp` : `#include "src/shared/network/ShardWireAuth.h"`, définir le setter, et dans `HandlePacket`, vérifier le tag **avant** de dispatcher :

```cpp
	void ShardRegisterHandler::SetSecret(std::string secret) { m_secret = std::move(secret); }

	void ShardRegisterHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t /*sessionIdHeader*/,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (!m_server || !m_registry)
		{
			LOG_WARN(Core, "[ShardRegisterHandler] HandlePacket: server or registry not set");
			return;
		}
		if (opcode == kOpcodeShardRegister || opcode == kOpcodeShardHeartbeat)
		{
			// Sécurité (audit F3) : le canal shard↔master est authentifié par un tag HMAC préfixe.
			auto body = UnwrapShardAuth(m_secret, payload, payloadSize);
			if (!body)
			{
				LOG_WARN(Server, "[SREG] paquet shard rejeté : authentification HMAC invalide (opcode={}, connId={})", opcode, connId);
				return;
			}
			if (opcode == kOpcodeShardRegister)
				HandleRegister(connId, requestId, body->first, body->second);
			else
				HandleHeartbeat(connId, body->first, body->second);
		}
	}
```

- [ ] **Step 7 : Câbler le secret dans les deux `main`**

Master — `src/masterd/main_linux.cpp`, près de la ligne 1051 (`shardRegisterHandler.SetServer(&server);`), ajouter :

```cpp
	shardRegisterHandler.SetSecret(config.GetString("shard.ticket_hmac_secret", ""));
```

Shard — `src/shardd/main_linux.cpp`, là où le `ShardToMasterClient` est configuré (`grep -n "ShardToMasterClient\|SetShardIdentity\|SetMasterAddress" src/shardd/main_linux.cpp`), ajouter, à côté des autres setters :

```cpp
	shardToMaster.SetSharedSecret(config.GetString("shard.ticket_hmac_secret", ""));
```

(adapter le nom de l'instance à celui réellement utilisé).

- [ ] **Step 8 : Vérifier (CI)**

Compilation master + shard verte ; `ShardWireAuthTests` vert. Le round-trip protocole est couvert par le test unitaire (Step 1) ; l'intégration réelle est validée en déploiement lock-step.

- [ ] **Step 9 : Commit**

```bash
git add src/shared/network/ShardWireAuth.h src/shared/network/ShardWireAuth.cpp src/shared/network/ShardWireAuthTests.cpp \
        src/shared/network/ShardToMasterClient.h src/shared/network/ShardToMasterClient.cpp \
        src/masterd/handlers/shard/ShardRegisterHandler.h src/masterd/handlers/shard/ShardRegisterHandler.cpp \
        src/masterd/main_linux.cpp src/shardd/main_linux.cpp src/CMakeLists.txt
git commit -m "fix(security): F3 — authentifier SHARD_REGISTER/HEARTBEAT par tag HMAC (wire-breaking)"
```

---

## Task 7 : F6 — refuser le secret HMAC par défaut au boot

**Files:**
- Create: `src/shared/security/SharedSecretPolicy.h`, `src/shared/security/SharedSecretPolicy.cpp`
- Create: `src/shared/security/SharedSecretPolicyTests.cpp`
- Modify: `src/masterd/main_linux.cpp`, `src/shardd/main_linux.cpp`
- Modify: `deploy/docker/config/master.config.json:10`, `deploy/docker/config/shard.config.json:9`, `deploy/docker/server-config/config.json`
- Modify: `src/CMakeLists.txt` (ajouter `SharedSecretPolicy.cpp` à la lib partagée **et** `server_app`)

**Interfaces:**
- Produces:
  - `bool engine::security::IsWeakSharedSecret(std::string_view secret);` — true si vide ou valeur de dev connue.
  - `bool engine::security::DevSecretOverrideEnabled();` — true si `LCDLLN_ALLOW_DEV_SECRET == "1"`.

- [ ] **Step 1 : Écrire le test qui échoue**

Créer `src/shared/security/SharedSecretPolicyTests.cpp` (modèle standalone, `int main()`), enregistré dans le `CMakeLists.txt` des tests de `src/shared/security` :

```cpp
#include "src/shared/security/SharedSecretPolicy.h"
#include <iostream>
using engine::security::IsWeakSharedSecret;
int main()
{
	int fails = 0;
	auto A = [&](bool c, const char* m){ if(!c){ ++fails; std::cerr << "[FAIL] " << m << "\n"; } };
	A(IsWeakSharedSecret("") == true, "vide = faible");
	A(IsWeakSharedSecret("dev_secret_change_in_production") == true, "valeur dev connue = faible");
	A(IsWeakSharedSecret("un-vrai-secret-aleatoire-long-9f3a") == false, "secret fort = OK");
	if (fails) return 1;
	std::cout << "OK\n"; return 0;
}
```

- [ ] **Step 2 : Vérifier l'échec (CI)**

`SharedSecretPolicy.h` absent → ne compile pas.

- [ ] **Step 3 : Implémenter le helper**

`src/shared/security/SharedSecretPolicy.h` :

```cpp
#pragma once
#include <string_view>

namespace engine::security
{
	/// Vrai si le secret partagé est vide ou égal à une valeur de développement connue
	/// (committée dans le repo) — donc impropre à la production.
	bool IsWeakSharedSecret(std::string_view secret);

	/// Vrai si l'opérateur a explicitement autorisé un secret de dev via
	/// la variable d'environnement LCDLLN_ALLOW_DEV_SECRET=1.
	bool DevSecretOverrideEnabled();
}
```

`src/shared/security/SharedSecretPolicy.cpp` :

```cpp
#include "src/shared/security/SharedSecretPolicy.h"

#include <array>
#include <cstdlib>
#include <string_view>

namespace engine::security
{
	bool IsWeakSharedSecret(std::string_view secret)
	{
		if (secret.empty())
			return true;
		static constexpr std::array<std::string_view, 2> kKnownDevSecrets = {
			"dev_secret_change_in_production",
			"changeme",
		};
		for (std::string_view weak : kKnownDevSecrets)
			if (secret == weak)
				return true;
		return false;
	}

	bool DevSecretOverrideEnabled()
	{
		const char* v = std::getenv("LCDLLN_ALLOW_DEV_SECRET");
		return v != nullptr && std::string_view(v) == "1";
	}
}
```

Ajouter `src/shared/security/SharedSecretPolicy.cpp` à `src/CMakeLists.txt` (lib partagée **et** `server_app`).

- [ ] **Step 4 : Vérifier le passage (CI)**

`ctest` sur `SharedSecretPolicyTests` vert.

- [ ] **Step 5 : Garde au boot (master + shard)**

Master — `src/masterd/main_linux.cpp`, juste après la lecture du secret (près de la ligne 1059, mais idéalement **avant** l'ouverture du port ; placer tôt dans `main`, après le chargement de `config`). Ajouter `#include "src/shared/security/SharedSecretPolicy.h"` et :

```cpp
	{
		const std::string shardSecret = config.GetString("shard.ticket_hmac_secret", "");
		if (engine::security::IsWeakSharedSecret(shardSecret) && !engine::security::DevSecretOverrideEnabled())
		{
			LOG_ERROR(Core, "[Boot] shard.ticket_hmac_secret vide ou par défaut : refus de démarrer. "
				"Définir un vrai secret, ou poser LCDLLN_ALLOW_DEV_SECRET=1 en développement.");
			return 1;
		}
	}
```

Shard — `src/shardd/main_linux.cpp`, même garde après le chargement de `config`, avant de lancer le `ShardToMasterClient`. Adapter le type de retour de `main` (renvoyer un code non-zéro).

- [ ] **Step 6 : Retirer la valeur faible des configs livrées**

Dans les 3 fichiers, remplacer `"dev_secret_change_in_production"` par `""` :
- `deploy/docker/config/master.config.json:10`
- `deploy/docker/config/shard.config.json:9`
- `deploy/docker/server-config/config.json` (ligne du `ticket_hmac_secret`)

Ainsi la config livrée **force** l'opérateur à renseigner un vrai secret (ou poser le flag dev).

- [ ] **Step 7 : Vérifier (CI + revue)**

Compilation verte ; les 3 configs ne contiennent plus la valeur faible ; le helper est testé. Revue : la garde est placée **avant** l'ouverture du port réseau.

- [ ] **Step 8 : Commit**

```bash
git add src/shared/security/SharedSecretPolicy.h src/shared/security/SharedSecretPolicy.cpp src/shared/security/SharedSecretPolicyTests.cpp \
        src/masterd/main_linux.cpp src/shardd/main_linux.cpp src/CMakeLists.txt \
        deploy/docker/config/master.config.json deploy/docker/config/shard.config.json deploy/docker/server-config/config.json
git commit -m "fix(security): F6 — refus au boot d'un secret HMAC faible/par défaut (sauf flag dev)"
```

---

## Task 8 : F1 (+F10) — synchroniser les migrations Docker + garde CI

**Files:**
- Modify: `deploy/docker/sql/migrations/` (resync depuis `sql/migrations/`)
- Modify/Create: garde CI dans `.github/workflows/` (ex. `build-linux.yml` ou un step dédié)

**Interfaces:** aucune (deploy/CI).

- [ ] **Step 1 : Resynchroniser le répertoire**

Exécuter le script de sync existant :

```bash
bash scripts/sync-db-to-docker-deploy.sh
```

Vérifier qu'il copie bien 0050–0071 et remplace la copie obsolète de 0043 (F10). S'il ne couvre pas tout, compléter par une copie explicite :

```bash
cp -f sql/migrations/*.sql deploy/docker/sql/migrations/
git status deploy/docker/sql/migrations/   # doit montrer 0050-0071 ajoutés + 0043 modifié
```

- [ ] **Step 2 : Vérifier la parité**

```bash
diff <(ls sql/migrations) <(ls deploy/docker/sql/migrations) && echo "IDENTIQUE"
```
Attendu : `IDENTIQUE` (mêmes noms de fichiers). Puis vérifier les contenus :
```bash
for f in sql/migrations/*.sql; do b=$(basename "$f"); diff -q "$f" "deploy/docker/sql/migrations/$b" || echo "DIVERGE: $b"; done
```
Attendu : aucune ligne `DIVERGE`.

- [ ] **Step 3 : Ajouter la garde CI**

Dans `.github/workflows/build-linux.yml` (ou un workflow dédié), ajouter un step **avant** le build qui échoue si les deux répertoires divergent :

```yaml
      - name: Vérifier la parité des migrations Docker (audit F1)
        run: |
          set -e
          miss=0
          for f in sql/migrations/*.sql; do
            b=$(basename "$f")
            if ! diff -q "$f" "deploy/docker/sql/migrations/$b" >/dev/null 2>&1; then
              echo "::error::Migration désynchronisée: $b (lancez scripts/sync-db-to-docker-deploy.sh)"
              miss=1
            fi
          done
          exit $miss
```

- [ ] **Step 4 : Vérifier (revue)**

Le step CI échouerait sur toute future divergence. Sur cette branche, après resync, il passe.

- [ ] **Step 5 : Commit**

```bash
git add deploy/docker/sql/migrations/ .github/workflows/
git commit -m "fix(deploy): F1/F10 — resync des migrations Docker + garde CI anti-divergence"
```

---

## Task 9 : F7 — marquer différé dans le rapport d'audit

**Files:**
- Modify: `docs/audit/2026-07-03-audit-complet-frais.md` (section F7)

**Interfaces:** aucune.

- [ ] **Step 1 : Annoter F7**

Dans la section F7 du rapport, ajouter une note de statut :

```markdown
> **Statut (2026-07-03)** : DIFFÉRÉ. Passer `allow_insecure_dev` à `false` sans
> `client.server_fingerprint` renseigné bloquerait tous les clients
> (`TlsVerifyFingerprint` refuse la connexion sur mismatch). Fermeture : fournir
> le SHA-256 du certificat du master de production dans `client.server_fingerprint`,
> puis passer `allow_insecure_dev` à `false` (tous les call sites +
> `BuildUserSettingsJson` + `config.json`).
```

- [ ] **Step 2 : Commit**

```bash
git add docs/audit/2026-07-03-audit-complet-frais.md
git commit -m "docs(audit): F7 marqué différé (nécessite le fingerprint du cert master)"
```

---

## Self-Review

- **Couverture spec** : F9 (Task 1), F5 (Task 2), F8 (Task 3), F2 (Task 4), F4 (Task 5), F3 (Task 6), F6 (Task 7), F1/F10 (Task 8), F7 différé documenté (Task 9). Toutes les corrections du spec sont couvertes ✅. Décisions validées respectées : F7 différé, F6 fail-fast + retrait valeur faible, F3 réutilise `shard.ticket_hmac_secret`, une branche server-first ✅.
- **Placeholders** : les steps de code portent du code concret ; les rares « adapter au type de retour réel » ciblent des signatures que l'implémenteur lit sur place (SmtpMailer::Send, VerifyHttp, GetShardConnection) — pas des TODO de logique.
- **Cohérence des types** : `WrapShardAuth`/`UnwrapShardAuth`/`kShardAuthTagSize` définis Task 6 step 3, consommés steps 5-6 ; `SetSharedSecret`/`SetSecret` définis et câblés Task 6 ; `IsWeakSharedSecret`/`DevSecretOverrideEnabled` définis Task 7 step 3, consommés step 5. `HandleHeartbeat(connId, …)` : signature changée en Task 4, re-touchée en Task 6 step 6 (l'appel passe `body->first/second`) — cohérent (Task 4 puis Task 6 dans l'ordre). Note d'ordre : **Task 4 avant Task 6** (Task 6 s'appuie sur la signature `connId` nommée de Task 4).
- **CMake** : `ShardWireAuth.cpp` et `SharedSecretPolicy.cpp` ajoutés à la lib partagée **et** à `server_app` (contrainte connue).
- **Déploiement** : ⚠️ **redéploiement lock-step master+shard** (F3 wire-breaking + F6 boot). Action opérateur : définir un vrai `shard.ticket_hmac_secret` identique des deux côtés avant démarrage (sinon boot en échec), ou `LCDLLN_ALLOW_DEV_SECRET=1` en dev. F1 = rejouer le build de l'image master.
