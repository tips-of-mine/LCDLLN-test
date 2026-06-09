# Slash Commands — RBAC & audit logging

> **Référence** : `game/data/config/slash_commands.json` est la source de vérité unique pour toutes les commandes slash du client. Toute commande créée, modifiée, ou supprimée doit y être reflétée.

## Hiérarchie des rôles

`accounts.role` (migration 0043) définit 4 niveaux ordonnés :

| Niveau | Rôle | Description |
|---|---|---|
| 0 | `player` | Joueur standard |
| 1 | `moderator` | Modérateur (kick / mute) |
| 2 | `game_master` | GM (ban / announce / Lune Noire events) |
| 3 | `administrator` | Admin système (debug overrides, promote) |

Une commande avec `minRole: "moderator"` est exécutable par moderator, game_master ET administrator (« supérieur ou égal »).

## Règles obligatoires (non négociables)

### 1. Toute commande slash doit être déclarée dans `slash_commands.json`

Avant d'écrire une seule ligne de code pour une nouvelle commande, l'entrée
JSON correspondante doit exister. Le fichier est versionné côté projet,
chargé au boot par client + master, et utilisé pour :
- Le client filtre les commandes que l'utilisateur peut voir / autocomplete.
- Le master valide le `minRole` à la réception.
- Les outils de doc / aide en jeu (futur `/help`) lisent ce fichier.

### 2. Toute commande slash doit interagir avec le serveur

Même les commandes purement visuelles (override local, inspection
client-only) doivent envoyer un packet au master via l'opcode dédié
`kOpcodeAdminCommandRequest` (ou équivalent par catégorie). Pourquoi :

- **Audit** : trace serveur de qui a tapé quoi (cf. règle 3).
- **RBAC** : le serveur valide le rôle. Le client peut faire un check
  préventif côté UI mais la décision finale est serveur (un client patché
  ne peut pas bypass).
- **Cohérence** : un seul chemin d'autorisation.

Exception très limitée : commandes purement informatives qui lisent un
state local (ex: `/sky info`). Même dans ce cas, le serveur doit logger
la tentative — au minimum via un opcode `AdminCommandLog`.

### 3. Toute exécution de commande doit être loggée serveur-side

Format obligatoire :
```
[AdminCommand] account_id=<X> role=<Y> command="<C>" args="<A>" result=<OK|DENIED|ERROR>
```

Subsystem de log : `Audit` (ou `Auth` si pas de subsystem dédié).

Le log doit être émis **dans tous les cas** :
- Succès (`result=OK`)
- Refus pour rôle insuffisant (`result=DENIED`)
- Erreur d'exécution (`result=ERROR <reason>`)

Cela permet de répondre à :
- « Qui a fait cette modification de l'environnement ? »
- « Combien de tentatives non autorisées sur ce compte ? »
- « Quand a été lancé ce dernier `/ban` ? »

## Workflow d'ajout d'une nouvelle commande

1. **Discuter le `minRole`** avec l'utilisateur AVANT d'implémenter.
2. **Ajouter l'entrée JSON** dans `game/data/config/slash_commands.json`
   avec status `planned`.
3. **Implémenter** via le pattern `AdminCommandHandler` master + intercept
   client (qui envoie au master plutôt que d'exécuter localement).
4. **Mettre à jour** le status à `implemented`, ajouter `implementation_file`.
5. **Tester** depuis un compte qui n'a PAS le rôle requis → vérifier le
   log master `[AdminCommand] DENIED ...` apparaît.
6. **Tester** depuis un compte qui A le rôle → vérifier
   `[AdminCommand] OK ...` apparaît.

## Statut courant des commandes existantes

Cf. `game/data/config/slash_commands.json` pour la liste exhaustive avec
le statut de chacune :

- **`client_only_legacy`** : commande implémentée AVANT la mise en place
  du RBAC. Elle intercepte client-side sans passer par le master. À
  refactorer pour conformité avec les règles 2 et 3.
- **`shardd_routed`** : commande parsée par `ChatCommandParser` côté
  shardd. Elle bénéficie déjà du routage serveur mais le logging
  d'audit doit être vérifié.
- **`server_routed`** : commande qui passe par `ChatCommandRouter` master
  avec `minRole` configuré (cf. `src/masterd/chat/ChatCommandRouter.h`).
  Pattern de référence pour les nouveaux ajouts.
- **`implemented`** : commande qui suit le pattern complet RBAC + log.
- **`planned`** : déclaration JSON présente mais pas encore implémentée.

### Commandes admin de test (level-up runtime)

| Commande | minRole (intention) | Gate shard réel | Audit | Statut |
|---|---|---|---|---|
| `/setlevel <player> <level>` | `administrator` | `ConnectedClient.chatModeratorRole` | `AuditLogModeration("SETLEVEL", ...)` | `shardd_routed` |

`/setlevel` fixe le niveau d'un joueur en ligne, recalcule ses stats (soin
complet via `CharacterStatsEngine::ComputeStats`), re-pousse la fiche
(`SendPlayerStats`) et persiste (`SaveConnectedClient("admin_setlevel")`).
Commande de triche destinée au test du système de level-up. Le `minRole`
JSON (`administrator`) exprime l'intention ; au niveau shard, le seul flag
de rôle disponible sur `ConnectedClient` est `chatModeratorRole` (aucun flag
admin/GM distinct n'existe à ce niveau), c'est donc lui qui garde la commande.

## Plan de migration

Les ~21 commandes actuellement marquées `client_only_legacy` doivent être
migrées progressivement vers le pattern complet. Priorité :

1. **Debug / override** (`/sky time`, `/sky moon`, `/loot simulate`) →
   admin-only, criticité auditing élevée.
2. **Moderation** (déjà shardd_routed mais à vérifier) → moderator/GM only.
3. **UI panels** (`/mail`, `/quest`, `/guild`, etc.) → player but doivent
   être loggées (qui a ouvert quel panel quand).

Les UI panels peuvent éventuellement utiliser un opcode léger
`AdminCommandLog` qui ne fait que tracer sans bloquer (status toujours
OK pour `minRole=player`), pour éviter la latence d'un round-trip
serveur sur des commandes UI fréquentes.
