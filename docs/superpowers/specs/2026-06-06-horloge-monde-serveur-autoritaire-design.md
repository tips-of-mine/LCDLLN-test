# Horloge monde serveur-autoritaire (jour/nuit + lune unifiés)

**Date** : 2026-06-06
**Statut** : design validé (en attente relecture utilisateur)
**Périmètre** : master (autorité) + shard (lecture) + client (sync & calcul local)

---

## 1. Contexte & problème

Aujourd'hui le cycle jour/nuit est **100 % client-local** : chaque client lance
`DayNightCycle::Init` au démarrage et fait avancer sa propre horloge
(`timeScale` 60 par défaut → un jour de jeu = 1 h réelle). Conséquences :

- **Pas de synchronisation** : deux joueurs connectés peuvent être à des heures
  différentes du jour.
- **Pas de contrôle serveur/admin** : impossible de forcer la nuit, geler ou
  accélérer le temps de façon globale.
- **Deux horloges incohérentes** : la **lune** est déjà serveur-autoritaire
  (`LunarHandler` master, `LunarCalendar` shard, opcodes 192-194, epoch réel
  `cycle_start_unix_ms` + durée `cycle_duration_ms` dans `config.json`), tandis
  que le **soleil** est client-local. Soleil et lune ne sont pas sur la même base.

## 2. Objectifs (tous retenus)

1. **Synchroniser les joueurs** : tous voient la même heure du jour.
2. **Contrôle serveur/admin** : fixer/geler/accélérer le temps globalement.
3. **Source de vérité unique** pour les mécaniques liées au temps (spawns
   nocturnes, PNJ, lune, météo).
4. **Temps continu** basé sur un epoch réel (avance même hors connexion).

## 3. Décisions actées

| Décision | Choix |
|---|---|
| Modèle de sync | **Epoch + paramètres**, calcul **local** côté client (pas de polling de l'heure brute). |
| Contrôle de dérive | **Re-sync périodique** (toutes les N min) avec correction **douce** (lerp). |
| Persistance des overrides admin | **Runtime seulement** — au reboot master, l'horloge repart de la formule du `config.json`. Pas de DB. |
| Unification soleil/lune | **Unifier maintenant** : une seule « horloge monde » d'où dérivent jour/nuit **et** phase de lune. |
| Autorisation des modifications | **RBAC « admin » uniquement** (enforcement master) pour MODIFIER. La **lecture** (sync) est ouverte à tout client authentifié. |

## 4. Architecture

### 4.1 Horloge monde (master, autoritaire)

Le master détient un état d'horloge unique :

```
WorldClockState {
    uint64 epochRefUnixMs;   // origine du temps (depuis config.json, défaut = 2026-01-01)
    float  timeScale;        // minutes RÉELLES par jour de jeu (défaut 60 = 1 jour/h)
    double offsetGameSec;    // décalage runtime appliqué par /settime (non persisté)
    bool   paused;           // /pausetime — fige l'horloge
    double pausedAtGameSec;  // valeur figée quand paused = true
}
```

**Temps de jeu courant** (en secondes de jeu depuis l'epoch) :

```
gameSec(nowUnixMs) = paused
    ? pausedAtGameSec
    : ((nowUnixMs - epochRefUnixMs) / 1000.0) * (86400.0 / (timeScale * 60.0)) + offsetGameSec
```

(`86400 / (timeScale*60)` = facteur « secondes de jeu par seconde réelle » ;
avec `timeScale=60`, 1 s réelle = 24 s de jeu.)

### 4.2 Dérivations (client ET serveur, même formule)

- **Heure du jour** : `timeOfDayHours = (gameSec / 3600.0) mod 24.0`.
- **Phase de lune** : `phase = floor((gameSec / lunarPeriodGameSec) * 16) mod 16`,
  où `lunarPeriodGameSec` = durée d'un cycle lunaire **en temps de jeu**
  (ex. 16 jours de jeu). Voir §8 pour la conséquence sur le timing lunaire.

### 4.3 Rôles

- **Master** : seule autorité. Calcule l'état, répond aux requêtes, applique les
  commandes admin, broadcaste les changements.
- **Shard** : lit la **même** formule (header-only, comme `LunarCalendar`) à
  partir des params reçus/config — pour ses besoins (spawns nocturnes, etc.).
- **Client** : reçoit les params + `serverTimeMs`, calcule **localement** soleil
  et lune chaque frame, re-cale sur notif et sur contrôle de dérive.

## 5. Protocole (wire)

### 5.1 Nouveaux opcodes — sync horloge (calque lunaire 192-194)

200-202 sont déjà pris par Interactive Props (`kOpInteractive*`) ; max opcode = 202. On prend **203-205** :

| Opcode | Nom | Sens | Charge |
|---|---|---|---|
| 203 | `kOpcodeWorldClockStateRequest` | Client → Master | vide |
| 204 | `kOpcodeWorldClockStateResponse` | Master → Client | `status`, `serverTimeUnixMs`, `epochRefUnixMs`, `timeScale`, `offsetGameSec`, `paused`, `pausedAtGameSec`, `lunarPeriodGameSec` |
| 205 | `kOpcodeWorldClockChangeNotification` | Master → Client (push, request_id=0) | mêmes champs que 204 |

`status` = `Ok` / `Unauthorized` (réutilise le pattern `LunarStatus`). La réponse
porte `serverTimeUnixMs` pour que le client cale son horloge (compense la latence
en estimant le RTT/2, comme `serverTimeMs` déjà présent dans d'autres payloads).

Payloads dans un nouveau `src/shared/network/WorldClockPayloads.h` (calque de
`LunarPayloads.h`), little-endian, sérialisation explicite.

### 5.2 Lecture libre, modification gated

- **203/204/205** (lecture/sync) : tout client authentifié (session valide). Pas
  de RBAC — c'est de la lecture.
- **Modification** : passe **exclusivement** par les commandes admin (§6), donc
  par l'opcode existant `kOpcodeAdminCommandRequest` (195) qui applique déjà le
  RBAC + le log. Aucun chemin de modification direct via 203-205.

## 6. RBAC & commandes admin

Réutilise l'infra existante (`AdminCommandHandler`, `slash_commands.json`,
`docs/slash_commands_rbac.md`). **Aucun nouvel opcode admin.** On ajoute 3 slash
commands, **rôle requis = `admin`** :

| Commande | Effet (master) |
|---|---|
| `/settime HH:MM` | calcule l'`offsetGameSec` pour que `timeOfDay == HH:MM` maintenant. |
| `/pausetime on\|off` | bascule `paused` (fige/reprend, en mémorisant `pausedAtGameSec`). |
| `/settimescale <minutes réelles par jour>` | change `timeScale` (borné, ex. [1, 1440]). |

Chaque commande, **après vérification RBAC `admin` côté master** (un client sans
le droit reçoit `Unauthorized`, comme les autres commandes admin) :
1. mute le `WorldClockState` master,
2. **broadcaste** une `WorldClockChangeNotification` (205) à tous les clients,
3. est **loggée côté serveur** (conforme à la règle « commandes admin loggées »).

## 7. Intégration client

- `DayNightCycle` **n'avance plus en autonomie** (`Advance(dt)` libre supprimé du
  pilotage) : son `timeOfDay` (et la phase de lune) sont **calculés depuis
  l'horloge monde synchronisée**. Le calcul reste local chaque frame (fluide).
- **Au login / entrée monde** : le client envoie `WorldClockStateRequest` (203),
  reçoit l'état, cale son horloge de référence (`localUnixMs ↔ serverTimeUnixMs`).
- **Sur `WorldClockChangeNotification` (205)** : re-cale immédiatement (un admin a
  modifié le temps).
- **Contrôle de dérive** : toutes les **N minutes** (config, défaut 5), renvoie un
  `WorldClockStateRequest` léger ; si l'écart entre l'heure calculée localement et
  l'heure serveur dépasse un **seuil** (config, ex. 30 s de jeu), corrige en
  **douceur** (lerp sur quelques secondes) au lieu de sauter. Sinon, ne fait rien.
- **Fallback hors-ligne / pas de réponse** : si le client n'a jamais reçu d'état
  (mode solo / serveur muet), il retombe sur la formule par défaut du `config.json`
  (comportement actuel) — pas de régression en l'absence de serveur.

## 8. Conséquence de l'unification lune (risque principal)

Aujourd'hui la lune avance en **temps réel** (cycle de 14 jours réels, indépendant
du `timeScale`). En la dérivant de l'horloge monde (temps de **jeu**), sa cadence
devient liée aux **jours de jeu** et **accélère avec `timeScale`**. C'est plus
cohérent (soleil + lune même base) mais **décale le timing lunaire existant**.

Le spec impose donc, côté refactor lunaire :

1. Exprimer `lunarPeriodGameSec` en **jours de jeu** (proposition : **16 jours de
   jeu** = 16 phases, soit ~16 h réelles à `timeScale` 60). Valeur dans `config.json`.
2. **`LunarCalendar` / `LunarHandler`** recalculent la phase depuis l'horloge monde
   (`gameSec`) au lieu de leur epoch/durée temps-réel propres. Les opcodes lunaires
   192-194 peuvent être **conservés** (le master continue de pousser les
   changements de phase pour `GameEventManager` / affichage), mais la **source**
   devient l'horloge monde unifiée.
3. **`GameEventManager`** (festival lunaire, Phase 5) : vérifier que le déclenchement
   d'événements lunaires reste cohérent une fois la cadence en temps de jeu. Si un
   event doit rester calé sur le **calendrier réel**, le garder sur l'epoch réel
   (l'horloge monde expose `nowUnixMs`, donc les deux cadences restent calculables).

**Mitigation** : l'unification se fait en gardant `LunarCalendar` comme **point de
calcul unique** (header-only) alimenté par `gameSec`. On ne supprime pas les
opcodes/handlers lunaires existants ; on rebranche leur **entrée** sur l'horloge
monde. Tests lunaires existants (`ObjectGuidTests`/`LunarCalendar`) à adapter.

## 9. Découpage en sous-chantiers (ordre d'implémentation)

1. **WorldClock master (état + formule + config)** — `WorldClockState`, lecture
   `config.json` (epoch, timeScale, lunarPeriodGameDays), calcul `gameSec`.
2. **Wire** — `WorldClockPayloads.h` + opcodes 203-205 + handler master
   (request→response, broadcast notification).
3. **Commandes admin** — 3 slash commands `admin` dans `slash_commands.json` +
   branchement `AdminCommandHandler` → mute état + broadcast + log.
4. **Refactor lunaire** — `LunarCalendar`/`LunarHandler` dérivent de `gameSec` ;
   adapter `GameEventManager` + tests.
5. **Client** — `DayNightCycle` piloté par l'horloge synchronisée ; sync login +
   notif + contrôle de dérive ; fallback hors-ligne.
6. **Shard** (si besoin) — lecture `gameSec` pour spawns/mécaniques.

## 10. Déploiement

⚠️ **REDÉPLOIEMENT SERVEUR REQUIS (master)** — nouveaux opcodes 203-205, nouveau
handler master, 3 commandes admin, refactor lunaire. **Client + serveur en
lock-step** : un client neuf parlant à un master ancien ne recevrait pas l'état
(→ fallback local), et un master neuf avec client ancien garderait l'ancien
comportement client-local. À déployer ensemble.
Pas de migration DB (overrides runtime-only).

## 11. Hors-scope (évolutions futures)

- **Persistance** des overrides admin (survie au reboot master) — explicitement
  écartée pour v1.
- **Météo** synchronisée serveur (même mécanisme, plus tard).
- Interpolation latence avancée (NTP-like) — v1 utilise un calage simple RTT/2.

## 12. Plan de test

- **Unitaire** (CI, sans GPU) : `gameSec` (epoch/scale/offset/paused), dérivation
  heure du jour + phase lunaire, `/settime` (offset calculé correct), bornes
  `/settimescale`. Adapter les tests lunaires.
- **RBAC** : un client non-admin reçoit `Unauthorized` sur `/settime` ; un admin
  réussit (test handler master).
- **En jeu** (manuel) : 2 clients voient la même heure ; `/settime 22:00` →
  bascule nuit chez tous ; `/pausetime on` → fige ; dérive corrigée en douceur ;
  lune cohérente avec le soleil.
