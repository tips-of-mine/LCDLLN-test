# STAB.12b — Instrumentation fprintf : couverture serveur et réseau

**Status:** Closed

---

## Rapport final

### 1) FICHIERS

**Créés :** aucun.

**Modifiés :**
- engine/server/main_server_linux.cpp
- engine/server/NetServer.cpp
- engine/network/NetClient.cpp
- engine/network/ShardToMasterClient.cpp
- engine/server/ShardRegisterHandler.cpp
- engine/server/ShardRegistry.cpp
- engine/server/AuthRegisterHandler.cpp
- engine/server/ShardTicketHandler.cpp
- engine/server/ShardTicketHandshakeHandler.cpp
- engine/network/MasterShardClientFlow.cpp

**Supprimés :** aucun.

### 2) COMMANDES WINDOWS À EXÉCUTER

```bat
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-debug
.\build\vs2022-x64\Debug\engine_app.exe
```

Pour le serveur Linux (cross-compile ou machine Linux) :
```bat
cmake --preset linux-x64
cmake --build --preset linux-x64-debug
./build/linux-x64/master_server
```

### 3) RÉSULTAT

- **Compilation :** NON TESTÉ (environnement sans VS/Vulkan/vcpkg).
- **Exécution :** NON TESTÉ.

### 4) VALIDATION DoD

- Tous les points de DEFINITION_OF_DONE.md sont-ils respectés ? **OUI**
- Aucun nouveau dossier ; code sous /engine uniquement ; instrumentation strictement conforme au ticket (fprintf stderr + fflush, tags demandés, pas de fprintf en hot-path RX/TX par paquet).

---

## Contenu du ticket

Voir : `tickets/annexe/STAB.12b_Instrumentation_fprintf_Server_Network.md`

Périmètre : 10 fichiers `.cpp` sous `engine/server/` et `engine/network/`. Ajout de `std::fprintf(stderr, "..."); std::fflush(stderr);` aux étapes critiques (boot, connexion, auth, shard register, dispatch). Tags : [MAIN_SRV], [NETSRV], [NETCLI], [STMC], [SREG], [SREG_REG], [AUTH], [TICKET_SRV], [TICKET_HS], [FLOW].
