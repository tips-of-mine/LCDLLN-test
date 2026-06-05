# Ombres soleil CSM — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Échantillonner les 4 cascades CSM déjà rendues dans `lighting.frag` pour que le soleil projette de vraies ombres portées.

**Architecture:** Les cascades sont transportées vers `lighting.frag` via un UBO host-visible (binding 11) + un sampler array de 4 shadow maps (binding 10). Le shader sélectionne la cascade par *containment*, applique un PCF 3×3 avec biais slope-scaled, et multiplie le seul terme soleil direct par la visibilité. Gating runtime via `shadows.enabled`.

**Tech Stack:** C++17, Vulkan (descripteurs raw, sans VMA), GLSL 450 (recompilé en SPIR-V par la CI). **Pas de build local** (cmake/MSVC/vcpkg absents) : compile validée en CI Windows/Linux, SPIR-V régénéré par `tools/compile_game_shaders.ps1`, **rendu validé manuellement en jeu**.

**Référence spec :** `docs/superpowers/specs/2026-06-05-ombres-csm-design.md`.

**Portée :** client uniquement — **pas de redéploiement serveur**. Aucune modification `frontFace`/`cullMode`/winding.

---

## Ordre & cohérence

Les 3 tâches code forment **un tout cohérent** (le contrat de binding 10/11 doit être identique entre shader et C++). Ordre : Task 1 (LightingPass fournit les ressources) → Task 2 (Engine remplit/passe) → Task 3 (shader consomme). Chaque commit compile ; la cohérence runtime n'est atteinte qu'après les 3 (une seule PR). Numéros de binding **figés** : 10 = `sampler2D uShadowMaps[4]`, 11 = UBO `ShadowUbo`.

## File Structure

- **Modify** `src/client/render/LightingPass.h` — struct `ShadowUbo`, membres (sampler + UBO host-visible), signature `Record`.
- **Modify** `src/client/render/LightingPass.cpp` — layout (bindings 10/11), pool, sampler shadow, UBO host-visible/frame, écriture descripteurs, `Destroy`.
- **Modify** `src/client/app/Engine.cpp` — pass "Lighting" : `b.read` cascades, remplir `ShadowUbo`, appeler `Record`.
- **Modify** `game/data/shaders/lighting.frag` — bindings 10/11, `ShadowVisibility`, multiplication de `Lo`.

---

### Task 1: LightingPass — descripteurs shadow + UBO cascades (C++)

**Files:** `src/client/render/LightingPass.h`, `src/client/render/LightingPass.cpp`

Ajoute le **binding 10** (4 shadow maps, `COMBINED_IMAGE_SAMPLER` `descriptorCount=4`), le **binding 11** (UBO cascades), un UBO host-visible **par frame** (272 o), un sampler shadow, et étend `Record`. Repère pour l'UBO host-visible (raw Vulkan, sans VMA) : `SsaoKernelNoise.cpp:90-187`. `physicalDevice` est déjà reçu par `LightingPass::Init` (`LightingPass.cpp:87`, actuellement `/*physicalDevice*/`).

> Vérification globale Task 1 : pas de build local → compile validée CI Windows. Auto-vérifs : `static_assert(sizeof(ShadowUbo)==272)` ; `maxSets` inchangé (`m_maxFrames`) ; `descriptorCount=4` du binding 10 = `uShadowMaps[4]` (Task 3) ; chaque échec appelle `Destroy(device)` avant `return false`.

- [ ] **Step 1 — `LightingPass.h` : struct `ShadowUbo` + membres + signature `Record`.**

Après la déclaration de `LightParams` (~`LightingPass.h:53`), ajouter :
```cpp
		/// CPU-side de l'UBO cascades (binding 11, std140). 4 lightViewProj (256 o)
		/// + shadowParams (16 o) = 272 o. SÉPARÉ de LightParams (push-constant figé
		/// à 224 o). shadowParams : x=useShadows(0/1), y=texelSize(1/résolution),
		/// z=biasConstant, w=biasSlopeMax. Voir lighting.frag binding 11.
		struct ShadowUbo
		{
			float lightViewProj[16 * 4]; ///< 4 matrices colonne-major (cascade 0..3), 256 o.
			float shadowParams[4];       ///< x=useShadows, y=texelSize, z=biasConstant, w=biasSlopeMax.
		};
		static_assert(sizeof(ShadowUbo) == 272, "ShadowUbo must be exactly 272 bytes (256 + 16)");
```
Étendre la signature `Record` (`LightingPass.h:91-92`). **Avant :**
```cpp
			VkImageView ddgiIrradianceView, VkSampler ddgiSampler,
			const LightParams& params, uint32_t frameIndex);
```
**Après** (avec doc Doxygen) :
```cpp
			VkImageView ddgiIrradianceView, VkSampler ddgiSampler,
			/// \param shadowViews 4 vues des shadow maps cascades (binding 10). Toute
			///        entrée VK_NULL_HANDLE => fallback GBufferA ; shadowData.shadowParams[0]
			///        (useShadows) doit alors valoir 0 (le shader ne lit pas le fallback).
			/// \param shadowData lightViewProj[4] (256 o) + shadowParams (16 o), uploadés
			///        dans l'UBO host-visible binding 11.
			const VkImageView shadowViews[4], const ShadowUbo& shadowData,
			const LightParams& params, uint32_t frameIndex);
```
Ajouter les membres privés (après `m_depthSampler`, ~`LightingPass.h:107`) :
```cpp
		VkSampler m_shadowSampler = VK_NULL_HANDLE; ///< nearest clamp, lecture plain de la profondeur shadow (binding 10).
		std::vector<VkBuffer>       m_shadowUboBuffers; ///< UBO cascades host-visible, un par frame (binding 11).
		std::vector<VkDeviceMemory> m_shadowUboMemory;  ///< Mémoire host-visible.
		std::vector<void*>          m_shadowUboMapped;  ///< Pointeurs mappés persistants (HOST_VISIBLE|HOST_COHERENT).
```

- [ ] **Step 2 — `LightingPass.cpp` : descriptor set layout (bindings 10 + 11).**

Remplacer le `std::array<...,10>` (`LightingPass.cpp:159-172`). **Avant :**
```cpp
			std::array<VkDescriptorSetLayoutBinding, 10> bindings{};
			for (size_t i = 0; i < bindings.size(); ++i)
			{
				bindings[i].binding            = i;
				bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].descriptorCount    = 1;
				bindings[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
				bindings[i].pImmutableSamplers = nullptr;
			}

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
			layoutInfo.pBindings    = bindings.data();
```
**Après :**
```cpp
			// 0..9 : COMBINED_IMAGE_SAMPLER count=1. 10 : 4 shadow maps (count=4).
			// 11 : UBO cascades.
			std::array<VkDescriptorSetLayoutBinding, 12> bindings{};
			for (size_t i = 0; i < 10; ++i)
			{
				bindings[i].binding            = static_cast<uint32_t>(i);
				bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].descriptorCount    = 1;
				bindings[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
				bindings[i].pImmutableSamplers = nullptr;
			}
			bindings[10].binding            = 10;
			bindings[10].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[10].descriptorCount    = 4;
			bindings[10].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[10].pImmutableSamplers = nullptr;
			bindings[11].binding            = 11;
			bindings[11].descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[11].descriptorCount    = 1;
			bindings[11].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[11].pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
			layoutInfo.pBindings    = bindings.data();
```

- [ ] **Step 3 — `LightingPass.cpp` : descriptor pool.**

Remplacer (`LightingPass.cpp:187-195`). **Avant :**
```cpp
			VkDescriptorPoolSize poolSize{};
			poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 10 * m_maxFrames;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes    = &poolSize;
			poolInfo.maxSets       = m_maxFrames;
```
**Après :**
```cpp
			// Par set : 10 (bindings 0-9) + 4 (binding 10) = 14 image samplers,
			// + 1 UNIFORM_BUFFER (binding 11).
			std::array<VkDescriptorPoolSize, 2> poolSizes{};
			poolSizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[0].descriptorCount = 14 * m_maxFrames;
			poolSizes[1].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			poolSizes[1].descriptorCount = 1 * m_maxFrames;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
			poolInfo.pPoolSizes    = poolSizes.data();
			poolInfo.maxSets       = m_maxFrames;
```

- [ ] **Step 4 — `LightingPass.cpp` : décommenter `physicalDevice` + sampler shadow + UBO/frame.**

Décommenter le paramètre : `LightingPass.cpp:87` → `bool LightingPass::Init(VkDevice device, VkPhysicalDevice physicalDevice,`.

Après la création de `m_depthSampler` (~`LightingPass.cpp:253-259`), créer `m_shadowSampler` (réutiliser la `VkSamplerCreateInfo si` du bloc samplers — nearest clamp, **pas** de compare) :
```cpp
			// Sampler des shadow maps (binding 10) : nearest clamp, PAS de compare
			// (PCF + test profondeur faits manuellement dans lighting.frag).
			res = vkCreateSampler(device, &si, nullptr, &m_shadowSampler);
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "LightingPass: vkCreateSampler (shadow) failed: {}", static_cast<int>(res));
				Destroy(device);
				return false;
			}
```
> Si `si` n'est plus en scope à ce point, déclarer une nouvelle `VkSamplerCreateInfo` identique à celle du depth sampler (nearest, `addressMode=CLAMP_TO_EDGE`, `compareEnable=VK_FALSE`).

Ajouter un nouveau bloc (après le bloc samplers, avant le bloc pipeline layout) créant un UBO cascades host-visible **par frame** (modèle `SsaoKernelNoise.cpp:90-187`) :
```cpp
		// UBO cascades (binding 11), host-visible, un par frame in-flight (272 o,
		// mappé en permanence HOST_COHERENT). Repère : SsaoKernelNoise.cpp.
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

			m_shadowUboBuffers.resize(m_maxFrames, VK_NULL_HANDLE);
			m_shadowUboMemory.resize(m_maxFrames, VK_NULL_HANDLE);
			m_shadowUboMapped.resize(m_maxFrames, nullptr);

			for (uint32_t f = 0; f < m_maxFrames; ++f)
			{
				VkBufferCreateInfo bi{};
				bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				bi.size        = sizeof(ShadowUbo);
				bi.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
				bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				if (vkCreateBuffer(device, &bi, nullptr, &m_shadowUboBuffers[f]) != VK_SUCCESS)
				{
					LOG_ERROR(Render, "LightingPass: vkCreateBuffer (shadow UBO) failed");
					Destroy(device);
					return false;
				}

				VkMemoryRequirements memReq{};
				vkGetBufferMemoryRequirements(device, m_shadowUboBuffers[f], &memReq);
				const uint32_t memTypeIdx = findMemoryType(memReq.memoryTypeBits,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				if (memTypeIdx == UINT32_MAX)
				{
					LOG_ERROR(Render, "LightingPass: no host-visible memory type for shadow UBO");
					Destroy(device);
					return false;
				}

				VkMemoryAllocateInfo ai{};
				ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				ai.allocationSize  = memReq.size;
				ai.memoryTypeIndex = memTypeIdx;
				if (vkAllocateMemory(device, &ai, nullptr, &m_shadowUboMemory[f]) != VK_SUCCESS ||
					vkBindBufferMemory(device, m_shadowUboBuffers[f], m_shadowUboMemory[f], 0) != VK_SUCCESS ||
					vkMapMemory(device, m_shadowUboMemory[f], 0, sizeof(ShadowUbo), 0, &m_shadowUboMapped[f]) != VK_SUCCESS)
				{
					LOG_ERROR(Render, "LightingPass: shadow UBO alloc/bind/map failed");
					Destroy(device);
					return false;
				}
			}
		}
```

- [ ] **Step 5 — `LightingPass.cpp` : `Record` — signature + fallback + descripteurs + upload UBO.**

Étendre la signature de `Record` (`LightingPass.cpp:404-405`) comme dans le `.h`. Après le fallback DDGI (~`LightingPass.cpp:438-439`), ajouter le fallback shadow :
```cpp
			// binding 10 (4 shadow maps). Vue nulle => fallback GBufferA (viewA) ;
			// useShadows (shadowParams.x) vaudra 0 et le shader ne lira pas le fallback.
			VkImageView shView[4];
			for (int i = 0; i < 4; ++i)
				shView[i] = (shadowViews && shadowViews[i] != VK_NULL_HANDLE) ? shadowViews[i] : viewA;
```
Remplacer le bloc `imageInfos`/`writes` (`LightingPass.cpp:447-470`) — **conserver `imageInfos[0..9]` à l'identique**, élargir à 14, ajouter le binding 10 (count=4) et le binding 11 (UBO). Réutiliser le `setIdx` déjà calculé (`LightingPass.cpp:444`, `const uint32_t setIdx = frameIndex % m_maxFrames;`) pour indexer l'UBO :
```cpp
			std::array<VkDescriptorImageInfo, 14> imageInfos{};
			imageInfos[0] = { m_sampler,        viewA,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			imageInfos[1] = { m_sampler,        viewB,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			imageInfos[2] = { m_sampler,        viewC,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			imageInfos[3] = { m_depthSampler,   viewD,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			imageInfos[4] = { irrSamp,          irrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			imageInfos[5] = { prefilterSampler, prefilterView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			imageInfos[6] = { brdfLutSampler,   brdfLutView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			imageInfos[7] = { m_sampler,        viewSsao,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			imageInfos[8] = { m_sampler,        viewDecal, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			imageInfos[9] = { ddgiSampler,      ddgiIrradianceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			for (int i = 0; i < 4; ++i)
				imageInfos[10 + i] = { m_shadowSampler, shView[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

			std::memcpy(m_shadowUboMapped[setIdx], &shadowData, sizeof(ShadowUbo));
			VkDescriptorBufferInfo shadowBufInfo{};
			shadowBufInfo.buffer = m_shadowUboBuffers[setIdx];
			shadowBufInfo.offset = 0;
			shadowBufInfo.range  = sizeof(ShadowUbo);

			std::array<VkWriteDescriptorSet, 12> writes{};
			for (size_t i = 0; i < 10; ++i)
			{
				writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet          = ds;
				writes[i].dstBinding      = static_cast<uint32_t>(i);
				writes[i].dstArrayElement = 0;
				writes[i].descriptorCount = 1;
				writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[i].pImageInfo      = &imageInfos[i];
			}
			writes[10].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[10].dstSet          = ds;
			writes[10].dstBinding      = 10;
			writes[10].dstArrayElement = 0;
			writes[10].descriptorCount = 4;
			writes[10].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[10].pImageInfo      = &imageInfos[10];
			writes[11].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[11].dstSet          = ds;
			writes[11].dstBinding      = 11;
			writes[11].dstArrayElement = 0;
			writes[11].descriptorCount = 1;
			writes[11].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[11].pBufferInfo     = &shadowBufInfo;

			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
```
> Vérifier par lecture que `ds` est bien le set indexé par `setIdx` (le buffer `m_shadowUboBuffers[setIdx]` doit correspondre au même set). `<cstring>` (`std::memcpy`) : vérifier qu'il est inclus dans `LightingPass.cpp` ; sinon l'ajouter.

- [ ] **Step 6 — `LightingPass.cpp` : `Destroy` — libérer sampler + UBO.**

Dans `Destroy` (`LightingPass.cpp:538-584`), avant la destruction du pool, ajouter :
```cpp
		if (m_shadowSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_shadowSampler, nullptr);
			m_shadowSampler = VK_NULL_HANDLE;
		}
		for (size_t f = 0; f < m_shadowUboBuffers.size(); ++f)
		{
			if (m_shadowUboMemory[f] != VK_NULL_HANDLE)
				vkFreeMemory(device, m_shadowUboMemory[f], nullptr); // unmappe implicitement
			if (m_shadowUboBuffers[f] != VK_NULL_HANDLE)
				vkDestroyBuffer(device, m_shadowUboBuffers[f], nullptr);
		}
		m_shadowUboBuffers.clear();
		m_shadowUboMemory.clear();
		m_shadowUboMapped.clear();
```
> Ne pas modifier le guard `if (device == VK_NULL_HANDLE) return;` existant en tête de `Destroy`.

- [ ] **Step 7 — Commit**
```bash
git add src/client/render/LightingPass.h src/client/render/LightingPass.cpp
git commit -m "feat(render): LightingPass expose bindings shadow + UBO cascades (CSM)"
```

---

### Task 2: Engine — câbler le pass Lighting sur les shadow maps

**Files:** `src/client/app/Engine.cpp` (lambda du pass "Lighting", ~5659-5788)

`m_fgShadowMapIds[i]` (D32, SAMPLED) déjà créées (`Engine.cpp:3905-3910`) et écrites (`5427`). `rs.cascades` accessible (utilisé par VolumetricFog `5985` : `rs.cascades.lightViewProj[0].m`). `kCascadeCount` visible via `CascadedShadowMaps.h`.

> Vérification Task 2 : lecture seule ; compile CI. Confirmer que `rs.cascades.lightViewProj[i].m` est un `float[16]` colonne-major (déjà utilisé tel quel `Engine.cpp:5444`, `5985`).

- [ ] **Step 1 — `b.read` des 4 cascades dans le PassBuilder.**

Après `b.read(m_fgDecalOverlayId, ...)` (~`Engine.cpp:5666`). **Avant :**
```cpp
				b.read(m_fgDecalOverlayId,   engine::render::ImageUsage::SampledRead);
				b.write(m_fgSceneColorHDRId, engine::render::ImageUsage::ColorWrite);
```
**Après :**
```cpp
				b.read(m_fgDecalOverlayId,   engine::render::ImageUsage::SampledRead);
				// CSM — les 4 shadow maps cascades (rendues lisibles SAMPLED par le FG).
				for (uint32_t i = 0; i < engine::render::kCascadeCount; ++i)
					b.read(m_fgShadowMapIds[i], engine::render::ImageUsage::SampledRead);
				b.write(m_fgSceneColorHDRId, engine::render::ImageUsage::ColorWrite);
```

- [ ] **Step 2 — Remplir `ShadowUbo`, récupérer les vues, lire la config.**

Dans la lambda d'exécution, après le bloc DDGI (~`Engine.cpp:5777`), avant l'appel `Record` :
```cpp
				// ---- CSM — Ombres cascades --------------------------------------
				engine::render::LightingPass::ShadowUbo shadowUbo{};
				VkImageView shadowViews[engine::render::kCascadeCount] = {};
				bool shadowViewsOk = true;
				for (uint32_t i = 0; i < engine::render::kCascadeCount; ++i)
				{
					shadowViews[i] = reg.getImageView(m_fgShadowMapIds[i]);
					if (shadowViews[i] == VK_NULL_HANDLE) shadowViewsOk = false;
					std::memcpy(&shadowUbo.lightViewProj[i * 16],
						rs.cascades.lightViewProj[i].m, sizeof(float) * 16);
				}
				const bool shadowsEnabled = m_cfg.GetBool("shadows.enabled", true);
				const uint32_t shadowRes  = static_cast<uint32_t>(m_cfg.GetInt("shadows.resolution", 1024));
				shadowUbo.shadowParams[0] = (shadowsEnabled && shadowViewsOk) ? 1.0f : 0.0f;
				shadowUbo.shadowParams[1] = (shadowRes > 0) ? (1.0f / static_cast<float>(shadowRes)) : 0.0f;
				shadowUbo.shadowParams[2] = static_cast<float>(m_cfg.GetDouble("shadows.bias_constant", 0.0015));
				shadowUbo.shadowParams[3] = static_cast<float>(m_cfg.GetDouble("shadows.bias_slope_max", 0.005));
```
> Vérifier par lecture le nom exact de l'objet config (`m_cfg` vs `m_config`) et les méthodes `GetBool/GetInt/GetDouble` (utiliser celles déjà employées ailleurs dans `Engine.cpp` pour `shadows.*`, ex. `shadows.resolution` à `Engine.cpp:3898`). Adapter si la signature diffère. `kCascadeCount` doit valoir 4 (sinon `shadowUbo.lightViewProj` n'a que 4 slots — vérifier).

- [ ] **Step 3 — Étendre l'appel `Record`.**

(`Engine.cpp:5780-5787`). **Avant :**
```cpp
					ddgiView, ddgiSamp,
					lp, frameIdx);
```
**Après :**
```cpp
					ddgiView, ddgiSamp,
					shadowViews, shadowUbo,
					lp, frameIdx);
```
> Vérifier les noms réels des variables (`ddgiView`/`ddgiSamp`/`lp`/`frameIdx`) au site d'appel et insérer `shadowViews, shadowUbo` juste avant les 2 derniers arguments (`params`, `frameIndex`).

- [ ] **Step 4 — Commit**
```bash
git add src/client/app/Engine.cpp
git commit -m "feat(render): Engine câble les cascades shadow vers LightingPass (CSM)"
```

---

### Task 3: lighting.frag — sélection cascade + PCF + multiplication

**Files:** `game/data/shaders/lighting.frag`

> Vérification Task 3 : SPIR-V recompilé par la CI (le runtime charge `lighting.frag.spv`). Cohérence : binding 10 = `sampler2D[4]` ↔ C++ count=4 ; binding 11 UBO ↔ C++ `ShadowUbo` ; ordre `lightViewProj[4]` puis `shadowParams`.

- [ ] **Step 1 — Déclarer bindings 10 et 11.**

Après le binding 9 (~`lighting.frag:58`) :
```glsl
// ---- CSM — Ombres cascades --------------------------------------------------
// binding 10 : 4 shadow maps (depth Vulkan [0,1], sampler nearest clamp, compare
//   manuel comme volumetric_fog.frag — PAS de sampler2DShadow).
// binding 11 : UBO cascades (std140). shadowParams : x=useShadows, y=texelSize
//   (1/résolution), z=biasConstant, w=biasSlopeMax.
layout(set = 0, binding = 10) uniform sampler2D uShadowMaps[4];
layout(set = 0, binding = 11) uniform ShadowUbo
{
    mat4 lightViewProj[4];
    vec4 shadowParams;
} uShadow;
```

- [ ] **Step 2 — Ajouter `SampleShadowDepth` (index constant) + `ShadowVisibility`.**

Avant `void main()` (~`lighting.frag:222`). **Important** : l'échantillonnage utilise un **switch à index constant** (et non `uShadowMaps[i]` avec `i` variable) pour éviter l'indexation non-uniforme d'un tableau de samplers (UB possible sans `nonuniformEXT`) :
```glsl
// CSM — lit la profondeur stockée dans la cascade i à l'UV donné. Index CONSTANT
// (switch) pour éviter l'indexation dynamique non-uniforme d'un sampler array.
float SampleShadowDepth(int i, vec2 uv)
{
    if (i == 0) return texture(uShadowMaps[0], uv).r;
    if (i == 1) return texture(uShadowMaps[1], uv).r;
    if (i == 2) return texture(uShadowMaps[2], uv).r;
    return texture(uShadowMaps[3], uv).r;
}

// CSM — visibilité du soleil. Sélectionne la première cascade i (0→3) où P se
// projette dans [0,1]² (UV) et [0,1] (profondeur) [containment]. Hors de toutes
// -> éclairé (1.0). PCF 3×3 (9 taps, pas = shadowParams.y) ; compare manuel sur
// profondeur Vulkan [0,1] (cf. volumetric_fog.frag) ; biais slope-scaled.
// \param P     Position monde du fragment.
// \param NdotL normale·soleil (>=0), pour le biais.
// \return [0,1] : 1 = éclairé, 0 = ombré.
float ShadowVisibility(vec3 P, float NdotL)
{
    float texel = uShadow.shadowParams.y;
    float bias  = max(uShadow.shadowParams.z,
                      uShadow.shadowParams.w * (1.0 - NdotL));

    for (int i = 0; i < 4; ++i)
    {
        vec4 clip = uShadow.lightViewProj[i] * vec4(P, 1.0);
        if (clip.w <= 0.0) continue;
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv  = ndc.xy * 0.5 + 0.5;   // [-1,1] -> [0,1]
        float refDepth = ndc.z;          // profondeur Vulkan, déjà [0,1]

        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
            refDepth < 0.0 || refDepth > 1.0)
            continue; // pas dans cette cascade -> essaie la suivante

        float vis = 0.0;
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            vec2  off = vec2(float(dx), float(dy)) * texel;
            float occ = SampleShadowDepth(i, uv + off);
            vis += (refDepth - bias <= occ) ? 1.0 : 0.0;
        }
        return vis / 9.0;
    }
    return 1.0; // hors de toutes les cascades -> éclairé
}
```

- [ ] **Step 3 — Multiplier `Lo` par la visibilité.**

(`lighting.frag:291-292`). **Avant :**
```glsl
    // ---- Direct lighting contribution ----------------------------------
    vec3  Lo = (diffuse + specular) * pc.lightColor.rgb * NdotL;
```
**Après :**
```glsl
    // ---- Direct lighting contribution ----------------------------------
    // CSM — atténue le SEUL soleil direct par les ombres cascades. Ambient/IBL/DDGI
    // non ombrés. Gating : shadowParams.x <= 0.5 (shadows off / maps invalides) -> 1.
    float vis = (uShadow.shadowParams.x > 0.5) ? ShadowVisibility(P, NdotL) : 1.0;
    vec3  Lo = (diffuse + specular) * pc.lightColor.rgb * NdotL * vis;
```
> Vérifier que la variable de position monde s'appelle bien `P` et la normale·soleil `NdotL` à ce point (sinon adapter aux noms réels lus). Ne PAS toucher aux blocs ambient/IBL/DDGI ni à la perspective aérienne.

- [ ] **Step 4 — Commit**
```bash
git add game/data/shaders/lighting.frag
git commit -m "feat(shader): lighting.frag échantillonne les cascades CSM (ombres soleil)"
```

---

### Task 4: Validation en jeu (manuelle)

**Aucun build local.** Compile C++ + SPIR-V via CI ; rendu validé manuellement (build CI/VS).

- [ ] **Ombres présentes** : `shadows.enabled=true` (défaut), soleil rasant → ombres portées visibles, alignées avec la géométrie et la direction du soleil.
- [ ] **Pas d'acné** : surfaces éclairées non striées. Si acné → augmenter `shadows.bias_constant`/`shadows.bias_slope_max`.
- [ ] **Pas de peter-panning** : ombres attachées au pied des objets. Si décollement → biais trop fort, réduire.
- [ ] **Transition cascades** : pas de coupure brutale d'ombre en s'éloignant ; ombres lointaines présentes.
- [ ] **Gating off** : `"shadows": { "enabled": false }` → rendu sans ombres, pas d'artefact, pas de crash.
- [ ] **Validation layers Vulkan** : aucune erreur sur le set 0 (bindings 10/11), aucun type/array out-of-bounds.

---

## Self-Review

**Couverture spec :**
- Binding 10 `sampler2D[4]` + binding 11 UBO std140 → Task 1 (C++) + Task 3 (GLSL). ✅
- Sélection containment i=0→3, sinon vis=1 → Task 3 Step 2. ✅
- PCF 3×3 + compare manuel + profondeur Vulkan [0,1] + biais slope-scaled → Task 3 Step 2. ✅
- `Lo` (soleil direct seul) × `vis` ; ambient/IBL/DDGI non ombrés → Task 3 Step 3. ✅
- Transport UBO host-visible 272 o (pas push-constant) → Task 1 Steps 1,4. ✅
- Gating `shadows.enabled` + repli éclairé si maps invalides → Task 2 Step 2. ✅
- Aucune modif winding (raster du pass Lighting inchangée). ✅

**Placeholders :** aucun ; code réel partout. Les « vérifier par lecture » portent sur des noms de variables/config existants, pas du code à inventer.

**Cohérence bindings Task 1 ↔ Task 3 :** binding 10 (C++ count=4 ↔ GLSL `[4]`, write count=4 sur `imageInfos[10]`) ; binding 11 (C++ UNIFORM_BUFFER ↔ GLSL UBO, `range=272`). Pool : 14 image-samplers + 1 UBO ×maxFrames. ✅

**Types :** `ShadowUbo{ float lightViewProj[64]; float shadowParams[4]; }` (272 o, `static_assert`) ↔ GLSL `mat4 lightViewProj[4]; vec4 shadowParams;`. Remplissage `memcpy` colonne-major depuis `rs.cascades.lightViewProj[i].m`. ✅

**Risques :** (1) indexation sampler array → résolue par `SampleShadowDepth` à index constant ; (2) UBO per-frame indexé `setIdx` (= `frameIndex % m_maxFrames`), cohérent avec le set ; (3) std140 sans padding (verrouillé par `static_assert`) ; (4) régénération `.spv` à confirmer côté CI (Task 3/4) ; (5) noms config `m_cfg`/méthodes à confirmer (Task 2).
