# Plan d'implémentation — Personnalisation de personnage, Slice 1 (fondation de données, autorité serveur)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rendre le serveur autoritaire sur la personnalisation de personnage : un catalogue data-driven par race, un validateur partagé, et la persistance/renvoi de la customization, en étendant le système M39.1 existant sans le dupliquer.

**Architecture:** Code partagé dans `src/shared/Character/` (PascalCase, ajouté à la lib `engine_core`, compile sous Linux et est réutilisable par le client en slice 2). La customization circule sur le wire dans un bloc binaire versionné (`CharacterCustomization` étendue) et est stockée en JSON dans la colonne existante `characters.appearance_json` (aucune migration). Le master valide à la création contre le catalogue avant l'INSERT et renvoie la customization dans la liste des personnages.

**Tech Stack:** C++20, `ByteReader`/`ByteWriter` (protocole v1 LE), parseur JSON maison (pas de nlohmann), MySQL via `engine::server::db` (`DbQuery`/`DbExecute`), macros `LOG_*`, harnais de test maison (exécutables autonomes `int main` + `add_test`), CMake (listes explicites).

**Référence spec :** `docs/superpowers/specs/2026-05-20-character-customization-design.md`.

**Hors périmètre (slices ultérieures) :** métriques continues (taille/proportions), morph targets, attachement/rendu de mesh, capsule, pipeline d'assets, équipement, UI client.

---

## Carte des fichiers

**Nouveaux (lib `engine_core`, dossier `src/shared/Character/` en PascalCase) :**
- `Json.h` / `Json.cpp` — mini-parseur JSON réutilisable (`engine::json::Value` + `Parse`).
- `CustomizationJson.h` / `CustomizationJson.cpp` — (dé)sérialisation `CharacterCustomization` ↔ JSON (`appearance_json`).
- `CustomizationCatalog.h` / `CustomizationCatalog.cpp` — chargement du catalogue par race (modules + features + tailles de palettes).
- `CustomizationValidator.h` / `CustomizationValidator.cpp` — validation d'une `CharacterCustomization` contre le catalogue.

**Nouveaux tests (exécutables autonomes) :**
- `src/shared/Character/JsonTests.cpp`
- `src/shared/Character/CustomizationJsonTests.cpp`
- `src/shared/Character/CustomizationCatalogTests.cpp`
- `src/shared/Character/CustomizationValidatorTests.cpp`
- `src/shared/network/CharacterCreatePayloadsTests.cpp`

**Nouvelles données :** `game/data/races/customization/{humains,elfes,orcs,nains,morts_vivants,corrompus,divins,demons}.json`

**Modifiés :**
- `src/shared/network/CharacterPayloads.h` (champs struct), `CharacterPayloads.cpp` (bloc wire + create/list build/parse)
- `src/shared/network/CharacterListPayloadsTests.cpp` (round-trip customization)
- `src/masterd/handlers/character/CharacterCreateHandler.h` / `.cpp` (validation + sérialisation + injection catalogue)
- `src/masterd/handlers/character/CharacterListHandler.cpp` (SELECT + désérialisation)
- `src/masterd/main_linux.cpp` (construction/chargement/injection du catalogue)
- `CMakeLists.txt` (sources `engine_core` + nouveaux exécutables de test)

**Conventions de build :** les nouveaux `.cpp` partagés vont dans la liste de sources `add_library(engine_core …)` (root `CMakeLists.txt`, débute ligne 206 ; ancre : `src/shared/network/CharacterPayloads.cpp` ligne 585). Les nouveaux tests vont dans la section tests (root `CMakeLists.txt`, à partir de la ligne ~795) au format `add_executable(<n> <src>)` + `target_link_libraries(<n> PRIVATE engine_core)` + `add_test(NAME <n> COMMAND <n>)`. Les handlers/`main_linux.cpp` sont déjà compilés via `src/CMakeLists.txt` (lignes 112+) et linkent `engine_core` — aucune entrée CMake nouvelle pour eux.

---

## Task 1 : Mini-parseur JSON partagé

**Files:**
- Create: `src/shared/Character/Json.h`
- Create: `src/shared/Character/Json.cpp`
- Test: `src/shared/Character/JsonTests.cpp`
- Modify: `CMakeLists.txt` (sources `engine_core` + test)

- [ ] **Step 1 : Écrire le header**

Create `src/shared/Character/Json.h` :

```cpp
#pragma once
// Mini-parseur JSON partagé (le projet n'expose pas de bibliothèque JSON ;
// repris du parser éprouvé de SlashCommandRegistry.cpp, généralisé en
// engine::json). Subset : object, array, string, number, bool, null.
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::json
{
    struct Value;
    using Object = std::unordered_map<std::string, Value>;
    using Array  = std::vector<Value>;

    struct Value
    {
        enum class Type { Null, Bool, Number, String, Array, Object };
        Type        type = Type::Null;
        bool        b    = false;
        double      n    = 0.0;
        std::string s;
        Array       a;
        Object      o;

        /// Retourne le membre \p key si \c this est un objet le contenant, sinon nullptr.
        const Value* Find(const std::string& key) const
        {
            if (type != Type::Object) return nullptr;
            auto it = o.find(key);
            return it == o.end() ? nullptr : &it->second;
        }
    };

    /// Parse un document JSON UTF-8 complet. Retourne true si tout le document
    /// est consommé sans erreur.
    bool Parse(const std::string& src, Value& out);
}
```

- [ ] **Step 2 : Écrire le test (qui échoue)**

Create `src/shared/Character/JsonTests.cpp` :

```cpp
// Tests du mini-parseur JSON. Retourne 0 si OK, non-zéro au premier échec.
#include "src/shared/Character/Json.h"

#include <iostream>

namespace
{
    int s_fail = 0;
    void Assert(bool cond, const char* msg)
    {
        if (!cond) { ++s_fail; std::cerr << "[FAIL] " << msg << std::endl; }
    }
}

int main()
{
    using engine::json::Value;

    {
        Value v;
        bool ok = engine::json::Parse(R"({"a":1,"b":"x","c":[true,false],"d":{"e":2}})", v);
        Assert(ok, "parse object ok");
        Assert(v.type == Value::Type::Object, "root is object");
        const Value* a = v.Find("a");
        Assert(a && a->type == Value::Type::Number && a->n == 1.0, "a == 1");
        const Value* b = v.Find("b");
        Assert(b && b->type == Value::Type::String && b->s == "x", "b == x");
        const Value* c = v.Find("c");
        Assert(c && c->type == Value::Type::Array && c->a.size() == 2, "c is array[2]");
        Assert(c && c->a[0].type == Value::Type::Bool && c->a[0].b, "c[0] true");
        const Value* d = v.Find("d");
        Assert(d && d->Find("e") && d->Find("e")->n == 2.0, "d.e == 2");
    }
    {
        Value v;
        Assert(!engine::json::Parse("{bad", v), "malformed rejected");
    }
    {
        Value v;
        bool ok = engine::json::Parse(R"(["s1","s2","s3"])", v);
        Assert(ok && v.type == Value::Type::Array && v.a.size() == 3, "string array");
        Assert(v.a[2].type == Value::Type::String && v.a[2].s == "s3", "array[2]==s3");
    }

    return s_fail == 0 ? 0 : 1;
}
```

- [ ] **Step 3 : Vérifier que le test échoue (lien manquant)**

Run: `cmake --build --preset linux-x64 --target json_tests`
Expected: FAIL — la cible `json_tests` n'existe pas encore (et `Json.cpp` absent).

- [ ] **Step 4 : Écrire l'implémentation**

Create `src/shared/Character/Json.cpp` :

```cpp
#include "src/shared/Character/Json.h"

#include <cctype>
#include <cstddef>

namespace engine::json
{
    namespace
    {
        void SkipWs(const std::string& src, size_t& pos)
        {
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])))
                ++pos;
        }

        bool ParseValue(const std::string& src, size_t& pos, Value& out);

        bool ParseStringLit(const std::string& src, size_t& pos, std::string& out)
        {
            if (pos >= src.size() || src[pos] != '"') return false;
            ++pos;
            out.clear();
            while (pos < src.size())
            {
                const char c = src[pos++];
                if (c == '"') return true;
                if (c == '\\')
                {
                    if (pos >= src.size()) return false;
                    const char e = src[pos++];
                    switch (e)
                    {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u':
                        if (pos + 4 > src.size()) return false;
                        pos += 4;
                        out.push_back('?');
                        break;
                    default: return false;
                    }
                }
                else
                {
                    out.push_back(c);
                }
            }
            return false;
        }

        bool ParseObject(const std::string& src, size_t& pos, Value& out)
        {
            if (pos >= src.size() || src[pos] != '{') return false;
            ++pos;
            out.type = Value::Type::Object;
            SkipWs(src, pos);
            if (pos < src.size() && src[pos] == '}') { ++pos; return true; }
            while (pos < src.size())
            {
                SkipWs(src, pos);
                std::string key;
                if (!ParseStringLit(src, pos, key)) return false;
                SkipWs(src, pos);
                if (pos >= src.size() || src[pos] != ':') return false;
                ++pos;
                SkipWs(src, pos);
                Value val;
                if (!ParseValue(src, pos, val)) return false;
                out.o.emplace(std::move(key), std::move(val));
                SkipWs(src, pos);
                if (pos < src.size() && src[pos] == ',') { ++pos; continue; }
                if (pos < src.size() && src[pos] == '}') { ++pos; return true; }
                return false;
            }
            return false;
        }

        bool ParseArray(const std::string& src, size_t& pos, Value& out)
        {
            if (pos >= src.size() || src[pos] != '[') return false;
            ++pos;
            out.type = Value::Type::Array;
            SkipWs(src, pos);
            if (pos < src.size() && src[pos] == ']') { ++pos; return true; }
            while (pos < src.size())
            {
                SkipWs(src, pos);
                Value val;
                if (!ParseValue(src, pos, val)) return false;
                out.a.push_back(std::move(val));
                SkipWs(src, pos);
                if (pos < src.size() && src[pos] == ',') { ++pos; continue; }
                if (pos < src.size() && src[pos] == ']') { ++pos; return true; }
                return false;
            }
            return false;
        }

        bool ParseNumber(const std::string& src, size_t& pos, Value& out)
        {
            const size_t start = pos;
            if (pos < src.size() && src[pos] == '-') ++pos;
            bool any = false;
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) { any = true; ++pos; }
            if (pos < src.size() && src[pos] == '.')
            {
                ++pos;
                while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) { any = true; ++pos; }
            }
            if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E'))
            {
                ++pos;
                if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
                while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) { any = true; ++pos; }
            }
            if (!any) return false;
            out.type = Value::Type::Number;
            try { out.n = std::stod(src.substr(start, pos - start)); }
            catch (...) { return false; }
            return true;
        }

        bool ParseValue(const std::string& src, size_t& pos, Value& out)
        {
            SkipWs(src, pos);
            if (pos >= src.size()) return false;
            const char c = src[pos];
            if (c == '{') return ParseObject(src, pos, out);
            if (c == '[') return ParseArray(src, pos, out);
            if (c == '"') { out.type = Value::Type::String; return ParseStringLit(src, pos, out.s); }
            if (c == 't' && src.compare(pos, 4, "true") == 0)  { pos += 4; out.type = Value::Type::Bool; out.b = true;  return true; }
            if (c == 'f' && src.compare(pos, 5, "false") == 0) { pos += 5; out.type = Value::Type::Bool; out.b = false; return true; }
            if (c == 'n' && src.compare(pos, 4, "null") == 0)  { pos += 4; out.type = Value::Type::Null; return true; }
            return ParseNumber(src, pos, out);
        }
    }

    bool Parse(const std::string& src, Value& out)
    {
        size_t pos = 0;
        if (!ParseValue(src, pos, out)) return false;
        SkipWs(src, pos);
        return pos == src.size();
    }
}
```

- [ ] **Step 5 : Enregistrer dans CMake**

Dans `CMakeLists.txt`, ajouter à la liste de sources de `add_library(engine_core …)`, juste après la ligne `  src/shared/network/CharacterPayloads.cpp` :

```cmake
  src/shared/Character/Json.cpp
```

Puis dans la section tests (après le bloc `protocol_v1_tests`, vers la ligne 798), ajouter :

```cmake
# Slice 1 customization — mini JSON parser tests
add_executable(json_tests src/shared/Character/JsonTests.cpp)
target_link_libraries(json_tests PRIVATE engine_core)
add_test(NAME json_tests COMMAND json_tests)
```

- [ ] **Step 6 : Vérifier que le test passe**

Run: `cmake --build --preset linux-x64 --target json_tests && ctest --preset linux-x64 -R json_tests --output-on-failure`
Expected: PASS (1 test, 0 failure).

- [ ] **Step 7 : Commit**

```bash
git add src/shared/Character/Json.h src/shared/Character/Json.cpp src/shared/Character/JsonTests.cpp CMakeLists.txt
git commit -m "customization: add shared mini JSON parser (engine::json)"
```

---

## Task 2 : Étendre les structs CharacterCustomization / CharacterListEntry

**Files:**
- Modify: `src/shared/network/CharacterPayloads.h:13-20` (struct) et `:65-87` (entry)

- [ ] **Step 1 : Étendre `CharacterCustomization`**

Dans `src/shared/network/CharacterPayloads.h`, remplacer la struct (lignes 12-20) par :

```cpp
	/// M39.1 — Customization options chosen by the player during character creation.
	/// Slice 1 (2026-05-20) — extended with discrete identity fields.
	struct CharacterCustomization
	{
		uint8_t faceType     = 0; ///< Face type index [0, N).
		uint8_t hairStyle    = 0; ///< Hair style index [0, N).
		uint8_t skinColorIdx = 0; ///< Skin colour palette index [0, N).
		uint8_t hairColorIdx = 0; ///< Hair colour palette index [0, N).
		uint8_t eyeColorIdx  = 0; ///< Eye colour palette index [0, N).
		uint8_t bodyFrame    = 0; ///< Mesh frame index [0, N) (catalog.frames).
		uint8_t bodyType     = 0; ///< Body type index [0, N) (modules[frame].bodyTypes).
		uint8_t facialHair   = 0; ///< Facial hair index [0, N) (modules[frame].facialHair).
		std::vector<std::pair<std::string, uint8_t>> racialFeatures; ///< featureKey -> index.
	};
```

Et ajouter en tête de fichier l'include manquant (après `#include <vector>`, ligne 8) :

```cpp
#include <utility>
```

- [ ] **Step 2 : Ajouter la customization à `CharacterListEntry`**

Dans la struct `CharacterListEntry` (vers la ligne 85, juste après `std::string class_str;`), ajouter :

```cpp
		// Slice 1 — appearance customization (depuis characters.appearance_json).
		CharacterCustomization customization{};
```

- [ ] **Step 3 : Vérifier que ça compile**

Run: `cmake --build --preset linux-x64 --target engine_core`
Expected: PASS (compilation OK ; le wire ne lit/écrit pas encore les nouveaux champs — c'est la Task 3).

- [ ] **Step 4 : Commit**

```bash
git add src/shared/network/CharacterPayloads.h
git commit -m "customization: extend CharacterCustomization + list entry with identity fields"
```

---

## Task 3 : Bloc wire versionné (create + list)

**Files:**
- Modify: `src/shared/network/CharacterPayloads.cpp`
- Test: `src/shared/network/CharacterListPayloadsTests.cpp` (étendu) + `src/shared/network/CharacterCreatePayloadsTests.cpp` (nouveau)
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Écrire les tests (qui échouent)**

Create `src/shared/network/CharacterCreatePayloadsTests.cpp` :

```cpp
// Round-trip du payload CHARACTER_CREATE incluant le bloc customization versionné.
#include "src/shared/network/CharacterPayloads.h"

#include <iostream>

namespace
{
    int s_fail = 0;
    void Assert(bool cond, const char* msg)
    {
        if (!cond) { ++s_fail; std::cerr << "[FAIL] " << msg << std::endl; }
    }
}

using namespace engine::network;

int main()
{
    CharacterCustomization c;
    c.faceType = 2; c.hairStyle = 4; c.skinColorIdx = 1; c.hairColorIdx = 3; c.eyeColorIdx = 5;
    c.bodyFrame = 1; c.bodyType = 2; c.facialHair = 3;
    c.racialFeatures = { {"horns", 2}, {"tails", 1} };

    auto buf = BuildCharacterCreateRequestPayload("Alyx", "demons", "warrior", c);
    Assert(!buf.empty(), "build create payload not empty");

    auto parsed = ParseCharacterCreateRequestPayload(buf.data(), buf.size());
    Assert(parsed.has_value(), "parse create payload ok");
    if (parsed)
    {
        Assert(parsed->name == "Alyx", "name round-trips");
        Assert(parsed->raceId == "demons", "raceId round-trips");
        Assert(parsed->classId == "warrior", "classId round-trips");
        const auto& g = parsed->customization;
        Assert(g.faceType == 2 && g.hairStyle == 4 && g.skinColorIdx == 1 &&
               g.hairColorIdx == 3 && g.eyeColorIdx == 5, "5 base fields round-trip");
        Assert(g.bodyFrame == 1 && g.bodyType == 2 && g.facialHair == 3, "identity fields round-trip");
        Assert(g.racialFeatures.size() == 2, "2 features");
        Assert(g.racialFeatures.size() == 2 && g.racialFeatures[0].first == "horns" &&
               g.racialFeatures[0].second == 2, "feature 0 == horns:2");
        Assert(g.racialFeatures.size() == 2 && g.racialFeatures[1].first == "tails" &&
               g.racialFeatures[1].second == 1, "feature 1 == tails:1");
    }

    // Pas de customization : valeurs par défaut.
    auto buf2 = BuildCharacterCreateRequestPayload("Bob", "humains", "mage", CharacterCustomization{});
    auto parsed2 = ParseCharacterCreateRequestPayload(buf2.data(), buf2.size());
    Assert(parsed2.has_value() && parsed2->customization.racialFeatures.empty(), "default has no features");

    return s_fail == 0 ? 0 : 1;
}
```

Dans `src/shared/network/CharacterListPayloadsTests.cpp`, dans `TestPopulatedResponseRoundTrip`, après avoir rempli `a.class_str = "warrior";` (ligne 98), ajouter :

```cpp
			a.customization.faceType = 1;
			a.customization.bodyFrame = 1;
			a.customization.bodyType = 2;
			a.customization.racialFeatures = { {"ears", 1} };
```

Puis, dans la section de vérification de `entry0` (après `Assert(a.class_str == "humains" ... )`, vers la ligne 148), ajouter :

```cpp
			Assert(a.customization.faceType == 1, "entry0 customization faceType");
			Assert(a.customization.bodyFrame == 1, "entry0 customization bodyFrame");
			Assert(a.customization.bodyType == 2, "entry0 customization bodyType");
			Assert(a.customization.racialFeatures.size() == 1 &&
				a.customization.racialFeatures[0].first == "ears" &&
				a.customization.racialFeatures[0].second == 1, "entry0 customization feature ears:1");
```

- [ ] **Step 2 : Vérifier l'échec**

Run: `cmake --build --preset linux-x64 --target character_create_payloads_tests`
Expected: FAIL — cible inexistante (et le wire n'encode pas encore les nouveaux champs).

- [ ] **Step 3 : Ajouter les helpers de bloc dans `CharacterPayloads.cpp`**

Dans `src/shared/network/CharacterPayloads.cpp`, juste après l'ouverture du namespace (après la ligne 8 `namespace engine::network\n{`), ajouter :

```cpp
	namespace
	{
		// Slice 1 — bloc customization versionné (wire). Version 1 :
		// 9 octets fixes (version + 8 champs discrets) + featureCount + features.
		constexpr uint8_t kCustomizationBlockVersion = 1u;

		bool WriteCustomizationBlock(ByteWriter& w, const CharacterCustomization& c)
		{
			const uint8_t head[9] = {
				kCustomizationBlockVersion,
				c.faceType, c.hairStyle, c.skinColorIdx, c.hairColorIdx, c.eyeColorIdx,
				c.bodyFrame, c.bodyType, c.facialHair
			};
			if (!w.WriteBytes(head, sizeof(head)))
				return false;
			const size_t n = c.racialFeatures.size() > 255u ? 255u : c.racialFeatures.size();
			const uint8_t featureCount = static_cast<uint8_t>(n);
			if (!w.WriteBytes(&featureCount, 1u))
				return false;
			for (size_t i = 0; i < n; ++i)
			{
				if (!w.WriteString(c.racialFeatures[i].first))
					return false;
				const uint8_t idx = c.racialFeatures[i].second;
				if (!w.WriteBytes(&idx, 1u))
					return false;
			}
			return true;
		}

		bool ReadCustomizationBlock(ByteReader& r, CharacterCustomization& c)
		{
			uint8_t head[9] = {0};
			if (!r.ReadBytes(head, sizeof(head)))
				return false;
			if (head[0] != kCustomizationBlockVersion)
				return false; // version inconnue / plus récente → rejet propre
			c.faceType     = head[1];
			c.hairStyle    = head[2];
			c.skinColorIdx = head[3];
			c.hairColorIdx = head[4];
			c.eyeColorIdx  = head[5];
			c.bodyFrame    = head[6];
			c.bodyType     = head[7];
			c.facialHair   = head[8];
			uint8_t featureCount = 0;
			if (!r.ReadBytes(&featureCount, 1u))
				return false;
			c.racialFeatures.clear();
			c.racialFeatures.reserve(featureCount);
			for (uint8_t i = 0; i < featureCount; ++i)
			{
				std::string key;
				uint8_t idx = 0;
				if (!r.ReadString(key) || !r.ReadBytes(&idx, 1u))
					return false;
				c.racialFeatures.emplace_back(std::move(key), idx);
			}
			return true;
		}
	}
```

- [ ] **Step 4 : Remplacer le tail customization de create (Build + Parse)**

Dans `BuildCharacterCreateRequestPayload`, remplacer le bloc des 5 octets (lignes ~89-98, du commentaire `// Customization (5 bytes).` jusqu'au `if (!w.WriteBytes(customBytes, 5u)) return {};`) par :

```cpp
		// Customization — bloc versionné (slice 1). Wire-breaking vs le format 5 octets de M39.1.
		if (!WriteCustomizationBlock(w, customization))
			return {};
```

Dans `ParseCharacterCreateRequestPayload`, remplacer les 5 lectures optionnelles (lignes ~38-63, du commentaire `// Customization: 5 bytes, all optional.` jusqu'à la dernière lecture `eyeColorIdx`) par :

```cpp
		// Customization — bloc versionné (slice 1). Optionnel : absent → valeurs par défaut.
		if (r.Remaining() > 0)
		{
			if (!ReadCustomizationBlock(r, out.customization))
				return std::nullopt;
		}
```

- [ ] **Step 5 : Ajouter le bloc dans la réponse list (Build + Parse)**

Dans `BuildCharacterListResponsePacket`, dans la boucle des entrées, juste après `if (!w.WriteString(e.race_str) || !w.WriteString(e.class_str)) return {};` (ligne ~256), ajouter :

```cpp
					// Slice 1 — bloc customization versionné.
					if (!WriteCustomizationBlock(w, e.customization))
						return {};
```

Dans `ParseCharacterListResponsePayload`, dans la boucle, juste après `if (!r.ReadString(e.race_str) || !r.ReadString(e.class_str)) return std::nullopt;` (ligne ~213), ajouter :

```cpp
				// Slice 1 — bloc customization versionné.
				if (!ReadCustomizationBlock(r, e.customization))
					return std::nullopt;
```

- [ ] **Step 6 : Enregistrer le nouveau test dans CMake**

Dans `CMakeLists.txt`, après le bloc `character_list_payloads_tests` (vers la ligne 803), ajouter :

```cmake
# Slice 1 customization — CHARACTER_CREATE payload round-trip tests
add_executable(character_create_payloads_tests src/shared/network/CharacterCreatePayloadsTests.cpp)
target_link_libraries(character_create_payloads_tests PRIVATE engine_core)
add_test(NAME character_create_payloads_tests COMMAND character_create_payloads_tests)
```

- [ ] **Step 7 : Vérifier que les tests passent**

Run: `cmake --build --preset linux-x64 --target character_create_payloads_tests --target character_list_payloads_tests && ctest --preset linux-x64 -R "character_create_payloads_tests|character_list_payloads_tests" --output-on-failure`
Expected: PASS (2 tests, 0 failure).

- [ ] **Step 8 : Commit**

```bash
git add src/shared/network/CharacterPayloads.cpp src/shared/network/CharacterCreatePayloadsTests.cpp src/shared/network/CharacterListPayloadsTests.cpp CMakeLists.txt
git commit -m "customization: versioned wire block for create + list payloads"
```

---

## Task 4 : (Dé)sérialisation JSON pour `appearance_json`

**Files:**
- Create: `src/shared/Character/CustomizationJson.h` / `.cpp`
- Test: `src/shared/Character/CustomizationJsonTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Écrire le header**

Create `src/shared/Character/CustomizationJson.h` :

```cpp
#pragma once
// (Dé)sérialisation JSON de CharacterCustomization pour la colonne
// characters.appearance_json. Forward-compatible : l'ajout de clés en slice 2
// ne casse pas la lecture (clés inconnues ignorées).
#include "src/shared/network/CharacterPayloads.h"

#include <string>

namespace engine::character
{
    /// Sérialise en objet JSON compact (clé "v" = version de schéma = 1).
    std::string CustomizationToJson(const engine::network::CharacterCustomization& c);

    /// Parse un JSON appearance ; valeurs absentes/invalides → défauts. Robuste
    /// à `'{}'` et aux chaînes vides (lignes pré-slice-1).
    engine::network::CharacterCustomization CustomizationFromJson(const std::string& jsonStr);
}
```

- [ ] **Step 2 : Écrire le test (qui échoue)**

Create `src/shared/Character/CustomizationJsonTests.cpp` :

```cpp
#include "src/shared/Character/CustomizationJson.h"

#include <iostream>

namespace
{
    int s_fail = 0;
    void Assert(bool cond, const char* msg)
    {
        if (!cond) { ++s_fail; std::cerr << "[FAIL] " << msg << std::endl; }
    }
}

using engine::network::CharacterCustomization;

int main()
{
    CharacterCustomization c;
    c.faceType = 2; c.hairStyle = 4; c.skinColorIdx = 1; c.hairColorIdx = 3; c.eyeColorIdx = 5;
    c.bodyFrame = 1; c.bodyType = 2; c.facialHair = 3;
    c.racialFeatures = { {"horns", 2}, {"tails", 1} };

    const std::string json = engine::character::CustomizationToJson(c);
    Assert(!json.empty() && json.front() == '{', "json starts with brace");

    const CharacterCustomization r = engine::character::CustomizationFromJson(json);
    Assert(r.faceType == 2 && r.hairStyle == 4 && r.skinColorIdx == 1 &&
           r.hairColorIdx == 3 && r.eyeColorIdx == 5, "base fields round-trip");
    Assert(r.bodyFrame == 1 && r.bodyType == 2 && r.facialHair == 3, "identity fields round-trip");
    Assert(r.racialFeatures.size() == 2, "2 features round-trip");

    // Robustesse : entrée vide / '{}' → défauts, pas de crash.
    const CharacterCustomization empty1 = engine::character::CustomizationFromJson("");
    const CharacterCustomization empty2 = engine::character::CustomizationFromJson("{}");
    Assert(empty1.faceType == 0 && empty1.racialFeatures.empty(), "empty string -> defaults");
    Assert(empty2.faceType == 0 && empty2.racialFeatures.empty(), "'{}' -> defaults");

    return s_fail == 0 ? 0 : 1;
}
```

- [ ] **Step 3 : Vérifier l'échec**

Run: `cmake --build --preset linux-x64 --target customization_json_tests`
Expected: FAIL — cible/source absentes.

- [ ] **Step 4 : Écrire l'implémentation**

Create `src/shared/Character/CustomizationJson.cpp` :

```cpp
#include "src/shared/Character/CustomizationJson.h"

#include "src/shared/Character/Json.h"

#include <string>

namespace engine::character
{
    namespace
    {
        std::string Escape(const std::string& in)
        {
            std::string out;
            out.reserve(in.size() + 2);
            for (char ch : in)
            {
                if (ch == '"' || ch == '\\') out.push_back('\\');
                out.push_back(ch);
            }
            return out;
        }

        uint8_t AsU8(const engine::json::Value* v)
        {
            if (!v || v->type != engine::json::Value::Type::Number) return 0u;
            double d = v->n;
            if (d < 0.0) d = 0.0;
            if (d > 255.0) d = 255.0;
            return static_cast<uint8_t>(d);
        }
    }

    std::string CustomizationToJson(const engine::network::CharacterCustomization& c)
    {
        std::string out = "{\"v\":1";
        out += ",\"face\":"       + std::to_string(c.faceType);
        out += ",\"hair\":"       + std::to_string(c.hairStyle);
        out += ",\"skin\":"       + std::to_string(c.skinColorIdx);
        out += ",\"hairColor\":"  + std::to_string(c.hairColorIdx);
        out += ",\"eye\":"        + std::to_string(c.eyeColorIdx);
        out += ",\"frame\":"      + std::to_string(c.bodyFrame);
        out += ",\"body\":"       + std::to_string(c.bodyType);
        out += ",\"facialHair\":" + std::to_string(c.facialHair);
        out += ",\"features\":{";
        for (size_t i = 0; i < c.racialFeatures.size(); ++i)
        {
            if (i) out += ",";
            out += "\"" + Escape(c.racialFeatures[i].first) + "\":" + std::to_string(c.racialFeatures[i].second);
        }
        out += "}}";
        return out;
    }

    engine::network::CharacterCustomization CustomizationFromJson(const std::string& jsonStr)
    {
        engine::network::CharacterCustomization c;
        engine::json::Value root;
        if (!engine::json::Parse(jsonStr, root) || root.type != engine::json::Value::Type::Object)
            return c;

        c.faceType     = AsU8(root.Find("face"));
        c.hairStyle    = AsU8(root.Find("hair"));
        c.skinColorIdx = AsU8(root.Find("skin"));
        c.hairColorIdx = AsU8(root.Find("hairColor"));
        c.eyeColorIdx  = AsU8(root.Find("eye"));
        c.bodyFrame    = AsU8(root.Find("frame"));
        c.bodyType     = AsU8(root.Find("body"));
        c.facialHair   = AsU8(root.Find("facialHair"));

        if (const engine::json::Value* feats = root.Find("features");
            feats && feats->type == engine::json::Value::Type::Object)
        {
            for (const auto& [key, val] : feats->o)
            {
                if (val.type == engine::json::Value::Type::Number)
                    c.racialFeatures.emplace_back(key, AsU8(&val));
            }
        }
        return c;
    }
}
```

- [ ] **Step 5 : Enregistrer dans CMake**

Dans `CMakeLists.txt`, ajouter à la liste de sources `engine_core`, après `src/shared/Character/Json.cpp` :

```cmake
  src/shared/Character/CustomizationJson.cpp
```

Et dans la section tests, après `json_tests` :

```cmake
add_executable(customization_json_tests src/shared/Character/CustomizationJsonTests.cpp)
target_link_libraries(customization_json_tests PRIVATE engine_core)
add_test(NAME customization_json_tests COMMAND customization_json_tests)
```

- [ ] **Step 6 : Vérifier que le test passe**

Run: `cmake --build --preset linux-x64 --target customization_json_tests && ctest --preset linux-x64 -R customization_json_tests --output-on-failure`
Expected: PASS.

- [ ] **Step 7 : Commit**

```bash
git add src/shared/Character/CustomizationJson.h src/shared/Character/CustomizationJson.cpp src/shared/Character/CustomizationJsonTests.cpp CMakeLists.txt
git commit -m "customization: JSON (de)serialization for appearance_json"
```

---

## Task 5 : Fichiers de catalogue par race (données)

**Files:**
- Create: `game/data/races/customization/{humains,elfes,orcs,nains,morts_vivants,corrompus,divins,demons}.json`

Aucun test ici (couvert par la Task 6 qui charge ces fichiers réels). Ids alignés sur `game/data/races/races.json`. Les `racialFeatures` sont génériques (clé → liste d'ids). Toutes les races ont les frames `["masculine","feminine"]`.

- [ ] **Step 1 : Créer `game/data/races/customization/humains.json`**

```json
{
  "version": "1.0.0",
  "raceId": "humains",
  "frames": ["masculine", "feminine"],
  "modules": {
    "masculine": {
      "bodyTypes":  ["base", "muscular", "lean"],
      "faces":      ["face_01", "face_02", "face_03", "face_04", "face_05"],
      "hair":       ["short_01", "short_02", "medium_01", "long_01", "bald"],
      "facialHair": ["none", "beard_full", "goatee", "mustache"]
    },
    "feminine": {
      "bodyTypes":  ["base", "athletic", "curvy"],
      "faces":      ["face_01", "face_02", "face_03", "face_04", "face_05"],
      "hair":       ["short_01", "medium_01", "long_01", "long_02", "braided_01", "ponytail_01"],
      "facialHair": ["none"]
    }
  },
  "racialFeatures": {}
}
```

- [ ] **Step 2 : Créer `game/data/races/customization/elfes.json`**

```json
{
  "version": "1.0.0",
  "raceId": "elfes",
  "frames": ["masculine", "feminine"],
  "modules": {
    "masculine": {
      "bodyTypes":  ["base", "lean"],
      "faces":      ["face_01", "face_02", "face_03"],
      "hair":       ["long_01", "long_02", "braided_01"],
      "facialHair": ["none", "beard_thin"]
    },
    "feminine": {
      "bodyTypes":  ["base", "athletic"],
      "faces":      ["face_01", "face_02", "face_03"],
      "hair":       ["long_01", "long_02", "long_03", "braided_01"],
      "facialHair": ["none"]
    }
  },
  "racialFeatures": {
    "ears": ["pointed_short", "pointed_medium", "pointed_long"]
  }
}
```

- [ ] **Step 3 : Créer `game/data/races/customization/orcs.json`**

```json
{
  "version": "1.0.0",
  "raceId": "orcs",
  "frames": ["masculine", "feminine"],
  "modules": {
    "masculine": {
      "bodyTypes":  ["base", "massive", "hulking"],
      "faces":      ["face_01", "face_02", "face_03"],
      "hair":       ["mohawk_01", "topknot_01", "braids_01", "shaved"],
      "facialHair": ["none", "beard_braided"]
    },
    "feminine": {
      "bodyTypes":  ["base", "strong"],
      "faces":      ["face_01", "face_02"],
      "hair":       ["long_01", "braids_01"],
      "facialHair": ["none"]
    }
  },
  "racialFeatures": {
    "tusks": ["small", "medium", "large", "broken"]
  }
}
```

- [ ] **Step 4 : Créer `game/data/races/customization/nains.json`**

```json
{
  "version": "1.0.0",
  "raceId": "nains",
  "frames": ["masculine", "feminine"],
  "modules": {
    "masculine": {
      "bodyTypes":  ["base", "stout", "broad"],
      "faces":      ["face_01", "face_02", "face_03"],
      "hair":       ["short_01", "medium_01"],
      "facialHair": ["beard_braided_long", "beard_braided_short", "beard_forked", "beard_full_massive", "beard_full_medium"]
    },
    "feminine": {
      "bodyTypes":  ["base", "stout", "broad"],
      "faces":      ["face_01", "face_02", "face_03"],
      "hair":       ["short_01", "medium_01", "braided_01", "long_01"],
      "facialHair": ["none"]
    }
  },
  "racialFeatures": {}
}
```

- [ ] **Step 5 : Créer `game/data/races/customization/morts_vivants.json`**

```json
{
  "version": "1.0.0",
  "raceId": "morts_vivants",
  "frames": ["masculine", "feminine"],
  "modules": {
    "masculine": {
      "bodyTypes":  ["base", "gaunt"],
      "faces":      ["face_01", "face_02", "face_03"],
      "hair":       ["short_01", "long_01", "bald"],
      "facialHair": ["none"]
    },
    "feminine": {
      "bodyTypes":  ["base", "gaunt"],
      "faces":      ["face_01", "face_02", "face_03"],
      "hair":       ["short_01", "long_01", "bald"],
      "facialHair": ["none"]
    }
  },
  "racialFeatures": {
    "exposed_bone": ["none", "jaw", "ribs"]
  }
}
```

- [ ] **Step 6 : Créer `game/data/races/customization/corrompus.json`**

```json
{
  "version": "1.0.0",
  "raceId": "corrompus",
  "frames": ["masculine", "feminine"],
  "modules": {
    "masculine": {
      "bodyTypes":  ["base", "muscular"],
      "faces":      ["face_01", "face_02", "face_03"],
      "hair":       ["short_01", "medium_01", "long_01"],
      "facialHair": ["none", "goatee"]
    },
    "feminine": {
      "bodyTypes":  ["base", "athletic"],
      "faces":      ["face_01", "face_02"],
      "hair":       ["medium_01", "long_01"],
      "facialHair": ["none"]
    }
  },
  "racialFeatures": {
    "corruption_marks": ["none", "veins", "spikes"]
  }
}
```

- [ ] **Step 7 : Créer `game/data/races/customization/divins.json`**

```json
{
  "version": "1.0.0",
  "raceId": "divins",
  "frames": ["masculine", "feminine"],
  "modules": {
    "masculine": {
      "bodyTypes":  ["base", "muscular"],
      "faces":      ["face_01", "face_02", "face_03"],
      "hair":       ["short_01", "medium_01", "long_01"],
      "facialHair": ["none", "beard_short"]
    },
    "feminine": {
      "bodyTypes":  ["base", "athletic"],
      "faces":      ["face_01", "face_02", "face_03"],
      "hair":       ["medium_01", "long_01", "long_02"],
      "facialHair": ["none"]
    }
  },
  "racialFeatures": {
    "halo": ["none", "ring", "radiant"]
  }
}
```

- [ ] **Step 8 : Créer `game/data/races/customization/demons.json`**

```json
{
  "version": "1.0.0",
  "raceId": "demons",
  "frames": ["masculine", "feminine"],
  "modules": {
    "masculine": {
      "bodyTypes":  ["base", "muscular"],
      "faces":      ["face_01", "face_02", "face_03"],
      "hair":       ["short_01", "medium_01"],
      "facialHair": ["none", "goatee"]
    },
    "feminine": {
      "bodyTypes":  ["base", "athletic"],
      "faces":      ["face_01", "face_02"],
      "hair":       ["long_01", "medium_01"],
      "facialHair": ["none"]
    }
  },
  "racialFeatures": {
    "horns": ["none", "curved_01", "straight_01", "ram_style"],
    "tails": ["none", "spaded_long", "thin_long"]
  }
}
```

- [ ] **Step 9 : Vérifier la validité JSON**

Run: `for f in game/data/races/customization/*.json; do python -c "import json,sys; json.load(open(sys.argv[1]))" "$f" && echo "OK $f"; done`
Expected: 8 lignes `OK …`, aucune exception.

- [ ] **Step 10 : Commit**

```bash
git add game/data/races/customization/
git commit -m "customization: add per-race customization catalog data (8 races)"
```

---

## Task 6 : Chargeur `CustomizationCatalog`

**Files:**
- Create: `src/shared/Character/CustomizationCatalog.h` / `.cpp`
- Test: `src/shared/Character/CustomizationCatalogTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Écrire le header**

Create `src/shared/Character/CustomizationCatalog.h` :

```cpp
#pragma once
// Catalogue de personnalisation par race, data-driven. Lit game/data/races/races.json
// (ids + tailles de palettes couleurs) et game/data/races/customization/<id>.json
// (modules par frame + features raciales). Read-only après chargement.
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::character
{
    struct FrameModules
    {
        std::vector<std::string> bodyTypes;
        std::vector<std::string> faces;
        std::vector<std::string> hair;
        std::vector<std::string> facialHair;
    };

    struct RaceCustomization
    {
        std::vector<std::string> frames;                         ///< ex. ["masculine","feminine"]
        std::unordered_map<std::string, FrameModules> modules;   ///< frame -> modules
        std::unordered_map<std::string, std::vector<std::string>> racialFeatures; ///< key -> ids
        uint32_t skinColorCount = 0;                             ///< depuis races.json
        uint32_t hairColorCount = 0;
        uint32_t eyeColorCount  = 0;
    };

    class CustomizationCatalog
    {
    public:
        /// Charge depuis le dossier des races (\p racesDir doit contenir races.json
        /// et le sous-dossier customization/). Retourne true si au moins une race chargée.
        bool LoadFromDir(const std::string& racesDir);

        /// Insère/remplace une race (utilisé par le chargeur et par les tests).
        void Set(const std::string& raceId, RaceCustomization rc);

        /// Retourne la config d'une race ou nullptr.
        const RaceCustomization* Find(const std::string& raceId) const;

        /// Nombre de races chargées.
        std::size_t Size() const { return m_races.size(); }

    private:
        std::unordered_map<std::string, RaceCustomization> m_races;
    };
}
```

- [ ] **Step 2 : Écrire le test (qui échoue)**

Create `src/shared/Character/CustomizationCatalogTests.cpp` :

```cpp
#include "src/shared/Character/CustomizationCatalog.h"

#include <iostream>
#include <string>

#ifndef LCDLLN_DATA_DIR
#define LCDLLN_DATA_DIR "game/data"
#endif

namespace
{
    int s_fail = 0;
    void Assert(bool cond, const char* msg)
    {
        if (!cond) { ++s_fail; std::cerr << "[FAIL] " << msg << std::endl; }
    }
}

int main()
{
    engine::character::CustomizationCatalog cat;
    const std::string racesDir = std::string(LCDLLN_DATA_DIR) + "/races";
    const bool ok = cat.LoadFromDir(racesDir);
    Assert(ok, "catalog loads from data dir");
    Assert(cat.Size() == 8u, "8 races loaded");

    const auto* humains = cat.Find("humains");
    Assert(humains != nullptr, "humains present");
    if (humains)
    {
        Assert(humains->frames.size() == 2u, "humains has 2 frames");
        auto it = humains->modules.find("masculine");
        Assert(it != humains->modules.end(), "humains masculine modules present");
        Assert(it != humains->modules.end() && it->second.faces.size() == 5u, "humains masculine 5 faces");
        Assert(it != humains->modules.end() && it->second.bodyTypes.size() == 3u, "humains masculine 3 body types");
        Assert(humains->skinColorCount == 4u, "humains 4 skin colors (races.json)");
        Assert(humains->hairColorCount == 6u, "humains 6 hair colors (races.json)");
        Assert(humains->eyeColorCount == 4u, "humains 4 eye colors (races.json)");
        Assert(humains->racialFeatures.empty(), "humains has no racial features");
    }

    const auto* demons = cat.Find("demons");
    Assert(demons != nullptr, "demons present");
    if (demons)
    {
        auto h = demons->racialFeatures.find("horns");
        Assert(h != demons->racialFeatures.end() && h->second.size() == 4u, "demons horns has 4 ids");
    }

    Assert(cat.Find("orkh") == nullptr, "unknown race not found");

    return s_fail == 0 ? 0 : 1;
}
```

- [ ] **Step 3 : Vérifier l'échec**

Run: `cmake --build --preset linux-x64 --target customization_catalog_tests`
Expected: FAIL — cible/source absentes.

- [ ] **Step 4 : Écrire l'implémentation**

Create `src/shared/Character/CustomizationCatalog.cpp` :

```cpp
#include "src/shared/Character/CustomizationCatalog.h"

#include "src/shared/Character/Json.h"
#include "src/shared/core/Log.h"

#include <fstream>
#include <sstream>

namespace engine::character
{
    namespace
    {
        bool ReadFile(const std::string& path, std::string& out)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            std::ostringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return true;
        }

        // Remplit \p dst avec les chaînes d'un array JSON (ignore les non-strings).
        void ReadStringArray(const engine::json::Value* arr, std::vector<std::string>& dst)
        {
            if (!arr || arr->type != engine::json::Value::Type::Array) return;
            for (const auto& item : arr->a)
                if (item.type == engine::json::Value::Type::String)
                    dst.push_back(item.s);
        }

        uint32_t ArraySize(const engine::json::Value* arr)
        {
            return (arr && arr->type == engine::json::Value::Type::Array)
                ? static_cast<uint32_t>(arr->a.size()) : 0u;
        }
    }

    void CustomizationCatalog::Set(const std::string& raceId, RaceCustomization rc)
    {
        m_races[raceId] = std::move(rc);
    }

    const RaceCustomization* CustomizationCatalog::Find(const std::string& raceId) const
    {
        auto it = m_races.find(raceId);
        return it == m_races.end() ? nullptr : &it->second;
    }

    bool CustomizationCatalog::LoadFromDir(const std::string& racesDir)
    {
        m_races.clear();

        // 1. races.json : ids + tailles de palettes couleurs.
        std::string racesRaw;
        if (!ReadFile(racesDir + "/races.json", racesRaw))
        {
            LOG_WARN(Auth, "[CustomizationCatalog] cannot read {}/races.json", racesDir);
            return false;
        }
        engine::json::Value racesRoot;
        if (!engine::json::Parse(racesRaw, racesRoot) || racesRoot.type != engine::json::Value::Type::Object)
        {
            LOG_WARN(Auth, "[CustomizationCatalog] races.json parse failed");
            return false;
        }
        const engine::json::Value* racesArr = racesRoot.Find("races");
        if (!racesArr || racesArr->type != engine::json::Value::Type::Array)
        {
            LOG_WARN(Auth, "[CustomizationCatalog] races.json missing 'races' array");
            return false;
        }

        for (const auto& raceNode : racesArr->a)
        {
            if (raceNode.type != engine::json::Value::Type::Object) continue;
            const engine::json::Value* idNode = raceNode.Find("id");
            if (!idNode || idNode->type != engine::json::Value::Type::String) continue;
            const std::string raceId = idNode->s;

            RaceCustomization rc;
            rc.skinColorCount = ArraySize(raceNode.Find("defaultSkinColors"));
            rc.hairColorCount = ArraySize(raceNode.Find("defaultHairColors"));
            rc.eyeColorCount  = ArraySize(raceNode.Find("defaultEyeColors"));

            // 2. customization/<id>.json : frames + modules + features.
            std::string custRaw;
            if (!ReadFile(racesDir + "/customization/" + raceId + ".json", custRaw))
            {
                LOG_WARN(Auth, "[CustomizationCatalog] missing customization file for race {}", raceId);
                continue;
            }
            engine::json::Value custRoot;
            if (!engine::json::Parse(custRaw, custRoot) || custRoot.type != engine::json::Value::Type::Object)
            {
                LOG_WARN(Auth, "[CustomizationCatalog] customization parse failed for {}", raceId);
                continue;
            }

            ReadStringArray(custRoot.Find("frames"), rc.frames);

            if (const engine::json::Value* modules = custRoot.Find("modules");
                modules && modules->type == engine::json::Value::Type::Object)
            {
                for (const auto& [frame, modVal] : modules->o)
                {
                    if (modVal.type != engine::json::Value::Type::Object) continue;
                    FrameModules fm;
                    ReadStringArray(modVal.Find("bodyTypes"),  fm.bodyTypes);
                    ReadStringArray(modVal.Find("faces"),      fm.faces);
                    ReadStringArray(modVal.Find("hair"),       fm.hair);
                    ReadStringArray(modVal.Find("facialHair"), fm.facialHair);
                    rc.modules.emplace(frame, std::move(fm));
                }
            }

            if (const engine::json::Value* feats = custRoot.Find("racialFeatures");
                feats && feats->type == engine::json::Value::Type::Object)
            {
                for (const auto& [key, idsVal] : feats->o)
                {
                    std::vector<std::string> ids;
                    ReadStringArray(&idsVal, ids);
                    rc.racialFeatures.emplace(key, std::move(ids));
                }
            }

            m_races.emplace(raceId, std::move(rc));
        }

        LOG_INFO(Auth, "[CustomizationCatalog] loaded {} races from {}", m_races.size(), racesDir);
        return !m_races.empty();
    }
}
```

- [ ] **Step 5 : Enregistrer dans CMake (avec le chemin data pour le test)**

Dans `CMakeLists.txt`, ajouter à `engine_core`, après `src/shared/Character/CustomizationJson.cpp` :

```cmake
  src/shared/Character/CustomizationCatalog.cpp
```

Et dans la section tests :

```cmake
add_executable(customization_catalog_tests src/shared/Character/CustomizationCatalogTests.cpp)
target_link_libraries(customization_catalog_tests PRIVATE engine_core)
target_compile_definitions(customization_catalog_tests PRIVATE LCDLLN_DATA_DIR="${CMAKE_SOURCE_DIR}/game/data")
add_test(NAME customization_catalog_tests COMMAND customization_catalog_tests)
```

- [ ] **Step 6 : Vérifier que le test passe**

Run: `cmake --build --preset linux-x64 --target customization_catalog_tests && ctest --preset linux-x64 -R customization_catalog_tests --output-on-failure`
Expected: PASS (8 races, comptages corrects).

- [ ] **Step 7 : Commit**

```bash
git add src/shared/Character/CustomizationCatalog.h src/shared/Character/CustomizationCatalog.cpp src/shared/Character/CustomizationCatalogTests.cpp CMakeLists.txt
git commit -m "customization: data-driven per-race catalog loader"
```

---

## Task 7 : Validateur `CustomizationValidator`

**Files:**
- Create: `src/shared/Character/CustomizationValidator.h` / `.cpp`
- Test: `src/shared/Character/CustomizationValidatorTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Écrire le header**

Create `src/shared/Character/CustomizationValidator.h` :

```cpp
#pragma once
// Validation autoritaire (serveur) d'une CharacterCustomization contre le catalogue.
#include "src/shared/Character/CustomizationCatalog.h"
#include "src/shared/network/CharacterPayloads.h"

#include <string>
#include <vector>

namespace engine::character
{
    struct ValidationResult
    {
        bool ok = true;
        std::vector<std::string> errors;
    };

    /// Valide \p c pour la race \p raceId. ok=false si la race est inconnue ou si
    /// un index/clé est hors limites. Les messages d'erreur sont anglais (logs).
    ValidationResult ValidateCustomization(const CustomizationCatalog& catalog,
                                           const std::string& raceId,
                                           const engine::network::CharacterCustomization& c);
}
```

- [ ] **Step 2 : Écrire le test (qui échoue)**

Create `src/shared/Character/CustomizationValidatorTests.cpp` :

```cpp
#include "src/shared/Character/CustomizationValidator.h"

#include <iostream>

namespace
{
    int s_fail = 0;
    void Assert(bool cond, const char* msg)
    {
        if (!cond) { ++s_fail; std::cerr << "[FAIL] " << msg << std::endl; }
    }

    // Construit un catalogue déterministe en mémoire (pas de fichiers).
    engine::character::CustomizationCatalog MakeCatalog()
    {
        using namespace engine::character;
        CustomizationCatalog cat;
        RaceCustomization rc;
        rc.frames = { "masculine", "feminine" };
        FrameModules m;
        m.bodyTypes  = { "base", "muscular" };
        m.faces      = { "f0", "f1", "f2" };
        m.hair       = { "h0", "h1" };
        m.facialHair = { "none", "goatee" };
        rc.modules["masculine"] = m;
        rc.modules["feminine"]  = m;
        rc.racialFeatures["horns"] = { "none", "curved_01" };
        rc.skinColorCount = 4;
        rc.hairColorCount = 3;
        rc.eyeColorCount  = 2;
        cat.Set("demons", std::move(rc));
        return cat;
    }
}

using engine::network::CharacterCustomization;

int main()
{
    const auto cat = MakeCatalog();

    {
        CharacterCustomization c;
        c.bodyFrame = 0; c.bodyType = 1; c.faceType = 2; c.hairStyle = 1; c.facialHair = 1;
        c.skinColorIdx = 3; c.hairColorIdx = 2; c.eyeColorIdx = 1;
        c.racialFeatures = { {"horns", 1} };
        auto r = engine::character::ValidateCustomization(cat, "demons", c);
        Assert(r.ok, "valid customization accepted");
    }
    {
        CharacterCustomization c;
        auto r = engine::character::ValidateCustomization(cat, "unknown_race", c);
        Assert(!r.ok, "unknown race rejected");
    }
    {
        CharacterCustomization c; c.bodyFrame = 5; // hors frames
        auto r = engine::character::ValidateCustomization(cat, "demons", c);
        Assert(!r.ok, "bad bodyFrame rejected");
    }
    {
        CharacterCustomization c; c.faceType = 9; // > faces.size()
        auto r = engine::character::ValidateCustomization(cat, "demons", c);
        Assert(!r.ok, "bad faceType rejected");
    }
    {
        CharacterCustomization c; c.skinColorIdx = 4; // == count -> hors borne
        auto r = engine::character::ValidateCustomization(cat, "demons", c);
        Assert(!r.ok, "bad skinColorIdx rejected");
    }
    {
        CharacterCustomization c; c.racialFeatures = { {"wings", 0} }; // clé inconnue
        auto r = engine::character::ValidateCustomization(cat, "demons", c);
        Assert(!r.ok, "unknown feature key rejected");
    }
    {
        CharacterCustomization c; c.racialFeatures = { {"horns", 9} }; // index hors borne
        auto r = engine::character::ValidateCustomization(cat, "demons", c);
        Assert(!r.ok, "out-of-range feature index rejected");
    }

    return s_fail == 0 ? 0 : 1;
}
```

- [ ] **Step 3 : Vérifier l'échec**

Run: `cmake --build --preset linux-x64 --target customization_validator_tests`
Expected: FAIL — cible/source absentes.

- [ ] **Step 4 : Écrire l'implémentation**

Create `src/shared/Character/CustomizationValidator.cpp` :

```cpp
#include "src/shared/Character/CustomizationValidator.h"

namespace engine::character
{
    ValidationResult ValidateCustomization(const CustomizationCatalog& catalog,
                                           const std::string& raceId,
                                           const engine::network::CharacterCustomization& c)
    {
        ValidationResult res;
        auto fail = [&res](std::string msg) { res.ok = false; res.errors.push_back(std::move(msg)); };

        const RaceCustomization* rc = catalog.Find(raceId);
        if (!rc)
        {
            fail("unknown race: " + raceId);
            return res; // impossible de continuer sans config de race
        }

        if (c.bodyFrame >= rc->frames.size())
        {
            fail("bodyFrame out of range");
            return res; // le frame conditionne les modules suivants
        }
        const std::string& frame = rc->frames[c.bodyFrame];

        auto it = rc->modules.find(frame);
        if (it == rc->modules.end())
        {
            fail("no modules for frame: " + frame);
            return res;
        }
        const FrameModules& m = it->second;

        if (c.bodyType   >= m.bodyTypes.size())  fail("bodyType out of range");
        if (c.faceType   >= m.faces.size())      fail("faceType out of range");
        if (c.hairStyle  >= m.hair.size())       fail("hairStyle out of range");
        if (c.facialHair >= m.facialHair.size()) fail("facialHair out of range");

        if (c.skinColorIdx >= rc->skinColorCount) fail("skinColorIdx out of range");
        if (c.hairColorIdx >= rc->hairColorCount) fail("hairColorIdx out of range");
        if (c.eyeColorIdx  >= rc->eyeColorCount)  fail("eyeColorIdx out of range");

        for (const auto& [key, idx] : c.racialFeatures)
        {
            auto fit = rc->racialFeatures.find(key);
            if (fit == rc->racialFeatures.end())
            {
                fail("unknown racial feature: " + key);
                continue;
            }
            if (idx >= fit->second.size())
                fail("racial feature index out of range: " + key);
        }

        return res;
    }
}
```

- [ ] **Step 5 : Enregistrer dans CMake**

Dans `CMakeLists.txt`, ajouter à `engine_core`, après `src/shared/Character/CustomizationCatalog.cpp` :

```cmake
  src/shared/Character/CustomizationValidator.cpp
```

Et dans la section tests :

```cmake
add_executable(customization_validator_tests src/shared/Character/CustomizationValidatorTests.cpp)
target_link_libraries(customization_validator_tests PRIVATE engine_core)
add_test(NAME customization_validator_tests COMMAND customization_validator_tests)
```

- [ ] **Step 6 : Vérifier que le test passe**

Run: `cmake --build --preset linux-x64 --target customization_validator_tests && ctest --preset linux-x64 -R customization_validator_tests --output-on-failure`
Expected: PASS (cas valides + 6 cas de rejet).

- [ ] **Step 7 : Commit**

```bash
git add src/shared/Character/CustomizationValidator.h src/shared/Character/CustomizationValidator.cpp src/shared/Character/CustomizationValidatorTests.cpp CMakeLists.txt
git commit -m "customization: server-authoritative validator"
```

---

## Task 8 : Intégration dans `CharacterCreateHandler` (valider + sérialiser)

**Files:**
- Modify: `src/masterd/handlers/character/CharacterCreateHandler.h`
- Modify: `src/masterd/handlers/character/CharacterCreateHandler.cpp`

Pas de test unitaire dédié (chemin DB ; la logique pure valider+sérialiser est déjà couverte par les Tasks 4 et 7). Vérification à la Task 10 (build + boot + smoke).

- [ ] **Step 1 : Déclarer la dépendance catalogue dans le header**

Dans `src/masterd/handlers/character/CharacterCreateHandler.h`, ajouter l'include en tête :

```cpp
#include "src/shared/Character/CustomizationCatalog.h"
```

Ajouter le setter parmi les autres `Set*` (dans la section publique) :

```cpp
		void SetCustomizationCatalog(const engine::character::CustomizationCatalog* catalog);
```

Et le membre privé (à côté des autres `m_*`) :

```cpp
		const engine::character::CustomizationCatalog* m_catalog = nullptr;
```

- [ ] **Step 2 : Définir le setter + inclure les utilitaires dans le .cpp**

Dans `src/masterd/handlers/character/CharacterCreateHandler.cpp`, ajouter aux includes (après la ligne 12 `#include "src/shared/db/DbHelpers.h"`) :

```cpp
#include "src/shared/Character/CustomizationValidator.h"
#include "src/shared/Character/CustomizationJson.h"

#include <vector>
```

Ajouter la définition du setter à côté des autres (après la ligne 26 `SetConfig`) :

```cpp
	void CharacterCreateHandler::SetCustomizationCatalog(const engine::character::CustomizationCatalog* catalog) { m_catalog = catalog; }
```

- [ ] **Step 3 : Valider la customization avant l'INSERT**

Dans `HandlePacket`, juste après le bloc `FindNextSlot` qui rejette si `slot < 0` (après la ligne 211 `}`) et avant `char escapedName[128]{};`, insérer :

```cpp
		// Slice 1 — validation autoritaire de la customization contre le catalogue.
		if (m_catalog)
		{
			auto validation = engine::character::ValidateCustomization(*m_catalog, parsed->raceId, parsed->customization);
			if (!validation.ok)
			{
				const std::string reason = validation.errors.empty() ? "invalid customization" : validation.errors.front();
				LOG_WARN(Auth, "[CharacterCreateHandler] customization rejected (account_id={}, race='{}'): {}",
					*accountId, parsed->raceId, reason);
				auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid customization", requestId, sessionIdHeader);
				if (!pkt.empty())
					m_server->Send(connId, pkt);
				return;
			}
		}
```

- [ ] **Step 4 : Sérialiser la customization en JSON et l'écrire dans `appearance_json`**

Toujours dans `HandlePacket`, juste avant la construction de `std::string sql =` (avant la ligne 264), insérer :

```cpp
		// Slice 1 — sérialise la customization validée en JSON pour appearance_json.
		const std::string appearanceJson = engine::character::CustomizationToJson(parsed->customization);
		std::vector<char> escapedAppearance(appearanceJson.size() * 2u + 1u, '\0');
		mysql_real_escape_string(mysql, escapedAppearance.data(),
			appearanceJson.c_str(), static_cast<unsigned long>(appearanceJson.size()));
```

Puis, dans la construction du `std::string sql`, remplacer le littéral `'{}'` (le fragment exact `", 0, 0, 1, '{}', "` à la ligne ~270) par :

```cpp
				+ std::to_string(serverId) + ", 0, 0, 1, '" + std::string(escapedAppearance.data()) + "', "
```

(c.-à-d. remplacer `+ std::to_string(serverId) + ", 0, 0, 1, '{}', "` par la ligne ci-dessus).

- [ ] **Step 5 : Vérifier la compilation**

Run: `cmake --build --preset linux-x64`
Expected: PASS (le master compile ; le handler valide et persiste la customization).

- [ ] **Step 6 : Commit**

```bash
git add src/masterd/handlers/character/CharacterCreateHandler.h src/masterd/handlers/character/CharacterCreateHandler.cpp
git commit -m "customization: validate + persist appearance on character create"
```

---

## Task 9 : Intégration dans `CharacterListHandler` (désérialiser)

**Files:**
- Modify: `src/masterd/handlers/character/CharacterListHandler.cpp`

- [ ] **Step 1 : Inclure l'utilitaire JSON**

Dans `src/masterd/handlers/character/CharacterListHandler.cpp`, ajouter aux includes (après la ligne 11 `#include "src/shared/db/DbHelpers.h"`) :

```cpp
#include "src/shared/Character/CustomizationJson.h"
```

- [ ] **Step 2 : Ajouter `appearance_json` au SELECT**

Remplacer la fin de la liste de colonnes (ligne 75, `"c.race_str, c.class_str "`) par :

```cpp
			"c.race_str, c.class_str, c.appearance_json "
```

- [ ] **Step 3 : Désérialiser dans l'entrée**

Dans la boucle `while ((row = mysql_fetch_row(res)) ...)`, juste après `if (row[15]) e.class_str = row[15];` (ligne 114), ajouter :

```cpp
				// Slice 1 — appearance customization (NULL/'{}' → défauts).
				if (row[16]) e.customization = engine::character::CustomizationFromJson(row[16]);
```

- [ ] **Step 4 : Vérifier la compilation**

Run: `cmake --build --preset linux-x64`
Expected: PASS.

- [ ] **Step 5 : Commit**

```bash
git add src/masterd/handlers/character/CharacterListHandler.cpp
git commit -m "customization: return appearance in character list"
```

---

## Task 10 : Câblage du catalogue au boot du master

**Files:**
- Modify: `src/masterd/main_linux.cpp`

- [ ] **Step 1 : Inclure le catalogue**

Dans `src/masterd/main_linux.cpp`, ajouter aux includes (à côté de l'include `CharacterCreateHandler.h`, ligne 32) :

```cpp
#include "src/shared/Character/CustomizationCatalog.h"
```

- [ ] **Step 2 : Construire et charger le catalogue, l'injecter dans le handler**

Dans `main_linux.cpp`, juste avant la construction `engine::server::CharacterCreateHandler characterCreateHandler;` (ligne 337), insérer :

```cpp
	engine::character::CustomizationCatalog customizationCatalog;
	{
		const std::string racesDir = config.GetString("character_creation.races_dir", "game/data/races");
		if (!customizationCatalog.LoadFromDir(racesDir))
			LOG_WARN(Auth, "[main] customization catalog failed to load from {} (creation will reject all customization)", racesDir);
	}
```

Puis, parmi les setters de `characterCreateHandler` (après la ligne 342 `characterCreateHandler.SetConfig(&config);`), ajouter :

```cpp
	characterCreateHandler.SetCustomizationCatalog(&customizationCatalog);
```

- [ ] **Step 3 : Vérifier la compilation complète**

Run: `cmake --build --preset linux-x64`
Expected: PASS (master + tous les tests compilent).

- [ ] **Step 4 : Lancer toute la suite de tests customization**

Run: `ctest --preset linux-x64 -R "json_tests|customization_|character_create_payloads_tests|character_list_payloads_tests" --output-on-failure`
Expected: PASS (6 tests : json, customization_json, customization_catalog, customization_validator, character_create_payloads, character_list_payloads).

- [ ] **Step 5 : Smoke test manuel (optionnel, nécessite DB + clients)**

Lancer master + shard, créer un personnage via le client avec des choix de customization, vérifier en DB que `characters.appearance_json` contient le JSON attendu (pas `{}`), puis revenir à l'écran de sélection et confirmer que la customization est renvoyée dans la liste. (Aucune migration à appliquer ; rebuild + redéploiement serveur suffisent.)

- [ ] **Step 6 : Commit**

```bash
git add src/masterd/main_linux.cpp
git commit -m "customization: load catalog at master boot and inject into create handler"
```

---

## Rappel déploiement

> **Déploiement** : ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS (master ; shard si lecture)**.
> Wire-breaking (bloc customization versionné dans les payloads create + list) + changement de logique des handlers. **Pas de migration DB** (réutilisation de `characters.appearance_json`). Déploiement **client + serveur en lock-step** (parser strict).

---

## Self-Review

**Couverture spec (slice 1) :**
- Catalogue data-driven par race → Tasks 5 (données) + 6 (loader). ✅
- Loader + validateur partagés dans `src/shared/Character/`, compilables Linux → Tasks 6, 7. ✅
- Étendre `engine::network::CharacterCustomization` (pas de doublon) + `bodyFrame` → Task 2. ✅
- Bloc binaire versionné sur le wire (create + list) → Task 3. ✅
- Stockage JSON dans `appearance_json` existant, pas de migration → Tasks 4, 8, 9. ✅
- Validation autoritaire dans le create handler (rejet si invalide) → Task 8. ✅
- Renvoi de la customization dans la liste → Task 9. ✅
- Catalogue chargé au boot → Task 10. ✅
- Tests unitaires (validateur, loader, JSON, wire) → Tasks 1, 3, 4, 6, 7. ✅
- 8 races conservées (humains…demons) → Task 5. ✅

**Cohérence des types/signatures :** `engine::network::CharacterCustomization` (champs `bodyFrame/bodyType/facialHair/racialFeatures`) utilisée de façon cohérente entre wire (Task 3), JSON (Task 4), validateur (Task 7) et handlers (Tasks 8-9). `engine::character::{CustomizationCatalog, RaceCustomization, FrameModules, ValidationResult, ValidateCustomization, CustomizationToJson, CustomizationFromJson}` et `engine::json::{Value, Parse}` cohérents entre header et usages.

**Placeholders :** aucun TODO/TBD ; chaque étape de code contient du code complet ; les fichiers data sont fournis intégralement (8 races).

**Limite CI connue :** `build-linux.yml` est compile-only (pas de `ctest` runtime — angle mort connu). Les tests doivent au minimum **compiler** sous GCC ; leur exécution se fait localement / via CI Windows. Les commandes `ctest` ci-dessus sont à lancer en local.

**Point à confirmer à l'exécution :** la clé de config `character_creation.races_dir` (défaut `game/data/races`) et le répertoire de travail du master au runtime (le catalogue doit pouvoir lire `races.json` + `customization/*.json` depuis ce chemin).
