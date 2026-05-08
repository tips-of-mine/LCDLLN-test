# CMANGOS.42 — Weather (Markov / zones / authoritative)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.42_Weather_markov_zones_authoritative.md](../../../../tickets/CMANGOS/CMANGOS.42_Weather_markov_zones_authoritative.md)
> **Priorité** : P3 — ajout à valeur (ambiance)
> **Cible** : shard

## 1. Statut implémentation

🟡 **Partiel** — M100.26 (Weather System and Dynamic Surface Modifiers)
+ M100.28 (Gameplay Zones and Weather Zones) couvrent **côté éditeur
+ rendu**. Le système **server-authoritative** avec Markov chain
+ broadcast cohérent multi-joueurs côté **shard** est probablement absent.

## 2. Preuves dans le code

**Existant :**
- M100.26 — Weather System and Dynamic Surface Modifiers (éditeur +
  rendu)
- M100.28 — Gameplay Zones and Weather Zones (délimitation)
- M100.27 — Shade Map and Thermal Map (effets sol)

**Manquant (vs spec ticket — côté serveur) :**
- ❌ `engine/server/shard/weather/`
- ❌ `WeatherMgr` server-authoritative
- ❌ Markov chain par zone (rotation aléatoire pondérée)
- ❌ Table DB `game_weather` (zone, season, type, chance)
- ❌ Broadcast cohérent multi-joueurs (opcode + grade fade)
- ❌ Update lazy 10min par zone occupée

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement, côté client/éditeur)** — M100.26-28 livrent
le **rendu** météo + zones définies. Reste à câbler le **driver
serveur** authoritative.

## 4. Écart par rapport à la spec CMANGOS

L'écart est entre **rendu client** (déjà fait par M100) et **logique
serveur** (à porter de cmangos). Les deux côtés doivent se rejoindre :
le serveur décide quel weather, le client affiche.

## 5. Effort estimé

**S-M** (1-2 PR) — Markov chain simple + broadcast. Réutilise
infrastructure M100 côté client. Wire-breaking probable (opcode weather
update client).

## 6. Valeur joueur/serveur

**Moyenne** — feature ambiance. Server-authoritative = cohérence
multi-joueurs ("regarde la pluie là-bas"). Critique pour immersion
mais polish.

## 7. Dépendances bloquantes

- **CMANGOS.13 Database** — table `game_weather`
- **CMANGOS.03 Grids** — savoir quelles zones occupées
- M100.26+28 livrés (côté client + zones)

## 8. Risque / piège ⚠️

- ⚠️ Wire-breaking — opcode `WEATHER_UPDATE`. Bump.
- ⚠️ Migration DB simple (1 table) — idempotent
- ⚠️ Markov chain calibration — chaque zone a ses pondérations.
  Beaucoup de tuning post-launch.
- ⚠️ Coordination M100.26-28 — éviter doublon driver serveur (LCDLLN
  may already have weather state in M100, à auditer).
- ⚠️ Désync client/serveur — si client se connecte en plein weather,
  envoyer state actuel au login.

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** audit M100.26-28 :

1. **Étape 0 (audit)** : examiner ce que M100.26 et M100.28 livrent
   exactement. Probable : rendu OK, driver serveur manquant.
2. **Étape 1** : `WeatherMgr` shard + table DB + Markov chain.
3. **Étape 2** : opcode broadcast + intégration client (réutilise
   M100.26 rendering).
4. **Étape 3** : update lazy 10min par zone occupée.

À planifier après livraison M100.26-28 si pas déjà couvert. Effort
faible si M100 a fait le gros du travail rendu.

---

*Audit du 2026-05-08. Mises à jour : —*
