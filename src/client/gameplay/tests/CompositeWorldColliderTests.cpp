#include "src/client/gameplay/CompositeWorldCollider.h"
#include "src/client/gameplay/CellWorldCollider.h"

#include <cmath>
#include <cstdio>
#include <vector>

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
		// Repos attendu : pieds sur topY=3 -> centre = 3 + halfH(0.9) = 3.9 (même
		// convention que le terrain et le rendu de l'avatar).
		const float restY = 20.0f + hit.fraction * (1.0f - 20.0f);
		check(std::fabs(restY - 3.9f) < 0.05f, "dessus: repos a topY + demi-capsule");
	}

	// 4quater) Une fois POSÉ sur le dessus (pieds au niveau topY=3, centre y=3.9),
	// le déplacement horizontal n'est PLUS bloqué par le flanc (sinon le perso
	// resterait coincé en haut du mesh).
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 3.9f, 0 }, Vec3{ 10, 3.9f, 0 }, hit);
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

	// B6) MUR À PORTE — 3 boîtes (jambage gauche, jambage droit, linteau) laissant
	//     une embrasure centrale libre. Le mur est à x=5, orienté face selon X.
	//     Jambages : largeur (en Z) 0.4 chacun, à z=±0.75 ; épaisseur (en X) 0.2.
	//     Linteau : au-dessus de la porte (loY=2.3), couvre toute la largeur en Z.
	//     L'embrasure = bande z ∈ [-0.35, 0.35], y < 2.3. La capsule (h=1.8, r=0.3)
	//     a un sommet effectif à y=1.9 (centre 1 + halfH 0.9) : elle passe
	//     donc SOUS le linteau à 2.3 (porte assez haute), mais bute les jambages.
	{
		auto jamb = [](float zc) {
			PropBox b; b.cx = 5.0f; b.cz = zc; b.halfX = 0.1f; b.halfZ = 0.2f;
			b.axisX = Vec3{ 1,0,0 }; b.axisZ = Vec3{ 0,0,1 }; b.loY = 0.0f; b.hiY = 3.0f;
			return b;
		};
		PropBox lintel; lintel.cx = 5.0f; lintel.cz = 0.0f; lintel.halfX = 0.1f; lintel.halfZ = 1.0f;
		lintel.axisX = Vec3{ 1,0,0 }; lintel.axisZ = Vec3{ 0,0,1 }; lintel.loY = 2.3f; lintel.hiY = 3.0f;

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

	// === Roadmap-5 (2026-07-19) : dessus MARCHABLE des boîtes (capuchon 3a) ===
	// Dalle de sol 2x2 m à y ∈ [1.0, 1.05], centrée (5,0).
	auto makeFloor = []() {
		PropBox b;
		b.cx = 5.0f; b.cz = 0.0f; b.halfX = 1.0f; b.halfZ = 1.0f;
		b.axisX = Vec3{ 1, 0, 0 }; b.axisZ = Vec3{ 0, 0, 1 };
		b.loY = 1.0f; b.hiY = 1.05f;
		b.wall = false; b.walkableTop = true;
		return b;
	};

	// B7) Descente au-dessus de la dalle : les pieds (centre - 0.9) traversent
	//     hiY=1.05 -> pose (normale verticale). Départ centre y=2.7 (pieds 1.8),
	//     arrivée y=1.2 (pieds 0.3) : tc = (1.8-1.05)/1.5 = 0.5.
	{
		CompositeWorldCollider c(&terrain); c.AddBox(makeFloor());
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 5, 2.7f, 0 }, Vec3{ 5, 1.2f, 0 }, hit);
		check(h && hit.hit, "sol: descente -> pose sur le dessus");
		check(hit.normal.y > 0.9f, "sol: normale verticale (walkable)");
		check(hit.fraction > 0.45f && hit.fraction < 0.55f, "sol: fraction plausible");
	}

	// B8) Même descente sur une boîte MUR (walkableTop=false) : PAS de pose
	//     (les murs restent des barrières latérales pures — anti « vol »).
	{
		PropBox wallBox = makeFloor();
		wallBox.wall = true; wallBox.walkableTop = false;
		CompositeWorldCollider c(&terrain); c.AddBox(wallBox);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 5, 2.7f, 0 }, Vec3{ 5, 1.2f, 0 }, hit);
		check(!h, "mur: pas de capuchon superieur (pas de pose)");
	}

	// B9) Descente HORS de l'empreinte (x=8, au-delà de halfX+r) : pas de pose.
	{
		CompositeWorldCollider c(&terrain); c.AddBox(makeFloor());
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 8, 2.7f, 0 }, Vec3{ 8, 1.2f, 0 }, hit);
		check(!h, "sol: hors empreinte -> pas de pose");
	}

	// B10) Marche d'escalier (stair=true) : le flag stair se propage au hit
	//      du capuchon (TryStepUp montera via maxClimb).
	{
		PropBox step = makeFloor(); step.stair = true;
		CompositeWorldCollider c(&terrain); c.AddBox(step);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 5, 2.7f, 0 }, Vec3{ 5, 1.2f, 0 }, hit);
		check(h && hit.stair, "marche: flag stair propage par le capuchon");
	}

	// B11) Capsule DÉJÀ dans un mur (pas, téléportation) : s'éloigner de la boîte
	//      n'est PAS bloqué (sinon perso figé, seule la rotation répond), s'y
	//      enfoncer l'est. Mur 2 m le long de Z, épaisseur 0.2, centré (5,0).
	{
		PropBox w; w.cx = 5.0f; w.cz = 0.0f; w.halfX = 0.1f; w.halfZ = 1.0f;
		w.axisX = Vec3{ 1, 0, 0 }; w.axisZ = Vec3{ 0, 0, 1 }; w.loY = 0.0f; w.hiY = 3.0f;
		CompositeWorldCollider c(&terrain); c.AddBox(w);
		IWorldCollider::SweepHit away;
		const bool ha = c.SweepCapsule(cap, Vec3{ 4.75f, 1.0f, 0 }, Vec3{ 3.75f, 1.0f, 0 }, away);
		check(!ha, "dans le mur: s'eloigner n'est pas bloque");
		IWorldCollider::SweepHit into;
		const bool hi = c.SweepCapsule(cap, Vec3{ 4.75f, 1.0f, 0 }, Vec3{ 5.75f, 1.0f, 0 }, into);
		check(hi && into.hit && into.fraction < 1e-4f, "dans le mur: s'enfoncer est bloque");
		check(into.normal.x < -0.9f, "dans le mur: normale vers la sortie la plus proche");
	}

	// B12) Plancher de bâtiment à fleur de terrain (dessus 1 cm au-dessus du
	//      sol y=0) : marcher dessus depuis le terrain ne le heurte PAS (pieds au
	//      sol = centre − 0.9). Régression : l'ancienne convention (pieds à
	//      centre − 1.2) en faisait une marche → pas, tête dans le linteau, blocage.
	{
		PropBox fl = makeFloor(); fl.loY = -0.01f; fl.hiY = 0.01f;
		CompositeWorldCollider c(&terrain); c.AddBox(fl);
		IWorldCollider::SweepHit hit;
		const bool h = c.SweepCapsule(cap, Vec3{ 2.0f, 0.9f, 0 }, Vec3{ 5.0f, 0.9f, 0 }, hit);
		check(!h, "plancher a fleur: pas de blocage lateral");
	}

	// 6) Index spatial (monde ouvert) : un collisionneur peuplé de centaines de
	//    cylindres et de boîtes doit rendre EXACTEMENT le hit le plus proche que
	//    donnerait chaque primitive testée isolément (référence brute force).
	{
		uint32_t seed = 12345u;
		auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) * (1.0f / 16777216.0f); };
		CompositeWorldCollider all(&terrain);
		std::vector<PropCylinder> cyls;
		std::vector<PropBox> boxes;
		for (int i = 0; i < 400; ++i)
		{
			PropCylinder cy{ rnd() * 200.0f - 100.0f, rnd() * 200.0f - 100.0f, 0.3f + rnd() * 1.5f, 0.0f, 2.0f + rnd() * 6.0f };
			cy.wall = (i % 3) == 0;
			cyls.push_back(cy);
			all.AddCylinder(cy);
		}
		for (int i = 0; i < 300; ++i)
		{
			PropBox bx;
			bx.cx = rnd() * 200.0f - 100.0f; bx.cz = rnd() * 200.0f - 100.0f;
			bx.halfX = 0.2f + rnd() * 3.0f; bx.halfZ = 0.1f + rnd() * 0.6f;
			const float a = rnd() * 6.2831853f;
			bx.axisX = Vec3{ std::cos(a), 0.0f, std::sin(a) };
			bx.axisZ = Vec3{ -std::sin(a), 0.0f, std::cos(a) };
			bx.loY = 0.0f; bx.hiY = 3.0f; bx.walkableTop = (i % 4) == 0; bx.wall = !bx.walkableTop;
			boxes.push_back(bx);
			all.AddBox(bx);
		}
		int mismatches = 0;
		for (int k = 0; k < 600; ++k)
		{
			const Vec3 st{ rnd() * 200.0f - 100.0f, 0.5f + rnd() * 5.0f, rnd() * 200.0f - 100.0f };
			const Vec3 en{ st.x + (rnd() - 0.5f) * 12.0f, st.y + (rnd() - 0.6f) * 3.0f, st.z + (rnd() - 0.5f) * 12.0f };
			IWorldCollider::SweepHit hAll;
			const bool gotAll = all.SweepCapsule(cap, st, en, hAll);
			float bestF = 2.0f;
			for (const auto& cy : cyls)
			{
				CompositeWorldCollider one(&terrain); one.AddCylinder(cy);
				IWorldCollider::SweepHit h;
				if (one.SweepCapsule(cap, st, en, h) && h.fraction < bestF) bestF = h.fraction;
			}
			for (const auto& bx : boxes)
			{
				CompositeWorldCollider one(&terrain); one.AddBox(bx);
				IWorldCollider::SweepHit h;
				if (one.SweepCapsule(cap, st, en, h) && h.fraction < bestF) bestF = h.fraction;
			}
			const bool gotRef = bestF <= 1.0f;
			if (gotAll != gotRef || (gotAll && std::fabs(hAll.fraction - bestF) > 1e-5f)) ++mismatches;
		}
		check(mismatches == 0, "index spatial: resultats identiques a la reference brute force");
		// Ajout apres requete : l'index doit etre reconstruit (pas de cache perime).
		CompositeWorldCollider c(&terrain);
		IWorldCollider::SweepHit h0;
		check(!c.SweepCapsule(cap, Vec3{ 300, 1, 0 }, Vec3{ 310, 1, 0 }, h0), "index: vide -> pas de hit");
		c.AddCylinder(PropCylinder{ 305.0f, 0.0f, 0.5f, 0.0f, 3.0f });
		check(c.SweepCapsule(cap, Vec3{ 300, 1, 0 }, Vec3{ 310, 1, 0 }, h0), "index: reconstruit apres AddCylinder");
	}

	// 5) QueryWater délégué au terrain.
	{
		CompositeWorldCollider c(&terrain);
		IWorldCollider::WaterQuery wq;
		bool inW = c.QueryWater(Vec3{ 0, 0, 0 }, wq);
		check(inW && wq.inWater && std::fabs(wq.surfaceY - 42.0f) < 1e-3f, "querywater: delegation");
	}

	// 6) Translate (rebase de l'origine de rendu) : la primitive suit le décalage.
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit h0;
		check(c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, h0), "translate: hit avant");
		c.Translate(-10000.0f, 0.0f);
		IWorldCollider::SweepHit h1;
		check(!c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, h1), "translate: plus de hit a l'ancienne place");
		IWorldCollider::SweepHit h2;
		check(c.SweepCapsule(cap, Vec3{ -10000, 1, 0 }, Vec3{ -9990, 1, 0 }, h2), "translate: hit a la nouvelle place");
	}

	// 7) CellWorldCollider : primitives LOCALES à une cellule décalée de +10 km en X.
	{
		CompositeWorldCollider cellLocal(nullptr);
		cellLocal.AddCylinder(cyl); // (5, 0) local
		CompositeWorldCollider base(&terrain);
		CellWorldCollider world;
		world.SetBase(&base);
		world.SetCells({ CellWorldCollider::CellEntry{ &cellLocal, 10000.0f, 0.0f } });
		IWorldCollider::SweepHit h;
		check(world.SweepCapsule(cap, Vec3{ 10000, 1, 0 }, Vec3{ 10010, 1, 0 }, h) && h.hit, "cellule: hit a 10 km");
		check(h.fraction > 0.3f && h.fraction < 0.55f, "cellule: meme fraction qu'en local");
		IWorldCollider::SweepHit miss;
		check(!world.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, miss), "cellule: rien a l'origine");
		IWorldCollider::WaterQuery wq;
		check(world.QueryWater(Vec3{ 0, 0, 0 }, wq) && wq.inWater, "cellule: eau deleguee a la base");
	}

	if (g_fail == 0) std::printf("CompositeWorldColliderTests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
