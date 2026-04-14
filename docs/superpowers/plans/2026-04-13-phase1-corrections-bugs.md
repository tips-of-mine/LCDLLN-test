# Phase 1 — Corrections de bugs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Corriger tous les bugs identifiés dans le client et le serveur master de LCDLLN (superpositions UI, événements souris cassés, liste serveurs vide, logs non horodatés, etc.) selon le spec approuvé.

**Architecture:** Fixes par couche technique — serveur (auto-enregistrement, logs, bannière) → layout/rendu (superpositions, polices, popups info, dropdowns date) → événements souris → textes/labels. Chaque couche est commitée séparément.

**Tech Stack:** C++20, Vulkan UI custom (AuthUiPresenter/AuthGlyphPass), GLFW3, MySQL, MSVC, CMake

---

## Carte des fichiers

| Fichier | Rôle | Action |
|---------|------|--------|
| `db/migrations/0017_game_servers.sql` | Table de registre des serveurs de jeu | Créer |
| `engine/server/ServerRegistry.h` | Interface d'auto-enregistrement du master | Créer |
| `engine/server/ServerRegistry.cpp` | Implémentation DB (INSERT/UPDATE game_servers) | Créer |
| `engine/server/main.cpp` | Point d'entrée serveur : bannière + ServerRegistry | Modifier |
| `engine/server/ServerListHandler.cpp` | Utiliser ServerRegistry en plus de ShardRegistry | Modifier |
| `engine/core/Log.cpp` | Format horodatage `[JJ/MM/AAAA][HH:MM:SS]` | Modifier |
| `engine/client/AuthUi.cpp` | Fixes UI : superpositions, événements, lifecycle, labels | Modifier |
| `engine/client/AuthUi.h` | Nouveaux champs : popup info, dropdowns, phase email | Modifier |
| `engine/render/AuthUiRenderer.h` | Constante taille police champs | Modifier |
| `engine/render/AuthGlyphPass.cpp` | Rendu icône "i" + popup opaque | Modifier |
| `game/data/localization/fr/fr.json` | Nouvelles clés : popup info, label se souvenir, erreur générique | Modifier |
| `game/data/localization/en/en.json` | Idem en anglais | Modifier |

---

## Couche 1 — Serveur

### Task 1 : Migration DB — table game_servers

**Files:**
- Create: `db/migrations/0017_game_servers.sql`
- Create: `deploy/docker/db/migrations/0017_game_servers.sql`

- [ ] **Step 1 : Créer la migration**

```sql
-- 0017_game_servers.sql
-- Registre des serveurs de jeu disponibles.
-- Le master s'y inscrit au démarrage et s'y désactive à l'arrêt.

CREATE TABLE IF NOT EXISTS game_servers (
    server_id      INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name           VARCHAR(64)  NOT NULL,
    host           VARCHAR(128) NOT NULL,
    port           SMALLINT UNSIGNED NOT NULL,
    max_players    INT UNSIGNED NOT NULL DEFAULT 0,
    online_players INT UNSIGNED NOT NULL DEFAULT 0,
    status         ENUM('online','offline','maintenance') NOT NULL DEFAULT 'offline',
    last_heartbeat DATETIME NULL,
    created_at     DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY idx_game_servers_host_port (host, port)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
```

- [ ] **Step 2 : Copier dans deploy/docker**

```bash
cp db/migrations/0017_game_servers.sql deploy/docker/db/migrations/0017_game_servers.sql
```

- [ ] **Step 3 : Commit**

```bash
git add db/migrations/0017_game_servers.sql deploy/docker/db/migrations/0017_game_servers.sql
git commit -m "feat(db): migration 0017 — table game_servers pour registre des serveurs"
```

---

### Task 2 : ServerRegistry — auto-enregistrement du master

**Files:**
- Create: `engine/server/ServerRegistry.h`
- Create: `engine/server/ServerRegistry.cpp`

- [ ] **Step 1 : Créer `engine/server/ServerRegistry.h`**

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace engine::core { class Config; }

namespace engine::server
{
    /// Enregistre le serveur master dans la table game_servers au démarrage.
    /// Un seul enregistrement par (host, port) — UNIQUE KEY garantit l'idempotence.
    class ServerRegistry
    {
    public:
        ServerRegistry() = default;

        /// Lit name/host/port/max_players depuis config et fait un UPSERT dans game_servers.
        /// Retourne false si la connexion DB échoue (non bloquant : le serveur continue).
        bool RegisterSelf(const engine::core::Config& cfg);

        /// Passe le statut à 'offline' dans game_servers. Appelé à l'arrêt propre.
        void SetOffline();

        uint32_t GetServerId() const { return m_serverId; }
        const std::string& GetName() const { return m_name; }
        const std::string& GetHost() const { return m_host; }
        uint16_t GetPort() const { return m_port; }
        uint32_t GetMaxPlayers() const { return m_maxPlayers; }

    private:
        uint32_t    m_serverId   = 0;
        std::string m_name;
        std::string m_host;
        uint16_t    m_port       = 0;
        uint32_t    m_maxPlayers = 0;
        bool        m_registered = false;

        // Connexion MySQL (même pattern que MysqlAccountStore)
        // Déclarée comme void* pour éviter d'inclure mysql.h dans le header.
        void* m_conn = nullptr;

        bool Connect(const engine::core::Config& cfg);
        void Disconnect();
    };
}
```

- [ ] **Step 2 : Créer `engine/server/ServerRegistry.cpp`**

```cpp
#include "engine/server/ServerRegistry.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

// MySQL — même pattern que MysqlAccountStore.cpp
#include <mysql/mysql.h>

#include <cstdio>
#include <string>

namespace engine::server
{
    bool ServerRegistry::Connect(const engine::core::Config& cfg)
    {
        const std::string host   = cfg.GetString("database.host", "127.0.0.1");
        const std::string user   = cfg.GetString("database.user", "lcdlln");
        const std::string pass   = cfg.GetString("database.password", "");
        const std::string dbname = cfg.GetString("database.name", "lcdlln");
        const int         port   = static_cast<int>(cfg.GetInt("database.port", 3306));

        MYSQL* conn = mysql_init(nullptr);
        if (!conn)
        {
            LOG_ERROR(Core, "[ServerRegistry] mysql_init failed");
            return false;
        }
        if (!mysql_real_connect(conn, host.c_str(), user.c_str(), pass.c_str(),
                                dbname.c_str(), port, nullptr, 0))
        {
            LOG_ERROR(Core, "[ServerRegistry] mysql_real_connect failed: {}", mysql_error(conn));
            mysql_close(conn);
            return false;
        }
        m_conn = conn;
        return true;
    }

    void ServerRegistry::Disconnect()
    {
        if (m_conn)
        {
            mysql_close(static_cast<MYSQL*>(m_conn));
            m_conn = nullptr;
        }
    }

    bool ServerRegistry::RegisterSelf(const engine::core::Config& cfg)
    {
        m_name       = cfg.GetString("server.name", "Serveur Principal");
        m_host       = cfg.GetString("server.public_host", "127.0.0.1");
        m_port       = static_cast<uint16_t>(cfg.GetInt("server.listen_port", 3840));
        m_maxPlayers = static_cast<uint32_t>(cfg.GetInt("server.max_players", 1000));

        if (!Connect(cfg))
        {
            LOG_WARN(Core, "[ServerRegistry] RegisterSelf: DB unavailable — server list may be empty");
            return false;
        }

        MYSQL* conn = static_cast<MYSQL*>(m_conn);
        char sql[512]{};
        std::snprintf(sql, sizeof(sql),
            "INSERT INTO game_servers (name, host, port, max_players, online_players, status, last_heartbeat) "
            "VALUES ('%s', '%s', %u, %u, 0, 'online', NOW()) "
            "ON DUPLICATE KEY UPDATE "
            "name=VALUES(name), max_players=VALUES(max_players), online_players=0, "
            "status='online', last_heartbeat=NOW()",
            m_name.c_str(), m_host.c_str(), m_port, m_maxPlayers);

        if (mysql_query(conn, sql) != 0)
        {
            LOG_ERROR(Core, "[ServerRegistry] RegisterSelf INSERT failed: {}", mysql_error(conn));
            Disconnect();
            return false;
        }

        // Si INSERT (pas UPDATE), récupérer l'id généré.
        const uint64_t insertId = mysql_insert_id(conn);
        if (insertId != 0)
            m_serverId = static_cast<uint32_t>(insertId);
        else
        {
            // UPDATE : retrouver l'id existant.
            char selSql[256]{};
            std::snprintf(selSql, sizeof(selSql),
                "SELECT server_id FROM game_servers WHERE host='%s' AND port=%u",
                m_host.c_str(), m_port);
            if (mysql_query(conn, selSql) == 0)
            {
                MYSQL_RES* res = mysql_store_result(conn);
                if (res)
                {
                    MYSQL_ROW row = mysql_fetch_row(res);
                    if (row && row[0]) m_serverId = static_cast<uint32_t>(std::stoul(row[0]));
                    mysql_free_result(res);
                }
            }
        }

        m_registered = true;
        LOG_INFO(Core, "[ServerRegistry] RegisterSelf OK: id={} name='{}' {}:{} max={}",
            m_serverId, m_name, m_host, m_port, m_maxPlayers);
        return true;
    }

    void ServerRegistry::SetOffline()
    {
        if (!m_registered || !m_conn)
            return;

        MYSQL* conn = static_cast<MYSQL*>(m_conn);
        char sql[256]{};
        std::snprintf(sql, sizeof(sql),
            "UPDATE game_servers SET status='offline', last_heartbeat=NOW() "
            "WHERE host='%s' AND port=%u",
            m_host.c_str(), m_port);

        if (mysql_query(conn, sql) != 0)
            LOG_WARN(Core, "[ServerRegistry] SetOffline UPDATE failed: {}", mysql_error(conn));
        else
            LOG_INFO(Core, "[ServerRegistry] SetOffline OK (id={})", m_serverId);

        m_registered = false;
        Disconnect();
    }
}
```

- [ ] **Step 3 : Commit**

```bash
git add engine/server/ServerRegistry.h engine/server/ServerRegistry.cpp
git commit -m "feat(server): ServerRegistry — auto-enregistrement du master dans game_servers"
```

---

### Task 3 : Modifier ServerListHandler pour inclure le master

**Files:**
- Modify: `engine/server/ServerListHandler.h`
- Modify: `engine/server/ServerListHandler.cpp`

Le `ServerListHandler` utilise actuellement `ShardRegistry` (shards distants). Nous ajoutons l'entrée du master lui-même via `ServerRegistry`.

- [ ] **Step 1 : Ajouter SetServerRegistry dans ServerListHandler.h**

Dans `engine/server/ServerListHandler.h`, ajouter après la déclaration `SetShardRegistry` :

```cpp
// Avant (ligne ~21) :
void SetShardRegistry(ShardRegistry* registry);

// Après :
void SetShardRegistry(ShardRegistry* registry);
void SetServerRegistry(ServerRegistry* selfRegistry);   // master self-entry
```

Et dans la section private :

```cpp
// Avant :
ShardRegistry* m_registry = nullptr;

// Après :
ShardRegistry*  m_registry       = nullptr;
ServerRegistry* m_serverRegistry = nullptr;  // master self-entry
```

- [ ] **Step 2 : Implémenter SetServerRegistry + fusionner dans HandlePacket**

Dans `engine/server/ServerListHandler.cpp` :

```cpp
// Ajouter l'include :
#include "engine/server/ServerRegistry.h"

// Ajouter la méthode :
void ServerListHandler::SetServerRegistry(ServerRegistry* selfRegistry)
{
    m_serverRegistry = selfRegistry;
}
```

Dans `HandlePacket`, après `entries.reserve(list.size())` et la boucle sur les shards, ajouter l'entrée master :

```cpp
// Après la boucle for (const auto& s : list) { ... } :

// Ajouter l'entrée du master lui-même si ServerRegistry est disponible.
if (m_serverRegistry && m_serverRegistry->GetServerId() != 0)
{
    engine::network::ServerListEntry self;
    self.shard_id       = m_serverRegistry->GetServerId();
    self.status         = 1; // Online
    self.current_load   = 0;
    self.max_capacity   = m_serverRegistry->GetMaxPlayers();
    self.character_count = m_characterCountHook
        ? m_characterCountHook(m_serverRegistry->GetServerId()) : 0u;
    self.endpoint       = m_serverRegistry->GetHost() + ":"
                        + std::to_string(m_serverRegistry->GetPort());
    entries.push_back(self);
}
```

- [ ] **Step 3 : Vérifier où ServerListHandler est instancié**

```bash
grep -r "ServerListHandler" engine/server/ --include="*.cpp" -l
```

Ouvrir chaque fichier trouvé et vérifier que `SetServerRegistry()` peut être appelé après `RegisterSelf()`.

- [ ] **Step 4 : Commit**

```bash
git add engine/server/ServerListHandler.h engine/server/ServerListHandler.cpp
git commit -m "feat(server): ServerListHandler inclut l'entrée master via ServerRegistry"
```

---

### Task 4 : Bannière de démarrage + wiring ServerRegistry dans main.cpp

**Files:**
- Modify: `engine/server/main.cpp`

- [ ] **Step 1 : Ajouter l'include ServerRegistry**

Dans `engine/server/main.cpp`, en tête du fichier :

```cpp
#include "engine/server/ServerApp.h"
#include "engine/server/ServerRegistry.h"  // <-- ajouter
#include "engine/core/Config.h"
#include "engine/core/Log.h"
```

- [ ] **Step 2 : Ajouter la version et la bannière**

Après les includes, ajouter :

```cpp
namespace
{
    // Version du serveur — maintenir à jour avec CMakeLists.txt PROJECT_VERSION.
    constexpr std::string_view kServerVersion = "0.1.0";

    void PrintStartupBanner()
    {
        const char* banner =
            "\n###############################################################\n"
            "#\n"
            "# Serveur\n"
            "# Les Chroniques De La Lune Noire\n"
            "# Version " /* version injected below */ "\n"
            "#\n"
            "###############################################################\n"
            "# Serveur ready\n"
            "###############################################################\n";
        // Utiliser printf pour garantir l'affichage même si le logger n'est pas encore actif.
        std::printf(
            "\n###############################################################\n"
            "#\n"
            "# Serveur\n"
            "# Les Chroniques De La Lune Noire\n"
            "# Version %s\n"
            "#\n"
            "###############################################################\n"
            "# Serveur ready\n"
            "###############################################################\n\n",
            std::string(kServerVersion).c_str());
        std::fflush(stdout);
        // Répéter via le logger pour le fichier de log.
        LOG_INFO(Core, "###############################################################");
        LOG_INFO(Core, "# Serveur — Les Chroniques De La Lune Noire — Version {}", kServerVersion);
        LOG_INFO(Core, "###############################################################");
        LOG_INFO(Core, "# Serveur ready");
        LOG_INFO(Core, "###############################################################");
    }
}
```

- [ ] **Step 3 : Wirer ServerRegistry et bannière dans main()**

Remplacer le bloc `if (!app.Init()) { ... } else { ... result = app.Run(); }` dans `main()` par :

```cpp
engine::server::ServerRegistry serverRegistry;

int result = 1;
if (!app.Init())
{
    LOG_ERROR(Core, "[ServerMain] Init FAILED");
}
else
{
    LOG_INFO(Core, "[ServerMain] Init OK");

    // Auto-enregistrement du master (non bloquant si DB indisponible).
    serverRegistry.RegisterSelf(config);

    // Wirer ServerRegistry dans ServerListHandler si accessible via ServerApp.
    // TODO: app.SetServerRegistry(&serverRegistry); -- à activer quand l'accesseur existe.

    PrintStartupBanner();
    result = app.Run();
}

serverRegistry.SetOffline();   // avant Shutdown pour mise à jour propre en DB
app.Shutdown();
```

**Note :** Si `ServerListHandler` est un membre de `ServerApp`, ajouter un accesseur `ServerApp::GetServerListHandler()` et appeler `SetServerRegistry()` là. Investiguer en lisant `ServerApp.h` lignes 580–683.

- [ ] **Step 4 : Commit**

```bash
git add engine/server/main.cpp
git commit -m "feat(server): bannière de démarrage + wiring ServerRegistry dans main"
```

---

### Task 5 : Format d'horodatage des logs `[JJ/MM/AAAA][HH:MM:SS]`

**Files:**
- Modify: `engine/core/Log.cpp`

Le format actuel (ligne ~100 de Log.cpp) est :
```cpp
char timeBuf[16]{};
std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d.%03d",
    tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));

char header[64]{};
std::snprintf(header, sizeof(header), "[%s][%-5s][%s] ", timeBuf, lvlStr, sys);
```

- [ ] **Step 1 : Modifier le format dans `Log::WriteLine`**

Remplacer les deux blocs `snprintf` ci-dessus par :

```cpp
char timeBuf[32]{};
std::snprintf(timeBuf, sizeof(timeBuf), "%02d/%02d/%04d][%02d:%02d:%02d",
    tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,
    tm.tm_hour, tm.tm_min, tm.tm_sec);

char header[72]{};
std::snprintf(header, sizeof(header), "[%s][%-5s][%s] ", timeBuf, lvlStr, sys);
```

Le résultat produit : `[13/04/2026][14:32:07][INFO ][Core] Message...`

- [ ] **Step 2 : Supprimer le calcul de ms (milliseconds) devenu inutile**

La ligne `const auto ms = std::chrono::duration_cast<...>` peut être retirée si elle n'est plus utilisée. Vérifier qu'aucune autre ligne dans `WriteLine` ne l'utilise. Si oui, supprimer :

```cpp
// Supprimer cette ligne :
const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
```

- [ ] **Step 3 : Compiler et vérifier visuellement**

```bash
# Build serveur (adapter à la config CMake locale)
cmake --build build --target lcdlln_server --config Debug
# Lancer brièvement et observer la console
```

Sortie attendue :
```
[13/04/2026][14:32:07][INFO ][Core] [ServerMain] Log initialized ...
```

- [ ] **Step 4 : Commit**

```bash
git add engine/core/Log.cpp
git commit -m "fix(core): format log [JJ/MM/AAAA][HH:MM:SS] sans millisecondes"
```

---

## Couche 2 — Layout / Rendu

### Task 6 : Corriger les superpositions systémiques (BuildRenderModel clear)

**Files:**
- Modify: `engine/client/AuthUi.cpp` (section BuildRenderModel / construction du RenderModel)

**Problème :** Des éléments de l'état précédent persistent dans `RenderModel` lors d'un changement de `Phase`.

- [ ] **Step 1 : Localiser la construction du RenderModel**

```bash
grep -n "RenderModel" engine/client/AuthUi.cpp | head -40
```

Trouver la ligne où `RenderModel model{}` est déclaré ou réinitialisé dans `Update()` (vers ligne 3281).

- [ ] **Step 2 : S'assurer que le RenderModel est toujours fraîchement construit**

Vérifier que le pattern est :
```cpp
RenderModel model{};   // construit par valeur = tous les champs à zéro/vide
```
et NON :
```cpp
// RenderModel m_model; // membre persistant réutilisé sans clear
```

Si `RenderModel` est un membre (`m_renderModel`) réutilisé entre frames, ajouter un reset explicite au début de la construction :

```cpp
m_renderModel = RenderModel{};   // clear complet avant de peupler
```

- [ ] **Step 3 : Vérifier les changements de Phase**

Rechercher tous les endroits où `m_phase =` est assigné et s'assurer qu'aucun état UI "résiduel" n'est laissé. En particulier, lors du passage à une nouvelle phase, les champs suivants doivent être nettoyés si non pertinents :

```cpp
// Ajouter cette helper lambda à appeler lors de chaque changement de phase :
auto clearTransientUiState = [&]()
{
    m_hoveredFieldIndex   = -1;
    m_hoveredActionIndex  = -1;
    m_hoveredBodyLineIndex = -1;
    m_userErrorText.clear();
    // Ne PAS clear m_infoBanner ici — il peut être voulu.
};
```

Appeler `clearTransientUiState()` dans les transitions de phase principales (Login→Register, Register→VerifyEmail, etc.).

- [ ] **Step 4 : Corriger la superposition "serveur indisponible"**

Le message "serveur indisponible" est affiché via `model.infoBanner` ou `model.errorText`. Vérifier que ces champs ne s'accumulent pas. Si `infoBanner` et `errorText` sont tous deux remplis simultanément, décider de la priorité (errorText l'emporte) et s'assurer qu'un seul est non-vide à la fois dans chaque phase.

- [ ] **Step 5 : Compiler + test visuel**

Builder le client, naviguer entre Login → Register → retour Login. Vérifier qu'aucun texte de la page Register ne persiste sur la page Login.

- [ ] **Step 6 : Commit**

```bash
git add engine/client/AuthUi.cpp
git commit -m "fix(ui): superpositions — RenderModel reset complet à chaque frame/changement de phase"
```

---

### Task 7 : Augmenter la taille de police des champs de saisie

**Files:**
- Modify: `engine/render/AuthUiRenderer.h`
- Modify: `config.json`

La taille de police des champs est contrôlée par `value_font_pixel_height` dans config.json (actuellement 24) et la constante de hauteur des champs `kAuthUiFieldBoxHeightPx = 32`.

- [ ] **Step 1 : Augmenter value_font_pixel_height dans config.json**

Dans `config.json`, section `render.auth_ui` :

```json
// Avant :
"value_font_pixel_height": 24

// Après :
"value_font_pixel_height": 28
```

- [ ] **Step 2 : Augmenter la hauteur des champs si nécessaire**

Si les glyphes débordent du champ après l'augmentation, ajuster `kAuthUiFieldBoxHeightPx` dans `engine/render/AuthUiRenderer.h` :

```cpp
// Avant :
inline constexpr int32_t kAuthUiFieldBoxHeightPx = 32;

// Après (si 28px ne tient pas dans 32) :
inline constexpr int32_t kAuthUiFieldBoxHeightPx = 36;
```

- [ ] **Step 3 : Vérifier visuellement**

Builder et vérifier que le texte saisi dans les champs login/password/email est lisible. Les glyphes ne doivent pas dépasser la bordure des champs.

- [ ] **Step 4 : Commit**

```bash
git add config.json engine/render/AuthUiRenderer.h
git commit -m "fix(ui): augmenter taille police champs de saisie (24→28px)"
```

---

### Task 8 : Icône "i" — affichage + état popup

**Files:**
- Modify: `engine/client/AuthUi.h` (RenderModel, nouveaux champs)
- Modify: `engine/client/AuthUi.cpp` (construction du modèle, popup visible)
- Modify: `game/data/localization/fr/fr.json`
- Modify: `game/data/localization/en/en.json`

- [ ] **Step 1 : Ajouter les clés de localisation manquantes**

Dans `game/data/localization/fr/fr.json`, vérifier l'existence de clés de type `auth.info.login_help` et `auth.info.register_help`. Si absentes, ajouter :

```json
"auth.info.login_help": "Saisissez votre identifiant (login) et votre mot de passe pour vous connecter. En cas d'oubli, utilisez le lien \"Mot de passe oublié\".",
"auth.info.register_help": "Le login doit contenir 3 à 20 caractères alphanumériques. Le mot de passe doit faire au moins 8 caractères. La date de naissance est requise pour vérifier votre âge.",
"common.close": "Fermer"
```

Dans `game/data/localization/en/en.json`, idem :

```json
"auth.info.login_help": "Enter your login and password to sign in. If you forgot your password, use the \"Forgot password\" link.",
"auth.info.register_help": "Login must be 3 to 20 alphanumeric characters. Password must be at least 8 characters. Date of birth is required to verify your age.",
"common.close": "Close"
```

- [ ] **Step 2 : Ajouter les champs popup dans RenderModel (AuthUi.h)**

Dans `engine/client/AuthUi.h`, dans la struct `RenderModel` (vers ligne 165), ajouter après le dernier champ existant :

```cpp
// Popup info (icône "i") — affiché par-dessus tout le reste quand visible.
bool        infoPopupVisible = false;
std::string infoPopupText;      // Texte complet (localisé) à afficher dans le popup.
```

- [ ] **Step 3 : Ajouter l'état popup dans AuthUiPresenter (AuthUi.h)**

Dans la section des membres privés de `AuthUiPresenter`, ajouter :

```cpp
bool        m_infoPopupVisible = false;
std::string m_infoPopupText;
```

- [ ] **Step 4 : Remplir le popup dans BuildRenderModel (AuthUi.cpp)**

Dans la section qui construit `model` (chercher `model.infoBanner`), ajouter à la fin de la construction :

```cpp
// Popup info — propagé tel quel depuis l'état.
model.infoPopupVisible = m_infoPopupVisible;
model.infoPopupText    = m_infoPopupText;
```

Et remplir `m_infoPopupText` selon la phase active (ajouter dans la section de construction de RenderModel, avant la propagation) :

```cpp
// Définir le texte du popup info selon la phase.
if (m_phase == Phase::Login || m_phase == Phase::ForgotPassword)
    m_infoPopupText = Tr("auth.info.login_help");
else if (m_phase == Phase::Register)
    m_infoPopupText = Tr("auth.info.register_help");
// Les autres phases n'ont pas de popup info.
```

- [ ] **Step 5 : Localiser le rendu de l'icône "i"**

```bash
grep -n "hoveredFieldInfoIndex\|info.*logo\|info.*icon\|infoIcon\|info_icon" \
    engine/render/AuthGlyphPass.cpp engine/render/AuthUiRenderer.cpp -i
```

Identifier comment l'icône "i" est censée être rendue. Si elle dépend de `model.hoveredFieldInfoIndex`, vérifier que cette valeur est correctement mise à jour lors du survol.

- [ ] **Step 6 : Commit**

```bash
git add engine/client/AuthUi.h engine/client/AuthUi.cpp \
        game/data/localization/fr/fr.json game/data/localization/en/en.json
git commit -m "feat(ui): état popup info — champs RenderModel + textes localisés"
```

---

### Task 9 : Rendu du popup info avec fond opaque

**Files:**
- Modify: `engine/render/AuthGlyphPass.h`
- Modify: `engine/render/AuthGlyphPass.cpp`

- [ ] **Step 1 : Ajouter la méthode RecordInfoPopup dans AuthGlyphPass.h**

Dans `engine/render/AuthGlyphPass.h`, ajouter la déclaration :

```cpp
/// Rend le popup info (fond opaque + texte centré) si model.infoPopupVisible.
void RecordInfoPopup(VkDevice device, VkCommandBuffer cmd, VkExtent2D extent,
                     const engine::client::AuthUiPresenter::RenderModel& model,
                     const AuthUiTheme& theme);
```

- [ ] **Step 2 : Implémenter RecordInfoPopup dans AuthGlyphPass.cpp**

```cpp
void AuthGlyphPass::RecordInfoPopup(VkDevice device, VkCommandBuffer cmd, VkExtent2D extent,
                                     const engine::client::AuthUiPresenter::RenderModel& model,
                                     const AuthUiTheme& theme)
{
    if (!model.infoPopupVisible || model.infoPopupText.empty())
        return;

    const int32_t w = static_cast<int32_t>(extent.width);
    const int32_t h = static_cast<int32_t>(extent.height);

    // 1. Fond opaque (rectangle noir semi-transparent couvrant tout l'écran).
    //    Rendu comme un quad coloré via le pipeline glyph avec une couleur de fond.
    //    Utiliser les glyphes de l'espace pour remplir — ou un quad dédié si le pipeline le supporte.
    // Note : si le pipeline ne supporte pas les quads de fond, utiliser un push constant ou
    // un pass séparé. Pour l'instant, approximer avec une boîte de texte remplie.
    const float overlayAlpha = 0.82f;
    // TODO: appeler la primitive de rendu quad du pipeline existant avec couleur (0,0,0,overlayAlpha).
    // Investiguer comment les autres fond de panneaux sont rendus dans AuthGlyphPass::RecordModel.

    // 2. Cadre centré avec le texte du popup.
    const int32_t popupW   = std::min(w * 70 / 100, 640);
    const int32_t popupH   = std::min(h * 50 / 100, 360);
    const int32_t popupX   = (w - popupW) / 2;
    const int32_t popupY   = (h - popupH) / 2;
    const int32_t textX    = popupX + 24;
    const int32_t textY    = popupY + 24;
    const int32_t maxTextW = popupW - 48;

    // 3. Rendu du texte.
    Record(device, cmd, extent, model.infoPopupText, textX, textY, maxTextW);
}
```

- [ ] **Step 3 : Appeler RecordInfoPopup en dernier dans RecordModel**

Dans `AuthGlyphPass::RecordModel()`, à la toute fin (après tous les autres rendus) :

```cpp
// Popup info en dernier — s'affiche par-dessus tout.
RecordInfoPopup(device, cmd, extent, model, theme);
```

- [ ] **Step 4 : Compiler et tester visuellement**

Déclencher l'affichage du popup (via le clic sur "i" — implémenté en Task 15). En attendant, forcer temporairement `m_infoPopupVisible = true` dans le code et vérifier le rendu.

- [ ] **Step 5 : Commit**

```bash
git add engine/render/AuthGlyphPass.h engine/render/AuthGlyphPass.cpp
git commit -m "feat(ui): RecordInfoPopup — popup info avec fond opaque par-dessus l'UI"
```

---

### Task 10 : Sélecteurs de date — DropdownField dans RenderModel

**Files:**
- Modify: `engine/client/AuthUi.h` (nouveau type DropdownField, champs dans RenderModel)
- Modify: `engine/client/AuthUi.cpp` (construction de la date dans Register phase)

- [ ] **Step 1 : Définir DropdownField dans AuthUi.h**

Dans `engine/client/AuthUi.h`, avant la struct `RenderModel`, ajouter :

```cpp
struct DropdownOption
{
    std::string label;   // Texte affiché (ex. "Janvier", "1", "2000")
    std::string value;   // Valeur interne (ex. "01", "2000")
};

struct RenderDropdown
{
    std::string         label;          // Label au-dessus du dropdown (ex. "Jour")
    std::vector<DropdownOption> options;
    int32_t             selectedIndex = 0;  // Index de l'option sélectionnée
    bool                isOpen = false;     // Liste déroulante ouverte ?
    int32_t             x = 0, y = 0, w = 0, h = 0;  // Bounding box (remplie par renderer)
};
```

Dans `RenderModel`, ajouter :

```cpp
// Sélecteurs de date de naissance (page d'inscription).
std::vector<RenderDropdown> dropdowns;
```

- [ ] **Step 2 : Ajouter les états de sélection dans AuthUiPresenter**

Dans les membres privés (AuthUi.h) :

```cpp
// États des dropdowns date.
int32_t m_birthDayIndex   = 0;   // Index dans 1-31
int32_t m_birthMonthIndex = 0;   // Index dans 1-12
int32_t m_birthYearIndex  = 0;   // Index dans plage années
int32_t m_openDropdownIndex = -1; // -1 = aucun ouvert, 0=jour, 1=mois, 2=année
```

- [ ] **Step 3 : Construire les dropdowns dans la phase Register (AuthUi.cpp)**

Dans la section de construction du `RenderModel` pour la phase `Register`, localiser où les champs de date sont actuellement ajoutés (chercher `m_birthDay`, `m_birthMonth`, `m_birthYear`). Remplacer ou compléter par :

```cpp
// Helper pour construire les options 1-N.
auto makeIntOptions = [](int lo, int hi) -> std::vector<DropdownOption>
{
    std::vector<DropdownOption> opts;
    opts.reserve(hi - lo + 1);
    for (int i = lo; i <= hi; ++i)
    {
        char buf[8]{};
        std::snprintf(buf, sizeof(buf), "%d", i);
        opts.push_back({ std::string(buf), std::string(buf) });
    }
    return opts;
};

// Dropdown Jour (1-31).
{
    RenderDropdown dd;
    dd.label         = Tr("auth.label.birth_day");   // "Jour" / "Day"
    dd.options       = makeIntOptions(1, 31);
    dd.selectedIndex = std::clamp(m_birthDayIndex, 0, 30);
    dd.isOpen        = (m_openDropdownIndex == 0);
    model.dropdowns.push_back(dd);
}

// Dropdown Mois (noms localisés).
{
    RenderDropdown dd;
    dd.label = Tr("auth.label.birth_month");  // "Mois" / "Month"
    for (int m = 1; m <= 12; ++m)
    {
        char key[16]{};
        std::snprintf(key, sizeof(key), "month.%d", m);
        dd.options.push_back({ Tr(key), std::to_string(m) });
    }
    dd.selectedIndex = std::clamp(m_birthMonthIndex, 0, 11);
    dd.isOpen        = (m_openDropdownIndex == 1);
    model.dropdowns.push_back(dd);
}

// Dropdown Année (1900-2010).
{
    RenderDropdown dd;
    dd.label   = Tr("auth.label.birth_year");  // "Année" / "Year"
    dd.options = makeIntOptions(1900, 2010);
    dd.selectedIndex = std::clamp(m_birthYearIndex, 0, 110);
    dd.isOpen        = (m_openDropdownIndex == 2);
    model.dropdowns.push_back(dd);
}

// Synchroniser m_birthDay/Month/Year depuis les index sélectionnés.
if (!model.dropdowns[0].options.empty())
    m_birthDay   = model.dropdowns[0].options[m_birthDayIndex].value;
if (!model.dropdowns[1].options.empty())
    m_birthMonth = model.dropdowns[1].options[m_birthMonthIndex].value;
if (!model.dropdowns[2].options.empty())
    m_birthYear  = model.dropdowns[2].options[m_birthYearIndex].value;
```

Ajouter dans `game/data/localization/fr/fr.json` si absents :

```json
"auth.label.birth_day":   "Jour",
"auth.label.birth_month": "Mois",
"auth.label.birth_year":  "Année"
```

Et dans `game/data/localization/en/en.json` :

```json
"auth.label.birth_day":   "Day",
"auth.label.birth_month": "Month",
"auth.label.birth_year":  "Year"
```

- [ ] **Step 4 : Ajouter le rendu des dropdowns dans AuthGlyphPass**

Dans `AuthGlyphPass::RecordModel()`, après le rendu des champs texte normaux :

```cpp
// Rendu des dropdowns (date de naissance).
for (const auto& dd : model.dropdowns)
{
    if (dd.options.empty()) continue;
    const std::string& selectedLabel = dd.options[dd.selectedIndex].label;
    // Afficher le label + la valeur sélectionnée + indicateur "▼".
    const std::string display = dd.label + ": " + selectedLabel + " ▼";
    Record(device, cmd, extent, display, dd.x, dd.y, dd.w);
    if (dd.isOpen)
    {
        int32_t optY = dd.y + dd.h + 2;
        for (int i = 0; i < static_cast<int>(dd.options.size()); ++i)
        {
            Record(device, cmd, extent, dd.options[i].label, dd.x, optY, dd.w);
            optY += kAuthUiFieldBoxHeightPx + 2;
        }
    }
}
```

- [ ] **Step 5 : Commit**

```bash
git add engine/client/AuthUi.h engine/client/AuthUi.cpp \
        engine/render/AuthGlyphPass.cpp \
        game/data/localization/fr/fr.json game/data/localization/en/en.json
git commit -m "feat(ui): sélecteurs de date remplacés par dropdowns (RenderDropdown)"
```

---

### Task 11 : Rectangle blanc au lancement

**Files:**
- Modify: Fichier d'initialisation Vulkan (chercher la couleur de clear)

- [ ] **Step 1 : Localiser la clear color Vulkan**

```bash
grep -rn "clearValue\|clearColor\|VkClearValue\|VK_ATTACHMENT_LOAD_OP_CLEAR" \
    engine/render/ --include="*.cpp" | head -20
```

- [ ] **Step 2 : Forcer la clear color à noir**

Dans le fichier trouvé, s'assurer que la clear color est initialisée à noir :

```cpp
// Chercher un bloc comme :
VkClearValue clearColor{};
clearColor.color = {{ 1.0f, 1.0f, 1.0f, 1.0f }};  // BLANC — problème !

// Corriger en :
VkClearValue clearColor{};
clearColor.color = {{ 0.0f, 0.0f, 0.0f, 1.0f }};  // NOIR
```

- [ ] **Step 3 : Vérifier le conditional rendering au démarrage**

```bash
grep -rn "m_initialized\|assetsReady\|m_ready\|loaded" engine/render/ --include="*.cpp" | head -20
```

Si des passes de rendu s'exécutent avant que les textures/assets soient chargés, ajouter un guard :

```cpp
if (!m_assetsReady)
{
    // Rendre seulement un écran noir jusqu'à ce que les assets soient prêts.
    return;
}
```

- [ ] **Step 4 : Commit**

```bash
git add <fichiers_modifiés>
git commit -m "fix(render): clear color initialisée à noir — supprime le flash blanc au démarrage"
```

---

### Task 12 : Phase EmailConfirmationPending post-inscription

**Files:**
- Modify: `engine/client/AuthUi.h` (ajouter Phase::EmailConfirmationPending)
- Modify: `engine/client/AuthUi.cpp` (transition + rendu)
- Modify: `game/data/localization/fr/fr.json`
- Modify: `game/data/localization/en/en.json`

- [ ] **Step 1 : Ajouter la phase dans l'enum**

Dans `engine/client/AuthUi.h`, l'enum `Phase` :

```cpp
enum class Phase
{
    Login,
    Register,
    VerifyEmail,
    EmailConfirmationPending,   // <-- ajouter après VerifyEmail
    ForgotPassword,
    Terms,
    CharacterCreate,
    LanguageSelectionFirstRun,
    LanguageOptions,
    Submitting,
    Error
};
```

- [ ] **Step 2 : Ajouter les clés de localisation**

Dans `game/data/localization/fr/fr.json` :

```json
"auth.email_confirmation.title": "Inscription réussie",
"auth.email_confirmation.message": "Un email de confirmation vient de vous être envoyé. Veuillez cliquer sur le lien qu'il contient pour activer votre compte.",
"auth.email_confirmation.back_to_login": "Retour à la connexion"
```

Dans `game/data/localization/en/en.json` :

```json
"auth.email_confirmation.title": "Registration successful",
"auth.email_confirmation.message": "A confirmation email has been sent to you. Please click the link in the email to activate your account.",
"auth.email_confirmation.back_to_login": "Back to login"
```

- [ ] **Step 3 : Transition vers EmailConfirmationPending après inscription réussie**

Dans `AuthUi.cpp`, dans `PollAsyncResult()`, section register success (vers ligne 1494) :

```cpp
// Avant :
if (copy.success)
{
    m_pendingVerifyAccountId = copy.accountId;
    m_phase = Phase::VerifyEmail;
    // ...
}

// Après :
if (copy.success)
{
    m_pendingVerifyAccountId = copy.accountId;
    m_phase = Phase::EmailConfirmationPending;  // page intermédiaire
    m_userErrorText.clear();
    if (!copy.tagId.empty())
        m_registeredTagId = copy.tagId;
}
```

- [ ] **Step 4 : Construire le RenderModel pour EmailConfirmationPending**

Dans la section switch/if sur `m_phase` qui construit le RenderModel, ajouter :

```cpp
case Phase::EmailConfirmationPending:
{
    model.titleLine1    = Tr("auth.email_confirmation.title");
    model.sectionTitle  = "";

    // Message principal.
    std::string msg = Tr("auth.email_confirmation.message");
    if (!m_registeredTagId.empty())
        msg += "\n\n" + Tr("auth.info.tag_id") + " " + m_registeredTagId;
    model.infoBanner = msg;

    // Bouton retour.
    RenderAction back;
    back.label   = Tr("auth.email_confirmation.back_to_login");
    back.primary = true;
    model.actions.push_back(back);
    break;
}
```

- [ ] **Step 5 : Gérer l'action "Retour" depuis EmailConfirmationPending**

Dans la section de gestion des clics/actions (chercher `applyPrimaryAction`), ajouter :

```cpp
case Phase::EmailConfirmationPending:
    // Le bouton ramène à la page de connexion.
    m_phase = Phase::Login;
    m_infoBanner.clear();
    m_registeredTagId.clear();
    m_userErrorText.clear();
    break;
```

- [ ] **Step 6 : Commit**

```bash
git add engine/client/AuthUi.h engine/client/AuthUi.cpp \
        game/data/localization/fr/fr.json game/data/localization/en/en.json
git commit -m "feat(ui): page intermédiaire EmailConfirmationPending post-inscription"
```

---

### Task 13 : Supprimer l'affichage du nombre de comptes

**Files:**
- Modify: `engine/client/AuthUi.cpp`

- [ ] **Step 1 : Localiser l'affichage du nombre de comptes**

```bash
grep -n "account.*count\|totalAccounts\|nbAccounts\|nb_accounts\|account_count\|comptes" \
    engine/client/AuthUi.cpp -i
```

- [ ] **Step 2 : Retirer la ligne d'ajout au RenderModel**

Supprimer ou commenter la ligne qui ajoute le nombre de comptes dans le `RenderModel` (probablement un `addBodyLine(...)` ou une affectation à `model.footerHint`).

- [ ] **Step 3 : Commit**

```bash
git add engine/client/AuthUi.cpp
git commit -m "fix(ui): supprimer l'affichage du nombre de comptes existants"
```

---

## Couche 3 — Événements Souris

### Task 14 : Audit des bounding boxes — boutons non cliquables

**Files:**
- Modify: `engine/client/AuthUi.cpp` (Update() — section hit-testing des actions)

- [ ] **Step 1 : Localiser le hit-testing des boutons**

```bash
grep -n "hoveredActionIndex\|actions\[.*\].*x\|actions\[.*\].*y\|actionRect\|actionBox" \
    engine/client/AuthUi.cpp | head -30
```

Identifier la section où les bounding boxes des `RenderAction` sont comparées à la position souris.

- [ ] **Step 2 : Vérifier que les bounding boxes sont remplies par le renderer**

Les `RenderAction` ont des champs `x, y, w, h`. Ces champs doivent être remplis par `BuildAuthUiLayoutMetrics()` ou équivalent, AVANT d'être utilisés dans `Update()` pour le hit-testing.

Vérifier l'ordre des opérations dans `Update()` :
```cpp
// Ordre correct :
auto model   = BuildRenderModel();                  // 1. Construire le modèle
auto metrics = BuildAuthUiLayoutMetrics(..., model); // 2. Calculer les métriques/bounding boxes
// ... remplir model.actions[i].x/y/w/h depuis metrics
// 3. Hit-test avec positions connues
if (input.mouseX >= action.x && input.mouseX <= action.x + action.w && ...)
    m_hoveredActionIndex = i;
```

Si les bounding boxes sont calculées mais pas propagées dans `model.actions`, ajouter la propagation.

- [ ] **Step 3 : Vérifier toutes les phases**

S'assurer que le hit-testing des boutons est actif pour les phases :
- `Phase::Login` (bouton "Connexion" + "Inscription" + "Mot de passe oublié")
- `Phase::Register` (bouton "Valider" + "Retour")
- `Phase::VerifyEmail` (bouton "Vérifier" + "Retour")
- `Phase::EmailConfirmationPending` (bouton "Retour à la connexion")
- `Phase::Terms` (bouton "Accepter" + scroll)

Pour chaque phase, tracer un breakpoint ou log pour confirmer que `m_hoveredActionIndex` se met à jour lors du survol.

- [ ] **Step 4 : Corriger le click handling**

Dans la section de gestion du clic (chercher `leftClick` ou `mousePressed`) :

```cpp
// S'assurer que le clic est traité si hoveredActionIndex >= 0 :
if (input.IsMouseButtonPressed(MouseButton::Left) && m_hoveredActionIndex >= 0)
{
    const int32_t actionIdx = m_hoveredActionIndex;
    // dispatcher selon la phase...
}
```

- [ ] **Step 5 : Test**

Builder et vérifier que les boutons "Valider" et "Retour" de la page d'inscription répondent au clic souris.

- [ ] **Step 6 : Commit**

```bash
git add engine/client/AuthUi.cpp
git commit -m "fix(ui): bounding boxes boutons — hit-testing souris actif sur toutes les phases"
```

---

### Task 15 : Interaction souris — dropdowns date

**Files:**
- Modify: `engine/client/AuthUi.cpp` (Update() — hit-testing dropdowns)

- [ ] **Step 1 : Ajouter le hit-testing des dropdowns dans Update()**

Dans `Update()`, après le hit-testing des champs texte et avant celui des boutons, ajouter :

```cpp
// Hit-testing des dropdowns.
if (!model.dropdowns.empty())
{
    for (int32_t di = 0; di < static_cast<int32_t>(model.dropdowns.size()); ++di)
    {
        const auto& dd = model.dropdowns[di];
        // Survol du header du dropdown (zone de clic pour ouvrir/fermer).
        if (input.mouseX >= dd.x && input.mouseX <= dd.x + dd.w
            && input.mouseY >= dd.y && input.mouseY <= dd.y + dd.h)
        {
            // Clic : toggle ouverture.
            if (input.IsMouseButtonPressed(MouseButton::Left))
            {
                m_openDropdownIndex = (m_openDropdownIndex == di) ? -1 : di;
            }
        }
        // Si ce dropdown est ouvert, tester le survol des options.
        else if (m_openDropdownIndex == di && dd.isOpen)
        {
            int32_t optY = dd.y + dd.h + 2;
            for (int32_t oi = 0; oi < static_cast<int32_t>(dd.options.size()); ++oi)
            {
                const int32_t optH = kAuthUiFieldBoxHeightPx + 2;
                if (input.mouseX >= dd.x && input.mouseX <= dd.x + dd.w
                    && input.mouseY >= optY && input.mouseY <= optY + optH)
                {
                    if (input.IsMouseButtonPressed(MouseButton::Left))
                    {
                        // Sélectionner cette option.
                        if (di == 0) m_birthDayIndex   = oi;
                        else if (di == 1) m_birthMonthIndex = oi;
                        else if (di == 2) m_birthYearIndex  = oi;
                        m_openDropdownIndex = -1;  // Fermer.
                    }
                }
                optY += optH;
            }
        }
    }

    // Clic en dehors de tous les dropdowns : fermer le dropdown ouvert.
    if (input.IsMouseButtonPressed(MouseButton::Left) && m_openDropdownIndex >= 0)
    {
        // Vérifier qu'on n'a pas cliqué sur un dropdown (déjà géré ci-dessus).
        bool clickedOnDropdown = false;
        for (const auto& dd : model.dropdowns)
        {
            if (input.mouseX >= dd.x && input.mouseX <= dd.x + dd.w
                && input.mouseY >= dd.y && input.mouseY <= dd.y + dd.h + 200)
            {
                clickedOnDropdown = true; break;
            }
        }
        if (!clickedOnDropdown)
            m_openDropdownIndex = -1;
    }
}
```

- [ ] **Step 2 : Réinitialiser m_openDropdownIndex lors des changements de phase**

Dans `clearTransientUiState()` (Task 6) ou dans chaque transition de phase vers Register :

```cpp
m_openDropdownIndex = -1;
```

- [ ] **Step 3 : Test**

Builder, aller sur la page d'inscription, cliquer sur le dropdown "Mois" — la liste doit s'ouvrir. Cliquer sur "Mars" — doit sélectionner Mars et fermer.

- [ ] **Step 4 : Commit**

```bash
git add engine/client/AuthUi.cpp
git commit -m "fix(ui): interaction souris dropdowns — ouverture, sélection, fermeture"
```

---

### Task 16 : Clic sur l'icône "i" — afficher/fermer le popup

**Files:**
- Modify: `engine/client/AuthUi.cpp` (Update() — hit-testing icône info)

- [ ] **Step 1 : Localiser la bounding box de l'icône "i"**

```bash
grep -n "hoveredFieldInfoIndex\|infoIcon\|info.*box\|infoBox" engine/client/AuthUi.cpp | head -20
```

L'icône "i" correspond à `model.hoveredFieldInfoIndex`. Sa bounding box est calculée dans le renderer. Elle doit être propagée dans `RenderModel` pour le hit-testing.

- [ ] **Step 2 : Ajouter la bounding box de l'icône "i" dans RenderModel (si absente)**

Dans `engine/client/AuthUi.h`, dans `RenderModel` :

```cpp
// Bounding box de l'icône info "i" (vide si pas d'icône sur cette page).
int32_t infoIconX = 0, infoIconY = 0, infoIconW = 0, infoIconH = 0;
bool    infoIconVisible = false;
```

Remplir depuis les métriques dans le renderer ou dans `AuthUi.cpp` lors de la construction du modèle.

- [ ] **Step 3 : Ajouter le hit-test de l'icône info dans Update()**

Dans `Update()`, dans la section de hit-testing, ajouter :

```cpp
// Hit-test icône info "i".
if (model.infoIconVisible
    && input.mouseX >= model.infoIconX
    && input.mouseX <= model.infoIconX + model.infoIconW
    && input.mouseY >= model.infoIconY
    && input.mouseY <= model.infoIconY + model.infoIconH)
{
    if (input.IsMouseButtonPressed(MouseButton::Left))
    {
        m_infoPopupVisible = !m_infoPopupVisible;  // Toggle.
    }
}

// Clic sur le fond opaque du popup ou bouton "Fermer" → fermer.
if (m_infoPopupVisible && input.IsMouseButtonPressed(MouseButton::Left))
{
    // Si le clic n'est pas sur le cadre central du popup → fermer.
    const int32_t pw = static_cast<int32_t>(m_viewportW) * 70 / 100;
    const int32_t ph = static_cast<int32_t>(m_viewportH) * 50 / 100;
    const int32_t px = (static_cast<int32_t>(m_viewportW) - pw) / 2;
    const int32_t py = (static_cast<int32_t>(m_viewportH) - ph) / 2;
    const bool inPopup = input.mouseX >= px && input.mouseX <= px + pw
                      && input.mouseY >= py && input.mouseY <= py + ph;
    if (!inPopup)
        m_infoPopupVisible = false;
}
```

- [ ] **Step 4 : Réinitialiser le popup lors des changements de phase**

```cpp
m_infoPopupVisible = false;  // ajouter dans clearTransientUiState()
```

- [ ] **Step 5 : Test**

Vérifier que le clic sur "i" affiche le popup, et qu'un clic en dehors le ferme.

- [ ] **Step 6 : Commit**

```bash
git add engine/client/AuthUi.cpp engine/client/AuthUi.h
git commit -m "fix(ui): clic icône info — toggle popup avec fermeture au clic extérieur"
```

---

## Couche 4 — Textes / Labels / États

### Task 17 : Label "Se souvenir" → "Mémoriser mon identifiant"

**Files:**
- Modify: `game/data/localization/fr/fr.json`
- Modify: `game/data/localization/en/en.json`

- [ ] **Step 1 : Trouver la clé de localisation actuelle**

```bash
grep -n "souvenir\|remember\|Se souvenir" game/data/localization/fr/fr.json engine/client/AuthUi.cpp
```

- [ ] **Step 2 : Modifier la valeur dans les fichiers de langue**

Dans `game/data/localization/fr/fr.json`, trouver la clé `auth.label.remember` (ou similaire) et changer la valeur :

```json
// Avant :
"auth.label.remember": "Se souvenir"

// Après :
"auth.label.remember": "Mémoriser mon identifiant"
```

Dans `game/data/localization/en/en.json` :

```json
// Avant :
"auth.label.remember": "Remember me"

// Après :
"auth.label.remember": "Remember my username"
```

- [ ] **Step 3 : Vérifier que la bounding box du label est assez large**

```bash
grep -n "remember\|rememberLogin" engine/client/AuthUi.cpp | head -10
```

Si la largeur du label est codée en dur, s'assurer qu'elle accommode le nouveau texte plus long.

- [ ] **Step 4 : Commit**

```bash
git add game/data/localization/fr/fr.json game/data/localization/en/en.json
git commit -m "fix(ui): label 'Se souvenir' renommé en 'Mémoriser mon identifiant'"
```

---

### Task 18 : Bouton "Inscription" — libellé et/ou taille

**Files:**
- Modify: `game/data/localization/fr/fr.json` (si texte incorrect)
- Modify: `engine/render/AuthUiRenderer.cpp` (si bounding box trop petite)

- [ ] **Step 1 : Vérifier le libellé actuel**

```bash
grep -n "Inscription\|inscription\|register.*button\|auth.action.register" \
    game/data/localization/fr/fr.json engine/client/AuthUi.cpp
```

- [ ] **Step 2a : Si le texte est tronqué ou incorrect**

Corriger dans `game/data/localization/fr/fr.json` :

```json
"auth.action.register": "Inscription"
```

- [ ] **Step 2b : Si la bounding box est trop petite**

Dans `engine/render/AuthUiRenderer.cpp`, dans `BuildAuthUiLayoutMetrics()`, vérifier la largeur allouée aux boutons d'action secondaires. Si des boutons sont côte à côte, ils partagent la largeur du `contentW`. Augmenter `contentW` ou ajuster la répartition.

- [ ] **Step 3 : Commit**

```bash
git add game/data/localization/fr/fr.json engine/render/AuthUiRenderer.cpp
git commit -m "fix(ui): bouton Inscription — libellé et/ou taille corrigés"
```

---

### Task 19 : Message d'erreur générique en cas d'échec d'auth

**Files:**
- Modify: `engine/client/AuthUi.cpp` (PollAsyncResult — cas AUTH_RESPONSE failure)
- Modify: `game/data/localization/fr/fr.json`
- Modify: `game/data/localization/en/en.json`

- [ ] **Step 1 : Ajouter la clé de localisation**

Dans `game/data/localization/fr/fr.json` :

```json
"auth.error.invalid_credentials": "Identifiant ou mot de passe incorrect."
```

Dans `game/data/localization/en/en.json` :

```json
"auth.error.invalid_credentials": "Incorrect username or password."
```

- [ ] **Step 2 : Localiser le message d'erreur d'auth dans PollAsyncResult**

```bash
grep -n "auth.*fail\|login.*fail\|error.*login\|error.*password\|AUTH_RESPONSE\|copy.success.*0" \
    engine/client/AuthUi.cpp | head -20
```

- [ ] **Step 3 : Remplacer le message spécifique par le message générique**

Dans la section `PollAsyncResult()` qui gère l'échec d'authentification :

```cpp
// Avant (exemple) :
m_userErrorText = copy.message;  // Peut révéler "login incorrect" vs "mot de passe incorrect"

// Après :
// Toujours afficher un message générique pour ne pas révéler quelle partie est fausse.
m_userErrorText = Tr("auth.error.invalid_credentials");
(void)copy.message;  // Le message serveur est ignoré pour raison de sécurité.
```

- [ ] **Step 4 : Commit**

```bash
git add engine/client/AuthUi.cpp \
        game/data/localization/fr/fr.json game/data/localization/en/en.json
git commit -m "fix(security): erreur auth générique — ne révèle pas login vs mot de passe"
```

---

### Task 20 : Cycle de vie de l'écran de sélection de langue

**Files:**
- Modify: `engine/client/AuthUi.cpp` (transition depuis LanguageSelectionFirstRun)

**Problème :** La `Phase::LanguageSelectionFirstRun` reste visible et se superpose après la première utilisation.

- [ ] **Step 1 : Localiser `ApplyLocaleSelection`**

```bash
grep -n "ApplyLocaleSelection\|LanguageSelectionFirstRun.*->.*Login\|hasPersistedLocale" \
    engine/client/AuthUi.cpp | head -20
```

- [ ] **Step 2 : Vérifier la transition de phase après sélection**

Dans `ApplyLocaleSelection(bool isFirstRun)` (chercher la définition) :

```cpp
// Vérifier que si isFirstRun == true, la phase est changée vers Login :
if (isFirstRun)
{
    m_hasPersistedLocale = true;
    m_phase = Phase::Login;   // Doit impérativement quitter LanguageSelectionFirstRun.
    m_infoBanner = Tr("language.apply_success", { { "language", LocalizedLanguageName(m_selectedLocale) } });
}
```

Si `m_phase` n'est pas mis à jour, l'écran persiste. Ajouter l'affectation si absente.

- [ ] **Step 3 : Vérifier la condition d'entrée dans LanguageSelectionFirstRun**

Trouver où `m_phase = Phase::LanguageSelectionFirstRun` est assigné (ligne 1071) et vérifier qu'il est conditionné à `!m_hasPersistedLocale` :

```cpp
// S'assurer que la condition est :
if (!m_hasPersistedLocale)
    m_phase = Phase::LanguageSelectionFirstRun;
else
    m_phase = Phase::Login;
```

- [ ] **Step 4 : Test**

Lancer le client une première fois, sélectionner la langue. Vérifier que la page Login s'affiche ensuite sans superposition. Relancer le client : l'écran de langue ne doit plus apparaître.

- [ ] **Step 5 : Commit**

```bash
git add engine/client/AuthUi.cpp
git commit -m "fix(ui): écran sélection langue — transition vers Login après première sélection"
```

---

### Task 21 : Page d'erreur — empilement vertical des messages

**Files:**
- Modify: `engine/client/AuthUi.cpp` (construction RenderModel pour Phase::Error)
- Modify: `engine/render/AuthGlyphPass.cpp` (rendu multi-messages avec cadres rouge/orange)

- [ ] **Step 1 : Localiser la construction du RenderModel en Phase::Error**

```bash
grep -n "Phase::Error\|errorText\|m_userErrorText\|error.*model\|model.*error" \
    engine/client/AuthUi.cpp | head -30
```

- [ ] **Step 2 : Stocker plusieurs messages d'erreur**

Si les erreurs et warnings sont actuellement stockés dans un seul `std::string m_userErrorText`, modifier pour supporter une liste :

Dans `engine/client/AuthUi.h`, dans `RenderModel` :

```cpp
// Messages d'erreur/warning empilés (sans superposition).
struct ErrorMessage
{
    std::string text;
    bool isWarning = false;  // true → cadre orange, false → cadre rouge
};
std::vector<ErrorMessage> errorMessages;  // liste ordonnée, affiché de haut en bas
```

Dans les membres de `AuthUiPresenter` :

```cpp
std::vector<std::pair<std::string, bool>> m_errorMessages;  // (text, isWarning)
```

- [ ] **Step 3 : Propager dans BuildRenderModel**

```cpp
// Dans la construction du RenderModel pour Phase::Error :
for (const auto& [text, isWarning] : m_errorMessages)
    model.errorMessages.push_back({ text, isWarning });
```

- [ ] **Step 4 : Rendu empilé dans AuthGlyphPass**

Dans `AuthGlyphPass::RecordModel()`, remplacer le rendu d'un seul `errorText` par :

```cpp
// Rendu empilé des messages d'erreur/warning.
if (!model.errorMessages.empty())
{
    int32_t msgY = metrics.authErrorBoxTopFromPanelTopPx + metrics.panelY + 8;
    for (const auto& msg : model.errorMessages)
    {
        // Choisir la couleur du cadre selon le type.
        // isWarning → cadre orange (1.0, 0.5, 0.0, 1.0)
        // erreur    → cadre rouge  (0.8, 0.1, 0.1, 1.0)
        // Dessiner le cadre puis le texte.
        Record(device, cmd, extent, msg.text,
               metrics.contentX, msgY, metrics.contentW);
        msgY += kAuthUiFieldBoxHeightPx + 8;  // espacement fixe entre messages
    }
}
```

- [ ] **Step 5 : Commit**

```bash
git add engine/client/AuthUi.h engine/client/AuthUi.cpp engine/render/AuthGlyphPass.cpp
git commit -m "fix(ui): page d'erreur — messages empilés verticalement (rouge/orange)"
```

---

## Critères de succès

| # | Critère | Test |
|---|---------|------|
| 1 | Le master s'enregistre en DB au démarrage | `SELECT * FROM game_servers` après lancement |
| 2 | La liste des serveurs est non vide après auth | Se connecter et observer la page serveurs |
| 3 | Aucune superposition de texte | Naviguer entre toutes les pages |
| 4 | Tous les boutons répondent au clic souris | Tester boutons inscription et retour |
| 5 | Sélecteurs de date utilisables à la souris | Ouvrir dropdown "Mois", sélectionner "Mars" |
| 6 | Logo "i" s'affiche et ouvre un popup opaque | Cliquer sur "i" sur la page login |
| 7 | Rectangle blanc au lancement disparu | Lancer le client 5 fois |
| 8 | Logs horodatés `[JJ/MM/AAAA][HH:MM:SS]` | Observer la console serveur |
| 9 | Bannière de démarrage affichée | Observer la console au lancement |
| 10 | L'écran de langue ne se superpose plus | Sélectionner langue, revenir à login |
| 11 | Messages d'erreur empilés verticalement | Provoquer plusieurs erreurs simultanées |
| 12 | Page de confirmation email post-inscription | S'inscrire avec un compte valide |
| 13 | Pas d'affichage du nombre de comptes | Observer la page d'inscription |
