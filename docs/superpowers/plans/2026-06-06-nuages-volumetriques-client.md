# Nuages volumétriques (client) — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ajouter des nuages volumétriques ray-marchés dans le ciel du client, pilotés par l'état météo serveur déjà diffusé, intégrés au cycle jour/nuit, avec ombres au sol et couplage ambiant.

**Architecture:** Une passe graphique plein écran (`CloudPass`, calquée sur `VolumetricFogPass`) ray-marche une dalle de nuages dans un fragment shader (bruit FBM procédural in-shader), composite par-dessus la scène brouillardée et avant le bloom. L'apparence (couverture/densité/teinte) est dérivée côté client de l'unique `engine::render::WeatherSystem` existant via un mapper pur `CloudWeatherMapper`. Aucun changement serveur/wire.

**Tech Stack:** C++17, Vulkan, GLSL (compilé en SPIR-V par `tools/compile_game_shaders.ps1`), tests via `assert`/`std::puts` + `lcdlln_add_simple_test` (CMake).

**Spec source :** `docs/superpowers/specs/2026-06-06-nuages-volumetriques-client-design.md`

---

## Notes de cadrage (lire avant de commencer)

- **Feature 100 % client.** Aucun fichier serveur, opcode, payload ou migration. **Pas de redéploiement serveur.**
- **Anti-doublon (exigence utilisateur)** : il n'existe qu'**une** classe météo client, `engine::render::WeatherSystem` (`src/client/render/WeatherSystem.h`). On la **réutilise**. On ne crée **aucun** nouveau détenteur d'état météo. Le chemin `src/client/world/weather/` n'existe pas — ne pas le créer.
- **Gap pré-existant corrigé ici** : le broadcast serveur (opcode 156) n'est aujourd'hui routé que vers `m_weatherUi` ; `m_weatherSystem.SetWeather()` n'est jamais appelé. La Task 3 branche le signal autoritaire sur le `WeatherSystem` visuel (donc sur les nuages ET les particules existantes).
- **Pattern de référence GPU** : `src/client/render/VolumetricFogPass.{h,cpp}` — `CloudPass` en est une copie structurelle (render pass 1 attachment, samplers, pipeline fullscreen triangle, framebuffer temporaire dans `Record`). **Lire ce fichier d'abord.**
- **Convention winding (CLAUDE.md)** : la passe est fullscreen, `cullMode = VK_CULL_MODE_NONE`. **Ne toucher à aucun autre `frontFace`.**
- **Bruit** : FBM hash procédural dans le shader (comme `sky.frag`). Pas de textures 3D en v1 (optimisation future).

## Plan de fichiers

| Fichier | Création/Modif | Responsabilité |
|---------|----------------|----------------|
| `src/client/render/clouds/CloudParams.h` | Créer | Struct CPU `CloudParams` (apparence) + `Lerp`. Pur, sans Vulkan. |
| `src/client/render/clouds/CloudWeatherMapper.h` | Créer | `WeatherState` → `CloudParams` (déclaration). Pur. |
| `src/client/render/clouds/CloudWeatherMapper.cpp` | Créer | Table de correspondance. Pur. |
| `src/client/render/clouds/CloudParamsTests.cpp` | Créer | Tests `Lerp`. |
| `src/client/render/clouds/CloudWeatherMapperTests.cpp` | Créer | Tests mapping + mapping serveur→client. |
| `src/client/render/clouds/WeatherKindMap.h` | Créer | `MapServerKindToWeatherState(uint8)` (pur, testable). |
| `game/data/shaders/clouds.frag` | Créer | Fragment shader raymarch + compositing. |
| `src/client/render/CloudPass.h` | Créer | Passe Vulkan (interface). |
| `src/client/render/CloudPass.cpp` | Créer | Passe Vulkan (impl, calquée fog). |
| `src/client/app/Engine.cpp` | Modifier | Opcode 156 → `SetWeather` (Task 3) ; insertion frame-graph + push-constants (Task 8). |
| `src/client/render/DeferredPipeline.{h,cpp}` | Modifier | Init `CloudPass` + accesseur (Task 7). |
| `CMakeLists.txt` | Modifier | Sources `engine_core` + tests (Tasks 2, 9). |

---

## PHASE 1 — Nuages visibles pilotés météo + jour/nuit

### Task 1 : `CloudParams` (struct apparence + interpolation)

**Files:**
- Create: `src/client/render/clouds/CloudParams.h`
- Create: `src/client/render/clouds/CloudParamsTests.cpp`

- [ ] **Step 1: Écrire le test qui échoue**

`src/client/render/clouds/CloudParamsTests.cpp` :

```cpp
#include "src/client/render/clouds/CloudParams.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using engine::render::CloudParams;

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// Lerp à t=0 rend a, à t=1 rend b, à t=0.5 rend la moyenne.
void TestLerpEndpointsAndMid()
{
    CloudParams a{};
    a.coverage = 0.0f; a.density = 0.0f; a.baseAltMeters = 100.0f; a.topAltMeters = 200.0f;
    CloudParams b{};
    b.coverage = 1.0f; b.density = 2.0f; b.baseAltMeters = 300.0f; b.topAltMeters = 600.0f;

    CloudParams at0 = CloudParams::Lerp(a, b, 0.0f);
    assert(Near(at0.coverage, 0.0f));
    assert(Near(at0.density, 0.0f));

    CloudParams at1 = CloudParams::Lerp(a, b, 1.0f);
    assert(Near(at1.coverage, 1.0f));
    assert(Near(at1.topAltMeters, 600.0f));

    CloudParams mid = CloudParams::Lerp(a, b, 0.5f);
    assert(Near(mid.coverage, 0.5f));
    assert(Near(mid.density, 1.0f));
    assert(Near(mid.baseAltMeters, 200.0f));
    assert(Near(mid.topAltMeters, 400.0f));
    std::puts("[OK] TestLerpEndpointsAndMid");
}

// t hors [0,1] est clampé.
void TestLerpClamps()
{
    CloudParams a{}; a.coverage = 0.2f;
    CloudParams b{}; b.coverage = 0.8f;
    CloudParams below = CloudParams::Lerp(a, b, -1.0f);
    CloudParams above = CloudParams::Lerp(a, b, 5.0f);
    assert(Near(below.coverage, 0.2f));
    assert(Near(above.coverage, 0.8f));
    std::puts("[OK] TestLerpClamps");
}

int main()
{
    TestLerpEndpointsAndMid();
    TestLerpClamps();
    std::puts("[ALL OK] CloudParamsTests");
    return 0;
}
```

- [ ] **Step 2: Lancer le test pour confirmer l'échec de compilation**

Le build échoue : `CloudParams.h` n'existe pas. (Pas de toolchain locale — l'échec est attendu à la compilation CI/VS.)

- [ ] **Step 3: Écrire l'implémentation minimale**

`src/client/render/clouds/CloudParams.h` :

```cpp
#pragma once
// Apparence des nuages volumétriques, dérivée de la météo (CloudWeatherMapper)
// puis interpolée pour les transitions douces. Pur CPU, aucune dépendance Vulkan.

namespace engine::render
{
	/// Paramètres d'apparence d'une couche nuageuse. Toutes les valeurs sont
	/// continues pour permettre l'interpolation entre deux états météo.
	struct CloudParams
	{
		float coverage      = 0.4f;   ///< [0..1] fraction de ciel couverte.
		float density       = 0.6f;   ///< [0..2] opacité/épaisseur du milieu.
		float baseAltMeters = 800.0f; ///< Altitude (m) de la base des nuages.
		float topAltMeters  = 2200.0f;///< Altitude (m) du sommet des nuages.
		float tintR         = 1.0f;   ///< Teinte multiplicative R (1 = neutre).
		float tintG         = 1.0f;   ///< Teinte multiplicative G.
		float tintB         = 1.0f;   ///< Teinte multiplicative B.

		/// Interpolation linéaire composant par composant. \p t est clampé à [0,1].
		static CloudParams Lerp(const CloudParams& a, const CloudParams& b, float t)
		{
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			CloudParams r;
			r.coverage      = a.coverage      + (b.coverage      - a.coverage)      * t;
			r.density       = a.density       + (b.density       - a.density)       * t;
			r.baseAltMeters = a.baseAltMeters + (b.baseAltMeters - a.baseAltMeters) * t;
			r.topAltMeters  = a.topAltMeters  + (b.topAltMeters  - a.topAltMeters)  * t;
			r.tintR         = a.tintR         + (b.tintR         - a.tintR)         * t;
			r.tintG         = a.tintG         + (b.tintG         - a.tintG)         * t;
			r.tintB         = a.tintB         + (b.tintB         - a.tintB)         * t;
			return r;
		}
	};
}
```

- [ ] **Step 4: Enregistrer le test dans CMake et vérifier qu'il passe (fait en Task 2 avec le mapper, regroupé)**

On enregistre les deux tests purs ensemble en Task 2 pour limiter les éditions CMake. Passer à Task 2.

- [ ] **Step 5: Commit**

```bash
git add src/client/render/clouds/CloudParams.h src/client/render/clouds/CloudParamsTests.cpp
git commit -m "feat(clouds): CloudParams (apparence + interpolation, pur CPU)"
```

---

### Task 2 : `CloudWeatherMapper` (WeatherState → CloudParams) + enregistrement tests

**Files:**
- Create: `src/client/render/clouds/CloudWeatherMapper.h`
- Create: `src/client/render/clouds/CloudWeatherMapper.cpp`
- Create: `src/client/render/clouds/CloudWeatherMapperTests.cpp`
- Modify: `CMakeLists.txt` (sources engine_core + 2 tests)

- [ ] **Step 1: Écrire le test qui échoue**

`src/client/render/clouds/CloudWeatherMapperTests.cpp` :

```cpp
#include "src/client/render/clouds/CloudWeatherMapper.h"
#include "src/client/render/WeatherSystem.h"

#include <cassert>
#include <cstdio>

using engine::render::CloudParams;
using engine::render::CloudWeatherMapper;
using engine::render::WeatherState;

// Clear -> couverture faible ; Storm -> couverture quasi pleine + dense + sombre.
void TestKindExtremes()
{
    CloudParams clear = CloudWeatherMapper::ParamsFor(WeatherState::Clear);
    CloudParams storm = CloudWeatherMapper::ParamsFor(WeatherState::Storm);

    assert(clear.coverage < 0.35f);
    assert(storm.coverage > 0.85f);
    assert(storm.density  > clear.density);
    assert(storm.tintR    < clear.tintR); // Storm plus sombre que Clear.
    std::puts("[OK] TestKindExtremes");
}

// Tous les états retournent des params bornés et cohérents (base < top).
void TestAllKindsSane()
{
    const WeatherState all[] = { WeatherState::Clear, WeatherState::Rain,
        WeatherState::Snow, WeatherState::Fog, WeatherState::Storm };
    for (WeatherState s : all)
    {
        CloudParams p = CloudWeatherMapper::ParamsFor(s);
        assert(p.coverage >= 0.0f && p.coverage <= 1.0f);
        assert(p.density  >= 0.0f);
        assert(p.baseAltMeters < p.topAltMeters);
    }
    std::puts("[OK] TestAllKindsSane");
}

int main()
{
    TestKindExtremes();
    TestAllKindsSane();
    std::puts("[ALL OK] CloudWeatherMapperTests");
    return 0;
}
```

- [ ] **Step 2: Vérifier l'échec (compilation : header absent)**

Build CI/VS échoue : `CloudWeatherMapper.h` manquant. Attendu.

- [ ] **Step 3: Écrire l'implémentation**

`src/client/render/clouds/CloudWeatherMapper.h` :

```cpp
#pragma once
// Mapper PUR : type météo client (engine::render::WeatherState) -> CloudParams.
// Aucune dépendance Vulkan. Source unique de vérité de l'apparence des nuages
// en fonction de l'état météo serveur déjà diffusé (réutilise WeatherSystem).

#include "src/client/render/clouds/CloudParams.h"
#include "src/client/render/WeatherSystem.h" // WeatherState

namespace engine::render
{
	class CloudWeatherMapper
	{
	public:
		/// Retourne les CloudParams cibles pour un état météo donné.
		/// Déterministe, sans état, testable sans GPU.
		static CloudParams ParamsFor(WeatherState state);
	};
}
```

`src/client/render/clouds/CloudWeatherMapper.cpp` :

```cpp
#include "src/client/render/clouds/CloudWeatherMapper.h"

namespace engine::render
{
	CloudParams CloudWeatherMapper::ParamsFor(WeatherState state)
	{
		CloudParams p;
		switch (state)
		{
		case WeatherState::Clear:
			p.coverage = 0.25f; p.density = 0.45f;
			p.baseAltMeters = 1200.0f; p.topAltMeters = 2600.0f;
			p.tintR = 1.0f; p.tintG = 1.0f; p.tintB = 1.0f;
			break;
		case WeatherState::Rain:
			p.coverage = 0.85f; p.density = 1.1f;
			p.baseAltMeters = 700.0f; p.topAltMeters = 2200.0f;
			p.tintR = 0.7f; p.tintG = 0.72f; p.tintB = 0.78f;
			break;
		case WeatherState::Snow:
			p.coverage = 0.7f; p.density = 0.8f;
			p.baseAltMeters = 800.0f; p.topAltMeters = 2000.0f;
			p.tintR = 0.92f; p.tintG = 0.94f; p.tintB = 1.0f;
			break;
		case WeatherState::Fog:
			p.coverage = 0.6f; p.density = 0.7f;
			p.baseAltMeters = 300.0f; p.topAltMeters = 1200.0f; // couche basse
			p.tintR = 0.85f; p.tintG = 0.86f; p.tintB = 0.88f;
			break;
		case WeatherState::Storm:
			p.coverage = 0.97f; p.density = 1.8f;
			p.baseAltMeters = 600.0f; p.topAltMeters = 3000.0f; // cumulonimbus
			p.tintR = 0.42f; p.tintG = 0.44f; p.tintB = 0.5f;
			break;
		default:
			break; // garde les defauts de CloudParams (clear-ish).
		}
		return p;
	}
}
```

- [ ] **Step 4: Enregistrer sources + tests dans CMake**

Dans `CMakeLists.txt`, ajouter à la liste des sources `engine_core` **après** la ligne `src/client/render/WeatherSystem.cpp` :

```cmake
  src/client/render/WeatherSystem.cpp
  src/client/render/clouds/CloudWeatherMapper.cpp
```

(`CloudParams.h` et `CloudWeatherMapper.h` sont header-only/déjà tirés.)

Puis, dans la zone des tests (près des autres `lcdlln_add_simple_test`), ajouter :

```cmake
lcdlln_add_simple_test(cloud_params_tests
  ${CMAKE_SOURCE_DIR}/src/client/render/clouds/CloudParamsTests.cpp)
lcdlln_add_simple_test(cloud_weather_mapper_tests
  ${CMAKE_SOURCE_DIR}/src/client/render/clouds/CloudWeatherMapperTests.cpp)
```

- [ ] **Step 5: Lancer les tests, vérifier qu'ils passent**

Run (CI Linux ou VS) : `ctest -R "cloud_params_tests|cloud_weather_mapper_tests" -V`
Expected: `[ALL OK] CloudParamsTests` et `[ALL OK] CloudWeatherMapperTests`, ctest PASS.

- [ ] **Step 6: Commit**

```bash
git add src/client/render/clouds/CloudWeatherMapper.h src/client/render/clouds/CloudWeatherMapper.cpp src/client/render/clouds/CloudWeatherMapperTests.cpp CMakeLists.txt
git commit -m "feat(clouds): CloudWeatherMapper (WeatherState -> CloudParams) + tests"
```

---

### Task 3 : Brancher le broadcast serveur (opcode 156) sur `WeatherSystem` (anti-doublon, gap pré-existant)

**Files:**
- Create: `src/client/render/clouds/WeatherKindMap.h`
- Create: `src/client/render/clouds/WeatherKindMapTests.cpp`
- Modify: `src/client/app/Engine.cpp:2968-2977` (handler `kOpcodeWeatherUpdateNotification`)
- Modify: `CMakeLists.txt` (1 test)

- [ ] **Step 1: Écrire le test qui échoue**

`src/client/render/clouds/WeatherKindMapTests.cpp` :

```cpp
#include "src/client/render/clouds/WeatherKindMap.h"
#include "src/client/render/WeatherSystem.h"

#include <cassert>
#include <cstdio>

using engine::render::WeatherState;
using engine::render::MapServerKindToWeatherState;

// Le wire serveur : Clear=0,Rain=1,Snow=2,Storm=3,Sandstorm=4,Fog=5.
// Le client     : Clear=0,Rain=1,Snow=2,Fog=3,Storm=4 (indices DIFFERENTS).
void TestServerKindMapping()
{
    assert(MapServerKindToWeatherState(0) == WeatherState::Clear);
    assert(MapServerKindToWeatherState(1) == WeatherState::Rain);
    assert(MapServerKindToWeatherState(2) == WeatherState::Snow);
    assert(MapServerKindToWeatherState(3) == WeatherState::Storm); // serveur Storm=3
    assert(MapServerKindToWeatherState(4) == WeatherState::Storm); // Sandstorm -> Storm (repli)
    assert(MapServerKindToWeatherState(5) == WeatherState::Fog);   // serveur Fog=5
    assert(MapServerKindToWeatherState(99) == WeatherState::Clear);// inconnu -> Clear
    std::puts("[OK] TestServerKindMapping");
}

int main()
{
    TestServerKindMapping();
    std::puts("[ALL OK] WeatherKindMapTests");
    return 0;
}
```

- [ ] **Step 2: Vérifier l'échec (header absent)**

Build échoue : `WeatherKindMap.h` manquant. Attendu.

- [ ] **Step 3: Écrire l'implémentation**

`src/client/render/clouds/WeatherKindMap.h` :

```cpp
#pragma once
// Conversion PURE du WeatherKind serveur (wire opcode 156, uint8) vers le
// WeatherState client (engine::render::WeatherState). Les deux enums DIFFERENT
// (indices Fog/Storm inversés ; pas de Sandstorm côté client) : mapping explicite,
// jamais un cast. Testable sans GPU.

#include "src/client/render/WeatherSystem.h" // WeatherState

#include <cstdint>

namespace engine::render
{
	/// \param serverKind valeur wire (Clear=0,Rain=1,Snow=2,Storm=3,Sandstorm=4,Fog=5).
	/// \return WeatherState client correspondant ; Clear pour toute valeur inconnue.
	inline WeatherState MapServerKindToWeatherState(uint8_t serverKind)
	{
		switch (serverKind)
		{
		case 0: return WeatherState::Clear;
		case 1: return WeatherState::Rain;
		case 2: return WeatherState::Snow;
		case 3: return WeatherState::Storm;      // serveur Storm
		case 4: return WeatherState::Storm;      // serveur Sandstorm -> repli Storm
		case 5: return WeatherState::Fog;        // serveur Fog
		default: return WeatherState::Clear;
		}
	}
}
```

- [ ] **Step 4: Câbler le handler opcode 156 sur `m_weatherSystem`**

Dans `src/client/app/Engine.cpp`, ajouter l'include en tête de fichier (près des autres includes render) :

```cpp
#include "src/client/render/clouds/WeatherKindMap.h"
```

Puis modifier le bloc existant `case kOpcodeWeatherUpdateNotification` (actuellement lignes ~2968-2977) pour qu'il alimente AUSSI le système visuel :

```cpp
			case kOpcodeWeatherUpdateNotification:
			{
				auto parsed = ParseWeatherUpdateNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] WEATHER_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_weatherUi.OnUpdateNotification(*parsed);
				// Signal autoritaire unique -> pilote le visuel (particules + nuages).
				// (Branchement absent jusqu'ici : la météo serveur n'affectait pas le rendu.)
				m_weatherSystem.SetWeather(
					engine::render::MapServerKindToWeatherState(parsed->kind));
				return;
			}
```

- [ ] **Step 5: Enregistrer le test dans CMake**

```cmake
lcdlln_add_simple_test(weather_kind_map_tests
  ${CMAKE_SOURCE_DIR}/src/client/render/clouds/WeatherKindMapTests.cpp)
```

- [ ] **Step 6: Lancer le test, vérifier**

Run : `ctest -R weather_kind_map_tests -V`
Expected: `[ALL OK] WeatherKindMapTests`, PASS.

- [ ] **Step 7: Commit**

```bash
git add src/client/render/clouds/WeatherKindMap.h src/client/render/clouds/WeatherKindMapTests.cpp src/client/app/Engine.cpp CMakeLists.txt
git commit -m "fix(weather): broadcast serveur (opcode 156) pilote enfin le WeatherSystem visuel"
```

---

### Task 4 : Shader `clouds.frag` (raymarch + compositing)

**Files:**
- Create: `game/data/shaders/clouds.frag`

> Le vertex shader est réutilisé : `shaders/lighting.vert.spv` (triangle plein écran partagé avec `volumetric_fog.frag`). La varying d'entrée DOIT correspondre à celle que `volumetric_fog.frag` reçoit de `lighting.vert` — **ouvrir `game/data/shaders/volumetric_fog.frag` et copier la ligne `layout(location = 0) in vec2 ...;`** (nom exact de la varying UV). Le code ci-dessous suppose `vUv`.

- [ ] **Step 1: Écrire le shader**

`game/data/shaders/clouds.frag` :

```glsl
#version 450

// Entrée plein écran (triangle généré par lighting.vert). Adapter le nom de la
// varying à celui de volumetric_fog.frag si différent.
layout(location = 0) in vec2 vUv;

layout(location = 0) out vec4 outColor;

// binding 0 = scene color HDR (post-fog), linéaire clamp
// binding 1 = depth scene (D32_SFLOAT en .r), nearest clamp
layout(binding = 0) uniform sampler2D uSceneColor;
layout(binding = 1) uniform sampler2D uSceneDepth;

layout(push_constant) uniform CloudPC
{
	mat4  invViewProj;   // reconstruit le rayon monde
	vec4  cameraPos;     // xyz = caméra ; w = temps (s)
	vec4  sunDir;        // xyz = direction VERS le soleil ; w = coverage [0..1]
	vec4  sunColor;      // xyz = couleur soleil ; w = density
	vec4  zenithColor;   // xyz = teinte zénith ciel ; w = baseAltMeters
	vec4  horizonColor;  // xyz = teinte horizon ciel ; w = topAltMeters
	vec4  windParams;    // x = ventX ; y = ventZ ; z = vitesse ; w = anisotropie HG g
	vec4  stepParams;    // x = nbStepsVue ; y = nbStepsLumière ; z = distMax (m) ; w = forceAmbiante
} pc;

// ---- Bruit value-noise 3D + FBM (hash, sans texture) ----
float hash13(vec3 p)
{
	p = fract(p * 0.1031);
	p += dot(p, p.yzx + 33.33);
	return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p)
{
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float n000 = hash13(i + vec3(0,0,0));
	float n100 = hash13(i + vec3(1,0,0));
	float n010 = hash13(i + vec3(0,1,0));
	float n110 = hash13(i + vec3(1,1,0));
	float n001 = hash13(i + vec3(0,0,1));
	float n101 = hash13(i + vec3(1,0,1));
	float n011 = hash13(i + vec3(0,1,1));
	float n111 = hash13(i + vec3(1,1,1));
	float nx00 = mix(n000, n100, f.x);
	float nx10 = mix(n010, n110, f.x);
	float nx01 = mix(n001, n101, f.x);
	float nx11 = mix(n011, n111, f.x);
	float nxy0 = mix(nx00, nx10, f.y);
	float nxy1 = mix(nx01, nx11, f.y);
	return mix(nxy0, nxy1, f.z);
}

float fbm(vec3 p)
{
	float a = 0.5;
	float sum = 0.0;
	for (int i = 0; i < 5; ++i)
	{
		sum += a * valueNoise(p);
		p *= 2.02;
		a *= 0.5;
	}
	return sum;
}

// Densité de nuage en un point monde p.
float cloudDensity(vec3 p)
{
	float baseAlt = pc.zenithColor.w;
	float topAlt  = pc.horizonColor.w;
	float h = (p.y - baseAlt) / max(topAlt - baseAlt, 1.0);
	if (h < 0.0 || h > 1.0) return 0.0;

	// Profil vertical : doux en bas et en haut.
	float heightGrad = smoothstep(0.0, 0.15, h) * smoothstep(1.0, 0.6, h);

	// Animation par le vent.
	vec3 wind = vec3(pc.windParams.x, 0.0, pc.windParams.y) * pc.windParams.z * pc.cameraPos.w;
	vec3 sp = (p + wind) * 0.0006;

	float coverage = pc.sunDir.w;
	float base = fbm(sp);
	// Seuil par couverture : plus coverage est haut, plus le ciel se remplit.
	float d = base - (1.0 - coverage);
	d = max(d, 0.0);

	// Érosion de détail haute fréquence sur les bords.
	float detail = fbm(sp * 4.0 + wind * 0.01);
	d = max(d - detail * 0.25 * (1.0 - coverage), 0.0);

	return d * heightGrad * pc.sunColor.w; // * density
}

void main()
{
	// Couleur de scène existante (déjà brouillardée par la passe fog amont).
	vec3 sceneCol = texture(uSceneColor, vUv).rgb;
	float depth   = texture(uSceneDepth, vUv).r;

	// Reconstruit le rayon monde de la caméra vers ce pixel.
	vec2 ndc = vUv * 2.0 - 1.0;
	vec4 farClip = pc.invViewProj * vec4(ndc, 1.0, 1.0);
	vec3 farWorld = farClip.xyz / farClip.w;
	vec3 ro = pc.cameraPos.xyz;
	vec3 rd = normalize(farWorld - ro);

	// Intersection de la dalle horizontale [baseAlt, topAlt].
	float baseAlt = pc.zenithColor.w;
	float topAlt  = pc.horizonColor.w;
	float tBase = (baseAlt - ro.y) / rd.y;
	float tTop  = (topAlt  - ro.y) / rd.y;
	float tEnter = min(tBase, tTop);
	float tExit  = max(tBase, tTop);
	tEnter = max(tEnter, 0.0);

	// Pas de dalle visible (rayon ne traverse pas, ou regarde le sol).
	if (tExit <= tEnter || rd.y == 0.0)
	{
		outColor = vec4(sceneCol, 1.0);
		return;
	}

	// Limite par la géométrie : si du solide est présent (depth < 1), on borne
	// la marche à sa distance approchée (évite que les nuages couvrent un relief proche).
	if (depth < 1.0)
	{
		vec4 gClip = pc.invViewProj * vec4(ndc, depth, 1.0);
		vec3 gWorld = gClip.xyz / gClip.w;
		float gDist = length(gWorld - ro);
		tExit = min(tExit, gDist);
		if (tExit <= tEnter)
		{
			outColor = vec4(sceneCol, 1.0);
			return;
		}
	}

	tExit = min(tExit, pc.stepParams.z); // distMax

	int   steps     = int(pc.stepParams.x);
	int   lightSteps= int(pc.stepParams.y);
	float dt        = (tExit - tEnter) / float(max(steps, 1));

	// Dithering bleu-noise-ish pour casser le banding (cf. SSAO IGN, PR #851).
	float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
	float t = tEnter + dt * dither;

	vec3  sun  = normalize(pc.sunDir.xyz);
	float g    = pc.windParams.w;
	float mu   = dot(rd, sun);
	// Phase Henyey-Greenstein.
	float hg = (1.0 - g * g) / (4.0 * 3.14159265 * pow(1.0 + g * g - 2.0 * g * mu, 1.5));

	float transmittance = 1.0;
	vec3  scattered     = vec3(0.0);

	vec3 sunCol  = pc.sunColor.rgb;
	vec3 skyAmb  = mix(pc.horizonColor.rgb, pc.zenithColor.rgb, 0.5) * pc.stepParams.w;
	vec3 tint    = vec3(1.0); // teinte appliquée via push (zenith/horizon déjà colorés)

	for (int i = 0; i < steps; ++i)
	{
		vec3 p = ro + rd * t;
		float dens = cloudDensity(p);
		if (dens > 0.001)
		{
			// Transmittance vers le soleil (marche courte).
			float lt = 0.0;
			float lightTrans = 1.0;
			float ldt = (topAlt - baseAlt) / float(max(lightSteps, 1));
			for (int j = 0; j < lightSteps; ++j)
			{
				lt += ldt;
				float ld = cloudDensity(p + sun * lt);
				lightTrans *= exp(-ld * ldt * 0.02);
			}
			// Powder (auto-ombrage des bords).
			float powder = 1.0 - exp(-dens * 2.0);
			vec3 lightCol = sunCol * lightTrans * hg * powder + skyAmb;

			float aT = exp(-dens * dt * 0.05);
			scattered += transmittance * (1.0 - aT) * lightCol;
			transmittance *= aT;
			if (transmittance < 0.01) break;
		}
		t += dt;
	}

	// Teinte d'apparence (Rain/Storm assombrit via zenith/horizon déjà passés).
	vec3 cloudCol = scattered;
	vec3 finalCol = sceneCol * transmittance + cloudCol;
	outColor = vec4(finalCol, 1.0);
}
```

- [ ] **Step 2: Compiler le shader en SPIR-V**

Run (Windows, depuis la racine) : `pwsh tools/compile_game_shaders.ps1`
Expected: génère `game/data/shaders/clouds.frag.spv` (aucune erreur glslangValidator).

- [ ] **Step 3: Commit**

```bash
git add game/data/shaders/clouds.frag game/data/shaders/clouds.frag.spv
git commit -m "feat(clouds): shader clouds.frag (raymarch FBM + compositing)"
```

---

### Task 5 : `CloudPass.h` (interface de la passe)

**Files:**
- Create: `src/client/render/CloudPass.h`

- [ ] **Step 1: Écrire le header**

`src/client/render/CloudPass.h` :

```cpp
#pragma once
// Passe GRAPHIQUE plein écran de nuages volumétriques ray-marchés (fragment shader).
// Calquée EXACTEMENT sur VolumetricFogPass : render pass 1 attachment color,
// descriptor set 0 de 2 combined image samplers (scene color, depth), push
// constants fragment, pipeline fullscreen triangle, framebuffer temporaire dans Record.
//
// Descriptor set 0 :
//   binding 0 = scene color HDR (post-fog)  (sampler linéaire clamp)
//   binding 1 = depth scene (D32_SFLOAT)     (sampler nearest clamp)

#include "src/client/render/FrameGraph.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	class CloudPass
	{
	public:
		/// Push constants (stage fragment). Layout EXACT du bloc push_constant de
		/// clouds.frag (vec4 = 16 o, mat4 = 64 o).
		struct CloudPushConstants
		{
			float invViewProj[16];  ///< 64 o.
			float cameraPos[4];     ///< xyz caméra ; w = temps (s).        16 o.
			float sunDir[4];        ///< xyz dir vers soleil ; w = coverage. 16 o.
			float sunColor[4];      ///< xyz couleur soleil ; w = density.   16 o.
			float zenithColor[4];   ///< xyz zénith ; w = baseAltMeters.      16 o.
			float horizonColor[4];  ///< xyz horizon ; w = topAltMeters.      16 o.
			float windParams[4];    ///< x ventX ; y ventZ ; z vitesse ; w = HG g. 16 o.
			float stepParams[4];    ///< x stepsVue ; y stepsLum ; z distMax ; w = ambiant. 16 o.
		};
		static_assert(sizeof(CloudPushConstants) == 176, "CloudPushConstants doit faire 176 octets");

		CloudPass() = default;
		CloudPass(const CloudPass&) = delete;
		CloudPass& operator=(const CloudPass&) = delete;

		/// Crée render pass, descriptor set layout/pool, samplers, pipeline plein écran.
		/// \param sceneColorHDRFormat doit être VK_FORMAT_R16G16B16A16_SFLOAT.
		/// \param vertSpirv/vertWordCount  SPIR-V du fullscreen triangle (lighting.vert.spv).
		/// \param fragSpirv/fragWordCount  SPIR-V de clouds.frag.spv.
		/// \param maxFrames frames en vol (un descriptor set par frame).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat sceneColorHDRFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Enregistre la passe. Lit scene color (idSceneColorIn) + depth (idDepth),
		/// composite les nuages, écrit dans idSceneColorOut.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
			ResourceId idSceneColorIn, ResourceId idDepth,
			ResourceId idSceneColorOut, const CloudPushConstants& params, uint32_t frameIndex);

		void Destroy(VkDevice device);
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_linearSampler       = VK_NULL_HANDLE;
		VkSampler             m_nearestSampler      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_descriptorSets;
		uint32_t m_maxFrames = 2;
	};
}
```

- [ ] **Step 2: Commit**

```bash
git add src/client/render/CloudPass.h
git commit -m "feat(clouds): CloudPass.h (interface passe fullscreen)"
```

---

### Task 6 : `CloudPass.cpp` (implémentation, calquée VolumetricFogPass)

**Files:**
- Create: `src/client/render/CloudPass.cpp`
- Modify: `CMakeLists.txt` (source engine_core)

> **Référence directe** : `src/client/render/VolumetricFogPass.cpp`. Structure identique ; deltas : **2 bindings** au lieu de 3, push constants `CloudPushConstants` (176 o) au lieu de `FogParams`, et `Record` prend 3 ResourceId (in, depth, out) au lieu de 4.

- [ ] **Step 1: Écrire l'implémentation**

`src/client/render/CloudPass.cpp` :

```cpp
#include "src/client/render/CloudPass.h"
#include "src/client/render/PipelineCache.h"
#include "src/shared/core/Log.h"

#include <array>
#include <cstring>

namespace engine::render
{
	namespace
	{
		VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t wordCount)
		{
			VkShaderModuleCreateInfo info{};
			info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = wordCount * sizeof(uint32_t);
			info.pCode    = code;
			VkShaderModule mod = VK_NULL_HANDLE;
			vkCreateShaderModule(device, &info, nullptr, &mod);
			return mod;
		}
	}

	bool CloudPass::Init(VkDevice device, VkPhysicalDevice /*physicalDevice*/,
		VkFormat sceneColorHDRFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		uint32_t maxFrames, VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || !vertSpirv || !fragSpirv
			|| vertWordCount == 0 || fragWordCount == 0)
		{
			LOG_ERROR(Render, "CloudPass::Init: invalid arguments");
			return false;
		}
		m_maxFrames = maxFrames > 0 ? maxFrames : 1;

		// 1. Render pass : 1 color attachment.
		{
			VkAttachmentDescription colorAtt{};
			colorAtt.format         = sceneColorHDRFormat;
			colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
			colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
			colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments    = &colorRef;

			VkSubpassDependency dep{};
			dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass    = 0;
			dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
				| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
				| VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dep.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			VkRenderPassCreateInfo rpInfo{};
			rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			rpInfo.attachmentCount = 1;
			rpInfo.pAttachments    = &colorAtt;
			rpInfo.subpassCount    = 1;
			rpInfo.pSubpasses      = &subpass;
			rpInfo.dependencyCount = 1;
			rpInfo.pDependencies   = &dep;

			if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateRenderPass failed");
				return false;
			}
		}

		// 2. Descriptor set layout : 2 combined image samplers (scene color, depth).
		{
			std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
			for (size_t i = 0; i < bindings.size(); ++i)
			{
				bindings[i].binding         = static_cast<uint32_t>(i);
				bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].descriptorCount = 1;
				bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			}
			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
			layoutInfo.pBindings    = bindings.data();
			if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateDescriptorSetLayout failed");
				Destroy(device); return false;
			}
		}

		// 3. Descriptor pool.
		{
			VkDescriptorPoolSize poolSize{};
			poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 2 * m_maxFrames;
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes    = &poolSize;
			poolInfo.maxSets       = m_maxFrames;
			if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateDescriptorPool failed");
				Destroy(device); return false;
			}
		}

		// 4. Alloue un descriptor set par frame.
		{
			std::vector<VkDescriptorSetLayout> layouts(m_maxFrames, m_descriptorSetLayout);
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool     = m_descriptorPool;
			allocInfo.descriptorSetCount = m_maxFrames;
			allocInfo.pSetLayouts        = layouts.data();
			m_descriptorSets.resize(m_maxFrames, VK_NULL_HANDLE);
			if (vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkAllocateDescriptorSets failed");
				Destroy(device); return false;
			}
		}

		// 5. Samplers : linéaire clamp (scene), nearest clamp (depth).
		{
			VkSamplerCreateInfo si{};
			si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			si.magFilter    = VK_FILTER_LINEAR;
			si.minFilter    = VK_FILTER_LINEAR;
			si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.maxLod       = 0.0f;
			if (vkCreateSampler(device, &si, nullptr, &m_linearSampler) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateSampler (linear) failed");
				Destroy(device); return false;
			}
			si.magFilter = VK_FILTER_NEAREST;
			si.minFilter = VK_FILTER_NEAREST;
			if (vkCreateSampler(device, &si, nullptr, &m_nearestSampler) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateSampler (nearest) failed");
				Destroy(device); return false;
			}
		}

		// 6. Pipeline layout : set 0 + push constants (176 o, fragment).
		{
			VkPushConstantRange pushRange{};
			pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			pushRange.offset     = 0;
			pushRange.size       = static_cast<uint32_t>(sizeof(CloudPushConstants));
			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount         = 1;
			layoutInfo.pSetLayouts            = &m_descriptorSetLayout;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges    = &pushRange;
			if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreatePipelineLayout failed");
				Destroy(device); return false;
			}
		}

		// 7. Pipeline graphique fullscreen triangle (pas de vertex input, pas de depth test).
		{
			VkShaderModule vertMod = CreateShaderModule(device, vertSpirv, vertWordCount);
			VkShaderModule fragMod = CreateShaderModule(device, fragSpirv, fragWordCount);
			if (vertMod == VK_NULL_HANDLE || fragMod == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "CloudPass: shader module creation failed");
				if (vertMod) vkDestroyShaderModule(device, vertMod, nullptr);
				if (fragMod) vkDestroyShaderModule(device, fragMod, nullptr);
				Destroy(device); return false;
			}

			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vertMod; stages[0].pName = "main";
			stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragMod; stages[1].pName = "main";

			VkPipelineVertexInputStateCreateInfo vi{};
			vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			VkPipelineInputAssemblyStateCreateInfo ia{};
			ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			VkPipelineViewportStateCreateInfo vp{};
			vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			vp.viewportCount = 1; vp.scissorCount = 1;
			VkPipelineRasterizationStateCreateInfo rs{};
			rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rs.polygonMode = VK_POLYGON_MODE_FILL;
			rs.cullMode    = VK_CULL_MODE_NONE; // fullscreen : pas de culling (CLAUDE.md OK)
			rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rs.lineWidth   = 1.0f;
			VkPipelineMultisampleStateCreateInfo ms{};
			ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			VkPipelineDepthStencilStateCreateInfo dss{};
			dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			dss.depthTestEnable = VK_FALSE; dss.depthWriteEnable = VK_FALSE;
			VkPipelineColorBlendAttachmentState blendAtt{};
			blendAtt.blendEnable    = VK_FALSE;
			blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
				| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			VkPipelineColorBlendStateCreateInfo cb{};
			cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			cb.attachmentCount = 1; cb.pAttachments = &blendAtt;
			VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dyn{};
			dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

			VkGraphicsPipelineCreateInfo gpInfo{};
			gpInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			gpInfo.stageCount          = 2;
			gpInfo.pStages             = stages;
			gpInfo.pVertexInputState   = &vi;
			gpInfo.pInputAssemblyState = &ia;
			gpInfo.pViewportState      = &vp;
			gpInfo.pRasterizationState = &rs;
			gpInfo.pMultisampleState   = &ms;
			gpInfo.pDepthStencilState  = &dss;
			gpInfo.pColorBlendState    = &cb;
			gpInfo.pDynamicState       = &dyn;
			gpInfo.layout              = m_pipelineLayout;
			gpInfo.renderPass          = m_renderPass;
			gpInfo.subpass             = 0;

			AssertPipelineCreationAllowed();
			PipelineCache::RegisterWarmupKey(HashGraphicsPsoKey(m_renderPass, 0, m_pipelineLayout, sceneColorHDRFormat, VK_FORMAT_UNDEFINED));
			VkResult res = vkCreateGraphicsPipelines(device, pipelineCache, 1, &gpInfo, nullptr, &m_pipeline);
			vkDestroyShaderModule(device, vertMod, nullptr);
			vkDestroyShaderModule(device, fragMod, nullptr);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "CloudPass: vkCreateGraphicsPipelines failed: {}", static_cast<int>(res));
				Destroy(device); return false;
			}
		}

		LOG_INFO(Render, "CloudPass: initialized (maxFrames={})", m_maxFrames);
		return true;
	}

	void CloudPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		VkExtent2D extent, ResourceId idSceneColorIn, ResourceId idDepth,
		ResourceId idSceneColorOut, const CloudPushConstants& params, uint32_t frameIndex)
	{
		if (!IsValid() || extent.width == 0 || extent.height == 0) return;

		VkImageView viewSceneIn = registry.getImageView(idSceneColorIn);
		VkImageView viewDepth   = registry.getImageView(idDepth);
		VkImageView viewOut     = registry.getImageView(idSceneColorOut);
		if (viewSceneIn == VK_NULL_HANDLE || viewDepth == VK_NULL_HANDLE || viewOut == VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "CloudPass::Record: missing image views, skipping");
			return;
		}

		const uint32_t setIdx = frameIndex % m_maxFrames;
		VkDescriptorSet ds = m_descriptorSets[setIdx];

		std::array<VkDescriptorImageInfo, 2> imageInfos{};
		imageInfos[0] = { m_linearSampler,  viewSceneIn, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[1] = { m_nearestSampler, viewDepth,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		std::array<VkWriteDescriptorSet, 2> writes{};
		for (size_t i = 0; i < writes.size(); ++i)
		{
			writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[i].dstSet          = ds;
			writes[i].dstBinding      = static_cast<uint32_t>(i);
			writes[i].descriptorCount = 1;
			writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].pImageInfo      = &imageInfos[i];
		}
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass      = m_renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments    = &viewOut;
		fbInfo.width           = extent.width;
		fbInfo.height          = extent.height;
		fbInfo.layers          = 1;
		VkFramebuffer fb = VK_NULL_HANDLE;
		if (vkCreateFramebuffer(device, &fbInfo, nullptr, &fb) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "CloudPass::Record: vkCreateFramebuffer failed");
			return;
		}

		VkClearValue clearVal{};
		clearVal.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		VkRenderPassBeginInfo rpBegin{};
		rpBegin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass      = m_renderPass;
		rpBegin.framebuffer     = fb;
		rpBegin.renderArea      = { { 0, 0 }, extent };
		rpBegin.clearValueCount = 1;
		rpBegin.pClearValues    = &clearVal;
		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &ds, 0, nullptr);

		VkViewport viewport{};
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor = { { 0, 0 }, extent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
			0, static_cast<uint32_t>(sizeof(CloudPushConstants)), &params);
		vkCmdDraw(cmd, 3, 1, 0, 0);
		vkCmdEndRenderPass(cmd);
		vkDestroyFramebuffer(device, fb, nullptr);
	}

	void CloudPass::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) { LOG_INFO(Render, "[CloudPass] Destroyed"); return; }
		if (m_pipeline)            { vkDestroyPipeline(device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
		if (m_pipelineLayout)      { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
		if (m_nearestSampler)      { vkDestroySampler(device, m_nearestSampler, nullptr); m_nearestSampler = VK_NULL_HANDLE; }
		if (m_linearSampler)       { vkDestroySampler(device, m_linearSampler, nullptr); m_linearSampler = VK_NULL_HANDLE; }
		m_descriptorSets.clear();
		if (m_descriptorPool)      { vkDestroyDescriptorPool(device, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
		if (m_descriptorSetLayout) { vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr); m_descriptorSetLayout = VK_NULL_HANDLE; }
		if (m_renderPass)          { vkDestroyRenderPass(device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
		LOG_INFO(Render, "[CloudPass] Destroyed");
	}
}
```

- [ ] **Step 2: Ajouter à `engine_core` dans CMake**

Dans `CMakeLists.txt`, **après** `src/client/render/VolumetricFogPass.cpp` :

```cmake
  src/client/render/VolumetricFogPass.cpp
  src/client/render/CloudPass.cpp
```

- [ ] **Step 3: Compiler (CI/VS) — vérifier que ça build**

Expected: build OK (le log `CloudPass: initialized` n'apparaît qu'au runtime une fois branché en Task 7).

- [ ] **Step 4: Commit**

```bash
git add src/client/render/CloudPass.cpp CMakeLists.txt
git commit -m "feat(clouds): CloudPass.cpp (passe fullscreen calquée VolumetricFogPass)"
```

---

### Task 7 : Brancher `CloudPass` dans `DeferredPipeline` (init + accesseur)

**Files:**
- Modify: `src/client/render/DeferredPipeline.h`
- Modify: `src/client/render/DeferredPipeline.cpp`

> Référence : l'init `VolumetricFogPass` dans `DeferredPipeline.cpp` (vers les lignes 320-337 ; chercher `m_volumetricFogPass.Init`). On ajoute un membre `m_cloudPass`, un flag, un accesseur, et un bloc Init symétrique réutilisant `lighting.vert.spv` + `clouds.frag.spv`.

- [ ] **Step 1: Déclarer membre + accesseur dans le header**

Dans `src/client/render/DeferredPipeline.h`, ajouter l'include et, près de `GetVolumetricFogPass()`, le membre + accesseur :

```cpp
#include "src/client/render/CloudPass.h"
```
```cpp
	CloudPass& GetCloudPass() { return m_cloudPass; }
	bool IsCloudPassReady() const { return m_cloudPass.IsValid(); }
```
```cpp
	CloudPass m_cloudPass; // membre privé, près de m_volumetricFogPass
```

- [ ] **Step 2: Init dans `DeferredPipeline.cpp`**

Juste après le bloc `m_volumetricFogPass.Init(...)` (réutilise la même lambda `loadSpirv` et `pipelineCacheHandle`) :

```cpp
		// Nuages volumétriques : réutilise le fullscreen triangle de lighting.vert.
		std::vector<uint32_t> cloudVert = loadSpirv("shaders/lighting.vert.spv");
		std::vector<uint32_t> cloudFrag = loadSpirv("shaders/clouds.frag.spv");
		if (!cloudVert.empty() && !cloudFrag.empty())
		{
			if (m_cloudPass.Init(device, physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT,
					cloudVert.data(), cloudVert.size(), cloudFrag.data(), cloudFrag.size(),
					2u, pipelineCacheHandle))
				LOG_INFO(Render, "[Boot] DeferredPipeline CloudPass OK");
			else
				LOG_WARN(Render, "clouds: CloudPass init failed");
		}
		else
		{
			LOG_WARN(Render, "clouds: SPIR-V manquant (lighting.vert.spv / clouds.frag.spv)");
		}
```

- [ ] **Step 3: Destroy**

Dans le `Destroy(device)` de `DeferredPipeline.cpp`, à côté de `m_volumetricFogPass.Destroy(device);` :

```cpp
		m_cloudPass.Destroy(device);
```

- [ ] **Step 4: Compiler (CI/VS)**

Expected: au boot, log `[Boot] DeferredPipeline CloudPass OK`.

- [ ] **Step 5: Commit**

```bash
git add src/client/render/DeferredPipeline.h src/client/render/DeferredPipeline.cpp
git commit -m "feat(clouds): init CloudPass dans DeferredPipeline + accesseur"
```

---

### Task 8 : Insertion frame-graph + assemblage des push-constants (Engine.cpp)

**Files:**
- Modify: `src/client/app/Engine.cpp` (déclaration ResourceId nuages ; pass frame-graph ; rebranchement Bloom)

> Référence : le bloc `m_frameGraph.addPass("VolumetricFog", ...)` (vers les lignes 6173-6274) et `addPass("Bloom_Prefilter", ...)` (vers 6276). On insère une passe `Clouds` entre les deux, qui lit `m_fgSceneColorFoggedId` + `m_fgDepthId` et écrit une nouvelle ressource `m_fgCloudsId` ; puis Bloom_Prefilter lit `m_fgCloudsId` au lieu de `m_fgSceneColorFoggedId`.

- [ ] **Step 1: Déclarer la ressource frame-graph nuages**

Repérer où `m_fgSceneColorFoggedId` est déclaré/créé (membre + `m_frameGraph.create...`/import) et ajouter en miroir `m_fgCloudsId` au **même format** `VK_FORMAT_R16G16B16A16_SFLOAT` et même extent. (Chercher `m_fgSceneColorFoggedId` pour localiser sa création de ressource transitoire, et dupliquer la ligne en `m_fgCloudsId`.)

Membre (près de `m_fgSceneColorFoggedId`) :
```cpp
	engine::render::ResourceId m_fgCloudsId{};
```

- [ ] **Step 2: Ajouter la passe `Clouds` après `VolumetricFog`**

Juste après le bloc `addPass("VolumetricFog", ...)` et avant `addPass("Bloom_Prefilter", ...)` :

```cpp
		const bool cloudsEnabled = m_cfg.GetBool("render.clouds.enabled", true);
		if (cloudsEnabled && m_pipeline && m_pipeline->IsCloudPassReady())
		{
			m_frameGraph.addPass("Clouds",
				[this](engine::render::PassBuilder& b) {
					b.read(m_fgSceneColorFoggedId, engine::render::ImageUsage::SampledRead);
					b.read(m_fgDepthId,            engine::render::ImageUsage::SampledRead);
					b.write(m_fgCloudsId,          engine::render::ImageUsage::ColorWrite);
				},
				[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
					// --- Assemble les push constants depuis l'état jour/nuit + météo + config. ---
					const engine::render::DayNightCycle::State& dn = m_dayNight.GetState();

					// Apparence cible dérivée du WeatherSystem, fondue par l'intensité de transition.
					engine::render::CloudParams target =
						engine::render::CloudWeatherMapper::ParamsFor(m_weatherSystem.GetTargetState());
					engine::render::CloudParams current =
						engine::render::CloudWeatherMapper::ParamsFor(m_weatherSystem.GetCurrentState());
					engine::render::CloudParams cp =
						engine::render::CloudParams::Lerp(current, target, m_weatherSystem.GetIntensity());

					engine::render::CloudPass::CloudPushConstants pc{};
					// invViewProj + cameraPos : réutilise EXACTEMENT la source que la passe fog
					// (chercher comment fp.invViewProj / fp.cameraPos sont remplis pour VolumetricFog
					//  dans cette même frame loop, et copier ces 16+3 floats ici).
					ComputeInvViewProj(pc.invViewProj);          // <- même helper/source que le fog
					pc.cameraPos[0] = m_cameraPosWorld[0];
					pc.cameraPos[1] = m_cameraPosWorld[1];
					pc.cameraPos[2] = m_cameraPosWorld[2];
					pc.cameraPos[3] = static_cast<float>(m_appTimeSeconds); // temps pour le vent

					for (int i = 0; i < 3; ++i)
					{
						pc.sunDir[i]       = dn.lightDir[i];
						pc.sunColor[i]     = dn.lightColor[i];
						pc.zenithColor[i]  = dn.skyZenith[i]  * (i == 0 ? cp.tintR : (i == 1 ? cp.tintG : cp.tintB));
						pc.horizonColor[i] = dn.skyHorizon[i] * (i == 0 ? cp.tintR : (i == 1 ? cp.tintG : cp.tintB));
					}
					pc.sunDir[3]       = cp.coverage;
					pc.sunColor[3]     = cp.density;
					pc.zenithColor[3]  = cp.baseAltMeters;
					pc.horizonColor[3] = cp.topAltMeters;

					// Vent déterministe depuis l'horloge partagée (cohérent multi-joueurs).
					pc.windParams[0] = 1.0f;  // direction X (normalisée approx)
					pc.windParams[1] = 0.3f;  // direction Z
					pc.windParams[2] = static_cast<float>(m_cfg.GetDouble("render.clouds.windScale", 6.0));
					pc.windParams[3] = 0.2f;  // anisotropie HG g

					pc.stepParams[0] = static_cast<float>(m_cfg.GetInt("render.clouds.raymarchSteps", 64));
					pc.stepParams[1] = static_cast<float>(m_cfg.GetInt("render.clouds.lightSteps", 6));
					pc.stepParams[2] = static_cast<float>(m_cfg.GetDouble("render.clouds.maxDistanceMeters", 60000.0));
					pc.stepParams[3] = static_cast<float>(m_cfg.GetDouble("render.clouds.ambientStrength", 0.4));

					m_pipeline->GetCloudPass().Record(
						m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
						m_fgSceneColorFoggedId, m_fgDepthId, m_fgCloudsId, pc, m_currentFrame % 2);
				});
		}
```

> **Note d'implémentation** : `ComputeInvViewProj`, `m_cameraPosWorld`, `m_appTimeSeconds` sont des repères. Lors de l'implémentation, **reprendre la source exacte** déjà utilisée pour remplir `FogParams::invViewProj`/`cameraPos` dans cette frame loop (les mêmes matrices caméra), pour garantir la cohérence. Ne pas introduire un 2e calcul de matrice.

- [ ] **Step 3: Rebrancher Bloom_Prefilter sur la sortie nuages**

Dans le `addPass("Bloom_Prefilter", ...)`, remplacer la lecture de `m_fgSceneColorFoggedId` par `m_fgCloudsId` **uniquement si** les nuages sont actifs. Le plus simple et sûr : introduire une variable locale qui pointe la bonne ressource :

```cpp
		const engine::render::ResourceId sceneAfterClouds =
			(cloudsEnabled && m_pipeline && m_pipeline->IsCloudPassReady())
				? m_fgCloudsId : m_fgSceneColorFoggedId;

		m_frameGraph.addPass("Bloom_Prefilter",
			[this, sceneAfterClouds](engine::render::PassBuilder& b) {
				b.read(sceneAfterClouds, engine::render::ImageUsage::SampledRead);
				b.write(m_fgBloomDownMipIds[0], engine::render::ImageUsage::ColorWrite);
			},
			/* ... reste inchangé ... */);
```

(Et adapter le corps de la lambda d'exécution de Bloom_Prefilter pour lire `sceneAfterClouds` au lieu de `m_fgSceneColorFoggedId`.)

- [ ] **Step 4: Compiler + lancer le client, valider visuellement**

Lancer le client (voir skill `run`). Attendu :
- Ciel dégagé (météo Clear) : quelques nuages épars.
- Forcer la météo (admin `/setweather` si dispo, ou via le panneau `WeatherUi`) vers Storm → ciel se couvre, nuages sombres et denses, transition douce sur ~30 s.
- Aube/coucher : nuages teintés par `skyHorizon`/`skyZenith`. Nuit : nuages sombres.
- Un relief proche occulte correctement les nuages (pas de nuage devant une montagne proche).

- [ ] **Step 5: Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(clouds): insertion passe Clouds (frame-graph) + push constants jour/nuit & météo"
```

---

### Task 9 : Clés de config + valeurs par défaut

**Files:**
- Modify: `game/data/config/config.json` (ou le fichier de config par défaut équivalent)

- [ ] **Step 1: Ajouter les clés `render.clouds.*`**

Ajouter (sous une section `render`) :

```json
"render": {
  "clouds": {
    "enabled": true,
    "raymarchSteps": 64,
    "lightSteps": 6,
    "maxDistanceMeters": 60000.0,
    "ambientStrength": 0.4,
    "windScale": 6.0
  }
}
```

(Si `render` existe déjà, fusionner l'objet `clouds` dedans sans dupliquer.)

- [ ] **Step 2: Valider**

Relancer le client : modifier `enabled` à `false` désactive proprement la passe (Bloom relit la scène brouillardée). `raymarchSteps` à 128 augmente la qualité (et le coût).

- [ ] **Step 3: Commit**

```bash
git add game/data/config/config.json
git commit -m "feat(clouds): clés de config render.clouds.* + valeurs par défaut"
```

---

## PHASE 2 — Ombres de nuages au sol (après P1 validée)

> Objectif : une cloud-shadow-map basse-rés (projection top-down de la couverture le long de `lightDir`), échantillonnée dans `lighting.frag` pour atténuer la lumière directionnelle. Implémenter seulement après que P1 rend des nuages corrects.

### Task 10 : Cloud shadow — atténuation directionnelle

**Files:**
- Create: `game/data/shaders/clouds_shadow.comp` (génère une R8 2D top-down de transmittance)
- Modify: `src/client/render/CloudPass.{h,cpp}` (méthode `GenerateShadow`) OU nouvelle petite passe `CloudShadowPass`
- Modify: `game/data/shaders/lighting.frag` (échantillonne la shadow nuage pour moduler le soleil)
- Modify: `src/client/app/Engine.cpp` (génère la map avant Lighting, passe la vue à LightingPass)

- [ ] **Step 1: Décision de structure**

Réutiliser le **même** `cloudDensity`/FBM que `clouds.frag` (factoriser dans un `.glsl` inclus par les deux, pour ne PAS dupliquer la fonction de bruit — exigence anti-doublon). Créer `game/data/shaders/clouds_common.glsl` avec `hash13/valueNoise/fbm/cloudDensity`, et l'`#include` depuis `clouds.frag` ET `clouds_shadow.comp`.

- [ ] **Step 2: Factoriser le bruit**

Extraire de `clouds.frag` (Task 4) les fonctions `hash13`, `valueNoise`, `fbm`, `cloudDensity` dans `game/data/shaders/clouds_common.glsl` ; remplacer dans `clouds.frag` par `#include "clouds_common.glsl"`. Recompiler, vérifier rendu identique à P1.

- [ ] **Step 3: Écrire `clouds_shadow.comp`**

Compute qui, pour chaque texel (x,z) d'une grille monde top-down (ex. 512×512 couvrant la zone autour de la caméra), marche verticalement de `topAlt` à `baseAlt` le long de `-lightDir`, accumule la densité, écrit `transmittance = exp(-sum)` dans une image R8.

```glsl
#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 0, r8) uniform writeonly image2D uShadow;
#include "clouds_common.glsl"
layout(push_constant) uniform ShadowPC {
	vec4 originXZ;   // xy = coin monde (m) ; z = taille couverte (m) ; w = temps
	vec4 sunDir;     // xyz dir vers soleil ; w = coverage
	vec4 altParams;  // x = baseAlt ; y = topAlt ; z = density ; w = steps
	vec4 windParams; // x ventX ; y ventZ ; z vitesse
} pc;
void main() {
	ivec2 px = ivec2(gl_GlobalInvocationID.xy);
	ivec2 sz = imageSize(uShadow);
	if (px.x >= sz.x || px.y >= sz.y) return;
	vec2 uv = (vec2(px) + 0.5) / vec2(sz);
	vec3 world = vec3(pc.originXZ.x + uv.x * pc.originXZ.z, pc.altParams.y,
	                  pc.originXZ.y + uv.y * pc.originXZ.z);
	vec3 sun = normalize(pc.sunDir.xyz);
	int steps = int(pc.altParams.w);
	float dt = (pc.altParams.y - pc.altParams.x) / float(max(steps,1));
	float sum = 0.0;
	vec3 p = world;
	for (int i = 0; i < steps; ++i) { sum += cloudDensity(p) * dt * 0.05; p -= sun * dt; }
	imageStore(uShadow, px, vec4(exp(-sum)));
}
```

> Note : `cloudDensity` dans `clouds_common.glsl` lit ses paramètres via des `#define` ou des uniformes communs — adapter pour qu'il prenne (coverage, density, baseAlt, topAlt, wind, time) en arguments explicites afin d'être appelable des deux shaders sans état global.

- [ ] **Step 4: Échantillonner dans `lighting.frag`**

Ajouter un sampler `uCloudShadow` + une matrice/origine monde→uv en push/UBO de `LightingPass`. Multiplier la contribution du soleil directionnel par `texture(uCloudShadow, worldToShadowUV(P)).r`. (Suivre le pattern d'échantillonnage de la shadow map CSM existante dans `lighting.frag`.)

- [ ] **Step 5: Générer la map chaque frame avant Lighting (Engine.cpp)**

Dispatch `clouds_shadow.comp` avant la passe Lighting (la cloud-shadow est une entrée du lighting). Throttling possible (regénérer toutes les N frames) car basse fréquence visuelle.

- [ ] **Step 6: Valider visuellement + commit**

Attendu : ombres douces mouvantes au sol sous couverture nuageuse, dérivant avec le vent. Commit :
```bash
git commit -am "feat(clouds): ombres de nuages au sol (clouds_shadow.comp + lighting.frag)"
```

---

## PHASE 3 — Couplage ambiant IBL (après P2)

### Task 11 : Modulation ambiante par la couverture

**Files:**
- Modify: `src/client/app/Engine.cpp` (calcule un scalaire de couverture et module l'ambiant/soleil de la scène)
- Modify: `src/client/render/LightingPass` push/UBO (facteur de couverture) si nécessaire

- [ ] **Step 1: Calculer le scalaire**

À partir de `cp.coverage` (déjà calculé en Task 8), dériver `overcast = clamp(cp.coverage * cp.density * 0.5, 0, 1)`.

- [ ] **Step 2: Moduler ambiant + soleil**

Atténuer l'intensité du soleil directionnel de `mix(1.0, 0.3, overcast)` et augmenter l'ambiant diffus de `mix(1.0, 1.4, overcast)` dans les paramètres passés à `LightingPass`. (Brancher sur les mêmes champs que ceux déjà passés au lighting ; ne pas recapturer l'IBL.)

- [ ] **Step 3: Valider + commit**

Attendu : sous Storm, scène globalement plus sombre/diffuse (ciel couvert), cohérent avec les nuages. Commit :
```bash
git commit -am "feat(clouds): couplage ambiant — ciel couvert assombrit/diffuse la scène"
```

---

## Auto-revue (effectuée par l'auteur du plan)

**Couverture du spec :**
- §3 composants : CloudParams (T1), CloudWeatherMapper (T2), CloudPass (T5/T6), shaders (T4/T10). CloudNoise délibérément remplacé par bruit in-shader (noté §cadrage). ✅
- §5 frame : insertion après fog/avant bloom (T8) — **déviation documentée** vs spec (« avant fog ») pour câblage minimal + perspective aérienne. À reporter dans le spec.
- §6 jour/nuit (T8 push constants depuis DayNightCycle::State). ✅
- §7 IBL ambiant (Phase 3 / T11). ✅
- §8 ombres au sol (Phase 2 / T10). ✅
- §9 pilotage météo + anti-doublon + gap opcode 156 (T2/T3). ✅
- §10 config (T9). ✅
- §12 tests (T1/T2/T3 purs). ✅ Raymarch non testable unitairement → validation visuelle (notée).
- §11 winding : `CULL_MODE_NONE`, aucun autre `frontFace` touché. ✅

**Scan placeholders :** les repères `ComputeInvViewProj`/`m_cameraPosWorld`/`m_appTimeSeconds` en T8 sont explicitement marqués « reprendre la source exacte du fog » — à résoudre à l'implémentation en lisant la frame loop. Ce n'est pas un trou de logique mais un point d'ancrage sur du code existant non lu au moment de la rédaction.

**Cohérence des types :** `CloudParams` (7 floats) ↔ `Lerp` ↔ `ParamsFor` ↔ `CloudPushConstants` (176 o, static_assert) ↔ bloc push_constant de `clouds.frag` : alignés. `MapServerKindToWeatherState(uint8)` ↔ `WeatherState` ↔ `SetWeather`. ✅

**Déviations à reporter dans le spec :** (1) bruit in-shader au lieu de textures 3D ; (2) passe fragment au lieu de compute ; (3) insertion après fog. Toutes cohérentes avec les contraintes réelles du moteur (FrameGraph sans storage-image, pattern fog).

## Déploiement

> **Déploiement** : ✅ **client uniquement, pas de redéploiement serveur.** Aucun changement wire/opcode/handler/migration.
