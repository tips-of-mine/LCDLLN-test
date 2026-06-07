#version 450

// Nuages volumétriques ray-marchés (client). Passe GRAPHIQUE plein écran
// (fullscreen triangle partagé avec volumetric_fog.frag via lighting.vert).
// Pour chaque pixel : reconstruit le rayon caméra->pixel, ray-marche une dalle
// horizontale [baseAlt, topAlt], accumule densité (bruit FBM in-shader) et
// éclairage (Beer + Powder + phase Henyey-Greenstein vers le soleil), puis
// composite par-dessus la scène déjà brouillardée. Le depth scène borne la
// marche (un relief proche occulte les nuages).
//
// Entrées (descriptor set 0) :
//   binding 0 = scene color HDR post-fog (sampler linéaire clamp)
//   binding 1 = depth scene (D32_SFLOAT, sampler nearest clamp, lu en .r)
//
// Sortie :
//   outColor (location 0) — scene + nuages composités.
//
// Push constants (192 octets, fragment) — cf. CloudPushConstants (CloudPass.h).

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uSceneColor; // HDR post-fog
layout(set = 0, binding = 1) uniform sampler2D uSceneDepth; // depth, .r = [0,1]

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
	// Échelle de bruit plus fine (0.0010 ~ flocons de ~1 km) : évite les 2 gros
	// blobs, donne plus de petits nuages découpés.
	vec3 sp = (p + wind) * 0.0010;

	float coverage = pc.sunDir.w;
	float base = fbm(sp);
	// Seuil placé dans la plage RÉELLE du FBM (moyenne ~0.48, max ~0.97) : un seuil
	// (1-coverage) brut donnait 0.75 à Clear -> densité nulle partout (nuages
	// invisibles). On remappe : coverage 0 -> seuil 0.55 (ciel quasi vide),
	// coverage 1 -> seuil 0.10 (couvert). smoothstep pour des bords doux + opacité visible.
	// Plage relevée (0.68..0.25) : à Clear (coverage~0.25) le seuil ~0.57 reste
	// au-dessus de la moyenne du FBM (~0.48) -> nuages ÉPARS avec du ciel entre eux
	// (et non un plafond couvrant). Storm (coverage~0.97) -> seuil ~0.26 -> couvert.
	float threshold = mix(0.68, 0.25, coverage);
	float d = smoothstep(threshold, threshold + 0.18, base);

	// Érosion de détail haute fréquence sur les bords.
	float detail = fbm(sp * 4.0 + wind * 0.01);
	d = max(d - detail * 0.2 * (1.0 - coverage), 0.0);

	return d * heightGrad * pc.sunColor.w * 1.2; // * density, gain d'opacité (plus translucide)
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

	vec3 sunCol = pc.sunColor.rgb;
	// Ambiant : moyenne des teintes ciel (déjà colorées par l'apparence météo).
	vec3 skyAmb = mix(pc.horizonColor.rgb, pc.zenithColor.rgb, 0.5) * pc.stepParams.w;

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
			// Éclairage : terme directionnel soleil = base lumineuse (0.8) + pic
			// Henyey-Greenstein vers le soleil, atténué par l'auto-ombrage vers le
			// soleil ; + ambiant ciel. La base évite les nuages gris/ternes (un
			// cumulus éclairé tend vers le blanc). hg ne fait que renforcer le pic.
			float sunPhase = 0.6 + 1.6 * hg;
			vec3 lightCol = sunCol * lightTrans * sunPhase * mix(0.7, 1.0, powder) + skyAmb;

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
