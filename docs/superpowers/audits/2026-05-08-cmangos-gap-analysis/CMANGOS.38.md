# CMANGOS.38 — PlayerBot (headless session / load test)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.38_PlayerBot_headless_session_load_test.md](../../../../tickets/CMANGOS/CMANGOS.38_PlayerBot_headless_session_load_test.md)
> **Priorité** : P3 — ajout à valeur (outil dev/QA)
> **Cible** : shard

## 1. Statut implémentation

🟡 **Partiel** — `tools/load_tester/` existe (M25.1, M40.3) pour TLS
connections + stress test, mais c'est un **client mock externe**, pas
des `WorldSession` headless intégrées dans le shard comme dans le
ticket cmangos.

## 2. Preuves dans le code

**Existant :**
- [tools/load_tester/LoadTester.h](../../../../tools/load_tester/LoadTester.h) + `.cpp` — load tester externe
- M25.1 — Load tester TLS connections 1000+
- M40.3 — Load testing concurrent connections + stress test

**Manquant (vs spec ticket) :**
- ❌ Mode `--headless-bot` dans le shard
- ❌ `WorldSession headless` réutilisant les vrais handlers
- ❌ Bots IA stratégie data-driven

## 3. Recouvrement milestones existantes

✅ **Couvert (autre approche)** — load_tester externe couvre une partie
du besoin (test charge réseau). PlayerBot cmangos teste plus profond
(handlers gameplay).

## 4. Écart par rapport à la spec CMANGOS

L'écart est **architectural** : load_tester teste le réseau et l'auth
(connexions), PlayerBot testerait le **gameplay** (combat, mouvement,
chat, AH, trade) sans rendu client.

Les deux sont complémentaires : load_tester pour saturation auth/relay,
PlayerBot pour saturation tick shard.

## 5. Effort estimé

**M-L** (2-3 PR) — réutilisation handlers existants + IA bot simple.

## 6. Valeur joueur/serveur

**Moyenne (pré-launch) → Élevée (avant scaling)** — invisible joueur,
**critique** pour valider la capacité shard sans 1000 vrais clients.

## 7. Dépendances bloquantes

- **CMANGOS.02 Entities** — Player headless
- **CMANGOS.04 Movement** — bot mouvement
- **CMANGOS.07 AI** — bot stratégie
- WorldSession existante (déjà OK côté LCDLLN)

## 8. Risque / piège ⚠️

- ⚠️ **Pas pour prod** — gate du mode `--headless-bot` par flag config
  + assertion pas de DB prod.
- ⚠️ **Comportement bot vs vrai client** — bypass certains chemins
  (sérialisation). Pas un test 100% representative.
- ⚠️ **Load synthétique non représentatif** — bots pattern uniforme,
  vrais joueurs hétérogènes. Patterns variés à scripter.
- ⚠️ **Ressources DB** — N bots × M operations DB peut saturer le pool.

## 9. Recommandation finale

⏸ **Reporter** — pas avant que la chaîne P1 + .07 AI + .04 Movement
soient livrés. À considérer avant le scale-up final.

Le `load_tester` existant suffit pour le pré-launch. PlayerBot devient
utile quand on veut valider 5000+ joueurs sur un shard.

---

*Audit du 2026-05-08. Mises à jour : —*
