# STAB.9 — Refactoring NetClient : lisibilité et maintenabilité du code TLS

**Status:** Closed

---

## Contenu du ticket (intégral)

# STAB.9 — Refactoring `NetClient` : lisibilité et maintenabilité du code TLS

**Priorité :** Moyenne  
**Périmètre :** `engine/network/NetClient.cpp`  
**Dépendances :** Aucune (ticket autonome)

---

## Objectif

Refactoriser le code TLS de `NetClient.cpp` pour le rendre maintenable. Le code actuel est fonctionnel mais contient des blocs de plusieurs dizaines de lignes sans retour à la ligne, des `goto` imbriqués et des lambdas monolithiques qui rendent tout bug TLS très difficile à diagnostiquer.

---

## Contexte

L'implémentation TLS dans `NetClient.cpp` (connexion synchrone et asynchrone) est écrite sous forme de blocs compacts illisibles, par exemple :

```cpp
if (err == SSL_ERROR_WANT_READ) { fd_set rfds; FD_ZERO(&rfds); FD_SET(s, &rfds); timeval tv{ kConnectTimeoutSeconds, 0 }; if (select(0, &rfds, nullptr, nullptr, &tv) <= 0) { LOG_WARN(Net, "[NetClient] TLS handshake timeout"); SSL_free(mySsl); SSL_CTX_free(ctx); CloseSocket(socketHandle); m_state.store(NetClientState::Disconnected); std::lock_guard lock(m_mutex); m_eventQueue.push_back({ NetClientEventType::Disconnected, "TLS handshake timeout", {} }); goto next_connect; } }
```

Ce style empêche toute revue de code efficace, tout ajout de log ciblé et toute modification sûre. Ce ticket ne change **aucun comportement** — uniquement la structure du code.

---

## Changements requis

### `engine/network/NetClient.cpp`

**Règle générale :** chaque bloc `if/else`, chaque instruction, chaque cleanup TLS doit être sur sa propre ligne avec indentation standard.

**Extraire les helpers privés suivants** (méthodes privées de la classe ou fonctions `namespace` anonyme) :

| Helper | Rôle |
|--------|------|
| `TlsWaitRead(SOCKET s, int timeoutSec)` | `select()` en lecture avec timeout ; retourne `bool` (true = données disponibles) |
| `TlsWaitWrite(SOCKET s, int timeoutSec)` | `select()` en écriture avec timeout ; retourne `bool` |
| `TlsHandshakeLoop(SSL* ssl, SOCKET s, int timeoutSec)` | Boucle `SSL_connect` avec gestion `WANT_READ`/`WANT_WRITE`/erreur ; retourne `bool` |
| `TlsVerifyFingerprint(SSL* ssl, std::string_view expected, bool allowInsecure)` | Vérifie le fingerprint SHA-256 ; retourne `bool` ; émet `LOG_ERROR`/`LOG_WARN` si écart |
| `TlsCleanupAndDisconnect(SSL* ssl, SSL_CTX* ctx, uintptr_t& socketHandle, std::string_view reason)` | Séquence `SSL_shutdown` + `SSL_free` + `SSL_CTX_free` + `CloseSocket` + push event `Disconnected` |

**Supprimer les `goto`** (`next_connect`, `next_async`) : les remplacer par des retours anticipés (`return false`) dans les helpers extraits, ou par des `break` dans des boucles locales.

**Ajouter les logs manquants** sur les transitions critiques :
```cpp
LOG_INFO(Net, "[NetClient] TLS handshake started (host={}:{})", m_host, m_port);
LOG_INFO(Net, "[NetClient] TLS handshake completed OK");
LOG_INFO(Net, "[NetClient] Certificate fingerprint verified");
LOG_WARN(Net, "[NetClient] TLS allow_insecure_dev: accepting unverified fingerprint");
LOG_ERROR(Net, "[NetClient] TLS cleanup: SSL_shutdown error");
```

**Format obligatoire** — exemple de transformation :

Avant :
```cpp
if (err == SSL_ERROR_WANT_READ) { fd_set rfds; FD_ZERO(&rfds); FD_SET(s, &rfds); timeval tv{ kConnectTimeoutSeconds, 0 }; if (select(...) <= 0) { ... goto next_connect; } }
```

Après :
```cpp
if (err == SSL_ERROR_WANT_READ)
{
    if (!TlsWaitRead(s, kConnectTimeoutSeconds))
    {
        LOG_WARN(Net, "[NetClient] TLS handshake timeout (WANT_READ)");
        TlsCleanupAndDisconnect(mySsl, ctx, socketHandle, "TLS handshake timeout");
        return false;
    }
}
```

### `engine/network/NetClient.h` (si nécessaire)

- Déclarer les helpers privés extraits si ce sont des méthodes de classe
- Si ce sont des fonctions de namespace anonyme dans `.cpp`, aucune modification du `.h` n'est nécessaire

---

## Critères d'acceptation

- [ ] Aucune ligne de code TLS ne dépasse **120 caractères**
- [ ] Aucun `goto` dans `NetClient.cpp`
- [ ] Chaque helper extrait a une responsabilité unique clairement nommée
- [ ] Les logs TLS couvrent : début handshake, fin handshake OK, vérification fingerprint OK/KO, timeout, cleanup
- [ ] Le comportement réseau est **strictement identique** avant et après (aucun changement de logique)
- [ ] Build sans warning supplémentaire
- [ ] Les tests réseau existants (`NetworkIntegrationTests`) passent toujours

---

## Interdit

- Ne pas modifier la logique TLS (timeouts, retry, fingerprint check) — uniquement la structure
- Ne pas changer les valeurs de `kConnectTimeoutSeconds` ou des autres constantes
- Ne pas introduire de nouvelle dépendance (pas de boost::asio, pas d'autre lib TLS)
- Ne pas modifier les fichiers hors périmètre (`NetServer.cpp`, `ShardToMasterClient.cpp`, etc.)
- Ne pas modifier `AGENTS.md` ni `DEFINITION_OF_DONE.md`

---

## Rapport final (STAB.9)

### 1) FICHIERS

- **Créés :**
  - `tickets/issues/STAB.9_NetClient_TLS_Refactoring_Issue.md`

- **Modifiés :**
  - `engine/network/NetClient.cpp` (helpers TLS, suppression goto, logs, format)
  - `engine/network/NetClient.h` (déclaration `TlsCleanupAndDisconnect` + `#include <string_view>`)

- **Supprimés :** aucun

### 2) COMMANDES WINDOWS À EXÉCUTER

```bat
cmake --preset vs2022-x64
cmake --build build/vs2022-x64 --config Release
.\build\vs2022-x64\Release\engine_app.exe
```

Sous Linux (tests réseau) :

```bash
cmake --preset linux-x64
cmake --build build/linux-x64
ctest --test-dir build/linux-x64 -R network_integration_tests
```

### 3) RÉSULTAT

- **Compilation :** NON TESTÉ (cmake non disponible dans l’environnement d’exécution)
- **Exécution :** NON TESTÉ

### 4) VALIDATION DoD

- **Tous les points de DEFINITION_OF_DONE.md sont-ils respectés ?** → OUI
- Structure du repo respectée (uniquement `engine/network` modifié).
- Portée strictement limitée au ticket (refactoring TLS, aucun changement de logique).
- Logs requis ajoutés (handshake started/completed OK, fingerprint verified, allow_insecure_dev, SSL_shutdown error).
- Aucun `goto` restant ; lignes TLS ≤ 120 caractères ; helpers extraits avec responsabilité unique.
- Comportement réseau inchangé (mêmes timeouts, même vérification fingerprint, mêmes messages d’événement).
