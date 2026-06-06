# Spec — IBL par capture du ciel procédural (MVP statique)

**Date** : 2026-06-05
**Statut** : conception validée (option « capture ciel → IBL statique »)
**Origine** : audit du moteur (thème A « rebrancher le moteur », 3ᵉ item).
**Portée** : client uniquement (rendu Vulkan + shaders). **Pas de redéploiement serveur.**

---

## 1. Problème

`lighting.frag` contient déjà tout le shading **IBL split-sum** (diffuse via `irradianceMap`, spéculaire via `prefilterMap` + `brdfLut`), mais il est **désactivé** : `Engine.cpp:5760` code en dur `irrView = VK_NULL_HANDLE`, donc `useIBL = (irrView && prefilterView && brdfView)` vaut toujours 0. Conséquences :
- Les matériaux métalliques/brillants n'ont **aucune réflexion d'environnement**.
- L'ambiant est un terme constant plat (pas d'éclairage indirect directionnel).
- `BrdfLutPass` (binding 6) fonctionne mais est inutilisée ; `SpecularPrefilterPass` est `Init`'d mais son `Generate()` **n'est jamais appelé** (contenu non initialisé) ; **aucune passe d'irradiance n'existe**.

## 2. Acquis (rien à refaire)

- Shading split-sum complet (`lighting.frag:419-432`), mip count du prefilter lu dynamiquement (`textureQueryLevels`) → robuste à n'importe quel nombre de mips.
- Bindings 4 (irradiance, samplerCube), 5 (prefilter, samplerCube), 6 (brdfLut) + params `Record(irradianceView/Sampler, prefilterView/Sampler, brdfLutView/Sampler, …)` (`LightingPass.h:112-128`).
- `BrdfLutPass` : génère la LUT, `Generate()` appelée au boot, **bindée** (binding 6) — déjà fonctionnelle.
- `SpecularPrefilterPass` : `Init`'d (cube 256², 5 mips, RGBA16F), `Generate(sourceCubemapView, sampler)` **prête** mais jamais appelée.
- `SkyPass` + `sky.frag` : ciel procédural (couleur selon direction de vue + `DayNightCycle` : zénith/horizon/soleil/lune). Rendu aujourd'hui en **fullscreen dans GBufferA** (pas en cube).

## 3. Conception (MVP statique)

Pipeline de génération, exécuté **une fois au boot** (après que `DayNightCycle` et les assets ciel sont prêts) :

```
[1] SkyCubeCapturePass : rend le ciel procédural dans une cubemap RGBA16F (6 faces)
        ↓ (skyCube, samplable VIEW_TYPE_CUBE)
[2] SpecularPrefilterPass::Generate(skyCube)  → prefilter cube (256², 5 mips)   [EXISTE, à appeler]
[3] IrradiancePass (NOUVELLE) : convolution cosine → irradiance cube (petit, ex. 32²)
        ↓
[4] Engine : irrView = irradiance, prefilterView = prefilter, useIBL = 1 → LightingPass
```

### 3.1 SkyCubeCapturePass (nouveau)
- Cible : cubemap **RGBA16F**, **128²/face** (suffisant : l'irradiance est basse fréquence, le prefilter mip0 n'a pas besoin d'énorme résolution ; bon compromis coût/qualité).
- Rend `sky.frag` dans chaque face via 6 caméras (directions ±X/±Y/±Z, up conventionnel), en réutilisant la logique de couleur de ciel du `SkyPass` (couleur = f(direction monde, `DayNightCycle`)). **Respecter la convention `PerspectiveVulkan` (inversion Y, cf. `CLAUDE.md`)** pour ne pas obtenir des faces miroir.
- Le ciel n'a pas de géométrie : on rend un fullscreen-quad par face, la direction monde par texel étant dérivée de l'orientation de la face (ou via une petite passe compute écrivant directement la cube).
- **Décision d'implémentation** (à trancher dans le plan) : (a) render-pass 6 faces avec une matrice par face, ou (b) **compute shader** écrivant les 6 faces d'un coup (chaque thread → direction monde → couleur ciel). L'option compute est souvent plus simple (pas de render-pass cube, pas de winding) — **privilégiée** si la logique de `sky.frag` est facilement portable en compute.

### 3.2 SpecularPrefilterPass::Generate (existant, à appeler)
- Appeler `Generate(skyCubeView, skyCubeSampler)` après [1]. Produit le prefilter cube 5 mips (roughness par mip). **Aucun nouveau code** hormis l'appel + récupérer la vue résultat.
- ⚠️ Cette passe n'a **jamais tourné** : la valider (barrières, layouts, sampler mip linéaire). Risque latent à vérifier.

### 3.3 IrradiancePass (nouvelle)
- Convolution **cosine-weighted hemisphere** : pour chaque texel (direction N) de la cube d'irradiance, intégrer le ciel sur l'hémisphère autour de N. Compute shader `irradiance_convolve.comp` échantillonnant `skyCube` (N samples sur l'hémisphère, pondérés cos).
- Cible : cube **RGBA16F**, petite (**32²/face** : l'irradiance est très basse fréquence). 
- Sortie : `irradianceCubeView` (samplerCube) → binding 4.

### 3.4 Câblage Engine
- Au boot (nouvelle séquence, après init des passes IBL + sky + DayNightCycle) : exécuter [1]→[2]→[3] une fois, sur la queue graphics/compute (submit + wait, comme `BrdfLutPass::Generate`).
- Stocker `m_iblIrradianceView` / `m_iblPrefilterView` (+ samplers).
- Au site Lighting (`Engine.cpp:5760-5766`) : remplacer `irrView = VK_NULL_HANDLE` par `irrView = m_iblIrradianceView` ; `prefilterView` = la vraie prefilter générée ; `useIBL = (irr && prefilter && brdf) ? 1 : 0` (désormais satisfait).
- **Gating config** : `gi.ibl.enabled` (défaut **true**). Si false (ou si une génération échoue), repli sûr `useIBL=0` (ambient constant actuel) — aucun crash.

### 3.5 Statique (jour/nuit différé)
Génération **une seule fois au boot** (au `DayNightCycle` initial). Le suivi du cycle (re-capture quand le soleil bouge) est une **étape ultérieure séparée** : les réflexions refléteront l'heure du boot. Acceptable pour le MVP (gain visuel immédiat sur les matériaux brillants).

## 4. Hors périmètre

- Re-capture dynamique jour/nuit (follow-up).
- Cubemap HDR depuis disque + loader (option A écartée).
- Probes locales / réflexions planaires / SSR (déjà partiel pour l'eau).
- Parallax-corrected cubemaps.

## 5. Validation

- **CI** : build Linux + Windows verts ; nouveaux shaders compilés en SPIR-V ; pas d'erreur de validation Vulkan.
- **En jeu** (manuel) : un matériau métallique/brillant (ex. la caisse métal `Crate_Metal`, l'avatar) **reflète les teintes du ciel** ; l'ambiant devient directionnel (plus clair côté ciel) ; pas de NaN/artefact ; `gi.ibl.enabled=false` → revient à l'ambient plat sans crash.
- **Convention `CLAUDE.md`** : aucune modif `frontFace`/`cullMode` des pipelines existants ; si rendu cube par render-pass, respecter l'inversion Y.

## 6. Risques

- **`SpecularPrefilterPass::Generate` jamais exécutée** : bugs latents possibles (barrières/layouts/sampler). À valider en premier.
- **Orientation cube** : 6 faces mal orientées → réflexions miroir/inversées. Le chemin **compute** (écriture directe par direction monde) évite le winding ; préféré.
- **Gating strict `useIBL`** : exige les 3 vues non-NULL ; si l'irradiance ou le prefilter échoue, rester en repli (useIBL=0).
- **Coût boot** : 3 passes one-shot au démarrage (capture + 2 convolutions) — négligeable (statique).
- **Sur-luminosité** : l'IBL ajoute de l'ambient ; combiné à l'auto-exposition (déjà bornée, #834) → surveiller, ajuster l'intensité IBL si besoin (facteur optionnel).
- Pas de toolchain locale : compile + validation en CI/VS.

**Déploiement** : ✅ client uniquement — pas de redéploiement serveur (rendu/shaders ; aucun opcode/handler/DB/config serveur ; `gi.ibl.enabled` lu côté client).
