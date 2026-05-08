# CMANGOS.12 — Server (PacketLog / OpcodeRegistry / DBCStores)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.12_Server_packetlog_opcodetable_dbcstores.md](../../../../tickets/CMANGOS/CMANGOS.12_Server_packetlog_opcodetable_dbcstores.md)
> **Priorité** : P2 — gameplay essentiel (mais infra/QoL prod)
> **Cible** : cross (master + shard)

## 1. Statut implémentation

🟡 **Partiel** — un filtre logger `PacketIo` existe (PR #468) pour
tracer les paquets en texte, mais le **PacketLog binaire rejouable** est
absent, ainsi que l'`OpcodeRegistry` typé (les opcodes sont des
`constexpr uint16_t` simples).

## 2. Preuves dans le code

**Existant :**
- [engine/core/Log.h](../../../../engine/core/Log.h) + [engine/core/LogConfig.cpp](../../../../engine/core/LogConfig.cpp) — `LogFilter::PacketIo`
  (filtre bitmask pour activer/désactiver le log paquet — pattern
  texte, pas binaire rejouable)
- [engine/server/ServerProtocol.h](../../../../engine/server/ServerProtocol.h) — référence à `PacketIo` filter
- [engine/network/ProtocolV1Constants.h](../../../../engine/network/ProtocolV1Constants.h) — table d'opcodes en
  `constexpr uint16_t kOpcodeXxx = nn` (simple, pas de métadonnée
  status/processing/handler)

**Manquant (vs spec ticket) :**
- ❌ `PacketLog` binaire rejouable (`Open`/`Close`/`LogPacket`/`Replay`)
- ❌ `OpcodeRegistry` typé avec `OpcodeMeta` (code, name, status,
  processing)
- ❌ `OpcodeStatus` enum (Loaded/UnHandled/Active/Deprecated)
- ❌ `OpcodeProcessing` enum (InPlace/InMapTick/InMasterTick)
- ❌ Outil offline `tools/packet_replay/`
- ❌ Préchargement RAM-mapped équivalent à DBCStores (à clarifier vs
  zone_builder/StreamCache existants)
- ❌ Stats par opcode (`opcode_stats_enabled`)
- ❌ Config `server.packet_log_*`, `server.opcode_stats_enabled`

## 3. Recouvrement milestones existantes

❌ **Non couvert** (pour le PacketLog binaire et l'OpcodeRegistry typé).
Le filtre logger PR #468 couvre une partie du besoin (debug textuel),
mais pas le rejeu binaire offline.

## 4. Écart par rapport à la spec CMANGOS

L'écart est **modéré** mais à fort impact opérationnel :

1. **PacketLog rejouable** — outil de debug protocole prod. Quand un
   joueur signale "j'ai été disconnecté à 14h32", on lit le `.pktlog`
   binaire et on reproduit la séquence offline. Game-changer pour les
   bugs intermittents.
2. **OpcodeRegistry typé** — métadonnées centralisées + dispatch
   automatique. Élimine les `if (opcode == X) handler.handle()`
   éparpillés.
3. **DBCStores** — pattern moins critique pour LCDLLN car zone_builder
   + StreamCache + PakReader couvrent déjà le préchargement de données
   statiques côté shard.

## 5. Effort estimé

**M** (2-3 PR) :
- PR 1 : `PacketLog` binaire (Open/Close/LogPacket) + format
  versionné + tests round-trip
- PR 2 : outil `packet_replay` offline (utilise `Replay` API) + tests
  intégration
- PR 3 : `OpcodeRegistry` typé + migration opcodes existants vers
  enregistrement registry

Pas de wire-breaking (le format paquet ne change pas, on **observe**
ce qui passe). Pas de migration DB.

## 6. Valeur joueur/serveur

**Élevée (prod)** — invisible joueur mais critique pour la productivité
debug. Un seul incident "joueur reproduit pas le bug" amorti via
PacketLog.

OpcodeRegistry = nettoyage architecture, gain modéré court-terme,
gros gain à terme quand le nombre d'opcodes explose.

## 7. Dépendances bloquantes

- **CMANGOS.13 Database** — pour les vraies stores SQL (DBCStores
  équivalent). Mais `PacketLog` et `OpcodeRegistry` sont
  indépendants.
- **Logger PR #468** — déjà livré, OK.

## 8. Risque / piège ⚠️

- ⚠️ **Format `.pktlog` versioning** — magic number + version dès le
  début, sinon outils incompatibles entre versions du serveur.
- ⚠️ **Confidentialité** — un `.pktlog` peut contenir des messages chat
  privés, mots de passe, tokens si non-filtré. **Filtre PII obligatoire**
  avant export joueur (rgpd + sécurité).
- ⚠️ **Taille du log** — un shard actif peut générer Mo/sec en réseau.
  Rotation + cap `packet_log_max_size_mb=100` à respecter strictement.
- ⚠️ **Performance** — écriture disque synchrone tue les perfs. Async +
  ring buffer obligatoire.
- ⚠️ **OpcodeRegistry runtime vs compile-time** — cmangos enregistre au
  boot. Risque de doublon (deux modules enregistrent le même opcode).
  Détection au load + abort.
- ⚠️ **Backward compat opcodes existants** — migration progressive : un
  opcode peut exister en `constexpr` ET registry pendant une période.
  Convention claire ou migration big-bang.
- Pas de wire-breaking, pas de migration DB.

## 9. Recommandation finale

🔧 **Adapter et faire** — **prioriser PacketLog** pour le ROI debug
prod, **différer OpcodeRegistry** tant que le nombre d'opcodes reste
gérable :

1. **Étape 1 (priorité haute)** : implémenter `PacketLog` binaire +
   filtre PII + rotation + format versionné.
2. **Étape 2** : créer `tools/packet_replay/` minimal (replay offline
   sur 1-2 scénarios test).
3. **Étape 3 (différé)** : implémenter `OpcodeRegistry` quand on a
   30+ opcodes éparpillés (actuellement on en a ~50, donc bon moment).
4. **Étape 4 (à clarifier)** : DBCStores équivalent — analyser ce que
   `zone_builder`/`StreamCache`/`PakReader` couvrent déjà côté
   `engine/world/`. Probablement pas besoin de réinventer.

Pas de dépendance bloquante, peut être livré tôt en parallèle des
autres P2. Effort raisonnable, pas de risque architectural.

---

*Audit du 2026-05-08. Mises à jour : —*
