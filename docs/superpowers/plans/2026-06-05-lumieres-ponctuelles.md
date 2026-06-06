# Lumières ponctuelles (point lights) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Consommer `DynamicLightSystem::GetActiveLights()` dans `lighting.frag` pour que les lumières ponctuelles (torches/lampes/fenêtres) éclairent les scènes de nuit.

**Architecture:** UBO `binding 12` (64 lumières max) calqué sur l'UBO cascades du CSM. `lighting.frag` accumule chaque lumière avec le BRDF Cook-Torrance existant + atténuation UE4 windowed, après le terme soleil et avant l'ambient. Snapshot des lumières dans `RenderState` (anti data-race). Facteur `world.point_lights.intensity_scale`.

**Tech Stack:** C++17, Vulkan (UBO raw host-visible), GLSL 450 (SPIR-V recompilé par la CI). **Pas de build local** : compile validée CI Windows/Linux ; rendu validé manuellement.

**Référence spec :** `docs/superpowers/specs/2026-06-05-lumieres-ponctuelles-design.md`.

**Portée :** client uniquement — **pas de redéploiement serveur**. Stackée sur le CSM (binding 12 après 10/11). Aucune modif `frontFace`/`cullMode`/winding.

---

## Ordre

Les 3 tâches code forment un contrat runtime cohérent (binding 12 identique shader↔C++). Ordre : Task 1 (LightingPass fournit l'UBO) → Task 2 (RenderState snapshot + Engine remplit/passe) → Task 3 (shader consomme). Une seule PR ; cohérence runtime après les 3.

## File Structure

- **Modify** `src/client/render/LightingPass.h` / `.cpp` — UBO point lights binding 12 (calque CSM).
- **Modify** `src/client/app/Engine.h` — champ `RenderState.pointLights`.
- **Modify** `src/client/app/Engine.cpp` — snapshot au site `out.cascades` + remplissage UBO dans le lambda Lighting.
- **Modify** `game/data/shaders/lighting.frag` — binding 12 + `PointAtten` + boucle d'accumulation.

---

### Task 1: LightingPass — UBO point lights (binding 12) — C++

Câbler un UBO host-visible par frame, **calque exact** du pattern CSM (`ShadowUbo` / `m_shadowUbo*`). Binding 12 = `UNIFORM_BUFFER` uniquement (pas d'image).

> Vérif Task 1 : pas de build local → CI compile ; `static_assert(sizeof(PointLightUbo)==2064)` casse la compilation si le layout dérive. Un seul appelant de `Record` (`Engine.cpp`, MAJ en Task 2).

- [ ] **Step 1.1 — `LightingPass.h` : structs `PointLightStd140`/`PointLightUbo` + `static_assert`.** Juste après `static_assert(sizeof(ShadowUbo)==272,...)` :
```cpp
		/// CPU-side de l'UBO point lights (binding 12, std140). SÉPARÉ de LightParams
		/// (push-constant 224 o) et de ShadowUbo. count.x = nb lumières [0..64] ;
		/// lights[i].posRadius (xyz=position monde m, w=rayon m) ; colorIntensity
		/// (rgb=couleur linéaire, a=intensité×intensity_scale). Voir lighting.frag b12.
		struct PointLightStd140
		{
			float posRadius[4];      ///< xyz = position monde (m) ; w = rayon (m).
			float colorIntensity[4]; ///< rgb = couleur linéaire ; a = intensité finale.
		};
		struct PointLightUbo
		{
			uint32_t        count[4];    ///< x = nb actives [0..64] ; yzw padding std140.
			PointLightStd140 lights[64]; ///< std140, stride 32 o/élément.
		};
		static_assert(sizeof(PointLightUbo) == 2064, "PointLightUbo must be 2064 bytes (16 + 64*32)");
```

- [ ] **Step 1.2 — `LightingPass.h` : membres `m_pointLightUbo*`.** Juste après les membres `m_shadowUbo*` :
```cpp
		std::vector<VkBuffer>       m_pointLightUboBuffers; ///< UBO point lights host-visible, un par frame (binding 12).
		std::vector<VkDeviceMemory> m_pointLightUboMemory;  ///< Mémoire host-visible.
		std::vector<void*>          m_pointLightUboMapped;  ///< Pointeurs mappés persistants.
```

- [ ] **Step 1.3 — `LightingPass.h` : paramètre `Record`.** Après `const ShadowUbo& shadowData,` et avant `const LightParams& params` :
```cpp
			/// \param pointLightData count + lights[64] (2064 o), uploadés dans l'UBO
			///        binding 12. count.x==0 => boucle point lights sautée (rendu inchangé).
			const PointLightUbo& pointLightData,
```

- [ ] **Step 1.4 — `LightingPass.cpp` : layout 12→13.** Passer `std::array<VkDescriptorSetLayoutBinding, 12>` à `13`, et après le bloc `bindings[11]` ajouter :
```cpp
			// 12 : UBO point lights (std140, 2064 o).
			bindings[12].binding            = 12;
			bindings[12].descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[12].descriptorCount    = 1;
			bindings[12].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[12].pImmutableSamplers = nullptr;
```
(`layoutInfo.bindingCount = bindings.size()` suit automatiquement.)

- [ ] **Step 1.5 — `LightingPass.cpp` : pool.** Le `poolSizes[1]` (UNIFORM_BUFFER) passe de `1 * m_maxFrames` à :
```cpp
			poolSizes[1].descriptorCount = 2 * m_maxFrames; // binding 11 (cascades) + binding 12 (point lights)
```

- [ ] **Step 1.6 — `LightingPass.cpp` : UBO point lights par frame.** Dupliquer le bloc « 5b. UBO cascades » en « 5c. UBO point lights » juste après sa `}` de fermeture, en remplaçant `sizeof(ShadowUbo)`→`sizeof(PointLightUbo)` et `m_shadowUbo*`→`m_pointLightUbo*` :
```cpp
		// 5c. UBO point lights (binding 12), host-visible, un par frame (2064 o,
		// mappé en permanence). Calque EXACT du bloc 5b.
		{
			VkPhysicalDeviceMemoryProperties memProps{};
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
			auto findMemoryType = [&](uint32_t typeBits, VkMemoryPropertyFlags want) -> uint32_t
			{
				for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
					if ((typeBits & (1u << i)) &&
						(memProps.memoryTypes[i].propertyFlags & want) == want)
						return i;
				return UINT32_MAX;
			};
			m_pointLightUboBuffers.resize(m_maxFrames, VK_NULL_HANDLE);
			m_pointLightUboMemory.resize(m_maxFrames, VK_NULL_HANDLE);
			m_pointLightUboMapped.resize(m_maxFrames, nullptr);
			for (uint32_t f = 0; f < m_maxFrames; ++f)
			{
				VkBufferCreateInfo bi{};
				bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				bi.size        = sizeof(PointLightUbo);
				bi.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
				bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				if (vkCreateBuffer(device, &bi, nullptr, &m_pointLightUboBuffers[f]) != VK_SUCCESS)
				{
					LOG_ERROR(Render, "LightingPass: vkCreateBuffer (point light UBO) failed");
					Destroy(device); return false;
				}
				VkMemoryRequirements memReq{};
				vkGetBufferMemoryRequirements(device, m_pointLightUboBuffers[f], &memReq);
				const uint32_t memTypeIdx = findMemoryType(memReq.memoryTypeBits,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				if (memTypeIdx == UINT32_MAX)
				{
					LOG_ERROR(Render, "LightingPass: no host-visible memory type for point light UBO");
					Destroy(device); return false;
				}
				VkMemoryAllocateInfo ai{};
				ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				ai.allocationSize  = memReq.size;
				ai.memoryTypeIndex = memTypeIdx;
				if (vkAllocateMemory(device, &ai, nullptr, &m_pointLightUboMemory[f]) != VK_SUCCESS ||
					vkBindBufferMemory(device, m_pointLightUboBuffers[f], m_pointLightUboMemory[f], 0) != VK_SUCCESS ||
					vkMapMemory(device, m_pointLightUboMemory[f], 0, sizeof(PointLightUbo), 0, &m_pointLightUboMapped[f]) != VK_SUCCESS)
				{
					LOG_ERROR(Render, "LightingPass: point light UBO alloc/bind/map failed");
					Destroy(device); return false;
				}
			}
		}
```

- [ ] **Step 1.7 — `LightingPass.cpp` : `Record` signature + write binding 12.** Ajouter le param `const PointLightUbo& pointLightData,` (même position que dans le `.h`). Après le bloc `shadowBufInfo` (`std::memcpy(m_shadowUboMapped[setIdx],...)`), ajouter :
```cpp
			std::memcpy(m_pointLightUboMapped[setIdx], &pointLightData, sizeof(PointLightUbo));
			VkDescriptorBufferInfo pointLightBufInfo{};
			pointLightBufInfo.buffer = m_pointLightUboBuffers[setIdx];
			pointLightBufInfo.offset = 0;
			pointLightBufInfo.range  = sizeof(PointLightUbo);
```
Passer `std::array<VkWriteDescriptorSet, 12> writes` à `13`, et après le bloc `writes[11]` ajouter :
```cpp
			writes[12].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[12].dstSet          = ds;
			writes[12].dstBinding      = 12;
			writes[12].dstArrayElement = 0;
			writes[12].descriptorCount = 1;
			writes[12].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[12].pBufferInfo     = &pointLightBufInfo;
```
(`vkUpdateDescriptorSets(..., writes.size(), ...)` suit la taille 13.)

- [ ] **Step 1.8 — `LightingPass.cpp` : `Destroy`.** Après la boucle de libération CSM (`m_shadowUbo*`), ajouter la même pour `m_pointLightUbo*` :
```cpp
		for (size_t f = 0; f < m_pointLightUboBuffers.size(); ++f)
		{
			if (m_pointLightUboMemory[f] != VK_NULL_HANDLE)
				vkFreeMemory(device, m_pointLightUboMemory[f], nullptr);
			if (m_pointLightUboBuffers[f] != VK_NULL_HANDLE)
				vkDestroyBuffer(device, m_pointLightUboBuffers[f], nullptr);
		}
		m_pointLightUboBuffers.clear();
		m_pointLightUboMemory.clear();
		m_pointLightUboMapped.clear();
```

- [ ] **Step 1.9 — Commit**
```bash
git add src/client/render/LightingPass.h src/client/render/LightingPass.cpp
git commit -m "feat(render): LightingPass expose un UBO point lights (binding 12)"
```

---

### Task 2: RenderState + Engine — snapshot et remplissage UBO

> Vérif Task 2 : `out.cascades` et `out.pointLights` écrits dans le même `out` (anti data-race) ; le lambda lit uniquement `rs.pointLights`. CI compile.

- [ ] **Step 2.1 — `Engine.h` : champ `pointLights` dans `RenderState`.** Ajouter l'include `#include "src/client/render/DynamicLightSystem.h"` en tête de `Engine.h` s'il est absent (pour `engine::render::ActivePointLight`). Puis, juste après `engine::render::CascadesUniform cascades;` :
```cpp
		// Snapshot des point lights actives pour la passe Lighting. Rempli au
		// moment de l'assemblage du RenderState (même endroit que cascades),
		// lu dans le lambda Lighting → découple m_dynamicLights du thread de
		// rendu (anti data-race).
		std::vector<engine::render::ActivePointLight> pointLights;
```

- [ ] **Step 2.2 — `Engine.cpp` : remplir `out.pointLights` au site `out.cascades`.** Juste après le bloc `ComputeCascades(... out.cascades)` :
```cpp
		// Snapshot des point lights actives dans le RenderState (anti data-race).
		// COPIE ici (assemblage de `out`) ; le lambda Lighting lit rs.pointLights
		// et ne touche jamais m_dynamicLights. Tick() déjà appelé plus tôt (Update).
		out.pointLights = m_dynamicLights.GetActiveLights();
```
> `GetActiveLights()` retourne un `const std::vector<ActivePointLight>&` valide (vide si jamais tické) — pas besoin de garde. Si un garde d'init existe déjà (`IsInitialized()`/équivalent) et est utilisé pour les autres systèmes, l'employer ; sinon NE PAS en inventer.

- [ ] **Step 2.3 — `Engine.cpp` : construire le `PointLightUbo` dans le lambda Lighting.** Après le remplissage de `shadowUbo` et avant l'appel `Record` :
```cpp
				// UBO point lights (binding 12). Snapshot rs.pointLights (anti
				// data-race) ; clamp 64 ; intensité globale world.point_lights.intensity_scale
				// (défaut 1.0, garde-fou anti sur-exposition HDR).
				engine::render::LightingPass::PointLightUbo pointLightUbo{};
				const float intensityScale =
					static_cast<float>(m_cfg.GetDouble("world.point_lights.intensity_scale", 1.0));
				const size_t activeCount = std::min<size_t>(rs.pointLights.size(), 64);
				pointLightUbo.count[0] = static_cast<uint32_t>(activeCount);
				for (size_t i = 0; i < activeCount; ++i)
				{
					const engine::render::ActivePointLight& pl = rs.pointLights[i];
					pointLightUbo.lights[i].posRadius[0] = pl.position[0];
					pointLightUbo.lights[i].posRadius[1] = pl.position[1];
					pointLightUbo.lights[i].posRadius[2] = pl.position[2];
					pointLightUbo.lights[i].posRadius[3] = pl.radius;
					pointLightUbo.lights[i].colorIntensity[0] = pl.color[0];
					pointLightUbo.lights[i].colorIntensity[1] = pl.color[1];
					pointLightUbo.lights[i].colorIntensity[2] = pl.color[2];
					pointLightUbo.lights[i].colorIntensity[3] = pl.intensity * intensityScale;
				}
```
Puis insérer `pointLightUbo,` dans l'appel `Record`, juste après `shadowViews, shadowUbo,` et avant `lp, frameIdx`.
> Vérifier `<algorithm>` inclus dans `Engine.cpp` (pour `std::min`) ; sinon l'ajouter. Vérifier le nom réel de l'objet config (`m_cfg`).

- [ ] **Step 2.4 — Commit**
```bash
git add src/client/app/Engine.h src/client/app/Engine.cpp
git commit -m "feat(render): snapshot point lights dans RenderState + remplissage UBO (Engine)"
```

---

### Task 3: lighting.frag — boucle d'accumulation point lights

> Vérif Task 3 : SPIR-V recompilé par la CI. Cohérence figée : binding 12, `uvec4 count` + `lights[64]` de 2× `vec4` (stride 32) ↔ C++ `PointLightUbo` 2064 o.

- [ ] **Step 3.1 — binding 12 + helper `PointAtten`.** Après le bloc `binding 11` (`ShadowUbo`), ajouter :
```glsl
// ---- Lumières ponctuelles (point lights) ------------------------------------
// binding 12 : UBO std140. count.x = nb actives [0..64] ; lights[i].posRadius
// (xyz=position monde m, w=rayon m) ; lights[i].colorIntensity (rgb=couleur
// linéaire, a=intensité×intensity_scale). Pas d'ombre (v1).
struct PointLightStd140
{
    vec4 posRadius;
    vec4 colorIntensity;
};
layout(set = 0, binding = 12) uniform PointLightUbo
{
    uvec4 count;
    PointLightStd140 lights[64];
} uPoint;
```
Et après `F_Schlick` (avant `main()`) :
```glsl
// Atténuation UE4 « windowed inverse square » : ~1/d² près de la source, fond à 0
// à d=r (coupure nette, cohérente avec le skip dist>radius).
float PointAtten(float d, float r)
{
    float f = d / max(r, 1e-4);
    float w = clamp(1.0 - f * f * f * f, 0.0, 1.0); // 1 - (d/r)^4, saturé
    return (w * w) / (d * d + 1.0);
}
```

- [ ] **Step 3.2 — boucle après `Lo` soleil, avant ambient.** Localiser la ligne `vec3  Lo = (diffuse + specular) * pc.lightColor.rgb * NdotL * vis;` (terme soleil, CSM). Juste après, avant le bloc ambient, insérer :
```glsl
	// ---- Point lights (additif, gated count>0, sans ombre) ------------------
	// Même BRDF Cook-Torrance que le soleil (D/G/F réutilisés). Skip si hors du
	// rayon (dist>radius). Atténuation UE4 windowed. count==0 (jour) => sauté.
	if (uPoint.count.x > 0u)
	{
		uint n = min(uPoint.count.x, 64u);
		for (uint i = 0u; i < n; ++i)
		{
			vec3  Lpos   = uPoint.lights[i].posRadius.xyz;
			float radius = uPoint.lights[i].posRadius.w;
			vec3  Lp     = Lpos - P;
			float dist   = length(Lp);
			if (dist > radius) continue;

			vec3  Lp_n = Lp / max(dist, 1e-4);
			vec3  Hp   = normalize(V + Lp_n);
			float NdLp = max(dot(normalW, Lp_n), 0.0);
			if (NdLp <= 0.0) continue;
			float NdHp = max(dot(normalW, Hp), 0.0);
			float VdHp = max(dot(V, Hp), 0.0);

			float Dp = D_GGX(NdHp, roughness);
			float Gp = G_Smith(NdotV, NdLp, roughness);
			vec3  Fp = F_Schlick(VdHp, F0);
			vec3  kSp = Fp;
			vec3  kDp = (1.0 - kSp) * (1.0 - metallic);
			vec3  specp = (Dp * Gp * Fp) / max(4.0 * NdotV * NdLp, 1e-7);
			vec3  diffp = kDp * albedo / PI;

			float atten = PointAtten(dist, radius);
			vec3  radiance = uPoint.lights[i].colorIntensity.rgb
			               * uPoint.lights[i].colorIntensity.a * atten;
			Lo += (diffp + specp) * radiance * NdLp;
		}
	}
```
> Vérifier que `P`, `V`, `normalW`, `F0`, `NdotV`, `albedo`, `metallic`, `roughness`, `PI` sont bien les noms réels en portée à ce point (sinon adapter). Ne pas toucher l'ambient/IBL/DDGI ni `color = ambient + Lo`.

- [ ] **Step 3.3 — Commit**
```bash
git add game/data/shaders/lighting.frag
git commit -m "feat(shader): lighting.frag accumule les lumières ponctuelles"
```

---

### Task 4: Validation en jeu (manuelle)

- [ ] **CI verte** : build Linux + Windows ; `lighting.frag` → SPIR-V ; pas d'erreur de validation Vulkan (binding 12 lié chaque frame).
- [ ] **Nuit** : les sources de `dynamic_lights.json` éclairent leur entourage, halo décroissant avec la distance, coupure à `radius`.
- [ ] **Fondu jour↔nuit** : allumage/extinction progressif (fade 60 s), pas de pop.
- [ ] **Sur-exposition** : plusieurs lumières + auto-exposure → pas de blanc cramé ; sinon baisser `world.point_lights.intensity_scale` (<1.0) et confirmer.
- [ ] **Jour (0 lumière)** : `count==0` → rendu **identique** à avant.

---

## Self-Review

- **Couverture spec** : UBO binding 12 + `static_assert(2064)` (Task 1.1) ; boucle après `Lo` avant ambient, gated count>0, BRDF réutilisé, atténuation UE4, skip dist>radius (Task 3.2) ; snapshot RenderState anti data-race (Task 2.1/2.2) ; lecture via `rs.pointLights` dans le lambda (Task 2.3) ; `intensity_scale` + clamp 64 (Task 2.3) ; UBO host-visible/frame calque CSM (Task 1.6/1.7/1.8). ✅
- **Cohérence binding 12 Task 1 ↔ Task 3** : `UNIFORM_BUFFER` binding 12 stage fragment ; std140 `uvec4 count` + 64×(`vec4`,`vec4`). `static_assert 2064` verrouille le stride 32. ✅
- **Types** : `uint32_t count[4]` (16) + 64×32 = 2064 ↔ GLSL. `pl.position/radius/color/intensity` (floats) → `posRadius`/`colorIntensity`. ✅
- **Anti data-race** : `out.cascades` et `out.pointLights` dans le même `out = m_renderStates[writeIdx]` ; lambda lit `m_renderStates[readIdx]` ; pas de lecture de `m_dynamicLights` dans le lambda. ✅
- **Non-régression** : `count==0` saute la boucle ; binding 12 toujours lié à un UBO valide écrit chaque frame ; aucun winding touché. ✅
- **Risques exécutant** : include `DynamicLightSystem.h` dans `Engine.h` si absent ; `<algorithm>` pour `std::min` ; un seul appelant de `Record` ; ne pas inventer de garde d'init si `GetActiveLights()` est sûr.
