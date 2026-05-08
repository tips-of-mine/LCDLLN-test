# CMANGOS.14 — DBScripts (DSL data-driven / hot-reload)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.14_DBScripts_dsl_data_driven_hot_reload.md](../../../../tickets/CMANGOS/CMANGOS.14_DBScripts_dsl_data_driven_hot_reload.md)
> **Priorité** : P2 — gameplay essentiel (contenu narratif)
> **Cible** : shard

## 1. Statut implémentation

❌ **Absent** — pas de moteur d'événements scriptés DB-driven, pas de
DSL ~30 commandes, pas de tables `*_scripts`, pas de hot-reload.
ShaderHotReload existe pour les shaders uniquement (orthogonal).

## 2. Preuves dans le code

**Existant (orthogonal) :**
- [engine/render/ShaderHotReload.h](../../../../engine/render/ShaderHotReload.h) — hot-reload pour shaders rendu, sans
  rapport avec DBScripts

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/scripts/` — dossier inexistant
- ❌ `ScriptCommand` enum (~30 commandes : `SCRIPT_TALK`,
  `SCRIPT_MOVE_TO`, `SCRIPT_CAST_SPELL`, etc.)
- ❌ `ScriptTargetType` enum
- ❌ `ScriptEntry` struct + `ScriptInstance` runtime
- ❌ `ScriptMgr` singleton (Load/Run/Tick/ReloadAll)
- ❌ Tables `event_scripts`, `quest_start_scripts`, `quest_end_scripts`,
  `gameobject_use_scripts`, `spell_scripts`
- ❌ Migration DB
- ❌ Hot-reload `.reload all_scripts` GM command
- ❌ Config `scripts.max_concurrent_instances`,
  `scripts.tick_interval_ms`, `scripts.log_command_execution`

## 3. Recouvrement milestones existantes

❌ **Non couvert** — aucune milestone scripts narratifs dans M00-M44.

## 4. Écart par rapport à la spec CMANGOS

100% absent. C'est un **multiplicateur de productivité** pour les
designers de contenu : sans DBScripts, chaque PNJ avec dialogue scripté,
chaque cinématique de quête, chaque trigger de zone demande C++ et
recompil. Avec DBScripts, un designer scripte en SQL en minutes.

Le DSL ~30 commandes est **délibérément limité** (pas de Lua/JS) : c'est
suffisant pour 95% du contenu narratif, garde la simplicité, évite les
exploits scripting.

## 5. Effort estimé

**L** (1 sprint complet) :
- Enums commandes + targets + struct ScriptEntry + tests
- Migration DB (5 tables, schéma identique)
- ScriptMgr load/run/tick/reload
- Implémentation des ~30 commandes (lot de petits handlers)
- Hot-reload commande GM (dépend `ChatCommandRouter` CMANGOS.01)
- Tests par commande + test reload (modif SQL → reload → comportement
  changé live)

Pas de wire-breaking. Migration DB substantielle (5 tables) mais
schéma identique = factorisable.

## 6. Valeur joueur/serveur

**Élevée → Critique pour le contenu** — sans DBScripts, le contenu PvE
narratif est très coûteux à produire (chaque interaction = code C++).
Avec DBScripts, on découple production contenu / production moteur.

Pas critique tant qu'aucun designer n'écrit du contenu PvE.

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.13 Database** — `SQLStorage` pour cache des scripts au boot
- **CMANGOS.07 AI** — un script peut être déclenché par un event AI
- **CMANGOS.16 Globals/Conditions** — `condition_id` filtre l'exécution
- **CMANGOS.01 Chat** — commande `.reload` GM

→ **CMANGOS.14 dépend de 4 tickets P1/P2 amont**. Pas le premier à
attaquer dans la chaîne P2.

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — 5 nouvelles tables à schéma quasi-identique.
  Idempotent, simple.
- ⚠️ **Boucles infinies** — un script qui se déclenche lui-même via
  une de ses commandes. Mécanisme de profondeur max + détection
  cycles requis.
- ⚠️ **Hot-reload thread-safety** — le swap des scripts en plein tick
  doit être atomique (`shared_ptr` swap). Si un `ScriptInstance` en
  cours référence l'ancien `ScriptEntry`, garder l'ancien jusqu'à
  fin de l'instance.
- ⚠️ **DSL extensibilité** — chaque nouvelle commande = code C++ + bump
  enum. Convention claire pour ajouter (ex: numéros réservés par lot
  de 10 par catégorie).
- ⚠️ **Logging volumétrie** — `log_command_execution=true` peut
  générer des Mo/sec. À filtrer par `LogFilter::Scripts` (à ajouter ?).
- ⚠️ **Délais cumulés** — un script avec 10 commandes × 5s = 50s.
  Si target meurt entre-temps → comportement à définir (abort, skip,
  re-target).
- ⚠️ **Sécurité commandes GM** — certaines commandes (TELEPORT,
  REMOVE_AURA) sont sensibles. `condition_id` doit pouvoir restreindre
  selon rôle (Player target only, etc.).
- Pas de wire-breaking côté protocole (server-only).

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** CMANGOS.13 Database (pré-requis
SQLStorage) et **après** CMANGOS.07 AI + CMANGOS.16 Conditions :

1. **Étape 0** : valider que le **contenu PvE narratif** est sur la
   roadmap court-terme. Si pas avant 6+ mois, **reporter**.
2. **Étape 1** : enums + struct + migration DB (5 tables) + tests.
3. **Étape 2** : ScriptMgr Load/Run/Tick + 5-10 commandes les plus
   utiles (TALK, MOVE_TO, CAST_SPELL, SUMMON, TELEPORT, DESPAWN,
   KILL_CREDIT, RESPAWN_GO, OPEN_DOOR, EMOTE).
3. **Étape 3** : ajouter les 20+ commandes restantes au fil des besoins
   contenu.
4. **Étape 4** : hot-reload GM (dépend ChatCommandRouter livré).
5. **Étape 5** : doc designer (table de référence des commandes +
   exemples).

⏸ **Reporter** tant que le contenu PvE narratif n'est pas amorcé. Sinon
livrable invisible (pas de scripts à exécuter), debt en attente.

---

*Audit du 2026-05-08. Mises à jour : —*
