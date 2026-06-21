# Anti-occlusion caméra — fondu tramé + clamp terrain — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Quand un prop ou un mur de bâtiment passe entre la caméra et le joueur, le
fondre (transparence tramée) pour garder le perso visible ; empêcher la caméra de
passer sous le terrain.

**Architecture:** Nouveau module math pur `CameraOcclusionFade` (testable hors Vulkan)
qui calcule un fondu lissé par objet. Le fondu est poussé au G-buffer via un champ
`fade` ajouté au push-constant partagé (réutilise un padding → taille 144 o inchangée),
et le fragment shader `gbuffer_geometry.frag` applique un screen-door dither. `Engine`
collecte les occulteurs depuis `m_props`, appelle le module chaque frame, transmet le
fondu par-prop à `GeometryPass::Record`, et clampe `camera.y` au-dessus du sol.

**Tech Stack:** C++17, Vulkan (deferred G-buffer), GLSL→SPIR-V (compilé par la CI),
`engine::core::Config`, ctest via `lcdlln_add_simple_test`. **100 % client.**

Spec de référence : [docs/superpowers/specs/2026-06-21-camera-occlusion-fade-design.md](2026-06-21-camera-occlusion-fade-design.md).

**Contraintes repo :** PascalCase pour nouveaux fichiers/classes ; commentaires en
français ; pas de toolchain locale (build/ctest via CI) ; les `.spv` ne sont pas
versionnés (la CI recompile le GLSL). Module **client-only** (pas d'ajout à server_app).

---

## File Structure

| Fichier | Rôle |
|---|---|
| `src/client/render/CameraOcclusionFade.h` (créer) | Interface du module : `OccluderSphere`, `Config`, `CameraOcclusionFade` |
| `src/client/render/CameraOcclusionFade.cpp` (créer) | Logique : cible de fondu par occulteur + lissage temporel + purge |
| `src/client/render/tests/CameraOcclusionFadeTests.cpp` (créer) | ctest math pur (9 cas) |
| `src/CMakeLists.txt` (modifier) | Enregistrer le test ; ajouter le .cpp au target client |
| `game/data/shaders/gbuffer_geometry.frag` (modifier) | Champ `fade` + screen-door dither |
| `src/client/render/GeometryPass.cpp` (modifier) | `padding0`→`fade` ; param `fade` de `Record` |
| `src/client/render/GeometryPass.h` (modifier) | Signature de `Record` (+ `float fade`) |
| `src/client/render/skinned/SkinnedRenderer.cpp` (modifier) | `fade=1.0f` (avatar jamais fondu) |
| `src/client/app/Engine.h` (modifier) | Membre `m_cameraOcclusionFade` ; champs occulteur dans `PropRenderable` |
| `src/client/app/Engine.cpp` (modifier) | Init module, collecte occulteurs, fondu par-prop, clamp terrain, config |

---

## Task 1 : Module `CameraOcclusionFade` (+ tests)

**Files:**
- Create: `src/client/render/CameraOcclusionFade.h`
- Create: `src/client/render/CameraOcclusionFade.cpp`
- Test: `src/client/render/tests/CameraOcclusionFadeTests.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1 : Écrire le header**

Créer `src/client/render/CameraOcclusionFade.h` :

```cpp
#pragma once

#include "src/shared/math/Math.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::render
{
	/// Sphère englobante d'un occulteur potentiel (prop, pièce de bâtiment).
	struct OccluderSphere
	{
		std::uint32_t id = 0;         ///< identifiant stable de l'objet (clé de suivi du fondu)
		engine::math::Vec3 center{};  ///< centre monde de la sphère
		float radius = 0.5f;          ///< rayon monde (m)
	};

	/// Calcule, par frame, un facteur de fondu (transparence tramée) pour chaque
	/// objet occultant la vue entre la caméra et le joueur, avec lissage temporel.
	/// Math pure (aucune dépendance Vulkan) → testable en ctest.
	class CameraOcclusionFade
	{
	public:
		struct Config
		{
			float fadeMin = 0.15f;            ///< opacité mini au cœur de l'occlusion (0 = invisible)
			float radiusMargin = 0.5f;        ///< marge ajoutée au rayon pour la transition (m)
			float fadeInPerSec = 6.0f;        ///< vitesse de retour vers l'opaque (1.0)
			float fadeOutPerSec = 8.0f;       ///< vitesse de passage vers fadeMin
			float playerProtectRadius = 0.6f; ///< occulteur à moins de ça du joueur : jamais fondu
		};

		/// Configure le module (réinitialise l'état de fondu).
		void Init(const Config& cfg);

		/// Met à jour les fondus. \p occluders est reconstruite chaque frame ;
		/// \p focusPoint = point regardé (tête joueur) ; \p dt en secondes.
		/// Effet de bord : met à jour la table interne id→fondu.
		void Update(const engine::math::Vec3& cameraPos,
			const engine::math::Vec3& focusPoint,
			const std::vector<OccluderSphere>& occluders,
			float dt);

		/// Fondu lissé courant d'un objet ; 1.0 (opaque) si l'id est inconnu.
		float FadeFor(std::uint32_t id) const;

	private:
		Config m_cfg{};
		std::unordered_map<std::uint32_t, float> m_fade; ///< id -> fondu lissé ∈ [fadeMin,1]
	};
}
```

- [ ] **Step 2 : Écrire les tests (ils ne compilent pas encore — pas de .cpp)**

Créer `src/client/render/tests/CameraOcclusionFadeTests.cpp` :

```cpp
#include "src/client/render/CameraOcclusionFade.h"

#include <cmath>
#include <cstdio>
#include <vector>

using engine::render::CameraOcclusionFade;
using engine::render::OccluderSphere;
using engine::math::Vec3;

namespace
{
	int g_fail = 0;
	void check(bool cond, const char* msg)
	{ if (!cond) { std::printf("FAIL: %s\n", msg); ++g_fail; } }

	CameraOcclusionFade::Config DefaultCfg()
	{
		CameraOcclusionFade::Config c{};
		c.fadeMin = 0.15f; c.radiusMargin = 0.5f;
		c.fadeInPerSec = 6.0f; c.fadeOutPerSec = 8.0f;
		c.playerProtectRadius = 0.6f;
		return c;
	}

	// Fait converger le lissage : N updates de dt fixe avec la même scène.
	float Converge(CameraOcclusionFade& f, const Vec3& cam, const Vec3& focus,
		const std::vector<OccluderSphere>& occ, std::uint32_t id, int frames, float dt)
	{
		for (int i = 0; i < frames; ++i) f.Update(cam, focus, occ, dt);
		return f.FadeFor(id);
	}
}

int main()
{
	const Vec3 cam{ 0, 0, 0 };
	const Vec3 focus{ 10, 0, 0 };

	// 1) Occulteur pile sur le segment, au centre -> converge vers fadeMin.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 1, Vec3{ 5, 0, 0 }, 0.5f } };
		const float v = Converge(f, cam, focus, occ, 1, 100, 0.1f);
		check(std::fabs(v - 0.15f) < 0.02f, "1: coeur d'occlusion -> fadeMin");
	}

	// 2) Occulteur derrière le joueur (proj > segLen) -> opaque.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 2, Vec3{ 12, 0, 0 }, 0.5f } };
		const float v = Converge(f, cam, focus, occ, 2, 100, 0.1f);
		check(std::fabs(v - 1.0f) < 1e-3f, "2: derriere le joueur -> opaque");
	}

	// 3) Occulteur derrière la caméra (proj < 0) -> opaque.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 3, Vec3{ -2, 0, 0 }, 0.5f } };
		const float v = Converge(f, cam, focus, occ, 3, 100, 0.1f);
		check(std::fabs(v - 1.0f) < 1e-3f, "3: derriere la camera -> opaque");
	}

	// 4) Occulteur latéral (d > radius+margin) -> opaque.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 4, Vec3{ 5, 0, 3 }, 0.5f } };
		const float v = Converge(f, cam, focus, occ, 4, 100, 0.1f);
		check(std::fabs(v - 1.0f) < 1e-3f, "4: lateral hors zone -> opaque");
	}

	// 5) Zone de transition : fade strictement entre fadeMin et 1.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		// d=0.75, r0=0.5, r1=1.0 -> target = 0.15 + 0.85*0.5 = 0.575.
		std::vector<OccluderSphere> occ{ { 5, Vec3{ 5, 0, 0.75f }, 0.5f } };
		const float v = Converge(f, cam, focus, occ, 5, 100, 0.1f);
		check(v > 0.16f && v < 0.99f, "5: transition -> fade intermediaire");
		check(std::fabs(v - 0.575f) < 0.03f, "5: transition -> valeur attendue ~0.575");
	}

	// 6) Garde joueur : occulteur collé au joueur -> jamais fondu.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 6, Vec3{ 9.8f, 0, 0 }, 0.5f } }; // |toFocus|=0.2<0.6
		const float v = Converge(f, cam, focus, occ, 6, 100, 0.1f);
		check(std::fabs(v - 1.0f) < 1e-3f, "6: garde joueur -> opaque");
	}

	// 7) Lissage : un seul petit dt ne saute pas directement à fadeMin.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 7, Vec3{ 5, 0, 0 }, 0.5f } };
		f.Update(cam, focus, occ, 0.01f); // 1 - 8*0.01 = 0.92
		const float v = f.FadeFor(7);
		check(v > 0.15f + 0.01f && v < 1.0f, "7: lissage progressif (pas de saut)");
	}

	// 8) Purge : occulteur présent puis absent -> revient à 1.0 et FadeFor=1.0.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		std::vector<OccluderSphere> occ{ { 8, Vec3{ 5, 0, 0 }, 0.5f } };
		Converge(f, cam, focus, occ, 8, 100, 0.1f); // converge à fadeMin
		const float back = Converge(f, cam, focus, {}, 8, 100, 0.1f); // plus d'occulteur
		check(std::fabs(back - 1.0f) < 1e-3f, "8: purge -> revient opaque");
	}

	// 9) Id inconnu -> 1.0.
	{
		CameraOcclusionFade f; f.Init(DefaultCfg());
		check(std::fabs(f.FadeFor(999) - 1.0f) < 1e-6f, "9: inconnu -> opaque");
	}

	if (g_fail == 0) std::printf("CameraOcclusionFadeTests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
```

- [ ] **Step 3 : Écrire l'implémentation**

Créer `src/client/render/CameraOcclusionFade.cpp` :

```cpp
#include "src/client/render/CameraOcclusionFade.h"

#include <algorithm>
#include <cmath>

namespace engine::render
{
	namespace
	{
		float Dot(const engine::math::Vec3& a, const engine::math::Vec3& b)
		{ return a.x * b.x + a.y * b.y + a.z * b.z; }

		float Length(const engine::math::Vec3& v)
		{ return std::sqrt(Dot(v, v)); }

		float Clamp01(float v)
		{ return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
	}

	void CameraOcclusionFade::Init(const Config& cfg)
	{
		m_cfg = cfg;
		m_fade.clear();
	}

	void CameraOcclusionFade::Update(const engine::math::Vec3& cameraPos,
		const engine::math::Vec3& focusPoint,
		const std::vector<OccluderSphere>& occluders,
		float dt)
	{
		if (dt < 0.0f) dt = 0.0f;

		const engine::math::Vec3 seg = focusPoint - cameraPos;
		const float segLen = Length(seg);
		const engine::math::Vec3 dir = (segLen > 1e-4f)
			? engine::math::Vec3{ seg.x / segLen, seg.y / segLen, seg.z / segLen }
			: engine::math::Vec3{ 0.0f, 0.0f, 1.0f };

		// Nouvelle table : on n'y garde que les ids encore "vivants" (vus cette
		// frame, ou en train de revenir à l'opaque). Évite la croissance mémoire.
		std::unordered_map<std::uint32_t, float> next;
		next.reserve(occluders.size());

		for (const auto& occ : occluders)
		{
			float target = 1.0f; // opaque par défaut

			// Garde joueur : occulteur collé au joueur -> jamais fondu.
			const float distToFocus = Length(occ.center - focusPoint);
			if (distToFocus >= m_cfg.playerProtectRadius)
			{
				// Projection sur le segment caméra->joueur.
				const engine::math::Vec3 toOcc = occ.center - cameraPos;
				const float proj = Dot(toOcc, dir);
				if (segLen > 1e-4f && proj > 0.0f && proj < segLen)
				{
					const engine::math::Vec3 closest{
						cameraPos.x + dir.x * proj,
						cameraPos.y + dir.y * proj,
						cameraPos.z + dir.z * proj };
					const float d = Length(occ.center - closest);
					const float r0 = occ.radius;
					const float r1 = occ.radius + m_cfg.radiusMargin;
					if (d <= r0)
						target = m_cfg.fadeMin;
					else if (d < r1)
						target = m_cfg.fadeMin + (1.0f - m_cfg.fadeMin) * ((d - r0) / (r1 - r0));
					// d >= r1 -> target reste 1.0
				}
			}

			float current = 1.0f;
			auto it = m_fade.find(occ.id);
			if (it != m_fade.end()) current = it->second;

			if (target < current)
				current = std::max(target, current - m_cfg.fadeOutPerSec * dt);
			else if (target > current)
				current = std::min(target, current + m_cfg.fadeInPerSec * dt);

			next[occ.id] = Clamp01(current);
		}

		// Ids non vus cette frame : on les ramène vers l'opaque ; une fois ~opaques
		// on les laisse tomber (purge) pour que FadeFor renvoie 1.0 par défaut.
		for (const auto& kv : m_fade)
		{
			if (next.find(kv.first) != next.end()) continue;
			const float current = std::min(1.0f, kv.second + m_cfg.fadeInPerSec * dt);
			if (current < 1.0f - 1e-3f)
				next[kv.first] = current;
		}

		m_fade.swap(next);
	}

	float CameraOcclusionFade::FadeFor(std::uint32_t id) const
	{
		auto it = m_fade.find(id);
		return (it != m_fade.end()) ? it->second : 1.0f;
	}
}
```

- [ ] **Step 4 : Enregistrer le test + ajouter le .cpp au client dans `src/CMakeLists.txt`**

Repérer la ligne existante qui enregistre `CompositeWorldColliderTests` (recherche
`composite_world_collider` ou `CompositeWorldColliderTests`) et, juste après, ajouter
le même style d'enregistrement :

```cmake
lcdlln_add_simple_test(camera_occlusion_fade_tests
    src/client/render/tests/CameraOcclusionFadeTests.cpp
    src/client/render/CameraOcclusionFade.cpp)
```

Puis ajouter `src/client/render/CameraOcclusionFade.cpp` à la **liste des sources du
target client** (là où figurent `src/client/render/GeometryPass.cpp` et les autres
`src/client/render/*.cpp`), pour que le jeu le linke.

- [ ] **Step 5 : Vérifier (CI) — les tests passent**

Pousser la branche ; sur la CI, le job `build-linux` exécute ctest. Attendu :
`camera_occlusion_fade_tests` → `CameraOcclusionFadeTests: OK`, 9/9 cas verts.
(Pas de toolchain locale : la validation des tests passe par la CI.)

- [ ] **Step 6 : Commit**

```bash
git add src/client/render/CameraOcclusionFade.h src/client/render/CameraOcclusionFade.cpp \
        src/client/render/tests/CameraOcclusionFadeTests.cpp src/CMakeLists.txt
git commit -m "feat(camera): module CameraOcclusionFade (fondu occlusion + tests)"
```

---

## Task 2 : Champ `fade` dans le push-constant + dither shader

**Files:**
- Modify: `game/data/shaders/gbuffer_geometry.frag`
- Modify: `src/client/render/GeometryPass.cpp:19-27` (struct) + `:46-53`/`:669`/`:789`/`:1061`
- Modify: `src/client/render/GeometryPass.h:46-53` (signature `Record`)
- Modify: `src/client/render/skinned/SkinnedRenderer.cpp:633-641` (struct) + sites de push

> ⚠️ Le fragment shader est **partagé** entre `GeometryPass` (props/bâtiments) et
> `SkinnedRenderer` (avatar). Le layout binaire du push-constant doit rester identique
> (144 o). On réutilise le **1er padding** (offset 132) comme `float fade`. Sans mise à
> jour de `SkinnedRenderer`, le slot vaudrait 0 → **avatar tramé/invisible** : c'est
> pourquoi l'étape SkinnedRenderer est obligatoire.

- [ ] **Step 1 : Shader — déclarer `fade` et ajouter le screen-door dither**

Dans `game/data/shaders/gbuffer_geometry.frag`, remplacer le bloc push_constant
(lignes 33-40) par (renomme `_pad0` en `fade`) :

```glsl
layout(push_constant) uniform PushConstants {
    mat4 prevViewProj;
    mat4 viewProj;
    uint materialIndex;
    float fade;     // 1.0 = opaque (anti-occlusion caméra)
    uint _pad1;
    uint _pad2;
} pc;
```

Puis, tout en haut de `main()` (juste après `void main() {`), avant la lecture du
matériau, ajouter :

```glsl
    // Anti-occlusion caméra : transparence tramée (screen-door). fade=1 -> rien.
    if (pc.fade < 0.999) {
        const float bayer[16] = float[16](
            0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
           12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
            3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
           15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0);
        ivec2 p = ivec2(gl_FragCoord.xy) & 3;
        if (pc.fade < bayer[p.y * 4 + p.x])
            discard;
    }
```

(Les `.spv` ne sont pas versionnés : la CI recompile via `compile_game_shaders.ps1`.)

- [ ] **Step 2 : `GeometryPass.cpp` — renommer le padding en `fade`**

Dans la struct `GeometryPushConstants` (lignes 19-27), remplacer `uint32_t padding0 = 0;`
par `float fade = 1.0f;` :

```cpp
		struct GeometryPushConstants
		{
			float prevViewProj[16]{};
			float viewProj[16]{};
			uint32_t materialIndex = 0;
			float    fade = 1.0f;   // 1.0 = opaque (anti-occlusion caméra)
			uint32_t padding1 = 0;
			uint32_t padding2 = 0;
		};
```

(`kPushConstantSize` reste `144u`. Les sites `RecordInstanced`/`RecordIndirect`
construisent `GeometryPushConstants pushConstants{};` → `fade` vaut 1.0 par défaut,
comportement inchangé.)

- [ ] **Step 3 : `GeometryPass.h` — ajouter `float fade` à `Record`**

Dans la signature de `Record` (lignes 46-53), ajouter un paramètre `float fade = 1.0f`
juste après `uint32_t materialIndex = 0,` :

```cpp
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
		            ResourceId idA, ResourceId idB, ResourceId idC, ResourceId idVelocity, ResourceId idDepth,
		            const float* prevViewProjMat4, const float* viewProjMat4, const MeshAsset* mesh,
		            uint32_t lodLevel = 0,
		            VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE,
		            const float* instanceMatrix = nullptr,
		            uint32_t materialIndex = 0,
		            float fade = 1.0f,
		            bool loadExistingGbuffer = false);
```

- [ ] **Step 4 : `GeometryPass.cpp` — propager `fade` dans `Record`**

Mettre à jour la **définition** de `Record` pour accepter le même paramètre
`float fade = 1.0f` (même position, juste avant `bool loadExistingGbuffer`), puis dans
le corps de `Record` (au site push-constant ~ligne 669) ajouter après
`pushConstants.materialIndex = materialIndex;` :

```cpp
		pushConstants.fade = fade;
```

(Ne pas toucher `RecordInstanced`/`RecordIndirect` : `fade` y reste 1.0 par défaut.)

- [ ] **Step 5 : `SkinnedRenderer.cpp` — forcer `fade=1.0f` (avatar jamais fondu)**

Dans la struct locale `PushConstants` (lignes 633-641), remplacer le 1er champ de
padding (celui à l'offset 132, juste après `uint32_t materialIndex;`) par
`float fade;`, en conservant `static_assert(sizeof(PushConstants) == 144, ...)`. Par
ex. :

```cpp
	struct PushConstants {
		float prevViewProj[16];
		float viewProj[16];
		uint32_t materialIndex;
		float    fade;      // toujours 1.0 : l'avatar n'est jamais fondu
		uint32_t _pad1;
		uint32_t _pad2;
	};
	static_assert(sizeof(PushConstants) == 144, "PushConstants must match shader layout");
```

Puis, là où `pc` est initialisé (avant les `vkCmdPushConstants`, sites ~679 et ~688),
poser explicitement `pc.fade = 1.0f;` (une fois après la construction de `pc` suffit si
`pc` est réutilisé ; sinon le poser à chaque site). Vérifier que `pc` est bien
zéro-initialisé ailleurs et que `fade` n'est jamais laissé à 0.

> Si d'autres champs de la struct portent déjà des noms (`prevViewProj`/`viewProj`),
> garder l'ordre exact existant ; seul le **1er padding** devient `fade`. L'important :
> offset 132 = `fade` = 1.0f, identique au layout de `GeometryPushConstants` et du shader.

- [ ] **Step 6 : Vérifier (CI) — build + compilation shader OK**

Pousser ; la CI doit : compiler le GLSL (`gbuffer_geometry.frag` → `.spv`) sans erreur,
et builder client. Pas de test unitaire ici (chemin GPU) — validation visuelle en
Task 3.

- [ ] **Step 7 : Commit**

```bash
git add game/data/shaders/gbuffer_geometry.frag src/client/render/GeometryPass.h \
        src/client/render/GeometryPass.cpp src/client/render/skinned/SkinnedRenderer.cpp
git commit -m "feat(render): push-constant fade + screen-door dither (avatar opaque)"
```

---

## Task 3 : Intégration `Engine` — occulteurs, fondu par-prop, clamp terrain

**Files:**
- Modify: `src/client/app/Engine.h:889-910` (PropRenderable + membre module)
- Modify: `src/client/app/Engine.cpp` : bake props (~13248 `BuildPropFromMesh`,
  ~14019 `BuildPropFromMeshMatrix`), boucle Update (~9895), `RecordPropsGeometry`
  (~14317), init (où les autres systèmes s'initialisent)

- [ ] **Step 1 : `Engine.h` — champs occulteur + membre module**

Dans `struct PropRenderable` (lignes 889-907), ajouter :

```cpp
		engine::math::Vec3 occluderCenter{ 0.0f, 0.0f, 0.0f }; ///< centre monde sphère englobante (anti-occlusion)
		float occluderRadius = 0.0f;                            ///< rayon monde ; 0 = non occulteur
```

Ajouter l'include en tête de `Engine.h` (près des autres render includes) :

```cpp
#include "src/client/render/CameraOcclusionFade.h"
```

Et un membre (près de `m_orbitalCameraController`, ligne ~1537) :

```cpp
		engine::render::CameraOcclusionFade m_cameraOcclusionFade;
```

- [ ] **Step 2 : Calculer la sphère englobante au bake des props**

Dans `BuildPropFromMesh` (~13248) **et** `BuildPropFromMeshMatrix` (~14019), repérer la
boucle qui calcule les positions de sommets **en espace monde** (les sommets « cuits »).
Y suivre min/max, et après la boucle, renseigner le prop :

```cpp
		// (avant la boucle de bake des sommets)
		engine::math::Vec3 bbMin{ 1e30f, 1e30f, 1e30f };
		engine::math::Vec3 bbMax{ -1e30f, -1e30f, -1e30f };
		// (dans la boucle, pour chaque position monde `p`)
		bbMin.x = std::min(bbMin.x, p.x); bbMin.y = std::min(bbMin.y, p.y); bbMin.z = std::min(bbMin.z, p.z);
		bbMax.x = std::max(bbMax.x, p.x); bbMax.y = std::max(bbMax.y, p.y); bbMax.z = std::max(bbMax.z, p.z);
		// (après la boucle, une fois `prop` construit)
		prop.occluderCenter = engine::math::Vec3{
			(bbMin.x + bbMax.x) * 0.5f, (bbMin.y + bbMax.y) * 0.5f, (bbMin.z + bbMax.z) * 0.5f };
		const engine::math::Vec3 ext{ bbMax.x - bbMin.x, bbMax.y - bbMin.y, bbMax.z - bbMin.z };
		prop.occluderRadius = 0.5f * std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z);
```

> Si la fonction ne réitère pas explicitement les sommets monde (ex. transform appliqué
> en lot), utiliser à la place les bornes du `MeshAsset` baké : `mesh->localBoundsMin`
> / `mesh->localBoundsMax` (qui, pour un prop aux sommets cuits en monde, sont déjà en
> espace monde) pour remplir `occluderCenter`/`occluderRadius` de la même façon. Garder
> `<algorithm>` et `<cmath>` inclus dans Engine.cpp (déjà le cas).

- [ ] **Step 3 : Initialiser le module (lecture config)**

Là où les sous-systèmes s'initialisent au boot (près de l'init caméra / gameplay),
ajouter :

```cpp
		engine::render::CameraOcclusionFade::Config occCfg{};
		occCfg.fadeMin             = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.fade_min", 0.15));
		occCfg.radiusMargin        = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.radius_margin", 0.5));
		occCfg.fadeInPerSec        = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.fade_in_per_sec", 6.0));
		occCfg.fadeOutPerSec       = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.fade_out_per_sec", 8.0));
		occCfg.playerProtectRadius = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.player_protect_radius", 0.6));
		m_cameraOcclusionFade.Init(occCfg);
```

- [ ] **Step 4 : Mettre à jour le fondu + clamper le terrain (boucle Update, ~9895)**

Juste après `m_orbitalCameraController.Update(m_input, dt, mouseSensitivity, invertY, rmbLook, out.camera);`
(ligne ~9895), ajouter :

```cpp
				// --- Clamp anti-sous-sol : la caméra ne descend pas sous le terrain. ---
				const bool occEnabled = m_cfg.GetBool("client.camera.occlusion_fade.enabled", true);
				const float clampMargin = static_cast<float>(
					m_cfg.GetDouble("client.camera.terrain_clamp_margin", 0.5));
				const float groundY = m_terrain.SampleHeightAtWorldXZ(
					out.camera.position.x, out.camera.position.z);
				const float minCamY = groundY + clampMargin;
				if (out.camera.position.y < minCamY)
					out.camera.position.y = minCamY;

				// --- Fondu d'occlusion : collecte des occulteurs (props + bâtiments). ---
				if (occEnabled)
				{
					std::vector<engine::render::OccluderSphere> occluders;
					occluders.reserve(m_props.size());
					for (std::size_t i = 0; i < m_props.size(); ++i)
					{
						const auto& prop = m_props[i];
						if (prop.occluderRadius <= 0.0f) continue;
						occluders.push_back({ static_cast<std::uint32_t>(i),
							prop.occluderCenter, prop.occluderRadius });
					}
					const engine::math::Vec3 focus = m_orbitalCameraController.GetTargetPosition();
					m_cameraOcclusionFade.Update(out.camera.position, focus,
						occluders, static_cast<float>(dt));
				}
```

> Vérifier le nom exact du membre TerrainRenderer (`m_terrain`, cf. `m_terrain.Record`
> ligne 5441) et que `SampleHeightAtWorldXZ` est public (il l'est :
> `TerrainRenderer.h:185`). `dt` est disponible dans cette fonction (déjà passé à
> `m_orbitalCameraController.Update`).

- [ ] **Step 5 : Appliquer le fondu par-prop dans `RecordPropsGeometry` (~14317)**

Dans `RecordPropsGeometry`, rendre l'index de prop disponible et passer le fondu à
`Record`. Remplacer la boucle `for (const auto& prop : m_props)` par une boucle indexée :

```cpp
	for (std::size_t propIndex = 0; propIndex < m_props.size(); ++propIndex)
	{
		const auto& prop = m_props[propIndex];
		const float fade = m_cameraOcclusionFade.FadeFor(static_cast<std::uint32_t>(propIndex));
		// ... (culling distance / impostor inchangés) ...
		for (const auto& part : prop.parts)
		{
			geom.Record(
				/* ... params inchangés jusqu'à materialIndex ... */
				prop.modelMatrix.m,
				highlight ? part.highlightMaterialIndex : part.materialIndex,
				fade,          // <-- nouveau : fondu d'occlusion de ce prop
				true);         // loadExistingGbuffer
		}
	}
```

> Conserver tel quel le reste du corps de la boucle (sélection LOD/impostor, etc.).
> Seuls changent : l'itération indexée, le calcul `fade`, et l'argument `fade` ajouté à
> `geom.Record` (juste avant le `true` final `loadExistingGbuffer`). L'index `propIndex`
> est exactement l'`id` utilisé en Step 4 → correspondance garantie.

- [ ] **Step 6 : Vérifier (CI) — build client OK**

Pousser ; la CI doit builder client sans erreur (signatures `Record` cohérentes,
includes en place).

- [ ] **Step 7 : Validation en jeu (manuelle, utilisateur)**

- Entrer dans l'auberge, tourner la caméra derrière un mur → le mur se **trame** et le
  perso reste visible ; en s'éloignant, le mur **redevient opaque** en douceur.
- Reculer la caméra dans une pente / coller un mur → la caméra ne **plonge plus sous le
  sol** (1re capture corrigée).
- L'avatar n'est **jamais** tramé.
- Réglages possibles sans recompiler via `config.json` :
  `client.camera.occlusion_fade.fade_min` (opacité résiduelle), `radius_margin`,
  `fade_out_per_sec` / `fade_in_per_sec`, `client.camera.terrain_clamp_margin`.

- [ ] **Step 8 : Commit**

```bash
git add src/client/app/Engine.h src/client/app/Engine.cpp
git commit -m "feat(camera): occlusion fade par-prop + clamp terrain (config client.camera.*)"
```

---

## Notes de déploiement

✅ **Client uniquement** — rendu + caméra. Aucun opcode, payload, handler ni migration.
**Pas de redéploiement serveur.**

## Récapitulatif des tâches

1. **Task 1** — Module `CameraOcclusionFade` + 9 tests ctest (math pur). *Testable CI.*
2. **Task 2** — `fade` dans push-constant (144 o inchangé) + dither shader + avatar opaque. *Build/compil shader CI.*
3. **Task 3** — Intégration Engine : sphères englobantes au bake, collecte occulteurs,
   fondu par-prop, clamp terrain, config. *Build CI + validation en jeu.*
