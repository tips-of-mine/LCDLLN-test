# Sélection de langue 1er lancement + suggestion géo-IP — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Au premier lancement, l'écran de choix de langue ne propose plus toutes les locales mais l'union filtrée { langue système, langue du pays déduit de l'IP publique, anglais } ∩ catalogues disponibles.

**Architecture:** Une fonction pure `ComputeSuggestedLocales` (cœur testable) ; une table pays→langue chargée d'un fichier data ; un fournisseur géo (WinHTTP → ip-api.com) injectable derrière une interface ; un `LanguageSuggestionService` qui orchestre le tout sur un thread worker non bloquant ; intégration dans `AuthUiPresenter` qui route **tous** les sites « 1er lancement » vers la liste suggérée. Client uniquement, aucun changement protocole.

**Tech Stack:** C++17, WinHTTP (Windows), parseur JSON plat maison (déjà présent), tests unitaires « main() + Assert » façon `LocalizationServiceTests.cpp`, CMake.

**Spec de référence :** [docs/superpowers/specs/2026-06-18-langue-1er-lancement-geoip-design.md](../specs/2026-06-18-langue-1er-lancement-geoip-design.md)

**Rappels projet (CLAUDE.md / mémoire) :**
- Commentaires en **français**, clarté > brièveté.
- **PascalCase** pour tout nouveau type/fichier/dossier ; `m_camelCase` pour les membres.
- La CI Windows ne lance **pas** ctest ; la CI Linux lance ctest avec exclusions `-E`. Les nouveaux tests doivent compiler partout et ne dépendre **d'aucun** réseau.
- Pas de toolchain locale : compilation via CI/VS, jamais en local. Les étapes « run test » décrivent la commande **attendue** ; en pratique la vérification passe par la CI.
- **Déploiement : ✅ client uniquement, pas de redéploiement serveur.**

---

## Structure des fichiers

| Fichier | Création / Modif | Responsabilité |
|---------|------------------|----------------|
| `game/data/localization/country_language.json` | Créer | Table plate `{"FR":"fr",...}` pays ISO → tag langue. Éditable sans recompiler. |
| `src/client/localization/CountryLanguageMap.h/.cpp` | Créer | Charge la table, expose `LanguageForCountry(cc)` (défaut `en`). |
| `src/client/localization/CountryLanguageMapTests.cpp` | Créer | Tests de mapping. |
| `src/client/localization/LanguageSuggestionService.h/.cpp` | Créer | Fonction pure `ComputeSuggestedLocales` + orchestration géoloc async + `GetSuggestedLocales`/`PollGeoUpdate`. |
| `src/client/localization/LanguageSuggestionServiceTests.cpp` | Créer | Tests union (les 2 exemples + fallbacks) avec faux fournisseur géo. |
| `src/client/localization/GeoCountryProvider.h` | Créer | Interface `IGeoCountryProvider` (injection). |
| `src/client/localization/IpApiGeoProvider.h/.cpp` | Créer | Implémentation WinHTTP (GET `http://ip-api.com/json/`), stub no-op non-Windows. |
| `game/data/localization/{es,de,it,pl,pt}/<tag>.json` | Créer | Catalogues complets, miroir des clés de `en.json`. |
| `src/client/localization/CatalogParityTests.cpp` | Créer | Garde-fou : chaque catalogue a exactement les clés de `en.json`. |
| `src/client/localization/LocalizationService.h` | Modifier | Exposer `ParseFlatJsonCatalog` en `public static` (réutilisé par les tests de parité). |
| `game/data/localization/en/en.json`, `fr/fr.json` | Modifier | Ajouter les clés `language.name.*`, `language.native_line.*`, `language.first_run.welcome.*` pour es/de/it/pl/pt. |
| `src/client/auth/AuthUi.h` | Modifier | Membre `m_languageSuggestion`, `m_firstRunLocales`, accesseur privé `FirstRunLocales()`. |
| `src/client/auth/AuthUiPresenterCore.cpp` | Modifier | Démarrer la détection à l'Init 1er lancement ; router les sites 1er lancement vers `FirstRunLocales()`. |
| `src/client/auth/screens/AuthScreenLanguageSelect.cpp` | Modifier | `BuildModel_LanguageSelect` / `Update_LanguageSelect` lisent `FirstRunLocales()` + poll géoloc. |
| `CMakeLists.txt` | Modifier | Ajouter les .cpp à `engine_core`, lier `winhttp` (Windows), enregistrer 3 tests. |

---

## Task 1 : Table pays → langue (`CountryLanguageMap`)

**Files:**
- Create: `game/data/localization/country_language.json`
- Create: `src/client/localization/CountryLanguageMap.h`
- Create: `src/client/localization/CountryLanguageMap.cpp`
- Test: `src/client/localization/CountryLanguageMapTests.cpp`
- Modify: `CMakeLists.txt` (source + test)

- [ ] **Step 1 : Créer le fichier data**

Create `game/data/localization/country_language.json` :

```json
{
  "FR": "fr", "BE": "fr", "CH": "fr", "LU": "fr", "MC": "fr", "CA": "fr", "CI": "fr", "SN": "fr", "CM": "fr",
  "ES": "es", "MX": "es", "AR": "es", "CO": "es", "CL": "es", "PE": "es", "VE": "es",
  "DE": "de", "AT": "de", "LI": "de",
  "IT": "it", "SM": "it", "VA": "it",
  "PL": "pl",
  "PT": "pt", "BR": "pt", "AO": "pt", "MZ": "pt",
  "GB": "en", "US": "en", "IE": "en", "AU": "en", "NZ": "en", "IN": "en"
}
```

- [ ] **Step 2 : Écrire le test (échec attendu)**

Create `src/client/localization/CountryLanguageMapTests.cpp` :

```cpp
#include "src/client/localization/CountryLanguageMap.h"

#include <iostream>

namespace
{
	int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	// Petite table JSON plate de test (sous-ensemble représentatif).
	constexpr const char* kTestJson =
		"{ \"FR\":\"fr\", \"ES\":\"es\", \"DE\":\"de\", \"BR\":\"pt\", \"GB\":\"en\" }";

	void TestMappingKnownCountries()
	{
		engine::client::CountryLanguageMap map;
		Assert(map.LoadFromJson(kTestJson), "load flat json");
		Assert(map.LanguageForCountry("FR") == "fr", "FR -> fr");
		Assert(map.LanguageForCountry("ES") == "es", "ES -> es");
		Assert(map.LanguageForCountry("DE") == "de", "DE -> de");
		Assert(map.LanguageForCountry("BR") == "pt", "BR -> pt");
	}

	void TestUnknownCountryDefaultsToEnglish()
	{
		engine::client::CountryLanguageMap map;
		Assert(map.LoadFromJson(kTestJson), "load flat json");
		Assert(map.LanguageForCountry("JP") == "en", "unknown JP -> en default");
		Assert(map.LanguageForCountry("") == "en", "empty -> en default");
	}

	void TestCaseInsensitiveCountryCode()
	{
		engine::client::CountryLanguageMap map;
		Assert(map.LoadFromJson(kTestJson), "load flat json");
		Assert(map.LanguageForCountry("fr") == "fr", "lowercase fr -> fr");
	}
}

int main()
{
	TestMappingKnownCountries();
	TestUnknownCountryDefaultsToEnglish();
	TestCaseInsensitiveCountryCode();
	if (s_failCount != 0)
		return 1;
	std::cout << "CountryLanguageMap tests: all passed." << std::endl;
	return 0;
}
```

- [ ] **Step 3 : Vérifier l'échec de compilation**

Run: `ctest --test-dir build/vs2022-x64 -C Release -R country_language_map_tests`
Expected: échec de **build** (`CountryLanguageMap.h` introuvable). C'est l'échec attendu de cette étape TDD.

- [ ] **Step 4 : Écrire le header**

Create `src/client/localization/CountryLanguageMap.h` :

```cpp
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::client
{
	/// Table pays (ISO-3166 alpha-2) -> tag de langue court (fr/en/es/de/it/pl/pt).
	/// Chargée depuis game/data/localization/country_language.json. Tout pays absent
	/// retombe sur "en" (filet de sécurité universel).
	class CountryLanguageMap final
	{
	public:
		/// Charge la table depuis un texte JSON plat {"FR":"fr",...}. Renvoie false si parse invalide.
		bool LoadFromJson(std::string_view json);

		/// Langue associée au code pays (insensible à la casse). "en" par défaut si inconnu/vide.
		std::string LanguageForCountry(std::string_view countryCode) const;

		/// true si au moins une entrée a été chargée.
		bool Empty() const { return m_map.empty(); }

	private:
		std::unordered_map<std::string, std::string> m_map;
	};
}
```

- [ ] **Step 5 : Écrire l'implémentation**

Create `src/client/localization/CountryLanguageMap.cpp` :

```cpp
#include "src/client/localization/CountryLanguageMap.h"

#include "src/shared/core/Log.h"

#include <cctype>

namespace engine::client
{
	namespace
	{
		/// Parse un objet JSON plat {"clé":"valeur",...} sans dépendance externe.
		/// Réplique volontairement la tolérance du parseur de LocalizationService
		/// (mêmes fichiers de localisation, même format simple). Renvoie false
		/// si la structure n'est pas un objet plat de chaînes.
		bool ParseFlatStringObject(std::string_view text, std::unordered_map<std::string, std::string>& out)
		{
			out.clear();
			size_t pos = 0;
			auto skipWs = [&]() {
				while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0)
					++pos;
			};
			auto parseString = [&](std::string& s) -> bool {
				if (pos >= text.size() || text[pos] != '"')
					return false;
				++pos;
				s.clear();
				while (pos < text.size())
				{
					const char c = text[pos++];
					if (c == '"')
						return true;
					s.push_back(c);
				}
				return false;
			};

			skipWs();
			if (pos >= text.size() || text[pos] != '{')
				return false;
			++pos;
			for (;;)
			{
				skipWs();
				if (pos >= text.size())
					return false;
				if (text[pos] == '}')
					return true;
				std::string key;
				std::string value;
				if (!parseString(key))
					return false;
				skipWs();
				if (pos >= text.size() || text[pos] != ':')
					return false;
				++pos;
				skipWs();
				if (!parseString(value))
					return false;
				// Clé pays normalisée en MAJUSCULES pour un lookup insensible à la casse.
				for (char& ch : key)
					ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
				out[key] = value;
				skipWs();
				if (pos < text.size() && text[pos] == ',')
				{
					++pos;
					continue;
				}
				skipWs();
				if (pos < text.size() && text[pos] == '}')
					return true;
				if (pos >= text.size())
					return false;
			}
		}
	}

	bool CountryLanguageMap::LoadFromJson(std::string_view json)
	{
		if (!ParseFlatStringObject(json, m_map))
		{
			LOG_WARN(Core, "[CountryLanguageMap] JSON invalide, table vide");
			m_map.clear();
			return false;
		}
		LOG_INFO(Core, "[CountryLanguageMap] {} pays chargés", m_map.size());
		return true;
	}

	std::string CountryLanguageMap::LanguageForCountry(std::string_view countryCode) const
	{
		std::string key(countryCode);
		for (char& ch : key)
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		const auto it = m_map.find(key);
		if (it != m_map.end())
			return it->second;
		return "en";
	}
}
```

- [ ] **Step 6 : Enregistrer source + test dans CMake**

Modify `CMakeLists.txt` — après la ligne `src/client/localization/LocalizationService.cpp` (ligne 249), ajouter :

```cmake
  src/client/localization/CountryLanguageMap.cpp
```

Et après le bloc `localization_service_tests` (vers ligne 1349), ajouter :

```cmake
# Langue 1er lancement — table pays->langue
add_executable(country_language_map_tests src/client/localization/CountryLanguageMapTests.cpp)
target_link_libraries(country_language_map_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(country_language_map_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME country_language_map_tests COMMAND country_language_map_tests)
```

- [ ] **Step 7 : Vérifier que le test passe**

Run: `ctest --test-dir build/vs2022-x64 -C Release -R country_language_map_tests --output-on-failure`
Expected: PASS — `CountryLanguageMap tests: all passed.`

- [ ] **Step 8 : Commit**

```bash
git add game/data/localization/country_language.json \
        src/client/localization/CountryLanguageMap.h \
        src/client/localization/CountryLanguageMap.cpp \
        src/client/localization/CountryLanguageMapTests.cpp \
        CMakeLists.txt
git commit -m "feat(i18n): table pays->langue (CountryLanguageMap) + données + tests"
```

---

## Task 2 : Logique d'union pure (`ComputeSuggestedLocales`)

**Files:**
- Create: `src/client/localization/LanguageSuggestionService.h`
- Create: `src/client/localization/LanguageSuggestionService.cpp`
- Test: `src/client/localization/LanguageSuggestionServiceTests.cpp`
- Modify: `CMakeLists.txt` (source + test)

> Note : ce Task crée le service avec **uniquement** la fonction pure. Le Task 4 y ajoutera la géoloc async. On sépare pour pouvoir tester le cœur sans thread ni réseau.

- [ ] **Step 1 : Écrire le test (échec attendu)**

Create `src/client/localization/LanguageSuggestionServiceTests.cpp` :

```cpp
#include "src/client/localization/LanguageSuggestionService.h"

#include <iostream>
#include <vector>

namespace
{
	int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	using Vec = std::vector<std::string>;

	// Univers complet des catalogues (cas nominal : les 7 langues existent).
	const Vec kAll7 = { "de", "en", "es", "fr", "it", "pl", "pt" };

	void TestExampleFranceFrench()
	{
		// Poste FR, système FR, IP=fr -> {fr, en}, système d'abord.
		const Vec got = engine::client::ComputeSuggestedLocales("fr", "fr", kAll7);
		Assert(got == Vec({ "fr", "en" }), "FR+FR -> {fr, en}");
	}

	void TestExampleSpainGerman()
	{
		// Poste ES, système DE, IP=es -> {de, es, en} : système, puis IP, puis en.
		const Vec got = engine::client::ComputeSuggestedLocales("de", "es", kAll7);
		Assert(got == Vec({ "de", "es", "en" }), "DE+ES -> {de, es, en}");
	}

	void TestGeoFailureFallsBackToSystemPlusEnglish()
	{
		// IP inconnue ("") -> {système, en}.
		const Vec got = engine::client::ComputeSuggestedLocales("de", "", kAll7);
		Assert(got == Vec({ "de", "en" }), "DE+(none) -> {de, en}");
	}

	void TestSystemEqualsIpDeduplicates()
	{
		const Vec got = engine::client::ComputeSuggestedLocales("es", "es", kAll7);
		Assert(got == Vec({ "es", "en" }), "ES+ES dedup -> {es, en}");
	}

	void TestLocaleWithoutCatalogExcluded()
	{
		// Seuls fr/en ont un catalogue (état actuel du repo). IP=es mais pas de
		// catalogue es -> exclu, on retombe sur {fr, en}.
		const Vec only2 = { "en", "fr" };
		const Vec got = engine::client::ComputeSuggestedLocales("fr", "es", only2);
		Assert(got == Vec({ "fr", "en" }), "es sans catalogue exclu -> {fr, en}");
	}

	void TestEnglishAlwaysPresentEvenIfSystemUnavailable()
	{
		// Système = pl mais catalogues = {en} -> {en} seulement (en toujours là).
		const Vec onlyEn = { "en" };
		const Vec got = engine::client::ComputeSuggestedLocales("pl", "fr", onlyEn);
		Assert(got == Vec({ "en" }), "tout indispo sauf en -> {en}");
	}
}

int main()
{
	TestExampleFranceFrench();
	TestExampleSpainGerman();
	TestGeoFailureFallsBackToSystemPlusEnglish();
	TestSystemEqualsIpDeduplicates();
	TestLocaleWithoutCatalogExcluded();
	TestEnglishAlwaysPresentEvenIfSystemUnavailable();
	if (s_failCount != 0)
		return 1;
	std::cout << "LanguageSuggestionService tests: all passed." << std::endl;
	return 0;
}
```

- [ ] **Step 2 : Vérifier l'échec de compilation**

Run: `ctest --test-dir build/vs2022-x64 -C Release -R language_suggestion_service_tests`
Expected: échec de build (`LanguageSuggestionService.h` introuvable).

- [ ] **Step 3 : Écrire le header (fonction pure seule)**

Create `src/client/localization/LanguageSuggestionService.h` :

```cpp
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace engine::client
{
	/// Calcule la liste ordonnée et filtrée des langues à proposer au 1er lancement.
	///
	/// union = dédupliquer([ systemLocale, ipLocale, "en" ]) ∩ availableCatalogs
	///
	/// Ordre : langue système d'abord (carte sélectionnée par défaut), puis langue
	/// IP si différente, puis anglais s'il n'est pas déjà présent. "en" est toujours
	/// ajouté avant le filtrage ; le filtre ∩ availableCatalogs garantit qu'on ne
	/// propose jamais une langue sans catalogue. Résultat vide impossible tant que
	/// "en" est dans availableCatalogs.
	///
	/// \param systemLocale tag langue système normalisé (ex "fr"), peut être vide.
	/// \param ipLocale     tag langue déduit du pays IP, "" si géoloc indisponible.
	/// \param availableCatalogs locales ayant réellement un catalogue (ordre indifférent).
	std::vector<std::string> ComputeSuggestedLocales(
		std::string_view systemLocale,
		std::string_view ipLocale,
		const std::vector<std::string>& availableCatalogs);
}
```

- [ ] **Step 4 : Écrire l'implémentation**

Create `src/client/localization/LanguageSuggestionService.cpp` :

```cpp
#include "src/client/localization/LanguageSuggestionService.h"

#include <algorithm>

namespace engine::client
{
	namespace
	{
		bool Contains(const std::vector<std::string>& v, std::string_view tag)
		{
			return std::find(v.begin(), v.end(), tag) != v.end();
		}
	}

	std::vector<std::string> ComputeSuggestedLocales(
		std::string_view systemLocale,
		std::string_view ipLocale,
		const std::vector<std::string>& availableCatalogs)
	{
		// Ordre voulu : système, IP, anglais.
		const std::string ordered[] = {
			std::string(systemLocale),
			std::string(ipLocale),
			std::string("en"),
		};

		std::vector<std::string> result;
		for (const std::string& tag : ordered)
		{
			if (tag.empty())
				continue;
			if (!Contains(availableCatalogs, tag))   // pas de catalogue -> jamais proposé
				continue;
			if (Contains(result, tag))               // déduplication
				continue;
			result.push_back(tag);
		}
		return result;
	}
}
```

- [ ] **Step 5 : Enregistrer source + test dans CMake**

Modify `CMakeLists.txt` — après `src/client/localization/CountryLanguageMap.cpp` (ajouté au Task 1), ajouter :

```cmake
  src/client/localization/LanguageSuggestionService.cpp
```

Et après le bloc `country_language_map_tests`, ajouter :

```cmake
# Langue 1er lancement — logique d'union suggérée
add_executable(language_suggestion_service_tests src/client/localization/LanguageSuggestionServiceTests.cpp)
target_link_libraries(language_suggestion_service_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(language_suggestion_service_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME language_suggestion_service_tests COMMAND language_suggestion_service_tests)
```

- [ ] **Step 6 : Vérifier que le test passe**

Run: `ctest --test-dir build/vs2022-x64 -C Release -R language_suggestion_service_tests --output-on-failure`
Expected: PASS — `LanguageSuggestionService tests: all passed.`

- [ ] **Step 7 : Commit**

```bash
git add src/client/localization/LanguageSuggestionService.h \
        src/client/localization/LanguageSuggestionService.cpp \
        src/client/localization/LanguageSuggestionServiceTests.cpp \
        CMakeLists.txt
git commit -m "feat(i18n): logique d'union ComputeSuggestedLocales + tests (2 exemples + fallbacks)"
```

---

## Task 3 : Fournisseur géo (interface + WinHTTP ip-api.com)

**Files:**
- Create: `src/client/localization/GeoCountryProvider.h`
- Create: `src/client/localization/IpApiGeoProvider.h`
- Create: `src/client/localization/IpApiGeoProvider.cpp`
- Modify: `CMakeLists.txt` (source + lien winhttp)

> Pas de test unitaire ici : c'est de l'I/O réseau, non testable en CI sans réseau. Le test de la logique passe par le **faux** fournisseur injecté au Task 4. On vérifie seulement que ça **compile et lie**.

- [ ] **Step 1 : Écrire l'interface**

Create `src/client/localization/GeoCountryProvider.h` :

```cpp
#pragma once

#include <string>

namespace engine::client
{
	/// Abstraction du service de géolocalisation pays. Permet d'injecter un faux
	/// fournisseur dans les tests sans toucher au réseau.
	class IGeoCountryProvider
	{
	public:
		virtual ~IGeoCountryProvider() = default;

		/// Renvoie le code pays ISO-3166 alpha-2 (ex "FR") de l'IP publique appelante,
		/// ou "" en cas d'échec/timeout/hors-ligne. Appel **bloquant** (à exécuter sur
		/// un thread worker par l'appelant).
		virtual std::string FetchCountryCode() = 0;
	};
}
```

- [ ] **Step 2 : Écrire le header de l'implémentation**

Create `src/client/localization/IpApiGeoProvider.h` :

```cpp
#pragma once

#include "src/client/localization/GeoCountryProvider.h"

#include <cstdint>

namespace engine::client
{
	/// Fournisseur géo basé sur ip-api.com : GET http://ip-api.com/json/ (sans IP :
	/// géolocalise l'appelant) puis extrait le champ "countryCode". Implémenté via
	/// WinHTTP sous Windows ; no-op (renvoie "") sur les autres plateformes.
	///
	/// ⚠️ ip-api.com gratuit est HTTP non chiffré ; best-effort, jamais bloquant pour
	/// l'UI (l'appelant l'exécute sur un thread worker).
	class IpApiGeoProvider final : public IGeoCountryProvider
	{
	public:
		/// \param timeoutMs délai max total de la requête (défaut 2000 ms).
		explicit IpApiGeoProvider(uint32_t timeoutMs = 2000u) : m_timeoutMs(timeoutMs) {}

		std::string FetchCountryCode() override;

	private:
		uint32_t m_timeoutMs;
	};
}
```

- [ ] **Step 3 : Écrire l'implémentation WinHTTP**

Create `src/client/localization/IpApiGeoProvider.cpp` :

```cpp
#include "src/client/localization/IpApiGeoProvider.h"

#include "src/shared/core/Log.h"

#include <string>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <winhttp.h>
#endif

namespace engine::client
{
	namespace
	{
		/// Extrait la valeur de "countryCode" d'un corps JSON ip-api, sans parseur
		/// complet : cherche la clé, le ':' puis la chaîne entre guillemets.
		/// Renvoie "" si absente. Robuste aux espaces.
		std::string ExtractCountryCode(const std::string& body)
		{
			const std::string key = "\"countryCode\"";
			size_t pos = body.find(key);
			if (pos == std::string::npos)
				return {};
			pos += key.size();
			while (pos < body.size() && body[pos] != ':')
				++pos;
			if (pos >= body.size())
				return {};
			++pos; // saute ':'
			while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t'))
				++pos;
			if (pos >= body.size() || body[pos] != '"')
				return {};
			++pos;
			std::string out;
			while (pos < body.size() && body[pos] != '"')
				out.push_back(body[pos++]);
			return out;
		}
	}

#if defined(_WIN32)
	std::string IpApiGeoProvider::FetchCountryCode()
	{
		std::string result;
		HINTERNET session = WinHttpOpen(L"LCDLLN-GeoIP/1.0",
			WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session)
		{
			LOG_WARN(Core, "[IpApiGeoProvider] WinHttpOpen échoué -> géoloc ignorée");
			return result;
		}
		WinHttpSetTimeouts(session, static_cast<int>(m_timeoutMs), static_cast<int>(m_timeoutMs),
			static_cast<int>(m_timeoutMs), static_cast<int>(m_timeoutMs));

		HINTERNET connect = WinHttpConnect(session, L"ip-api.com", INTERNET_DEFAULT_HTTP_PORT, 0);
		HINTERNET request = nullptr;
		if (connect)
		{
			request = WinHttpOpenRequest(connect, L"GET", L"/json/",
				nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
		}

		bool ok = false;
		if (request)
		{
			ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
				WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE;
			if (ok)
				ok = WinHttpReceiveResponse(request, nullptr) != FALSE;
		}

		if (ok)
		{
			std::string body;
			DWORD available = 0;
			while (WinHttpQueryDataAvailable(request, &available) && available > 0)
			{
				std::string chunk(available, '\0');
				DWORD read = 0;
				if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0)
					break;
				chunk.resize(read);
				body += chunk;
			}
			result = ExtractCountryCode(body);
			LOG_INFO(Core, "[IpApiGeoProvider] countryCode='{}' (body {} octets)", result, body.size());
		}
		else
		{
			LOG_WARN(Core, "[IpApiGeoProvider] requête échouée -> géoloc ignorée");
		}

		if (request) WinHttpCloseHandle(request);
		if (connect) WinHttpCloseHandle(connect);
		if (session) WinHttpCloseHandle(session);
		return result;
	}
#else
	std::string IpApiGeoProvider::FetchCountryCode()
	{
		// Pas de client UI ni de géoloc hors Windows (cohérent avec l'auth UI).
		return {};
	}
#endif
}
```

- [ ] **Step 4 : CMake — source + lien winhttp**

Modify `CMakeLists.txt` — après `src/client/localization/LanguageSuggestionService.cpp`, ajouter :

```cmake
  src/client/localization/IpApiGeoProvider.cpp
```

Puis localiser la définition de la cible `engine_core` (`add_library(engine_core ...)`) et, après ses `target_link_libraries`, ajouter le lien WinHTTP conditionnel Windows. Chercher d'abord un `target_link_libraries(engine_core ...)` existant ; sinon ajouter :

```cmake
if(WIN32)
  target_link_libraries(engine_core PUBLIC winhttp)
endif()
```

- [ ] **Step 5 : Vérifier la compilation/édition de liens**

Run: `cmake --build build/vs2022-x64 --config Release --target engine_core`
Expected: build OK, aucun symbole WinHTTP non résolu.

- [ ] **Step 6 : Commit**

```bash
git add src/client/localization/GeoCountryProvider.h \
        src/client/localization/IpApiGeoProvider.h \
        src/client/localization/IpApiGeoProvider.cpp \
        CMakeLists.txt
git commit -m "feat(i18n): fournisseur géo ip-api.com (WinHTTP) + interface injectable"
```

---

## Task 4 : Orchestration async dans `LanguageSuggestionService`

**Files:**
- Modify: `src/client/localization/LanguageSuggestionService.h`
- Modify: `src/client/localization/LanguageSuggestionService.cpp`
- Modify: `src/client/localization/LanguageSuggestionServiceTests.cpp` (ajout tests faux fournisseur)

- [ ] **Step 1 : Ajouter les tests d'orchestration (échec attendu)**

Dans `src/client/localization/LanguageSuggestionServiceTests.cpp`, ajouter en haut un faux fournisseur et de nouveaux tests, puis les appeler depuis `main()` :

```cpp
// (ajouter après les includes existants)
#include "src/client/localization/CountryLanguageMap.h"
#include "src/client/localization/GeoCountryProvider.h"
#include <memory>

namespace
{
	// Faux fournisseur : renvoie un code pays figé (ou "" pour simuler un échec),
	// sans aucun accès réseau.
	class FakeGeoProvider final : public engine::client::IGeoCountryProvider
	{
	public:
		explicit FakeGeoProvider(std::string code) : m_code(std::move(code)) {}
		std::string FetchCountryCode() override { return m_code; }
	private:
		std::string m_code;
	};

	engine::client::CountryLanguageMap MakeMap()
	{
		engine::client::CountryLanguageMap m;
		m.LoadFromJson("{ \"FR\":\"fr\", \"ES\":\"es\", \"DE\":\"de\" }");
		return m;
	}

	// Attend (de façon déterministe, borne dure) que le thread géo se termine,
	// puis consomme le résultat via un dernier Poll. sleep_for évite l'attente
	// active flaky (le worker doit être ordonnancé avant qu'on vérifie).
	void PumpUntilDone(engine::client::LanguageSuggestionService& svc)
	{
		for (int i = 0; i < 2000 && !svc.GeoDetectionFinished(); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		svc.PollGeoUpdate();   // intègre le résultat (recompute) si arrivé
	}

	void TestAsyncSpainGermanProducesThree()
	{
		// Catalogues complets, système DE, IP -> ES.
		const Vec all = { "de", "en", "es", "fr" };
		engine::client::LanguageSuggestionService svc;
		svc.BeginDetection("de", all, MakeMap(),
			std::make_unique<FakeGeoProvider>("ES"));
		PumpUntilDone(svc);
		svc.PollGeoUpdate();
		Assert(svc.GetSuggestedLocales() == Vec({ "de", "es", "en" }), "async DE+ES -> {de, es, en}");
	}

	void TestAsyncFailureKeepsSystemAndEnglish()
	{
		const Vec all = { "de", "en", "es", "fr" };
		engine::client::LanguageSuggestionService svc;
		svc.BeginDetection("de", all, MakeMap(),
			std::make_unique<FakeGeoProvider>(""));   // échec géoloc
		PumpUntilDone(svc);
		svc.PollGeoUpdate();
		Assert(svc.GetSuggestedLocales() == Vec({ "de", "en" }), "async échec -> {de, en}");
	}

	void TestImmediateListBeforeGeo()
	{
		// Avant toute réponse géo, la liste vaut déjà {système, en}.
		const Vec all = { "de", "en", "es", "fr" };
		engine::client::LanguageSuggestionService svc;
		svc.BeginDetection("de", all, MakeMap(),
			std::make_unique<FakeGeoProvider>("ES"));
		// Sans pomper : la liste initiale ne contient pas encore la langue IP.
		const Vec initial = svc.GetSuggestedLocales();
		Assert(initial.front() == "de", "liste initiale commence par le système");
		Assert(std::find(initial.begin(), initial.end(), "en") != initial.end(), "en présent d'emblée");
	}
}
```

Et dans `main()` ajouter les appels :

```cpp
	TestAsyncSpainGermanProducesThree();
	TestAsyncFailureKeepsSystemAndEnglish();
	TestImmediateListBeforeGeo();
```

(Ajouter en tête du fichier de test : `#include <algorithm>` pour `std::find`, `#include <thread>` et `#include <chrono>` pour `PumpUntilDone`.)

- [ ] **Step 2 : Vérifier l'échec de compilation**

Run: `ctest --test-dir build/vs2022-x64 -C Release -R language_suggestion_service_tests`
Expected: échec de build (méthodes `BeginDetection`/`PollGeoUpdate`/`GetSuggestedLocales`/`GeoDetectionFinished` absentes).

- [ ] **Step 3 : Étendre le header**

Modify `src/client/localization/LanguageSuggestionService.h` — ajouter les includes et la classe sous la fonction pure existante :

```cpp
// (ajouter en tête, après les includes existants)
#include "src/client/localization/CountryLanguageMap.h"
#include "src/client/localization/GeoCountryProvider.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
```

```cpp
// (ajouter dans namespace engine::client, après la déclaration de ComputeSuggestedLocales)

	/// Orchestre la suggestion de langues au 1er lancement : calcule immédiatement
	/// {système, en}, lance la géoloc sur un thread worker, et intègre la langue du
	/// pays IP quand la réponse arrive. Tout est best-effort : un échec laisse la
	/// liste {système, en}.
	class LanguageSuggestionService final
	{
	public:
		LanguageSuggestionService() = default;
		~LanguageSuggestionService();

		LanguageSuggestionService(const LanguageSuggestionService&) = delete;
		LanguageSuggestionService& operator=(const LanguageSuggestionService&) = delete;

		/// Démarre la détection. Calcule la liste initiale {système, en} et lance le
		/// fournisseur géo sur un thread. Idempotent : un second appel est ignoré.
		/// \param systemLocale tag langue système (ex "fr").
		/// \param availableCatalogs locales ayant un catalogue.
		/// \param countryMap table pays->langue (copiée).
		/// \param provider fournisseur géo (propriété transférée) ; nullptr -> pas de géoloc.
		void BeginDetection(std::string_view systemLocale,
			const std::vector<std::string>& availableCatalogs,
			CountryLanguageMap countryMap,
			std::unique_ptr<IGeoCountryProvider> provider);

		/// À appeler chaque frame (main thread). Si le résultat géo vient d'arriver,
		/// recalcule la liste suggérée et renvoie true (le presenter doit reconstruire
		/// son modèle). Renvoie false sinon.
		bool PollGeoUpdate();

		/// true une fois que le thread géo a terminé (succès ou échec).
		bool GeoDetectionFinished() const { return m_geoFinished.load(); }

		/// Liste suggérée courante (thread-safe en lecture main thread après Poll).
		std::vector<std::string> GetSuggestedLocales() const { return m_suggested; }

	private:
		void RecomputeSuggested();   // main thread uniquement

		std::string m_systemLocale;
		std::string m_ipLocale;                  // rempli après géoloc réussie
		std::vector<std::string> m_available;
		std::vector<std::string> m_suggested;
		CountryLanguageMap m_countryMap;

		std::unique_ptr<IGeoCountryProvider> m_provider;
		std::thread m_geoThread;
		std::atomic<bool> m_geoFinished{ false };
		std::atomic<bool> m_geoConsumed{ false };
		std::mutex m_geoMutex;
		std::string m_geoCountryCode;            // protégé par m_geoMutex
		bool m_started = false;
	};
```

- [ ] **Step 4 : Étendre l'implémentation**

Modify `src/client/localization/LanguageSuggestionService.cpp` — ajouter sous la fonction pure :

```cpp
	LanguageSuggestionService::~LanguageSuggestionService()
	{
		if (m_geoThread.joinable())
			m_geoThread.join();
	}

	void LanguageSuggestionService::BeginDetection(std::string_view systemLocale,
		const std::vector<std::string>& availableCatalogs,
		CountryLanguageMap countryMap,
		std::unique_ptr<IGeoCountryProvider> provider)
	{
		if (m_started)
			return;
		m_started = true;
		m_systemLocale = std::string(systemLocale);
		m_available = availableCatalogs;
		m_countryMap = std::move(countryMap);
		m_provider = std::move(provider);

		// Liste immédiate : {système, en}, sans attendre la géoloc.
		RecomputeSuggested();

		if (!m_provider)
		{
			m_geoFinished.store(true);
			return;
		}

		// Thread worker : appel bloquant du fournisseur, résultat stocké sous mutex.
		IGeoCountryProvider* provPtr = m_provider.get();
		m_geoThread = std::thread([this, provPtr]() {
			const std::string code = provPtr->FetchCountryCode();
			{
				std::lock_guard<std::mutex> lock(m_geoMutex);
				m_geoCountryCode = code;
			}
			m_geoFinished.store(true);
		});
	}

	bool LanguageSuggestionService::PollGeoUpdate()
	{
		if (!m_geoFinished.load())
			return false;
		if (m_geoConsumed.exchange(true))
			return false;   // déjà intégré

		if (m_geoThread.joinable())
			m_geoThread.join();

		std::string code;
		{
			std::lock_guard<std::mutex> lock(m_geoMutex);
			code = m_geoCountryCode;
		}
		if (code.empty())
			return false;   // échec géoloc : on garde {système, en}

		m_ipLocale = m_countryMap.LanguageForCountry(code);
		const std::vector<std::string> before = m_suggested;
		RecomputeSuggested();
		return m_suggested != before;
	}

	void LanguageSuggestionService::RecomputeSuggested()
	{
		m_suggested = ComputeSuggestedLocales(m_systemLocale, m_ipLocale, m_available);
	}
```

- [ ] **Step 5 : Vérifier que les tests passent**

Run: `ctest --test-dir build/vs2022-x64 -C Release -R language_suggestion_service_tests --output-on-failure`
Expected: PASS — `LanguageSuggestionService tests: all passed.`

- [ ] **Step 6 : Commit**

```bash
git add src/client/localization/LanguageSuggestionService.h \
        src/client/localization/LanguageSuggestionService.cpp \
        src/client/localization/LanguageSuggestionServiceTests.cpp
git commit -m "feat(i18n): orchestration géoloc async (BeginDetection/PollGeoUpdate) + tests faux fournisseur"
```

---

## Task 5 : Catalogues de traduction es/de/it/pl/pt + test de parité

**Files:**
- Modify: `src/client/localization/LocalizationService.h` (exposer `ParseFlatJsonCatalog`)
- Modify: `game/data/localization/en/en.json`, `game/data/localization/fr/fr.json` (clés par-langue)
- Create: `game/data/localization/es/es.json`, `de/de.json`, `it/it.json`, `pl/pl.json`, `pt/pt.json`
- Test: `src/client/localization/CatalogParityTests.cpp`
- Modify: `CMakeLists.txt` (test)

> **Note pragmatique sur le volume :** `en.json` contient ~524 clés. Reproduire 5×524 chaînes traduites dans ce plan serait illisible et inutile. La règle opérationnelle est : **chaque nouveau catalogue est le miroir exact des clés de `en.json`, valeurs traduites best-effort**, et le **test de parité** (Step 4) échoue tant qu'une clé manque ou est en trop. Les traductions sont à relire par un natif ultérieurement (hors périmètre, cf. spec §12).

- [ ] **Step 1 : Ajouter les clés par-langue à `en.json` (source) et `fr.json`**

Dans `game/data/localization/en/en.json`, ajouter (près des clés `language.name.*` / `language.native_line.*` existantes) :

```json
  "language.name.es": "Spanish",
  "language.name.de": "German",
  "language.name.it": "Italian",
  "language.name.pl": "Polish",
  "language.name.pt": "Portuguese",
  "language.native_line.es": "Espanol",
  "language.native_line.de": "Deutsch",
  "language.native_line.it": "Italiano",
  "language.native_line.pl": "Polski",
  "language.native_line.pt": "Portugues",
  "language.first_run.welcome.es": "Bienvenido, viajero.",
  "language.first_run.welcome.de": "Willkommen, Reisender.",
  "language.first_run.welcome.it": "Benvenuto, viaggiatore.",
  "language.first_run.welcome.pl": "Witaj, podrozniku.",
  "language.first_run.welcome.pt": "Bem-vindo, viajante.",
```

Ajouter **les mêmes clés** dans `game/data/localization/fr/fr.json` (avec les noms de langue en français : `language.name.es` = `"Espagnol"`, `.de` = `"Allemand"`, `.it` = `"Italien"`, `.pl` = `"Polonais"`, `.pt` = `"Portugais"` ; les `native_line.*` et `welcome.*` sont **identiques** à `en.json` car ce sont déjà des chaînes dans la langue native).

> Note : les `native_line.*` n'utilisent pas de caractères accentués (`Espanol`, `Portugues`) pour rester cohérent avec l'existant (`language.native_line.fr` = `"Francais"` sans cédille) — l'atlas police in-game est limité (cf. mémoire `reference_imgui_font_atlas`).

- [ ] **Step 2 : Générer les 5 catalogues miroir**

Pour chaque langue de `{es, de, it, pl, pt}`, créer `game/data/localization/<tag>/<tag>.json` contenant **exactement les mêmes clés** que `en.json` (les 524 + les 15 ajoutées au Step 1), valeurs traduites dans la langue cible (best-effort). Les valeurs `language.native_line.*` et `language.first_run.welcome.*` restent identiques d'un catalogue à l'autre (chaînes natives). Exemple de tête de fichier pour `es/es.json` :

```json
{
  "app.title": "LCDLLN",
  "common.apply": "Aplicar",
  "common.back": "Atras",
  "common.cancel": "Cancelar",
  "common.continue": "Continuar",
  "common.email": "Correo",
  "common.language": "Idioma",
  "language.name.en": "Ingles",
  "language.name.fr": "Frances",
  "language.name.es": "Espanol",
  "language.first_run.welcome.es": "Bienvenido, viajero."
}
```

(… compléter avec **toutes** les clés de `en.json`. Le test de parité du Step 4 est le juge de complétude.)

- [ ] **Step 3 : Exposer le parseur de catalogue pour les tests**

Modify `src/client/localization/LocalizationService.h` — déplacer `ParseFlatJsonCatalog` de la section `private:` vers `public:` (la signature ne change pas) :

```cpp
	public:
		/// Parse un catalogue JSON plat {"clé":"valeur",...}. Exposé pour les tests
		/// de parité de clés entre catalogues.
		static bool ParseFlatJsonCatalog(std::string_view text, Catalog& outCatalog);
```

> `Catalog` est `std::unordered_map<std::string,std::string>` ; le rendre aussi accessible (déplacer le `using Catalog = ...;` en `public:` également).

- [ ] **Step 4 : Écrire le test de parité (échec attendu tant qu'un catalogue est incomplet)**

Create `src/client/localization/CatalogParityTests.cpp` :

```cpp
#include "src/client/localization/LocalizationService.h"
#include "src/shared/platform/FileSystem.h"

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
	int s_failCount = 0;

	void Assert(bool cond, const std::string& msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	// Racine de contenu passée en argv[1] (la CI fournit le chemin game/data).
	std::set<std::string> LoadKeys(const std::filesystem::path& root, const std::string& tag)
	{
		const std::filesystem::path file = root / "localization" / tag / (tag + ".json");
		const std::string text = engine::platform::FileSystem::ReadAllText(file);
		engine::client::LocalizationService::Catalog cat;
		const bool ok = engine::client::LocalizationService::ParseFlatJsonCatalog(text, cat);
		Assert(ok, "parse catalogue " + tag + " (" + file.string() + ")");
		std::set<std::string> keys;
		for (const auto& [k, v] : cat)
		{
			(void)v;
			keys.insert(k);
		}
		return keys;
	}
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cerr << "usage: catalog_parity_tests <content_root>" << std::endl;
		return 2;
	}
	const std::filesystem::path root = argv[1];
	const std::set<std::string> ref = LoadKeys(root, "en");
	Assert(!ref.empty(), "en.json non vide");

	for (const std::string& tag : { "fr", "es", "de", "it", "pl", "pt" })
	{
		const std::set<std::string> keys = LoadKeys(root, tag);
		// Clés manquantes par rapport à en.
		for (const std::string& k : ref)
			Assert(keys.count(k) == 1, "catalogue " + tag + " : clé manquante '" + k + "'");
		// Clés en trop.
		for (const std::string& k : keys)
			Assert(ref.count(k) == 1, "catalogue " + tag + " : clé en trop '" + k + "'");
	}

	if (s_failCount != 0)
		return 1;
	std::cout << "Catalog parity tests: all passed." << std::endl;
	return 0;
}
```

- [ ] **Step 5 : Enregistrer le test dans CMake (avec argument chemin contenu)**

Modify `CMakeLists.txt` — après le bloc `language_suggestion_service_tests`, ajouter :

```cmake
# Langue 1er lancement — parité des clés entre catalogues de traduction
add_executable(catalog_parity_tests src/client/localization/CatalogParityTests.cpp)
target_link_libraries(catalog_parity_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(catalog_parity_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME catalog_parity_tests COMMAND catalog_parity_tests ${CMAKE_SOURCE_DIR}/game/data)
```

- [ ] **Step 6 : Vérifier que le test passe**

Run: `ctest --test-dir build/vs2022-x64 -C Release -R catalog_parity_tests --output-on-failure`
Expected: PASS — `Catalog parity tests: all passed.` (sinon la sortie liste précisément chaque clé manquante/en trop à corriger dans le catalogue fautif).

- [ ] **Step 7 : Commit**

```bash
git add src/client/localization/LocalizationService.h \
        src/client/localization/CatalogParityTests.cpp \
        game/data/localization/en/en.json game/data/localization/fr/fr.json \
        game/data/localization/es/es.json game/data/localization/de/de.json \
        game/data/localization/it/it.json game/data/localization/pl/pl.json \
        game/data/localization/pt/pt.json \
        CMakeLists.txt
git commit -m "feat(i18n): catalogues es/de/it/pl/pt complets + test de parité des clés"
```

---

## Task 6 : Intégration dans `AuthUiPresenter` (1er lancement filtré)

**Files:**
- Modify: `src/client/auth/AuthUi.h`
- Modify: `src/client/auth/AuthUiPresenterCore.cpp`
- Modify: `src/client/auth/screens/AuthScreenLanguageSelect.cpp`

> **Principe d'intégration :** tous les sites « 1er lancement » qui indexaient `m_localization.GetAvailableLocales()` doivent lire **une seule** source : `m_firstRunLocales`. Sinon le clic sur la carte N sélectionnerait la mauvaise locale (les cartes proviennent de la liste filtrée, pas de la liste complète).

- [ ] **Step 1 : Ajouter membres + accesseur dans `AuthUi.h`**

Modify `src/client/auth/AuthUi.h` — ajouter l'include en tête :

```cpp
#include "src/client/localization/LanguageSuggestionService.h"
```

Près de `LocalizationService m_localization;` (ligne ~1092), ajouter :

```cpp
		LanguageSuggestionService m_languageSuggestion;
		// Liste des locales affichées au 1er lancement (union filtrée système+IP+en).
		// Source unique pour TOUS les sites de l'écran LanguageSelectionFirstRun.
		std::vector<std::string> m_firstRunLocales;
```

Près des autres déclarations de méthodes privées de l'écran langue (vers ligne ~858), ajouter :

```cpp
		/// Locales à afficher au 1er lancement. Si la liste suggérée est vide
		/// (cas dégénéré), retombe sur les locales disponibles complètes.
		const std::vector<std::string>& FirstRunLocales() const;
```

- [ ] **Step 2 : Implémenter l'accesseur + démarrer la détection à l'Init**

Modify `src/client/auth/screens/AuthScreenLanguageSelect.cpp` — ajouter les includes en tête :

```cpp
#include "src/client/localization/CountryLanguageMap.h"
#include "src/client/localization/IpApiGeoProvider.h"
#include "src/shared/platform/FileSystem.h"
```

Ajouter l'accesseur dans le bloc `#if defined(_WIN32)` (et son stub dans le `#else`) :

```cpp
	const std::vector<std::string>& AuthUiPresenter::FirstRunLocales() const
	{
		if (!m_firstRunLocales.empty())
			return m_firstRunLocales;
		return m_localization.GetAvailableLocales();   // filet de sécurité
	}
```

Stub `#else` :

```cpp
	const std::vector<std::string>& AuthUiPresenter::FirstRunLocales() const
	{
		return m_localization.GetAvailableLocales();
	}
```

- [ ] **Step 3 : Lancer la détection dans la branche 1er lancement de `Init`**

Modify `src/client/auth/AuthUiPresenterCore.cpp` — dans `Init`, branche `if (requestedLocale.empty())` (lignes ~1265-1273), **après** `SetPhase(Phase::LanguageSelectionFirstRun);`, insérer :

```cpp
#if defined(_WIN32)
			// Charge la table pays->langue et démarre la suggestion (système + géoloc IP).
			engine::client::CountryLanguageMap countryMap;
			const std::string mapJson = engine::platform::FileSystem::ReadAllText(
				engine::platform::FileSystem::ResolveContentPath(cfg, "localization/country_language.json"));
			if (!mapJson.empty())
				countryMap.LoadFromJson(mapJson);
			m_languageSuggestion.BeginDetection(
				m_selectedLocale,
				m_localization.GetAvailableLocales(),
				std::move(countryMap),
				std::make_unique<engine::client::IpApiGeoProvider>());
			m_firstRunLocales = m_languageSuggestion.GetSuggestedLocales();
			// Recale l'index de sélection sur la liste filtrée (système en tête).
			{
				auto it = std::find(m_firstRunLocales.begin(), m_firstRunLocales.end(), m_selectedLocale);
				m_languageSelectionIndex = (it != m_firstRunLocales.end())
					? static_cast<uint32_t>(std::distance(m_firstRunLocales.begin(), it)) : 0u;
			}
#endif
```

Ajouter en tête de `AuthUiPresenterCore.cpp` les includes nécessaires (s'ils manquent) :

```cpp
#include "src/client/localization/CountryLanguageMap.h"
#include "src/client/localization/IpApiGeoProvider.h"
#include <memory>
```

- [ ] **Step 4 : Poller la géoloc dans `Update_LanguageSelect`**

Modify `src/client/auth/screens/AuthScreenLanguageSelect.cpp` — dans `Update_LanguageSelect` (bloc `#if defined(_WIN32)`), **au début** (après le `return` de garde `if (usingNativeAuth || m_phase != Phase::LanguageSelectionFirstRun) return;`), insérer :

```cpp
		// Intègre la langue déduite de l'IP dès qu'elle arrive (non bloquant).
		if (m_languageSuggestion.PollGeoUpdate())
		{
			const std::string previouslySelected = m_selectedLocale;
			m_firstRunLocales = m_languageSuggestion.GetSuggestedLocales();
			// Conserve la sélection courante si elle existe encore, sinon index 0.
			auto it = std::find(m_firstRunLocales.begin(), m_firstRunLocales.end(), previouslySelected);
			m_languageSelectionIndex = (it != m_firstRunLocales.end())
				? static_cast<uint32_t>(std::distance(m_firstRunLocales.begin(), it)) : 0u;
			if (!m_firstRunLocales.empty())
				m_selectedLocale = m_firstRunLocales[m_languageSelectionIndex];
			LOG_INFO(Core, "[AuthUiPresenter] Liste langues 1er lancement mise à jour ({} entrées)", m_firstRunLocales.size());
		}
```

Remplacer ensuite, dans cette même fonction, l'usage de `m_localization.GetAvailableLocales()` par `FirstRunLocales()` (la navigation ←/→ doit cycler sur la liste filtrée).

- [ ] **Step 5 : Router `BuildModel_LanguageSelect` vers la liste filtrée**

Modify `src/client/auth/screens/AuthScreenLanguageSelect.cpp` — dans `BuildModel_LanguageSelect`, remplacer les deux usages de `m_localization.GetAvailableLocales()` (la variable locale `localesFr`, lignes ~133 et ~149) par `FirstRunLocales()` :

```cpp
		const auto& localesFr = FirstRunLocales();
```

(le reste de la fonction est inchangé : il itère `localesFr`).

- [ ] **Step 6 : Router les autres sites 1er lancement du presenter**

Modify `src/client/auth/AuthUiPresenterCore.cpp` — remplacer chaque `m_localization.GetAvailableLocales()` situé **dans un contexte `Phase::LanguageSelectionFirstRun`** par `FirstRunLocales()`. Les ancres connues (issues du grep) à traiter : lignes ~3441, ~3457, ~3702. **Ne pas** toucher : l'accesseur public ligne 753, l'init ligne 1268 (déjà géré au Step 3), ni les usages liés à d'autres phases (shard pick, options).

Aussi `ImGuiApplyFirstRunLanguageContinue` et `ImGuiSelectFirstRunLanguageCard` dans `AuthScreenLanguageSelect.cpp` : remplacer leurs `m_localization.GetAvailableLocales()` (variables `locales`) par `FirstRunLocales()`, pour que l'index de carte indexe la liste filtrée.

- [ ] **Step 7 : Vérification anti-régression (aucun site 1er lancement oublié)**

Run:
```bash
grep -n "GetAvailableLocales" src/client/auth/screens/AuthScreenLanguageSelect.cpp \
                              src/client/auth/AuthUiPresenterCore.cpp
```
Expected: dans `AuthScreenLanguageSelect.cpp`, **plus aucun** `m_localization.GetAvailableLocales()` (tous remplacés par `FirstRunLocales()`). Dans `AuthUiPresenterCore.cpp`, ne subsistent que les usages **hors** `Phase::LanguageSelectionFirstRun` (accesseur public l.753, init l.1268, shard pick). Inspecter visuellement chaque occurrence restante pour confirmer qu'elle n'est pas dans un bloc 1er lancement.

- [ ] **Step 8 : Build complet**

Run: `cmake --build build/vs2022-x64 --config Release`
Expected: build OK (client + tests).

- [ ] **Step 9 : Vérifier la non-régression des tests existants**

Run: `ctest --test-dir build/vs2022-x64 -C Release -R "localization_service_tests|country_language_map_tests|language_suggestion_service_tests|catalog_parity_tests" --output-on-failure`
Expected: les 4 cibles PASS.

- [ ] **Step 10 : Commit**

```bash
git add src/client/auth/AuthUi.h \
        src/client/auth/AuthUiPresenterCore.cpp \
        src/client/auth/screens/AuthScreenLanguageSelect.cpp
git commit -m "feat(i18n): écran 1er lancement filtré par suggestion langue (système + géo-IP)"
```

---

## Validation finale (manuelle, en jeu — hors CI)

Le rendu et la géoloc réelle ne sont pas testables en CI. À valider en jeu sur poste Windows :

- [ ] Supprimer `user_settings.json` (ou sa clé `client.locale`) pour forcer le 1er lancement.
- [ ] Lancer le client en ligne en France, système FR → l'écran propose **Français + English** (Français présélectionné, message de bienvenue en français).
- [ ] Forcer un système non-FR (ou tester via VPN) → vérifier qu'une 3ᵉ langue régionale apparaît quand la géoloc répond, sans figer l'écran au démarrage.
- [ ] Couper le réseau juste avant l'écran → l'écran reste sur {système, en} sans blocage ni crash.
- [ ] Valider une langue → relancer → l'écran est **sauté** (locale persistée) et le jeu démarre dans la langue choisie.
- [ ] Vérifier Options > Langue : la liste y reste **complète** (7 langues), non filtrée.

---

## Déploiement

> **Déploiement : ✅ client uniquement, pas de redéploiement serveur.** Aucun opcode, handler, migration ni clé de config serveur. Nouveaux fichiers data (`country_language.json`, catalogues) lus côté client.
