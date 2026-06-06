# Spec — Lumières ponctuelles (point lights)

**Date** : 2026-06-05
**Statut** : conception validée
**Origine** : audit du moteur (thème A « rebrancher le moteur »).
**Portée** : client uniquement. **Pas de redéploiement serveur.**
**Dépendance** : stackée sur le branchement CSM (`feat/csm-shadow-sampling` / PR #832) — utilise le binding 12 (après les 10/11 du CSM). À merger **après** #832.

---

## 1. Problème

`DynamicLightSystem` calcule chaque frame des lumières ponctuelles actives (torches, lampes, fenêtres) avec un cycle jour/nuit et un fondu, mais **`GetActiveLights()` n'a aucun consommateur côté rendu**. `lighting.frag` ne shade qu'un seul soleil directionnel → **aucune lumière de nuit n'est visible**. On consomme ces lumières dans le shading.

## 2. Acquis

- `ActivePointLight` (`DynamicLightSystem.h:33-39`) : `float position[3]` (monde, m), `float radius` (m), `float color[3]` (RGB **linéaire**), `float intensity` (= baseIntensity·fade). `GetActiveLights()` → `const std::vector<ActivePointLight>&`.
- `Tick(timeOfDay, dt)` reconstruit la liste chaque frame, allume la nuit avec fondu 60 s ; 0 lumière le jour (gating naturel).
- `lighting.frag` : BRDF Cook-Torrance réutilisable (`D_GGX`, `G_Smith`, `F_Schlick`) ; `F0/NdotV/V/albedo/metallic/roughness` déjà calculés avant le terme soleil (`Lo`, ~ligne 355).
- `LightingPass` : binding 12 libre ; pattern UBO host-visible par frame déjà en place (CSM `m_shadowUbo*`).

## 3. Conception

### 3.1 Transport — UBO binding 12
GLSL (std140) :
```glsl
struct PointLightStd140 { vec4 posRadius; vec4 colorIntensity; }; // 32 o
layout(set=0, binding=12) uniform PointLightUbo {
    uvec4 count;                  // x = nb actives [0..64]
    PointLightStd140 lights[64];
} uPoint;
```
C++ : `struct PointLightUbo { uint32_t count[4]; struct { float posRadius[4]; float colorIntensity[4]; } lights[64]; };` → **2064 o** (`static_assert`), < 16 Ko garanti. Un UBO host-visible par frame, calqué sur l'UBO cascades.

### 3.2 Shading — boucle dans `lighting.frag`
Après le terme soleil `Lo` (~ligne 355) et **avant l'ambient**, gated par `count>0` :
- pour chaque lumière : `Lp = pos - P` ; `dist = length(Lp)` ; skip si `dist > radius` ; `L = Lp/dist` ; même BRDF que le soleil (D/G/F) ; `Lo += (diffuse + specular) * radiance * NdotL`.
- `radiance = color * intensity * atten`.
- **Atténuation UE4 windowed** : `atten = sqr(saturate(1 - (d/r)^4)) / (d²+1)`. Coupure nette à `d=r` (cohérente avec le skip `dist>radius`).
- Les point lights ne reçoivent **pas** d'ombre (v1).

### 3.3 Sécurité thread — snapshot dans `RenderState`
Le rendu lit un snapshot `m_renderStates[readIdx]` découplé de l'update (raison pour laquelle `rs.cascades` est copié plutôt que lu en direct). `m_dynamicLights` n'est pas dans ce snapshot → le lire dans le lambda Lighting serait une **data race**. On ajoute donc un champ à `RenderState` (`std::vector<engine::render::ActivePointLight> pointLights;`) **rempli au moment du Tick / de l'assemblage du RenderState** (même endroit que `rs.cascades`). Le lambda Lighting lit `rs.pointLights` (sûr), clamp à 64, remplit l'UBO.

### 3.4 Intensité / sur-exposition
N lumières d'intensité ~2.0 sommées la nuit + auto-exposure HDR peuvent sur-exposer. On ajoute un facteur global **`world.point_lights.intensity_scale`** (config, défaut 1.0), appliqué côté Engine au remplissage (`colorIntensity.a = intensity * intensity_scale`). Plafond **64 lumières** (v1 sans clustering ; si dépassé, les 64 premières du JSON — documenté).

### 3.5 Câblage
- **LightingPass** : structs `PointLightStd140`/`PointLightUbo` (+ `static_assert 2064`) ; binding 12 (`UNIFORM_BUFFER`) dans le layout (tableau 12→13) ; poolSize UBO `+1×maxFrames` ; UBO host-visible par frame (calque CSM) ; param `const PointLightUbo&` à `Record` ; write binding 12 ; libération dans `Destroy`.
- **Engine** : `RenderState.pointLights` rempli au Tick ; dans le lambda Lighting, construire le `PointLightUbo` depuis `rs.pointLights` (clamp 64, scale d'intensité), passer à `Record`. `b.read` non requis (pas de ressource image ; UBO uniquement).
- **lighting.frag** : binding 12 + `PointAtten` + boucle d'accumulation.

## 4. Hors périmètre

- Clustering / tiling (optim pour beaucoup de lumières) — v2.
- Ombres des point lights (cube shadow maps) — non.
- Spots / area lights — non.

## 5. Validation

- **CI** : build Linux + Windows verts ; `lighting.frag` recompile en SPIR-V ; pas d'erreur de validation Vulkan (binding 12).
- **En jeu** (manuel) : la nuit, les sources (torches/lampes/fenêtres du `dynamic_lights.json`) éclairent leur entourage avec atténuation par distance ; le fondu jour/nuit fonctionne ; pas de sur-exposition (ajuster `intensity_scale`) ; le jour (0 lumière), rendu **identique** à avant.
- Aucune modif `frontFace`/`cullMode`/winding.

## 6. Risques

- **Cohérence binding 12 shader↔C++** (corruption sinon — figé ici).
- **std140** : `uvec4` + tableau de 2×`vec4` (stride 32) — pas de padding surprise (`static_assert 2064`).
- **Sur-exposition** : géré par `intensity_scale`, à régler en jeu.
- **Data race** : éliminée par le snapshot `RenderState`.
- Pas de toolchain locale : compile + validation en CI/VS.

**Déploiement** : ✅ client uniquement — pas de redéploiement serveur (rendu/shader ; `dynamic_lights.json` est du contenu client).
