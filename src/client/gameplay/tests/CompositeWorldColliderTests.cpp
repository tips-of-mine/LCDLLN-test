#include "src/client/gameplay/CompositeWorldCollider.h"

#include <cmath>
#include <cstdio>

using namespace engine::gameplay;
using engine::math::Vec3;

namespace
{
	// Terrain factice : ne collisionne jamais ; QueryWater renvoie inWater=true (pour
	// vérifier la délégation).
	class FakeTerrain final : public IWorldCollider
	{
	public:
		bool SweepCapsule(const Capsule&, const Vec3&, const Vec3&, SweepHit& out) const override
		{ out = SweepHit{}; return false; }
		bool QueryWater(const Vec3&, WaterQuery& out) const override
		{ out.inWater = true; out.surfaceY = 42.0f; return true; }
	};

	int g_fail = 0;
	void check(bool cond, const char* msg)
	{ if (!cond) { std::printf("FAIL: %s\n", msg); ++g_fail; } }
}

int main()
{
	FakeTerrain terrain;
	IWorldCollider::Capsule cap; cap.radius = 0.3f; cap.height = 1.8f;

	// Cylindre centré en (5,0), rayon 0.5, de Y=0 à Y=3.
	const PropCylinder cyl{ 5.0f, 0.0f, 0.5f, 0.0f, 3.0f };

	// 1) Capsule traversant le cylindre : doit être arrêtée, normale horizontale.
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, hit);
		check(h && hit.hit, "traversee: hit attendu");
		check(hit.fraction < 1.0f, "traversee: fraction < 1");
		check(std::fabs(hit.normal.y) < 1e-3f, "traversee: normale horizontale");
		// Contact attendu vers x ~ 5 - (0.3+0.5) = 4.2 -> fraction ~0.42.
		check(hit.fraction > 0.3f && hit.fraction < 0.55f, "traversee: fraction plausible");
	}

	// 2) Capsule passant à côté (z=5) : pas de hit.
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 5 }, Vec3{ 10, 1, 5 }, hit);
		check(!h && !hit.hit, "a_cote: pas de hit");
	}

	// 3) Capsule au-dessus du cylindre (Y=10) : pas de hit (hors [baseY, topY]).
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 10, 0 }, Vec3{ 10, 10, 0 }, hit);
		check(!h, "au_dessus: pas de hit");
	}

	// 4) Sans cylindre : comportement = terrain seul (ici pas de hit).
	{
		CompositeWorldCollider c(&terrain);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, hit);
		check(!h, "sans_cylindre: pas de hit");
	}

	// 4bis) MARCHABLE — un sweep DESCENDANT au-dessus de l'empreinte du cylindre se
	// POSE sur le sommet (topY=3) avec une normale verticale. C'est le comportement
	// voulu (« avancer sur les meshes comme sur le sol ») qui remplace l'ancien
	// "anti-teleport" : le perso se tient désormais sur le dessus des props.
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 5, 20, 0 }, Vec3{ 5, 1, 0 }, hit);
		check(h && hit.hit, "dessus: se pose sur le sommet (hit)");
		check(hit.normal.y > 0.99f, "dessus: normale verticale (marchable)");
		// Repos attendu : bas de la capsule sur topY=3 -> centre = 3 + halfH(0.9) + r(0.3) = 4.2.
		const float restY = 20.0f + hit.fraction * (1.0f - 20.0f);
		check(std::fabs(restY - 4.2f) < 0.05f, "dessus: repos a topY + demi-capsule");
	}

	// 4quater) Une fois POSÉ sur le dessus (pieds au niveau topY=3, centre y=4.2),
	// le déplacement horizontal n'est PLUS bloqué par le flanc (sinon le perso
	// resterait coincé en haut du mesh).
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 4.2f, 0 }, Vec3{ 10, 4.2f, 0 }, hit);
		check(!h, "sur le dessus: avance librement (pas de blocage de flanc)");
	}

	// 4ter) Déjà en chevauchement, mouvement s'ÉLOIGNANT de l'axe : pas de hit
	// (le perso doit pouvoir ressortir / glisser, pas rester collé).
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		// Départ au bord du cylindre (x=5.5 ~ R=0.8 de l'axe), on s'éloigne vers +x.
		bool h = c.SweepCapsule(cap, Vec3{ 5.5f, 1, 0 }, Vec3{ 7.0f, 1, 0 }, hit);
		check(!h, "chevauchement + eloignement: pas de hit (peut ressortir)");
	}

	// 6) PORTE (passable) — le cylindre n'oppose AUCUNE collision : un sweep qui
	//    le traverserait normalement (cf. test 1) passe librement (on franchit
	//    l'embrasure). Idem en descente : pas de capuchon.
	{
		PropCylinder door = cyl; door.passable = true;
		CompositeWorldCollider c(&terrain); c.AddCylinder(door);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, hit);
		check(!h && !hit.hit, "porte: passable -> aucune collision (franchissable)");
		IWorldCollider::SweepHit hitDown;
		bool hd = c.SweepCapsule(cap, Vec3{ 5, 20, 0 }, Vec3{ 5, 1, 0 }, hitDown);
		check(!hd, "porte: passable -> pas de capuchon non plus");
	}

	// 7) ESCALIER (stair) — collisionne toujours (flanc + capuchon), MAIS le hit est
	//    marqué stair=true afin que le contrôleur autorise un step-up jusqu'à maxClimb
	//    (montée d'escalier), au lieu du plafond maxStep réservé aux murs.
	{
		PropCylinder st = cyl; st.stair = true;
		CompositeWorldCollider c(&terrain); c.AddCylinder(st);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, hit);
		check(h && hit.hit, "escalier: flanc bloque (hit attendu)");
		check(hit.stair, "escalier: hit marque stair=true (flanc)");
		IWorldCollider::SweepHit hitTop;
		c.SweepCapsule(cap, Vec3{ 5, 20, 0 }, Vec3{ 5, 1, 0 }, hitTop);
		check(hitTop.stair, "escalier: hit marque stair=true (capuchon)");
	}

	// 8) Prop normal (mur/bâtiment) — hit.stair reste false : le contrôleur ne
	//    déclenchera PAS la montée haute (anti-« vol » le long des parois).
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, hit);
		check(h && !hit.stair, "mur normal: hit.stair=false");
	}

	// 9) MUR de bâtiment (wall) — PAS de dessus marchable : un sweep DESCENDANT
	//    au-dessus de l'empreinte ne s'accroche PAS au sommet (contrairement au
	//    prop normal, cf. test 4bis). C'est ce qui tue le « vol » : la sonde
	//    anti-encastrement du contrôleur ne peut plus remonter le perso au sommet
	//    du mur. Le flanc, lui, bloque toujours horizontalement.
	{
		PropCylinder wall = cyl; wall.wall = true;
		CompositeWorldCollider c(&terrain); c.AddCylinder(wall);
		// Descente au-dessus du disque : AUCUN capuchon (le prop normal, lui, posait).
		IWorldCollider::SweepHit down;
		bool hd = c.SweepCapsule(cap, Vec3{ 5, 20, 0 }, Vec3{ 5, 1, 0 }, down);
		check(!hd, "mur: pas de dessus marchable (sweep descendant ne s'accroche pas)");
		// Flanc toujours bloquant : entrée horizontale arrêtée, normale horizontale.
		IWorldCollider::SweepHit side;
		bool hs = c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, side);
		check(hs && side.hit, "mur: flanc bloque toujours (hit)");
		check(std::fabs(side.normal.y) < 1e-3f, "mur: normale de flanc horizontale");
	}

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

	// 5) QueryWater délégué au terrain.
	{
		CompositeWorldCollider c(&terrain);
		IWorldCollider::WaterQuery wq;
		bool inW = c.QueryWater(Vec3{ 0, 0, 0 }, wq);
		check(inW && wq.inWater && std::fabs(wq.surfaceY - 42.0f) < 1e-3f, "querywater: delegation");
	}

	if (g_fail == 0) std::printf("CompositeWorldColliderTests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
