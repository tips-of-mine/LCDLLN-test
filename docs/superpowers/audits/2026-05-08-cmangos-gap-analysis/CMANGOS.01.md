# CMANGOS.01 — Chat (routing / safety / commands split)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.01_Chat_routing_safety_commands_split.md](../../../../tickets/CMANGOS/CMANGOS.01_Chat_routing_safety_commands_split.md)
> **Priorité** : P1 — déblocant MVP
> **Cible** : cross (master + shard)

## 1. Statut implémentation

🟡 **Partiel** — relay master fonctionnel (whisper/guild/friends/broadcast),
mais l'ensemble des couches sécurité, commands hiérarchique et proximité
shard sont absents.

## 2. Preuves dans le code

**Existant (master uniquement) :**
- [engine/server/ChatRelayHandler.cpp](../../../../engine/server/ChatRelayHandler.cpp) (371 lignes) — handler opcode 45 (`kOpcodeChatSendRequest`) avec routing :
  - Whisper (résolution par nom normalisé via `SessionCharacterMap`)
  - Guild (SQL `guild_members` puis snapshot conn↔session)
  - Friends (SQL `friends` status=1)
  - Broadcast (toutes sessions actives)
- [engine/server/ChatCommandParser.cpp](../../../../engine/server/ChatCommandParser.cpp) (271 lignes) — parsing de commandes
- [engine/network/ChatPayloads.cpp](../../../../engine/network/ChatPayloads.cpp) (89 lignes) — sérialisation payloads chat
- [engine/network/ProtocolV1Constants.h:183-184](../../../../engine/network/ProtocolV1Constants.h) — opcodes 45/46 alloués
- Truncation présente : [ChatRelayHandler.cpp:78-81](../../../../engine/server/ChatRelayHandler.cpp) → `parsed->text.resize(kMaxChatTextBytes)` mais **NON safe UTF-8** (peut couper au milieu d'un codepoint multi-byte)

**Manquant (vs spec ticket) :**
- ❌ `engine/server/chat/` — dossier inexistant
- ❌ `ChatGate::CanSpeak()` — aucun anti-flood, aucun mute, aucune restriction niveau, aucune table `chat_mutes`
- ❌ `ChatSanitizer::Sanitize()` — pas de truncation UTF-8 safe, pas de whitelist hyperlinks (`|H...|h`), pas de strip zero-width characters
- ❌ `ChatChannelRegistry` — pas de canaux dynamiques `/join`, pas de password, pas de ban-list par canal
- ❌ `ChatCommandRouter` table-driven — `ChatCommandParser` existe mais aucun grep ne remonte `minRole`/`AccountRole`/`security_level` → pas de dispatch hiérarchique avec niveaux de sécurité
- ❌ `ChatLocalRelay` shard — aucun fichier dans `engine/server/shard/` ni équivalent
- ❌ Opcode `kOpcodeChatLocal` (47/48) — non alloué
- ❌ Migration `chat_mutes.sql` — absente de `db/migrations/`

## 3. Recouvrement milestones existantes

✅ **Couvert** — M29 contient 3 tickets chat :
- M29.1 Chat multi-channel messaging + UI
- M29.2 Chat commands + moderation tools
- M29.3 Chat emotes + chat bubbles 3D

(Le rapport CMANGOS.01 vs M29 sera à arbitrer : adapter CMANGOS.01 dans le
cadre M29, plutôt que ticket parallèle.)

## 4. Écart par rapport à la spec CMANGOS

5 livrables sur 6 absents. Le seul livrable existant (le `ChatRelayHandler`
master) est au stade MVP : il fait le routing canal-par-canal mais n'applique
**aucun** garde-fou de sécurité (pas de `CanSpeak`, pas de sanitization, pas
de quota anti-flood). La truncation à 255 octets actuelle peut produire des
codepoints UTF-8 corrompus côté client (emoji 4-bytes coupés).

Aucune séparation master/shard pour les messages de proximité (`say/yell/emote`)
— actuellement tout passe par master en broadcast, ce qui ne scale pas et
expose le master à du bruit local.

## 5. Effort estimé

**L** (1 sprint complet) — 5 nouveaux composants (Gate, Sanitizer, ChannelRegistry,
CommandRouter, LocalRelay) + tests + migration DB `chat_mutes` + nouvel opcode
shard + bump `kProtocolVersion` + redéploiement client/master/shard lock-step.
Recommandable de splitter en 3 PR (Sanitizer+Gate, Commands+Channels, ShardLocal).

## 6. Valeur joueur/serveur

**Critique** — explicitement marqué "déblocant MVP" dans `CMANGOS.INDEX.md`.
Ouvrir le chat publiquement sans `ChatSanitizer` expose à tous les exploits
classiques cmangos : item link forging, hyperlinks fake, spam, zero-width
character injection. C'est un risque de sécurité actif tant que le ticket
n'est pas livré.

## 7. Dépendances bloquantes

- **CMANGOS.06 Accounts** — requis pour `AccountRole` (Player/Moderator/GM/Admin)
  utilisé par `ChatCommandRouter` pour dispatcher selon le `minRole`. À livrer
  d'abord ou en parallèle avec coupling tardif.
- **M29.1 / M29.2** — milestones LCDLLN qui couvrent partiellement le sujet ;
  arbitrage à faire avant de développer.
- **Shard fonctionnel** — pour `ChatLocalRelay` (`say/yell/emote`). M19 Maps
  recommandé sinon fallback broadcast shard (acceptable pour la v1).

## 8. Risque / piège ⚠️

- ⚠️ **Wire-breaking** — allocation de `kOpcodeChatLocal` (47/48) + bump
  `kProtocolVersion` → **redéploiement serveur master + shard + client en
  lock-step obligatoire**. Les clients pré-PR ne pourront pas envoyer
  `say/yell/emote`.
- ⚠️ **Migration DB** — nouvelle table `chat_mutes` (`account_id`, `until_ts`,
  `reason`). Migration idempotente à ajouter dans `db/migrations/00xx_chat_mutes.sql`.
- ⚠️ **Redéploiement** — nouveau handler shard (`ChatLocalRelay`) + handler
  master modifié (intégration `ChatGate`/`ChatSanitizer`).
- ⚠️ **Sécurité** — la non-livraison de `ChatSanitizer` est un **risque actif**
  si le chat reste ouvert publiquement (pattern cmangos `CheckEscapeSequences`
  existe pour une raison).
- ⚠️ **Config** — nouvelles clés `chat.max_message_bytes`, `chat.anti_flood.*`,
  `chat.min_level_to_use_world`, `chat.command_prefix` à provisionner dans
  `config.json` serveur.
- ⚠️ **UTF-8 truncation** — la truncation actuelle (`resize(255)`) peut couper
  au milieu d'un codepoint multi-byte. Tester avec emoji 4-bytes.

## 9. Recommandation finale

✅ **Faire en l'état**, en priorité haute, en arbitrant avec M29 :

1. **Étape 0** : statuer M29.1/M29.2 vs CMANGOS.01 — soit fusionner les
   spec, soit décider que CMANGOS.01 remplace M29.1/M29.2.
2. **Étape 1** : livrer CMANGOS.06 Accounts (pré-requis `AccountRole`).
3. **Étape 2** : PR1 = `ChatSanitizer` + `ChatGate` (les plus testables, pas
   de wire-breaking).
4. **Étape 3** : PR2 = `ChatCommandRouter` + `ChatChannelRegistry` (master-only,
   pas de wire-breaking).
5. **Étape 4** : PR3 = `kOpcodeChatLocal` + `ChatLocalRelay` shard (wire-breaking
   → bump `kProtocolVersion`, redéploiement lock-step).
6. **Tester explicitement** : injection `|Hbadtype:foo|hbar|h`, message
   > 255 octets avec emoji 4-bytes en fin, anti-flood 11 messages en 10s.

---

*Audit du 2026-05-08. Mises à jour : —*
