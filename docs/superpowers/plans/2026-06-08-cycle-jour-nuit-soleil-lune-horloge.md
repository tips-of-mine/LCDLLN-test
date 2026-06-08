# Cycle jour/nuit — nuit sombre, soleil/lune visibles, horloge à la liste perso — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rendre la nuit plus sombre et désaturée, corriger l'invisibilité du soleil et de la lune, et récupérer l'heure du monde (horloge serveur) dès la réponse liste des personnages (opcode 40) au lieu de l'entrée dans le monde.

**Architecture :** Trois parties indépendantes et committables séparément. **Partie A** (soleil/lune) et **Partie B** (nuit sombre) sont purement client-rendu et touchent `DayNightCycle` + `Engine` + auto-exposition — à faire dans l'ordre A puis B (mêmes fichiers). **Partie C** (horloge piggyback) étend l'opcode 40 (wire-break, redéploiement master) : payload partagé → handler master → flux client → application dans l'Engine.

**Tech Stack :** C++17/20, Vulkan, sérialisation maison `ByteReader`/`ByteWriter` + helpers `WorldClockPayloads`, tests CTest (exécutables liés à `engine_core`), CMake.

**Branche :** `feature/jour-nuit-soleil-lune-horloge` (déjà créée, contient le spec).

**Spec source :** `docs/superpowers/specs/2026-06-08-cycle-jour-nuit-soleil-lune-horloge-design.md`

---

## Structure des fichiers touchés

| Fichier | Partie | Rôle du changement |
|---------|--------|--------------------|
| `src/client/render/DayNightCycle.h` | A, B | Ajout `sunDir`/`moonDir` au `State` + helper `DirFromElevAzimuth` ; constantes nuit. |
| `src/client/render/DayNightCycle.cpp` | A, B | Calcul correct des directions (fix `cos(sin)`) ; constantes nuit + ambiant. |
| `src/client/render/tests/DayNightCycleTests.cpp` | A, B | **Créé** — tests directions soleil/lune + invariants nuit. |
| `src/client/app/Engine.cpp` | A, B, C | Push `sunDir`/`moonDir` au SkyPass + IBL ; plafond auto-exposition nuit ; application horloge piggyback + suppression requête 203 initiale. |
| `src/shared/network/CharacterPayloads.h` | C | Champs horloge dans `CharacterListResponsePayload` + signature builder. |
| `src/shared/network/CharacterPayloads.cpp` | C | Sérialisation/parse du bloc horloge. |
| `src/shared/network/CharacterListPayloadsTests.cpp` | C | Tests round-trip avec/sans bloc horloge. |
| `src/masterd/handlers/worldclock/WorldClockHandler.h/.cpp` | C | Helper `BuildStateResponse()`. |
| `src/masterd/handlers/character/CharacterListHandler.h/.cpp` | C | Lien `WorldClockHandler` + horloge dans la réponse. |
| `src/masterd/main_linux.cpp` | C | Câblage `SetWorldClockHandler`. |
| `src/shared/network/MasterShardClientFlow.h/.cpp` | C | Champs horloge dans le résultat + capture à la réception. |
| `src/client/auth/AuthUi.h`, `AuthUiPresenterCore.cpp` | C | Stockage de l'horloge reçue + accesseur. |
| `CMakeLists.txt` | A/B | Cible de test `day_night_cycle_tests`. |

---

# PARTIE A — Soleil et lune visibles

Cause racine (cf. spec §4.1) : la nuit, `lightDir` devient la direction de la **lune**, mais `Engine.cpp` calcule `moonDir = -lightDir` → la lune est dessinée sous l'horizon. De plus `DayNightCycle.cpp` calcule `cos(clampedElev)` où `clampedElev` est un **sinus** (`cos(sin(x))`), déformant la trajectoire. Correctif : exposer `sunDir` et `moonDir` **explicites et toujours corrects** dans le `State`, et pousser les vraies directions au shader.

> Le shader `sky.frag` n'a **pas** besoin de changer : il utilise déjà `pc.lightDir` pour le glow solaire et `pc.moonDir` pour le disque lunaire. Le bug est uniquement dans ce qu'on lui envoie.

### Task A1 : Tests directions soleil/lune (échec attendu)

**Files:**
- Create: `src/client/render/tests/DayNightCycleTests.cpp`
- Modify: `CMakeLists.txt` (après le bloc `character_list_payloads_tests`, ~ligne 1115)

- [ ] **Step 1 : Écrire le test**

Créer `src/client/render/tests/DayNightCycleTests.cpp` :

```cpp
// Tests directions soleil/lune du DayNightCycle (Partie A) + invariants nuit
// (Partie B). Pure logique CPU — pas de Vulkan. Retourne 0 si OK.

#include "src/client/render/DayNightCycle.h"

#include <cmath>
#include <cstdio>

namespace
{
	int s_fail = 0;
	void Check(bool cond, const char* msg)
	{
		if (!cond) { ++s_fail; std::fprintf(stderr, "[FAIL] %s\n", msg); }
	}
	float Dot(const float a[3], const float b[3])
	{
		return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
	}
}

int main()
{
	using engine::render::DayNightCycle;
	DayNightCycle dn;
	DayNightCycle::Params p;
	p.timeScale = 60.0f;
	dn.Init(p);

	// --- Midi : soleil quasi au zénith, lune quasi au nadir ---
	dn.SetTime(12.0f);
	{
		const auto& s = dn.GetState();
		Check(s.isDaytime, "midi: isDaytime");
		Check(s.sunDir[1] > 0.99f, "midi: sunDir pointe quasi droit en haut (pas de cos(sin) bug)");
		Check(s.moonDir[1] < -0.90f, "midi: moonDir sous l'horizon");
	}

	// --- Minuit : soleil sous l'horizon, lune au-dessus ---
	dn.SetTime(0.0f);
	{
		const auto& s = dn.GetState();
		Check(!s.isDaytime, "minuit: nuit");
		Check(s.sunDir[1] < -0.90f, "minuit: sunDir sous l'horizon");
		Check(s.moonDir[1] > 0.90f, "minuit: moonDir au-dessus de l'horizon");
	}

	// --- Soleil et lune opposés à toute heure ---
	for (float t = 0.0f; t < 24.0f; t += 3.0f)
	{
		dn.SetTime(t);
		const auto& s = dn.GetState();
		Check(Dot(s.sunDir, s.moonDir) < -0.95f, "soleil/lune opposes");
		const float lenSun = std::sqrt(Dot(s.sunDir, s.sunDir));
		Check(std::fabs(lenSun - 1.0f) < 1e-3f, "sunDir normalise");
	}

	return s_fail == 0 ? 0 : 1;
}
```

- [ ] **Step 2 : Enregistrer la cible CTest**

Dans `CMakeLists.txt`, juste après la ligne 1115 (`add_test(NAME character_list_payloads_tests ...)`), ajouter :

```cmake

# Cycle jour/nuit — directions soleil/lune + invariants nuit (pure CPU).
add_executable(day_night_cycle_tests src/client/render/tests/DayNightCycleTests.cpp)
target_link_libraries(day_night_cycle_tests PRIVATE engine_core)
add_test(NAME day_night_cycle_tests COMMAND day_night_cycle_tests)
```

- [ ] **Step 3 : Vérifier l'échec de compilation/test**

Le test référence `State::sunDir` / `State::moonDir` qui n'existent pas encore.
Attendu : **échec de compilation** (`sunDir` membre inconnu) — ce qui vaut « test rouge ».
(Build local indisponible : la vérification se fait en CI ou via VS. Marquer cette
étape faite une fois la cible ajoutée ; le rouge sera confirmé par l'absence des membres.)

- [ ] **Step 4 : Commit**

```bash
git add src/client/render/tests/DayNightCycleTests.cpp CMakeLists.txt
git commit -m "test(daynight): directions soleil/lune attendues (rouge)"
```

### Task A2 : Ajouter sunDir/moonDir au State + helper

**Files:**
- Modify: `src/client/render/DayNightCycle.h`

- [ ] **Step 1 : Ajouter les champs au `State`**

Dans `src/client/render/DayNightCycle.h`, dans `struct State`, juste après le bloc
`lightDir` (après la ligne `float lightDir[3] = { 0.5774f, 0.5774f, 0.5774f };`), insérer :

```cpp
		/// Normalised direction *toward the sun*, valide jour ET nuit (peut être
		/// sous l'horizon). Utilisée par le glow solaire du sky shader.
		float sunDir[3] = { 0.0f, 1.0f, 0.0f };

		/// Normalised direction *toward the moon*, valide jour ET nuit (peut être
		/// sous l'horizon). Utilisée par le disque lunaire du sky shader.
		float moonDir[3] = { 0.0f, -1.0f, 0.0f };
```

- [ ] **Step 2 : Déclarer le helper**

Dans la section `private:`, juste après la déclaration de `Clamp` (`static float Clamp(...)`), ajouter :

```cpp
		/// Construit une direction unitaire "vers l'astre" à partir du SINUS de son
		/// élévation et de son azimut (radians). Convertit d'abord le sinus en
		/// angle réel (asin) avant le cosinus — évite le bug cos(sin(x)).
		/// \param sinElev sinus de l'élévation [-1,1].
		/// \param azimuth azimut en radians.
		/// \param out     vecteur unitaire résultat (3 floats).
		static void DirFromElevAzimuth(float sinElev, float azimuth, float out[3]);
```

- [ ] **Step 3 : Commit**

```bash
git add src/client/render/DayNightCycle.h
git commit -m "feat(daynight): expose sunDir/moonDir + helper DirFromElevAzimuth"
```

### Task A3 : Calculer les directions correctement (fix cos(sin))

**Files:**
- Modify: `src/client/render/DayNightCycle.cpp`

- [ ] **Step 1 : Implémenter le helper**

Dans `src/client/render/DayNightCycle.cpp`, juste après la définition de `DayNightCycle::Clamp` (qui finit ligne ~72), ajouter :

```cpp
	/*static*/ void DayNightCycle::DirFromElevAzimuth(float sinElev, float azimuth, float out[3])
	{
		const float s    = Clamp(sinElev, -1.0f, 1.0f);
		const float elev = std::asin(s);     // angle réel d'élévation
		const float ce   = std::cos(elev);   // composante horizontale
		out[0] = ce * std::cos(azimuth);
		out[1] = s;                          // sin(elev) == s
		out[2] = ce * std::sin(azimuth);
	}
```

- [ ] **Step 2 : Remplacer le bloc de direction dans `ComputeState`**

Dans `ComputeState`, remplacer **tout le bloc** depuis le commentaire
`// ---- Sun direction (toward-sun unit vector) ----` jusqu'à la fin de la
normalisation (le bloc `else { ... lightDir straight up ... }`, c.-à-d. les
lignes actuelles ~218-261), par :

```cpp
		// ---- Directions soleil & lune (toujours calculées, jour comme nuit) ----
		const bool isSunUp = (sunElevation > 0.0f);
		m_state.isDaytime  = isSunUp;

		// Soleil.
		DirFromElevAzimuth(sunElevation, sunAzimuth, m_state.sunDir);

		// Lune = soleil décalé de 12 h (arc opposé).
		const float moonTime    = (m_timeOfDay + 12.0f >= 24.0f)
		                        ? m_timeOfDay - 12.0f : m_timeOfDay + 12.0f;
		const float moonElev    = std::sin(moonTime * kPi / 12.0f - kPi / 2.0f);
		const float moonDayFrac = Clamp((moonTime - 6.0f) / 12.0f, 0.0f, 1.0f);
		const float moonAzimuth = kAzimuthDawn + (kAzimuthDusk - kAzimuthDawn) * moonDayFrac;
		DirFromElevAzimuth(moonElev, moonAzimuth, m_state.moonDir);

		// Lumière directionnelle active = soleil le jour, lune la nuit.
		const float* active = isSunUp ? m_state.sunDir : m_state.moonDir;
		m_state.lightDir[0] = active[0];
		m_state.lightDir[1] = active[1];
		m_state.lightDir[2] = active[2];
```

> Note : la variable `isSunUp` était déclarée plus bas dans l'ancien code ; cette
> nouvelle version la déclare ici. Vérifier qu'il ne reste pas de double
> déclaration `const bool isSunUp` plus loin dans `ComputeState` (le bloc couleur
> de lumière et le bloc ciel la réutilisent — ils doivent référencer celle-ci).
> Supprimer l'ancienne déclaration `const bool isSunUp = (sunElevation > 0.0f);`
> si elle subsiste après le remplacement.

- [ ] **Step 3 : Vérifier le test (vert)**

Run : `ctest -R day_night_cycle_tests --output-on-failure` (en CI / VS).
Attendu : **PASS** (sunDir.y≈1 à midi, moon opposé, normalisé).

- [ ] **Step 4 : Commit**

```bash
git add src/client/render/DayNightCycle.cpp
git commit -m "fix(daynight): directions soleil/lune correctes (corrige cos(sin))"
```

### Task A4 : Pousser sunDir/moonDir au SkyPass et à l'IBL

**Files:**
- Modify: `src/client/app/Engine.cpp`

- [ ] **Step 1 : SkyPass — utiliser sunDir pour le glow, moonDir réel**

Localiser le bloc de push-constants SkyPass (rechercher `skyPc.lightDir[0]      = dn.lightDir[0];`, ~ligne 5555). Remplacer les 3 lignes `skyPc.lightDir[...] = dn.lightDir[...]` par :

```cpp
												skyPc.lightDir[0]      = dn.sunDir[0];
												skyPc.lightDir[1]      = dn.sunDir[1];
												skyPc.lightDir[2]      = dn.sunDir[2];
```

Puis remplacer les 3 lignes `skyPc.moonDir[...] = -dn.lightDir[...]` (le commentaire
au-dessus dit « Lune = direction opposee au soleil ») par :

```cpp
													// Lune : direction réelle de la lune (jour comme nuit).
													skyPc.moonDir[0]       = dn.moonDir[0];
													skyPc.moonDir[1]       = dn.moonDir[1];
													skyPc.moonDir[2]       = dn.moonDir[2];
```

- [ ] **Step 2 : IBL (capture initiale) — sunDir/moonDir réels**

Localiser le 1er bloc IBL (rechercher `iblSky.moonDir[i]      = -dnIbl.lightDir[i];`, ~ligne 4078). Dans cette boucle, remplacer :

```cpp
												iblSky.lightDir[i]     = dnIbl.lightDir[i];
												iblSky.zenithColor[i]  = dnIbl.skyZenith[i];
												iblSky.horizonColor[i] = dnIbl.skyHorizon[i];
												iblSky.moonDir[i]      = -dnIbl.lightDir[i];
```

par :

```cpp
												iblSky.lightDir[i]     = dnIbl.sunDir[i];
												iblSky.zenithColor[i]  = dnIbl.skyZenith[i];
												iblSky.horizonColor[i] = dnIbl.skyHorizon[i];
												iblSky.moonDir[i]      = dnIbl.moonDir[i];
```

- [ ] **Step 3 : IBL (régénération périodique) — idem**

Localiser le 2e bloc IBL (rechercher `iblSky.moonDir[i]      = -dn.lightDir[i];`, ~ligne 7988). Remplacer :

```cpp
				iblSky.lightDir[i]     =  dn.lightDir[i];
				iblSky.zenithColor[i]  =  dn.skyZenith[i];
				iblSky.horizonColor[i] =  dn.skyHorizon[i];
				iblSky.moonDir[i]      = -dn.lightDir[i];
```

par :

```cpp
				iblSky.lightDir[i]     =  dn.sunDir[i];
				iblSky.zenithColor[i]  =  dn.skyZenith[i];
				iblSky.horizonColor[i] =  dn.skyHorizon[i];
				iblSky.moonDir[i]      =  dn.moonDir[i];
```

> Laisser `m_zoneAtmosphere.sunDirection = dnState.lightDir` (lignes ~7936-7938)
> **inchangé** : la lumière de scène doit rester l'astre actif (lune la nuit).

- [ ] **Step 4 : Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "fix(render): SkyPass/IBL utilisent sunDir/moonDir reels (lune visible la nuit)"
```

### Task A5 : Réglage taille soleil/lune (validation en jeu)

> Tâche de **tuning** — à exécuter pendant la validation en jeu (Partie finale).
> Le shader n'a pas besoin de changer pour le bug ; ces valeurs n'augmentent que
> la lisibilité si les astres apparaissent trop petits.

**Files:**
- Modify: `game/data/shaders/sky.frag`

- [ ] **Step 1 : (si soleil trop petit) élargir le disque solaire**

Dans `game/data/shaders/sky.frag`, ligne 142, remplacer :

```glsl
    float sunGlow = pow(sunDot, 256.0);
```

par :

```glsl
    float sunGlow = pow(sunDot, 90.0);
```

- [ ] **Step 2 : (si lune trop petite) agrandir le disque lunaire**

Ligne 65, remplacer `const float kMoonRadius = 0.012;` par `const float kMoonRadius = 0.018;`.

- [ ] **Step 3 : Recompiler les shaders SPIR-V** (selon le pipeline de build shaders du projet) puis valider en jeu. Commit seulement si conservé :

```bash
git add game/data/shaders/sky.frag
git commit -m "polish(render): disque soleil/lune plus lisibles"
```

---

# PARTIE B — Nuit plus sombre et moins bleue

### Task B1 : Invariants nuit (test)

**Files:**
- Modify: `src/client/render/tests/DayNightCycleTests.cpp`

- [ ] **Step 1 : Ajouter les assertions nuit/jour**

Dans `src/client/render/tests/DayNightCycleTests.cpp`, juste avant `return s_fail == 0 ? 0 : 1;`, insérer :

```cpp
	// --- Nuit sombre mais pas noire, et moins claire que le jour ---
	auto luminance = [](const float c[3]) { return 0.2126f*c[0] + 0.7152f*c[1] + 0.0722f*c[2]; };
	dn.SetTime(0.0f);
	const float ambNight = luminance(dn.GetState().ambientColor);
	const float zenNight = luminance(dn.GetState().skyZenith);
	dn.SetTime(12.0f);
	const float ambDay = luminance(dn.GetState().ambientColor);
	Check(ambNight > 0.0f, "nuit: ambiant non nul (navigable)");
	Check(ambNight < ambDay, "nuit: ambiant plus sombre que le jour");
	Check(zenNight < 0.02f, "nuit: zenith ciel sombre");
```

- [ ] **Step 2 : Vérifier l'échec**

Run : `ctest -R day_night_cycle_tests --output-on-failure`.
Attendu : **FAIL** sur « nuit: zenith ciel sombre » (zénith actuel = 0.08 en B > 0.02).

- [ ] **Step 3 : Commit**

```bash
git add src/client/render/tests/DayNightCycleTests.cpp
git commit -m "test(daynight): invariants nuit sombre (rouge)"
```

### Task B2 : Assombrir les constantes de nuit

**Files:**
- Modify: `src/client/render/DayNightCycle.cpp`

- [ ] **Step 1 : Couleurs de ciel de nuit**

Lignes 22-23, remplacer :

```cpp
		constexpr float kSkyZenithNight[3]  = { 0.01f, 0.02f, 0.08f };
		constexpr float kSkyHorizonNight[3] = { 0.04f, 0.06f, 0.14f };
```

par :

```cpp
		constexpr float kSkyZenithNight[3]  = { 0.004f, 0.005f, 0.012f };
		constexpr float kSkyHorizonNight[3] = { 0.012f, 0.014f, 0.025f };
```

- [ ] **Step 2 : Plancher d'ambiant**

Ligne 47, remplacer `constexpr float kAmbientNightMin = 0.04f;` par `constexpr float kAmbientNightMin = 0.02f;`.

- [ ] **Step 3 : Réduire le biais bleu de l'ambiant**

Lignes 301-303, remplacer :

```cpp
		m_state.ambientColor[0] = 0.04f * ambientScale + 0.02f * dayT;
		m_state.ambientColor[1] = 0.04f * ambientScale + 0.04f * dayT;
		m_state.ambientColor[2] = 0.08f * ambientScale + 0.06f * dayT;
```

par :

```cpp
		// Nuit : gris froid neutre (canal bleu ~1.3× au lieu de 2×) pour éviter
		// la dominante bleue. Le terme additif * dayT redonne du bleu le jour.
		m_state.ambientColor[0] = 0.030f * ambientScale + 0.02f * dayT;
		m_state.ambientColor[1] = 0.032f * ambientScale + 0.04f * dayT;
		m_state.ambientColor[2] = 0.040f * ambientScale + 0.06f * dayT;
```

- [ ] **Step 4 : Vérifier le test (vert)**

Run : `ctest -R day_night_cycle_tests --output-on-failure`.
Attendu : **PASS** (zénith nuit = 0.012 < 0.02 ; ambiant nuit non nul et < jour).

- [ ] **Step 5 : Commit**

```bash
git add src/client/render/DayNightCycle.cpp
git commit -m "feat(daynight): nuit plus sombre et desaturee (moins bleue)"
```

### Task B3 : Plafonner l'auto-exposition la nuit

**Files:**
- Modify: `src/client/app/Engine.cpp`

- [ ] **Step 1 : Calculer un maxExposure réduit la nuit**

Localiser le bloc auto-exposition (rechercher `const float maxExp = static_cast<float>(m_cfg.GetDouble("exposure.max", 3.0));`, ~ligne 10472). Remplacer les deux lignes `minExp`/`maxExp` et l'appel `Update` par :

```cpp
	        const float minExp = static_cast<float>(m_cfg.GetDouble("exposure.min", 0.1));
	        const float maxExpDay   = static_cast<float>(m_cfg.GetDouble("exposure.max", 3.0));
	        const float maxExpNight = static_cast<float>(m_cfg.GetDouble("exposure.max_night", 1.0));
	        // Plafond d'exposition lissé selon l'élévation du soleil : la nuit on
	        // empêche l'auto-exposition de réhausser la luminance et d'annuler
	        // l'assombrissement voulu. sunDir.y = sin(élévation) ∈ [-1,1].
	        const float sunUpFactor = std::clamp(m_dayNight.GetState().sunDir[1] * 5.0f, 0.0f, 1.0f);
	        const float maxExp = maxExpNight + (maxExpDay - maxExpNight) * sunUpFactor;
	        m_pipeline->GetAutoExposure().Update(device, dt, key, speed, minExp, maxExp, frameIndex);
```

> `std::clamp` nécessite `<algorithm>` ; Engine.cpp l'inclut déjà (utilisé
> abondamment). Si l'édition signale un symbole manquant, ajouter `#include <algorithm>`.

- [ ] **Step 2 : Valider en jeu** (non automatisable) : la nuit doit être nettement plus sombre, sans « pompage » d'exposition qui ré-éclaire la scène. Ajuster `exposure.max_night` dans `config.json` si besoin.

- [ ] **Step 3 : Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(render): plafond auto-exposition la nuit (garde la nuit sombre)"
```

---

# PARTIE C — Horloge récupérée à la liste des personnages (piggyback)

⚠️ **Wire-break opcode 40** → redéploiement master requis, client + master en lock-step.

### Task C1 : Étendre le payload liste perso + tests (rouge)

**Files:**
- Modify: `src/shared/network/CharacterPayloads.h`
- Modify: `src/shared/network/CharacterListPayloadsTests.cpp`

- [ ] **Step 1 : Champs horloge dans la struct + include**

Dans `src/shared/network/CharacterPayloads.h`, ajouter en haut (après les autres includes, ~ligne 8) :

```cpp
#include "src/shared/network/WorldClockPayloads.h"
```

Dans `struct CharacterListResponsePayload` (~ligne 97), après `std::vector<CharacterListEntry> entries;`, ajouter :

```cpp
		/// Horloge monde embarquée (piggyback opcode 40). Présente quand
		/// hasWorldClock == 1 et success == 1. Permet au client de synchroniser
		/// le cycle jour/nuit dès la réception de la liste (avant EnterWorld).
		bool hasWorldClock = false;
		engine::network::worldclock::WorldClockStateResponse worldClock{};
```

Modifier la signature du builder (~ligne 107) :

```cpp
	std::vector<uint8_t> BuildCharacterListResponsePacket(uint8_t success, const std::vector<CharacterListEntry>& entries,
	                                                     uint32_t requestId, uint64_t sessionIdHeader,
	                                                     bool hasWorldClock = false,
	                                                     const engine::network::worldclock::WorldClockStateResponse& worldClock = {});
```

- [ ] **Step 2 : Test round-trip avec horloge**

Dans `src/shared/network/CharacterListPayloadsTests.cpp`, ajouter une fonction avant `main()` :

```cpp
static void TestWorldClockPiggyback()
{
	std::vector<CharacterListEntry> entries;
	CharacterListEntry a;
	a.character_id = 1u; a.name = "Test"; a.spawn_y = 100.0f;
	entries.push_back(a);

	engine::network::worldclock::WorldClockStateResponse wc;
	wc.status = engine::network::worldclock::WorldClockStatus::Ok;
	wc.serverTimeUnixMs = 1700000000000ull;
	wc.epochRefUnixMs   = 1690000000000ull;
	wc.timeScaleRealMinPerDay = 90.0f;
	wc.offsetGameSec    = 1234.5;
	wc.paused           = 1u;
	wc.pausedAtGameSec  = 42.0;
	wc.lunarPeriodGameSec = 229000.0;

	auto pkt = BuildCharacterListResponsePacket(1u, entries, 5u, 77u, true, wc);
	Assert(!pkt.empty(), "Build response with worldclock");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK (wc)");
	auto parsed = ParseCharacterListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->success == 1u, "parse success (wc)");
	Assert(parsed && parsed->entries.size() == 1u, "one entry (wc)");
	Assert(parsed && parsed->hasWorldClock, "hasWorldClock true");
	Assert(parsed && parsed->worldClock.serverTimeUnixMs == 1700000000000ull, "wc serverTime round-trips");
	Assert(parsed && parsed->worldClock.timeScaleRealMinPerDay == 90.0f, "wc timeScale round-trips");
	Assert(parsed && parsed->worldClock.paused == 1u, "wc paused round-trips");
	Assert(parsed && parsed->worldClock.lunarPeriodGameSec == 229000.0, "wc lunarPeriod round-trips");

	// Sans horloge : hasWorldClock == false.
	auto pkt2 = BuildCharacterListResponsePacket(1u, entries, 5u, 77u);
	PacketView view2;
	PacketView::Parse(pkt2.data(), pkt2.size(), view2);
	auto parsed2 = ParseCharacterListResponsePayload(view2.Payload(), view2.PayloadSize());
	Assert(parsed2 && !parsed2->hasWorldClock, "no worldclock when omitted");
}
```

Dans `main()`, ajouter l'appel après `TestPopulatedResponseRoundTrip();` :

```cpp
	TestWorldClockPiggyback();
```

- [ ] **Step 3 : Vérifier l'échec**

Run : `ctest -R character_list_payloads_tests --output-on-failure`.
Attendu : **FAIL** (le builder ignore encore l'horloge ; `hasWorldClock` reste false).

- [ ] **Step 4 : Commit**

```bash
git add src/shared/network/CharacterPayloads.h src/shared/network/CharacterListPayloadsTests.cpp
git commit -m "test(charlist): round-trip horloge piggyback opcode 40 (rouge)"
```

### Task C2 : Sérialiser/parser le bloc horloge

**Files:**
- Modify: `src/shared/network/CharacterPayloads.cpp`

- [ ] **Step 1 : Écriture (builder)**

Dans `BuildCharacterListResponsePacket`, modifier d'abord la signature pour qu'elle
corresponde au header :

```cpp
	std::vector<uint8_t> BuildCharacterListResponsePacket(uint8_t success, const std::vector<CharacterListEntry>& entries,
	                                                     uint32_t requestId, uint64_t sessionIdHeader,
	                                                     bool hasWorldClock,
	                                                     const engine::network::worldclock::WorldClockStateResponse& worldClock)
```

Puis, juste avant `const size_t payloadBytes = w.Offset();` (à la fin du `if (success != 0) { ... }`, après la boucle entries mais **dans** le bloc `success != 0`), ajouter l'écriture du bloc horloge :

```cpp
			// Bloc horloge piggyback : 1 octet flag + (si présent) 46 octets WorldClock.
			const uint8_t hasWc = hasWorldClock ? 1u : 0u;
			if (!w.WriteBytes(&hasWc, 1u))
				return {};
			if (hasWc)
			{
				std::vector<uint8_t> wcBytes;
				engine::network::worldclock::BuildWorldClockStateResponsePayload(worldClock, wcBytes);
				if (!w.WriteBytes(wcBytes.data(), wcBytes.size()))
					return {};
			}
```

- [ ] **Step 2 : Lecture (parser)**

Dans `ParseCharacterListResponsePayload`, juste avant `return out;` (la fin, après la
boucle `for` qui lit les entries), ajouter :

```cpp
		// Bloc horloge piggyback (optionnel, en fin de payload). Tolérant à
		// l'absence du flag (anciens paquets sans horloge) : si plus rien à lire,
		// hasWorldClock reste false.
		if (r.Remaining() >= 1u)
		{
			uint8_t hasWc = 0;
			if (!r.ReadBytes(&hasWc, 1u))
				return std::nullopt;
			if (hasWc)
			{
				uint8_t wcBuf[46];
				if (!r.ReadBytes(wcBuf, sizeof(wcBuf)))
					return std::nullopt;
				if (!engine::network::worldclock::ParseWorldClockStateResponsePayload(wcBuf, sizeof(wcBuf), out.worldClock))
					return std::nullopt;
				out.hasWorldClock = true;
			}
		}
```

- [ ] **Step 3 : Vérifier le test (vert)**

Run : `ctest -R character_list_payloads_tests --output-on-failure`.
Attendu : **PASS** (round-trip horloge OK ; cas sans horloge → false).

- [ ] **Step 4 : Commit**

```bash
git add src/shared/network/CharacterPayloads.cpp
git commit -m "feat(charlist): serialise le bloc horloge piggyback (opcode 40)"
```

### Task C3 : Helper BuildStateResponse côté master

**Files:**
- Modify: `src/masterd/handlers/worldclock/WorldClockHandler.h`
- Modify: `src/masterd/handlers/worldclock/WorldClockHandler.cpp`

- [ ] **Step 1 : Déclarer le helper**

Dans `WorldClockHandler.h`, section `public:`, après `engine::world::WorldClockParams GetParams() const;` (~ligne 98), ajouter :

```cpp
		/// Construit un WorldClockStateResponse (status Ok) à partir des params
		/// courants, avec serverTimeUnixMs = maintenant. Réutilisable par d'autres
		/// handlers (ex. CharacterListHandler) pour embarquer l'horloge. Prend le
		/// mutex en interne.
		engine::network::worldclock::WorldClockStateResponse BuildStateResponse() const;
```

Ajouter l'include nécessaire en tête du header (après `#include "src/shared/world/WorldClock.h"`) :

```cpp
#include "src/shared/network/WorldClockPayloads.h"
```

- [ ] **Step 2 : Implémenter le helper**

Dans `WorldClockHandler.cpp`, après `WorldClockParams WorldClockHandler::GetParams() const { ... }` (~ligne 49), ajouter :

```cpp
	engine::network::worldclock::WorldClockStateResponse WorldClockHandler::BuildStateResponse() const
	{
		engine::network::worldclock::WorldClockStateResponse resp;
		resp.status           = engine::network::worldclock::WorldClockStatus::Ok;
		resp.serverTimeUnixMs = NowMs();
		std::lock_guard<std::mutex> lock(m_mutex);
		resp.epochRefUnixMs         = m_params.epochRefUnixMs;
		resp.timeScaleRealMinPerDay = m_params.timeScaleRealMinPerDay;
		resp.offsetGameSec          = m_params.offsetGameSec;
		resp.paused                 = m_params.paused ? 1u : 0u;
		resp.pausedAtGameSec        = m_params.pausedAtGameSec;
		resp.lunarPeriodGameSec     = m_params.lunarPeriodGameSec;
		return resp;
	}
```

- [ ] **Step 3 : Commit**

```bash
git add src/masterd/handlers/worldclock/WorldClockHandler.h src/masterd/handlers/worldclock/WorldClockHandler.cpp
git commit -m "feat(worldclock): helper BuildStateResponse reutilisable"
```

### Task C4 : CharacterListHandler embarque l'horloge

**Files:**
- Modify: `src/masterd/handlers/character/CharacterListHandler.h`
- Modify: `src/masterd/handlers/character/CharacterListHandler.cpp`

- [ ] **Step 1 : Lien vers le WorldClockHandler**

Dans `CharacterListHandler.h`, déclarer la classe et le setter/membre.
Après `class ConnectionSessionMap;` (~ligne 15), ajouter :

```cpp
	class WorldClockHandler;
```

Dans `public:`, après `void SetConnectionPool(...)`, ajouter :

```cpp
		/// Branche le WorldClockHandler pour embarquer l'horloge dans la réponse
		/// liste perso (piggyback opcode 40). Optionnel : si nullptr, aucune
		/// horloge n'est jointe (hasWorldClock = 0).
		void SetWorldClockHandler(WorldClockHandler* wc);
```

Dans `private:`, après `engine::server::db::ConnectionPool* m_pool = nullptr;`, ajouter :

```cpp
		WorldClockHandler* m_worldClock = nullptr;
```

- [ ] **Step 2 : Implémenter le setter + inclure l'horloge**

Dans `CharacterListHandler.cpp`, ajouter l'include en tête (après les autres includes masterd) :

```cpp
#include "src/masterd/handlers/worldclock/WorldClockHandler.h"
```

Après `void CharacterListHandler::SetConnectionPool(...) { ... }` (~ligne 23), ajouter :

```cpp
	void CharacterListHandler::SetWorldClockHandler(WorldClockHandler* wc) { m_worldClock = wc; }
```

Remplacer la construction du paquet (ligne 135) :

```cpp
		auto pkt = BuildCharacterListResponsePacket(1u, entries, requestId, sessionIdHeader);
```

par :

```cpp
		// Piggyback horloge monde (opcode 40) : le client synchronise le cycle
		// jour/nuit dès la sélection de personnage, sans requête 203 séparée.
		const bool hasWc = (m_worldClock != nullptr);
		engine::network::worldclock::WorldClockStateResponse wc;
		if (hasWc)
			wc = m_worldClock->BuildStateResponse();
		auto pkt = BuildCharacterListResponsePacket(1u, entries, requestId, sessionIdHeader, hasWc, wc);
```

- [ ] **Step 3 : Commit**

```bash
git add src/masterd/handlers/character/CharacterListHandler.h src/masterd/handlers/character/CharacterListHandler.cpp
git commit -m "feat(master): CharacterListHandler embarque l'horloge (piggyback)"
```

### Task C5 : Câbler SetWorldClockHandler au boot master

**Files:**
- Modify: `src/masterd/main_linux.cpp`

- [ ] **Step 1 : Localiser le câblage**

Rechercher dans `src/masterd/main_linux.cpp` l'instance de `CharacterListHandler`
et celle de `WorldClockHandler` (chercher `CharacterListHandler` et
`SetConnectionPool` / `WorldClockHandler`). Les deux handlers sont configurés via
leurs `Set*()` au boot.

- [ ] **Step 2 : Ajouter le lien**

Juste après les autres `Set*()` du `CharacterListHandler` (par ex. après l'appel
`...SetConnectionPool(...)`), ajouter :

```cpp
	characterListHandler.SetWorldClockHandler(&worldClockHandler);
```

> Adapter les noms de variables aux instances réelles (ex. `worldClockHandler`,
> `characterListHandler`, ou les membres d'une struct d'app). Le `WorldClockHandler`
> doit être déjà instancié et `Configure()` à ce point — vérifier l'ordre.

- [ ] **Step 3 : Commit**

```bash
git add src/masterd/main_linux.cpp
git commit -m "feat(master): cable WorldClockHandler dans CharacterListHandler"
```

### Task C6 : Flux client — capter l'horloge à la réception

**Files:**
- Modify: `src/shared/network/MasterShardClientFlow.h`
- Modify: `src/shared/network/MasterShardClientFlow.cpp`

- [ ] **Step 1 : Champs résultat**

Dans `MasterShardClientFlow.h`, ajouter l'include en tête (avec les autres includes réseau) :

```cpp
#include "src/shared/network/WorldClockPayloads.h"
```

Dans `struct MasterShardFlowResult`, après `std::vector<CharacterListEntry> character_list;` (~ligne 43), ajouter :

```cpp
		/// Horloge monde reçue en piggyback de la liste perso (opcode 40). Permet
		/// au client de synchroniser le cycle jour/nuit dès la sélection de perso.
		bool has_world_clock = false;
		engine::network::worldclock::WorldClockStateResponse world_clock{};
		/// Timestamp Unix client (ms) capturé à la RÉCEPTION de la liste, pour
		/// calculer l'offset d'horloge correct quelle que soit l'heure
		/// d'application ultérieure de SetServerClock.
		uint64_t world_clock_client_recv_ms = 0;
```

- [ ] **Step 2 : Capter dans le lambda de réponse**

Dans `MasterShardClientFlow.cpp`, ajouter l'include `<chrono>` s'il manque (il est
généralement déjà présent). Dans le lambda de `disp.SendRequest(kOpcodeCharacterListRequest, ...)`
(~ligne 374), après `auto parsed = ParseCharacterListResponsePayload(...)` et dans
le bloc `if (parsed && parsed->success != 0)`, ajouter la capture de l'horloge :

```cpp
					if (parsed && parsed->success != 0)
					{
						characters = std::move(parsed->entries);
						listSuccess = true;
						if (parsed->hasWorldClock)
						{
							result.has_world_clock = true;
							result.world_clock = parsed->worldClock;
							result.world_clock_client_recv_ms = static_cast<uint64_t>(
								std::chrono::duration_cast<std::chrono::milliseconds>(
									std::chrono::system_clock::now().time_since_epoch()).count());
						}
					}
```

> `result` est la `MasterShardFlowResult` capturée par le flow (cf. `result.character_list`
> renseigné juste en dessous). Le lambda capture par référence (`[&]`), donc `result`
> est accessible.

- [ ] **Step 3 : Commit**

```bash
git add src/shared/network/MasterShardClientFlow.h src/shared/network/MasterShardClientFlow.cpp
git commit -m "feat(client-flow): capte l'horloge piggyback a la reception liste perso"
```

### Task C7 : Stocker l'horloge dans l'état AuthUi

**Files:**
- Modify: `src/client/auth/AuthUi.h`
- Modify: `src/client/auth/AuthUiPresenterCore.cpp`

- [ ] **Step 1 : Champs d'état + accesseur (AuthUi.h)**

Dans `src/client/auth/AuthUi.h`, ajouter l'include en tête si absent :

```cpp
#include "src/shared/network/WorldClockPayloads.h"
```

Dans la struct `local`-équivalente (celle contenant `characterList` + `shardEndpoint`,
~ligne 1089), après `std::vector<engine::network::CharacterListEntry> characterList;`, ajouter :

```cpp
			/// Horloge monde reçue en piggyback (opcode 40). Appliquée au cycle
			/// jour/nuit par l'Engine (main thread) au moment de l'entrée monde.
			bool hasWorldClock = false;
			engine::network::worldclock::WorldClockStateResponse worldClock{};
			uint64_t worldClockClientRecvMs = 0;
```

Dans les membres de `AuthUi` (près de `m_characterList`, ~ligne 951), ajouter :

```cpp
		bool m_hasWorldClock = false;
		engine::network::worldclock::WorldClockStateResponse m_worldClock{};
		uint64_t m_worldClockClientRecvMs = 0;
```

Dans `public:` (près de `CharacterListEntries()`, ~ligne 649), ajouter des accesseurs :

```cpp
		/// True si une horloge monde a été reçue en piggyback de la liste perso.
		bool HasWorldClock() const { return m_hasWorldClock; }
		/// L'état horloge piggyback (valide si HasWorldClock()).
		const engine::network::worldclock::WorldClockStateResponse& WorldClock() const { return m_worldClock; }
		/// Timestamp client (ms) de réception de l'horloge piggyback.
		uint64_t WorldClockClientRecvMs() const { return m_worldClockClientRecvMs; }
		/// Consommé par l'Engine : marque l'horloge piggyback comme appliquée
		/// (évite une double application).
		void ClearWorldClock() { m_hasWorldClock = false; }
```

- [ ] **Step 2 : Renseigner depuis le résultat du flow (AuthUiPresenterCore.cpp)**

Dans `src/client/auth/AuthUiPresenterCore.cpp`, après `local.characterList = std::move(r.character_list);` (~ligne 2463), ajouter :

```cpp
					// Horloge monde piggyback (opcode 40) — propagée à l'état pour
					// application par l'Engine au démarrage du monde.
					local.hasWorldClock = r.has_world_clock;
					local.worldClock = r.world_clock;
					local.worldClockClientRecvMs = r.world_clock_client_recv_ms;
```

Puis, là où `local` est recopié dans les membres `m_*` de l'`AuthUi` (chercher
`m_characterList = ` ou l'endroit où `local.characterList` est transféré vers le
membre — typiquement dans la phase de publication de l'état post-flow), ajouter en
parallèle :

```cpp
				m_hasWorldClock = local.hasWorldClock;
				m_worldClock = local.worldClock;
				m_worldClockClientRecvMs = local.worldClockClientRecvMs;
```

> Suivre exactement le pattern de `m_characterList` : si `m_characterList` est
> renseigné via `m_characterList = std::move(local.characterList);` à un endroit
> donné, placer les trois lignes horloge juste à côté.

- [ ] **Step 3 : Commit**

```bash
git add src/client/auth/AuthUi.h src/client/auth/AuthUiPresenterCore.cpp
git commit -m "feat(auth): stocke l'horloge piggyback dans l'etat AuthUi"
```

### Task C8 : Engine applique l'horloge & supprime la requête 203 initiale

**Files:**
- Modify: `src/client/app/Engine.cpp`

- [ ] **Step 1 : Appliquer l'horloge piggyback à l'entrée monde**

Localiser le bloc qui envoie la requête 203 initiale post-EnterWorld (rechercher
`// WorldClock sync (Task 6.2) — fetch l'etat horloge serveur a`, ~ligne 8370, suivi
du bloc `{ std::vector<uint8_t> wcReq; BuildWorldClockStateRequestPayload(wcReq); ... }`).
Remplacer **tout ce bloc** (lignes ~8370-8382) par :

```cpp
				// WorldClock sync — l'horloge est désormais reçue en piggyback de
				// la réponse liste des personnages (opcode 40) et stockée dans
				// l'AuthUi. On l'applique ici (main thread) au lieu d'envoyer une
				// requête 203 séparée. Repli : si aucune horloge piggyback (vieux
				// master / requête liste échouée), on garde l'ancien comportement
				// (requête 203) pour ne pas régresser le solo/compat.
				if (m_authUi.HasWorldClock())
				{
					const auto& wc = m_authUi.WorldClock();
					engine::world::WorldClockParams p;
					p.epochRefUnixMs         = wc.epochRefUnixMs;
					p.timeScaleRealMinPerDay = wc.timeScaleRealMinPerDay;
					p.offsetGameSec          = wc.offsetGameSec;
					p.paused                 = (wc.paused != 0u);
					p.pausedAtGameSec        = wc.pausedAtGameSec;
					p.lunarPeriodGameSec     = wc.lunarPeriodGameSec;
					m_dayNight.SetServerClock(p, wc.serverTimeUnixMs, m_authUi.WorldClockClientRecvMs());
					m_authUi.ClearWorldClock();
					LOG_INFO(Render, "[Engine] WorldClock applique depuis le piggyback liste perso");
				}
				else
				{
					std::vector<uint8_t> wcReq;
					engine::network::worldclock::BuildWorldClockStateRequestPayload(wcReq);
					(void)m_authUi.SendGenericRequestAsync(
						engine::network::kOpcodeWorldClockStateRequest, wcReq);
				}
```

> Vérifier que les noms de champs de `engine::world::WorldClockParams` correspondent
> à ceux utilisés dans le handler 204 existant (Engine.cpp ~3240-3246 : `epochRefUnixMs`,
> `timeScaleRealMinPerDay`, `offsetGameSec`, `paused`, `pausedAtGameSec`,
> `lunarPeriodGameSec`). Recopier exactement le même mapping que ce bloc.

- [ ] **Step 2 : Conserver la re-synchro anti-drift**

Ne PAS toucher le bloc de re-synchro périodique (Engine.cpp ~7947-7964) ni le
handler des opcodes 204/205 (~3227-3253) : ils restent nécessaires pour le drift
et les changements admin (`/settime`, `/pausetime`).

- [ ] **Step 3 : Valider en jeu** (non automatisable) : à la connexion, le cycle
jour/nuit doit refléter l'heure serveur dès la sélection de personnage / l'entrée,
sans pop-in d'horloge. Vérifier les logs `[Engine] WorldClock applique depuis le piggyback`.

- [ ] **Step 4 : Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(client): applique l'horloge piggyback, supprime la requete 203 initiale"
```

---

## Validation finale (en jeu, manuelle)

- [ ] Nuit nettement plus sombre et **non bleue**, terrain encore discernable (pleine lune).
- [ ] Soleil visible le jour, à une position cohérente (lever est, coucher ouest).
- [ ] Lune visible la nuit (avec phase), **plus de halo blanc parasite**.
- [ ] Heure serveur correcte dès l'écran de sélection de personnage / l'entrée monde.
- [ ] Pas de requête 203 « initiale » dans les logs (sauf repli) ; re-synchro périodique toujours active.

## Déploiement

> **Déploiement** : ⚠️ redéploiement serveur (master) requis — wire-break opcode 40
> (horloge embarquée dans la réponse liste des personnages). Client + master en
> lock-step. Volets rendu (nuit + soleil/lune) : client uniquement.

---

## Self-review (auteur du plan)

**Couverture spec :**
- Spec §3 (nuit sombre) → Partie B (B1 test, B2 constantes, B3 auto-exposition). ✅
- Spec §4 (soleil/lune) → Partie A (A1 test, A2/A3 directions, A4 push Engine, A5 tuning). ✅
- Spec §5 (horloge piggyback) → Partie C (C1-C2 wire, C3-C5 master, C6-C8 client). ✅
- Spec §6 (tests) → A1/B1 (DayNightCycle), C1 (payload round-trip). ✅
- Spec §7 (déploiement) → section Déploiement. ✅

**Cohérence des types :** `sunDir`/`moonDir` (float[3]) définis en A2, utilisés en A3/A4/B3.
`hasWorldClock`/`worldClock` définis en C1, sérialisés en C2, produits en C3/C4, transportés
en C6, stockés en C7, consommés en C8. `BuildStateResponse()` défini en C3, appelé en C4.
`WorldClockParams` mappé identiquement au handler 204 existant.

**Pas de placeholder :** chaque étape de code montre le code complet. Les seuls points
« adapter aux noms réels » (C5 instances main_linux, C7 site de publication AuthUi) sont
des ancrages de localisation, pas des trous d'implémentation.
