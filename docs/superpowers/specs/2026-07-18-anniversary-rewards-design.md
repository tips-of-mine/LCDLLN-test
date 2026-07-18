# Récompenses d'anniversaire (inscription & naissance) — design

Date : 2026-07-18
Statut : validé par l'utilisateur (approche 1, découpage SP1/SP2/SP3)

## Objectif

Récompenser la fidélité des joueurs à partir de deux dates **immuables** :

1. **Anniversaire d'inscription** (`accounts.created_at`) : un exploit
   (« Fidèle depuis N an(s) ») par année révolue. **Jamais perdu** : crédité à
   la connexion suivante, quelle qu'elle soit (rattrapage automatique de tous
   les paliers manquants).
2. **Anniversaire de naissance** (`accounts.birth_date`, saisie à
   l'inscription) : **jour J strict** (UTC). Si le joueur se connecte ET entre
   en monde le jour même : un exploit annuel (« N anniversaire(s) fêté(s) »)
   + un courrier cadeau : **50 or + 50 argent + 50 bronze**, **1 gâteau parmi
   10 (aléatoire)**, **1 cosmétique millésimé**. Pas connecté le jour J =
   perdu pour l'année.

## Anti-triche (exigence forte)

- `created_at` et `birth_date` sont **immuables** : écrits une seule fois à
  l'inscription (INSERT), aucun chemin de modification (ni opcode, ni portail
  web — vérifier et retirer toute édition existante de `birth_date`).
- `birth_date` est validée côté serveur à l'inscription (format `yyyy-mm-dd`,
  date réelle, plausible : entre 1900 et aujourd'hui).
- Toutes les décisions (calcul d'années, éligibilité jour J, octrois) sont
  côté **master**, en **UTC**. Le client n'envoie jamais de date après
  l'inscription.
- Idempotence : exploits par PK `(account_id, exploit_id)` (INSERT IGNORE) ;
  cadeaux par table de garde `account_anniversary_rewards
  (account_id, reward_year, kind)` (PK) — une seule livraison par compte/an
  même en cas de reconnexions multiples le même jour.
- 29 février : fêté le 28/02 les années non bissextiles.

## Existant réutilisé

- `accounts.created_at` (0001) et `accounts.birth_date` (0023/0066) : déjà en
  DB ; `birth_date` déjà chargée (`AccountRecord`) ; **`created_at` à ajouter
  au chargement** (lecture seule).
- Écran d'inscription client : collecte déjà prénom/nom/date de
  naissance/pays.
- Schéma exploits (0008/0015) : `exploits` (catalogue) +
  `account_exploit_unlocks` — lu par le portail web ; **aucun writer C++**
  (à créer).
- Courrier in-game (opcodes 49-58, `MysqlMailStore::Insert`) : pièces jointes
  objets (`mail_items`) + or (`copper_gold`) → canal de livraison des cadeaux.
- Chat relay (opcode 46) : notification système « Exploit débloqué : … ».
- Système d'auras/buffs shard (Wave 23) : support du buff du gâteau.
- Groupes (LFG) et guildes : cibles du partage du gâteau.

## Architecture (approche 1 — détection à la connexion)

Nouveau `AnniversaryService` côté master, sans scheduler :

- **À l'AUTH** : années révolues depuis `created_at` → débloque tous les
  `signup_anniv_*` manquants. Si jour UTC == jour/mois de `birth_date` →
  débloque `birthday_N` avec N = (nombre de paliers `birthday_*` déjà
  débloqués dans `account_exploit_unlocks`) + 1 ; l'idempotence intra-jour
  est assurée par la garde `(account_id, year, 'birthday_exploit')`.
- **Au premier EnterWorld du jour J de naissance** : dépose le courrier
  cadeau au personnage entrant (il faut un destinataire personnage). Garde
  `(account_id, year, 'birthday_gift')`. Pas d'EnterWorld le jour J = pas de
  cadeaux.
- La logique de dates vit dans un module pur (`AnniversaryMath`) avec horloge
  injectée (testable).

## Données

- Migration `0074_anniversary_exploits.sql` :
  - seed `exploits` : `signup_anniv_1..10` (metric_source `signup_years`,
    threshold N) et `birthday_1..10` (metric_source `birthday_years`),
    scope `account`, titres FR ;
  - table `account_anniversary_rewards (account_id, reward_year, kind,
    granted_at)` PK `(account_id, reward_year, kind)`.
- `game/data/items/items.json` : 10 gâteaux (`anniv_cake_01..10`, un buff
  distinct chacun) + cosmétiques millésimés (`anniv_keepsake_<slot>_<année>`,
  bonus réels modestes, slot tournant par année de fidélité : an 1 = cape/dos,
  an 2 = anneau, an 3 = amulette…). Les cadeaux arrivent dans l'inventaire
  via le courrier, **jamais auto-équipés**.
- Or du courrier : montant encodé dans l'unité de base de la monnaie
  (50 or + 50 argent + 50 bronze, mapping vérifié sur la convention 100:1 de
  l'affichage client).

## Le gâteau (gameplay, SP3)

- Reçu le jour J, le gâteau est un objet d'inventaire. Le joueur le place
  **lui-même** dans la barre d'action (qui accepte désormais des objets en
  plus des sorts du Grimoire).
- Activé, il applique une aura de buff aux membres du **groupe ou de la
  guilde** dans un rayon configurable (défaut 30 m). L'aura est entretenue
  par le shard **tant que le gâteau reste slotté** ; retiré du slot, elle
  tombe immédiatement.
- 10 gâteaux = 10 buffs distincts (data), pour éviter que tout le monde
  compte sur le même.
- À minuit UTC de la fin du jour J, le gâteau est « mangé » : retiré de
  l'inventaire et de la barre (expiration par pile d'objet, nouveau champ).

## UI (SP2)

- Deux opcodes additifs (pattern Grimoire 88/89) : « liste de mes exploits »
  → (code, titre, débloqué le / verrouillé ; les `is_secret` non débloqués
  sont omis).
- Nouvel onglet **Exploits** dans la fenêtre Personnage (F1) : débloqués avec
  date, puis verrouillés grisés.
- Le portail web continue de lire les mêmes tables (zéro changement).

## Découpage & déploiement

| PR | Contenu | Déploiement |
|---|---|---|
| SP1 | created_at chargé + validation/gel birth_date + migration 0074 + MysqlExploitStore + AnniversaryService (AUTH + EnterWorld) + courrier cadeaux + items data + notifications chat | ⚠️ master + DB (migration au boot) |
| SP2 | opcodes liste exploits + onglet F1 Exploits | ⚠️ lock-step client + master |
| SP3 | objets dans la barre d'action + activation gâteau + aura de zone groupe/guilde + expiration minuit | ⚠️ lock-step client + shard (bump protocole probable) |

Ordre de merge : SP1 → SP2 → SP3 (SP2/SP3 portent les commits de leurs
prédécesseurs, PRs base main).

## Tests

- `AnniversaryMath` pur (années révolues, 29/02, éligibilité jour J, UTC) —
  ctest Linux.
- Round-trip des payloads des nouveaux opcodes (SP2/SP3).
- Idempotence du service avec stores factices (double AUTH le même jour = un
  seul octroi).

## Hors périmètre (dettes notées)

- Transmog/apparence pure (le cosmétique utilise l'équipement à stats).
- Persistance MySQL du portefeuille shard (`player_wallet` reste orpheline ;
  l'or transite par le courrier).
- Fenêtre de grâce pour l'anniversaire de naissance (choix : jour strict).
