# CMANGOS.01_Chat_routing_safety_commands_split

## Objectif

Solidifier le système de chat LCDLLN (actuellement MVP avec opcodes 45/46
relayés par `ChatRelayHandler` côté master) en portant 5 patterns éprouvés
de cmangos `src/game/Chat`. Trois objectifs simultanés :

1. **Sécurité** : protéger l'envoi public contre les exploits classiques
   (item links forgés, hyperlinks fake, caractères invisibles, messages
   trop longs).
2. **Architecture** : formaliser le **split master/shard** (relay sur
   master, proximité sur shard) pour ne pas charger inutilement le master
   de paquets `say/yell/emote` localisés.
3. **Extensibilité** : préparer le terrain pour les futures slash commands
   GM avec un dispatch table-driven hiérarchique, et pour des canaux
   thématiques dynamiques (`/join recrutement-guilde`).

C'est un **déblocant MVP** — sans ces protections, l'ouverture publique du
chat est risquée.

## Dépendances

- M00.1 (build base)
- M44.4 (logger avec filtres bitmask, déjà fait — le filtre `LogFilter::ChatRelay` peut être activé pour debug)
- Le `ChatRelayHandler` existant côté master (opcodes 45/46) reste la couche d'entrée.

## Livrables

### Côté master (`engine/server/`)

- `engine/server/chat/ChatGate.{h,cpp}` — équivalent de `Player::CanSpeak()`, agrège tous les checks (mute, anti-flood, restrictions niveau, ban global). Une seule fonction `bool ChatGate::CanSpeak(uint32_t accountId, ChatChannel ch, std::string& denyReason)`.
- `engine/server/chat/ChatSanitizer.{h,cpp}` — équivalent de `CheckEscapeSequences()` + truncation UTF-8 + strip caractères invisibles. API : `bool ChatSanitizer::Sanitize(std::string_view in, std::string& out, std::string& rejectReason)`.
- `engine/server/chat/ChatChannelRegistry.{h,cpp}` — gestion des canaux dynamiques (création, ownership, ban-list, password optionnel, transfert auto).
- `engine/server/chat/ChatCommandRouter.{h,cpp}` — dispatch table-driven hiérarchique pour slash commands GM. Tableau statique de `{name, security_level, handler, sub_table*}` parcouru récursivement.
- Modifications dans `ChatRelayHandler.cpp` pour passer chaque message par `ChatGate` puis `ChatSanitizer` avant routage.

### Côté shard (`engine/server/shard/` ou équivalent)

- `engine/server/shard/ChatLocalRelay.{h,cpp}` — gestion exclusive de `say/yell/emote` (proximité 3D). Filtrage par AoI (à câbler quand Grids arrive) ; fallback en broadcast shard tant que pas d'AoI.
- Le shard expose un opcode dédié `kOpcodeChatLocal` (à allouer, ex. 47/48) pour différencier des messages relay master.

### Configuration (`config.json`)

```json
"chat": {
  "max_message_bytes": 255,
  "anti_flood": {
    "max_messages_per_10s": 10,
    "max_messages_per_60s": 30
  },
  "min_level_to_use_world": 5,
  "min_level_to_create_channel": 1,
  "channel_password_required": false,
  "command_prefix": "."
}
```

### Tests

- `engine/server/chat/ChatSanitizerTests.cpp` — couvre : truncation à 255 octets sur frontière UTF-8 (pas couper un codepoint multi-byte), rejet de `|H...|h`, rejet de zero-width-joiner, échappement de backslashes, message vide.
- `engine/server/chat/ChatGateTests.cpp` — couvre : mute global → false, anti-flood seuil dépassé → false, restriction niveau → false, joueur en règle → true.
- `engine/server/chat/ChatCommandRouterTests.cpp` — couvre : `.ban account toto` route bien dans la sous-table `ban`, niveau de sécurité insuffisant → rejet, commande inexistante → message d'erreur.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- Contenu : N/A (pas d'asset)
- Outils offline : N/A
- Tous les chemins dans la config relatifs à `paths.content` si besoin (pas de chemin absolu)
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. `ChatGate::CanSpeak`

Agrège **dans cet ordre** (court-circuite à la première raison de refus) :

1. Le compte est-il banni globalement ? (`AccountStore::IsBanned`)
2. Le compte/personnage est-il muté ? (table `chat_mutes` à créer, colonnes `account_id`, `until_ts`, `reason`)
3. Anti-flood : compteur sliding-window (10s et 60s) par accountId. Implémentation `std::deque<int64_t>` des timestamps des N derniers messages.
4. Niveau minimum requis pour le canal (ex. world chat ≥ 5).
5. Restriction de canal (membre de la guilde pour guild chat, dans le groupe pour party chat, etc.).

Retourne `true` + `denyReason = ""` ou `false` + `denyReason` rempli pour log/erreur client.

### 2. `ChatSanitizer::Sanitize`

Étapes en série :

1. **Truncation UTF-8 safe** : on cherche le dernier byte dont le high bit est 0 ou la frontière de codepoint avant 255 octets. Tronque à cette position, jamais au milieu d'un codepoint.
2. **Strip caractères invisibles** : zéro-width joiner (U+200B-U+200F, U+FEFF, U+202A-U+202E). Optionnel via config.
3. **Rejet des hyperlinks forgés** : pattern `|H<type>:<args>|h<text>|h` — autorisé uniquement si `<type>` est dans la whitelist (`item:`, `quest:`, `achievement:`, `spell:`). Tout autre = rejet du message.
4. **Échappement** : aucun (les hyperlinks valides passent tels quels au client).
5. **Limite finale** : si après strip le message est vide, rejet.

### 3. `ChatChannelRegistry`

- Map `channelName → ChannelState`.
- `ChannelState` : `ownerAccountId`, `password` (vide = pas de password), `bannedAccounts: std::unordered_set<uint32>`, `members: std::unordered_set<uint32>`.
- `Join(accountId, name, password)` :
  - Crée le canal si inexistant et l'`accountId` devient owner.
  - Si existant : vérifie password, vérifie pas dans ban-list.
- `Leave(accountId, name)` :
  - Si l'owner part : transfert au plus ancien membre (priorité aux modérateurs si jamais on en ajoute).
- Channels persistés ? **Non au début** — purement runtime master. Persistance différée à un futur ticket.

### 4. `ChatCommandRouter`

```cpp
struct ChatCommand {
  std::string_view name;
  AccountRole minRole;          // de Accounts (P2)
  bool (*handler)(const ChatContext&, std::string_view args);
  const ChatCommand* subTable;  // nullptr si feuille
  size_t subTableSize;
};

static const ChatCommand kCommandsBan[] = {
  {"account", AccountRole::Moderator, HandleBanAccount, nullptr, 0},
  {"ip",      AccountRole::Admin,     HandleBanIp,      nullptr, 0},
};

static const ChatCommand kCommandsRoot[] = {
  {"ban",   AccountRole::Moderator, nullptr, kCommandsBan, std::size(kCommandsBan)},
  {"kick",  AccountRole::Moderator, HandleKick, nullptr, 0},
  {"help",  AccountRole::Player,    HandleHelp, nullptr, 0},
};
```

`Resolve(input)` parse `.ban account toto` → recherche `ban` dans root → puis `account` dans subTable → vérifie `minRole` ≤ rôle de l'appelant → invoque `HandleBanAccount`.

Note : `AccountRole` vient du ticket Accounts (CMANGOS.06) — câbler une fois ce dernier livré.

### 5. Split master/shard

| Type de message | Cible | Opcode | Justification |
|---|---|---|---|
| `say` | shard | `kOpcodeChatLocal` (nouveau) | Proximité 3D, broadcast aux joueurs dans X mètres |
| `yell` | shard | `kOpcodeChatLocal` (nouveau) | Proximité étendue, X×3 mètres |
| `emote` | shard | `kOpcodeChatLocal` (nouveau) | Proximité 3D |
| `whisper` | master | `kOpcodeChatRelay` (existant) | Routage par session, master connaît qui est en ligne |
| `party` | master | `kOpcodeChatRelay` (existant) | Master connaît la composition du groupe |
| `guild` | master | `kOpcodeChatRelay` (existant) | Master connaît les membres |
| `channel` | master | `kOpcodeChatRelay` (existant) | Channel est master-side |
| `world` | master | `kOpcodeChatRelay` (existant) | Broadcast cross-shard |

Le client envoie l'opcode adapté selon le type. Le `ChatRelayHandler` côté master rejette `say/yell/emote` (réponse erreur dirigeant vers le shard). Le `ChatLocalRelay` côté shard rejette `whisper/party/guild/channel/world` (réponse erreur dirigeant vers le master).

## Étapes d'implémentation

1. **Allouer les nouveaux opcodes** (`kOpcodeChatLocal`, `kOpcodeChatLocalEcho`) dans `engine/network/ProtocolV1Constants.h` et bumper `kProtocolVersion` (wire-breaking).
2. **Créer `engine/server/chat/`** avec les 4 fichiers `.{h,cpp}` (Gate, Sanitizer, ChannelRegistry, CommandRouter).
3. **Implémenter `ChatSanitizer`** en premier (le plus testable, isolé).
4. **Implémenter `ChatGate`** ; ajouter la migration DB `00xx_chat_mutes.sql`.
5. **Implémenter `ChatChannelRegistry`** ; ajouter handlers pour les nouveaux opcodes (`JoinChannel`, `LeaveChannel`, `SendChannelMessage`).
6. **Implémenter `ChatCommandRouter`** avec un set initial minimal (`.help`, `.kick`, `.mute`).
7. **Modifier `ChatRelayHandler`** pour passer chaque message par `ChatGate::CanSpeak` puis `ChatSanitizer::Sanitize` avant routage. Rejeter explicitement `say/yell/emote`.
8. **Créer `ChatLocalRelay`** côté shard pour `say/yell/emote`. Pas d'AoI au début — broadcast shard simple en fallback (tag TODO pour intégration AoI quand Grids arrive).
9. **Câbler la config** dans `engine/core/Config` (lecture des clés `chat.*`).
10. **Tests** : 3 fichiers de tests listés dans Livrables.
11. **Doc** : ajouter un paragraphe « Chat — split master/shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master + shard) via presets existants
- [ ] Build Windows OK (client) via presets existants
- [ ] Tous les tests `Chat*Tests` passent
- [ ] Smoke test : 2 clients connectés peuvent s'envoyer un whisper, un party message, un say (proximité), et `.help` côté client GM
- [ ] Tentative d'injection `|Hbadtype:foo|hbar|h` → message rejeté avec `denyReason` loggé via `LOG_FILTERED(Warn, ChatRelay, ...)`
- [ ] Tentative de message > 255 octets → tronqué proprement à la frontière UTF-8
- [ ] Tentative anti-flood : 11 messages en 10 secondes → 11ᵉ rejeté
- [ ] Migration DB `chat_mutes` appliquée et idempotente
- [ ] `kProtocolVersion` bumpé, master + shard + client redéployés en lock-step
- [ ] Aucun nouveau dossier racine non autorisé créé
- [ ] Rapport final : fichiers modifiés + commandes + résultats + DoD

## Notes / pièges à éviter

- **UTF-8 truncation** : ne **jamais** tronquer au milieu d'un codepoint multi-byte — sinon le client affiche `?` ou crashe selon le moteur de texte. Tester avec emoji 4-bytes (`U+1F600` = 😀).
- **Split master/shard wire-breaking** : c'est un changement de protocole. Les clients pré-PR ne pourront pas envoyer `say/yell` ; bumper `kProtocolVersion` et redéployer master + shard + client en lock-step (cf. CLAUDE.md).
- **Persistance des channels** : volontairement **pas** persistés au début. Si le master redémarre, tous les canaux dynamiques sont perdus. C'est acceptable pour la v1 ; à reconsidérer si les guildes utilisent massivement des canaux thématiques (futur ticket).
- **Anti-flood = état par accountId mais sliding window** : ne pas utiliser un simple compteur reset à chaque seconde, sinon attaque trivialisée par burst en début de fenêtre. `std::deque<int64_t>` purgé à chaque check est la bonne structure.
- **Command security** : ne jamais router une commande sans avoir vérifié `minRole`. Le routeur **doit** être le seul point qui fait ce check, pas le handler — sinon une commande oubliera de check et créera une escalade de privilèges.
- **Hyperlink whitelist** : démarrer **strict** (4 types). Élargir uniquement après audit de chaque nouveau type. Un type non-whitelisté = rejet, pas strip silencieux (le strip silencieux casse l'UX, le rejet apprend au joueur).
- **Mode debug** : activer `log.filters.chat_relay = true` (déjà disponible via PR #468) pour tracer le routage en cas de bug.

## Références

- `CMANGOS_ANALYSIS.md` § Chat (P1 cross master+shard)
- cmangos `src/game/Chat/Chat.cpp`, `src/game/Chat/ChannelMgr.cpp`, `src/game/Chat/Level0.cpp` à `Level3.cpp`
- PR #468 (logger avec filtres bitmask) — la PR suivante peut activer `LogFilter::ChatRelay`
