# Cimetière par défaut par zone — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Le respawn au cimetière choisit, de façon déterministe et **indépendante de la position de mort** (client-autoritaire, donc spoofable → anti-triche), le **cimetière par défaut de la zone** du joueur (1er cimetière éligible par propriété de faction).

**Architecture:** Une règle d'éligibilité pure position-indépendante dans le header partagé `RespawnRules.h` ; `ServerApp::HandleRespawnRequest` remplace la sélection « cimetière le plus proche » par « 1er cimetière par défaut éligible de la zone ». L'auberge reste inchangée (le plus proche).

**Tech Stack:** C++17, header-only pur (RespawnRules), serveur UDP (ServerApp), tests `int main()` + macro `REQUIRE`, CMake. Pas de toolchain locale (build/ctest via CI).

**Spec :** [docs/superpowers/specs/2026-06-19-cimetiere-par-zone-design.md](../specs/2026-06-19-cimetiere-par-zone-design.md)

**Rappels projet :** commentaires **français** ; branche `claude/zone-default-graveyard` (worktree `flamboyant-yalow-fda4a5`). ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS** (change la sélection respawn) ; **pas de changement wire** (client compatible).

---

## Structure des fichiers

| Fichier | Action | Rôle |
|---------|--------|------|
| `src/shared/world/RespawnRules.h` | Modifier | Ajouter `IsGraveyardEligibleAsZoneDefault` (éligibilité par propriété seule, sans distance) |
| `src/shared/world/tests/RespawnRulesTests.cpp` | Modifier | Tests de la nouvelle règle (cible CI `respawn_rules_tests` déjà enregistrée) |
| `src/shared/server_bootstrap/ServerApp.cpp` | Modifier | `HandleRespawnRequest` : sélection cimetière = défaut de zone (position-indépendant) ; auberge inchangée |

> **Données** : aucune modification requise. La zone active (`feyhin`) a déjà son cimetière dans
> `game/data/zones/feyhin/respawn_points.txt` (`0 graveyard 120.0 1.5 120.0 -`), qui devient son
> défaut. Convention (forward-looking) : toute future zone jouable doit avoir ≥1 ligne `graveyard`
> dans son `respawn_points.txt` ; le **premier** cimetière éligible (ordre de fichier) est le défaut.
> `demo_plains` est un placeholder (`zone.meta` = `ZONE`), pas une zone de respawn → non concerné.

---

## Task 1 : Règle d'éligibilité position-indépendante (`RespawnRules`)

**Files:**
- Modify: `src/shared/world/RespawnRules.h`
- Test: `src/shared/world/tests/RespawnRulesTests.cpp`

- [ ] **Step 1 : Écrire les tests (échec attendu)**

Dans `src/shared/world/tests/RespawnRulesTests.cpp`, ajouter le `using` et une fonction de test, puis l'appeler depuis `main()`.

Après la ligne `using engine::world::IsGraveyardEligibleForRespawn;` (ligne 18), ajouter :
```cpp
	using engine::world::IsGraveyardEligibleAsZoneDefault;
```

Avant la fermeture du `namespace` anonyme (avant la ligne `}` qui précède `int main()`, ~ligne 56), ajouter :
```cpp
	void Test_ZoneDefaultEligibilityByOwnership()
	{
		// Cimetière neutre ("" ou "-") : défaut éligible pour tout le monde.
		REQUIRE(IsGraveyardEligibleAsZoneDefault("", "alliance") == true);
		REQUIRE(IsGraveyardEligibleAsZoneDefault("-", "horde") == true);
		REQUIRE(IsGraveyardEligibleAsZoneDefault("", "") == true);
		// Cimetière de faction : défaut éligible UNIQUEMENT pour sa faction (aucune
		// notion de distance/rayon — anti-triche : indépendant de la position).
		REQUIRE(IsGraveyardEligibleAsZoneDefault("alliance", "alliance") == true);
		REQUIRE(IsGraveyardEligibleAsZoneDefault("alliance", "horde") == false);
		REQUIRE(IsGraveyardEligibleAsZoneDefault("alliance", "") == false);
	}
```

Dans `main()`, après `Test_ZeroRadiusOnlyOwnerBeyondZero();` (ligne 64), ajouter :
```cpp
	Test_ZoneDefaultEligibilityByOwnership();
```

- [ ] **Step 2 : Vérifier l'échec de compilation**

Run: `ctest --test-dir build/vs2022-x64 -C Release -R respawn_rules_tests`
Expected: échec de build (`IsGraveyardEligibleAsZoneDefault` non déclarée). *(Build local indisponible → vérifié en CI.)*

- [ ] **Step 3 : Implémenter la règle dans le header**

Dans `src/shared/world/RespawnRules.h`, à la fin du `namespace engine::world` (après la fonction `IsGraveyardEligibleForRespawn`, avant la `}` du namespace), ajouter :
```cpp
	/// Éligibilité d'un cimetière comme DÉFAUT de zone, INDÉPENDANTE de la position
	/// (anti-triche : la position de mort est client-autoritaire, donc non fiable).
	///   - cimetière neutre (`graveyardFaction` vide ou "-") → éligible pour tous ;
	///   - sinon, éligible UNIQUEMENT pour sa faction propriétaire.
	/// Aucune notion de distance / rayon neutre (à la différence de
	/// IsGraveyardEligibleForRespawn, conservée pour l'ancien modèle « plus proche »).
	///
	/// \param graveyardFaction id de la faction propriétaire (vide/"-" = neutre).
	/// \param playerFaction    id de la faction du joueur (peut être vide).
	/// \return true si ce cimetière peut être le défaut de respawn du joueur.
	inline bool IsGraveyardEligibleAsZoneDefault(std::string_view graveyardFaction,
	                                             std::string_view playerFaction)
	{
		if (graveyardFaction.empty() || graveyardFaction == "-")
		{
			return true;
		}
		return graveyardFaction == playerFaction;
	}
```

- [ ] **Step 4 : Vérifier que les tests passent**

Run: `ctest --test-dir build/vs2022-x64 -C Release -R respawn_rules_tests --output-on-failure`
Expected: PASS — `[PASS] RespawnRulesTests`. *(Vérifié en CI.)*

- [ ] **Step 5 : Commit**
```bash
git add src/shared/world/RespawnRules.h src/shared/world/tests/RespawnRulesTests.cpp
git commit -m "feat(world): IsGraveyardEligibleAsZoneDefault (éligibilité cimetière sans position) + tests"
```
(Terminer par `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.)

---

## Task 2 : Sélection du cimetière par défaut de zone (`ServerApp::HandleRespawnRequest`)

**Files:**
- Modify: `src/shared/server_bootstrap/ServerApp.cpp` (`HandleRespawnRequest`, bloc lignes ~3159-3196)

- [ ] **Step 1 : Remplacer la sélection « plus proche » par « défaut de zone » pour le cimetière**

Lire `HandleRespawnRequest` autour des lignes 3157-3196. Le code actuel initialise `respawnX/Y/Z`
sur le point d'entrée puis, dans un bloc `{ ... }`, boucle sur `m_respawnPoints` en gardant le
**plus proche** (filtre faction par rayon neutre pour le cimetière).

Remplacer **uniquement le bloc interne** (de `{` ligne 3162 à `}` ligne 3196 — le bloc qui
contient `float bestDistSq = ...` jusqu'au `if (!found) { LOG_INFO(... fallback ...); }`) par :
```cpp
		{
			bool found = false;
			if (destination == kRespawnDestinationGraveyard)
			{
				// Cimetière PAR DÉFAUT de la zone : le PREMIER cimetière éligible par
				// propriété (neutre ou même faction), INDÉPENDANT de la position de mort.
				// La position (client->positionMeters) est client-autoritaire donc
				// spoofable : on ne l'utilise plus pour choisir le cimetière (anti-triche).
				for (const RespawnPointDefinition& point : m_respawnPoints)
				{
					if (point.zoneId != client->zoneId
						|| point.destinationType != kRespawnDestinationGraveyard)
					{
						continue;
					}
					if (!engine::world::IsGraveyardEligibleAsZoneDefault(
							point.ownerFactionId, client->factionId))
					{
						continue;
					}
					respawnX = point.positionMetersX;
					respawnY = point.positionMetersY;
					respawnZ = point.positionMetersZ;
					found = true;
					break; // 1er éligible (ordre de fichier) = défaut déterministe de la zone
				}
			}
			else
			{
				// Auberge (inn) : comportement INCHANGÉ — le plus proche du lieu de mort.
				float bestDistSq = std::numeric_limits<float>::max();
				for (const RespawnPointDefinition& point : m_respawnPoints)
				{
					if (point.zoneId != client->zoneId || point.destinationType != destination)
					{
						continue;
					}
					const float dxr = point.positionMetersX - client->positionMetersX;
					const float dzr = point.positionMetersZ - client->positionMetersZ;
					const float distSqR = dxr * dxr + dzr * dzr;
					if (distSqR < bestDistSq)
					{
						bestDistSq = distSqR;
						respawnX = point.positionMetersX;
						respawnY = point.positionMetersY;
						respawnZ = point.positionMetersZ;
						found = true;
					}
				}
			}
			if (!found)
			{
				LOG_INFO(Net, "[ServerApp] Respawn fallback to enter-world spawn (client_id={}, destination={}, zone_id={})",
					client->clientId, destination, client->zoneId);
			}
		}
```

Notes pour la revue statique :
- `respawnX/Y/Z` sont déclarés JUSTE AVANT ce bloc (lignes 3159-3161, initialisés sur
  `client->spawnPositionMeters*`) — on ne les redéclare pas, on les écrase dans le bloc. Le repli
  (aucun point trouvé) garde donc le point d'entrée en monde, comme avant.
- `engine::world::IsGraveyardEligibleAsZoneDefault` vient de Task 1 ; `RespawnRules.h` est **déjà
  inclus** dans ce fichier (l'ancien code appelait `IsGraveyardEligibleForRespawn`).
- `std::numeric_limits` / `<limits>` : déjà utilisés dans le bloc d'origine.
- `kRespawnDestinationGraveyard` / `kRespawnDestinationInn` : constantes déjà utilisées dans ce fichier.
- Le mot-clé `destination` (paramètre de `HandleRespawnRequest`) vaut graveyard ou inn ; seul le
  cimetière change de logique.

- [ ] **Step 2 : Vérifier qu'aucune dépendance à `client->positionMeters` ne subsiste pour le cimetière**

Run:
```bash
grep -n "positionMetersX\|positionMetersZ" src/shared/server_bootstrap/ServerApp.cpp | sed -n '1,40p'
```
Inspecter le voisinage de `HandleRespawnRequest` : dans la branche **graveyard**, il ne doit plus
y avoir d'usage de `client->positionMetersX/Z` (seule la branche inn — auberge — les utilise encore).
Expected: les usages restants près de la fonction sont dans la branche `else` (inn) et dans
l'écriture finale `client->positionMetersX = respawnX;`.

- [ ] **Step 3 : (build/ctest — SKIP, pas de toolchain locale ; validé en CI).**

- [ ] **Step 4 : Commit**
```bash
git add src/shared/server_bootstrap/ServerApp.cpp
git commit -m "feat(server): respawn cimetière = défaut de zone (déterministe, anti-triche position)"
```
(Terminer par `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.)

---

## Validation finale (en jeu — hors CI)

L'état serveur UDP n'est pas unit-testable simplement. À valider en jeu :
- [ ] Mourir à **plusieurs endroits** d'une même zone (loin / près de différents lieux) →
  réapparaître au cimetière « Cimetière le plus proche » TOUJOURS au **même** cimetière (le défaut
  de la zone), quelle que soit la position de mort.
- [ ] L'auberge (si un bouton/flux la propose) reste sur le **plus proche** (inchangé).
- [ ] Repli : dans une zone sans cimetière, on réapparaît au point d'entrée en monde (inchangé).

---

## Déploiement

> ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS (master/shard).** Changement de `ServerApp::HandleRespawnRequest`.
> **Pas de changement wire** (opcodes/payload respawn v13 inchangés) → un client ancien reste
> compatible ; mais le binaire serveur doit être rejoué pour appliquer la nouvelle sélection.
> **Aucun changement / redéploiement client.**
