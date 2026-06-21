# Collision bâtiments par catalogue de boîtes — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remplacer le gros cylindre englobant par pièce de bâtiment par une boîte orientée fine (mur réel) + un catalogue par mesh, pour des murs qui bloquent précisément et des portes franchissables.

**Architecture:** Nouveau primitif `PropBox` (rectangle orienté XZ + bornes Y) dans `CompositeWorldCollider`, à côté de `PropCylinder` ; un `BuildingCollisionCatalog` (JSON via `engine::core::Config`) mappe `basename(mesh)` → `passable` ou liste de boîtes locales ; `Engine::BuildPropFromMeshMatrix` consulte le catalogue (fallback cylindre actuel si mesh absent → rétro-compatible).

**Tech Stack:** C++17, `engine::core::Config` (parse JSON → clés pointées), CMake `lcdlln_add_simple_test`, ctest (CI Linux). Pas de toolchain locale : compilation/tests via CI.

**Base de branche :** ce chantier DÉPEND de #919 (flag `PropCylinder.wall`). Baser la branche sur `main` une fois #919 mergé, OU rebaser `claude/building-collision-catalog` sur `origin/main` après merge de #919. Vérifier avant de commencer : `git show HEAD:src/client/gameplay/CompositeWorldCollider.h | grep -c "bool wall"` doit renvoyer `1`.

**Conventions repo :** PascalCase pour nouveaux fichiers/classes ; commentaires en français ; nouveaux tests enregistrés via `lcdlln_add_simple_test` dans `src/CMakeLists.txt`.

---

## Structure des fichiers

- `src/client/gameplay/CompositeWorldCollider.h` (modifié) — ajoute `struct PropBox`, `m_boxes`, `AddBox`/`ClearBoxes`.
- `src/client/gameplay/CompositeWorldCollider.cpp` (modifié) — sweep capsule-vs-`PropBox`.
- `src/client/gameplay/tests/CompositeWorldColliderTests.cpp` (modifié) — tests boîte + mur-à-porte.
- `src/client/world/BuildingCollisionCatalog.h` + `.cpp` (créés) — chargeur catalogue + `Lookup`.
- `src/client/world/tests/BuildingCollisionCatalogTests.cpp` (créé) — tests chargeur.
- `src/client/app/Engine.cpp` (modifié) — branchement dans `BuildPropFromMeshMatrix` + chargement du catalogue au boot.
- `src/client/app/Engine.h` (modifié) — membre `BuildingCollisionCatalog m_buildingCollisionCatalog`.
- `game/data/collision/building_pieces.json` (créé) — données catalogue (auberge).
- `src/CMakeLists.txt` (modifié) — enregistre les 2 (nouveaux) exécutables de test + ajoute `BuildingCollisionCatalog.cpp` à la cible client.

---

## Task 1 : Primitif `PropBox` + sweep capsule-vs-boîte

**Files:**
- Modify: `src/client/gameplay/CompositeWorldCollider.h`
- Modify: `src/client/gameplay/CompositeWorldCollider.cpp`
- Test: `src/client/gameplay/tests/CompositeWorldColliderTests.cpp`

- [ ] **Step 1 : Écrire les tests qui échouent** — ajouter, dans `CompositeWorldColliderTests.cpp`, AVANT la section `// 5) QueryWater` :

```cpp
	// === PropBox : boîte orientée (mur de bâtiment) ===
	// Boîte centrée en (5,0), demi-dim (1.0 en X, 0.1 en Z) -> mur fin orienté
	// selon les axes monde, de Y=0 à Y=3.
	auto makeWall = []() {
		PropBox b;
		b.cx = 5.0f; b.cz = 0.0f; b.halfX = 1.0f; b.halfZ = 0.1f;
		b.axisX = Vec3{ 1, 0, 0 }; b.axisZ = Vec3{ 0, 0, 1 };
		b.loY = 0.0f; b.hiY = 3.0f; b.wall = true;
		return b;
	};

	// B1) Capsule traversant la FACE large du mur (le long de +X, à z=0) : bloquée,
	//     normale horizontale (selon -X, face d'approche).
	{
		CompositeWorldCollider c(&terrain); c.AddBox(makeWall());
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, hit);
		check(h && hit.hit, "box: face -> hit");
		check(std::fabs(hit.normal.y) < 1e-3f, "box: normale horizontale");
		// Contact attendu : x = 5 - halfX(1.0) - r(0.3) = 3.7 -> fraction ~0.37.
		check(hit.fraction > 0.30f && hit.fraction < 0.45f, "box: fraction plausible");
	}

	// B2) Capsule passant à CÔTÉ du mur fin (à z=2, au-delà de halfZ+r=0.4) : pas de hit.
	{
		CompositeWorldCollider c(&terrain); c.AddBox(makeWall());
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 2 }, Vec3{ 10, 1, 2 }, hit);
		check(!h, "box: a cote (hors epaisseur) -> pas de hit");
	}

	// B3) Capsule AU-DESSUS (Y=10, hors [loY,hiY]) : pas de hit.
	{
		CompositeWorldCollider c(&terrain); c.AddBox(makeWall());
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 10, 0 }, Vec3{ 10, 10, 0 }, hit);
		check(!h, "box: au-dessus -> pas de hit");
	}

	// B4) Boîte passable : aucune collision.
	{
		PropBox p = makeWall(); p.passable = true;
		CompositeWorldCollider c(&terrain); c.AddBox(p);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, hit);
		check(!h, "box passable: aucune collision");
	}

	// B5) Boîte tournée de 90° (axisX <-> axisZ) : un mur fin orienté selon Z.
	//     halfX=1.0 le long de axisX=(0,0,1), halfZ=0.1 le long de axisZ=(1,0,0).
	//     Donc fin en X (epaisseur 0.1), large en Z. Une capsule le long de +X
	//     à z=0 traverse la fine épaisseur -> hit.
	{
		PropBox b = makeWall();
		b.axisX = Vec3{ 0, 0, 1 }; b.axisZ = Vec3{ 1, 0, 0 };
		CompositeWorldCollider c(&terrain); c.AddBox(b);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, hit);
		check(h && hit.hit, "box tournee: traverse l'epaisseur -> hit");
		// Epaisseur le long de X = halfZ(0.1)+r(0.3)=0.4 -> contact x~4.6 -> frac~0.46.
		check(hit.fraction > 0.40f && hit.fraction < 0.52f, "box tournee: fraction plausible");
	}
```

- [ ] **Step 2 : Lancer pour vérifier l'échec de compilation** — `PropBox`/`AddBox` n'existent pas encore.
  Commande (CI) : pousser la branche ; le job `build-linux` doit échouer à la compilation de `CompositeWorldColliderTests.cpp` (`PropBox` non déclaré). *(Sans toolchain locale : l'échec est constaté en CI ; on enchaîne directement Step 3 dans le même commit.)*

- [ ] **Step 3 : Implémenter `PropBox` dans le header** — dans `CompositeWorldCollider.h`, après la fin de `struct PropCylinder { … };`, ajouter :

```cpp
	/// Boîte de collision orientée pour une pièce de bâtiment (mur, jambage, linteau).
	/// Empreinte = rectangle dans le plan XZ orienté par (axisX, axisZ) unitaires
	/// monde, demi-dimensions (halfX, halfZ) ; bornée en Y par [loY, hiY]. Le sweep
	/// capsule fait un test rectangle-orienté-vs-cercle(rayon capsule) dans XZ +
	/// recouvrement vertical, calqué sur PropCylinder. Pas de dessus marchable
	/// (wall=true) : une boîte de mur ne fait que bloquer latéralement (cf. #919).
	struct PropBox
	{
		float cx = 0.0f, cz = 0.0f;          ///< centre XZ monde (m)
		float halfX = 0.5f, halfZ = 0.1f;    ///< demi-dimensions du rectangle (m), > 0
		engine::math::Vec3 axisX{ 1, 0, 0 }; ///< axe « largeur » monde (unitaire, XZ)
		engine::math::Vec3 axisZ{ 0, 0, 1 }; ///< axe « épaisseur » monde (unitaire, XZ)
		float loY = 0.0f, hiY = 2.0f;        ///< bornes Y monde [bas, haut]
		bool passable = false; ///< aucune collision (battant de porte)
		bool stair = false;    ///< gravissable (cf. CharacterController)
		bool wall = true;      ///< barrière latérale pure, pas de dessus marchable
	};
```

  Puis, dans `class CompositeWorldCollider`, à côté de `AddCylinder`/`ClearCylinders`, ajouter :

```cpp
		void AddBox(const PropBox& b) { m_boxes.push_back(b); }
		void ClearBoxes() { m_boxes.clear(); }
		std::size_t BoxCount() const { return m_boxes.size(); }
```

  et, dans la section `private:`, à côté de `std::vector<PropCylinder> m_cylinders;`, ajouter :

```cpp
		std::vector<PropBox> m_boxes;
```

- [ ] **Step 4 : Implémenter le sweep boîte dans le `.cpp`** — dans `CompositeWorldCollider::SweepCapsule`, JUSTE AVANT `outHit = best;` (après la boucle `for (const auto& c : m_cylinders)`), ajouter la boucle des boîtes :

```cpp
		// 3) Boîtes orientées de props (murs/jambages/linteaux de bâtiment).
		//    Même découpage que les cylindres : recouvrement Y + empreinte XZ, mais
		//    l'empreinte est un rectangle orienté au lieu d'un disque.
		for (const auto& b : m_boxes)
		{
			if (b.passable) continue;

			// Recouvrement vertical (identique aux cylindres).
			const float capLo = endCenter.y - halfH - capsule.radius;
			const float capHi = endCenter.y + halfH + capsule.radius;
			if (capHi < b.loY || capLo > b.hiY - kStandSkin) continue;

			// Projection du départ et du déplacement XZ dans le repère du rectangle.
			const float r = capsule.radius;
			const float rx = sx - b.cx, rz = sz - b.cz;
			const float u0 = rx * b.axisX.x + rz * b.axisX.z; // le long de axisX
			const float v0 = rx * b.axisZ.x + rz * b.axisZ.z; // le long de axisZ
			const float du = dx * b.axisX.x + dz * b.axisX.z;
			const float dv = dx * b.axisZ.x + dz * b.axisZ.z;

			// AABB 2D élargi du rayon capsule (Minkowski cercle-vs-rectangle).
			const float ex = b.halfX + r;
			const float ez = b.halfZ + r;

			// Slab test 2D (ray (u0,v0)+t(du,dv) vs [-ex,ex]x[-ez,ez]).
			float tEnter = 0.0f, tExit = 1.0f;
			int enterAxis = -1;     // 0 = axe u (axisX), 1 = axe v (axisZ)
			float enterSign = 0.0f; // signe de la face d'entrée
			bool separated = false;

			// Lambda slab sur un axe : met à jour tEnter/tExit/enterAxis.
			auto slab = [&](float p, float d, float e, int axis) {
				if (std::fabs(d) < 1e-8f) { if (p < -e || p > e) separated = true; return; }
				float t1 = (-e - p) / d, t2 = (e - p) / d;
				float sgn = -1.0f; // face -e si d>0 (on entre par -e)
				if (t1 > t2) { const float tmp = t1; t1 = t2; t2 = tmp; sgn = 1.0f; }
				if (t1 > tEnter) { tEnter = t1; enterAxis = axis; enterSign = sgn; }
				if (t2 < tExit) tExit = t2;
			};
			slab(u0, du, ex, 0);
			slab(v0, dv, ez, 1);

			if (separated || tEnter > tExit || tEnter > 1.0f) continue;
			float tHit = tEnter < 0.0f ? 0.0f : tEnter; // déjà à l'intérieur -> bloque à 0
			if (tHit >= best.fraction) continue;

			best.hit = true;
			best.fraction = tHit;
			best.stair = b.stair;
			// Normale = face d'entrée, exprimée en monde (axe XZ correspondant).
			engine::math::Vec3 n{ 1, 0, 0 };
			if (enterAxis == 0) n = engine::math::Vec3{ enterSign * b.axisX.x, 0.0f, enterSign * b.axisX.z };
			else if (enterAxis == 1) n = engine::math::Vec3{ enterSign * b.axisZ.x, 0.0f, enterSign * b.axisZ.z };
			else { // déjà à l'intérieur (tEnter<=0) : normale = sortie la plus proche en u.
				n = engine::math::Vec3{ (u0 >= 0.0f ? b.axisX.x : -b.axisX.x), 0.0f, (u0 >= 0.0f ? b.axisX.z : -b.axisX.z) };
			}
			const float nlen = std::sqrt(n.x * n.x + n.z * n.z);
			best.normal = nlen > 1e-6f ? engine::math::Vec3{ n.x / nlen, 0.0f, n.z / nlen }
			                           : engine::math::Vec3{ 1.0f, 0.0f, 0.0f };
		}
```

  *(Note : `<cmath>` est déjà inclus dans ce fichier.)*

- [ ] **Step 5 : Lancer les tests** — pousser ; `build-linux` compile et `composite_world_collider_tests` PASSE (B1–B5 + tests existants). Sortie attendue : `CompositeWorldColliderTests: OK`.

- [ ] **Step 6 : Commit**

```bash
git add src/client/gameplay/CompositeWorldCollider.h src/client/gameplay/CompositeWorldCollider.cpp src/client/gameplay/tests/CompositeWorldColliderTests.cpp
git commit -m "feat(gameplay): primitif PropBox (boite orientee) + sweep capsule"
```

---

## Task 2 : Cas mur-à-porte (jambages + linteau, embrasure franchissable)

**Files:**
- Test: `src/client/gameplay/tests/CompositeWorldColliderTests.cpp`

- [ ] **Step 1 : Écrire le test** — ajouter, après le bloc B5 :

```cpp
	// B6) MUR À PORTE — 3 boîtes (jambage gauche, jambage droit, linteau) laissant
	//     une embrasure centrale libre. Le mur est à x=5, orienté face selon X.
	//     Jambages : largeur (en Z) 0.4 chacun, à z=±0.75 ; épaisseur (en X) 0.2.
	//     Linteau : au-dessus de la porte (loY=2.1), couvre toute la largeur en Z.
	//     L'embrasure = bande z ∈ [-0.35, 0.35], y < 2.1.
	{
		auto jamb = [](float zc) {
			PropBox b; b.cx = 5.0f; b.cz = zc; b.halfX = 0.1f; b.halfZ = 0.2f;
			b.axisX = Vec3{ 1,0,0 }; b.axisZ = Vec3{ 0,0,1 }; b.loY = 0.0f; b.hiY = 3.0f;
			return b;
		};
		PropBox lintel; lintel.cx = 5.0f; lintel.cz = 0.0f; lintel.halfX = 0.1f; lintel.halfZ = 1.0f;
		lintel.axisX = Vec3{ 1,0,0 }; lintel.axisZ = Vec3{ 0,0,1 }; lintel.loY = 2.1f; lintel.hiY = 3.0f;

		CompositeWorldCollider c(&terrain);
		c.AddBox(jamb(-0.75f)); c.AddBox(jamb(0.75f)); c.AddBox(lintel);

		// Traverser l'EMBRASURE (z=0, hauteur perso ~1.8 < linteau 2.1) : pas de hit.
		IWorldCollider::SweepHit through;
		bool ht = c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, through);
		check(!ht, "porte: on traverse l'embrasure (pas de hit)");

		// Viser un JAMBAGE (z=0.75) : bloqué.
		IWorldCollider::SweepHit onJamb;
		bool hj = c.SweepCapsule(cap, Vec3{ 0, 1, 0.75f }, Vec3{ 10, 1, 0.75f }, onJamb);
		check(hj && onJamb.hit, "porte: on bute un jambage (hit)");
	}
```

- [ ] **Step 2 : Lancer** — aucune nouvelle implémentation requise (Task 1 couvre le sweep). Pousser ; `composite_world_collider_tests` PASSE, incluant B6. *(Si B6 échoue, c'est un bug du sweep Task 1 à corriger — typiquement le recouvrement Y du linteau ou la projection ; vérifier que la capsule de 1.8 m passe bien sous loY=2.1.)*

- [ ] **Step 3 : Commit**

```bash
git add src/client/gameplay/tests/CompositeWorldColliderTests.cpp
git commit -m "test(gameplay): mur-a-porte = embrasure franchissable, jambage bloque"
```

---

## Task 3 : `BuildingCollisionCatalog` (chargeur JSON + Lookup)

**Files:**
- Create: `src/client/world/BuildingCollisionCatalog.h`
- Create: `src/client/world/BuildingCollisionCatalog.cpp`
- Test: `src/client/world/tests/BuildingCollisionCatalogTests.cpp`
- Modify: `src/CMakeLists.txt`

Format JSON (clés pointées, compatible `engine::core::Config`) ; clé pièce = basename mesh **en minuscules**. Chaque pièce a soit `passable=true`, soit `box_count` + `box_<i>` (boîtes en **espace local du mesh** : centre/demi-dim 3D) :

```json
{
  "version": 1,
  "pieces": {
    "door_1_flat": { "passable": true },
    "wall_plaster_straight": {
      "box_count": 1,
      "box_0": { "cx": 0.0, "cy": 1.5, "cz": 0.0, "hx": 1.0, "hy": 1.5, "hz": 0.1 }
    }
  }
}
```

- [ ] **Step 1 : Écrire les tests** — créer `src/client/world/tests/BuildingCollisionCatalogTests.cpp` :

```cpp
#include "src/client/world/BuildingCollisionCatalog.h"

#include <cmath>
#include <cstdio>

using engine::world::BuildingCollisionCatalog;

namespace { int g_fail = 0; void check(bool c, const char* m){ if(!c){ std::printf("FAIL: %s\n", m); ++g_fail; } } }

int main()
{
	const char* json = R"({
		"pieces": {
			"door_1_flat": { "passable": true },
			"wall_plaster_straight": {
				"box_count": 1,
				"box_0": { "cx": 0.0, "cy": 1.5, "cz": 0.0, "hx": 1.0, "hy": 1.5, "hz": 0.1 }
			},
			"wall_plaster_door_flat": {
				"box_count": 2,
				"box_0": { "cx": -0.8, "cy": 1.5, "cz": 0.0, "hx": 0.2, "hy": 1.5, "hz": 0.1 },
				"box_1": { "cx":  0.8, "cy": 1.5, "cz": 0.0, "hx": 0.2, "hy": 1.5, "hz": 0.1 }
			}
		}
	})";

	BuildingCollisionCatalog cat;
	check(cat.LoadFromJson(json), "load: ok");

	// 1) passable
	{
		const auto* e = cat.Lookup("Door_1_Flat"); // casse d'origine -> normalisée
		check(e != nullptr, "door: present");
		check(e && e->passable, "door: passable");
		check(e && e->boxes.empty(), "door: pas de boites");
	}
	// 2) une boîte
	{
		const auto* e = cat.Lookup("wall_plaster_straight");
		check(e != nullptr && !e->passable, "wall: present non passable");
		check(e && e->boxes.size() == 1, "wall: 1 boite");
		check(e && std::fabs(e->boxes[0].hx - 1.0f) < 1e-4f, "wall: hx=1.0");
		check(e && std::fabs(e->boxes[0].cy - 1.5f) < 1e-4f, "wall: cy=1.5");
	}
	// 3) multi-boîtes
	{
		const auto* e = cat.Lookup("Wall_Plaster_Door_Flat");
		check(e != nullptr && e->boxes.size() == 2, "doorwall: 2 boites");
	}
	// 4) mesh absent -> nullptr (fallback cylindre)
	check(cat.Lookup("Tree_Oak") == nullptr, "absent: nullptr");

	// 5) JSON invalide -> LoadFromJson renvoie false
	{
		BuildingCollisionCatalog bad;
		check(!bad.LoadFromJson("{ ceci n'est pas du json"), "invalide: load echoue");
	}

	if (g_fail == 0) std::printf("BuildingCollisionCatalogTests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
```

- [ ] **Step 2 : Créer le header** — `src/client/world/BuildingCollisionCatalog.h` :

```cpp
#pragma once

#include "src/shared/core/Config.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::world
{
	/// Catalogue de collision des pièces de bâtiment, indexé par BASENAME de mesh
	/// (sans dossier ni extension), insensible à la casse. Chargé depuis un JSON
	/// (game/data/collision/building_pieces.json) via engine::core::Config.
	/// Une pièce est soit « passable » (aucune collision : battant de porte), soit
	/// décrite par une liste de boîtes en ESPACE LOCAL du mesh.
	class BuildingCollisionCatalog
	{
	public:
		/// Boîte de collision en espace local du mesh (centre + demi-dimensions, m).
		struct LocalBox { float cx, cy, cz, hx, hy, hz; };

		/// Résultat de Lookup pour une pièce présente au catalogue.
		struct Piece { bool passable = false; std::vector<LocalBox> boxes; };

		/// Charge le catalogue depuis le texte JSON. \return false si parse invalide.
		bool LoadFromJson(const std::string& jsonText);

		/// Renvoie la pièce si \p meshBaseName est au catalogue, sinon nullptr
		/// (l'appelant retombe alors sur la collision cylindre par défaut).
		/// \p meshBaseName : basename sans extension ; la casse est ignorée.
		const Piece* Lookup(std::string_view meshBaseName) const;

	private:
		engine::core::Config m_cfg;
		bool m_loaded = false;
		// Cache des pièces déjà résolues (clé = basename minuscule).
		mutable std::unordered_map<std::string, Piece> m_cache;
	};
}
```

- [ ] **Step 3 : Créer le .cpp** — `src/client/world/BuildingCollisionCatalog.cpp` :

```cpp
#include "src/client/world/BuildingCollisionCatalog.h"

#include <cctype>

namespace engine::world
{
	namespace
	{
		std::string ToLowerAscii(std::string_view s)
		{
			std::string o; o.reserve(s.size());
			for (char ch : s) o.push_back((ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch);
			return o;
		}
	}

	bool BuildingCollisionCatalog::LoadFromJson(const std::string& jsonText)
	{
		m_cache.clear();
		m_loaded = m_cfg.LoadFromString(jsonText);
		return m_loaded;
	}

	const BuildingCollisionCatalog::Piece* BuildingCollisionCatalog::Lookup(std::string_view meshBaseName) const
	{
		if (!m_loaded) return nullptr;
		const std::string key = ToLowerAscii(meshBaseName);
		auto it = m_cache.find(key);
		if (it != m_cache.end()) return it->second.boxes.empty() && !it->second.passable ? nullptr : &it->second;

		const std::string base = "pieces." + key + ".";
		const bool passable = m_cfg.GetBool(base + "passable", false);
		const int count = static_cast<int>(m_cfg.GetInt(base + "box_count", -1));
		if (!passable && count < 0)
			return nullptr; // absent du catalogue

		Piece p;
		p.passable = passable;
		for (int i = 0; i < count; ++i)
		{
			const std::string b = base + "box_" + std::to_string(i) + ".";
			LocalBox lb{};
			lb.cx = static_cast<float>(m_cfg.GetDouble(b + "cx", 0.0));
			lb.cy = static_cast<float>(m_cfg.GetDouble(b + "cy", 0.0));
			lb.cz = static_cast<float>(m_cfg.GetDouble(b + "cz", 0.0));
			lb.hx = static_cast<float>(m_cfg.GetDouble(b + "hx", 0.0));
			lb.hy = static_cast<float>(m_cfg.GetDouble(b + "hy", 0.0));
			lb.hz = static_cast<float>(m_cfg.GetDouble(b + "hz", 0.0));
			if (lb.hx > 0.0f && lb.hy > 0.0f && lb.hz > 0.0f)
				p.boxes.push_back(lb);
		}
		auto [ins, ok] = m_cache.emplace(key, std::move(p));
		return &ins->second;
	}
}
```

  *(Ajouter `#include <unordered_map>` au header si non transitif : il l'est via Config.h, mais l'ajouter explicitement par sûreté.)*

- [ ] **Step 4 : Enregistrer le test dans CMake** — dans `src/CMakeLists.txt`, après le bloc `lcdlln_add_simple_test(composite_world_collider_tests …)`, ajouter :

```cmake
  # Catalogue de collision des bâtiments : parse JSON (via Config), passable,
  # multi-boîtes, mesh absent -> nullptr, JSON invalide -> echec.
  lcdlln_add_simple_test(building_collision_catalog_tests
    ${CMAKE_SOURCE_DIR}/src/client/world/BuildingCollisionCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/client/world/tests/BuildingCollisionCatalogTests.cpp)
```

  *(Note : `lcdlln_add_simple_test` lie déjà `engine_core` qui fournit `Config` ; on ajoute le `.cpp` du catalogue car il n'est pas encore dans une cible.)*

- [ ] **Step 5 : Lancer** — pousser ; `build-linux` compile et `building_collision_catalog_tests` PASSE → `BuildingCollisionCatalogTests: OK`.

- [ ] **Step 6 : Commit**

```bash
git add src/client/world/BuildingCollisionCatalog.h src/client/world/BuildingCollisionCatalog.cpp src/client/world/tests/BuildingCollisionCatalogTests.cpp src/CMakeLists.txt
git commit -m "feat(world): BuildingCollisionCatalog (chargeur JSON + Lookup par mesh)"
```

---

## Task 4 : Brancher le catalogue dans `BuildPropFromMeshMatrix`

**Files:**
- Modify: `src/client/app/Engine.h`
- Modify: `src/client/app/Engine.cpp`
- Modify: `src/CMakeLists.txt` (ajouter `BuildingCollisionCatalog.cpp` à la cible client si pas déjà lié)

- [ ] **Step 1 : Membre catalogue + include** — dans `src/client/app/Engine.h`, ajouter l'include `#include "src/client/world/BuildingCollisionCatalog.h"` (avec les autres includes client) et, dans la classe `Engine`, près des autres membres monde, ajouter :

```cpp
	engine::world::BuildingCollisionCatalog m_buildingCollisionCatalog;
```

- [ ] **Step 2 : Charger le catalogue au boot** — dans `Engine.cpp`, au DÉBUT de `LoadBuildings()` (juste après `if (!m_pipeline) return;`), ajouter :

```cpp
		// Catalogue de collision des pièces (boîtes fines + portes passables).
		// Absent -> catalogue vide -> fallback cylindre (rétro-compatible).
		{
			const std::string contentRoot0 = m_cfg.GetString("paths.content", "game/data");
			const std::string catPath = contentRoot0 + "/collision/building_pieces.json";
			const std::string catJson = engine::platform::FileSystem::ReadAllText(catPath);
			if (!catJson.empty() && m_buildingCollisionCatalog.LoadFromJson(catJson))
				LOG_INFO(Render, "[Buildings] catalogue collision chargé ('{}')", catPath);
			else
				LOG_DEBUG(Render, "[Buildings] pas de catalogue collision ('{}') -> fallback cylindre", catPath);
		}
```

- [ ] **Step 3 : Brancher dans `BuildPropFromMeshMatrix`** — remplacer le bloc cylindre actuel (le `engine::gameplay::PropCylinder cyl{ cx, cz, radius, minY, maxY }; … AddCylinder(cyl);`) par :

```cpp
				// Basename du mesh (sans dossier ni extension) pour le catalogue.
				std::string meshBase = meshPath;
				{ const auto sl = meshBase.find_last_of("/\\"); if (sl != std::string::npos) meshBase = meshBase.substr(sl + 1);
				  const auto dot = meshBase.find_last_of('.'); if (dot != std::string::npos) meshBase = meshBase.substr(0, dot); }

				const auto* piece = m_buildingCollisionCatalog.Lookup(meshBase);
				const bool isStair = (meshLower.find("escalier") != std::string::npos)
					|| (meshLower.find("stair") != std::string::npos);
				if (piece == nullptr)
				{
					// Fallback : comportement actuel (cylindre englobant), rétro-compatible.
					engine::gameplay::PropCylinder cyl{ cx, cz, radius, minY, maxY };
					cyl.passable = meshLower.find("door") != std::string::npos;
					cyl.stair = isStair;
					cyl.wall = !cyl.stair;
					m_worldCollider.AddCylinder(cyl);
				}
				else if (!piece->passable)
				{
					// Boîtes du catalogue : transformer chaque boîte LOCALE par la
					// matrice monde de la pièce (worldM). Colonnes 3x3 = axes+échelle.
					const float* M = worldM.m; // column-major M[col*4+row]
					const engine::math::Vec3 colX{ M[0], M[1], M[2] };
					const engine::math::Vec3 colY{ M[4], M[5], M[6] };
					const engine::math::Vec3 colZ{ M[8], M[9], M[10] };
					const float sX = std::sqrt(colX.x*colX.x + colX.y*colX.y + colX.z*colX.z);
					const float sY = std::sqrt(colY.x*colY.x + colY.y*colY.y + colY.z*colY.z);
					const float sZ = std::sqrt(colZ.x*colZ.x + colZ.y*colZ.y + colZ.z*colZ.z);
					for (const auto& lb : piece->boxes)
					{
						// Centre local -> monde.
						const float wx = M[0]*lb.cx + M[4]*lb.cy + M[8]*lb.cz  + M[12];
						const float wy = M[1]*lb.cx + M[5]*lb.cy + M[9]*lb.cz  + M[13];
						const float wz = M[2]*lb.cx + M[6]*lb.cy + M[10]*lb.cz + M[14];
						engine::gameplay::PropBox box;
						box.cx = wx; box.cz = wz;
						box.halfX = lb.hx * sX; box.halfZ = lb.hz * sZ;
						box.axisX = (sX > 1e-6f) ? engine::math::Vec3{ colX.x/sX, 0.0f, colX.z/sX } : engine::math::Vec3{ 1,0,0 };
						box.axisZ = (sZ > 1e-6f) ? engine::math::Vec3{ colZ.x/sZ, 0.0f, colZ.z/sZ } : engine::math::Vec3{ 0,0,1 };
						box.loY = wy - lb.hy * sY; box.hiY = wy + lb.hy * sY;
						box.stair = isStair; box.wall = !isStair;
						m_worldCollider.AddBox(box);
					}
				}
				// piece->passable : on n'ajoute rien (battant franchissable).
```

  *(Remarque : `worldM` est la matrice passée à `BuildPropFromMeshMatrix` ; vérifier son nom exact de paramètre dans la signature et l'utiliser. `meshLower` est déjà calculé juste au-dessus dans le bloc existant — le conserver.)*

- [ ] **Step 4 : Lier `BuildingCollisionCatalog.cpp` à la cible client** — dans `src/CMakeLists.txt`, repérer la liste des sources de la cible client (celle qui contient `Engine.cpp`) et y ajouter `${CMAKE_SOURCE_DIR}/src/client/world/BuildingCollisionCatalog.cpp` (chercher `app/Engine.cpp` dans le fichier pour localiser la liste). *(S'il est déjà dans `engine_core` via un glob, vérifier qu'il n'est pas ajouté en double.)*

- [ ] **Step 5 : Lancer** — pousser ; `build-linux` compile (la cible client + tous les tests). Pas de test ctest direct ici (intégration moteur, non instanciable hors GPU) ; la non-régression est couverte par les tests collider/catalogue. Vérifier que `build-windows` compile aussi.

- [ ] **Step 6 : Commit**

```bash
git add src/client/app/Engine.h src/client/app/Engine.cpp src/CMakeLists.txt
git commit -m "feat(app): BuildPropFromMeshMatrix consulte le catalogue (boites) sinon cylindre"
```

---

## Task 5 : Données catalogue pour les meshes de l'auberge

**Files:**
- Create: `game/data/collision/building_pieces.json`

Les dimensions viennent des bounds réels des glTF de l'auberge. Les pièces clés (vues dans `game/data/meshes/props/`) : `Wall_Plaster_Straight`, `Wall_Plaster_Straight_L/R`, `Wall_Arch`, `Wall_Plaster_Door_Flat/Round`, `Door_1..4_Flat/Round`, `DoorFrame_*`.

- [ ] **Step 1 : Mesurer les bounds** — pour CHAQUE mesh listé ci-dessus, lire le bounding box XZ + hauteur. Méthode (sans toolchain) : ouvrir le `.gltf` et lire les `min`/`max` de l'accessor de l'attribut `POSITION` (sous `meshes[].primitives[].attributes.POSITION` → `accessors[k].min/max` = `[x,y,z]`). Si absents, repli : convention bâtiment standard (mur ~2.0 m large × 3.0 m haut × 0.2 m épais ; ouverture de porte ~1.1 m large × 2.1 m haut, centrée).

- [ ] **Step 2 : Écrire le catalogue** — créer `game/data/collision/building_pieces.json`. Valeurs de départ (à ajuster selon les bounds mesurés au Step 1 ; ces valeurs supposent un mur centré sur son origine, large en X, fin en Z) :

```json
{
  "version": 1,
  "pieces": {
    "door_1_flat":  { "passable": true },
    "door_1_round": { "passable": true },
    "door_2_flat":  { "passable": true },
    "door_2_round": { "passable": true },
    "door_4_flat":  { "passable": true },
    "door_4_round": { "passable": true },
    "doorframe_flat_brick":    { "passable": true },
    "doorframe_flat_wooddark": { "passable": true },
    "doorframe_round_brick":   { "passable": true },
    "doorframe_round_wooddark":{ "passable": true },

    "wall_plaster_straight": {
      "box_count": 1,
      "box_0": { "cx": 0.0, "cy": 1.5, "cz": 0.0, "hx": 1.0, "hy": 1.5, "hz": 0.12 }
    },
    "wall_plaster_straight_base": {
      "box_count": 1,
      "box_0": { "cx": 0.0, "cy": 1.5, "cz": 0.0, "hx": 1.0, "hy": 1.5, "hz": 0.12 }
    },
    "wall_plaster_straight_l": {
      "box_count": 1,
      "box_0": { "cx": 0.0, "cy": 1.5, "cz": 0.0, "hx": 1.0, "hy": 1.5, "hz": 0.12 }
    },
    "wall_plaster_straight_r": {
      "box_count": 1,
      "box_0": { "cx": 0.0, "cy": 1.5, "cz": 0.0, "hx": 1.0, "hy": 1.5, "hz": 0.12 }
    },
    "wall_arch": {
      "box_count": 1,
      "box_0": { "cx": 0.0, "cy": 1.5, "cz": 0.0, "hx": 1.0, "hy": 1.5, "hz": 0.12 }
    },

    "wall_plaster_door_flat": {
      "box_count": 3,
      "box_0": { "cx": -0.78, "cy": 1.5,  "cz": 0.0, "hx": 0.22, "hy": 1.5,  "hz": 0.12 },
      "box_1": { "cx":  0.78, "cy": 1.5,  "cz": 0.0, "hx": 0.22, "hy": 1.5,  "hz": 0.12 },
      "box_2": { "cx":  0.0,  "cy": 2.55, "cz": 0.0, "hx": 1.0,  "hy": 0.45, "hz": 0.12 }
    },
    "wall_plaster_door_round": {
      "box_count": 3,
      "box_0": { "cx": -0.78, "cy": 1.5,  "cz": 0.0, "hx": 0.22, "hy": 1.5,  "hz": 0.12 },
      "box_1": { "cx":  0.78, "cy": 1.5,  "cz": 0.0, "hx": 0.22, "hy": 1.5,  "hz": 0.12 },
      "box_2": { "cx":  0.0,  "cy": 2.55, "cz": 0.0, "hx": 1.0,  "hy": 0.45, "hz": 0.12 }
    },
    "wall_plaster_door_roundinset": {
      "box_count": 3,
      "box_0": { "cx": -0.78, "cy": 1.5,  "cz": 0.0, "hx": 0.22, "hy": 1.5,  "hz": 0.12 },
      "box_1": { "cx":  0.78, "cy": 1.5,  "cz": 0.0, "hx": 0.22, "hy": 1.5,  "hz": 0.12 },
      "box_2": { "cx":  0.0,  "cy": 2.55, "cz": 0.0, "hx": 1.0,  "hy": 0.45, "hz": 0.12 }
    }
  }
}
```

- [ ] **Step 3 : Valider la syntaxe JSON** — le fichier doit être un JSON valide (membres séparés par des virgules, pas de virgule traînante). Vérification : aucun parser local requis ; le chargement réel sera confirmé par le log `[Buildings] catalogue collision chargé` au prochain build en jeu.

- [ ] **Step 4 : Commit**

```bash
git add game/data/collision/building_pieces.json
git commit -m "data(collision): catalogue de boites pour les murs/portes de l'auberge"
```

---

## Validation finale (en jeu, après build CI)

Sur un build incluant ce chantier (+ #919) :
- L'auberge a des **murs fins** : on ne se cogne plus à un gros volume débordant ; on peut longer le mur de près.
- On **franchit la porte** (l'embrasure du `Wall_Plaster_Door_*` est libre, jambages + linteau seuls bloquent).
- Aucune régression sur le décor (arbres/coffres : non catalogués, restent en cylindre marchable).
- Log au boot : `[Buildings] catalogue collision chargé`.

**Déploiement** : ✅ client uniquement, pas de redéploiement serveur.
