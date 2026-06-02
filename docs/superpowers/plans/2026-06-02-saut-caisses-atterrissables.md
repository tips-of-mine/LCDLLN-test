# Saut plus haut & dessus de props atterrissables — Plan d'implémentation (Épic A)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Permettre de sauter plus haut (global) et d'atterrir/se tenir sur le dessus de tout prop solide (caisses incluses), pour ouvrir l'exploration de zones cachées.

**Architecture:** Deux changements client isolés — (1) relever `jumpSpeed` dans `CharacterController::Config` ; (2) faire émettre à `CompositeWorldCollider::SweepCapsule` un contact « sol » sur le dessus d'un `PropCylinder` pour les sweeps descendants de proximité (en préservant l'exclusion de la sonde anti-encastrement à 50 m). Plus une note de doc « caisses non-lootables ».

**Tech Stack:** C++ (engine maison), tests C++ « simple test » (exécutables `main()` renvoyant le nombre d'échecs), CMake + CTest. Build via CI uniquement (pas de toolchain locale). Spec : `docs/superpowers/specs/2026-06-02-saut-caisses-atterrissables-design.md`.

---

## Task 1 : Saut plus haut (clear caisse métal + 0,1 m)

**Files:**
- Modify: `src/client/gameplay/CharacterController.h` (struct `Config`, ~93-96)
- Test: `src/client/gameplay/tests/CharacterControllerJumpTests.cpp`
- Modify (commentaire): `src/CMakeLists.txt:756-759`

- [ ] **Step 1 : Écrire le test qui échoue (apex par défaut franchit une caisse).**

Ajouter dans `CharacterControllerJumpTests.cpp` (dans l'espace anonyme, après `Test_Jump_HigherSpeedGivesHigherApex`) :

```cpp
	// Le saut PAR DEFAUT doit franchir une caisse metal (~0.87 m) avec marge 0.1 m,
	// soit un apex >= 0.97 m. Garde le calibrage "sauter sur les caisses".
	void Test_Jump_DefaultClearsMetalCrate()
	{
		const float defaultSpeed = CharacterController::Config{}.jumpSpeed;
		const float apex = SimulateJumpApex(defaultSpeed);
		std::fprintf(stderr, "[INFO] apex(default=%.2f) = %.3f m (attendu >= 0.97)\n",
			defaultSpeed, apex);
		REQUIRE(apex >= 0.97f);   // 0.87 (caisse) + 0.10 (marge)
		REQUIRE(apex < 1.30f);    // borne haute : pas de saut absurde
	}
```

Et l'appeler dans `main()` :

```cpp
	Test_Jump_DefaultClearsMetalCrate();
```

- [ ] **Step 2 : (CI) Vérifier que le test échoue.**

Le test échoue tant que `jumpSpeed` par défaut = 4.9 (apex ~0.60 < 0.97).
Run (CI build-linux) : `ctest -R character_controller_jump_tests --output-on-failure`
Expected : FAIL sur `apex >= 0.97f`.

- [ ] **Step 3 : Relever `jumpSpeed` par défaut.**

Dans `src/client/gameplay/CharacterController.h`, remplacer le bloc commentaire + la valeur (lignes ~93-96) par :

```cpp
			// Saut : apex = jumpSpeed^2 / (2*|gravity|). Avec gravity=-20,
			// jumpSpeed=6.25 -> ~0.98 m. Calibre pour sauter SUR une "caisse metal"
			// (~0.87 m de haut) avec une marge de securite de +0.1 m. Le dessus des
			// props solides est atterrissable (cf. CompositeWorldCollider::SweepCapsule).
			// Historique : 9.0 (~2.0 m, irrealiste) -> 4.9 (~0.60 m) -> 6.25 (~0.98 m).
			float jumpSpeed = 6.25f;       ///< m/s, impulse applied when jumping
```

- [ ] **Step 4 : (CI) Vérifier que tous les tests de saut passent.**

Run : `ctest -R character_controller_jump_tests --output-on-failure`
Expected : PASS. (`Test_Jump_Apex_IsRealistic` et `Test_Jump_NotTheOldUnrealisticHeight`
passent toujours : ils appellent `SimulateJumpApex(4.9f)` explicitement.)

- [ ] **Step 5 : MAJ commentaire CMake.**

Dans `src/CMakeLists.txt:756-757`, remplacer :

```
  # Saut realiste : verifie l'apex (~0.60 m) du CharacterController apres le
  # passage jumpSpeed 9.0 -> 4.9. Garde anti-regression (pas de saut ~2 m).
```

par :

```
  # Saut : verifie l'apex par defaut (~0.98 m, jumpSpeed=6.25) qui franchit une
  # caisse metal (~0.87 m) + marge 0.1. Garde anti-regression (pas de saut ~2 m).
```

- [ ] **Step 6 : Commit.**

```bash
git add src/client/gameplay/CharacterController.h \
        src/client/gameplay/tests/CharacterControllerJumpTests.cpp \
        src/CMakeLists.txt
git commit -m "feat(gameplay): saut plus haut (apex ~0.98 m) pour monter sur les caisses"
```

---

## Task 2 : Dessus de prop atterrissable (`CompositeWorldCollider`)

**Files:**
- Modify: `src/client/gameplay/CompositeWorldCollider.cpp` (boucle des cylindres dans `SweepCapsule`)
- Test: `src/client/gameplay/tests/CompositeWorldColliderTests.cpp`
- Modify (commentaire): `src/CMakeLists.txt:761-765`

- [ ] **Step 1 : Écrire les tests qui échouent.**

Ajouter dans `CompositeWorldColliderTests.cpp`, dans `main()`, avant le bloc `// 5)` :

```cpp
	// 6) ATTERRISSAGE sur le DESSUS : sweep descendant court dont le bas de capsule
	//    (center.y - halfH) franchit topY de haut en bas, dans l'empreinte XZ.
	//    -> hit "sol" (normale +Y) au niveau topY.
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl); // topY = 3
		IWorldCollider::SweepHit hit;
		// halfH = 0.9 ; start bottom = 4.5-0.9 = 3.6 (>3) ; end bottom = 3.5-0.9 = 2.6 (<3).
		bool h = c.SweepCapsule(cap, Vec3{ 5, 4.5f, 0 }, Vec3{ 5, 3.5f, 0 }, hit);
		check(h && hit.hit, "dessus: hit attendu");
		check(hit.normal.y > 0.99f, "dessus: normale verticale (sol)");
		check(hit.fraction > 0.5f && hit.fraction < 0.7f, "dessus: fraction ~0.6");
	}

	// 7) Déjà posé sur le dessus (start bottom <= topY) : hit immédiat (frac ~0) ->
	//    le sticky ground probe garde le perso "grounded" sur la caisse.
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		// start bottom = 3.9-0.9 = 3.0 (== topY) ; end bottom = 3.8-0.9 = 2.9 (<3).
		bool h = c.SweepCapsule(cap, Vec3{ 5, 3.9f, 0 }, Vec3{ 5, 3.8f, 0 }, hit);
		check(h && hit.hit, "dessus pose: hit attendu");
		check(hit.fraction < 0.01f, "dessus pose: fraction ~0");
		check(hit.normal.y > 0.99f, "dessus pose: normale verticale");
	}

	// 8) Sweep descendant HORS empreinte XZ : pas de hit dessus (ni horizontal).
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 7, 4.5f, 0 }, Vec3{ 7, 3.5f, 0 }, hit);
		check(!h && !hit.hit, "dessus hors empreinte: pas de hit");
	}
```

Note : le test **4bis** existant (sweep de 50 m `Vec3{5,20,0} -> Vec3{5,1,0}` → pas de hit)
est la garde anti-régression du couvercle ; il doit continuer à passer grâce à la marge.

- [ ] **Step 2 : (CI) Vérifier que les nouveaux tests échouent.**

Run : `ctest -R composite_world_collider_tests --output-on-failure`
Expected : FAIL sur `dessus: hit attendu` (le couvercle n'existe pas encore).

- [ ] **Step 3 : Implémenter le couvercle.**

Dans `src/client/gameplay/CompositeWorldCollider.cpp`, à l'intérieur de la boucle
`for (const auto& c : m_cylinders)`, **tout au début du corps** (avant le calcul de
`R` et la garde de recouvrement vertical), insérer :

```cpp
		// Atterrissage sur le DESSUS du prop (couvercle). Le perso peut se poser et
		// se tenir sur tout prop solide. On ne traite QUE les sweeps descendants de
		// PROXIMITE : le bas de la capsule (center.y - halfH, meme convention que le
		// sol/terrain) franchit `topY` de haut en bas, a l'interieur de l'empreinte XZ.
		// GARDE ANTI-SONDE : on ignore les sweeps qui partent loin au-dessus du sommet
		// (ex. sonde anti-encastrement du CharacterController, depuis 50 m), pour
		// preserver "jamais de blocage vertical par un prop" hors atterrissage proche.
		{
			constexpr float kPropTopMargin = 4.0f; // >> demi-capsule, << 50 m (sonde)
			const bool descending = endCenter.y < startCenter.y;
			if (descending && (startCenter.y - c.topY) <= kPropTopMargin)
			{
				const float startBottom = startCenter.y - halfH;
				const float endBottom   = endCenter.y   - halfH;
				const float ex = endCenter.x - c.cx, ez = endCenter.z - c.cz;
				const bool insideXZ = (ex * ex + ez * ez) <= (c.radius * c.radius);
				// Franchissement de topY de haut en bas dans l'empreinte (mirroir du
				// sol plat a y=topY). startBottom <= topY => deja pose (frac 0).
				if (insideXZ && endBottom < c.topY && startBottom >= c.topY)
				{
					const float denom = startBottom - endBottom;
					float frac = (denom > 1e-8f) ? (startBottom - c.topY) / denom : 0.0f;
					if (frac < 0.0f) frac = 0.0f;
					if (frac > 1.0f) frac = 1.0f;
					if (frac < best.fraction)
					{
						best.hit = true;
						best.fraction = frac;
						best.normal = engine::math::Vec3{ 0.0f, 1.0f, 0.0f };
					}
					// On est au-dessus : ce cylindre ne bloque pas horizontalement cette
					// frame. Passer au cylindre suivant.
					continue;
				}
			}
		}
```

(`halfH` est déjà calculé juste avant la boucle : `const float halfH = capsule.height * 0.5f;`.)

- [ ] **Step 4 : (CI) Vérifier que tous les tests du collisionneur passent.**

Run : `ctest -R composite_world_collider_tests --output-on-failure`
Expected : PASS (cas 1-8 + 4bis/4ter + querywater). En particulier **4bis** (sweep 50 m)
reste sans hit (marge `kPropTopMargin` = 4 ≪ 17 = 20−3).

- [ ] **Step 5 : MAJ commentaire CMake.**

Dans `src/CMakeLists.txt:761-763`, remplacer :

```
  # Collision props : CompositeWorldCollider (terrain + cylindres verticaux).
  # Verifie sweep capsule-vs-cylindre (traversee bloquee, a-cote ignore, hors
  # bornes Y ignore, delegation QueryWater au terrain).
```

par :

```
  # Collision props : CompositeWorldCollider (terrain + cylindres verticaux).
  # Verifie sweep capsule-vs-cylindre (traversee bloquee, a-cote ignore, hors
  # bornes Y ignore, ATTERRISSAGE sur le dessus + anti-teleport sonde 50 m,
  # delegation QueryWater au terrain).
```

- [ ] **Step 6 : Commit.**

```bash
git add src/client/gameplay/CompositeWorldCollider.cpp \
        src/client/gameplay/tests/CompositeWorldColliderTests.cpp \
        src/CMakeLists.txt
git commit -m "feat(gameplay): dessus des props atterrissable (se tenir sur les caisses)"
```

---

## Task 3 : Documenter « caisses non-lootables »

**Files:**
- Modify: `CODEBASE_MAP.md` (section props / loot)

- [ ] **Step 1 : Trouver la section props/loot.**

Run (recherche) : repérer dans `CODEBASE_MAP.md` la section qui décrit les props
(`game/data/meshes/props/`) ou le loot (`GatheringSystem` / nœuds de récolte).

- [ ] **Step 2 : Ajouter la note.**

Insérer (à l'endroit pertinent) :

```markdown
> **Règle loot — caisses non-lootables.** Les caisses `Crate_Metal` et `Crate_Wooden`
> (`game/data/meshes/props/`) ne sont **pas** des emplacements de loot. Le loot reste
> réservé aux nœuds de récolte (`GatheringSystem`) — et, à terme, aux coffres dédiés
> (épic B « coffres à clé »). Ne pas associer de table de loot à ces props.
```

- [ ] **Step 3 : Commit.**

```bash
git add CODEBASE_MAP.md
git commit -m "docs: acter que les caisses (metal/bois) ne sont pas des spots de loot"
```

---

## Vérification finale (CI)

- [ ] Pousser la branche, ouvrir la PR, laisser tourner `build-windows` + `build-linux` (ctest).
- [ ] Confirmer dans les logs CI : `character_controller_jump_tests` et
      `composite_world_collider_tests` au vert.
- [ ] **Déploiement** : ✅ client uniquement, pas de redéploiement serveur (vérifié,
      cf. spec §8 : l'anti-cheat contrôle la vitesse 3D 19,5 m/s et le téléport 50 m,
      pas la hauteur de saut ; sprint+saut = 14,4 m/s < 19,5).

## Self-review (couverture spec)

- Spec §4.1 (saut) → Task 1. §4.2 (couvercle) → Task 2. §4.3 (doc loot) → Task 3.
- Spec §7 (tests) → tests dans Task 1 (apex défaut) et Task 2 (cas 6/7/8 + 4bis régression).
- Spec §8 (déploiement) → note de vérification finale.
- Pas de placeholder ; types/champs (`PropCylinder.topY`, `SweepHit.normal/fraction`,
  `Config.jumpSpeed`, `halfH`) cohérents avec le code existant lu.
