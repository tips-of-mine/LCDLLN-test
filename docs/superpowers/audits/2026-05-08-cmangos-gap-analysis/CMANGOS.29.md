# CMANGOS.29 — Anticheat (plugin interface / position deltas)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.29_Anticheat_plugin_interface_position_deltas.md](../../../../tickets/CMANGOS/CMANGOS.29_Anticheat_plugin_interface_position_deltas.md)
> **Priorité** : P3 — ajout à valeur
> **Cible** : shard

## 1. Statut implémentation

🟡 **Partiel** — `BotDetector` LCDLLN existe (anti-bot lors de l'auth),
mais pas d'interface plugin `IServerSideValidator` pour validation
gameplay (deltas position, fly-hack, speed-hack).

## 2. Preuves dans le code

**Existant :**
- [engine/server/BotDetector.h](../../../../engine/server/BotDetector.h) + `.cpp` — détection bots côté auth
- [engine/server/AntiBotTests.cpp](../../../../engine/server/AntiBotTests.cpp) — tests
- [engine/server/CaptchaVerifier.cpp](../../../../engine/server/CaptchaVerifier.cpp) — captcha auth
- M33.3 — Security rate limiting + anti-bot measures

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/anticheat/` — dossier inexistant
- ❌ `IServerSideValidator` interface plugin
- ❌ `MoveValidator` (delta position vs vitesse théorique)
- ❌ `FlyHackDetector` (height check via vmap)
- ❌ `SpeedHackDetector` (vitesse réelle vs max théorique)
- ❌ Hook chat commands intercept
- ❌ Loader plugins runtime

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement)** — anti-bot auth en place. Anti-cheat
gameplay (deltas, fly, speed) absent.

## 4. Écart par rapport à la spec CMANGOS

L'écart majeur : **anti-cheat gameplay**, pas auth. Sans ça, exploits
mouvement (fly, speed, no-clip) passent.

## 5. Effort estimé

**M** (2 PR) — interface plugin + 1-2 validators (move delta + height
fly-hack). Pas de wire-breaking.

## 6. Valeur joueur/serveur

**Moyenne → Élevée** — invisible joueur sain, **critique** dès qu'un
exploiter apparaît. Réserver l'interface tôt évite refonte ultérieure.

## 7. Dépendances bloquantes

- **CMANGOS.04 Movement** — vitesse théorique via `MoveSpline`
- **CMANGOS.05 vmap** — height check anti-fly
- **CMANGOS.01 Chat** — hook intercept commands

## 8. Risque / piège ⚠️

- ⚠️ **Faux positifs** — joueur lag pic = vitesse > max sur 1 tick.
  Buffer + tolérance + seuils calibrés.
- ⚠️ **Plugin loader** — DLL/SO runtime ? Sécurité (signature) ?
  Statique compile-time = simple, dynamic = risque.
- ⚠️ **Performance** — validation par tick × N joueurs. Cache + fast-path.
- ⚠️ **Bypass via lag triggering** — cheater peut simuler lag. Détection
  patterns inclus dans validator.

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** CMANGOS.04 + .05 livrés :

1. **Étape 1** : `IServerSideValidator` interface (architecture
   uniquement, pas de validator).
2. **Étape 2** : `MoveValidator` delta position + buffer + tolérance.
3. **Étape 3** : `FlyHackDetector` via `VMapManager::GetHeight`.
4. **Étape 4** : hook chat (futur quand abuse détecté).

Reporter tant que pas d'exploits visibles. Mais réserver l'interface
sans coût.

---

*Audit du 2026-05-08. Mises à jour : —*
