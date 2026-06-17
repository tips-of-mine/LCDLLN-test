# Éclatement de `config.json` par rôle — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Éclater le fourre-tout `config.json` (722 lignes) en fichiers à responsabilité unique — config serveur (`config/server.config.json`, non livrée au client), contenu de zone multi-zones (`game/data/zones/<zone>/`), et config cimetière par-zone — sans casser un seul call-site C++.

**Architecture:** Le `Config` est un store **plat à clés pointées** qui fusionne plusieurs fichiers (`LoadFromFile` successifs, override). On déplace des blocs vers d'autres fichiers chargés en plus, **en conservant les préfixes** (`world.scenery.*`, `db.*`…), donc aucun `GetX("...")` ne change. On ajoute 2 helpers testables dans `Config` (chargement zone + chargement config serveur) et on câble chaque binaire. Livré en **4 PRs stackées, server-first, déploiement lock-step**.

**Tech Stack:** C++20, `engine::core::Config` (`src/shared/core/Config.{h,cpp}`), CMake + CTest (exécutables de test standalone), Docker Compose (déploiement serveur).

---

## Contraintes d'environnement (À LIRE AVANT DE COMMENCER)

- **Pas de build local** : `cmake`/MSVC/vcpkg absents des shells. **On ne compile/teste jamais en local.** La vérification se fait :
  - soit en poussant la branche → la CI GitHub `build-linux.yml` lance `ctest` ;
  - soit en ouvrant la solution dans Visual Studio.
  Les étapes « Run test » du plan signifient donc **« vérifier via CI/VS »**, pas un appel shell local.
- **Convention** : commentaires en **français**, nouveau code/fichiers en **PascalCase**, docs en kebab-case.
- **`Config.{h,cpp}` est déjà compilé côté client (engine_core) ET serveur** (server_app). En ajoutant les helpers **dans `Config.cpp`** (pas de nouveau .cpp), aucun ajout CMake n'est nécessaire pour le code de prod. Seuls les **exécutables de test** ajoutent des cibles CMake.
- **Dédup avant livraison** : avant de créer un helper, vérifier qu'il n'existe pas déjà (`grep`).

---

## Structure des fichiers (vue d'ensemble)

| Fichier | Action | Responsabilité |
|---|---|---|
| `src/shared/core/Config.h` | Modifier | Déclare `LoadServerConfig`, `LoadActiveZone` |
| `src/shared/core/Config.cpp` | Modifier | Implémente les 2 helpers |
| `src/shared/core/tests/ConfigServerLoadTests.cpp` | Créer | Test du chargement serveur |
| `src/shared/core/tests/ConfigZoneLoadTests.cpp` | Créer | Test du chargement de zone |
| `CMakeLists.txt` | Modifier | Enregistre les 2 nouveaux tests |
| `config.json` (racine) | Modifier | Retire `db/accounts/globals/chat` puis `world.*` (sauf `lunar`) ; ajoute `world.active_zone` |
| `config/server.config.json` | Créer | `db/accounts/chat` (dev local) |
| `deploy/docker/config/server.config.json` | Créer | `db/accounts/chat` (template versionné, interpolation `.env`) |
| `deploy/docker/config/{master,shard}.config.json` | Modifier | Retire `db/accounts/chat` |
| `deploy/docker/docker-compose.yml` | Modifier | Monte `server.config.json` sur master + shard |
| `src/masterd/main_linux.cpp` | Modifier | `LoadServerConfig` après `Config::Load` (l.163) |
| `src/shardd/main_linux.cpp` | Modifier | idem (l.48) |
| `src/shardd/main_win.cpp` | Modifier | idem (l.101) |
| `src/shared/server_bootstrap/main.cpp` | Modifier | idem (l.89) |
| `game/data/zones/feyhin/zone.json` | Créer | `world.*` contenu + réglages (hors scenery/lunar) |
| `game/data/zones/feyhin/scenery.json` | Créer | `world.scenery.*` (340+ instances) |
| `game/data/zones/feyhin/respawn_points.txt` | Créer | cimetières/auberges par-zone (déplacé) |
| `src/client/app/Engine.cpp` | Modifier | `LoadActiveZone` après `Config::Load` (l.1218) |
| `src/world_editor/main.cpp` | Modifier | `LoadActiveZone` de la zone active |
| `src/shared/server_bootstrap/ServerApp.cpp` | Modifier | Résoudre `respawn_points.txt` depuis la zone active (l.1929) |
| `CODEBASE_MAP.md` | Modifier | Section config mise à jour |

---

# PR 1 — Extraction de `config/server.config.json` (server-first)

**But :** sortir `db/accounts/chat` de `config.json` (racine = client) vers un fichier serveur dédié, chargé uniquement par les binaires serveur, monté sur master **et** shard (déduplique `db`).

## Task 1.1 : Helper `Config::LoadServerConfig` (TDD)

**Files:**
- Modify: `src/shared/core/Config.h`
- Modify: `src/shared/core/Config.cpp`
- Test: `src/shared/core/tests/ConfigServerLoadTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Écrire le test qui échoue**

Créer `src/shared/core/tests/ConfigServerLoadTests.cpp` :

```cpp
/// Tests CPU du chargement de la config serveur dédiée (PR1 — éclatement config.json).
/// Vérifie que LoadServerConfig fusionne les clés serveur (db/accounts/chat) dans le
/// Config existant, et renvoie false proprement si le fichier est absent.
#include "src/shared/core/Config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

	using engine::core::Config;

	std::filesystem::path TempDir()
	{
		std::random_device rd; std::mt19937_64 rng(rd());
		auto d = std::filesystem::temp_directory_path() / ("lcdlln_srvcfg_" + std::to_string(rng()));
		std::filesystem::create_directories(d);
		return d;
	}

	void Test_LoadsServerKeys()
	{
		const auto dir = TempDir();
		const auto file = dir / "server.config.json";
		{
			std::ofstream out(file);
			out << R"({ "db": { "host": "db.example", "port": 3307, "password": "secret" },
			            "chat": { "gate": { "flood_max_messages": 9 } } })";
		}
		Config cfg;
		REQUIRE(Config::LoadServerConfig(cfg, dir.string()));
		REQUIRE(cfg.GetString("db.host", "") == "db.example");
		REQUIRE(cfg.GetInt("db.port", 0) == 3307);
		REQUIRE(cfg.GetString("db.password", "") == "secret");
		REQUIRE(cfg.GetInt("chat.gate.flood_max_messages", 0) == 9);
		std::error_code ec; std::filesystem::remove_all(dir, ec);
	}

	void Test_MissingFileReturnsFalse()
	{
		const auto dir = TempDir();
		Config cfg;
		REQUIRE(Config::LoadServerConfig(cfg, dir.string()) == false);
		std::error_code ec; std::filesystem::remove_all(dir, ec);
	}
}

int main()
{
	Test_LoadsServerKeys();
	Test_MissingFileReturnsFalse();
	if (g_failed == 0) { std::printf("[PASS] ConfigServerLoadTests\n"); return 0; }
	std::printf("[FAIL] ConfigServerLoadTests: %d failure(s)\n", g_failed);
	return 1;
}
```

- [ ] **Step 2 : Enregistrer le test dans CMake**

Dans `CMakeLists.txt`, à la suite du bloc `config_save_to_file_tests` (~l.1402-1405), ajouter :

```cmake
add_executable(config_server_load_tests src/shared/core/tests/ConfigServerLoadTests.cpp)
target_include_directories(config_server_load_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(config_server_load_tests PRIVATE engine_core)
add_test(NAME config_server_load_tests COMMAND config_server_load_tests)
```

- [ ] **Step 3 : Déclarer le helper dans `Config.h`**

Dans `src/shared/core/Config.h`, dans la classe `Config` (section méthodes statiques, près de `Load`), ajouter :

```cpp
/// Charge la config SERVEUR dédiée (`<configDir>/server.config.json`) par-dessus
/// `cfg` (override des clés existantes : db/accounts/chat). Réservé aux binaires
/// serveur — jamais appelé côté client. Renvoie false si le fichier est absent
/// ou JSON invalide (le serveur garde alors ses defaults).
/// \param configDir répertoire contenant server.config.json (ex. "config").
static bool LoadServerConfig(Config& cfg, std::string_view configDir);
```

- [ ] **Step 4 : Implémenter dans `Config.cpp`**

Dans `src/shared/core/Config.cpp`, après la définition de `Config::Load` (~l.519), ajouter :

```cpp
bool Config::LoadServerConfig(Config& cfg, std::string_view configDir)
{
	std::filesystem::path path = std::filesystem::path(std::string(configDir)) / "server.config.json";
	return cfg.LoadFromFile(path.string());
}
```

- [ ] **Step 5 : Vérifier (CI/VS)**

Pousser la branche → CI `build-linux` exécute `ctest`. Attendu : `config_server_load_tests ... Passed`.
(En VS : compiler la cible `config_server_load_tests` et l'exécuter, attendu `[PASS]`.)

- [ ] **Step 6 : Commit**

```bash
git add src/shared/core/Config.h src/shared/core/Config.cpp \
        src/shared/core/tests/ConfigServerLoadTests.cpp CMakeLists.txt
git commit -m "feat(config): helper LoadServerConfig + test (PR1 éclatement config.json)"
```

## Task 1.2 : Câbler les binaires serveur

**Files:**
- Modify: `src/masterd/main_linux.cpp:163`
- Modify: `src/shardd/main_linux.cpp:48`
- Modify: `src/shardd/main_win.cpp:101`
- Modify: `src/shared/server_bootstrap/main.cpp:89`

- [ ] **Step 1 : Master**

Dans `src/masterd/main_linux.cpp`, juste après la ligne 163
`engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);`, ajouter :

```cpp
	// Config serveur dédiée (db/accounts/chat) : jamais livrée au client. Montée en
	// Docker sur master ET shard (un seul fichier source → plus de duplication du bloc db).
	if (!engine::core::Config::LoadServerConfig(config, "config"))
	{
		LOG_WARN(Net, "[master] config/server.config.json absent : repli sur clés inline éventuelles");
	}
```

- [ ] **Step 2 : Shard (Linux)**

Dans `src/shardd/main_linux.cpp`, après la ligne 48 (même `Config::Load`), ajouter le **même** bloc que Step 1 (adapter le tag de log si nécessaire au contexte du fichier).

- [ ] **Step 3 : Shard (Windows)**

Dans `src/shardd/main_win.cpp`, après la ligne 101, ajouter le même bloc.

- [ ] **Step 4 : Bootstrap**

Dans `src/shared/server_bootstrap/main.cpp`, après la ligne 89, ajouter le même bloc.

- [ ] **Step 5 : Vérifier (CI/VS)**

Pousser → CI `build-linux` doit compiler master + shard + bootstrap sans erreur (le helper est résolu). Attendu : build vert.

- [ ] **Step 6 : Commit**

```bash
git add src/masterd/main_linux.cpp src/shardd/main_linux.cpp \
        src/shardd/main_win.cpp src/shared/server_bootstrap/main.cpp
git commit -m "feat(server): charge config/server.config.json au boot (master/shard/bootstrap)"
```

## Task 1.3 : Créer les fichiers serveur et retirer les blocs de `config.json`

**Files:**
- Create: `config/server.config.json`
- Create: `deploy/docker/config/server.config.json`
- Modify: `config.json` (retire `db`, `accounts`, `chat` ; `globals` traité en PR3)
- Modify: `deploy/docker/config/master.config.json` (retire `db`, `accounts`, `chat`)
- Modify: `deploy/docker/config/shard.config.json` (retire `db`, `accounts`, `chat`)
- Modify: `deploy/docker/docker-compose.yml`

- [ ] **Step 1 : Créer `config/server.config.json` (dev local)**

Y déplacer **verbatim** les blocs `db`, `accounts`, `chat` actuellement dans `config.json` (racine), enveloppés dans un objet JSON :

```json
{
    "db": { "...": "contenu du bloc db de config.json" },
    "accounts": { "...": "contenu du bloc accounts" },
    "chat": { "...": "contenu du bloc chat" }
}
```

(Copier le contenu réel depuis `config.json` lignes ~671-697. Conserver les commentaires `_comment_*`.)

- [ ] **Step 2 : Créer `deploy/docker/config/server.config.json`**

Y déplacer les **mêmes** blocs `db/accounts/chat` actuellement présents dans `deploy/docker/config/master.config.json` (qui utilisent l'interpolation `.env` — conserver la syntaxe `${...}` telle quelle). C'est la source unique partagée master+shard.

- [ ] **Step 3 : Retirer `db/accounts/chat` de `config.json` racine**

Supprimer les blocs `db`, `accounts`, `chat` de `config.json` (laisser `globals` pour la PR3). Vérifier que le JSON reste valide (virgules).

- [ ] **Step 4 : Retirer `db/accounts/chat` de master.config.json et shard.config.json**

Dans `deploy/docker/config/master.config.json` et `deploy/docker/config/shard.config.json`, supprimer les blocs `db`, `accounts`, `chat` (désormais dans `server.config.json` monté).

- [ ] **Step 5 : Monter `server.config.json` dans le compose**

Dans `deploy/docker/docker-compose.yml`, service **master** (après la ligne 94 `./config/master.config.json:/app/config.json:ro`), ajouter :

```yaml
      - ./config/server.config.json:/app/config/server.config.json:ro
```

Service **shard** (après la ligne 135 `./config/shard.config.json:/app/config.json:ro`), ajouter la **même** ligne.

- [ ] **Step 6 : Vérifier la cohérence JSON (CI/VS)**

Pousser → CI verte. Aucun test unitaire ne couvre le contenu réel (données de déploiement) ; la validation porte sur : build vert + le test `config_server_load_tests` (PR1.1) toujours vert.

> ⚠️ Validation manuelle au déploiement : démarrer la stack Docker, vérifier que master et shard se connectent à la DB (logs `[master]`/`[shard]` sans erreur DB) et que les stats perso s'affichent (cf. bug `shard_db_config_gap`).

- [ ] **Step 7 : Commit**

```bash
git add config/server.config.json deploy/docker/config/server.config.json \
        config.json deploy/docker/config/master.config.json \
        deploy/docker/config/shard.config.json deploy/docker/docker-compose.yml
git commit -m "feat(config): extrait db/accounts/chat dans server.config.json (master+shard)"
```

---

# PR 2 — Extraction du contenu de zone (client/éditeur)

**But :** déplacer le bloc `world.*` (sauf `world.lunar`) vers `game/data/zones/feyhin/{zone.json,scenery.json}`, ajouter `world.active_zone`, et câbler client + éditeur. Stackée sur PR1.

## Task 2.1 : Helper `Config::LoadActiveZone` (TDD)

**Files:**
- Modify: `src/shared/core/Config.h`
- Modify: `src/shared/core/Config.cpp`
- Test: `src/shared/core/tests/ConfigZoneLoadTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Écrire le test qui échoue**

Créer `src/shared/core/tests/ConfigZoneLoadTests.cpp` :

```cpp
/// Tests CPU du chargement du contenu de zone (PR2 — éclatement config.json).
/// Vérifie que LoadActiveZone lit world.active_zone et fusionne
/// <contentRoot>/zones/<zone>/{zone.json,scenery.json} avec les préfixes world.* conservés.
#include "src/shared/core/Config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

	using engine::core::Config;

	std::filesystem::path TempRoot()
	{
		std::random_device rd; std::mt19937_64 rng(rd());
		return std::filesystem::temp_directory_path() / ("lcdlln_zone_" + std::to_string(rng()));
	}

	void Test_LoadsZoneAndScenery()
	{
		const auto root = TempRoot();
		const auto zoneDir = root / "zones" / "feyhin";
		std::filesystem::create_directories(zoneDir);
		{
			std::ofstream(zoneDir / "zone.json")
				<< R"({ "world": { "default_spawn": { "x": 12.0 }, "fog": { "start_m": 40.0 } } })";
			std::ofstream(zoneDir / "scenery.json")
				<< R"({ "world": { "scenery": { "count": 3 } } })";
		}
		Config cfg;
		cfg.SetValue("world.active_zone", Config::Value{ std::string("feyhin") });
		REQUIRE(Config::LoadActiveZone(cfg, root.string()));
		REQUIRE(cfg.GetInt("world.scenery.count", 0) == 3);
		REQUIRE(cfg.GetDouble("world.default_spawn.x", 0.0) == 12.0);
		REQUIRE(cfg.GetDouble("world.fog.start_m", 0.0) == 40.0);
		std::error_code ec; std::filesystem::remove_all(root, ec);
	}

	void Test_MissingZoneReturnsFalse()
	{
		const auto root = TempRoot();
		std::filesystem::create_directories(root);
		Config cfg;
		cfg.SetValue("world.active_zone", Config::Value{ std::string("absente") });
		REQUIRE(Config::LoadActiveZone(cfg, root.string()) == false);
		std::error_code ec; std::filesystem::remove_all(root, ec);
	}
}

int main()
{
	Test_LoadsZoneAndScenery();
	Test_MissingZoneReturnsFalse();
	if (g_failed == 0) { std::printf("[PASS] ConfigZoneLoadTests\n"); return 0; }
	std::printf("[FAIL] ConfigZoneLoadTests: %d failure(s)\n", g_failed);
	return 1;
}
```

- [ ] **Step 2 : Enregistrer le test dans CMake**

Après le bloc `config_server_load_tests` (PR1.1), ajouter :

```cmake
add_executable(config_zone_load_tests src/shared/core/tests/ConfigZoneLoadTests.cpp)
target_include_directories(config_zone_load_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(config_zone_load_tests PRIVATE engine_core)
add_test(NAME config_zone_load_tests COMMAND config_zone_load_tests)
```

- [ ] **Step 3 : Déclarer dans `Config.h`**

```cpp
/// Charge le contenu de la zone active par-dessus `cfg` : lit `world.active_zone`,
/// puis fusionne `<contentRoot>/zones/<zone>/zone.json` et `…/scenery.json` (préfixes
/// world.* conservés). Renvoie false si `world.active_zone` est vide ou si zone.json
/// est absent (le moteur garde alors ses defaults). scenery.json absent n'est pas fatal.
/// \param contentRoot racine du contenu (ex. valeur de `paths.content` = "game/data").
static bool LoadActiveZone(Config& cfg, std::string_view contentRoot);
```

- [ ] **Step 4 : Implémenter dans `Config.cpp`**

Après `LoadServerConfig` :

```cpp
bool Config::LoadActiveZone(Config& cfg, std::string_view contentRoot)
{
	const std::string zone = cfg.GetString("world.active_zone", "");
	if (zone.empty())
	{
		return false;
	}
	const std::filesystem::path zoneDir =
		std::filesystem::path(std::string(contentRoot)) / "zones" / zone;
	const bool zoneOk = cfg.LoadFromFile((zoneDir / "zone.json").string());
	// scenery.json optionnel (peut être absent sur une zone vide) : on n'échoue pas dessus.
	(void)cfg.LoadFromFile((zoneDir / "scenery.json").string());
	return zoneOk;
}
```

- [ ] **Step 5 : Vérifier (CI/VS)**

Pousser → `config_zone_load_tests ... Passed`.

- [ ] **Step 6 : Commit**

```bash
git add src/shared/core/Config.h src/shared/core/Config.cpp \
        src/shared/core/tests/ConfigZoneLoadTests.cpp CMakeLists.txt
git commit -m "feat(config): helper LoadActiveZone + test (PR2 éclatement config.json)"
```

## Task 2.2 : Câbler le client et l'éditeur

**Files:**
- Modify: `src/client/app/Engine.cpp` (après l.1218)
- Modify: `src/world_editor/main.cpp`

- [ ] **Step 1 : Client (Engine)**

`Engine.cpp:1218` charge `m_cfg` dans la liste d'initialisation. Dans le **corps du constructeur** (premières lignes, avant la première lecture `world.*` qui est à ~l.1152 — attention : si la lecture précède, déplacer le chargement de zone avant), ajouter :

```cpp
	// Contenu de la zone active (scenery, interactables, réglages de zone) : fusionné
	// par-dessus config.json. Les clés gardent leur préfixe world.* → call-sites inchangés.
	if (!engine::core::Config::LoadActiveZone(m_cfg, m_cfg.GetString("paths.content", "game/data")))
	{
		LOG_WARN(World, "[Engine] Zone active introuvable (world.active_zone='{}') : monde par défaut",
			m_cfg.GetString("world.active_zone", ""));
	}
```

> Vérifier l'ordre : ce chargement DOIT précéder toute lecture `m_cfg.GetX("world.…")` (ex. `Engine.cpp:1152`, `:5254`, `:12982`). Placer l'appel au tout début du corps du constructeur.

- [ ] **Step 2 : Éditeur**

Dans `src/world_editor/main.cpp`, après l'obtention du `Config` (recherche du `config.json`, ~l.63-76 puis le `Config::Load` correspondant), ajouter le même appel `LoadActiveZone(cfg, cfg.GetString("paths.content", "game/data"))` pour que l'éditeur charge la zone à éditer.

- [ ] **Step 3 : Vérifier (CI/VS)**

Pousser → build client + éditeur vert.

- [ ] **Step 4 : Commit**

```bash
git add src/client/app/Engine.cpp src/world_editor/main.cpp
git commit -m "feat(client/editor): charge la zone active après config.json"
```

## Task 2.3 : Créer les fichiers de zone et retirer `world.*` de `config.json`

**Files:**
- Create: `game/data/zones/feyhin/scenery.json`
- Create: `game/data/zones/feyhin/zone.json`
- Modify: `config.json` (retire `world.*` sauf `lunar` ; ajoute `world.active_zone`)

- [ ] **Step 1 : Créer `game/data/zones/feyhin/scenery.json`**

Déplacer **verbatim** le sous-objet `world.scenery` de `config.json` (le bloc `"scenery": { … }`, ~345 lignes) dans :

```json
{
    "world": {
        "scenery": { "...": "contenu verbatim du bloc world.scenery" }
    }
}
```

- [ ] **Step 2 : Créer `game/data/zones/feyhin/zone.json`**

Déplacer le **reste** du bloc `world` (tout sauf `scenery` ET sauf `lunar`) :

```json
{
    "world": {
        "max_draw_distance_m": 0.0,
        "props": { "...": "" },
        "impostor": { "...": "" },
        "fog": { "...": "" },
        "volfog": { "...": "" },
        "dof": { "...": "" },
        "interactables": { "...": "" },
        "test_water": { "...": "" },
        "default_spawn": { "...": "" },
        "zone_meta_path": "...",
        "probes_path": "...",
        "atmosphere_path": "...",
        "day_night": { "...": "" },
        "weather": { "...": "" }
    }
}
```

(Copier les valeurs réelles depuis `config.json`. Conserver les commentaires `_comment_*` correspondants.)

- [ ] **Step 3 : Nettoyer `config.json` racine**

Dans `config.json`, supprimer du bloc `world` tout **sauf `lunar`**. Conserver `world.lunar` (temps global, lu par le master). Ajouter, dans le bloc `world` restant, la clé de sélection :

```json
    "world": {
        "active_zone": "feyhin",
        "lunar": { "...": "contenu existant de world.lunar conservé" }
    },
```

Vérifier la validité JSON (virgules).

- [ ] **Step 4 : Vérifier (CI/VS + manuel)**

Pousser → CI verte (les tests `config_zone_load_tests` couvrent le mécanisme).

> ⚠️ Validation manuelle en jeu : lancer le client, vérifier que le décor (`scenery`), les interactibles (coffre/villageois), l'eau de test et le spawn s'affichent **comme avant**. Vérifier que le master lit toujours `world.lunar` (cycle lunaire correct).

- [ ] **Step 5 : Commit**

```bash
git add game/data/zones/feyhin/scenery.json game/data/zones/feyhin/zone.json config.json
git commit -m "feat(zones): extrait world.* vers game/data/zones/feyhin (sauf lunar)"
```

---

# PR 3 — Cimetières par-zone (`globals` → contenu de zone serveur)

**But :** ne pas perdre `globals` ; rattacher la config cimetière à la zone, côté serveur, en s'alignant sur `respawn_points.txt`. Stackée sur PR2.

## Task 3.1 : Confirmer les clés `globals` mortes vs vivantes

- [ ] **Step 1 : Grep de confirmation**

```bash
grep -rnE '"globals\.(default_locale|fallback_locale)' --include=*.cpp src
```

Attendu : aucun résultat → ces 2 clés sont mortes (à supprimer en Step suivant). `graveyard_default_faction_neutral_radius_m` est conservé (migré en zone).

- [ ] **Step 2 : Décider et noter**

Si le grep est vide : supprimer `default_locale`/`fallback_locale`. Sinon, les conserver dans `config/server.config.json` (créé en PR1) et documenter le lecteur.

## Task 3.2 : Déplacer `respawn_points.txt` sous la zone et y porter le rayon cimetière

**Files:**
- Create: `game/data/zones/feyhin/respawn_points.txt`
- Modify: `src/shared/server_bootstrap/ServerApp.cpp:1929`
- Modify: `config.json` (retire le bloc `globals`)

- [ ] **Step 1 : Créer le fichier respawn de zone**

Déplacer le `respawn_points.txt` existant (référencé par `server.respawn_points_path`, défaut `respawn/respawn_points.txt`) sous `game/data/zones/feyhin/respawn_points.txt`. Conserver le format ligne `graveyard|inn x z …`. Ajouter en tête un commentaire `# rayon neutre faction (m): <valeur de globals.graveyard_default_faction_neutral_radius_m>` pour traçabilité (le runtime multi-cimetières par distance reste un chantier futur, cf. spec §8).

- [ ] **Step 2 : Résoudre le chemin depuis la zone active**

Dans `src/shared/server_bootstrap/ServerApp.cpp`, `InitRespawnPoints()` (l.1925-1967), remplacer la résolution du chemin (l.1929) pour préférer la zone active si `world.active_zone` est défini :

```cpp
		// Respawn points de la zone active si défini (game/data/zones/<zone>/respawn_points.txt),
		// sinon repli sur l'ancien chemin global (rétro-compat).
		const std::string activeZone = m_config.GetString("world.active_zone", "");
		std::string relativePath;
		if (!activeZone.empty())
		{
			relativePath = m_config.GetString("paths.content", "game/data")
				+ "/zones/" + activeZone + "/respawn_points.txt";
		}
		else
		{
			relativePath = m_config.GetString("server.respawn_points_path", "respawn/respawn_points.txt");
		}
```

> Note : le serveur lit `world.active_zone` depuis son `config.json` (= master/shard.config.json). Ajouter `world.active_zone` dans ces fichiers de déploiement (Step 3).

- [ ] **Step 3 : Propager `world.active_zone` côté serveur + retirer `globals`**

- Dans `deploy/docker/config/master.config.json` et `shard.config.json`, ajouter `"world": { "active_zone": "feyhin" }` (le serveur ne charge pas le `config.json` racine).
- Dans `config.json` racine, supprimer le bloc `globals` (clés mortes retirées en 3.1 ; rayon cimetière porté dans le fichier respawn de zone).

- [ ] **Step 4 : Vérifier (CI/VS + manuel)**

Pousser → build serveur vert (`ServerApp.cpp` compile).

> ⚠️ Validation manuelle au déploiement : mourir en jeu, vérifier le respawn au cimetière/auberge de Feyhin (les marqueurs cimetière 120,120 / auberge 88,100 doivent rester cohérents).

- [ ] **Step 5 : Commit**

```bash
git add game/data/zones/feyhin/respawn_points.txt src/shared/server_bootstrap/ServerApp.cpp \
        deploy/docker/config/master.config.json deploy/docker/config/shard.config.json config.json
git commit -m "feat(zones): respawn/cimetière par-zone + retrait du bloc globals de config.json"
```

---

# PR 4 — Documentation et clôture

**But :** mettre à jour la carte du code et les notes de déploiement. Stackée sur PR3.

## Task 4.1 : Mettre à jour `CODEBASE_MAP.md`

**Files:**
- Modify: `CODEBASE_MAP.md` (section « 8. Configuration et build »)

- [ ] **Step 1 : Documenter le nouveau schéma**

Ajouter une sous-section décrivant les 3 familles de fichiers :
- `config.json` racine = config moteur/client partagée (+ `world.lunar`, `world.active_zone`) ;
- `config/server.config.json` (+ Docker `deploy/docker/config/server.config.json` monté master+shard) = `db/accounts/chat` ;
- `game/data/zones/<zone>/{zone.json,scenery.json,respawn_points.txt}` = contenu de zone.
Mentionner que les préfixes `world.*` sont conservés (call-sites inchangés) et le mécanisme `LoadActiveZone`/`LoadServerConfig`.

- [ ] **Step 2 : Commit**

```bash
git add CODEBASE_MAP.md
git commit -m "docs: CODEBASE_MAP — nouveau schéma de config éclaté (moteur/serveur/zone)"
```

---

## Note de déploiement (à inclure dans la description de CHAQUE PR)

> **Déploiement** : ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS** (master **et** shard) — `db/accounts/chat`
> relocalisés dans `server.config.json` (monté sur les deux), `main.cpp` serveur modifiés, et
> `respawn_points.txt` résolu par zone. Le **client** change aussi (charge les fichiers de zone)
> → **lock-step** client ↔ serveur. Ordre de merge : **PR1 → PR2 → PR3 → PR4** (CI verte à chaque
> étape), déploiement serveur + client ensemble après PR3.

---

## Self-review (couverture du spec)

- §4.1.1 config.json moteur/client + lunar + active_zone → PR2 Task 2.3 ✔
- §4.1.2 server.config.json (db/accounts/chat), master+shard, retiré des configs de rôle → PR1 ✔
- §4.1.3 zone.json + scenery.json → PR2 Task 2.3 ✔ ; cimetière par-zone serveur → PR3 ✔
- §4.2 world.active_zone + résolution → PR2 (client) + PR3 (serveur) ✔
- §4.3 chargement par binaire → PR1 Task 1.2 + PR2 Task 2.2 ✔
- §6 globals (mort vs graveyard) → PR3 Task 3.1/3.2 ✔
- §7 CI/CD (compose, templates, packaging) → PR1 Task 1.3 ✔ ; CODEBASE_MAP → PR4 ✔
- §9 critères d'acceptation 1-8 → couverts par PR1-4 ✔

Cohérence des signatures : `LoadServerConfig(Config&, std::string_view)` et
`LoadActiveZone(Config&, std::string_view)` utilisées de façon identique dans tests, header,
implémentation et call-sites. ✔
