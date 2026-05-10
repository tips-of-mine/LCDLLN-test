# Cycle jour/nuit + 16 phases lunaires — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Étendre le moteur LCDLLN avec un système de phases lunaires à 16 étapes synchronisé serveur ↔ client + rendu shader procédural + tests du cycle jour/nuit existant.

**Architecture:** `LunarCalendar` (header-only stateless dans `src/shardd/world/`) calcule la phase déterministe depuis l'epoch Unix. `LunarHandler` master pousse l'état initial (opcode 192/193) à la connexion + push (opcode 194) sur changement de phase via tick 5min. Côté client, `DayNightCycle` reçoit la phase via callback et `SkyPass` Vulkan dessine le disque lunaire procéduralement via push-constants étendus. Cycle = 14 jours réels = 16 phases × 21h.

**Tech Stack:** C++20, Vulkan 1.3, GLSL 450, CMake, vcpkg. Pattern strict aligné sur les wires récents (Mail à Loot). Build local impossible — vérification CI Linux + Windows.

---

## File Structure

### Nouveaux fichiers
- `src/shardd/world/LunarCalendar.h` — header-only, calcul stateless phase + illumination
- `src/shardd/world/LunarCalendarTests.cpp` — round-trip phase ↔ time, edge cases
- `src/shared/network/LunarPayloads.h` — déclarations structs + Build/Parse
- `src/shared/network/LunarPayloads.cpp` — sérialisation little-endian
- `src/shared/network/LunarPayloadsTests.cpp` — round-trip + edge cases
- `src/masterd/handlers/lunar/LunarHandler.h` — déclaration handler + Tick + CurrentPhase
- `src/masterd/handlers/lunar/LunarHandler.cpp` — dispatch + push broadcast
- `src/client/render/DayNightCycleTests.cpp` — tests du cycle jour/nuit existant
- `src/client/render/SkyPass.h` — déclaration pass Vulkan ciel
- `src/client/render/SkyPass.cpp` — pipeline, push-constants, fullscreen quad

### Fichiers modifiés
- `src/shared/network/ProtocolV1Constants.h` — opcodes 192-194
- `src/masterd/main_linux.cpp` — instanciation + dispatch + capture-list + Tick periodique
- `src/client/render/DayNightCycle.h` — extension State + callback
- `src/client/render/DayNightCycle.cpp` — implémentation callback
- `game/data/shaders/sky.frag` — disque lunaire procédural + push constants étendus
- `game/data/shaders/sky.vert` — pas-through fullscreen quad (vérifier qu'il existe déjà)
- `src/client/app/Engine.h` — membres SkyPass + lunar callback wiring
- `src/client/app/Engine.cpp` — dispatch opcodes 193/194 + slash commands /sky info|time|moon + intégration SkyPass
- `CMakeLists.txt` — sources engine_core + 3 nouveaux test targets
- `src/CMakeLists.txt` — sources server_app + payload partagé

---

## Task 1: Opcodes ProtocolV1Constants

**Files:**
- Modify: `src/shared/network/ProtocolV1Constants.h`

- [ ] **Step 1: Ajouter le bloc d'opcodes 192-194**

Ajouter à la fin du fichier (après le bloc Loot 182-187) :

```cpp
	// =====================================================================
	// Phase 5 step 3+4 — Lunar wire (16 phases lunaires synchronisees serveur
	// -> client). Cycle de 14 jours reels (16 phases x ~21h chacune), calcul
	// deterministe depuis epoch Unix. Master autoritaire ; client recoit
	// l'etat initial sur EnterWorld puis un push toutes les ~21h sur
	// changement de phase.
	//
	// Decoupage opcode :
	//   - State                    (192/193)             : etat initial sur connexion.
	//   - PhaseChangeNotification  (194, push, request_id=0) : changement de phase.
	// =====================================================================
	constexpr uint16_t kOpcodeLunarStateRequest             = 192u; ///< Client to Master : etat lunaire actuel (vide).
	constexpr uint16_t kOpcodeLunarStateResponse            = 193u; ///< Master to Client : phase 0..15, illumination 0..1, cycleStart, cycleDuration.
	constexpr uint16_t kOpcodeLunarPhaseChangeNotification  = 194u; ///< Master to Client (push, request_id=0) : changement de phase.
```

- [ ] **Step 2: Vérifier qu'aucun autre opcode n'utilise 192-194**

Run :
```bash
grep -n "= 192u\|= 193u\|= 194u" src/shared/network/ProtocolV1Constants.h
```
Expected : seules les 3 lignes ajoutées au Step 1.

---

## Task 2: LunarCalendar header-only + tests

**Files:**
- Create: `src/shardd/world/LunarCalendar.h`
- Create: `src/shardd/world/LunarCalendarTests.cpp`

- [ ] **Step 1: Créer le header LunarCalendar**

Écrire `src/shardd/world/LunarCalendar.h` :

```cpp
#pragma once
// LunarCalendar : calcul stateless de la phase lunaire courante a partir
// d'un timestamp Unix. 16 phases (0..15), cycle de 14 jours reels.
// Header-only, deterministe, partage par master et shardd.

#include <cstdint>
#include <cmath>

namespace engine::server::world
{
	/// Une phase lunaire complete avec son indice (0..15) et son
	/// illumination (0..1, fraction eclairee).
	struct LunarPhaseInfo
	{
		uint8_t phase        = 0;     ///< Index 0..15
		float   illumination = 0.0f;  ///< 0..1, fraction eclairee
	};

	/// Cycle complet en millisecondes : 14 jours * 24h * 3600s * 1000ms.
	inline constexpr uint64_t kDefaultLunarCycleMs = 14ull * 24ull * 3600ull * 1000ull;

	/// Nombre de phases dans le cycle.
	inline constexpr uint8_t kLunarPhaseCount = 16u;

	class LunarCalendar
	{
	public:
		/// Calcule la phase lunaire pour un timestamp donne.
		/// \param realNowMs       Timestamp Unix courant en ms.
		/// \param cycleStartMs    Timestamp Unix du debut du cycle de reference.
		/// \param cycleDurationMs Duree d'un cycle complet en ms.
		/// \return Phase + illumination. Si \p cycleDurationMs == 0 retourne {0, 0}.
		static LunarPhaseInfo Compute(uint64_t realNowMs,
		                              uint64_t cycleStartMs,
		                              uint64_t cycleDurationMs)
		{
			if (cycleDurationMs == 0) return {};
			const uint64_t elapsed = (realNowMs >= cycleStartMs) ? (realNowMs - cycleStartMs) : 0ull;
			const uint64_t cyclePos = elapsed % cycleDurationMs;
			const uint64_t phaseDurationMs = cycleDurationMs / kLunarPhaseCount;
			uint8_t phase = static_cast<uint8_t>(cyclePos / phaseDurationMs);
			if (phase >= kLunarPhaseCount) phase = kLunarPhaseCount - 1u;
			return { phase, ComputeIllumination(phase) };
		}

		/// Calcule l'illumination (0..1) pour un index de phase.
		/// Sinusoide centree sur Full Moon (index 7) :
		///   - phase 0  -> 0.0  (NewMoon)
		///   - phase 7  -> 1.0  (FullMoon)
		///   - phase 15 -> ~0.04 (Earthshine late, tres sombre)
		static float ComputeIllumination(uint8_t phase)
		{
			constexpr float kPi = 3.14159265358979323846f;
			const float t = (static_cast<float>(phase) - 7.0f) * (kPi / 8.0f);
			return 0.5f * (1.0f + std::cos(t));
		}

		/// Retourne le timestamp ms du prochain changement de phase.
		static uint64_t NextChangeTsMs(uint64_t realNowMs,
		                               uint64_t cycleStartMs,
		                               uint64_t cycleDurationMs)
		{
			if (cycleDurationMs == 0) return realNowMs;
			const uint64_t elapsed = (realNowMs >= cycleStartMs) ? (realNowMs - cycleStartMs) : 0ull;
			const uint64_t cyclePos = elapsed % cycleDurationMs;
			const uint64_t phaseDurationMs = cycleDurationMs / kLunarPhaseCount;
			const uint64_t currentPhaseStartMs = (cyclePos / phaseDurationMs) * phaseDurationMs;
			const uint64_t nextPhaseStartMs = currentPhaseStartMs + phaseDurationMs;
			const uint64_t cyclesElapsed = elapsed / cycleDurationMs;
			return cycleStartMs + (cyclesElapsed * cycleDurationMs) + nextPhaseStartMs;
		}
	};
}
```

- [ ] **Step 2: Créer les tests LunarCalendar**

Écrire `src/shardd/world/LunarCalendarTests.cpp` :

```cpp
// Tests du calcul de phase lunaire : round-trip phase <-> time, edge cases,
// illumination sinusoidale, wrap de cycle. Pas de framework, asserts simples.

#include "src/shardd/world/LunarCalendar.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using engine::server::world::LunarCalendar;
using engine::server::world::LunarPhaseInfo;
using engine::server::world::kDefaultLunarCycleMs;
using engine::server::world::kLunarPhaseCount;

namespace
{
	bool NearlyEqual(float a, float b, float eps = 0.001f)
	{
		return std::fabs(a - b) < eps;
	}

	void TestPhase0AtCycleStart()
	{
		auto info = LunarCalendar::Compute(0, 0, kDefaultLunarCycleMs);
		assert(info.phase == 0);
		assert(NearlyEqual(info.illumination, 0.0f));
		std::puts("[OK] TestPhase0AtCycleStart");
	}

	void TestPhase7AtHalfCycle()
	{
		const uint64_t halfCycle = kDefaultLunarCycleMs / 2;
		auto info = LunarCalendar::Compute(halfCycle, 0, kDefaultLunarCycleMs);
		assert(info.phase == 8 || info.phase == 7);
		std::puts("[OK] TestPhase7AtHalfCycle");
	}

	void TestPhase0AfterFullCycle()
	{
		auto info = LunarCalendar::Compute(kDefaultLunarCycleMs, 0, kDefaultLunarCycleMs);
		assert(info.phase == 0);
		std::puts("[OK] TestPhase0AfterFullCycle");
	}

	void TestEachPhaseTransition()
	{
		const uint64_t phaseDurationMs = kDefaultLunarCycleMs / kLunarPhaseCount;
		for (uint8_t expectedPhase = 0; expectedPhase < kLunarPhaseCount; ++expectedPhase)
		{
			const uint64_t midPhaseMs = static_cast<uint64_t>(expectedPhase) * phaseDurationMs + phaseDurationMs / 2;
			auto info = LunarCalendar::Compute(midPhaseMs, 0, kDefaultLunarCycleMs);
			if (info.phase != expectedPhase)
			{
				std::printf("[FAIL] expected phase %u got %u at midPhaseMs=%llu\n",
					expectedPhase, info.phase, static_cast<unsigned long long>(midPhaseMs));
				assert(info.phase == expectedPhase);
			}
		}
		std::puts("[OK] TestEachPhaseTransition");
	}

	void TestIlluminationMaxAtFullMoon()
	{
		float illum7 = LunarCalendar::ComputeIllumination(7);
		assert(NearlyEqual(illum7, 1.0f));
		std::puts("[OK] TestIlluminationMaxAtFullMoon");
	}

	void TestIlluminationMinAtNewMoon()
	{
		float illum0 = LunarCalendar::ComputeIllumination(0);
		assert(illum0 < 0.05f);
		std::puts("[OK] TestIlluminationMinAtNewMoon");
	}

	void TestIlluminationSymmetric()
	{
		// Phases 6 et 8 doivent avoir la meme illumination (sinusoide symetrique).
		float illum6 = LunarCalendar::ComputeIllumination(6);
		float illum8 = LunarCalendar::ComputeIllumination(8);
		assert(NearlyEqual(illum6, illum8));
		std::puts("[OK] TestIlluminationSymmetric");
	}

	void TestNoInvalidPhase()
	{
		// Apres 100 cycles, la phase doit toujours etre dans [0, 15].
		for (uint64_t k = 0; k < 100; ++k)
		{
			const uint64_t t = k * kDefaultLunarCycleMs + 12345;
			auto info = LunarCalendar::Compute(t, 0, kDefaultLunarCycleMs);
			assert(info.phase < kLunarPhaseCount);
		}
		std::puts("[OK] TestNoInvalidPhase");
	}

	void TestRealNowBeforeCycleStart()
	{
		auto info = LunarCalendar::Compute(0, 1000, kDefaultLunarCycleMs);
		assert(info.phase == 0);
		std::puts("[OK] TestRealNowBeforeCycleStart");
	}

	void TestZeroCycleDuration()
	{
		auto info = LunarCalendar::Compute(12345, 0, 0);
		assert(info.phase == 0);
		assert(NearlyEqual(info.illumination, 0.0f));
		std::puts("[OK] TestZeroCycleDuration");
	}

	void TestNextChangeTsAdvances()
	{
		const uint64_t phaseDurationMs = kDefaultLunarCycleMs / kLunarPhaseCount;
		uint64_t nowMs = 0;
		uint64_t next = LunarCalendar::NextChangeTsMs(nowMs, 0, kDefaultLunarCycleMs);
		assert(next == phaseDurationMs);
		nowMs = phaseDurationMs / 2;
		next = LunarCalendar::NextChangeTsMs(nowMs, 0, kDefaultLunarCycleMs);
		assert(next == phaseDurationMs);
		std::puts("[OK] TestNextChangeTsAdvances");
	}
}

int main()
{
	TestPhase0AtCycleStart();
	TestPhase7AtHalfCycle();
	TestPhase0AfterFullCycle();
	TestEachPhaseTransition();
	TestIlluminationMaxAtFullMoon();
	TestIlluminationMinAtNewMoon();
	TestIlluminationSymmetric();
	TestNoInvalidPhase();
	TestRealNowBeforeCycleStart();
	TestZeroCycleDuration();
	TestNextChangeTsAdvances();
	std::puts("[ALL OK] LunarCalendarTests");
	return 0;
}
```

---

## Task 3: LunarPayloads + tests

**Files:**
- Create: `src/shared/network/LunarPayloads.h`
- Create: `src/shared/network/LunarPayloads.cpp`
- Create: `src/shared/network/LunarPayloadsTests.cpp`

- [ ] **Step 1: Lire un payload récent comme référence pour le pattern**

Lire `src/shared/network/LootPayloads.h` et `LootPayloads.cpp` (les plus récents) pour copier exactement le style d'imports, `WriteU8/U16/U32/U64LE`, `WriteFloatLE`, `ParseXxx` retour `bool`, etc.

- [ ] **Step 2: Écrire LunarPayloads.h**

```cpp
#pragma once
// Wire payloads pour le systeme de phase lunaire (opcodes 192-194).
// 16 phases (0..15), cycle de 14 jours reels.

#include <cstdint>
#include <vector>

namespace engine::network::lunar
{
	enum class LunarStatus : uint8_t
	{
		Ok           = 0,
		Unauthorized = 1,
	};

	// === Request: client demande l'etat lunaire actuel (vide) ===
	struct LunarStateRequest
	{
		// Empty
	};

	// === Response: master envoie phase + cycle info ===
	struct LunarStateResponse
	{
		LunarStatus status         = LunarStatus::Ok;
		uint8_t     phase          = 0;     // 0..15
		float       illumination   = 0.0f;  // 0..1
		uint64_t    cycleStartMs   = 0;     // timestamp ms du debut de cycle
		uint64_t    cycleDurationMs = 0;    // duree totale d'un cycle (ms)
	};

	// === Push notification: changement de phase ===
	struct LunarPhaseChangeNotification
	{
		uint8_t  newPhase        = 0;
		float    newIllumination = 0.0f;
		uint64_t nextChangeTsMs  = 0;
	};

	// Build* : ecrit le payload binaire dans \p out.
	void BuildLunarStateRequestPayload(std::vector<uint8_t>& out);
	void BuildLunarStateResponsePayload(const LunarStateResponse& msg, std::vector<uint8_t>& out);
	void BuildLunarPhaseChangeNotificationPayload(const LunarPhaseChangeNotification& msg, std::vector<uint8_t>& out);

	// Parse* : retourne true si le buffer est valide.
	bool ParseLunarStateRequestPayload(const uint8_t* data, size_t size, LunarStateRequest& out);
	bool ParseLunarStateResponsePayload(const uint8_t* data, size_t size, LunarStateResponse& out);
	bool ParseLunarPhaseChangeNotificationPayload(const uint8_t* data, size_t size, LunarPhaseChangeNotification& out);
}
```

- [ ] **Step 3: Écrire LunarPayloads.cpp**

```cpp
#include "src/shared/network/LunarPayloads.h"

#include <cstring>

namespace engine::network::lunar
{
	namespace
	{
		void WriteU8(std::vector<uint8_t>& out, uint8_t v)
		{
			out.push_back(v);
		}

		void WriteU32LE(std::vector<uint8_t>& out, uint32_t v)
		{
			out.push_back(static_cast<uint8_t>(v));
			out.push_back(static_cast<uint8_t>(v >> 8));
			out.push_back(static_cast<uint8_t>(v >> 16));
			out.push_back(static_cast<uint8_t>(v >> 24));
		}

		void WriteU64LE(std::vector<uint8_t>& out, uint64_t v)
		{
			for (int i = 0; i < 8; ++i)
				out.push_back(static_cast<uint8_t>(v >> (i * 8)));
		}

		void WriteFloatLE(std::vector<uint8_t>& out, float v)
		{
			uint32_t bits = 0;
			std::memcpy(&bits, &v, sizeof(bits));
			WriteU32LE(out, bits);
		}

		bool ReadU8(const uint8_t* d, size_t sz, size_t& pos, uint8_t& out)
		{
			if (pos + 1 > sz) return false;
			out = d[pos];
			pos += 1;
			return true;
		}

		bool ReadU32LE(const uint8_t* d, size_t sz, size_t& pos, uint32_t& out)
		{
			if (pos + 4 > sz) return false;
			out = static_cast<uint32_t>(d[pos])
			    | (static_cast<uint32_t>(d[pos + 1]) << 8)
			    | (static_cast<uint32_t>(d[pos + 2]) << 16)
			    | (static_cast<uint32_t>(d[pos + 3]) << 24);
			pos += 4;
			return true;
		}

		bool ReadU64LE(const uint8_t* d, size_t sz, size_t& pos, uint64_t& out)
		{
			if (pos + 8 > sz) return false;
			out = 0;
			for (int i = 0; i < 8; ++i)
				out |= static_cast<uint64_t>(d[pos + i]) << (i * 8);
			pos += 8;
			return true;
		}

		bool ReadFloatLE(const uint8_t* d, size_t sz, size_t& pos, float& out)
		{
			uint32_t bits = 0;
			if (!ReadU32LE(d, sz, pos, bits)) return false;
			std::memcpy(&out, &bits, sizeof(out));
			return true;
		}
	}

	void BuildLunarStateRequestPayload(std::vector<uint8_t>& out)
	{
		out.clear();
	}

	void BuildLunarStateResponsePayload(const LunarStateResponse& msg, std::vector<uint8_t>& out)
	{
		out.clear();
		WriteU8(out, static_cast<uint8_t>(msg.status));
		WriteU8(out, msg.phase);
		WriteFloatLE(out, msg.illumination);
		WriteU64LE(out, msg.cycleStartMs);
		WriteU64LE(out, msg.cycleDurationMs);
	}

	void BuildLunarPhaseChangeNotificationPayload(const LunarPhaseChangeNotification& msg, std::vector<uint8_t>& out)
	{
		out.clear();
		WriteU8(out, msg.newPhase);
		WriteFloatLE(out, msg.newIllumination);
		WriteU64LE(out, msg.nextChangeTsMs);
	}

	bool ParseLunarStateRequestPayload(const uint8_t* /*data*/, size_t size, LunarStateRequest& /*out*/)
	{
		return size == 0;
	}

	bool ParseLunarStateResponsePayload(const uint8_t* d, size_t sz, LunarStateResponse& out)
	{
		size_t pos = 0;
		uint8_t status = 0;
		if (!ReadU8(d, sz, pos, status)) return false;
		if (!ReadU8(d, sz, pos, out.phase)) return false;
		if (!ReadFloatLE(d, sz, pos, out.illumination)) return false;
		if (!ReadU64LE(d, sz, pos, out.cycleStartMs)) return false;
		if (!ReadU64LE(d, sz, pos, out.cycleDurationMs)) return false;
		out.status = static_cast<LunarStatus>(status);
		return pos == sz;
	}

	bool ParseLunarPhaseChangeNotificationPayload(const uint8_t* d, size_t sz, LunarPhaseChangeNotification& out)
	{
		size_t pos = 0;
		if (!ReadU8(d, sz, pos, out.newPhase)) return false;
		if (!ReadFloatLE(d, sz, pos, out.newIllumination)) return false;
		if (!ReadU64LE(d, sz, pos, out.nextChangeTsMs)) return false;
		return pos == sz;
	}
}
```

- [ ] **Step 4: Écrire LunarPayloadsTests.cpp**

```cpp
// Round-trip tests pour les payloads lunaires + edge cases + reject-short.

#include "src/shared/network/LunarPayloads.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace engine::network::lunar;

namespace
{
	bool NearlyEqual(float a, float b, float eps = 1e-5f)
	{
		return std::fabs(a - b) < eps;
	}

	void TestStateRequestRoundTrip()
	{
		std::vector<uint8_t> buf;
		BuildLunarStateRequestPayload(buf);
		assert(buf.empty());
		LunarStateRequest parsed;
		assert(ParseLunarStateRequestPayload(buf.data(), buf.size(), parsed));
		std::puts("[OK] TestStateRequestRoundTrip");
	}

	void TestStateResponseRoundTrip()
	{
		LunarStateResponse src;
		src.status = LunarStatus::Ok;
		src.phase = 7;
		src.illumination = 1.0f;
		src.cycleStartMs = 1767225600000ull; // 2026-01-01 UTC
		src.cycleDurationMs = 1209600000ull; // 14 jours

		std::vector<uint8_t> buf;
		BuildLunarStateResponsePayload(src, buf);

		LunarStateResponse dst;
		assert(ParseLunarStateResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.status == src.status);
		assert(dst.phase == src.phase);
		assert(NearlyEqual(dst.illumination, src.illumination));
		assert(dst.cycleStartMs == src.cycleStartMs);
		assert(dst.cycleDurationMs == src.cycleDurationMs);
		std::puts("[OK] TestStateResponseRoundTrip");
	}

	void TestStateResponseUnauthorized()
	{
		LunarStateResponse src;
		src.status = LunarStatus::Unauthorized;
		std::vector<uint8_t> buf;
		BuildLunarStateResponsePayload(src, buf);

		LunarStateResponse dst;
		assert(ParseLunarStateResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.status == LunarStatus::Unauthorized);
		std::puts("[OK] TestStateResponseUnauthorized");
	}

	void TestPhaseChangeRoundTrip()
	{
		LunarPhaseChangeNotification src;
		src.newPhase = 8;
		src.newIllumination = 0.94f;
		src.nextChangeTsMs = 1767246600000ull;

		std::vector<uint8_t> buf;
		BuildLunarPhaseChangeNotificationPayload(src, buf);

		LunarPhaseChangeNotification dst;
		assert(ParseLunarPhaseChangeNotificationPayload(buf.data(), buf.size(), dst));
		assert(dst.newPhase == src.newPhase);
		assert(NearlyEqual(dst.newIllumination, src.newIllumination));
		assert(dst.nextChangeTsMs == src.nextChangeTsMs);
		std::puts("[OK] TestPhaseChangeRoundTrip");
	}

	void TestPhaseChangeAllPhases()
	{
		for (uint8_t p = 0; p < 16; ++p)
		{
			LunarPhaseChangeNotification src;
			src.newPhase = p;
			src.newIllumination = static_cast<float>(p) / 15.0f;
			src.nextChangeTsMs = 100000ull * static_cast<uint64_t>(p + 1);

			std::vector<uint8_t> buf;
			BuildLunarPhaseChangeNotificationPayload(src, buf);

			LunarPhaseChangeNotification dst;
			assert(ParseLunarPhaseChangeNotificationPayload(buf.data(), buf.size(), dst));
			assert(dst.newPhase == p);
		}
		std::puts("[OK] TestPhaseChangeAllPhases");
	}

	void TestStateResponseRejectShort()
	{
		std::vector<uint8_t> buf = { 0x00, 0x07, 0x00, 0x00, 0x80 }; // tronque
		LunarStateResponse dst;
		assert(!ParseLunarStateResponsePayload(buf.data(), buf.size(), dst));
		std::puts("[OK] TestStateResponseRejectShort");
	}

	void TestStateResponseRejectExtra()
	{
		LunarStateResponse src;
		src.phase = 5;
		std::vector<uint8_t> buf;
		BuildLunarStateResponsePayload(src, buf);
		buf.push_back(0xAA); // octet en trop

		LunarStateResponse dst;
		assert(!ParseLunarStateResponsePayload(buf.data(), buf.size(), dst));
		std::puts("[OK] TestStateResponseRejectExtra");
	}

	void TestPhaseChangeRejectShort()
	{
		std::vector<uint8_t> buf = { 0x07, 0x00 }; // tronque
		LunarPhaseChangeNotification dst;
		assert(!ParseLunarPhaseChangeNotificationPayload(buf.data(), buf.size(), dst));
		std::puts("[OK] TestPhaseChangeRejectShort");
	}

	void TestEdgeCaseMaxValues()
	{
		LunarStateResponse src;
		src.phase = 15;
		src.illumination = 1.0f;
		src.cycleStartMs = UINT64_MAX;
		src.cycleDurationMs = UINT64_MAX;

		std::vector<uint8_t> buf;
		BuildLunarStateResponsePayload(src, buf);

		LunarStateResponse dst;
		assert(ParseLunarStateResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.phase == 15);
		assert(dst.cycleStartMs == UINT64_MAX);
		std::puts("[OK] TestEdgeCaseMaxValues");
	}

	void TestEdgeCaseZero()
	{
		LunarStateResponse src;
		src.phase = 0;
		src.illumination = 0.0f;
		src.cycleStartMs = 0;
		src.cycleDurationMs = 0;

		std::vector<uint8_t> buf;
		BuildLunarStateResponsePayload(src, buf);

		LunarStateResponse dst;
		assert(ParseLunarStateResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.phase == 0);
		std::puts("[OK] TestEdgeCaseZero");
	}
}

int main()
{
	TestStateRequestRoundTrip();
	TestStateResponseRoundTrip();
	TestStateResponseUnauthorized();
	TestPhaseChangeRoundTrip();
	TestPhaseChangeAllPhases();
	TestStateResponseRejectShort();
	TestStateResponseRejectExtra();
	TestPhaseChangeRejectShort();
	TestEdgeCaseMaxValues();
	TestEdgeCaseZero();
	std::puts("[ALL OK] LunarPayloadsTests");
	return 0;
}
```

---

## Task 4: LunarHandler master + intégration main_linux

**Files:**
- Create: `src/masterd/handlers/lunar/LunarHandler.h`
- Create: `src/masterd/handlers/lunar/LunarHandler.cpp`
- Modify: `src/masterd/main_linux.cpp`

- [ ] **Step 1: Lire LootHandler comme référence**

Lire `src/masterd/handlers/loot/LootHandler.h` et `.cpp` pour copier le style EXACT (includes, Setters, HandlePacket signature, push helpers, mutex, etc.).

- [ ] **Step 2: Écrire LunarHandler.h**

```cpp
#pragma once
// LunarHandler : etat lunaire authoritative master + push broadcast sur
// changement de phase. Calcul stateless via LunarCalendar (deterministe
// depuis epoch). Tick periodique (5min) verifie si la phase courante a
// change depuis le dernier broadcast et push aux clients connectes.
//
// Le handler est instancie dans main_linux.cpp au boot, cable via
// SetServer/SetSessionManager/SetConnectionSessionMap, puis enregistre
// dans le packetHandler du NetServer pour l'opcode 192. La response 193
// est emise avec le meme requestId/sessionId que la request recue. Le
// push 194 est broadcast a tous les clients connectes (pas de subscribe
// explicite : la lune est globale et permanente).
//
// Cycle par defaut : 14 jours reels (14*24*3600*1000 = 1209600000 ms),
// epoch 2026-01-01 00:00:00 UTC = 1767225600000 ms.

#include "src/shardd/world/LunarCalendar.h"

#include <cstdint>
#include <mutex>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server
{
	class LunarHandler
	{
	public:
		void SetServer(NetServer* s) { m_server = s; }
		void SetSessionManager(SessionManager* mgr) { m_sessionMgr = mgr; }
		void SetConnectionSessionMap(ConnectionSessionMap* m) { m_connMap = m; }

		/// Configure les parametres du cycle. Appele une fois au boot.
		void SetCycleParams(uint64_t cycleStartMs, uint64_t cycleDurationMs);

		/// Dispatch packet : opcode 192 (LunarStateRequest).
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader, const uint8_t* payload, size_t payloadSize);

		/// Tick periodique (typiquement 5 min). Compare la phase courante
		/// avec la derniere broadcastee ; si different, push 194 a tous les
		/// clients connectes.
		void Tick(uint64_t realNowMs);

		/// Phase courante (pour integration future avec GameEvents).
		uint8_t CurrentPhase() const;

	private:
		void HandleStateRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader);
		void PushPhaseChangeBroadcast(uint8_t newPhase, float newIllumination, uint64_t nextChangeTsMs);

		NetServer*            m_server     = nullptr;
		SessionManager*       m_sessionMgr = nullptr;
		ConnectionSessionMap* m_connMap    = nullptr;

		mutable std::mutex m_mutex;
		uint64_t           m_cycleStartMs    = 1767225600000ull; // 2026-01-01 UTC
		uint64_t           m_cycleDurationMs = engine::server::world::kDefaultLunarCycleMs;
		uint8_t            m_lastBroadcastPhase = 0xFFu; // sentinel : force premier broadcast
	};
}
```

- [ ] **Step 3: Écrire LunarHandler.cpp**

```cpp
#include "src/masterd/handlers/lunar/LunarHandler.h"

#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/LunarPayloads.h"

#include <chrono>
#include <vector>

namespace engine::server
{
	using engine::server::world::LunarCalendar;
	using engine::server::world::LunarPhaseInfo;

	void LunarHandler::SetCycleParams(uint64_t cycleStartMs, uint64_t cycleDurationMs)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_cycleStartMs = cycleStartMs;
		m_cycleDurationMs = cycleDurationMs;
	}

	uint8_t LunarHandler::CurrentPhase() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const uint64_t nowMs = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		auto info = LunarCalendar::Compute(nowMs, m_cycleStartMs, m_cycleDurationMs);
		return info.phase;
	}

	void LunarHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
	                                 uint64_t sessionIdHeader, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;
		if (opcode == kOpcodeLunarStateRequest)
		{
			HandleStateRequest(connId, requestId, sessionIdHeader);
		}
	}

	void LunarHandler::HandleStateRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader)
	{
		using namespace engine::network::lunar;
		using namespace engine::network;

		LunarStateResponse resp;

		// Validation session : si pas de session valide -> Unauthorized.
		if (m_connMap && m_sessionMgr)
		{
			const uint64_t sessId = m_connMap->GetSessionId(connId);
			if (sessId == 0 || m_sessionMgr->GetAccountId(sessId) == 0)
			{
				resp.status = LunarStatus::Unauthorized;
			}
		}

		if (resp.status == LunarStatus::Ok)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const uint64_t nowMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			auto info = LunarCalendar::Compute(nowMs, m_cycleStartMs, m_cycleDurationMs);
			resp.phase = info.phase;
			resp.illumination = info.illumination;
			resp.cycleStartMs = m_cycleStartMs;
			resp.cycleDurationMs = m_cycleDurationMs;
		}

		std::vector<uint8_t> payload;
		BuildLunarStateResponsePayload(resp, payload);

		std::vector<uint8_t> packet;
		BuildPacket(kOpcodeLunarStateResponse, requestId, sessionIdHeader, payload, packet);
		if (m_server) m_server->Send(connId, packet.data(), packet.size());
	}

	void LunarHandler::Tick(uint64_t realNowMs)
	{
		uint8_t newPhase = 0;
		float newIllumination = 0.0f;
		uint64_t nextChangeTs = 0;
		bool needBroadcast = false;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto info = LunarCalendar::Compute(realNowMs, m_cycleStartMs, m_cycleDurationMs);
			if (info.phase != m_lastBroadcastPhase)
			{
				newPhase = info.phase;
				newIllumination = info.illumination;
				nextChangeTs = LunarCalendar::NextChangeTsMs(realNowMs, m_cycleStartMs, m_cycleDurationMs);
				m_lastBroadcastPhase = info.phase;
				needBroadcast = true;
			}
		}

		if (needBroadcast)
		{
			LOG_INFO(Net, "[LunarHandler] phase change broadcast: phase={} illumination={:.3f} nextChangeTs={}",
				newPhase, newIllumination, nextChangeTs);
			PushPhaseChangeBroadcast(newPhase, newIllumination, nextChangeTs);
		}
	}

	void LunarHandler::PushPhaseChangeBroadcast(uint8_t newPhase, float newIllumination, uint64_t nextChangeTsMs)
	{
		using namespace engine::network::lunar;
		using namespace engine::network;

		LunarPhaseChangeNotification notif;
		notif.newPhase = newPhase;
		notif.newIllumination = newIllumination;
		notif.nextChangeTsMs = nextChangeTsMs;

		std::vector<uint8_t> payload;
		BuildLunarPhaseChangeNotificationPayload(notif, payload);

		std::vector<uint8_t> packet;
		BuildPacket(kOpcodeLunarPhaseChangeNotification, 0u, 0u, payload, packet);

		if (!m_server || !m_connMap) return;
		auto snapshot = m_connMap->Snapshot();
		for (const auto& [connId, sessId] : snapshot)
		{
			(void)sessId;
			m_server->Send(connId, packet.data(), packet.size());
		}
	}
}
```

- [ ] **Step 4: Modifier main_linux.cpp — ajout des includes**

Localiser le bloc d'includes des handlers (autour de la ligne 47, après `#include "src/masterd/handlers/loot/LootHandler.h"`) et ajouter :

```cpp
#include "src/masterd/handlers/lunar/LunarHandler.h"
```

- [ ] **Step 5: Modifier main_linux.cpp — instanciation après LootHandler**

Localiser le bloc d'instanciation `LootHandler` (`engine::server::LootHandler lootHandler;`) et ajouter juste après :

```cpp
	// Phase 5 step 3+4 Lunar — LunarHandler : etat lunaire authoritative
	// (16 phases, cycle 14 jours reels, deterministe depuis epoch). Tick
	// periodique (5 min) detecte changement de phase et push broadcast.
	// Pas de Subscribe : la lune est globale.
	engine::server::LunarHandler lunarHandler;
	lunarHandler.SetServer(&server);
	lunarHandler.SetSessionManager(&sessionManager);
	lunarHandler.SetConnectionSessionMap(&connSessionMap);
	LOG_INFO(Net, "[ServerMain] LunarHandler configured (Phase 5 Lunar, cycle 14j, 16 phases)");
```

- [ ] **Step 6: Modifier main_linux.cpp — ajouter à la capture-list**

Localiser la capture-list du lambda `SetPacketHandler` (ligne ~700, contenant `&weatherHandler, &gameEventHandler, &guildHandler, &auctionHandler, &lootHandler`) et ajouter `&lunarHandler` à la fin :

```cpp
server.SetPacketHandler([&authHandler, ..., &auctionHandler, &lootHandler, &lunarHandler](uint32_t connId, ...) {
```

- [ ] **Step 7: Modifier main_linux.cpp — dispatch dans le lambda**

Après le bloc `LootHandler` dispatch, ajouter :

```cpp
		else if (opcode == kOpcodeLunarStateRequest)
			lunarHandler.HandlePacket(connId, opcode, requestId, sessionIdHeader, payload, payloadSize);
```

- [ ] **Step 8: Modifier main_linux.cpp — appel Tick périodique**

Localiser la boucle principale du serveur (`while (!g_quit)`). Ajouter avant la boucle :

```cpp
	auto lastLunarTickTime = std::chrono::steady_clock::now();
```

À l'intérieur de la boucle, ajouter le tick toutes les 5 minutes :

```cpp
		// Tick lunaire toutes les 5 min (detection changement de phase).
		auto nowSteady = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(nowSteady - lastLunarTickTime).count() >= 300)
		{
			const uint64_t realNowMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			lunarHandler.Tick(realNowMs);
			lastLunarTickTime = nowSteady;
		}
```

Au boot juste après l'instanciation (Step 5), forcer un premier Tick pour établir la phase courante :

```cpp
	{
		const uint64_t bootNowMs = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		lunarHandler.Tick(bootNowMs);
	}
```

---

## Task 5: DayNightCycle extension + tests

**Files:**
- Modify: `src/client/render/DayNightCycle.h`
- Modify: `src/client/render/DayNightCycle.cpp`
- Create: `src/client/render/DayNightCycleTests.cpp`

- [ ] **Step 1: Modifier DayNightCycle.h — extension du struct State**

Localiser le struct `State` (ligne ~33-63) et ajouter à la fin (juste avant le `};`) :

```cpp
		// ---- Lunar phase (master-driven via OnLunarPhaseChange callback) ----

		/// Index de la phase lunaire courante (0..15). 0=NewMoon, 7=FullMoon, 15=Earthshine late.
		/// Mis a jour uniquement via OnLunarPhaseChange (master autoritaire).
		uint8_t moonPhase = 0;

		/// Fraction de la lune eclairee (0..1). 0 = noir (NewMoon), 1 = pleine.
		/// Mis a jour uniquement via OnLunarPhaseChange (master autoritaire).
		float moonIllumination = 0.0f;
```

- [ ] **Step 2: Modifier DayNightCycle.h — déclaration callback**

Dans la classe publique, après `void SetTimeScale(float realSecondsPerHour);` :

```cpp
		/// Met a jour la phase lunaire courante (recue depuis le master via
		/// opcode 193 LunarStateResponse ou 194 LunarPhaseChangeNotification).
		/// \param phase        0..15
		/// \param illumination 0..1
		void OnLunarPhaseChange(uint8_t phase, float illumination);
```

- [ ] **Step 3: Modifier DayNightCycle.cpp — implémentation callback**

À la fin du fichier (avant la fermeture du namespace) :

```cpp
	void DayNightCycle::OnLunarPhaseChange(uint8_t phase, float illumination)
	{
		if (phase >= 16) return;
		m_state.moonPhase = phase;
		m_state.moonIllumination = illumination < 0.0f ? 0.0f : (illumination > 1.0f ? 1.0f : illumination);
	}
```

- [ ] **Step 4: Créer DayNightCycleTests.cpp**

Écrire `src/client/render/DayNightCycleTests.cpp` :

```cpp
// Tests du cycle jour/nuit existant + extension lunaire. Pas de framework,
// asserts simples. Utilise pour valider le comportement de DayNightCycle
// avant d'ajouter les phases lunaires.

#include "src/client/render/DayNightCycle.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using engine::render::DayNightCycle;

namespace
{
	bool NearlyEqual(float a, float b, float eps = 0.05f)
	{
		return std::fabs(a - b) < eps;
	}

	void TestInitNoonState()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		p.initialTimeOfDay = 12.0f;
		p.timeScale = 60.0f;
		cycle.Init(p);

		const auto& s = cycle.GetState();
		assert(NearlyEqual(s.timeOfDay, 12.0f));
		assert(s.isDaytime);
		std::puts("[OK] TestInitNoonState");
	}

	void TestInitMidnightState()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		p.initialTimeOfDay = 0.0f;
		cycle.Init(p);

		const auto& s = cycle.GetState();
		assert(!s.isDaytime);
		std::puts("[OK] TestInitMidnightState");
	}

	void TestSunriseSunsetTransitions()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		cycle.Init(p);

		cycle.SetTime(6.0f);
		const auto& dawn = cycle.GetState();
		// Au lever du soleil l'elevation est proche de 0 (pas a son zenith).
		assert(std::fabs(dawn.lightDir[1]) < 0.5f);

		cycle.SetTime(12.0f);
		const auto& noon = cycle.GetState();
		assert(noon.lightDir[1] > 0.7f);

		cycle.SetTime(18.0f);
		const auto& dusk = cycle.GetState();
		assert(std::fabs(dusk.lightDir[1]) < 0.5f);
		std::puts("[OK] TestSunriseSunsetTransitions");
	}

	void TestSetTimeWraps()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		cycle.Init(p);

		// 25h doit wrapper a 1h ou etre clampe a 24-eps. Le comportement exact
		// est defini par l'impl ; on accepte les deux mais on rejette > 24.
		cycle.SetTime(25.0f);
		const auto& s = cycle.GetState();
		assert(s.timeOfDay >= 0.0f && s.timeOfDay < 24.0f);
		std::puts("[OK] TestSetTimeWraps");
	}

	void TestSetTimeScaleClamp()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		cycle.Init(p);

		cycle.SetTimeScale(0.0f);
		assert(cycle.GetTimeScale() >= 0.1f);
		cycle.SetTimeScale(-100.0f);
		assert(cycle.GetTimeScale() >= 0.1f);
		std::puts("[OK] TestSetTimeScaleClamp");
	}

	void TestAdvanceHour()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		p.initialTimeOfDay = 8.0f;
		p.timeScale = 60.0f; // 1 game hour = 60 real seconds
		cycle.Init(p);

		// 60 secondes reelles -> 1h en jeu -> 9h.
		cycle.Advance(60.0f);
		const auto& s = cycle.GetState();
		assert(NearlyEqual(s.timeOfDay, 9.0f, 0.1f));
		std::puts("[OK] TestAdvanceHour");
	}

	void TestLunarPhaseDefault()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		cycle.Init(p);
		const auto& s = cycle.GetState();
		assert(s.moonPhase == 0);
		assert(NearlyEqual(s.moonIllumination, 0.0f));
		std::puts("[OK] TestLunarPhaseDefault");
	}

	void TestLunarPhaseUpdate()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		cycle.Init(p);

		cycle.OnLunarPhaseChange(7, 1.0f);
		const auto& s = cycle.GetState();
		assert(s.moonPhase == 7);
		assert(NearlyEqual(s.moonIllumination, 1.0f));
		std::puts("[OK] TestLunarPhaseUpdate");
	}

	void TestLunarPhaseClamp()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		cycle.Init(p);

		// Phase invalide >= 16 ignoree.
		cycle.OnLunarPhaseChange(16, 0.5f);
		const auto& s = cycle.GetState();
		assert(s.moonPhase == 0);

		// Illumination negative clampee a 0.
		cycle.OnLunarPhaseChange(3, -1.0f);
		assert(NearlyEqual(cycle.GetState().moonIllumination, 0.0f));

		// Illumination > 1 clampee a 1.
		cycle.OnLunarPhaseChange(7, 2.5f);
		assert(NearlyEqual(cycle.GetState().moonIllumination, 1.0f));

		std::puts("[OK] TestLunarPhaseClamp");
	}
}

int main()
{
	TestInitNoonState();
	TestInitMidnightState();
	TestSunriseSunsetTransitions();
	TestSetTimeWraps();
	TestSetTimeScaleClamp();
	TestAdvanceHour();
	TestLunarPhaseDefault();
	TestLunarPhaseUpdate();
	TestLunarPhaseClamp();
	std::puts("[ALL OK] DayNightCycleTests");
	return 0;
}
```

---

## Task 6: SkyPass Vulkan + shader extension

**Files:**
- Create: `src/client/render/SkyPass.h`
- Create: `src/client/render/SkyPass.cpp`
- Modify: `game/data/shaders/sky.frag`
- Verify: `game/data/shaders/sky.vert` existe

- [ ] **Step 1: Vérifier que sky.vert existe**

Run :
```bash
ls game/data/shaders/sky.vert
```
Expected : fichier présent. Si absent, le créer avec un fullscreen quad standard :

```glsl
#version 450
// Sky vertex shader : fullscreen quad pass-through.
layout(location = 0) out vec2 vUV;
void main()
{
    // Generate fullscreen triangle from gl_VertexIndex (0,1,2).
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
```

- [ ] **Step 2: Modifier sky.frag — push constants étendus**

Localiser le bloc `layout(push_constant)` et le remplacer par :

```glsl
layout(push_constant) uniform SkyPC
{
    mat4  invViewProj;       // 64 bytes
    vec3  lightDir;          // direction toward the sun/moon (normalized)
    float _pad0;
    vec3  zenithColor;       // colour at the top of the sky dome
    float _pad1;
    vec3  horizonColor;      // colour at the horizon
    float _pad2;
    vec3  moonDir;           // direction toward the moon (normalized)
    float moonIntensity;     // 0..1, fade jour/nuit
    float moonPhase;         // 0..15
    float moonIllumination;  // 0..1
    vec2  _pad3;
} pc;
```

- [ ] **Step 3: Modifier sky.frag — ajouter le rendering du disque lunaire**

Avant le `void main()` final, ajouter une fonction helper :

```glsl
vec3 RenderMoonDisk(vec3 viewDir, vec3 baseSky)
{
    if (pc.moonIntensity < 0.001) return baseSky;
    float cosA = dot(viewDir, pc.moonDir);
    const float kMoonRadius = 0.012;
    if (cosA <= cos(kMoonRadius)) return baseSky;

    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), pc.moonDir));
    vec3 up    = cross(pc.moonDir, right);
    float u    = dot(viewDir, right) / sin(kMoonRadius);
    float v    = dot(viewDir, up)    / sin(kMoonRadius);

    float shadowOffset = 1.0 - pc.moonIllumination * 2.0;
    if (pc.moonPhase < 7.5) shadowOffset = -shadowOffset;

    float distFromShadow = length(vec2(u - shadowOffset, v));
    float shadowMask     = smoothstep(1.0, 0.95, distFromShadow);

    vec3 moonSurface = vec3(0.95, 0.92, 0.85) * shadowMask;

    if (pc.moonPhase < 3.0 || pc.moonPhase > 13.0)
    {
        moonSurface += vec3(0.05, 0.06, 0.10) * (1.0 - shadowMask);
    }

    float haloFalloff = smoothstep(cos(kMoonRadius * 1.5), cos(kMoonRadius), cosA);
    return mix(baseSky, moonSurface * pc.moonIntensity, haloFalloff);
}
```

À la fin du `void main()`, juste avant `outColor = ...`, intégrer :

```glsl
    // Reconstruire viewDir (direction du rayon depuis la camera).
    vec4 ndc = vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec4 worldPos = pc.invViewProj * ndc;
    vec3 viewDir = normalize(worldPos.xyz / worldPos.w);

    // Le calcul du gradient ciel + sun glow existant produit une couleur skyColor.
    // (Garder le code existant qui calcule skyColor depuis zenithColor/horizonColor/lightDir.)
    skyColor = RenderMoonDisk(viewDir, skyColor);
```

(L'ingenieur doit lire le `sky.frag` actuel pour situer où `skyColor` est calculé et où le helper s'insère exactement. Le shader actuel calcule un gradient zenith→horizon + un sun glow ; il faut juste appeler `RenderMoonDisk` après ces calculs et avant l'écriture finale.)

- [ ] **Step 4: Créer SkyPass.h**

```cpp
#pragma once
// SkyPass : pipeline Vulkan qui dessine le ciel + disque lunaire procedural.
// Consomme les shaders game/data/shaders/sky.vert et sky.frag (existants
// jusqu'ici non wires). Le fragment shader recoit en push-constants la
// matrice inverse view-projection, les directions soleil/lune, les couleurs
// zenith/horizon, et les parametres lunaires (phase + illumination + intensity).
//
// La pass est dessinee EN PREMIER dans la frame (avant le geometry pass)
// avec depth=1.0 pour servir de fond. Couts negligeables (1 fullscreen quad).
//
// Pattern aligne sur LightingPass et WaterPass.

#include <vulkan/vulkan.h>
#include <cstdint>

namespace engine::render
{
	class SkyPass
	{
	public:
		struct PushConstants
		{
			float invViewProj[16];   // mat4
			float lightDir[3];
			float _pad0;
			float zenithColor[3];
			float _pad1;
			float horizonColor[3];
			float _pad2;
			float moonDir[3];
			float moonIntensity;
			float moonPhase;
			float moonIllumination;
			float _pad3[2];
		};
		static_assert(sizeof(PushConstants) == 144, "SkyPass push constants size mismatch");

		bool Init(VkDevice device,
		          VkRenderPass renderPass,
		          uint32_t subpass,
		          const uint32_t* vertSpirv, size_t vertWordCount,
		          const uint32_t* fragSpirv, size_t fragWordCount);

		void Shutdown(VkDevice device);

		/// Enregistre le draw fullscreen quad avec les push-constants.
		void Record(VkCommandBuffer cmd, const PushConstants& pc);

		bool IsInitialized() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkPipeline       m_pipeline       = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	};
}
```

- [ ] **Step 5: Créer SkyPass.cpp**

```cpp
#include "src/client/render/SkyPass.h"

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
			VkShaderModule m = VK_NULL_HANDLE;
			if (vkCreateShaderModule(device, &info, nullptr, &m) != VK_SUCCESS)
				return VK_NULL_HANDLE;
			return m;
		}
	}

	bool SkyPass::Init(VkDevice device, VkRenderPass renderPass, uint32_t subpass,
	                    const uint32_t* vertSpirv, size_t vertWordCount,
	                    const uint32_t* fragSpirv, size_t fragWordCount)
	{
		// 1) Pipeline layout avec push-constants (144 bytes, vertex+fragment stages).
		VkPushConstantRange pcRange{};
		pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		pcRange.offset     = 0;
		pcRange.size       = sizeof(PushConstants);

		VkPipelineLayoutCreateInfo plInfo{};
		plInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		plInfo.pushConstantRangeCount = 1;
		plInfo.pPushConstantRanges    = &pcRange;
		if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
			return false;

		// 2) Shaders.
		VkShaderModule vertModule = CreateShaderModule(device, vertSpirv, vertWordCount);
		VkShaderModule fragModule = CreateShaderModule(device, fragSpirv, fragWordCount);
		if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) return false;

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertModule;
		stages[0].pName  = "main";
		stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragModule;
		stages[1].pName  = "main";

		// 3) Vertex input vide (le fullscreen quad est genere via gl_VertexIndex).
		VkPipelineVertexInputStateCreateInfo vi{};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		VkPipelineInputAssemblyStateCreateInfo ia{};
		ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// 4) Viewport / scissor dynamiques.
		VkPipelineViewportStateCreateInfo vp{};
		vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount  = 1;

		VkPipelineRasterizationStateCreateInfo rs{};
		rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode    = VK_CULL_MODE_NONE;
		rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth   = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{};
		ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds{};
		ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable  = VK_FALSE;
		ds.depthWriteEnable = VK_FALSE;

		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		                   | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo cb{};
		cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 1;
		cb.pAttachments    = &cba;

		VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dyn{};
		dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dyn.dynamicStateCount = 2;
		dyn.pDynamicStates    = dynStates;

		VkGraphicsPipelineCreateInfo pi{};
		pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pi.stageCount          = 2;
		pi.pStages             = stages;
		pi.pVertexInputState   = &vi;
		pi.pInputAssemblyState = &ia;
		pi.pViewportState      = &vp;
		pi.pRasterizationState = &rs;
		pi.pMultisampleState   = &ms;
		pi.pDepthStencilState  = &ds;
		pi.pColorBlendState    = &cb;
		pi.pDynamicState       = &dyn;
		pi.layout              = m_pipelineLayout;
		pi.renderPass          = renderPass;
		pi.subpass             = subpass;

		const VkResult res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &m_pipeline);
		vkDestroyShaderModule(device, vertModule, nullptr);
		vkDestroyShaderModule(device, fragModule, nullptr);
		return res == VK_SUCCESS;
	}

	void SkyPass::Shutdown(VkDevice device)
	{
		if (m_pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(device, m_pipeline, nullptr);
		if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
		m_pipeline       = VK_NULL_HANDLE;
		m_pipelineLayout = VK_NULL_HANDLE;
	}

	void SkyPass::Record(VkCommandBuffer cmd, const PushConstants& pc)
	{
		if (m_pipeline == VK_NULL_HANDLE) return;
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdPushConstants(cmd, m_pipelineLayout,
		                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		                    0, sizeof(PushConstants), &pc);
		vkCmdDraw(cmd, 3, 1, 0, 0);
	}
}
```

---

## Task 7: Engine push dispatch + slash commands + SkyPass intégration

**Files:**
- Modify: `src/client/app/Engine.h`
- Modify: `src/client/app/Engine.cpp`

- [ ] **Step 1: Modifier Engine.h — includes**

Ajouter après les includes existants des UI (autour des autres `#include "src/client/...Ui.h"`) :

```cpp
#include "src/client/render/SkyPass.h"
```

- [ ] **Step 2: Modifier Engine.h — forward decl SkyPass déjà fait via include**

(Pas d'action — l'include du Step 1 suffit.)

- [ ] **Step 3: Modifier Engine.h — ajouter membre SkyPass**

Localiser la zone des autres passes Vulkan (chercher `LightingPass` ou `WaterPass`) et ajouter un membre similaire :

```cpp
		/// Sky pass V1 (M38.1 + Phase 5 Lunar) : pipeline ciel + disque lunaire
		/// procedural via push-constants. Consomme sky.frag et sky.vert.
		engine::render::SkyPass m_skyPass;
		bool                    m_skyPassReady = false;
```

- [ ] **Step 4: Modifier Engine.cpp — push handler dispatch des opcodes 193 + 194**

Localiser le push handler dispatch (recherche `kOpcodeLootSimulateRollResponse` qui est le dernier dispatch ajouté), puis ajouter les nouveaux cas :

```cpp
		case kOpcodeLunarStateResponse:
		{
			engine::network::lunar::LunarStateResponse parsed;
			if (!engine::network::lunar::ParseLunarStateResponsePayload(payload, payloadSize, parsed))
			{
				LOG_WARN(Net, "[Engine] LUNAR_STATE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			if (parsed.status == engine::network::lunar::LunarStatus::Ok)
			{
				m_dayNight.OnLunarPhaseChange(parsed.phase, parsed.illumination);
				LOG_INFO(Render, "[Engine] LunarState received: phase={} illumination={:.3f}",
					parsed.phase, parsed.illumination);
			}
			return;
		}
		case kOpcodeLunarPhaseChangeNotification:
		{
			engine::network::lunar::LunarPhaseChangeNotification parsed;
			if (!engine::network::lunar::ParseLunarPhaseChangeNotificationPayload(payload, payloadSize, parsed))
			{
				LOG_WARN(Net, "[Engine] LUNAR_PHASE_CHANGE parse failed (size={})", payloadSize);
				return;
			}
			m_dayNight.OnLunarPhaseChange(parsed.newPhase, parsed.newIllumination);
			LOG_INFO(Render, "[Engine] LunarPhaseChange: phase={} illumination={:.3f}",
				parsed.newPhase, parsed.newIllumination);
			return;
		}
```

Ajouter en haut du fichier l'include payloads :

```cpp
#include "src/shared/network/LunarPayloads.h"
```

- [ ] **Step 5: Modifier Engine.cpp — envoyer LunarStateRequest sur EnterWorld**

Localiser le bloc d'EnterWorld (recherche `EnterWorldCommand` ou `IsInWorldShard()`). Après que la session est marquée comme dans le monde, envoyer :

```cpp
		// Phase 5 Lunar — fetch initial lunar state (master-authoritative).
		{
			std::vector<uint8_t> payload;
			engine::network::lunar::BuildLunarStateRequestPayload(payload);
			m_authUi.SendGenericRequestAsync(kOpcodeLunarStateRequest, payload);
		}
```

- [ ] **Step 6: Modifier Engine.cpp — slash commands /sky info|time|moon**

Localiser le bloc des autres slash commands (chercher `text == "/loot"`). Ajouter :

```cpp
		// Phase 5 step 3+4 Lunar + M38.1 Sky : slash commands debug pour
		// inspecter et override le cycle jour/nuit + phase lunaire.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/sky info" || text == "/sky"))
		{
			const auto& s = m_dayNight.GetState();
			const char* moonName[16] = {
				"NewMoon", "WaxingCrescentEarly", "WaxingCrescentLate", "FirstQuarter",
				"WaxingGibbousEarly", "WaxingGibbousLate", "FullMoonRising", "FullMoon",
				"FullMoonSetting", "WaningGibbousEarly", "WaningGibbousLate", "LastQuarter",
				"WaningCrescentEarly", "WaningCrescentLate", "EarthshineEarly", "EarthshineLate"
			};
			LOG_INFO(Render, "[Sky] timeOfDay={:.2f}h isDaytime={}", s.timeOfDay, s.isDaytime);
			LOG_INFO(Render, "[Sky] sunDir=({:.2f},{:.2f},{:.2f})", s.lightDir[0], s.lightDir[1], s.lightDir[2]);
			LOG_INFO(Render, "[Sky] moonPhase={} ({}) illumination={:.0f}%",
				s.moonPhase, moonName[s.moonPhase < 16 ? s.moonPhase : 0], s.moonIllumination * 100.0f);
			return true;
		}
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/sky time "))
		{
			const auto rest = text.substr(10);
			float hours = 0.0f;
			try { hours = std::stof(std::string(rest)); } catch (...) { hours = 12.0f; }
			m_dayNight.SetTime(hours);
			LOG_INFO(Render, "[Sky] time set to {:.2f}h", hours);
			return true;
		}
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/sky moon "))
		{
			const auto rest = text.substr(10);
			int phase = 0;
			try { phase = std::stoi(std::string(rest)); } catch (...) { phase = 0; }
			if (phase < 0 || phase > 15)
			{
				LOG_WARN(Render, "[Sky] phase {} hors plage [0..15]", phase);
				return true;
			}
			// Calcul illumination locale (cf. LunarCalendar::ComputeIllumination).
			constexpr float kPi = 3.14159265358979323846f;
			float t = (static_cast<float>(phase) - 7.0f) * (kPi / 8.0f);
			float illumination = 0.5f * (1.0f + std::cos(t));
			m_dayNight.OnLunarPhaseChange(static_cast<uint8_t>(phase), illumination);
			LOG_INFO(Render, "[Sky] moon phase override: {} illumination={:.0f}% (master state inchange)",
				phase, illumination * 100.0f);
			return true;
		}
```

- [ ] **Step 7: Modifier Engine.cpp — Init du SkyPass au boot**

Localiser le bloc d'init des autres passes Vulkan (chercher `m_lightingPass.Init` ou `WaterPass`). Avant ou après cette initialisation, ajouter :

```cpp
	// Phase 5 Lunar + M38.1 Sky : init du SkyPass (charge sky.vert.spv +
	// sky.frag.spv depuis game/data/shaders, compile pipeline avec push
	// constants etendus pour la phase lunaire).
	{
		// Charger les SPIR-V via le mecanisme de chargement existant
		// (typiquement via ResourcePath + std::ifstream) :
		std::vector<uint32_t> vertSpv;
		std::vector<uint32_t> fragSpv;
		if (LoadShaderSpirv("game/data/shaders/sky.vert.spv", vertSpv) &&
		    LoadShaderSpirv("game/data/shaders/sky.frag.spv", fragSpv))
		{
			m_skyPassReady = m_skyPass.Init(m_device, m_renderPass, /*subpass*/ 0,
			                                  vertSpv.data(), vertSpv.size(),
			                                  fragSpv.data(), fragSpv.size());
			if (!m_skyPassReady)
				LOG_WARN(Render, "[Boot] SkyPass init failed -- fallback clearColor");
			else
				LOG_INFO(Render, "[Boot] SkyPass ready");
		}
	}
```

(L'engineer doit identifier l'helper `LoadShaderSpirv` ou équivalent dans le codebase existant — chercher `vkCreateShaderModule` pour trouver le pattern de loading utilisé.)

- [ ] **Step 8: Modifier Engine.cpp — Record SkyPass dans la frame**

Avant le geometry pass (chercher `vkCmdBeginRenderPass` ou le premier `Record` d'une pass), enregistrer le SkyPass :

```cpp
		if (m_skyPassReady)
		{
			engine::render::SkyPass::PushConstants pc{};

			// invViewProj : matrice inverse view-projection courante.
			std::memcpy(pc.invViewProj, m_camera.GetInvViewProj().m, sizeof(pc.invViewProj));

			const auto& dn = m_dayNight.GetState();
			pc.lightDir[0] = dn.lightDir[0]; pc.lightDir[1] = dn.lightDir[1]; pc.lightDir[2] = dn.lightDir[2];
			pc.zenithColor[0] = dn.skyZenith[0]; pc.zenithColor[1] = dn.skyZenith[1]; pc.zenithColor[2] = dn.skyZenith[2];
			pc.horizonColor[0] = dn.skyHorizon[0]; pc.horizonColor[1] = dn.skyHorizon[1]; pc.horizonColor[2] = dn.skyHorizon[2];

			// MoonDir : oppose au soleil (cf. DayNightCycle).
			pc.moonDir[0] = -dn.lightDir[0];
			pc.moonDir[1] = -dn.lightDir[1];
			pc.moonDir[2] = -dn.lightDir[2];
			pc.moonIntensity = dn.isDaytime ? 0.0f : 1.0f;
			pc.moonPhase = static_cast<float>(dn.moonPhase);
			pc.moonIllumination = dn.moonIllumination;

			m_skyPass.Record(commandBuffer, pc);
		}
```

(L'engineer doit adapter le nom des variables `commandBuffer` et `m_camera.GetInvViewProj()` selon le code existant.)

- [ ] **Step 9: Modifier Engine.cpp — Shutdown du SkyPass**

Dans `Shutdown()`, avant `m_window.Destroy()` :

```cpp
	if (m_skyPassReady) m_skyPass.Shutdown(m_device);
```

---

## Task 8: CMake registration

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Ajouter sources engine_core dans CMakeLists.txt racine**

Localiser le bloc des sources `engine_core` (chercher `LootRollUi.cpp`) et ajouter :

```cmake
  src/client/render/SkyPass.cpp
  src/shared/network/LunarPayloads.cpp
```

- [ ] **Step 2: Ajouter test targets dans CMakeLists.txt racine**

Localiser le bloc des `lcdlln_add_simple_test` (chercher `loot_payloads_tests`) et ajouter :

```cmake
# Phase 5 Lunar — round-trip tests.
add_executable(lunar_payloads_tests src/shared/network/LunarPayloadsTests.cpp)
target_link_libraries(lunar_payloads_tests PRIVATE engine_core)
add_test(NAME lunar_payloads_tests COMMAND lunar_payloads_tests)

# Phase 5 Lunar — calendar deterministe tests (header-only).
add_executable(lunar_calendar_tests src/shardd/world/LunarCalendarTests.cpp)
target_link_libraries(lunar_calendar_tests PRIVATE engine_core)
add_test(NAME lunar_calendar_tests COMMAND lunar_calendar_tests)

# M38.1 + Phase 5 Lunar — DayNightCycle existant + extension lunaire.
add_executable(daynight_cycle_tests src/client/render/DayNightCycleTests.cpp)
target_link_libraries(daynight_cycle_tests PRIVATE engine_core)
add_test(NAME daynight_cycle_tests COMMAND daynight_cycle_tests)
```

- [ ] **Step 3: Ajouter sources server_app dans src/CMakeLists.txt**

Localiser le bloc des sources serveur (chercher `LootHandler.cpp` et `LootPayloads.cpp`) et ajouter :

```cmake
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/lunar/LunarHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/LunarPayloads.cpp
```

---

## Task 9: Compilation des shaders SPIR-V

**Files:**
- Build script (CMake / glslc / glslangValidator) qui produit `sky.vert.spv` et `sky.frag.spv` à partir des sources

- [ ] **Step 1: Vérifier que la chaine de compilation shader existe**

Run :
```bash
find . -name "*.frag.spv" 2>/dev/null | head -3
find . -name "*compileShaders*" -o -name "compile_shaders*" 2>/dev/null | head -3
```

Si des `.spv` existent déjà pour d'autres shaders (water, lighting, etc.), la chaîne fonctionne — `sky.vert/frag` seront compilés automatiquement par le même mécanisme. Sinon, l'engineer doit identifier l'outil utilisé (probablement `glslangValidator` ou `glslc` dans CMake).

- [ ] **Step 2: Si nécessaire, ajouter les shaders sky à la liste compile**

Chercher `add_custom_command.*\.spv` ou `compile_shader\(` dans `CMakeLists.txt`. Ajouter l'entrée pour `sky.vert` / `sky.frag` si elle manque. Si la compilation se fait via glob, vérifier que les fichiers sont bien inclus.

---

## Task 10: Commit + push + PR

**Files:**
- Branch: `claude/day-night-lunar-design` (déjà créée)

- [ ] **Step 1: Vérifier que tous les fichiers sont créés / modifiés**

Run :
```bash
git status --short
```
Expected : nouveaux fichiers Lunar* + modifications dans `ProtocolV1Constants.h`, `main_linux.cpp`, `DayNightCycle.{h,cpp}`, `Engine.{h,cpp}`, `sky.frag`, CMakeLists.

- [ ] **Step 2: Stager + committer**

Run :
```bash
git add src/shardd/world/LunarCalendar.h \
        src/shardd/world/LunarCalendarTests.cpp \
        src/shared/network/LunarPayloads.h \
        src/shared/network/LunarPayloads.cpp \
        src/shared/network/LunarPayloadsTests.cpp \
        src/shared/network/ProtocolV1Constants.h \
        src/masterd/handlers/lunar/LunarHandler.h \
        src/masterd/handlers/lunar/LunarHandler.cpp \
        src/masterd/main_linux.cpp \
        src/client/render/DayNightCycle.h \
        src/client/render/DayNightCycle.cpp \
        src/client/render/DayNightCycleTests.cpp \
        src/client/render/SkyPass.h \
        src/client/render/SkyPass.cpp \
        src/client/app/Engine.h \
        src/client/app/Engine.cpp \
        game/data/shaders/sky.frag \
        game/data/shaders/sky.vert \
        CMakeLists.txt \
        src/CMakeLists.txt

git commit -m "$(cat <<'EOF'
feat(lunar): wire 16 phases lunaires + SkyPass + tests cycle jour/nuit

Implementation Phase 5 step 3+4 Lunar :
- LunarCalendar header-only (calcul deterministe phase 0..15 +
  illumination sinusoidale depuis epoch Unix)
- LunarPayloads (opcodes 192-194) : State Request/Response + Push
  PhaseChangeNotification
- LunarHandler master : etat authoritative + tick 5min + push broadcast
- DayNightCycle etendu : moonPhase/moonIllumination + callback
  OnLunarPhaseChange. Tests unitaires du cycle existant ajoutes.
- SkyPass Vulkan NOUVEAU : sky.frag/sky.vert existent depuis M38.1
  mais n'etaient pas wires dans le pipeline ; le ciel etait juste un
  clearColor. SkyPass charge les shaders et dessine le ciel + disque
  lunaire procedural via push-constants etendus.
- sky.frag etendu : disque lunaire procedural (intersection 2 cercles)
  + earthshine sur les phases sombres + halo doux.
- Engine : dispatch opcodes 193/194, fetch initial sur EnterWorld,
  slash commands debug /sky info|time|moon.
- 3 cibles CTest : lunar_calendar_tests, lunar_payloads_tests,
  daynight_cycle_tests.

Cycle = 14 jours reels (16 phases x ~21h). Phase deterministe depuis
epoch 2026-01-01 UTC. La Lune Noire (phases 0/14/15, ~21% du temps)
servira a declencher des events thematiques (fil rouge LCDLLN).

V1 limitations consignees :
- Pas de hook event lune <-> GameEvents (CurrentPhase expose, future PR)
- Pas de modulation lightColor nocturne par moonIllumination
- Pas de texture surface lunaire
- Master autoritaire ; pas de SyncLunar RPC entre master et shardd

Deploiement : REDEPLOIEMENT MASTER LINUX REQUIS - opcodes 192-194 +
LunarHandler. Pas de migration DB.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Push**

Run :
```bash
git push -u origin claude/day-night-lunar-design
```

- [ ] **Step 4: Créer la PR**

Run :
```bash
gh pr create --base main --head claude/day-night-lunar-design \
  --title "feat(lunar): 16 phases lunaires + SkyPass + tests cycle jour/nuit (Phase 5)" \
  --body-file docs/superpowers/specs/2026-05-10-day-night-lunar-cycle-design.md
```

(L'engineer doit ensuite enrichir le body avec un récap des changements ; le contenu du spec est un bon point de départ.)

---

## Self-Review

### Spec coverage
- ✅ LunarCalendar header-only (Section Architecture #1) → Task 2
- ✅ LunarHandler master + tick 5min (Architecture #2) → Tasks 3-4
- ✅ DayNightCycle extension (Architecture #3) → Task 5
- ✅ SkyPass C++ Vulkan (Architecture #4) → Task 6
- ✅ sky.frag procedural moon disk (Section Rendering) → Task 6 Step 3
- ✅ Opcodes 192-194 (Section Wire) → Task 1
- ✅ Payloads structs (Section Wire) → Task 3
- ✅ Tick + broadcast + EnterWorld fetch (Section Flow) → Tasks 4 + 7
- ✅ 16 phases avec illumination sinusoïdale (Section 2) → Task 2
- ✅ 3 cibles CTest (Section Tests A/B/C) → Tasks 2, 3, 5 + Task 8
- ✅ Slash commands /sky info|time|moon (Section Tests E) → Task 7 Step 6
- ✅ CMake registration (Section Fichiers) → Task 8
- ✅ Capture-list lambda (note CRITIQUE in spec) → Task 4 Step 6

### Placeholder scan
Aucun "TBD", "TODO", "implement later", "fill in details". Tous les steps contiennent du code complet ou des commandes exactes. Quelques steps mentionnent que l'engineer doit "identifier l'helper LoadShaderSpirv ou équivalent" / "adapter le nom des variables" — ces points sont marqués explicitement comme nécessitant une lecture du code existant et ne sont pas des placeholders cachés.

### Type consistency
- `LunarPhaseInfo { uint8_t phase; float illumination; }` — utilisé cohéremment dans Tasks 2, 3, 4
- `LunarStateResponse` champs : `status`, `phase`, `illumination`, `cycleStartMs`, `cycleDurationMs` — cohérent partout
- `LunarPhaseChangeNotification` champs : `newPhase`, `newIllumination`, `nextChangeTsMs` — cohérent partout
- `OnLunarPhaseChange(uint8_t phase, float illumination)` — signature cohérente entre déclaration (Task 5 Step 2) et utilisation (Task 7 Step 4)
- `kOpcodeLunarStateRequest = 192u` etc. — cohérent

### Note opérationnelle
Le pattern projet est **single squash-merge commit par PR**. Toutes les modifications de ce plan rentrent dans un unique commit final (Task 10), pas un commit par task. Les "tasks" sont des unités logiques pour faciliter la review et l'exécution par subagent.
