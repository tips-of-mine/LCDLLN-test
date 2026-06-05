# Spec — Branchement des ombres soleil (CSM)

**Date** : 2026-06-05
**Statut** : conception validée
**Origine** : audit du moteur (`docs/audit/2026-06-05-moteur-rendu-client.md`, thème A « rebrancher le moteur »).
**Portée** : client uniquement (rendu Vulkan + shader). **Pas de redéploiement serveur.**

---

## 1. Problème

Les 4 cascades d'ombre (CSM) sont **rendues chaque frame** (`Engine.cpp` zone "ShadowMap", boucle ~5427-5452), et `CascadedShadowMaps::ComputeCascades` produit des matrices monde→ombre de bonne qualité (split pratique, stabilisation par snap texel). **Mais `lighting.frag` ne contient aucun échantillonnage d'ombre** : le terme soleil n'est jamais multiplié par une visibilité. Conséquence : **le soleil ne projette aucune ombre** sur le terrain ni les objets. On paie 4 rendus de depth utilisés seulement par DDGI et le brouillard volumique.

C'est le poste à plus fort impact visuel du moteur pour un effort modéré : tout est déjà calculé, il « suffit » de **consommer** les cascades dans le shading.

## 2. Acquis (rien à refaire)

- 4 shadow maps `m_fgShadowMapIds[0..3]` (images D32_SFLOAT, résolution `shadows.resolution`, défaut 1024), rendues chaque frame avec biais (`shadows.depth_bias_constant/slope`).
- `CascadesUniform` calculé dans `rs.cascades` : `Mat4 lightViewProj[4]` (monde→clip ombre, déjà en Z[0,1] Y-down via `OrthoVulkan`) + `float splitDepths[4]`.
- Reconstruction de la position monde `P` du fragment dans `lighting.frag` (depth + `invVP`).
- Terme soleil isolé `Lo` (`lighting.frag` ligne 292).
- Motif d'échantillonnage d'une cascade déjà présent dans `volumetric_fog.frag` (projection + compare manuel, profondeur Vulkan [0,1] sans remise à l'échelle) — à généraliser aux 4 cascades.
- Câblage de référence : `DDGI_Update` lit déjà la cascade 0 via `b.read(m_fgShadowMapIds[0])` + `reg.getImageView(...)` + un sampler.
- Compilation SPIR-V automatique en CI (`tools/compile_game_shaders.ps1`).

## 3. Conception

### 3.1 Sélection de cascade — par *containment*
On projette `P` dans l'espace de chaque cascade, de la 0 (plus haute résolution) à la 3, et on retient la **première** cascade où le fragment tombe dans le domaine valide (UV ∈ [0,1] et profondeur ∈ [0,1]). Avantage : n'utilise **que** `lightViewProj[i]` (déjà calculées), pas besoin de la profondeur-vue ni des `splitDepths` (en mètres, plus délicats). Robuste aux transitions.

### 3.2 Transport des données — UBO dédié (pas push-constant)
Le push-constant `LightParams` fait déjà **224 octets** ; y ajouter 4 `mat4` le pousserait à ~480 o et risquerait de dépasser `maxPushConstantsSize` sur du matériel modeste. On passe donc les cascades par un **UBO** dédié.

Layout (std140, set 0) :
- **binding 10** : `uniform sampler2D uShadowMaps[4];` (array de 4 samplers → les 4 images D32 séparées via 4 `VkDescriptorImageInfo`).
- **binding 11** : `uniform ShadowUbo { mat4 lightViewProj[4]; vec4 shadowParams; } uShadow;`
  - `shadowParams.x` = `useShadows` (0/1),
  - `shadowParams.y` = texel size = `1.0 / résolution` (pour le PCF),
  - `shadowParams.z` = biais de profondeur constant,
  - `shadowParams.w` = pente max du biais (slope, fonction de NdotL).
  - Taille : 4×64 + 16 = **272 octets**.

### 3.3 Échantillonnage — PCF 3×3 + anti-acné
- Compare manuel (sampler normal, comme `volumetric_fog`) : `shadowDepth = texture(uShadowMaps[c], uv).r ; lit = shadowDepth >= sampleDepth - bias`.
- **PCF 3×3** : moyenne de 9 taps espacés de `shadowParams.y` (texel). Donne un bord d'ombre doux.
- **Biais** : `bias = max(shadowParams.z, shadowParams.w * (1.0 - NdotL))` (slope-scaled) pour limiter le shadow-acne sur les pentes.
- Hors domaine de toutes les cascades → **éclairé** (visibility = 1).

### 3.4 Insertion dans le shading
`lighting.frag` ligne 292 :
```glsl
vec3 Lo = (diffuse + specular) * pc.lightColor.rgb * NdotL;
```
→ multiplier le terme **soleil direct uniquement** par la visibilité :
```glsl
float vis = (uShadow.shadowParams.x > 0.5) ? ShadowVisibility(P, NdotL) : 1.0;
vec3 Lo = (diffuse + specular) * pc.lightColor.rgb * NdotL * vis;
```
L'ambient/IBL/DDGI (lignes ~295-327) **n'est PAS ombré**.

### 3.5 Gating
`useShadows` (`shadowParams.x`) piloté par la config `shadows.enabled` (défaut **activé**) ET par la validité des shadow maps. Repli sûr « éclairé » sinon. Permet de désactiver les ombres sans recompiler.

### 3.6 Câblage Engine / LightingPass
- **LightingPass** : étendre le descriptor layout (ajouter binding 10 = sampler array ×4, binding 11 = UBO), agrandir le pool en conséquence, créer un **buffer UBO host-visible** (272 o, mis à jour chaque frame), ajouter à `Record` les 4 `VkImageView` shadow + un sampler + les données cascades (matrices + `shadowParams`), écrire les descripteurs.
- **Engine** (pass "Lighting", ~5664-5793) : ajouter `b.read(m_fgShadowMapIds[i])` pour i=0..3 (crée la dépendance FG ShadowMap→Lighting, comme DDGI pour la cascade 0), récupérer les vues via `reg.getImageView(...)`, remplir l'UBO depuis `rs.cascades.lightViewProj[i]` + `shadowParams` (résolution, biais, flag `shadows.enabled`), passer le tout à `Record`.
- Réutiliser le sampler shadow existant (pattern DDGI/LightingPass : sampler normal, `compareEnable=false`, clamp).

## 4. Hors périmètre

- IBL (irradiance/specular prefilter) — autre item « rebrancher le moteur », séparé.
- Lumières ponctuelles (clustered) — prochaine étape dédiée.
- Front-face culling anti-acné côté rendu depth (toggle `shadows.cull_front_faces` aujourd'hui ignoré dans `ShadowMapPass::Record`) — fix séparé seulement si l'acné persiste après le biais slope-scaled.

## 5. Validation

- **CI** : build Linux + Windows verts ; `lighting.frag` recompile en SPIR-V sans erreur ; pas d'erreur de validation Vulkan (descriptor layout cohérent shader↔C++).
- **En jeu** (validation manuelle, rendu non automatisable) :
  - le soleil projette des ombres portées nettes sur le terrain et les objets ;
  - pas de shadow-acne marqué (biais OK) ni de peter-panning excessif ;
  - transition entre cascades peu visible ;
  - `shadows.enabled=false` → plus d'ombres, scène toujours correcte (pas de crash, repli éclairé).
- **Convention `CLAUDE.md`** : aucune modification `frontFace`/`cullMode`/winding.

## 6. Risques

- **Cohérence descriptor layout shader↔C++** : les binding 10/11 doivent correspondre exactement entre `lighting.frag` et `LightingPass`. Risque d'erreur de validation Vulkan au runtime (non détecté par la compilation). Mitigation : numéros de binding figés dans ce spec.
- **std140** : `mat4 lightViewProj[4]` (4×64) + `vec4` est std140-propre (alignements 16) — pas de padding surprise.
- **Acné/peter-panning** : à régler par les biais ; le terrain est grand → tuning probable en validation.
- Pas de toolchain locale : compile + validation en CI / VS uniquement.

**Déploiement** : ✅ client uniquement — pas de redéploiement serveur (aucun opcode/handler/DB/protocole touché).
