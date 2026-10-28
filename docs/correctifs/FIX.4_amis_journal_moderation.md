# FIX.4 — Amis en base et journaux d'audit en écriture

Origine : logs serveur du 28/10/2026 (anomalies 3 et 4 de l'analyse).

## Journal de modération (shard)

Symptôme : `[SecurityAuditLog] Init FAILED: cannot open file game/data/logs/moderation_audit.log
— Read-only file system`, puis `Moderation audit log Init FAILED`. Les sanctions (/mute, /ban…)
n'étaient plus tracées.

Cause : `server.moderation_audit_log` (défaut `logs/moderation_audit.log`) était résolu sous la
racine de contenu (`game/data`), montée en lecture seule dans le conteneur.

Correction (`ServerApp::InitModerationAuditSubsystem`) : un chemin relatif est pris depuis le
répertoire de travail du shard (`/app` en Docker), donc `logs/moderation_audit.log` =
`/app/logs` = volume `./data/logs/shard`. Le dossier est créé s'il manque. Un chemin absolu
reste utilisé tel quel.

## Journal de sécurité (master)

`security.audit_log_path` passe de `security_audit.log` à `logs/security_audit.log` : en
Docker, il est désormais dans le volume `./data/logs/master` et survit à la recréation du
conteneur.

## Amis (shard)

Symptômes : `[FriendSystem] Init OK (no DB configured — no-DB mode)` au démarrage et
`GetFriendList: no DB` à chaque connexion ; les demandes d'ami échouaient toujours.

Causes :

1. Le shard Linux est compilé avec `SHARD_POSITION_DB`, pas `ENGINE_HAS_MYSQL` : tout le
   code SQL de `FriendSystem.cpp` était retiré de la compilation.
2. `ServerApp` passait `nullptr` comme connexion à chaque appel.
3. `GetFriendList` renvoyait les demandes **envoyées** et le client les affichait comme reçues.
4. Sans base, la présence était envoyée à **tous** les joueurs, sous l'étiquette `P<id>`.

Correction :

- `FriendSystem.cpp` compile le SQL si `ENGINE_HAS_MYSQL` **ou** `SHARD_POSITION_DB`.
  `Init(bool dbAvailable)` remplace `Init(MYSQL*)`.
- Chaque opération (/friend add|accept|decline|remove et paquets dédiés, lecture de la
  liste à la connexion) est déposée dans la file de sauvegarde (`EnqueueFriendDbJob`, à
  l'ordre des écritures du joueur), **jamais sur le tick**. Les résultats sont appliqués au
  tick par `ApplyFriendDbResults` : message au joueur (et à l'autre personnage : « X wants to
  be your friend », « X accepted… »), relecture des deux listes.
- Liste = amis acceptés + demandes reçues en attente (requête `UNION ALL`).
- Présence : seuls les amis acceptés **connectés** sont prévenus, sous le nom du personnage
  (`ConnectedClient::friendCharacterKeys`, lu à la connexion).
- Le compteur anti-spam (5 demandes / 60 s) est protégé par un verrou (threads de la file).
- Sans base (build Windows) : comportement historique inchangé.

Limite connue : le personnage retiré (/friend remove) voit sa liste mise à jour
immédiatement s'il est connecté sur le **même** shard ; sinon à sa prochaine connexion.

Table utilisée : `friends` (migration `0002_friends.sql`, déjà appliquée par le master), clés =
`characters.id`.

Vérification faite : scénario demande → refus du doublon → liste du destinataire (reçue) →
acceptation → listes des deux côtés → retrait → refus, contre MariaDB 10.11.
