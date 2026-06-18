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
