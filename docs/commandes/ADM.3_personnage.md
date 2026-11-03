# ADM.3 — Commandes de personnage (maître de jeu)

Aide en jeu : `/help character`. Sans nom de joueur, la commande vise
l'appelant. Le joueur visé est prévenu par un message système.

| Commande | Alias | Effet | Sauvegardé |
|---|---|---|---|
| `/level <n> [joueur]` | `/setlevel` | fixe le niveau, XP du niveau à 0, stats recalculées, soin complet (`/setlevel <joueur> <n>` accepté) | oui |
| `/xp <points> [joueur]` | `/addxp` | ajoute de l'XP, passages de niveau compris | oui |
| `/heal [joueur]` | | vie et ressource au maximum | — |
| `/revive [joueur]` | `/resurrect` | ressuscite sur place, ferme l'écran de mort | oui |
| `/kill [joueur]` | | sans argument : tue la cible (mob : butin et XP pour l'appelant) ; avec un nom : tue le joueur | oui (joueur) |
| `/hp <n> [joueur]` | | fixe la vie actuelle (≥ 1) | — |
| `/mana <n> [joueur]` | `/resource` | fixe la ressource actuelle | — |
| `/invincible [on\|off] [joueur]` | `/god` | aucun dégât subi ; les mobs l'oublient | session |
| `/invisible [on\|off] [joueur]` | `/invis` | absent des snapshots des autres joueurs, jamais ciblé par les mobs | session |
| `/nocost [on\|off] [joueur]` | | sorts et compétences sans coût ni recharge | session |
| `/cooldown [joueur]` | `/cd` | temps de recharge remis à zéro | — |
| `/learn <id>\|all [joueur]` | | compétence de classe (remplace le choix du même palier) ; `all` = un choix par palier débloqué vide | oui |
| `/unlearn <id> [joueur]` | | retire une compétence | oui |
| `/resetskills [joueur]` | | retire toutes les compétences choisies | oui |
| `/profession <métier> <1-300> [joueur]` | `/setprofession` | niveau de métier (appris si besoin) | oui |
| `/setrep <joueur\|login> <faction> <valeur>` | | réputation d'un compte (master), sans débordement sur les autres factions | oui (base) |

« Session » : pouvoir porté par `ConnectedClient::gm`, perdu à la déconnexion.

## Synchronisation du client

- **Vie / mort** : CombatEvent « résurrection » (`kCombatEventFlagResurrection`) —
  seul canal qui rafraîchit les PV du joueur local, sans ligne dans le journal de
  combat. `/revive` envoie aussi un ForcePosition « réapparition » sur place (ferme
  l'écran de mort même si l'événement se perd). `/kill` d'un joueur envoie un
  CombatEvent de dégâts normal (l'écran de mort s'ouvre).
- **Ressource** : ResourceUpdate forcé. **Fiche** : PlayerStats ; **niveau** :
  PlayerXpUpdate ; **compétences** : ClassProgressionUpdate ; **métiers** :
  ProfessionUpdate ; **pouvoirs** : GmStateUpdate (ADM.1).

## Points touchés dans le shard

- `TryMobAttackPlayer` : dégâts nuls si la cible est invincible.
- `BuildRelevantEntityIds` : un joueur invisible est retiré des entités envoyées
  aux autres ; `RefreshMobAggroTarget` le retire des tables de menace.
- `HandleCastRequest`, `ResolveSpellCast`, `ResolveClassSkillCast` : coût nul et
  pas de temps de recharge avec `/nocost`.
- `ReputationManager::SetReputation` (master) : valeur absolue, bornée, sans
  débordement.

## Fichiers

| Fichier | Rôle |
|---|---|
| `src/shared/server_bootstrap/ServerAppGmCharacter.cpp` | 15 commandes shard |
| `src/masterd/admin/MasterGmCommands.cpp` (`RegisterCharacter`) | `/setrep` |
| `src/masterd/reputation/ReputationManager.h` | `SetReputation` |

L'ancienne entrée « /setlevel <player> <level> » du registre (jamais relayée au
shard depuis la refonte du chat) devient l'alias de `/level`.

## Déploiement

**Master + shard** en lock-step. Client inchangé (messages existants), pas de
migration.
