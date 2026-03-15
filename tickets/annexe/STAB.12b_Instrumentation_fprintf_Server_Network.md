# STAB.12b — Instrumentation `fprintf` : couverture serveur et réseau

**Priorité :** Haute  
**Périmètre :** 10 fichiers `.cpp` sous `engine/server/` et `engine/network/`  
**Dépendances :** Aucune (complémentaire à STAB.12, indépendant)

---

## Objectif

Compléter la couverture d'instrumentation `fprintf` démarrée par STAB.12 pour la
partie **serveur Linux** et **réseau client Windows**. Chaque étape critique de
connexion, d'authentification, d'enregistrement de shard et de dispatch de paquets
doit produire une trace sur `stderr` avant et après son exécution.

**Règle universelle** : un `fprintf` **avant** et **après** chaque étape atomique.
Format : `[TAG] avant <action>` / `[TAG] <action> OK` ou `[TAG] <action> r=<valeur>`.
Toujours `std::fflush(stderr)` immédiatement après chaque `fprintf`.

---

## Fichiers et zones à instrumenter

---

### 1. `engine/server/main_server_linux.cpp`

**Tag : `[MAIN_SRV]`**

Séquence de boot du serveur Master :

```cpp
std::fprintf(stderr, "[MAIN_SRV] boot start\n"); std::fflush(stderr);

// avant config.Load :
std::fprintf(stderr, "[MAIN_SRV] avant config load\n"); std::fflush(stderr);
std::fprintf(stderr, "[MAIN_SRV] config OK port=%u\n", port); std::fflush(stderr);

// avant Log::Init :
std::fprintf(stderr, "[MAIN_SRV] avant Log::Init\n"); std::fflush(stderr);
std::fprintf(stderr, "[MAIN_SRV] Log::Init OK\n"); std::fflush(stderr);

// avant ShardRegistry init :
std::fprintf(stderr, "[MAIN_SRV] avant ShardRegistry setup\n"); std::fflush(stderr);
std::fprintf(stderr, "[MAIN_SRV] ShardRegistry setup OK\n"); std::fflush(stderr);

// avant NetServer::Init :
std::fprintf(stderr, "[MAIN_SRV] avant NetServer::Init port=%u\n", port); std::fflush(stderr);
// après :
std::fprintf(stderr, "[MAIN_SRV] NetServer::Init r=%d\n", (int)initOk); std::fflush(stderr);

// avant SetPacketHandler :
std::fprintf(stderr, "[MAIN_SRV] avant SetPacketHandler\n"); std::fflush(stderr);
std::fprintf(stderr, "[MAIN_SRV] SetPacketHandler OK\n"); std::fflush(stderr);

// entrée boucle principale :
std::fprintf(stderr, "[MAIN_SRV] entering main loop\n"); std::fflush(stderr);

// sortie boucle :
std::fprintf(stderr, "[MAIN_SRV] main loop exited, avant Shutdown\n"); std::fflush(stderr);

// après NetServer::Shutdown :
std::fprintf(stderr, "[MAIN_SRV] NetServer::Shutdown OK\n"); std::fflush(stderr);

// fin de main :
std::fprintf(stderr, "[MAIN_SRV] shutdown complete\n"); std::fflush(stderr);
```

Dans la boucle principale, watchdog + stats dump :
```cpp
// avant EvictStaleHeartbeats :
std::fprintf(stderr, "[MAIN_SRV] EvictStaleHeartbeats timeout=%d\n", shardHeartbeatTimeoutSec); std::fflush(stderr);
```

---

### 2. `engine/server/NetServer.cpp` (Linux uniquement — bloc `#if defined(__linux__)`)

**Tag : `[NETSRV]`**

Dans `NetServer::Init` :
```cpp
std::fprintf(stderr, "[NETSRV] Init enter port=%u\n", listenPort); std::fflush(stderr);
// après socket() :
std::fprintf(stderr, "[NETSRV] socket() fd=%d\n", m_impl->listenFd); std::fflush(stderr);
// après bind() :
std::fprintf(stderr, "[NETSRV] bind() OK\n"); std::fflush(stderr);
// après listen() :
std::fprintf(stderr, "[NETSRV] listen() OK\n"); std::fflush(stderr);
// après epoll_create1 :
std::fprintf(stderr, "[NETSRV] epoll_create1 epollFd=%d\n", m_impl->epollFd); std::fflush(stderr);
// après SSL_CTX_new (si TLS) :
std::fprintf(stderr, "[NETSRV] SSL_CTX_new OK tls=1\n"); std::fflush(stderr);
// après démarrage threads :
std::fprintf(stderr, "[NETSRV] Init OK workers=%u ioThread started\n", config.workerThreadCount); std::fflush(stderr);
```

Dans `NetServer::Shutdown` :
```cpp
std::fprintf(stderr, "[NETSRV] Shutdown enter\n"); std::fflush(stderr);
// après running.store(false) :
std::fprintf(stderr, "[NETSRV] running=false, avant ioThread.join\n"); std::fflush(stderr);
// après ioThread.join :
std::fprintf(stderr, "[NETSRV] ioThread joined\n"); std::fflush(stderr);
// après workers join :
std::fprintf(stderr, "[NETSRV] workers joined count=%zu\n", m_impl->workers.size()); std::fflush(stderr);
// après SSL_CTX_free :
std::fprintf(stderr, "[NETSRV] SSL_CTX_free OK\n"); std::fflush(stderr);
// fin :
std::fprintf(stderr, "[NETSRV] Shutdown OK\n"); std::fflush(stderr);
```

Dans `Impl::IoThreadRun` — transitions importantes uniquement (pas chaque paquet) :
```cpp
// au démarrage du thread :
std::fprintf(stderr, "[NETSRV] IoThread started\n"); std::fflush(stderr);
// à chaque accept() réussi :
std::fprintf(stderr, "[NETSRV] accept connId=%u fd=%d\n", newConnId, clientFd); std::fflush(stderr);
// à chaque CloseConnection :
std::fprintf(stderr, "[NETSRV] CloseConnection connId=%u reason=%s\n", connId, DisconnectReasonString(reason)); std::fflush(stderr);
// sortie de la boucle :
std::fprintf(stderr, "[NETSRV] IoThread exiting\n"); std::fflush(stderr);
```

Dans `Impl::WorkerThreadRun` :
```cpp
// au démarrage du thread :
std::fprintf(stderr, "[NETSRV] WorkerThread started\n"); std::fflush(stderr);
// sortie :
std::fprintf(stderr, "[NETSRV] WorkerThread exiting\n"); std::fflush(stderr);
```

---

### 3. `engine/network/NetClient.cpp` (Windows — `NetworkThreadRun`)

**Tag : `[NETCLI]`**

Dans `NetworkThreadRun` :
```cpp
// au démarrage du thread :
std::fprintf(stderr, "[NETCLI] NetworkThread started\n"); std::fflush(stderr);
// après WSAStartup :
std::fprintf(stderr, "[NETCLI] WSAStartup OK\n"); std::fflush(stderr);
// au moment de la tentative de connexion :
std::fprintf(stderr, "[NETCLI] avant connect host=%s port=%u\n", host.c_str(), (unsigned)port); std::fflush(stderr);
// après connect() réussi (socket connecté) :
std::fprintf(stderr, "[NETCLI] TCP connect OK\n"); std::fflush(stderr);
// au début du handshake TLS :
std::fprintf(stderr, "[NETCLI] avant TLS handshake\n"); std::fflush(stderr);
// après handshake TLS réussi :
std::fprintf(stderr, "[NETCLI] TLS handshake OK fingerprint_check=%d\n", (int)!m_expectedServerFingerprintHex.empty()); std::fflush(stderr);
// à chaque déconnexion :
std::fprintf(stderr, "[NETCLI] disconnect reason='%s'\n", disconnectReason.c_str()); std::fflush(stderr);
// sortie de la boucle :
std::fprintf(stderr, "[NETCLI] NetworkThread exiting\n"); std::fflush(stderr);
```

Dans `NetClient::Connect` :
```cpp
std::fprintf(stderr, "[NETCLI] Connect called host=%s port=%u\n", host.data(), (unsigned)port); std::fflush(stderr);
```

Dans `NetClient::Disconnect` :
```cpp
std::fprintf(stderr, "[NETCLI] Disconnect called reason='%s'\n", reason.c_str()); std::fflush(stderr);
```

---

### 4. `engine/network/ShardToMasterClient.cpp`

**Tag : `[STMC]`**

Dans `Start` :
```cpp
std::fprintf(stderr, "[STMC] Start host=%s port=%u\n", m_host.c_str(), (unsigned)m_port); std::fflush(stderr);
```

Dans `Pump` (uniquement les transitions d'état) :
```cpp
// à chaque changement d'état :
std::fprintf(stderr, "[STMC] Pump state=%d reconnect_in=%llds\n",
    (int)m_state,
    (long long)std::chrono::duration_cast<std::chrono::seconds>(m_reconnect_after - std::chrono::steady_clock::now()).count());
std::fflush(stderr);
```

Dans `OnConnected` :
```cpp
std::fprintf(stderr, "[STMC] OnConnected -> Registering\n"); std::fflush(stderr);
```

Dans `OnDisconnected` :
```cpp
std::fprintf(stderr, "[STMC] OnDisconnected reason='%s' next_backoff=%ds\n",
    std::string(reason).c_str(), m_reconnect_backoff_sec); std::fflush(stderr);
```

Dans `OnPacketReceived` — transitions d'état :
```cpp
// après Register OK :
std::fprintf(stderr, "[STMC] RegisterOK shard_id=%u\n", m_shard_id); std::fflush(stderr);
// après Register ERROR :
std::fprintf(stderr, "[STMC] RegisterERROR from Master\n"); std::fflush(stderr);
```

Dans `SendRegister` :
```cpp
std::fprintf(stderr, "[STMC] SendRegister name='%s' endpoint='%s' cap=%u\n",
    m_name.c_str(), m_endpoint.c_str(), m_max_capacity); std::fflush(stderr);
```

Dans `SendHeartbeat` :
```cpp
std::fprintf(stderr, "[STMC] SendHeartbeat shard_id=%u load=%u\n", m_shard_id, m_current_load); std::fflush(stderr);
```

Dans `ScheduleReconnect` :
```cpp
std::fprintf(stderr, "[STMC] ScheduleReconnect backoff=%ds\n", m_reconnect_backoff_sec); std::fflush(stderr);
```

Dans le destructeur :
```cpp
std::fprintf(stderr, "[STMC] destructor enter state=%d\n", (int)m_state); std::fflush(stderr);
std::fprintf(stderr, "[STMC] destructor OK\n"); std::fflush(stderr);
```

---

### 5. `engine/server/ShardRegisterHandler.cpp`

**Tag : `[SREG]`**

Dans `HandleRegister` :
```cpp
std::fprintf(stderr, "[SREG] HandleRegister connId=%u\n", connId); std::fflush(stderr);
// après RegisterShard :
std::fprintf(stderr, "[SREG] RegisterShard id=%u (0=duplicate)\n", id ? *id : 0u); std::fflush(stderr);
// après Send REGISTER_OK :
std::fprintf(stderr, "[SREG] REGISTER_OK sent connId=%u shard_id=%u\n", connId, *id); std::fflush(stderr);
// si erreur :
std::fprintf(stderr, "[SREG] REGISTER_ERROR sent connId=%u\n", connId); std::fflush(stderr);
```

Dans `HandleHeartbeat` :
```cpp
// uniquement si parsed == nullptr (erreur parse) :
std::fprintf(stderr, "[SREG] HandleHeartbeat: parse failed\n"); std::fflush(stderr);
```

---

### 6. `engine/server/ShardRegistry.cpp`

**Tag : `[SREG_REG]`** — complément aux logs `LOG_*` déjà présents :

Dans `RegisterShard` :
```cpp
std::fprintf(stderr, "[SREG_REG] RegisterShard name='%s' endpoint='%s' cap=%u\n",
    name.c_str(), endpoint.c_str(), max_capacity); std::fflush(stderr);
// fin (succès ou doublon) :
std::fprintf(stderr, "[SREG_REG] RegisterShard result id=%u\n", id); std::fflush(stderr);
```

Dans `UpdateHeartbeat` :
```cpp
// uniquement si transition d'état (became_online) :
std::fprintf(stderr, "[SREG_REG] UpdateHeartbeat shard_id=%u became_online=%d\n", shard_id, (int)became_online); std::fflush(stderr);
```

Dans `EvictStaleHeartbeats` :
```cpp
std::fprintf(stderr, "[SREG_REG] EvictStaleHeartbeats timeout=%ds marked=%zu\n",
    timeout_sec, marked.size()); std::fflush(stderr);
```

---

### 7. `engine/server/AuthRegisterHandler.cpp`

**Tag : `[AUTH]`**

Dans `HandleRegister` :
```cpp
std::fprintf(stderr, "[AUTH] HandleRegister connId=%u\n", connId); std::fflush(stderr);
// résultat :
std::fprintf(stderr, "[AUTH] HandleRegister result=%s\n", success ? "OK" : "FAIL"); std::fflush(stderr);
```

Dans `HandleAuth` :
```cpp
std::fprintf(stderr, "[AUTH] HandleAuth connId=%u\n", connId); std::fflush(stderr);
// après vérification hash :
std::fprintf(stderr, "[AUTH] HandleAuth hash_ok=%d\n", (int)hashOk); std::fflush(stderr);
// résultat final :
std::fprintf(stderr, "[AUTH] HandleAuth result=%s session_id=%llu\n",
    success ? "OK" : "FAIL", (unsigned long long)sessionId); std::fflush(stderr);
```

---

### 8. `engine/server/ShardTicketHandler.cpp`

**Tag : `[TICKET_SRV]`**

Dans `HandlePacket` (demande de ticket côté Master) :
```cpp
std::fprintf(stderr, "[TICKET_SRV] HandlePacket connId=%u opcode=%u\n", connId, opcode); std::fflush(stderr);
// après création du ticket :
std::fprintf(stderr, "[TICKET_SRV] ticket created account_id=%llu shard_id=%u\n",
    (unsigned long long)account_id, shard_id); std::fflush(stderr);
// si erreur :
std::fprintf(stderr, "[TICKET_SRV] ticket creation FAILED\n"); std::fflush(stderr);
```

---

### 9. `engine/server/ShardTicketHandshakeHandler.cpp`

**Tag : `[TICKET_HS]`**

Dans `HandlePacket` (validation du ticket côté Shard) :
```cpp
std::fprintf(stderr, "[TICKET_HS] HandlePacket connId=%u opcode=%u\n", connId, opcode); std::fflush(stderr);
// après VerifyAndConsume :
std::fprintf(stderr, "[TICKET_HS] VerifyAndConsume result=%s\n", accept ? "ACCEPTED" : "REJECTED"); std::fflush(stderr);
```

---

### 10. `engine/network/MasterShardClientFlow.cpp`

**Tag : `[FLOW]`**

Dans `Run` — chaque étape du flux vertical :
```cpp
std::fprintf(stderr, "[FLOW] Run start host=%s port=%u\n", m_masterHost.c_str(), (unsigned)m_masterPort); std::fflush(stderr);
// après WaitConnected :
std::fprintf(stderr, "[FLOW] Master connected=%d\n", (int)connected); std::fflush(stderr);
// avant AUTH :
std::fprintf(stderr, "[FLOW] avant AUTH login='%s'\n", m_login.c_str()); std::fflush(stderr);
// après AUTH :
std::fprintf(stderr, "[FLOW] AUTH result=%s account_id=%llu\n",
    authOk ? "OK" : "FAIL", (unsigned long long)accountId); std::fflush(stderr);
// avant SERVER_LIST :
std::fprintf(stderr, "[FLOW] avant SERVER_LIST\n"); std::fflush(stderr);
// après SERVER_LIST :
std::fprintf(stderr, "[FLOW] SERVER_LIST shards=%zu\n", shardCount); std::fflush(stderr);
// avant REQUEST_SHARD_TICKET :
std::fprintf(stderr, "[FLOW] avant SHARD_TICKET shard_id=%u\n", targetShardId); std::fflush(stderr);
// après ticket reçu :
std::fprintf(stderr, "[FLOW] SHARD_TICKET received len=%zu\n", ticketPayload.size()); std::fflush(stderr);
// avant connexion Shard :
std::fprintf(stderr, "[FLOW] avant Shard connect endpoint='%s'\n", shardEndpoint.c_str()); std::fflush(stderr);
// après PRESENT_SHARD_TICKET :
std::fprintf(stderr, "[FLOW] PRESENT_SHARD_TICKET result=%s\n", accepted ? "ACCEPTED" : "REJECTED"); std::fflush(stderr);
// résultat final :
std::fprintf(stderr, "[FLOW] Run end success=%d\n", (int)result.success); std::fflush(stderr);
```

---

## Règles communes (identiques à STAB.12)

1. **Toujours** `std::fflush(stderr)` immédiatement après chaque `std::fprintf`.
2. **Format** : `[TAG] avant <action>` → `[TAG] <action> r=<code>` ou `[TAG] <action> OK`.
3. **Ne pas ajouter** de `fprintf` dans les boucles hot-path (RX/TX par paquet) — uniquement sur les événements de connexion, déconnexion, et transitions d'état.
4. **Ne pas supprimer** les `fprintf` existants.
5. **Ne pas convertir** en `LOG_*` (c'est le rôle de STAB.8, différé).
6. `#include <cstdio>` doit être présent en tête de chaque fichier modifié s'il ne l'est pas déjà.

---

## Note sur les plateformes

- `NetServer.cpp` : Linux uniquement (`#if defined(__linux__)`). Les blocs `fprintf` sont dans le bloc Linux.
- `NetClient.cpp`, `ShardToMasterClient.cpp`, `MasterShardClientFlow.cpp` : Windows uniquement (`#if defined(_WIN32)`).
- `main_server_linux.cpp`, `ShardRegistry.cpp`, `ShardRegisterHandler.cpp`, `AuthRegisterHandler.cpp`, `ShardTicketHandler.cpp`, `ShardTicketHandshakeHandler.cpp` : Linux uniquement.

---

## Critères d'acceptation

- [ ] Les 10 fichiers listés contiennent des `fprintf` aux points identifiés
- [ ] Chaque `fprintf` est immédiatement suivi de `fflush(stderr)`
- [ ] Le boot du serveur Linux produit une trace de `[MAIN_SRV] boot start` jusqu'à `[MAIN_SRV] entering main loop`
- [ ] Un flux Shard → Master produit la trace : `[STMC] Start` → `[STMC] OnConnected` → `[STMC] SendRegister` → `[STMC] RegisterOK`
- [ ] Un flux client `MasterShardClientFlow::Run` produit les 8 étapes tracées
- [ ] Aucun `fprintf` dans les boucles hot-path RX/TX (un paquet = une écriture `fprintf` max par connexion, pas par paquet)
- [ ] Build sans warning supplémentaire

---

## Interdit

- Ne pas modifier la logique de code — uniquement ajouter des `fprintf`/`fflush`
- Ne pas ajouter de `fprintf` dans les boucles RX/TX hot-path de `NetServer::Impl::IoThreadRun`
- Ne pas supprimer les `fprintf` existants
- Ne pas convertir en `LOG_*`
- Ne pas modifier `AGENTS.md` ni `DEFINITION_OF_DONE.md`
