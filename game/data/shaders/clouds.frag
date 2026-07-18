#version 450

// Nuages volumétriques ray-marchés (client). Passe GRAPHIQUE plein écran
// (fullscreen triangle partagé avec volumetric_fog.frag via lighting.vert).
// Pour chaque pixel : reconstruit le rayon caméra->pixel, ray-marche une dalle
// horizontale [baseAlt, topAlt], accumule densité (textures 3D Perlin-Worley
// pré-cuites, chantier ciel 2026-07-17 — ex-bruit FBM in-shader) et
// éclairage (Beer + Powder + phase Henyey-Greenstein vers le soleil), puis
// composite par-dessus la scène déjà brouillardée. Le depth scène borne la
// marche (un relief proche occulte les nuages).
//
// Entrées (descriptor set 0) :
//   binding 0 = scene color HDR post-fog (sampler linéaire clamp)
//   binding 1 = depth scene (D32_SFLOAT, sampler nearest clamp, lu en .r)
//   binding 2 = bruit 3D base 64³ (R=Perlin fBm, G/B/A=Worley 8/16/32,
//               sampler linéaire REPEAT) — chantier ciel 2026-07-17
//   binding 3 = bruit 3D détail 32³ (R/G/B=Worley 4/8/16, REPEAT)
//
// Sortie :
//   outColor (location 0) — scene + nuages composités.
//
// Push constants (192 octets, fragment) — cf. CloudPushConstants (CloudPass.h).

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uSceneColor; // HDR post-fog
layout(set = 0, binding = 1) uniform sampler2D uSceneDepth; // depth, .r = [0,1]
layout(set = 0, binding = 2) uniform sampler3D uNoiseBase;   // Perlin-Worley base
layout(set = 0, binding = 3) uniform sampler3D uNoiseDetail; // Worley détail

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
	vec4  shadowParams;  // x = force des ombres de nuages au sol [0..1] ; yzw réservés
} pc;

const float PI = 3.14159265358979;

// Remap borné [0,1] — dilatation de Schneider (Horizon Zero Dawn) et
// érosion de détail (chantier ciel 2026-07-17).
float remap01(float v, float lo, float hi)
{
	return clamp((v - lo) / max(hi - lo, 1e-5), 0.0, 1.0);
}

// Densité de nuage en un point monde p (chantier ciel 2026-07-17 : les
// textures 3D Perlin-Worley pré-cuites remplacent le value-noise FBM
// in-shader — cf. CloudNoiseGenerator.cpp).
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

	// Forme de base : texture 64³ tuilée sur ~5,2 km. R = Perlin fBm,
	// dilaté par le fBm de Worley (G/B/A) — la « dilatation Schneider »
	// donne des cumulus à bords nets là où le value-noise donnait des blobs.
	vec3 sp = (p + wind) / 5200.0;
	vec4 nb = texture(uNoiseBase, sp);
	float worleyFbm = nb.g * 0.625 + nb.b * 0.25 + nb.a * 0.125;
	float baseShape = remap01(nb.r, worleyFbm - 1.0, 1.0);

	// Seuil de couverture : coverage 0 -> ciel quasi vide, 1 -> couvert.
	// Plage choisie pour la distribution du baseShape remappé (moyenne ~0.5).
	// Fix crépuscule 2026-07-18 — variation basse fréquence anti-répétition :
	// vu de loin (rayons rasants), le motif 5,2 km se répétait visiblement
	// (retour utilisateur, capture crépuscule). On module le seuil par un
	// échantillon très basse fréquence (~tuile 40 km, décalé) : les paquets
	// de nuages varient d'une tuile à l'autre, la répétition disparaît.
	float coverage = pc.sunDir.w;
	float covVar = texture(uNoiseBase, sp * 0.13 + vec3(0.17, 0.0, 0.31)).g;
	float threshold = mix(0.72, 0.28, coverage) + (covVar - 0.5) * 0.16;
	float d = smoothstep(threshold, threshold + 0.14, baseShape);

	// Érosion de détail : texture 32³ tuilée sur ~800 m. Bords effilochés
	// (wispy) vers la base du nuage, cotonneux (billowy) vers le sommet.
	if (d > 0.001)
	{
		vec3 dp = (p + wind * 1.4) / 800.0;
		vec3 nd = texture(uNoiseDetail, dp).rgb;
		float detailFbm = nd.r * 0.625 + nd.g * 0.25 + nd.b * 0.125;
		float erode = mix(detailFbm, 1.0 - detailFbm, clamp(h * 4.0, 0.0, 1.0));
		d = remap01(d, erode * 0.4, 1.0);
	}

	return d * heightGrad * pc.sunColor.w * 1.5; // * density, gain d'opacité
}

void main()
{
	// Couleur de scène existante (déjà brouillardée par la passe fog amont).
	vec3 sceneCol = texture(uSceneColor, inUV).rgb;
	float depth   = texture(uSceneDepth, inUV).r;

	// Reconstruit le rayon monde de la caméra vers ce pixel (NDC xy [-1,1]).
	vec2 ndc = inUV * 2.0 - 1.0;
	vec4 farClip = pc.invViewProj * vec4(ndc, 1.0, 1.0);
	vec3 farWorld = farClip.xyz / farClip.w;
	vec3 ro = pc.cameraPos.xyz;
	vec3 rd = normalize(farWorld - ro);

	// --- Ombres de nuages au sol (Phase 2) ---
	// Pour les pixels de géométrie (depth < 1), marche le rayon SOLEIL à travers
	// la dalle de nuages depuis la position monde du sol et assombrit la scène.
	// Réutilise cloudDensity() (même champ de densité que la dalle vue).
	float shadowStrength = pc.shadowParams.x;
	if (depth < 1.0 && shadowStrength > 0.001)
	{
		vec4 gClipS = pc.invViewProj * vec4(ndc, depth, 1.0);
		vec3 P = gClipS.xyz / gClipS.w;
		vec3 sunS = normalize(pc.sunDir.xyz);
		if (sunS.y > 0.05) // soleil au-dessus de l'horizon
		{
			float baseA = pc.zenithColor.w;
			float topA  = pc.horizonColor.w;
			float tb = (baseA - P.y) / sunS.y;
			float tt = (topA  - P.y) / sunS.y;
			float te = max(min(tb, tt), 0.0);
			float tx = max(tb, tt);
			if (tx > te)
			{
				const int ss = 8;
				float sdt = (tx - te) / float(ss);
				float sum = 0.0;
				float st  = te;
				for (int i = 0; i < ss; ++i) { sum += cloudDensity(P + sunS * st) * sdt; st += sdt; }
				float shadowTrans = exp(-sum * 0.04);
				sceneCol *= mix(1.0, shadowTrans, shadowStrength);
			}
		}
	}

	// Intersection de la dalle horizontale [baseAlt, topAlt].
	float baseAlt = pc.zenithColor.w;
	float topAlt  = pc.horizonColor.w;
	if (abs(rd.y) < 1e-4)
	{
		// Rayon quasi-horizontal : pas d'intersection nette de la dalle.
		outColor = vec4(sceneCol, 1.0);
		return;
	}
	float tBase = (baseAlt - ro.y) / rd.y;
	float tTop  = (topAlt  - ro.y) / rd.y;
	float tEnter = min(tBase, tTop);
	float tExit  = max(tBase, tTop);
	tEnter = max(tEnter, 0.0);

	// Pas de dalle visible (rayon regarde le sol / dalle derrière la caméra).
	if (tExit <= tEnter)
	{
		outColor = vec4(sceneCol, 1.0);
		return;
	}

	// Limite par la géométrie : si du solide est présent (depth < 1), borne la
	// marche à sa distance (évite que les nuages couvrent un relief proche).
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
	if (tExit <= tEnter)
	{
		outColor = vec4(sceneCol, 1.0);
		return;
	}

	int   steps      = int(pc.stepParams.x);
	int   lightSteps = int(pc.stepParams.y);
	float dt         = (tExit - tEnter) / float(max(steps, 1));

	// Dithering pour casser le banding (cf. SSAO IGN, PR #851).
	float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
	float t = tEnter + dt * dither;

	vec3  sun = normalize(pc.sunDir.xyz);
	float g   = pc.windParams.w;
	float mu  = dot(rd, sun);
	// Phase Henyey-Greenstein.
	float hg = (1.0 - g * g) / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * mu, 1.5));

	float transmittance = 1.0;
	vec3  scattered     = vec3(0.0);

	// Fix crépuscule 2026-07-18 — facteur jour continu (1 = soleil haut,
	// 0 = nuit, transition douce autour de l'horizon). Le DayNightCycle
	// bascule sunColor vers ~0.02 dès le coucher alors que le ciel
	// analytique reste lumineux (crépuscule) : les nuages devenaient des
	// silhouettes NOIRES sur fond mauve (retour utilisateur, capture).
	float dayFactor = smoothstep(-0.08, 0.12, sun.y);
	// Couleur « soleil » effective : soleil le jour, clair de lune bleuté
	// la nuit (les nuages nocturnes restent lisibles, jamais noirs).
	vec3 sunCol = pc.sunColor.rgb * dayFactor
		+ vec3(0.10, 0.12, 0.18) * (1.0 - dayFactor);
	// Ambiant : moyenne des teintes ciel (déjà colorées par l'apparence météo)
	// + plancher crépuscule/nuit pour rester cohérent avec le fond analytique
	// encore lumineux après le coucher.
	vec3 skyAmb = mix(pc.horizonColor.rgb, pc.zenithColor.rgb, 0.5) * pc.stepParams.w;
	skyAmb = max(skyAmb, vec3(0.045, 0.05, 0.07) * (1.0 - dayFactor));

	for (int i = 0; i < steps; ++i)
	{
		vec3 p = ro + rd * t;
		float dens = cloudDensity(p);
		if (dens > 0.001)
		{
			// Transmittance vers le soleil (marche courte). Perf 2026-07-18 :
			// sautée la nuit (le soleil sous l'horizon n'éclaire plus la
			// dalle — la marche coûtait ~lightSteps échantillons par point
			// pour rien) et coupée dès que l'auto-ombrage sature (< 0.05).
			float lt = 0.0;
			float lightTrans = 1.0;
			float ldt = (topAlt - baseAlt) / float(max(lightSteps, 1));
			for (int j = 0; j < lightSteps && dayFactor > 0.02; ++j)
			{
				lt += ldt;
				float ld = cloudDensity(p + sun * lt);
				lightTrans *= exp(-ld * ldt * 0.02);
				if (lightTrans < 0.05) break;
			}
			// Powder (auto-ombrage des bords).
			float powder = 1.0 - exp(-dens * 2.0);
			// Éclairage : terme directionnel soleil = base lumineuse (0.8) + pic
			// Henyey-Greenstein vers le soleil, atténué par l'auto-ombrage vers le
			// soleil ; + ambiant ciel. La base évite les nuages gris/ternes (un
			// cumulus éclairé tend vers le blanc). hg ne fait que renforcer le pic.
			float sunPhase = 0.8 + 2.0 * hg;
			vec3 lightCol = sunCol * lightTrans * sunPhase * mix(0.7, 1.0, powder) + skyAmb;
			// Plafond de luminosité par échantillon : empêche les nuages de FLAMBER
			// en gros blobs blancs quand on regarde vers le soleil (rétro-éclairage +
			// diffusion avant). shadowParams.y = luminance max (0 = pas de plafond).
			if (pc.shadowParams.y > 0.0)
				lightCol = min(lightCol, vec3(pc.shadowParams.y));

			float aT = exp(-dens * dt * 0.05);
			scattered += transmittance * (1.0 - aT) * lightCol;
			transmittance *= aT;
			if (transmittance < 0.01) break;
		}
		t += dt;
	}

	// Atténuation par la distance (perspective aérienne) : les nuages lointains se
	// fondent dans le ciel au lieu de former un mur blanc opaque à l'horizon (un
	// rayon rasant traverse la fine dalle sur des km -> saturation). Basé sur tEnter
	// (distance au nuage le plus proche le long du rayon). shadowParams.z = distance
	// de début d'estompage (m) ; estompe entre fadeStart et 3x fadeStart ; 0 = off.
	float fadeStart = pc.shadowParams.z;
	float fade = (fadeStart > 0.0) ? (1.0 - smoothstep(fadeStart, fadeStart * 3.0, tEnter)) : 1.0;
	float opacity = (1.0 - transmittance) * fade;
	vec3 finalCol = sceneCol * (1.0 - opacity) + scattered * fade;
	outColor = vec4(finalCol, 1.0);
}
